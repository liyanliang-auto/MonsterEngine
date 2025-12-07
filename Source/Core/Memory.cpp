#include "Core/CoreMinimal.h"
#include "Core/Memory.h"
#include <malloc.h>
#include <new>
#include <algorithm>

// Platform-specific includes for huge pages
#if PLATFORM_WINDOWS
	#include <Windows.h>
#elif PLATFORM_LINUX
	#include <sys/mman.h>
	#include <unistd.h>
	#include <fcntl.h>
#endif

namespace MonsterRender {

	static constexpr size_t kSmallPageSize = 64ull * 1024ull; // 64KB pages for small bins
	static constexpr uint32 kEmptyPageThreshold = 4;  // Trim if this many empty pages exist

	static uint8* alignPtr(uint8* ptr, size_t alignment) {
		const size_t mask = alignment - 1;
		return reinterpret_cast<uint8*>((reinterpret_cast<size_t>(ptr) + mask) & ~mask);
	}

	// Thread-local cache (initialized lazily per thread)
	thread_local MemorySystem::ThreadLocalCache* MemorySystem::t_tlsCache = nullptr;

	MemorySystem& MemorySystem::get() {
		static MemorySystem instance;
		return instance;
	}

	bool MemorySystem::initialize(uint64 frameScratchSizeBytes, uint64 texturePoolBlockSizeBytes) {
		// Detect huge pages support
		m_hugePagesAvailable = detectHugePagesSupport();
		if (m_hugePagesAvailable) {
			MR_LOG_INFO("Huge Pages (2MB) are available on this system");
		} else {
			MR_LOG_INFO("Huge Pages not available, using standard page allocation");
		}

		// Initialize small bins: 16,32,64,128,256,512,1024
		uint32 size = 16;
		for (uint32 i = 0; i < kNumSmallBins; ++i) {
			m_smallBins[i].elementSize = size;
			size <<= 1;
		}

		// Frame scratch
		m_frameScratch.capacity = frameScratchSizeBytes;
		m_frameScratch.buffer = MakeUnique<uint8[]>(static_cast<size_t>(frameScratchSizeBytes));
		m_frameScratch.offset.store(0, std::memory_order_relaxed);
		m_frameScratch.peak.store(0, std::memory_order_relaxed);
		m_frameScratch.allocations.store(0, std::memory_order_relaxed);

		// Texture pool
		m_textureBlockSize = texturePoolBlockSizeBytes;
		m_textureBlocks.clear();
		m_textureReservedBytes.store(0, std::memory_order_relaxed);
		m_textureUsedBytes.store(0, std::memory_order_relaxed);

		// Reset statistics
		m_smallAllocatedBytes.store(0, std::memory_order_relaxed);
		m_smallReservedBytes.store(0, std::memory_order_relaxed);
		m_smallCacheHits.store(0, std::memory_order_relaxed);
		m_smallCacheMisses.store(0, std::memory_order_relaxed);

		MR_LOG_INFO("MemorySystem initialized: frameScratch=" + std::to_string(frameScratchSizeBytes / 1024 / 1024) + "MB, textureBlock=" + std::to_string(texturePoolBlockSizeBytes / 1024 / 1024) + "MB");
		return true;
	}

