// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Memory Operations Implementation

#include "Core/HAL/FMemory.h"
#include "Core/HAL/FMemoryManager.h"

namespace MonsterRender {

void* FMemory::Malloc(SIZE_T Count, uint32 Alignment) {
    return FMemoryManager::Get().GetAllocator()->Malloc(Count, Alignment);
}

void* FMemory::Realloc(void* Original, SIZE_T Count, uint32 Alignment) {
    return FMemoryManager::Get().GetAllocator()->Realloc(Original, Count, Alignment);
}

void FMemory::Free(void* Original) {
    FMemoryManager::Get().GetAllocator()->Free(Original);
}

SIZE_T FMemory::GetAllocSize(void* Original) {
    return FMemoryManager::Get().GetAllocator()->GetAllocationSize(Original);
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
    return FMemoryManager::Get().GetAllocator()->GetTotalAllocatedMemory();
}

void FMemory::GetMemoryStats(FMemoryStats& OutStats) {
    FMalloc::FMemoryStats allocatorStats;
    FMemoryManager::Get().GetAllocator()->GetMemoryStats(allocatorStats);
    // Copy the stats to our structure
    OutStats.TotalAllocated = allocatorStats.TotalAllocated;
    OutStats.TotalReserved = allocatorStats.TotalReserved;
    OutStats.AllocationCount = allocatorStats.AllocationCount;
    OutStats.FreeCount = allocatorStats.FreeCount;
}

} // namespace MonsterRender

