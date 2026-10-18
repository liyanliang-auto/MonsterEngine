#include "Core/CoreMinimal.h"
#include "Core/Memory.h"
#include <malloc.h>
#include <new>
#include <algorithm>
#include <limits>
#include "Core/RuntimeLease.h"

// Platform-specific includes for huge pages
#if PLATFORM_WINDOWS
	#include <Windows.h>
	#elif PLATFORM_LINUX
	#include <sys/mman.h>
	#include <unistd.h>
	#include <fcntl.h>
#endif

namespace MonsterRender {
    namespace {
    struct FAlignedAllocation { void* Base; };
    bool ValidAlignment(size_t Alignment) { return Alignment && !(Alignment & (Alignment - 1)); }
    void* AllocateAligned(size_t Size, size_t Alignment) {
        if (!ValidAlignment(Alignment)) return nullptr;
        Alignment = std::max(Alignment, alignof(FAlignedAllocation));
        if (Alignment - 1 > SIZE_MAX - sizeof(FAlignedAllocation)) return nullptr;
        const size_t Extra = Alignment - 1 + sizeof(FAlignedAllocation);
        if (Size > std::numeric_limits<size_t>::max() - Extra) return nullptr;
        void* Base = std::malloc(Size + Extra);
        if (!Base) return nullptr;
        const size_t Address = (reinterpret_cast<size_t>(Base) + Extra) & ~(Alignment - 1);
        (reinterpret_cast<FAlignedAllocation*>(Address) - 1)->Base = Base;
        return reinterpret_cast<void*>(Address);
    }
    void FreeAligned(void* Ptr) { if (Ptr) std::free((static_cast<FAlignedAllocation*>(Ptr) - 1)->Base); }
    }


	static constexpr size_t kSmallPageSize = 64ull * 1024ull; // 64KB pages for small bins
	static constexpr uint32 kEmptyPageThreshold = 4;  // Trim if this many empty pages exist

	static uint8* alignPtr(uint8* ptr, size_t alignment) {
		const size_t mask = alignment - 1;
		return reinterpret_cast<uint8*>((reinterpret_cast<size_t>(ptr) + mask) & ~mask);
	}

	// Thread-local cache (initialized lazily per thread)
	thread_local MemorySystem::ThreadLocalCache MemorySystem::t_tlsCache{};

	MemorySystem& MemorySystem::get() {
		static MemorySystem instance;
		return instance;
	}

	bool MemorySystem::initialize(uint64 frameScratchSizeBytes, uint64 texturePoolBlockSizeBytes) {
        std::lock_guard<std::mutex> Lock(m_lifecycleMutex);
        if (m_initialized) return true;
        ++m_cacheGeneration;
        if (!frameScratchSizeBytes || !texturePoolBlockSizeBytes) return false;
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
		m_initialized = true;
		return true;
	}

	void MemorySystem::shutdown() {
        std::lock_guard<std::mutex> Lock(m_lifecycleMutex);
        if (!m_initialized || FProcessRuntimeLease::ActiveCount() != 0) return;
        m_initialized = false;
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
#if PLATFORM_ANDROID
        // Mobile worker lifetimes are short; return freed slots directly to pages.
        return nullptr;
#else
        if (t_tlsCache.generation != m_cacheGeneration) {
            t_tlsCache = ThreadLocalCache{};
            t_tlsCache.generation = m_cacheGeneration;
        }
        return &t_tlsCache;
#endif
    }

    void MemorySystem::releaseTLSCache(ThreadLocalCache* cache) {
        if (!cache || cache->generation != m_cacheGeneration) return;
        for (uint32 binIdx = 0; binIdx < kNumSmallBins; ++binIdx) {
            auto& bin = m_smallBins[binIdx];
            std::scoped_lock lock(bin.mutex);
            for (uint32 i = 0; i < cache->count[binIdx]; ++i) {
                void* ptr = cache->cache[binIdx][i];
                const auto address = reinterpret_cast<uintptr_t>(ptr);
                for (auto* page : bin.pages) {
                    const auto base = reinterpret_cast<uintptr_t>(page);
                    if (address >= base && address - base < kSmallPageSize) {
                        *static_cast<void**>(ptr) = page->header.freeList;
                        page->header.freeList = ptr;
                        ++page->header.freeCount;
                        break;
                    }
                }
            }
        }
        *cache = ThreadLocalCache{};
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
		if (tlsCache) ++tlsCache->misses;
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
        if (!ValidAlignment(alignment)) return nullptr;
        if (size == 0) size = 1;
        const size_t binSize = std::max(size, alignment);
        if (binSize > 1024) return AllocateAligned(size, alignment);
        return allocateFromBin(m_smallBins[selectSmallBin(binSize)], alignment, getTLSCache());
    }

