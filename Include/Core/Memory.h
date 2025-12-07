#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Templates/UniquePtr.h"
#include "Containers/Array.h"

namespace MonsterRender {
// Use MonsterEngine containers and smart pointers
using MonsterEngine::TArray;
using MonsterEngine::TUniquePtr;
using MonsterEngine::MakeUnique;
}

// Platform-specific alignment and huge pages
#if PLATFORM_WINDOWS
	#define MR_CACHE_LINE_SIZE 64
	#define MR_SIMD_ALIGNMENT 32  // AVX2
	#define MR_HUGE_PAGE_SIZE (2ull * 1024ull * 1024ull)  // 2MB on Windows
#elif PLATFORM_LINUX
	#define MR_CACHE_LINE_SIZE 64
	#define MR_SIMD_ALIGNMENT 16  // SSE
	#define MR_HUGE_PAGE_SIZE (2ull * 1024ull * 1024ull)  // 2MB on Linux (default)
#else
	#define MR_CACHE_LINE_SIZE 64
	#define MR_SIMD_ALIGNMENT 16
	#define MR_HUGE_PAGE_SIZE (2ull * 1024ull * 1024ull)
#endif

namespace MonsterRender {

	/**
	 * Unified memory system providing pooled allocators with advanced features.
	 * - Small object pool (binned allocator, per-bin locks, TLS caching)
	 * - Frame scratch pool (per-frame linear allocator, lock-free)
	 * - Texture buffer pool (large block suballocator with free-list merging)
	 * - Page recycling and defragmentation
	 * - Comprehensive statistics and observability
	 *
	 * Design inspired by UE5 FMallocBinned2 and pool policies.
	 */
	class MemorySystem {
	public:
		static MemorySystem& get();

		// Lifecycle
		bool initialize(uint64 frameScratchSizeBytes = 8ull * 1024ull * 1024ull /* 8 MB */, uint64 texturePoolBlockSizeBytes = 64ull * 1024ull * 1024ull /* 64 MB */);
		void shutdown();

		// Small objects (<= 1024 bytes fast path, with TLS cache)
		void* allocateSmall(size_t size, size_t alignment = alignof(std::max_align_t));
		void  freeSmall(void* ptr, size_t size);

		// General allocations (falls back to small when applicable)
		void* allocate(size_t size, size_t alignment = alignof(std::max_align_t));
		void  free(void* ptr, size_t size);

		// Frame scratch (reset every frame, lock-free bump allocator)
		void* frameAllocate(size_t size, size_t alignment = alignof(std::max_align_t));
		void  frameReset();

		// Texture buffer pool (large transient/staging buffers with free-list)
		void* textureAllocate(size_t size, size_t alignment = 256);
		void  textureReleaseAll();
		void  textureFree(void* ptr);  // Free specific allocation

		// Maintenance & Defragmentation
		void  trimEmptyPages();  // Release empty pages back to system
		void  compactTextureBlocks();  // Merge free regions

		// Huge Pages Support
		bool  isHugePagesAvailable() const;  // Check if huge pages are supported
		bool  enableHugePages(bool enable);   // Enable/disable huge pages for new allocations
		void  setHugePagesForTextures(bool enable);  // Use huge pages for texture pool

		// Memory Statistics
		struct MemoryStats {
			// Small bins
			uint64 smallAllocatedBytes = 0;
			uint64 smallReservedBytes = 0;
			uint64 smallPageCount = 0;
			uint64 smallEmptyPageCount = 0;
			uint64 smallAllocations = 0;
			uint64 smallFrees = 0;
			uint64 smallCacheHits = 0;
			uint64 smallCacheMisses = 0;

			// Frame scratch
			uint64 frameAllocatedBytes = 0;
			uint64 frameCapacityBytes = 0;
			uint64 framePeakBytes = 0;
			uint64 frameAllocations = 0;

			// Texture pool
			uint64 textureReservedBytes = 0;
			uint64 textureUsedBytes = 0;
			uint64 textureBlockCount = 0;
			uint64 textureFreeRegions = 0;
			uint64 textureAllocations = 0;
			uint64 textureFrees = 0;

			// Overall
			uint64 totalAllocatedBytes = 0;
			uint64 totalReservedBytes = 0;
		};

		MemoryStats getStats() const;
		void resetStats();  // Reset counters (not memory)

