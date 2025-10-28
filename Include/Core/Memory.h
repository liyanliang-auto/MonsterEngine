#pragma once

#include "Core/CoreMinimal.h"

namespace MonsterRender {

	/**
	 * Unified memory system providing pooled allocators.
	 * - Small object pool (binned allocator style)
	 * - Frame scratch pool (per-frame linear allocator)
	 * - Texture buffer pool (large block suballocator for staging/IO)
	 *
	 * Design inspired by UE5 FMallocBinned2 and pool policies.
	 */
	class MemorySystem {
	public:
		static MemorySystem& get();

		// Lifecycle
		bool initialize(uint64 frameScratchSizeBytes = 8ull * 1024ull * 1024ull /* 8 MB */, uint64 texturePoolBlockSizeBytes = 64ull * 1024ull * 1024ull /* 64 MB */);
		void shutdown();

		// Small objects (<= 1024 bytes fast path)
		void* allocateSmall(size_t size, size_t alignment = alignof(std::max_align_t));
		void  freeSmall(void* ptr, size_t size);

		// General allocations (falls back to small when applicable)
		void* allocate(size_t size, size_t alignment = alignof(std::max_align_t));
		void  free(void* ptr, size_t size);

		// Frame scratch (reset every frame)
		void* frameAllocate(size_t size, size_t alignment = alignof(std::max_align_t));
		void  frameReset();

		// Texture buffer pool (large transient/staging buffers)
		void* textureAllocate(size_t size, size_t alignment = 256);
		void  textureReleaseAll();

		// Diagnostics
		uint64 getAllocatedSmallBytes() const;
		uint64 getAllocatedFrameBytes() const;
		uint64 getReservedTextureBytes() const;

	private:
		MemorySystem() = default;
		~MemorySystem() = default;
		MemorySystem(const MemorySystem&) = delete;
		MemorySystem& operator=(const MemorySystem&) = delete;

		// Internal structures
		struct SmallBinPage;
		struct SmallBin {
			uint32 elementSize = 0;
			TArray<SmallBinPage*> pages; // simple vector; pages are linked via freelist in page
		};

		struct SmallBinPageHeader {
			SmallBin* parentBin;
			uint32 elementSize;
			uint32 elementCount;
			uint32 freeCount;
			void*  freeList; // singly-linked list of free elements stored in element memory
		};

		struct SmallBinPage {
			SmallBinPageHeader header;
			// Followed by raw memory region for elements
		};

		struct FrameScratch {
			TUniquePtr<uint8[]> buffer;
			uint64 capacity = 0;
			std::atomic<uint64> offset{0};
		};

		struct TextureBlock {
			TUniquePtr<uint8[]> buffer;
			uint64 capacity = 0;
			std::atomic<uint64> offset{0};
		};

	private:
		// Small bins for sizes up to 1024 bytes (power-of-two buckets)
		static constexpr uint32 kNumSmallBins = 7; // 16,32,64,128,256,512,1024
		SmallBin m_smallBins[kNumSmallBins]{};
		mutable std::mutex m_smallMutex;
		uint64 m_smallAllocatedBytes = 0;

		// Frame scratch allocator
		FrameScratch m_frameScratch{};

		// Texture buffer pool - grows with new blocks
		uint64 m_textureBlockSize = 0;
		TArray<TUniquePtr<TextureBlock>> m_textureBlocks;
		uint64 m_textureReservedBytes = 0;

		// Helpers
		static uint32 selectSmallBin(size_t size);
		SmallBinPage* allocateSmallPage(uint32 elementSize);
		void* allocateFromBin(SmallBin& bin, size_t alignment);
		void  freeToBin(SmallBin& bin, void* ptr);
		static size_t alignUp(size_t value, size_t alignment);
	};

}