    void MemorySystem::freeSmall(void* ptr, size_t size) {
        if (!ptr) return;
        (void)size;
        const auto address = reinterpret_cast<uintptr_t>(ptr);
        for (auto& bin : m_smallBins) {
            bool owns = false;
            {
                std::lock_guard<std::mutex> lock(bin.mutex);
                for (auto* page : bin.pages) {
                    const auto base = reinterpret_cast<uintptr_t>(page);
                    if (address >= base && address - base < kSmallPageSize) { owns = true; break; }
                }
            }
            if (owns) { freeToBin(bin, ptr, getTLSCache()); return; }
        }
        FreeAligned(ptr);
    }

    void* MemorySystem::allocate(size_t size, size_t alignment) {
        return allocateSmall(size, alignment);
    }

    void MemorySystem::free(void* ptr, size_t size) {
        freeSmall(ptr, size);
    }

    void* MemorySystem::frameAllocate(size_t size, size_t alignment) {
        if (!size || !ValidAlignment(alignment) || !m_frameScratch.buffer) return nullptr;
        uint64 current = m_frameScratch.offset.load(std::memory_order_relaxed);
        while (true) {
            const auto base = reinterpret_cast<size_t>(m_frameScratch.buffer.get());
            if (current > SIZE_MAX - base || base + current > SIZE_MAX - (alignment - 1)) return nullptr;
            const uint64 aligned = alignUp(base + static_cast<size_t>(current), alignment) - base;
            if (aligned > m_frameScratch.capacity || size > m_frameScratch.capacity - aligned) {
#if PLATFORM_ANDROID
                return nullptr;
#else
                // Preserve the desktop grow-on-exhaustion policy (owner-thread only).
                if (m_frameScratch.capacity > SIZE_MAX / 2 || size > SIZE_MAX - alignment) return nullptr;
                const size_t newCapacity = std::max<size_t>(m_frameScratch.capacity * 2, size + alignment);
                auto newBuffer = MakeUnique<uint8[]>(newCapacity);
                m_frameScratch.buffer.swap(newBuffer);
                m_frameScratch.capacity = newCapacity;
                m_frameScratch.offset.store(0, std::memory_order_relaxed);
                current = 0;
                continue;
#endif
            }
            const uint64 next = aligned + size;
            if (!m_frameScratch.offset.compare_exchange_weak(current, next, std::memory_order_acq_rel)) continue;
            uint64 peak = m_frameScratch.peak.load(std::memory_order_relaxed);
            while (peak < next && !m_frameScratch.peak.compare_exchange_weak(peak, next, std::memory_order_relaxed)) {}
            m_frameScratch.allocations.fetch_add(1, std::memory_order_relaxed);
            return m_frameScratch.buffer.get() + aligned;
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
			const size_t base = reinterpret_cast<size_t>(basePtr);
            if (base > SIZE_MAX - region->offset || base + region->offset > SIZE_MAX - (alignment - 1)) return nullptr;
            uint64 alignedOffset = alignUp(base + static_cast<size_t>(region->offset), alignment) - base;
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
        if (!newRegion) throw std::bad_alloc();
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
        if (!size || !ValidAlignment(alignment) || size > SIZE_MAX - (alignment - 1)) return nullptr;
        std::scoped_lock poolLock(m_textureBlocksMutex);
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
			const size_t base = reinterpret_cast<size_t>(block->usesHugePages ? block->rawHugePagePtr : block->buffer.get());
            if (base > SIZE_MAX - off || base + off > SIZE_MAX - (alignment - 1)) continue;
            uint64 alignedOff = alignUp(base + static_cast<size_t>(off), alignment) - base;
            if (alignedOff > block->capacity || alignedSize > block->capacity - alignedOff) continue;
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
		uint64 blockSize = std::max<uint64>(m_textureBlockSize, alignedSize);
        if (blockSize > SIZE_MAX - (alignment - 1)) return nullptr;
		
		// Try huge pages first if enabled and size >= 2MB
		void* hugePagePtr = nullptr;
		bool usedHugePages = false;
		if (m_useHugePagesForTextures && blockSize >= MR_HUGE_PAGE_SIZE && alignment <= MR_HUGE_PAGE_SIZE) {
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
			// Include padding so the first allocation can satisfy any admitted alignment.
            blockSize += alignment - 1;
            block->capacity = blockSize;
			block->buffer = MakeUnique<uint8[]>(static_cast<size_t>(blockSize));
		}
		
		block->offset.store(0, std::memory_order_relaxed);
		block->usedBytes.store(0, std::memory_order_relaxed);
		block->freeList = nullptr;
		
		auto* basePtr = block->usesHugePages ? block->rawHugePagePtr : block->buffer.get();
        void* ptr = alignPtr(basePtr, alignment);
        const uint64 padding = static_cast<uint8*>(ptr) - basePtr;
        block->offset.store(padding + alignedSize, std::memory_order_relaxed);
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

#elif PLATFORM_ANDROID
		// Android does not expose a supported huge-page allocation contract for
		// this allocator. Keep the capability false even though __linux__ is set.
		return false;

#else
		return false;
#endif
	}

	void* MemorySystem::allocateHugePages(size_t size) {
		if (!m_hugePagesAvailable || !m_hugePagesEnabled || PLATFORM_ANDROID) {
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

#elif PLATFORM_ANDROID
		(void)size;

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