	void MemorySystem::shutdown() {
		// Release small bin pages (per-bin locks)
		for (uint32 i = 0; i < kNumSmallBins; ++i) {
			auto& bin = m_smallBins[i];
			std::scoped_lock lock(bin.mutex);
			for (auto* page : bin.pages) {
				::operator delete[](page, std::nothrow);
			}
			bin.pages.clear();
		}
		m_smallAllocatedBytes.store(0, std::memory_order_relaxed);
		m_smallReservedBytes.store(0, std::memory_order_relaxed);

		// Frame scratch
		m_frameScratch.buffer.reset();
		m_frameScratch.capacity = 0;
		m_frameScratch.offset.store(0, std::memory_order_relaxed);
		m_frameScratch.peak.store(0, std::memory_order_relaxed);

		// Texture pool
		{
			std::scoped_lock lock(m_textureBlocksMutex);
			for (auto& block : m_textureBlocks) {
				// Free all free-list regions
				TextureFreeRegion* region = block->freeList;
				while (region) {
					TextureFreeRegion* next = region->next;
					::free(region);
					region = next;
				}
				// Free huge pages if used
				if (block->usesHugePages && block->rawHugePagePtr) {
					freeHugePages(block->rawHugePagePtr, static_cast<size_t>(block->capacity));
					block->rawHugePagePtr = nullptr;
				}
			}
			m_textureBlocks.clear();
		}
		m_textureReservedBytes.store(0, std::memory_order_relaxed);
		m_textureUsedBytes.store(0, std::memory_order_relaxed);
	}

	uint32 MemorySystem::selectSmallBin(size_t size) {
		if (size <= 16) return 0;
		if (size <= 32) return 1;
		if (size <= 64) return 2;
		if (size <= 128) return 3;
		if (size <= 256) return 4;
		if (size <= 512) return 5;
		return 6; // up to 1024
	}

	size_t MemorySystem::alignUp(size_t value, size_t alignment) {
		const size_t mask = alignment - 1;
		return (value + mask) & ~mask;
	}

	MemorySystem::SmallBinPage* MemorySystem::allocateSmallPage(uint32 elementSize) {
		// Allocate a page buffer large enough to hold header + elements region
		const size_t rawSize = kSmallPageSize;
		auto* raw = reinterpret_cast<uint8*>(::operator new[](rawSize, std::nothrow));
		if (!raw) return nullptr;

		// Place header at start
		auto* page = reinterpret_cast<SmallBinPage*>(raw);
		page->header.parentBin = nullptr;
		page->header.elementSize = elementSize;
		page->header.freeList = nullptr;

		// Compute element region start aligned to element size
		uint8* regionStart = alignPtr(raw + sizeof(SmallBinPageHeader), elementSize);
		const size_t usable = rawSize - static_cast<size_t>(regionStart - raw);
		const uint32 count = static_cast<uint32>(usable / elementSize);

		page->header.elementCount = count;
		page->header.freeCount = count;

		// Initialize free list in-place (LIFO)
		uint8* current = regionStart;
		void* prev = nullptr;
		for (uint32 i = 0; i < count; ++i) {
			*reinterpret_cast<void**>(current) = prev;
			prev = current;
			current += elementSize;
		}
		page->header.freeList = prev;
		return page;
	}

	// TLS cache management
	MemorySystem::ThreadLocalCache* MemorySystem::getTLSCache() {
		if (!t_tlsCache) {
			// Use system malloc for internal structures to avoid circular dependency
			t_tlsCache = static_cast<ThreadLocalCache*>(::malloc(sizeof(ThreadLocalCache)));
			new(t_tlsCache) ThreadLocalCache();
			// Zero-initialize cache
			for (uint32 i = 0; i < kNumSmallBins; ++i) {
				t_tlsCache->count[i] = 0;
				for (uint32 j = 0; j < ThreadLocalCache::kCacheSize; ++j) {
					t_tlsCache->cache[i][j] = nullptr;
				}
			}
		}
		return t_tlsCache;
	}

	void MemorySystem::releaseTLSCache(ThreadLocalCache* cache) {
		if (!cache) return;
		// Flush all cached items back to bins
		for (uint32 binIdx = 0; binIdx < kNumSmallBins; ++binIdx) {
			auto& bin = m_smallBins[binIdx];
			std::scoped_lock lock(bin.mutex);
			for (uint32 i = 0; i < cache->count[binIdx]; ++i) {
				void* ptr = cache->cache[binIdx][i];
				// Return to bin (simplified - just add to first page's freelist)
				if (!bin.pages.empty()) {
					auto* page = bin.pages[0];
					*reinterpret_cast<void**>(ptr) = page->header.freeList;
					page->header.freeList = ptr;
					++page->header.freeCount;
				}
			}
		}
		cache->~ThreadLocalCache();
		::free(cache);
	}

