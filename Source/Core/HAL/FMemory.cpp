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
#if PLATFORM_WINDOWS
    return ::_aligned_malloc(Size, DEFAULT_ALIGNMENT);
#else
    return ::aligned_alloc(DEFAULT_ALIGNMENT, Size);
#endif
}

void FMemory::SystemFree(void* Ptr) {
#if PLATFORM_WINDOWS
    ::_aligned_free(Ptr);
#else
    ::free(Ptr);
#endif
}

uint64 FMemory::GetTotalAllocatedMemory() {
    return FMemoryManager::Get().GetAllocator()->GetTotalAllocatedMemory();
}

void FMemory::GetMemoryStats(FMemoryStats& OutStats) {
    FMemoryManager::Get().GetAllocator()->GetMemoryStats(OutStats);
}

} // namespace MonsterRender

