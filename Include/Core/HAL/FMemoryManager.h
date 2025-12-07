// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Memory Manager (UE5-style)

#pragma once

#include "Core/HAL/FMalloc.h"
#include "Core/HAL/FMallocBinned2.h"
#include "Core/Templates/UniquePtr.h"
#include <memory>

namespace MonsterRender {

// Use MonsterEngine smart pointers
using MonsterEngine::TUniquePtr;
using MonsterEngine::MakeUnique;

/**
 * FMemoryManager - Global memory manager singleton (similar to UE5)
 * 
 * Reference: UE5 Engine/Source/Runtime/Core/Private/HAL/MallocBinned2.cpp
 * 
 * Manages the global allocator and provides centralized memory management
 */
class FMemoryManager {
public:
    // Singleton access
    static FMemoryManager& Get();

    // Initialize the memory system
    bool Initialize();
    
    // Shutdown the memory system
    void Shutdown();

    // Get the current allocator
    FMalloc* GetAllocator() const { return Allocator.get(); }

    // Set a custom allocator (takes ownership)
    void SetAllocator(TUniquePtr<FMalloc> NewAllocator);

    // Memory statistics
    struct FGlobalMemoryStats {
        uint64 TotalPhysicalMemory;    // Total system RAM
        uint64 AvailablePhysicalMemory; // Available RAM
        uint64 TotalVirtualMemory;      // Total virtual address space
        uint64 AvailableVirtualMemory;  // Available virtual space
        uint64 PageSize;                // System page size
        uint64 LargePageSize;           // Large/huge page size (if available)
    };

    void GetGlobalMemoryStats(FGlobalMemoryStats& OutStats);

    // Huge pages support
    bool IsHugePagesAvailable() const { return bHugePagesAvailable; }
    bool EnableHugePages(bool bEnable);

private:
    FMemoryManager();
    ~FMemoryManager();

    // Prevent copying
    FMemoryManager(const FMemoryManager&) = delete;
    FMemoryManager& operator=(const FMemoryManager&) = delete;

    // Detect system capabilities
    void DetectSystemCapabilities();

    TUniquePtr<FMalloc> Allocator;
    bool bInitialized = false;
    bool bHugePagesAvailable = false;
    bool bHugePagesEnabled = false;
};

} // namespace MonsterRender

