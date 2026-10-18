// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Binned Memory Allocator Implementation

#include "Core/HAL/FMallocBinned2.h"
#include "Core/Log.h"
#include <new>
#include <algorithm>
#include <limits>

namespace MonsterRender {

namespace {
std::atomic<uint64> GAllocatorGeneration{1};
struct FLargeAllocationHeader {
    void* Base;
    SIZE_T Size;
};
}

// Static TLS cache
thread_local FMallocBinned2::FThreadCache FMallocBinned2::TLSCache{};

FMallocBinned2::FMallocBinned2() : CacheGeneration(GAllocatorGeneration.fetch_add(1, std::memory_order_relaxed)) {
    // Initialize bins with power-of-2 sizes
    uint32 size = 16;
    for (uint32 i = 0; i < NUM_SMALL_BINS; ++i) {
        SmallBins[i].ElementSize = size;
        size <<= 1;  // 16, 32, 64, 128, 256, 512, 1024
    }

    MR_LOG_INFO("FMallocBinned2 initialized with " + std::to_string(NUM_SMALL_BINS) + " bins");
}

FMallocBinned2::~FMallocBinned2() {
    // Free all pages in all bins
    for (uint32 i = 0; i < NUM_SMALL_BINS; ++i) {
        auto& bin = SmallBins[i];
        std::scoped_lock lock(bin.Mutex);
        for (auto* page : bin.Pages) {
            ::operator delete(page, std::nothrow);
        }
        bin.Pages.clear();
    }

    MR_LOG_INFO("FMallocBinned2 shutdown");
}

void* FMallocBinned2::Malloc(SIZE_T Size, uint32 Alignment) {
    if (Size == 0) Size = 1;

    if (!Alignment || (Alignment & (Alignment - 1)) != 0) return nullptr;
    Alignment = std::max<uint32>(Alignment, 16);

    // Keep the original allocation and size immediately before the aligned address.
    if (Size > SMALL_BIN_MAX_SIZE || Alignment > SMALL_BIN_MAX_SIZE) {
        const SIZE_T overhead = sizeof(FLargeAllocationHeader) + Alignment - 1;
        if (Size > std::numeric_limits<SIZE_T>::max() - overhead) return nullptr;
        void* base = std::malloc(Size + overhead);
        if (!base) return nullptr;
        const SIZE_T address = (reinterpret_cast<SIZE_T>(base) + sizeof(FLargeAllocationHeader) + Alignment - 1) & ~(SIZE_T(Alignment) - 1);
        auto* header = reinterpret_cast<FLargeAllocationHeader*>(address) - 1;
        header->Base = base;
        header->Size = Size;
        return reinterpret_cast<void*>(address);
    }

    // Small allocation - use binned allocator
    uint32 binIdx = SelectBinIndex(std::max<SIZE_T>(Size, Alignment));
    FThreadCache* cache = GetTLSCache();
    return AllocateFromBin(SmallBins[binIdx], Alignment, cache);
}

void* FMallocBinned2::Realloc(void* Original, SIZE_T Size, uint32 Alignment) {
    if (!Original) {
        return Malloc(Size, Alignment);
    }

    if (Size == 0) {
        Free(Original);
        return nullptr;
    }

    // For now, simple implementation: allocate new + copy + free
    // TODO: Optimize by checking if new size fits in same bin
    SIZE_T oldSize = GetAllocationSize(Original);
    void* newPtr = Malloc(Size, Alignment);
    if (!newPtr) return nullptr;
    if (newPtr && oldSize > 0) {
        SIZE_T copySize = (oldSize < Size) ? oldSize : Size;
        ::memcpy(newPtr, Original, copySize);
    }
    Free(Original);
    return newPtr;
}

void FMallocBinned2::Free(void* Original) {
    if (!Original) return;

    // Try to find which bin owns this pointer
    for (uint32 i = 0; i < NUM_SMALL_BINS; ++i) {
        auto& bin = SmallBins[i];
        bool ownsPointer = false;
        {
            std::scoped_lock lock(bin.Mutex);
            for (auto* page : bin.Pages) {
                uint8* pageStart = reinterpret_cast<uint8*>(page);
                uint8* pageEnd = pageStart + PAGE_SIZE;
                if (reinterpret_cast<uint8*>(Original) >= pageStart &&
                    reinterpret_cast<uint8*>(Original) < pageEnd) {
                    ownsPointer = true;
                    break;
                }
            }
        }
        if (ownsPointer) {
            FreeToBin(bin, Original, GetTLSCache());
            return;
        }
    }

    // Not found in bins: release the large allocation's original address.
    std::free((reinterpret_cast<FLargeAllocationHeader*>(Original) - 1)->Base);
}

SIZE_T FMallocBinned2::GetAllocationSize(void* Original) {
    if (!Original) return 0;

    // Check bins
    for (uint32 i = 0; i < NUM_SMALL_BINS; ++i) {
        auto& bin = SmallBins[i];
        std::scoped_lock lock(bin.Mutex);
        for (auto* page : bin.Pages) {
            uint8* pageStart = reinterpret_cast<uint8*>(page);
            uint8* pageEnd = pageStart + PAGE_SIZE;
            if (reinterpret_cast<uint8*>(Original) >= pageStart && 
                reinterpret_cast<uint8*>(Original) < pageEnd) {
                return bin.ElementSize;
            }
        }
    }

    return (reinterpret_cast<FLargeAllocationHeader*>(Original) - 1)->Size;
}

bool FMallocBinned2::ValidateHeap() {
    // Simple validation: check all pages have valid headers
    for (uint32 i = 0; i < NUM_SMALL_BINS; ++i) {
        auto& bin = SmallBins[i];
        std::scoped_lock lock(bin.Mutex);
        for (auto* page : bin.Pages) {
            if (page->ElementSize != bin.ElementSize) {
                MR_LOG_ERROR("Heap validation failed: bin element size mismatch");
                return false;
            }
            if (page->FreeCount > page->ElementCount) {
                MR_LOG_ERROR("Heap validation failed: free count exceeds element count");
                return false;
            }
        }
    }
    return true;
}

uint64 FMallocBinned2::GetTotalAllocatedMemory() {
    return TotalAllocated.load(std::memory_order_relaxed);
}

void FMallocBinned2::Trim() {
    // Release empty pages
    for (uint32 i = 0; i < NUM_SMALL_BINS; ++i) {
        auto& bin = SmallBins[i];
        std::scoped_lock lock(bin.Mutex);

        // Count empty pages
        uint32 emptyCount = 0;
        for (auto* page : bin.Pages) {
            if (page->FreeCount == page->ElementCount) {
                ++emptyCount;
            }
        }

        // Keep at least a few pages, release excess
        if (emptyCount > EMPTY_PAGE_THRESHOLD) {
            std::vector<FPageHeader*> keptPages;
            uint32 releasedCount = 0;
            for (auto* page : bin.Pages) {
                if (page->FreeCount == page->ElementCount && 
                    releasedCount < emptyCount - EMPTY_PAGE_THRESHOLD) {
                    ::operator delete(page, std::nothrow);
                    TotalReserved.fetch_sub(PAGE_SIZE, std::memory_order_relaxed);
                    ++releasedCount;
                } else {
                    keptPages.push_back(page);
                }
            }
            bin.Pages = std::move(keptPages);

            if (releasedCount > 0) {
                MR_LOG_INFO("Trimmed " + std::to_string(releasedCount) + " pages from bin " + std::to_string(i));
            }
        }
    }
}

void FMallocBinned2::GetMemoryStats(FMemoryStats& OutStats) {
    OutStats.TotalAllocated = TotalAllocated.load(std::memory_order_relaxed);
    OutStats.TotalReserved = TotalReserved.load(std::memory_order_relaxed);

    uint64 allocCount = 0;
    uint64 freeCount = 0;
    for (uint32 i = 0; i < NUM_SMALL_BINS; ++i) {
        allocCount += SmallBins[i].AllocCount.load(std::memory_order_relaxed);
        freeCount += SmallBins[i].FreeCount.load(std::memory_order_relaxed);
    }
    OutStats.AllocationCount = allocCount;
    OutStats.FreeCount = freeCount;
}

// Private methods

uint32 FMallocBinned2::SelectBinIndex(SIZE_T Size) {
    if (Size <= 16) return 0;
    if (Size <= 32) return 1;
    if (Size <= 64) return 2;
    if (Size <= 128) return 3;
    if (Size <= 256) return 4;
    if (Size <= 512) return 5;
    return 6;  // up to 1024
}

FMallocBinned2::FPageHeader* FMallocBinned2::AllocatePage(uint32 ElementSize) {
    const SIZE_T rawSize = PAGE_SIZE;
    auto* raw = reinterpret_cast<uint8*>(::operator new(rawSize, std::nothrow));
    if (!raw) return nullptr;

    auto* page = reinterpret_cast<FPageHeader*>(raw);
    page->FreeList = nullptr;
    page->ElementSize = ElementSize;
    page->NextPage = nullptr;

    // Align region start
    uint8* regionStart = raw + sizeof(FPageHeader);
    SIZE_T alignment = ElementSize;
    SIZE_T mask = alignment - 1;
    regionStart = reinterpret_cast<uint8*>((reinterpret_cast<SIZE_T>(regionStart) + mask) & ~mask);

    const SIZE_T usable = rawSize - (regionStart - raw);
    const uint32 count = static_cast<uint32>(usable / ElementSize);

    page->ElementCount = count;
    page->FreeCount = count;

    // Build free list
    uint8* current = regionStart;
    void* prev = nullptr;
    for (uint32 i = 0; i < count; ++i) {
        *reinterpret_cast<void**>(current) = prev;
        prev = current;
        current += ElementSize;
    }
    page->FreeList = prev;

    return page;
}

void* FMallocBinned2::AllocateFromBin(FBin& Bin, uint32 Alignment, FThreadCache* Cache) {
    const uint32 binIdx = SelectBinIndex(Bin.ElementSize);

    // Try TLS cache first (lock-free fast path)
    if (Cache && Cache->Count[binIdx] > 0) {
        --Cache->Count[binIdx];
        void* p = Cache->Cache[binIdx][Cache->Count[binIdx]];
        ++Cache->Hits;
        CacheHits.fetch_add(1, std::memory_order_relaxed);
        TotalAllocated.fetch_add(Bin.ElementSize, std::memory_order_relaxed);
        Bin.AllocCount.fetch_add(1, std::memory_order_relaxed);
        return p;
    }

    // Cache miss
    if (Cache) {
        ++Cache->Misses;
        CacheMisses.fetch_add(1, std::memory_order_relaxed);
    }

    // Acquire lock and allocate from bin
    std::scoped_lock lock(Bin.Mutex);

    // Find page with free slots
    for (auto* page : Bin.Pages) {
        if (page->FreeCount > 0) {
            void* p = page->FreeList;
            page->FreeList = *reinterpret_cast<void**>(p);
            --page->FreeCount;
            TotalAllocated.fetch_add(Bin.ElementSize, std::memory_order_relaxed);
            Bin.AllocCount.fetch_add(1, std::memory_order_relaxed);
            return p;
        }
    }

    // No free slots - allocate new page
    FPageHeader* newPage = AllocatePage(Bin.ElementSize);
    if (!newPage) return nullptr;

    Bin.Pages.push_back(newPage);
    TotalReserved.fetch_add(PAGE_SIZE, std::memory_order_relaxed);

    // Pop one element
    void* p = newPage->FreeList;
    newPage->FreeList = *reinterpret_cast<void**>(p);
    --newPage->FreeCount;
    TotalAllocated.fetch_add(Bin.ElementSize, std::memory_order_relaxed);
    Bin.AllocCount.fetch_add(1, std::memory_order_relaxed);
    return p;
}

void FMallocBinned2::FreeToBin(FBin& Bin, void* Ptr, FThreadCache* Cache) {
    const uint32 binIdx = SelectBinIndex(Bin.ElementSize);

    // Try to add to TLS cache (lock-free fast path)
    if (Cache && Cache->Count[binIdx] < TLS_CACHE_SIZE) {
        Cache->Cache[binIdx][Cache->Count[binIdx]] = Ptr;
        ++Cache->Count[binIdx];
        TotalAllocated.fetch_sub(Bin.ElementSize, std::memory_order_relaxed);
        Bin.FreeCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Cache full - return to bin
    std::scoped_lock lock(Bin.Mutex);

    // Find the owning page
    for (auto* page : Bin.Pages) {
        uint8* pageStart = reinterpret_cast<uint8*>(page);
        uint8* pageEnd = pageStart + PAGE_SIZE;
        if (reinterpret_cast<uint8*>(Ptr) >= pageStart && reinterpret_cast<uint8*>(Ptr) < pageEnd) {
            *reinterpret_cast<void**>(Ptr) = page->FreeList;
            page->FreeList = Ptr;
            ++page->FreeCount;
            TotalAllocated.fetch_sub(Bin.ElementSize, std::memory_order_relaxed);
            Bin.FreeCount.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    MR_LOG_WARNING("FreeToBin: pointer not found in bin");
}

FMallocBinned2::FThreadCache* FMallocBinned2::GetTLSCache() {
#if PLATFORM_ANDROID
    return nullptr;
#else
    // Cache storage belongs to the thread, entries belong to one allocator generation.
    if (TLSCache.OwnerGeneration != CacheGeneration) {
        TLSCache = FThreadCache{};
        TLSCache.OwnerGeneration = CacheGeneration;
    }
    return &TLSCache;
#endif
}

void FMallocBinned2::ReleaseTLSCache(FThreadCache* Cache) {
    if (!Cache || Cache->OwnerGeneration != CacheGeneration) return;
    for (uint32 binIdx = 0; binIdx < NUM_SMALL_BINS; ++binIdx) {
        auto& bin = SmallBins[binIdx];
        std::scoped_lock lock(bin.Mutex);
        for (uint32 i = 0; i < Cache->Count[binIdx]; ++i) {
            void* ptr = Cache->Cache[binIdx][i];
            const auto address = reinterpret_cast<uintptr_t>(ptr);
            for (auto* page : bin.Pages) {
                const auto base = reinterpret_cast<uintptr_t>(page);
                if (address >= base && address - base < PAGE_SIZE) {
                    *static_cast<void**>(ptr) = page->FreeList;
                    page->FreeList = ptr;
                    ++page->FreeCount;
                    break;
                }
            }
        }
    }
    *Cache = FThreadCache{};
}

} // namespace MonsterRender
