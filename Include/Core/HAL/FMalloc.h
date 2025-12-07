// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Memory Allocator Base Class (UE5-style)

#pragma once

#include "Core/CoreTypes.h"
#include <cstddef>  // for std::max_align_t

// Default alignment for memory allocations
#ifndef DEFAULT_ALIGNMENT
    #define DEFAULT_ALIGNMENT 16
#endif

namespace MonsterRender {

/**
 * FMalloc - Base class for memory allocators (similar to UE5's FMalloc)
 * 
 * Provides the interface that all memory allocators must implement.
 * Reference: Engine/Source/Runtime/Core/Public/HAL/MallocBinned2.h
 */
class FMalloc {
public:
    virtual ~FMalloc() = default;

    /**
     * Allocates memory with specified size and alignment
     * @param Size - Number of bytes to allocate
     * @param Alignment - Required alignment (must be power of 2)
     * @return Pointer to allocated memory, or nullptr on failure
     */
    virtual void* Malloc(SIZE_T Size, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;

    /**
     * Reallocates memory to a new size
     * @param Original - Pointer to original allocation
     * @param Size - New size in bytes
     * @param Alignment - Required alignment
     * @return Pointer to reallocated memory
     */
    virtual void* Realloc(void* Original, SIZE_T Size, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;

    /**
     * Frees previously allocated memory
     * @param Original - Pointer to memory to free
     */
    virtual void Free(void* Original) = 0;

    /**
     * Returns the size of an allocation
     * @param Original - Pointer to allocated memory
     * @return Size of allocation in bytes, or 0 if unknown
     */
    virtual SIZE_T GetAllocationSize(void* Original) { return 0; }

    /**
     * Validates the allocator's integrity (for debugging)
     * @return true if allocator is in valid state
     */
    virtual bool ValidateHeap() { return true; }

    /**
     * Returns total allocated memory in bytes
     */
    virtual uint64 GetTotalAllocatedMemory() { return 0; }

    /**
     * Trims unused memory back to the system
     */
    virtual void Trim() {}

    /**
     * Gets allocator stats for profiling/debugging
     */
    struct FMemoryStats {
        uint64 TotalAllocated;
        uint64 TotalReserved;
        uint64 AllocationCount;
        uint64 FreeCount;
    };

    virtual void GetMemoryStats(FMemoryStats& OutStats) {
        OutStats = FMemoryStats{};
    }

    // Default alignment for memory allocations (16 bytes for x64)
    static const unsigned int DefaultAlignment = 16;
};

} // namespace MonsterRender