		// Diagnostics (legacy, use getStats for detailed info)
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
		
		// Thread-local cache for small allocations (inspired by UE5 FMallocBinned2)
		struct alignas(MR_CACHE_LINE_SIZE) ThreadLocalCache {
			static constexpr uint32 kCacheSize = 16;  // elements per bin
			void* cache[7][kCacheSize];  // 7 bins
			uint32 count[7] = {};
			uint64 hits = 0;
			uint64 misses = 0;
		};

		struct SmallBin {
			uint32 elementSize = 0;
			TArray<SmallBinPage*> pages;
			std::mutex mutex;  // Per-bin lock (reduces contention)
			std::atomic<uint64> allocCount{0};
			std::atomic<uint64> freeCount{0};
		};

		struct SmallBinPageHeader {
			SmallBin* parentBin;
			uint32 elementSize;
			uint32 elementCount;
			uint32 freeCount;
			void*  freeList;
		};

		struct SmallBinPage {
			SmallBinPageHeader header;
			// Followed by raw memory region for elements
		};

		struct FrameScratch {
			TUniquePtr<uint8[]> buffer;
			uint64 capacity = 0;
			std::atomic<uint64> offset{0};
			std::atomic<uint64> peak{0};  // Track peak usage
			std::atomic<uint64> allocations{0};
		};

		// Free region in texture block (for sub-allocation)
		struct TextureFreeRegion {
			uint64 offset;
			uint64 size;
			TextureFreeRegion* next = nullptr;
		};

		struct TextureBlock {
			TUniquePtr<uint8[]> buffer;
			uint8* rawHugePagePtr = nullptr;  // Huge page raw pointer (if used)
			uint64 capacity = 0;
			std::atomic<uint64> offset{0};  // Bump allocator offset
			TextureFreeRegion* freeList = nullptr;  // Linked list of free regions
			std::mutex mutex;  // Per-block lock for free-list
			std::atomic<uint64> usedBytes{0};
			bool usesHugePages = false;  // Track if this block uses huge pages
		};

	private:
		// Small bins for sizes up to 1024 bytes (power-of-two buckets)
		static constexpr uint32 kNumSmallBins = 7; // 16,32,64,128,256,512,1024
		SmallBin m_smallBins[kNumSmallBins]{};
		std::atomic<uint64> m_smallAllocatedBytes{0};
		std::atomic<uint64> m_smallReservedBytes{0};

		// Statistics
		mutable std::mutex m_statsMutex;
		std::atomic<uint64> m_smallCacheHits{0};
		std::atomic<uint64> m_smallCacheMisses{0};

		// Frame scratch allocator
		FrameScratch m_frameScratch{};

		// Texture buffer pool
		uint64 m_textureBlockSize = 0;
		TArray<TUniquePtr<TextureBlock>> m_textureBlocks;
		std::mutex m_textureBlocksMutex;
		std::atomic<uint64> m_textureReservedBytes{0};
		std::atomic<uint64> m_textureUsedBytes{0};
		std::atomic<uint64> m_textureAllocations{0};
		std::atomic<uint64> m_textureFrees{0};

		// Huge pages configuration
		bool m_hugePagesEnabled = false;
		bool m_hugePagesAvailable = false;
		bool m_useHugePagesForTextures = true;  // Textures benefit most from huge pages

		// TLS cache (thread_local in cpp)
		static thread_local ThreadLocalCache* t_tlsCache;

		// Helpers
		static uint32 selectSmallBin(size_t size);
		SmallBinPage* allocateSmallPage(uint32 elementSize);
		void* allocateFromBin(SmallBin& bin, size_t alignment, ThreadLocalCache* tlsCache);
		void  freeToBin(SmallBin& bin, void* ptr, ThreadLocalCache* tlsCache);
		static size_t alignUp(size_t value, size_t alignment);
		ThreadLocalCache* getTLSCache();
		void releaseTLSCache(ThreadLocalCache* cache);
		
		// Texture free-list helpers
		void* allocateFromFreeList(TextureBlock& block, size_t size, size_t alignment);
		void addToFreeList(TextureBlock& block, void* ptr, size_t size);
		void mergeFreeRegions(TextureBlock& block);

		// Huge pages platform-specific helpers
		bool detectHugePagesSupport();
		void* allocateHugePages(size_t size);
		void freeHugePages(void* ptr, size_t size);
	};

}