	void* MemorySystem::allocateFromBin(SmallBin& bin, size_t alignment, ThreadLocalCache* tlsCache) {
		const uint32 binIdx = selectSmallBin(bin.elementSize);
		// Try TLS cache first
		if (tlsCache && tlsCache->count[binIdx] > 0) {
			--tlsCache->count[binIdx];
			void* p = tlsCache->cache[binIdx][tlsCache->count[binIdx]];
			++tlsCache->hits;
			m_smallCacheHits.fetch_add(1, std::memory_order_relaxed);
			m_smallAllocatedBytes.fetch_add(bin.elementSize, std::memory_order_relaxed);
			bin.allocCount.fetch_add(1, std::memory_order_relaxed);
			return p;
		}

		// Cache miss - acquire lock and allocate from bin
		++tlsCache->misses;
		m_smallCacheMisses.fetch_add(1, std::memory_order_relaxed);
		std::scoped_lock lock(bin.mutex);

		// Find a page with free slots
		for (auto* page : bin.pages) {
			if (page->header.freeCount > 0) {
				void* p = page->header.freeList;
				page->header.freeList = *reinterpret_cast<void**>(p);
				--page->header.freeCount;
				m_smallAllocatedBytes.fetch_add(bin.elementSize, std::memory_order_relaxed);
				bin.allocCount.fetch_add(1, std::memory_order_relaxed);
				return p;
			}
		}

		// Allocate new page
		SmallBinPage* newPage = allocateSmallPage(bin.elementSize);
		if (!newPage) return nullptr;
		newPage->header.parentBin = &bin;
		bin.pages.push_back(newPage);
		m_smallReservedBytes.fetch_add(kSmallPageSize, std::memory_order_relaxed);

		// Pop one
		void* p = newPage->header.freeList;
		newPage->header.freeList = *reinterpret_cast<void**>(p);
		--newPage->header.freeCount;
		m_smallAllocatedBytes.fetch_add(bin.elementSize, std::memory_order_relaxed);
		bin.allocCount.fetch_add(1, std::memory_order_relaxed);
		return p;
	}

