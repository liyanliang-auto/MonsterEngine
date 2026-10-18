// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Memory Operations Implementation

#include "Core/HAL/FMemory.h"
#include "Core/HAL/FMemoryManager.h"
#include <stdexcept>
#include <cstdlib>

namespace MonsterRender {
namespace {
FMalloc& RequireAllocator() {
    FMalloc* Allocator = FMemoryManager::Get().GetAllocator();
    if (!Allocator) throw std::logic_error("FMemory requires initialized process runtime");
    return *Allocator;
}
}

void* FMemory::Malloc(SIZE_T Count, uint32 Alignment) {
    return RequireAllocator().Malloc(Count, Alignment);
}

void* FMemory::Realloc(void* Original, SIZE_T Count, uint32 Alignment) {
    return RequireAllocator().Realloc(Original, Count, Alignment);
}

void FMemory::Free(void* Original) {
    if (!Original) return;
    RequireAllocator().Free(Original);
}

SIZE_T FMemory::GetAllocSize(void* Original) {
    return RequireAllocator().GetAllocationSize(Original);
}

void* FMemory::SystemMalloc(SIZE_T Size) {
    // Use standard malloc for debug heap compatibility
    return std::malloc(Size);
}

void FMemory::SystemFree(void* Ptr) {
    // Use standard free for debug heap compatibility
    std::free(Ptr);
}

uint64 FMemory::GetTotalAllocatedMemory() {
    return RequireAllocator().GetTotalAllocatedMemory();
}

void FMemory::GetMemoryStats(FMemoryStats& OutStats) {
    FMalloc::FMemoryStats allocatorStats;
    RequireAllocator().GetMemoryStats(allocatorStats);
    // Copy the stats to our structure
    OutStats.TotalAllocated = allocatorStats.TotalAllocated;
    OutStats.TotalReserved = allocatorStats.TotalReserved;
    OutStats.AllocationCount = allocatorStats.AllocationCount;
    OutStats.FreeCount = allocatorStats.FreeCount;
}

} // namespace MonsterRender

