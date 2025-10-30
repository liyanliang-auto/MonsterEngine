// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Binned Memory Allocator (UE5-style)

#pragma once

#include "Core/HAL/FMalloc.h"
#include <mutex>
#include <atomic>
#include <vector>

namespace MonsterRender {

/**
 * FMallocBinned2 - Fast multi-threaded allocator for small objects
 * 
 * Reference: UE5 Engine/Source/Runtime/Core/Public/HAL/MallocBinned2.h
 * 
 * Features:
 * - Per-size-class bins (16B to 1024B)
 * - Thread-local caching for lock-free fast path
 * - Per-bin locks for scalability
 * - Page-based allocation with free lists
 * - Large allocations fall back to OS allocator
 */
class FMallocBinned2 : public FMalloc {
public:
    FMallocBinned2();
    virtual ~FMallocBinned2();

    // FMalloc interface
    virtual void* Malloc(SIZE_T Size, uint32 Alignment = DEFAULT_ALIGNMENT) override;
    virtual void* Realloc(void* Original, SIZE_T Size, uint32 Alignment = DEFAULT_ALIGNMENT) override;
    virtual void Free(void* Original) override;
    virtual SIZE_T GetAllocationSize(void* Original) override;
    virtual bool ValidateHeap() override;
    virtual uint64 GetTotalAllocatedMemory() override;
    virtual void Trim() override;
    virtual void GetMemoryStats(FMemoryStats& OutStats) override;

private:
    // Constants (matching UE5)
    static constexpr uint32 NUM_SMALL_BINS = 7;      // 16, 32, 64, 128, 256, 512, 1024
    static constexpr SIZE_T SMALL_BIN_MAX_SIZE = 1024;
    static constexpr SIZE_T PAGE_SIZE = 64 * 1024;   // 64KB pages
    static constexpr uint32 TLS_CACHE_SIZE = 16;     // Items per bin in TLS cache
    static constexpr uint32 EMPTY_PAGE_THRESHOLD = 4; // Trim threshold

    // Page header for small bins
    struct FPageHeader {
        void* FreeList;           // LIFO free list
        uint32 ElementSize;       // Size of elements in this page
        uint32 ElementCount;      // Total elements in page
        uint32 FreeCount;         // Number of free elements
        FPageHeader* NextPage;    // Link to next page in bin
    };

    // Per-size bin
    struct FBin {
        uint32 ElementSize;
        std::vector<FPageHeader*> Pages;
        std::mutex Mutex;
        std::atomic<uint64> AllocCount{0};
        std::atomic<uint64> FreeCount{0};
    };

    // Thread-local cache for lock-free fast path
    struct alignas(64) FThreadCache {  // Cache-line aligned
        void* Cache[NUM_SMALL_BINS][TLS_CACHE_SIZE];
        uint32 Count[NUM_SMALL_BINS];
        uint64 Hits;
        uint64 Misses;
    };

    // Member variables
    FBin SmallBins[NUM_SMALL_BINS];
    
    std::atomic<uint64> TotalAllocated{0};
    std::atomic<uint64> TotalReserved{0};
    std::atomic<uint64> CacheHits{0};
    std::atomic<uint64> CacheMisses{0};

    static thread_local FThreadCache* TLSCache;

    // Helper methods
    uint32 SelectBinIndex(SIZE_T Size);
    FPageHeader* AllocatePage(uint32 ElementSize);
    void* AllocateFromBin(FBin& Bin, uint32 Alignment, FThreadCache* Cache);
    void FreeToBin(FBin& Bin, void* Ptr, FThreadCache* Cache);
    FThreadCache* GetTLSCache();
    void ReleaseTLSCache(FThreadCache* Cache);
};

} // namespace MonsterRender