	void MemorySystem::freeToBin(SmallBin& bin, void* ptr, ThreadLocalCache* tlsCache) {
		const uint32 binIdx = selectSmallBin(bin.elementSize);
		// Try to add to TLS cache first
		if (tlsCache && tlsCache->count[binIdx] < ThreadLocalCache::kCacheSize) {
			tlsCache->cache[binIdx][tlsCache->count[binIdx]] = ptr;
			++tlsCache->count[binIdx];
			m_smallAllocatedBytes.fetch_sub(bin.elementSize, std::memory_order_relaxed);
			bin.freeCount.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		// Cache full - return to bin
		std::scoped_lock lock(bin.mutex);
		// Find the page that owns ptr (linear scan)
		for (auto* page : bin.pages) {
			uint8* base = reinterpret_cast<uint8*>(page);
			uint8* regionStart = alignPtr(base + sizeof(SmallBinPageHeader), bin.elementSize);
			uint8* regionEnd = regionStart + page->header.elementCount * bin.elementSize;
			if (reinterpret_cast<uint8*>(ptr) >= regionStart && reinterpret_cast<uint8*>(ptr) < regionEnd) {
				*reinterpret_cast<void**>(ptr) = page->header.freeList;
				page->header.freeList = ptr;
				++page->header.freeCount;
				m_smallAllocatedBytes.fetch_sub(bin.elementSize, std::memory_order_relaxed);
				bin.freeCount.fetch_add(1, std::memory_order_relaxed);
				return;
			}
		}
		MR_LOG_WARNING("freeToBin: pointer not found in bin page range");
	}

	void* MemorySystem::allocateSmall(size_t size, size_t alignment) {
		if (size == 0) size = 1;
		const uint32 binIndex = selectSmallBin(size);
		ThreadLocalCache* tlsCache = getTLSCache();
		auto& bin = m_smallBins[binIndex];
		return allocateFromBin(bin, alignment, tlsCache);
	}

	void MemorySystem::freeSmall(void* ptr, size_t size) {
		if (!ptr) return;
		const uint32 binIndex = selectSmallBin(size);
		ThreadLocalCache* tlsCache = getTLSCache();
		freeToBin(m_smallBins[binIndex], ptr, tlsCache);
	}

	void* MemorySystem::allocate(size_t size, size_t alignment) {
		if (size <= 1024) {
			return allocateSmall(size, alignment);
		}
		// Use standard malloc for debug heap compatibility
		(void)alignment; // Alignment ignored for large allocations with malloc
		return std::malloc(size);
	}

	void MemorySystem::free(void* ptr, size_t size) {
		if (!ptr) return;
		if (size <= 1024) {
			freeSmall(ptr, size);
			return;
		}
		// Use standard free for debug heap compatibility
		std::free(ptr);
	}

	void* MemorySystem::frameAllocate(size_t size, size_t alignment) {
		if (size == 0) return nullptr;
		m_frameScratch.allocations.fetch_add(1, std::memory_order_relaxed);

		// Fast path: try to bump allocate
		uint64 current = m_frameScratch.offset.load(std::memory_order_relaxed);
		while (true) {
			uint64 aligned = static_cast<uint64>(alignUp(static_cast<size_t>(current), alignment));
			uint64 next = aligned + size;
			if (next <= m_frameScratch.capacity) {
				if (m_frameScratch.offset.compare_exchange_weak(current, next, std::memory_order_acq_rel)) {
					// Update peak
					uint64 currentPeak = m_frameScratch.peak.load(std::memory_order_relaxed);
					while (next > currentPeak) {
						if (m_frameScratch.peak.compare_exchange_weak(currentPeak, next, std::memory_order_relaxed)) {
							break;
						}
					}
					return m_frameScratch.buffer.get() + aligned;
				}
				continue;
			}
			// Need to grow buffer (single-threaded growth assumption)
			uint64 newCap = std::max<uint64>(m_frameScratch.capacity * 2, alignUp(size, 4096));
			auto newBuf = MakeUnique<uint8[]>(static_cast<size_t>(newCap));
			if (!newBuf) return nullptr;
			// Swap in
			m_frameScratch.buffer.swap(newBuf);
			m_frameScratch.capacity = newCap;
			m_frameScratch.offset.store(0, std::memory_order_relaxed);
			current = 0;
		}
	}

	void MemorySystem::frameReset() {
		m_frameScratch.offset.store(0, std::memory_order_relaxed);
	}

	// Texture free-list helpers
	void* MemorySystem::allocateFromFreeList(TextureBlock& block, size_t size, size_t alignment) {
		std::scoped_lock lock(block.mutex);
		TextureFreeRegion* prev = nullptr;
		TextureFreeRegion* region = block.freeList;
		uint8* basePtr = block.usesHugePages ? block.rawHugePagePtr : block.buffer.get();
		
		while (region) {
			uint64 alignedOffset = static_cast<uint64>(alignUp(static_cast<size_t>(region->offset), alignment));
			if (alignedOffset + size <= region->offset + region->size) {
				// Found a suitable region
				void* ptr = basePtr + alignedOffset;
				// Split region if there's leftover space
				uint64 usedSize = (alignedOffset - region->offset) + size;
				if (region->size > usedSize + 64) {  // Keep region if >64B left
					region->offset += usedSize;
					region->size -= usedSize;
				} else {
					// Remove region from list
					if (prev) {
						prev->next = region->next;
					} else {
						block.freeList = region->next;
					}
					::free(region);
				}
				block.usedBytes.fetch_add(size, std::memory_order_relaxed);
				m_textureUsedBytes.fetch_add(size, std::memory_order_relaxed);
				return ptr;
			}
			prev = region;
			region = region->next;
		}
		return nullptr;
	}

	void MemorySystem::addToFreeList(TextureBlock& block, void* ptr, size_t size) {
		std::scoped_lock lock(block.mutex);
		uint8* basePtr = block.usesHugePages ? block.rawHugePagePtr : block.buffer.get();
		uint64 offset = static_cast<uint8*>(ptr) - basePtr;
		// Use system malloc for internal structures to avoid circular dependency
		auto* newRegion = static_cast<TextureFreeRegion*>(::malloc(sizeof(TextureFreeRegion)));
		newRegion->offset = offset;
		newRegion->size = size;
		newRegion->next = nullptr;
		// Insert sorted by offset
		if (!block.freeList || block.freeList->offset > offset) {
			newRegion->next = block.freeList;
			block.freeList = newRegion;
		} else {
			TextureFreeRegion* current = block.freeList;
			while (current->next && current->next->offset < offset) {
				current = current->next;
			}
			newRegion->next = current->next;
			current->next = newRegion;
		}
		block.usedBytes.fetch_sub(size, std::memory_order_relaxed);
		m_textureUsedBytes.fetch_sub(size, std::memory_order_relaxed);
	}

	void MemorySystem::mergeFreeRegions(TextureBlock& block) {
		std::scoped_lock lock(block.mutex);
		TextureFreeRegion* current = block.freeList;
		while (current && current->next) {
			// Check if current and next are adjacent
			if (current->offset + current->size == current->next->offset) {
				// Merge
				TextureFreeRegion* next = current->next;
				current->size += next->size;
				current->next = next->next;
				::free(next);
			} else {
				current = current->next;
			}
		}
	}

	void* MemorySystem::textureAllocate(size_t size, size_t alignment) {
		const size_t alignedSize = alignUp(size, alignment);
		m_textureAllocations.fetch_add(1, std::memory_order_relaxed);

		// Try to allocate from free-lists first
		for (auto& block : m_textureBlocks) {
			void* ptr = allocateFromFreeList(*block, alignedSize, alignment);
			if (ptr) return ptr;
		}

		// Try bump allocation
		for (auto& block : m_textureBlocks) {
			uint64 off = block->offset.load(std::memory_order_relaxed);
			uint64 alignedOff = static_cast<uint64>(alignUp(static_cast<size_t>(off), alignment));
			uint64 next = alignedOff + alignedSize;
			if (next <= block->capacity) {
				if (block->offset.compare_exchange_weak(off, next, std::memory_order_acq_rel)) {
					block->usedBytes.fetch_add(alignedSize, std::memory_order_relaxed);
					m_textureUsedBytes.fetch_add(alignedSize, std::memory_order_relaxed);
					uint8* basePtr = block->usesHugePages ? block->rawHugePagePtr : block->buffer.get();
					return basePtr + alignedOff;
				}
			}
		}

		// Allocate new block
		std::scoped_lock lock(m_textureBlocksMutex);
		uint64 blockSize = std::max<uint64>(m_textureBlockSize, alignedSize);
		
		// Try huge pages first if enabled and size >= 2MB
		void* hugePagePtr = nullptr;
		bool usedHugePages = false;
		if (m_useHugePagesForTextures && blockSize >= MR_HUGE_PAGE_SIZE) {
			hugePagePtr = allocateHugePages(static_cast<size_t>(blockSize));
			if (hugePagePtr) {
				usedHugePages = true;
				MR_LOG_INFO("Allocated texture block with huge pages: " + std::to_string(blockSize / 1024 / 1024) + "MB");
			}
		}

		auto block = MakeUnique<TextureBlock>();
		block->capacity = blockSize;
		block->usesHugePages = usedHugePages;
		
		if (usedHugePages) {
			// Store huge page pointer
			block->rawHugePagePtr = reinterpret_cast<uint8*>(hugePagePtr);
		} else {
			// Standard allocation
			block->buffer = MakeUnique<uint8[]>(static_cast<size_t>(blockSize));
		}
		
		block->offset.store(0, std::memory_order_relaxed);
		block->usedBytes.store(0, std::memory_order_relaxed);
		block->freeList = nullptr;
		
		void* ptr = block->usesHugePages ? block->rawHugePagePtr : block->buffer.get();
		block->offset.store(alignedSize, std::memory_order_relaxed);
		block->usedBytes.store(alignedSize, std::memory_order_relaxed);
		m_textureReservedBytes.fetch_add(blockSize, std::memory_order_relaxed);
		m_textureUsedBytes.fetch_add(alignedSize, std::memory_order_relaxed);
		m_textureBlocks.push_back(std::move(block));
		return ptr;
	}

	void MemorySystem::textureFree(void* ptr) {
		if (!ptr) return;
		m_textureFrees.fetch_add(1, std::memory_order_relaxed);
		
		// Find which block owns this pointer
		for (auto& block : m_textureBlocks) {
			uint8* basePtr = block->usesHugePages ? block->rawHugePagePtr : block->buffer.get();
			uint8* blockStart = basePtr;
			uint8* blockEnd = blockStart + block->capacity;
			if (reinterpret_cast<uint8*>(ptr) >= blockStart && reinterpret_cast<uint8*>(ptr) < blockEnd) {
				// TODO: Track allocation size for proper free
				// For now, just add to free-list with placeholder size
				MR_LOG_WARNING("textureFree: allocation size tracking not yet implemented");
				return;
			}
		}
		MR_LOG_WARNING("textureFree: pointer not found in texture blocks");
	}

	void MemorySystem::textureReleaseAll() {
		std::scoped_lock lock(m_textureBlocksMutex);
		for (auto& block : m_textureBlocks) {
			block->offset.store(0, std::memory_order_relaxed);
			block->usedBytes.store(0, std::memory_order_relaxed);
			// Free all free-list regions
			TextureFreeRegion* region = block->freeList;
			while (region) {
				TextureFreeRegion* next = region->next;
				::free(region);
				region = next;
			}
			block->freeList = nullptr;
		}
		m_textureUsedBytes.store(0, std::memory_order_relaxed);
	}

	void MemorySystem::compactTextureBlocks() {
		std::scoped_lock lock(m_textureBlocksMutex);
		for (auto& block : m_textureBlocks) {
			mergeFreeRegions(*block);
		}
	}

	void MemorySystem::trimEmptyPages() {
		// Release empty pages from each bin if threshold exceeded
		for (uint32 i = 0; i < kNumSmallBins; ++i) {
			auto& bin = m_smallBins[i];
			std::scoped_lock lock(bin.mutex);
			
			// Count empty pages
			uint32 emptyCount = 0;
			for (auto* page : bin.pages) {
				if (page->header.freeCount == page->header.elementCount) {
					++emptyCount;
				}
			}

			// If we have more than threshold, release some
			if (emptyCount > kEmptyPageThreshold) {
				TArray<SmallBinPage*> keptPages;
				uint32 releasedCount = 0;
				for (auto* page : bin.pages) {
					if (page->header.freeCount == page->header.elementCount && releasedCount < emptyCount - kEmptyPageThreshold) {
						::operator delete[](page, std::nothrow);
						m_smallReservedBytes.fetch_sub(kSmallPageSize, std::memory_order_relaxed);
						++releasedCount;
					} else {
						keptPages.push_back(page);
					}
				}
				bin.pages = std::move(keptPages);
				if (releasedCount > 0) {
					MR_LOG_INFO("Trimmed " + std::to_string(releasedCount) + " empty pages from bin " + std::to_string(i));
				}
			}
		}
	}

	MemorySystem::MemoryStats MemorySystem::getStats() const {
		MemoryStats stats;
		
		// Small bins
		stats.smallAllocatedBytes = m_smallAllocatedBytes.load(std::memory_order_relaxed);
		stats.smallReservedBytes = m_smallReservedBytes.load(std::memory_order_relaxed);
		stats.smallCacheHits = m_smallCacheHits.load(std::memory_order_relaxed);
		stats.smallCacheMisses = m_smallCacheMisses.load(std::memory_order_relaxed);
		
		// Count pages and empty pages
		uint64 totalPages = 0;
		uint64 emptyPages = 0;
		uint64 totalAllocs = 0;
		uint64 totalFrees = 0;
		for (uint32 i = 0; i < kNumSmallBins; ++i) {
			const auto& bin = m_smallBins[i];
			totalPages += bin.pages.size();
			totalAllocs += bin.allocCount.load(std::memory_order_relaxed);
			totalFrees += bin.freeCount.load(std::memory_order_relaxed);
			for (const auto* page : bin.pages) {
				if (page->header.freeCount == page->header.elementCount) {
					++emptyPages;
				}
			}
		}
		stats.smallPageCount = totalPages;
		stats.smallEmptyPageCount = emptyPages;
		stats.smallAllocations = totalAllocs;
		stats.smallFrees = totalFrees;

		// Frame scratch
		stats.frameAllocatedBytes = m_frameScratch.offset.load(std::memory_order_relaxed);
		stats.frameCapacityBytes = m_frameScratch.capacity;
		stats.framePeakBytes = m_frameScratch.peak.load(std::memory_order_relaxed);
		stats.frameAllocations = m_frameScratch.allocations.load(std::memory_order_relaxed);

		// Texture pool
		stats.textureReservedBytes = m_textureReservedBytes.load(std::memory_order_relaxed);
		stats.textureUsedBytes = m_textureUsedBytes.load(std::memory_order_relaxed);
		stats.textureBlockCount = m_textureBlocks.size();
		stats.textureAllocations = m_textureAllocations.load(std::memory_order_relaxed);
		stats.textureFrees = m_textureFrees.load(std::memory_order_relaxed);
		
		// Count free regions
		uint64 freeRegions = 0;
		for (const auto& block : m_textureBlocks) {
			TextureFreeRegion* region = block->freeList;
			while (region) {
				++freeRegions;
				region = region->next;
			}
		}
		stats.textureFreeRegions = freeRegions;

		// Overall
		stats.totalAllocatedBytes = stats.smallAllocatedBytes + stats.frameAllocatedBytes + stats.textureUsedBytes;
		stats.totalReservedBytes = stats.smallReservedBytes + stats.frameCapacityBytes + stats.textureReservedBytes;

		return stats;
	}

	void MemorySystem::resetStats() {
		m_smallCacheHits.store(0, std::memory_order_relaxed);
		m_smallCacheMisses.store(0, std::memory_order_relaxed);
		m_frameScratch.peak.store(0, std::memory_order_relaxed);
		m_frameScratch.allocations.store(0, std::memory_order_relaxed);
		m_textureAllocations.store(0, std::memory_order_relaxed);
		m_textureFrees.store(0, std::memory_order_relaxed);
		
		// Reset per-bin counters
		for (uint32 i = 0; i < kNumSmallBins; ++i) {
			m_smallBins[i].allocCount.store(0, std::memory_order_relaxed);
			m_smallBins[i].freeCount.store(0, std::memory_order_relaxed);
		}
	}

	uint64 MemorySystem::getAllocatedSmallBytes() const { 
		return m_smallAllocatedBytes.load(std::memory_order_relaxed); 
	}
	
	uint64 MemorySystem::getAllocatedFrameBytes() const { 
		return m_frameScratch.offset.load(std::memory_order_relaxed); 
	}
	
	uint64 MemorySystem::getReservedTextureBytes() const { 
		return m_textureReservedBytes.load(std::memory_order_relaxed); 
	}

	// Huge Pages Implementation
	bool MemorySystem::detectHugePagesSupport() {
#if PLATFORM_WINDOWS
		// On Windows, check for SeLockMemoryPrivilege
		HANDLE hToken;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
			return false;
		}

		// Try to get lock memory privilege
		LUID luid;
		if (!LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &luid)) {
			CloseHandle(hToken);
			return false;
		}

