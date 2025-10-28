#include "Core/CoreMinimal.h"
#include "Core/Memory.h"
#include <malloc.h>
#include <new>

namespace MonsterRender {

	static constexpr size_t kSmallPageSize = 64ull * 1024ull; // 64KB pages for small bins

	static uint8* alignPtr(uint8* ptr, size_t alignment) {
		const size_t mask = alignment - 1;
		return reinterpret_cast<uint8*>((reinterpret_cast<size_t>(ptr) + mask) & ~mask);
	}

	MemorySystem& MemorySystem::get() {
		static MemorySystem instance;
		return instance;
	}

	bool MemorySystem::initialize(uint64 frameScratchSizeBytes, uint64 texturePoolBlockSizeBytes) {
		std::scoped_lock lock(m_smallMutex);
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

		// Texture pool
		m_textureBlockSize = texturePoolBlockSizeBytes;
		m_textureBlocks.clear();
		m_textureReservedBytes = 0;

		MR_LOG_INFO("MemorySystem initialized: frameScratch=" + std::to_string(frameScratchSizeBytes / 1024 / 1024) + "MB, textureBlock=" + std::to_string(texturePoolBlockSizeBytes / 1024 / 1024) + "MB");
		return true;
	}

	void MemorySystem::shutdown() {
		std::scoped_lock lock(m_smallMutex);
		// Release small bin pages
		for (uint32 i = 0; i < kNumSmallBins; ++i) {
			auto& bin = m_smallBins[i];
			for (auto* page : bin.pages) {
				// Pages allocated via ::operator new[]
				::operator delete[](page, std::nothrow);
			}
			bin.pages.clear();
		}
		m_smallAllocatedBytes = 0;

		// Frame scratch
		m_frameScratch.buffer.reset();
		m_frameScratch.capacity = 0;
		m_frameScratch.offset.store(0, std::memory_order_relaxed);

		// Texture pool
		m_textureBlocks.clear();
		m_textureReservedBytes = 0;
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

	void* MemorySystem::allocateFromBin(SmallBin& bin, size_t alignment) {
		// Find a page with free slots
		for (auto* page : bin.pages) {
			if (page->header.freeCount > 0) {
				void* p = page->header.freeList;
				page->header.freeList = *reinterpret_cast<void**>(p);
				--page->header.freeCount;
				m_smallAllocatedBytes += bin.elementSize;
				return p;
			}
		}
		// Allocate new page
		SmallBinPage* newPage = allocateSmallPage(bin.elementSize);
		if (!newPage) return nullptr;
		newPage->header.parentBin = &bin;
		bin.pages.push_back(newPage);
		// Pop one
		void* p = newPage->header.freeList;
		newPage->header.freeList = *reinterpret_cast<void**>(p);
		--newPage->header.freeCount;
		m_smallAllocatedBytes += bin.elementSize;
		return p;
	}

	void MemorySystem::freeToBin(SmallBin& bin, void* ptr) {
		// Find the page that owns ptr (linear scan)
		for (auto* page : bin.pages) {
			uint8* base = reinterpret_cast<uint8*>(page);
			uint8* regionStart = alignPtr(base + sizeof(SmallBinPageHeader), bin.elementSize);
			uint8* regionEnd = regionStart + page->header.elementCount * bin.elementSize;
			if (reinterpret_cast<uint8*>(ptr) >= regionStart && reinterpret_cast<uint8*>(ptr) < regionEnd) {
				*reinterpret_cast<void**>(ptr) = page->header.freeList;
				page->header.freeList = ptr;
				++page->header.freeCount;
				m_smallAllocatedBytes -= bin.elementSize;
				return;
			}
		}
		MR_LOG_WARNING("freeToBin: pointer not found in bin page range");
	}

	void* MemorySystem::allocateSmall(size_t size, size_t alignment) {
		if (size == 0) size = 1;
		const uint32 binIndex = selectSmallBin(size);
		std::scoped_lock lock(m_smallMutex);
		auto& bin = m_smallBins[binIndex];
		return allocateFromBin(bin, alignment);
	}

	void MemorySystem::freeSmall(void* ptr, size_t size) {
		if (!ptr) return;
		const uint32 binIndex = selectSmallBin(size);
		std::scoped_lock lock(m_smallMutex);
		freeToBin(m_smallBins[binIndex], ptr);
	}

	void* MemorySystem::allocate(size_t size, size_t alignment) {
		if (size <= 1024) {
			return allocateSmall(size, alignment);
		}
		return ::_aligned_malloc(size, alignment);
	}

	void MemorySystem::free(void* ptr, size_t size) {
		if (!ptr) return;
		if (size <= 1024) {
			freeSmall(ptr, size);
			return;
		}
		::_aligned_free(ptr);
	}

	void* MemorySystem::frameAllocate(size_t size, size_t alignment) {
		if (size == 0) return nullptr;
		// Fast path: try to bump allocate
		uint64 current = m_frameScratch.offset.load(std::memory_order_relaxed);
		while (true) {
			uint64 aligned = static_cast<uint64>(alignUp(static_cast<size_t>(current), alignment));
			uint64 next = aligned + size;
			if (next <= m_frameScratch.capacity) {
				if (m_frameScratch.offset.compare_exchange_weak(current, next, std::memory_order_acq_rel)) {
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

	void* MemorySystem::textureAllocate(size_t size, size_t alignment) {
		const size_t alignedSize = alignUp(size, alignment);
		for (auto& block : m_textureBlocks) {
			uint64 off = block->offset.load(std::memory_order_relaxed);
			uint64 alignedOff = static_cast<uint64>(alignUp(static_cast<size_t>(off), alignment));
			uint64 next = alignedOff + alignedSize;
			if (next <= block->capacity) {
				if (block->offset.compare_exchange_weak(off, next, std::memory_order_acq_rel)) {
					return block->buffer.get() + alignedOff;
				}
				// retry searching blocks
			}
		}
		// Allocate new block
		uint64 blockSize = std::max<uint64>(m_textureBlockSize, alignedSize);
		auto block = MakeUnique<TextureBlock>();
		block->capacity = blockSize;
		block->buffer = MakeUnique<uint8[]>(static_cast<size_t>(blockSize));
		block->offset.store(0, std::memory_order_relaxed);
		void* ptr = block->buffer.get();
		block->offset.store(alignedSize, std::memory_order_relaxed);
		m_textureReservedBytes += blockSize;
		m_textureBlocks.push_back(std::move(block));
		return ptr;
	}

	void MemorySystem::textureReleaseAll() {
		for (auto& block : m_textureBlocks) {
			block->offset.store(0, std::memory_order_relaxed);
		}
	}

	uint64 MemorySystem::getAllocatedSmallBytes() const { return m_smallAllocatedBytes; }
	uint64 MemorySystem::getAllocatedFrameBytes() const { return m_frameScratch.offset.load(std::memory_order_relaxed); }
	uint64 MemorySystem::getReservedTextureBytes() const { return m_textureReservedBytes; }

}


