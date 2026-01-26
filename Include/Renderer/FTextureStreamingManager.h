// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Texture Streaming Manager (UE5-style)

#pragma once

#include "Core/CoreTypes.h"
#include "Core/Templates/UniquePtr.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Engine/Texture/Texture2D.h"
#include <mutex>

namespace MonsterRender {

// Forward declarations
class FTexturePool;
class FAsyncTextureLoadManager;

/**
 * FTextureStreamingManager - Manages streaming of texture mip levels
 * 
 * Reference: UE5 Engine/Source/Runtime/Renderer/Private/Streaming/TextureStreamingManager.h
 * 
 * Responsibilities:
 * - Track textures requiring streaming
 * - Schedule mip level loads/unloads
 * - Manage texture memory budget
 * - Prioritize streaming based on distance/importance
 */
class FTextureStreamingManager {
public:
    static FTextureStreamingManager& Get();

    bool Initialize(SIZE_T TexturePoolSize);
    void Shutdown();

    // Texture registration
    void RegisterTexture(::MonsterEngine::FTexture2D* Texture);
    void UnregisterTexture(::MonsterEngine::FTexture2D* Texture);

    // Per-frame update
    void UpdateResourceStreaming(float DeltaTime);

    // Memory management
    SIZE_T GetAllocatedMemory() const;
    SIZE_T GetPoolSize() const;
    void SetPoolSize(SIZE_T NewSize);

    // Statistics
    struct FStreamingStats {
        uint32 NumStreamingTextures;    // Textures being streamed
        uint32 NumResidentTextures;     // Fully loaded textures
        SIZE_T AllocatedMemory;          // Current memory usage
        SIZE_T PoolSize;                 // Total pool size
        SIZE_T PendingStreamIn;          // Bytes to stream in
        SIZE_T PendingStreamOut;         // Bytes to stream out
        float StreamingBandwidth;        // MB/s
    };

    void GetStreamingStats(FStreamingStats& OutStats);

private:
    FTextureStreamingManager();
    ~FTextureStreamingManager();

    FTextureStreamingManager(const FTextureStreamingManager&) = delete;
    FTextureStreamingManager& operator=(const FTextureStreamingManager&) = delete;

    struct FStreamingTexture {
        ::MonsterEngine::FTexture2D* Texture;
        uint32 ResidentMips;       // Currently loaded mip count
        uint32 RequestedMips;      // Desired mip count
        float Priority;            // Streaming priority
        float Distance;            // Distance from camera
        
        // Async upload tracking (UE5-style)
        bool bHasPendingAsyncUpload = false;
        TArray<uint64> PendingFenceValues;
        uint32 PendingUploadStartMip = 0;
        uint32 PendingUploadEndMip = 0;
    };

    // Update streaming priorities
    void UpdatePriorities();
    
    // Process streaming requests
    void ProcessStreamingRequests();

    // Stream in/out methods
    void StreamInMips(FStreamingTexture* StreamingTexture);
    void StreamOutMips(FStreamingTexture* StreamingTexture);
    
    // Async upload methods (UE5-style)
    void StreamInMipsAsync(FStreamingTexture* StreamingTexture);
    void UpdatePendingAsyncUploads();
    bool IsAsyncUploadComplete(FStreamingTexture* StreamingTexture);
    void WaitForAsyncUpload(FStreamingTexture* StreamingTexture);
    
    // Helper methods
    bool EvictLowPriorityTextures(SIZE_T RequiredSize);
    void StartAsyncMipLoad(::MonsterEngine::FTexture2D* Texture, uint32 StartMip, uint32 EndMip, void* DestMemory);
    void OnMipLoadComplete(::MonsterEngine::FTexture2D* Texture, bool bSuccess, void* LoadedData, SIZE_T DataSize);
    float CalculateScreenSize(::MonsterEngine::FTexture2D* Texture);
    SIZE_T CalculateMipSize(::MonsterEngine::FTexture2D* Texture, uint32 StartMip, uint32 EndMip);

    TUniquePtr<FTexturePool> TexturePool;
    TArray<FStreamingTexture> StreamingTextures;
    std::mutex StreamingMutex;
    
    SIZE_T PoolSize;
    SIZE_T AllocatedMemory;
    bool bInitialized = false;
    
    // Async upload configuration (UE5-style)
    bool bUseAsyncUpload = true;              // Enable async uploads
    uint32 MaxConcurrentAsyncUploads = 4;     // Max parallel uploads per frame
};

/**
 * FTexturePool - Memory pool for streamed texture data
 * 
 * Reference: UE5 Engine/Source/Runtime/RenderCore/Public/TextureResource.h
 * 
 * Pre-allocated GPU memory pool for texture streaming
 */
class FTexturePool {
public:
    FTexturePool(SIZE_T PoolSize);
    ~FTexturePool();

    // Allocate texture memory from pool
    void* Allocate(SIZE_T Size, SIZE_T Alignment = 256);
    
    // Free texture memory back to pool
    void Free(void* Ptr);

    // Get allocation size
    SIZE_T GetAllocationSize(void* Ptr);

    // Pool statistics
    SIZE_T GetTotalSize() const { return TotalSize; }
    SIZE_T GetUsedSize() const { return UsedSize; }
    SIZE_T GetFreeSize() const { return TotalSize - UsedSize; }

    // Defragmentation
    void Compact();

private:
    struct FFreeRegion {
        SIZE_T Offset;
        SIZE_T Size;
        FFreeRegion* Next;
    };

    struct FAllocation {
        SIZE_T Offset;
        SIZE_T Size;
    };

    void* PoolMemory;
    SIZE_T TotalSize;
    SIZE_T UsedSize;
    
    FFreeRegion* FreeList;
    TMap<void*, FAllocation> Allocations;
    std::mutex PoolMutex;

    // Internal helpers
    void* AllocateFromFreeList(SIZE_T Size, SIZE_T Alignment);
    void AddToFreeList(SIZE_T Offset, SIZE_T Size);
    void MergeFreeRegions();
};

} // namespace MonsterRender

