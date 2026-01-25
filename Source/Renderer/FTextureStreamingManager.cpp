// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Texture Streaming Manager Implementation

#include "Renderer/FTextureStreamingManager.h"
#include "Renderer/FAsyncTextureLoadRequest.h"
#include "Core/HAL/FMemory.h"
#include "Core/Log.h"
#include <algorithm>

namespace MonsterRender {

// Placeholder FTexture class (will be implemented with RHI system)
class FTexture {
public:
    uint32 Width = 0;
    uint32 Height = 0;
    uint32 TotalMipLevels = 0;
    uint32 ResidentMips = 0;
    SIZE_T SizePerMip[16] = {};  // Size of each mip level
    void* MipData[16] = {};      // Mip data pointers
    String FilePath;
    
    SIZE_T GetTotalSize() const {
        SIZE_T total = 0;
        for (uint32 i = 0; i < ResidentMips; ++i) {
            total += SizePerMip[i];
        }
        return total;
    }
};

// ===== FTextureStreamingManager Implementation =====

FTextureStreamingManager& FTextureStreamingManager::Get() {
    static FTextureStreamingManager Instance;
    return Instance;
}

FTextureStreamingManager::FTextureStreamingManager()
    : PoolSize(0)
    , AllocatedMemory(0)
    , bInitialized(false)
{
}

FTextureStreamingManager::~FTextureStreamingManager() {
    Shutdown();
}

bool FTextureStreamingManager::Initialize(SIZE_T TexturePoolSizeBytes) {
    if (bInitialized) {
        MR_LOG(LogTextureStreaming, Warning, "FTextureStreamingManager already initialized");
        return true;
    }

    MR_LOG(LogTextureStreaming, Log, "Initializing FTextureStreamingManager with pool size: %llu MB", 
           TexturePoolSizeBytes / 1024 / 1024);

    // Create texture pool
    TexturePool = MakeUnique<FTexturePool>(TexturePoolSizeBytes);
    PoolSize = TexturePoolSizeBytes;

    // Initialize async load manager
    FAsyncTextureLoadManager::Get().Initialize(4); // Max 4 concurrent loads

    bInitialized = true;
    MR_LOG(LogTextureStreaming, Log, "FTextureStreamingManager initialized successfully");
    return true;
}

void FTextureStreamingManager::Shutdown() {
    if (!bInitialized) return;

    MR_LOG(LogTextureStreaming, Log, "Shutting down FTextureStreamingManager...");

    std::scoped_lock lock(StreamingMutex);

    // Shutdown async load manager first
    FAsyncTextureLoadManager::Get().Shutdown();

    // Clear all streaming textures
    StreamingTextures.Clear();

    // Release texture pool
    TexturePool.Reset();

    bInitialized = false;
    MR_LOG(LogTextureStreaming, Log, "FTextureStreamingManager shut down");
}

void FTextureStreamingManager::RegisterTexture(FTexture* Texture) {
    if (!Texture || !bInitialized) return;

    std::scoped_lock lock(StreamingMutex);

    // Check if already registered
    for (const auto& st : StreamingTextures) {
        if (st.Texture == Texture) {
            MR_LOG_WARNING("Texture already registered");
            return;
        }
    }

    // Create streaming texture entry
    FStreamingTexture st;
    st.Texture = Texture;
    st.ResidentMips = Texture->ResidentMips;
    st.RequestedMips = Texture->TotalMipLevels;  // Request all mips initially
    st.Priority = 1.0f;
    st.Distance = 1000.0f;  // Default distance

    StreamingTextures.Add(st);

    MR_LOG_INFO("Texture registered: " + Texture->FilePath + 
                " (Mips: " + std::to_string(Texture->TotalMipLevels) + ")");
}

void FTextureStreamingManager::UnregisterTexture(FTexture* Texture) {
    if (!Texture || !bInitialized) return;

    std::scoped_lock lock(StreamingMutex);

    // Remove from streaming list
    auto it = std::remove_if(StreamingTextures.begin(), StreamingTextures.end(),
        [Texture](const FStreamingTexture& st) {
            return st.Texture == Texture;
        });

    if (it != StreamingTextures.end()) {
        StreamingTextures.SetNum(it - StreamingTextures.begin());
        MR_LOG_INFO("Texture unregistered: " + Texture->FilePath);
    }
}

void FTextureStreamingManager::UpdateResourceStreaming(float DeltaTime) {
    if (!bInitialized) return;

    // Step 1: Process completed async load requests (outside lock)
    FAsyncTextureLoadManager::Get().ProcessCompletedRequests(10);

    std::scoped_lock lock(StreamingMutex);

    // Step 2: Update priorities based on distance, importance, etc.
    UpdatePriorities();

    // Step 3: Process streaming requests (load/unload mips)
    ProcessStreamingRequests();
}

SIZE_T FTextureStreamingManager::GetAllocatedMemory() const {
    if (!TexturePool) return 0;
    return TexturePool->GetUsedSize();
}

SIZE_T FTextureStreamingManager::GetPoolSize() const {
    return PoolSize;
}

void FTextureStreamingManager::SetPoolSize(SIZE_T NewSize) {
    if (NewSize == PoolSize) return;

    MR_LOG_INFO("Changing texture pool size: " + 
                std::to_string(PoolSize / 1024 / 1024) + "MB -> " + 
                std::to_string(NewSize / 1024 / 1024) + "MB");

    // TODO: Implement pool resize (requires careful migration of existing allocations)
    // For now, just update the size
    PoolSize = NewSize;
}

void FTextureStreamingManager::GetStreamingStats(FStreamingStats& OutStats) {
    std::scoped_lock lock(StreamingMutex);

    OutStats.NumStreamingTextures = 0;
    OutStats.NumResidentTextures = 0;
    OutStats.PendingStreamIn = 0;
    OutStats.PendingStreamOut = 0;

    for (const auto& st : StreamingTextures) {
        // Add safety check
        if (!st.Texture) {
            MR_LOG_WARNING("GetStreamingStats: Found null texture pointer");
            continue;
        }
        
        // Validate texture object
        if (st.Texture->TotalMipLevels == 0 || st.Texture->TotalMipLevels > 16) {
            MR_LOG_ERROR("GetStreamingStats: Invalid texture detected");
            continue;
        }
        if (st.RequestedMips != st.ResidentMips) {
            ++OutStats.NumStreamingTextures;
            
            if (st.RequestedMips > st.ResidentMips) {
                // Streaming in
                OutStats.PendingStreamIn += CalculateMipSize(st.Texture, st.ResidentMips, st.RequestedMips);
            } else {
                // Streaming out
                OutStats.PendingStreamOut += CalculateMipSize(st.Texture, st.RequestedMips, st.ResidentMips);
            }
        } else {
            ++OutStats.NumResidentTextures;
        }
    }

    if (TexturePool) {
        OutStats.AllocatedMemory = TexturePool->GetUsedSize();
        OutStats.PoolSize = TexturePool->GetTotalSize();
    } else {
        OutStats.AllocatedMemory = 0;
        OutStats.PoolSize = 0;
    }

    // Calculate streaming bandwidth (simplified)
    OutStats.StreamingBandwidth = 0.0f;  // TODO: Track actual bandwidth
}

// Private methods

void FTextureStreamingManager::UpdatePriorities() {
    // Reference: UE5's FStreamingManagerTexture::UpdatePriorities()
    
    for (auto& st : StreamingTextures) {
        // Add stricter safety check
        if (!st.Texture) {
            MR_LOG_WARNING("UpdatePriorities: Found null texture pointer");
            continue;
        }
        
        // Validate texture object
        if (st.Texture->TotalMipLevels == 0 || st.Texture->TotalMipLevels > 16) {
            MR_LOG_ERROR("UpdatePriorities: Invalid texture detected, skipping");
            continue;
        }

        // Calculate priority based on multiple factors:
        // 1. Distance from camera (closer = higher priority)
        // 2. Screen size (larger on screen = higher priority)
        // 3. Time since last use
        // 4. LOD bias

        float distanceFactor = 1.0f / std::max(1.0f, st.Distance);
        float screenSizeFactor = CalculateScreenSize(st.Texture);
        float timeFactor = 1.0f;  // TODO: Track time since last use

        // Combined priority (0.0 = lowest, 1.0+ = highest)
        st.Priority = distanceFactor * screenSizeFactor * timeFactor;

        // Determine requested mips based on priority
        if (st.Priority > 0.8f) {
            st.RequestedMips = st.Texture->TotalMipLevels;  // All mips
        } else if (st.Priority > 0.5f) {
            st.RequestedMips = std::max(1u, st.Texture->TotalMipLevels - 2);  // All but 2 lowest
        } else if (st.Priority > 0.2f) {
            st.RequestedMips = std::max(1u, st.Texture->TotalMipLevels / 2);  // Half mips
        } else {
            st.RequestedMips = 1;  // Only top mip
        }
    }
}

void FTextureStreamingManager::ProcessStreamingRequests() {
    // Reference: UE5's FStreamingManagerTexture::ProcessStreamingRequests()
    
    // Sort by priority (highest first)
    TArray<FStreamingTexture*> sortedTextures;
    for (auto& st : StreamingTextures) {
        sortedTextures.Add(&st);
    }

    std::sort(sortedTextures.begin(), sortedTextures.end(),
        [](const FStreamingTexture* a, const FStreamingTexture* b) {
            return a->Priority > b->Priority;
        });

    // Process requests in priority order
    for (auto* st : sortedTextures) {
        // Add safety check
        if (!st || !st->Texture) {
            MR_LOG_WARNING("ProcessStreamingRequests: Found null pointer");
            continue;
        }
        
        // Validate texture object
        if (st->Texture->TotalMipLevels == 0 || st->Texture->TotalMipLevels > 16) {
            MR_LOG_ERROR("ProcessStreamingRequests: Invalid texture detected");
            continue;
        }

        if (st->RequestedMips > st->ResidentMips) {
            // Stream in more mips
            StreamInMips(st);
        } else if (st->RequestedMips < st->ResidentMips) {
            // Stream out mips
            StreamOutMips(st);
        }
    }
}

void FTextureStreamingManager::StreamInMips(FStreamingTexture* StreamingTexture) {
    if (!StreamingTexture || !StreamingTexture->Texture) return;

    FTexture* texture = StreamingTexture->Texture;
    uint32 currentMips = StreamingTexture->ResidentMips;
    uint32 targetMips = StreamingTexture->RequestedMips;

    if (currentMips >= targetMips) return;

    // Calculate size needed for new mips
    SIZE_T sizeNeeded = CalculateMipSize(texture, currentMips, targetMips);

    // Check if we have enough space in pool
    if (TexturePool->GetFreeSize() < sizeNeeded) {
        // Try to evict low-priority textures
        if (!EvictLowPriorityTextures(sizeNeeded)) {
            MR_LOG(LogTextureStreaming, Warning, "Cannot stream in mips: insufficient memory");
            return;
        }
    }

    // Allocate memory from pool
    void* mipMemory = TexturePool->Allocate(sizeNeeded, 256);
    if (!mipMemory) {
        MR_LOG(LogTextureStreaming, Warning, "Failed to allocate memory for mip streaming");
        return;
    }

    // Start async load (non-blocking)
    StartAsyncMipLoad(texture, currentMips, targetMips, mipMemory);

    MR_LOG(LogTextureStreaming, Log, "Started async streaming in mips (Mips %u -> %u)", 
           currentMips, targetMips);
}

void FTextureStreamingManager::StreamOutMips(FStreamingTexture* StreamingTexture) {
    if (!StreamingTexture || !StreamingTexture->Texture) return;

    FTexture* texture = StreamingTexture->Texture;
    uint32 currentMips = StreamingTexture->ResidentMips;
    uint32 targetMips = StreamingTexture->RequestedMips;

    if (currentMips <= targetMips) return;

    // Free memory for removed mips
    for (uint32 mip = targetMips; mip < currentMips; ++mip) {
        if (texture->MipData[mip]) {
            TexturePool->Free(texture->MipData[mip]);
            texture->MipData[mip] = nullptr;
        }
    }

    StreamingTexture->ResidentMips = targetMips;
    texture->ResidentMips = targetMips;

    MR_LOG_INFO("Streamed out mips: " + texture->FilePath + 
                " (Mips " + std::to_string(currentMips) + " -> " + std::to_string(targetMips) + ")");
}

bool FTextureStreamingManager::EvictLowPriorityTextures(SIZE_T RequiredSize) {
    // Find textures with lowest priority and evict their mips
    TArray<FStreamingTexture*> evictionCandidates;
    
    for (auto& st : StreamingTextures) {
        if (st.Priority < 0.5f && st.ResidentMips > 1) {
            evictionCandidates.Add(&st);
        }
    }

    // Sort by priority (lowest first)
    std::sort(evictionCandidates.begin(), evictionCandidates.end(),
        [](const FStreamingTexture* a, const FStreamingTexture* b) {
            return a->Priority < b->Priority;
        });

    // Evict mips until we have enough space
    SIZE_T freedSpace = 0;
    for (auto* st : evictionCandidates) {
        if (freedSpace >= RequiredSize) break;

        uint32 mipsToRemove = std::max(1u, st->ResidentMips / 2);
        uint32 newMipCount = st->ResidentMips - mipsToRemove;
        
        SIZE_T sizeFreed = CalculateMipSize(st->Texture, newMipCount, st->ResidentMips);
        StreamOutMips(st);
        
        freedSpace += sizeFreed;
    }

    return freedSpace >= RequiredSize;
}

void FTextureStreamingManager::StartAsyncMipLoad(FTexture* Texture, uint32 StartMip, uint32 EndMip, void* DestMemory) {
    // Reference: UE5's FTextureStreamIn::DoGetMipData()
    
    if (!Texture || !DestMemory) {
        MR_LOG(LogTextureStreaming, Error, "Invalid texture or destination memory for async load");
        return;
    }

    // Create async load request with completion callback
    auto request = MakeUnique<FAsyncTextureLoadRequest>(
        Texture,
        StartMip,
        EndMip,
        DestMemory,
        [this, Texture](bool bSuccess, void* LoadedData, SIZE_T DataSize) {
            // Completion callback (invoked on main thread)
            OnMipLoadComplete(Texture, bSuccess, LoadedData, DataSize);
        }
    );

    // Queue request to async load manager
    FAsyncTextureLoadManager::Get().QueueLoadRequest(std::move(request));

    MR_LOG(LogTextureStreaming, VeryVerbose, "Queued async mip load (Mips %u -> %u)", StartMip, EndMip);
}

void FTextureStreamingManager::OnMipLoadComplete(FTexture* Texture, bool bSuccess, void* LoadedData, SIZE_T DataSize) {
    // Reference: UE5's FTextureStreamIn::FinalizeNewMips()
    
    if (!Texture) {
        MR_LOG(LogTextureStreaming, Error, "Null texture in load completion callback");
        return;
    }

    if (!bSuccess) {
        MR_LOG(LogTextureStreaming, Error, "Async mip load failed for texture");
        // TODO: Free allocated memory from pool
        return;
    }

    // Find streaming texture entry
    std::scoped_lock lock(StreamingMutex);
    
    for (auto& st : StreamingTextures) {
        if (st.Texture == Texture) {
            // Update resident mips
            st.ResidentMips = st.RequestedMips;
            Texture->ResidentMips = st.RequestedMips;
            
            MR_LOG(LogTextureStreaming, Log, "Async mip load completed successfully (ResidentMips: %u)", 
                   st.ResidentMips);
            return;
        }
    }

    MR_LOG(LogTextureStreaming, Warning, "Texture not found in streaming list during load completion");
}

float FTextureStreamingManager::CalculateScreenSize(FTexture* Texture) {
    if (!Texture) return 0.0f;

    // Simplified screen size calculation
    // TODO: Implement proper screen space projection
    return std::min(1.0f, (float)Texture->Width / 1920.0f);
}

SIZE_T FTextureStreamingManager::CalculateMipSize(FTexture* Texture, uint32 StartMip, uint32 EndMip) {
    // Add safety check
    if (!Texture) {
        MR_LOG_WARNING("CalculateMipSize: Texture pointer is null");
        return 0;
    }

    // Validate pointer is still valid (basic check)
    if (Texture->TotalMipLevels == 0 || Texture->TotalMipLevels > 16) {
        MR_LOG_ERROR("CalculateMipSize: Invalid texture (TotalMipLevels = " + 
                     std::to_string(Texture->TotalMipLevels) + ")");
        return 0;
    }

    SIZE_T totalSize = 0;
    for (uint32 mip = StartMip; mip < EndMip && mip < Texture->TotalMipLevels; ++mip) {
        totalSize += Texture->SizePerMip[mip];
    }
    return totalSize;
}

} // namespace MonsterRender