		// Check if process has the privilege
		PRIVILEGE_SET privs;
		privs.PrivilegeCount = 1;
		privs.Control = PRIVILEGE_SET_ALL_NECESSARY;
		privs.Privilege[0].Luid = luid;
		privs.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

		BOOL hasPrivilege = FALSE;
		if (!PrivilegeCheck(hToken, &privs, &hasPrivilege)) {
			CloseHandle(hToken);
			return false;
		}

		CloseHandle(hToken);
		return hasPrivilege == TRUE;

#elif PLATFORM_LINUX
		// On Linux, check if huge pages are available
		// Try to read /proc/meminfo for HugePages_Total
		FILE* fp = fopen("/proc/meminfo", "r");
		if (!fp) return false;

		char line[256];
		bool found = false;
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "HugePages_Total:", 16) == 0) {
				int total = 0;
				if (sscanf(line + 16, "%d", &total) == 1 && total > 0) {
					found = true;
				}
				break;
			}
		}
		fclose(fp);
		return found;

#else
		return false;
#endif
	}

	void* MemorySystem::allocateHugePages(size_t size) {
		if (!m_hugePagesAvailable || !m_hugePagesEnabled) {
			return nullptr;  // Fallback to normal allocation
		}

		// Align size to huge page boundary
		size = alignUp(size, MR_HUGE_PAGE_SIZE);

#if PLATFORM_WINDOWS
		// Windows: Use VirtualAlloc with MEM_LARGE_PAGES
		void* ptr = VirtualAlloc(
			nullptr,
			size,
			MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES,
			PAGE_READWRITE
		);

		if (ptr) {
			MR_LOG_DEBUG("Allocated " + std::to_string(size / 1024 / 1024) + "MB with huge pages (Windows)");
			return ptr;
		} else {
			DWORD error = GetLastError();
			MR_LOG_WARNING("Failed to allocate huge pages (error " + std::to_string(error) + "), falling back to normal allocation");
			return nullptr;
		}

#elif PLATFORM_LINUX
		// Linux: Use mmap with MAP_HUGETLB
		void* ptr = mmap(
			nullptr,
			size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
			-1,
			0
		);

		if (ptr != MAP_FAILED) {
			MR_LOG_DEBUG("Allocated " + std::to_string(size / 1024 / 1024) + "MB with huge pages (Linux)");
			return ptr;
		} else {
			MR_LOG_WARNING("Failed to allocate huge pages, falling back to normal allocation");
			return nullptr;
		}

#else
		return nullptr;
#endif
	}

	void MemorySystem::freeHugePages(void* ptr, size_t size) {
		if (!ptr) return;

#if PLATFORM_WINDOWS
		VirtualFree(ptr, 0, MEM_RELEASE);

#elif PLATFORM_LINUX
		size = alignUp(size, MR_HUGE_PAGE_SIZE);
		munmap(ptr, size);

#endif
	}

	bool MemorySystem::isHugePagesAvailable() const {
		return m_hugePagesAvailable;
	}

	bool MemorySystem::enableHugePages(bool enable) {
		if (enable && !m_hugePagesAvailable) {
			MR_LOG_WARNING("Cannot enable huge pages: not available on this system");
			return false;
		}
		m_hugePagesEnabled = enable;
		MR_LOG_INFO(String("Huge pages ") + (enable ? "enabled" : "disabled"));
		return true;
	}

	void MemorySystem::setHugePagesForTextures(bool enable) {
		m_useHugePagesForTextures = enable && m_hugePagesAvailable;
		MR_LOG_INFO(String("Huge pages for textures ") + (m_useHugePagesForTextures ? "enabled" : "disabled"));
	}

}


