// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Texture Streaming Manager Implementation

#include "Renderer/FTextureStreamingManager.h"
#include "Renderer/FAsyncTextureLoadRequest.h"
#include "Engine/Texture/Texture2D.h"
#include "Core/HAL/FMemory.h"
#include "Core/Log.h"
#include <algorithm>

namespace MonsterRender {

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
    StreamingTextures.Empty();

    // Release texture pool
    TexturePool.Reset();

    bInitialized = false;
    MR_LOG(LogTextureStreaming, Log, "FTextureStreamingManager shut down");
}

void FTextureStreamingManager::RegisterTexture(MonsterEngine::FTexture2D* Texture) {
    if (!Texture || !bInitialized) return;

    std::scoped_lock lock(StreamingMutex);

    // Check if already registered
    for (const auto& st : StreamingTextures) {
        if (st.Texture == Texture) {
            MR_LOG(LogTextureStreaming, Warning, "Texture already registered");
            return;
        }
    }

    // Create streaming texture entry
    FStreamingTexture st;
    st.Texture = Texture;
    st.ResidentMips = Texture->getResidentMips();
    st.RequestedMips = Texture->getTotalMips();  // Request all mips initially
    st.Priority = 1.0f;
    st.Distance = 1000.0f;  // Default distance

    StreamingTextures.Add(st);

    MR_LOG(LogTextureStreaming, Log, "Texture registered: %s (Mips: %u)", 
           Texture->getFilePath().c_str(), Texture->getTotalMips());
}

void FTextureStreamingManager::UnregisterTexture(MonsterEngine::FTexture2D* Texture) {
    if (!Texture || !bInitialized) return;

    std::scoped_lock lock(StreamingMutex);

    // Remove from streaming list
    auto it = std::remove_if(StreamingTextures.begin(), StreamingTextures.end(),
        [Texture](const FStreamingTexture& st) {
            return st.Texture == Texture;
        });

    if (it != StreamingTextures.end()) {
        StreamingTextures.SetNum(it - StreamingTextures.begin());
        MR_LOG(LogTextureStreaming, Log, "Texture unregistered: %s", Texture->getFilePath().c_str());
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

    MR_LOG(LogTextureStreaming, Log, "Changing texture pool size: %llu MB -> %llu MB", 
           PoolSize / 1024 / 1024, NewSize / 1024 / 1024);

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
            MR_LOG(LogTextureStreaming, Warning, "GetStreamingStats: Found null texture pointer");
            continue;
        }
        
        // Validate texture object
        uint32 totalMips = st.Texture->getTotalMips();
        if (totalMips == 0 || totalMips > 16) {
            MR_LOG(LogTextureStreaming, Error, "GetStreamingStats: Invalid texture detected");
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
            MR_LOG(LogTextureStreaming, Warning, "UpdatePriorities: Found null texture pointer");
            continue;
        }
        
        // Validate texture object
        uint32 totalMips = st.Texture->getTotalMips();
        if (totalMips == 0 || totalMips > 16) {
            MR_LOG(LogTextureStreaming, Error, "UpdatePriorities: Invalid texture detected, skipping");
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
            st.RequestedMips = totalMips;  // All mips
        } else if (st.Priority > 0.5f) {
            st.RequestedMips = std::max(1u, totalMips - 2);  // All but 2 lowest
        } else if (st.Priority > 0.2f) {
            st.RequestedMips = std::max(1u, totalMips / 2);  // Half mips
        } else {
            st.RequestedMips = 1;  // Only top mip
        }
    }
}

void FTextureStreamingManager::ProcessStreamingRequests() {
    // Reference: UE5's FStreamingManagerTexture::ProcessStreamingRequests()
    
    // Update pending async uploads first
    UpdatePendingAsyncUploads();
    
    // Sort by priority (highest first)
    TArray<FStreamingTexture*> sortedTextures;
    for (auto& st : StreamingTextures) {
        sortedTextures.Add(&st);
    }

    std::sort(sortedTextures.begin(), sortedTextures.end(),
        [](const FStreamingTexture* a, const FStreamingTexture* b) {
            return a->Priority > b->Priority;
        });

    // Count current async uploads
    uint32 currentAsyncUploads = 0;
    for (const auto& st : StreamingTextures) {
        if (st.bHasPendingAsyncUpload) {
            currentAsyncUploads++;
        }
    }

    // Process requests in priority order
    for (auto* st : sortedTextures) {
        // Add safety check
        if (!st || !st->Texture) {
            MR_LOG(LogTextureStreaming, Warning, "ProcessStreamingRequests: Found null pointer");
            continue;
        }
        
        // Validate texture object
        if (st->Texture->getTotalMips() == 0 || st->Texture->getTotalMips() > 16) {
            MR_LOG(LogTextureStreaming, Error, "ProcessStreamingRequests: Invalid texture detected");
            continue;
        }
        
        // Skip if already has pending async upload
        if (st->bHasPendingAsyncUpload) {
            continue;
        }

        if (st->RequestedMips > st->ResidentMips) {
            // Stream in more mips (use async if enabled and under limit)
            if (bUseAsyncUpload && currentAsyncUploads < MaxConcurrentAsyncUploads) {
                StreamInMipsAsync(st);
                currentAsyncUploads++;
            } else {
                StreamInMips(st);
            }
        } else if (st->RequestedMips < st->ResidentMips) {
            // Stream out mips
            StreamOutMips(st);
        }
    }
}

void FTextureStreamingManager::StreamInMips(FStreamingTexture* StreamingTexture) {
    if (!StreamingTexture || !StreamingTexture->Texture) return;

    ::MonsterEngine::FTexture2D* texture = StreamingTexture->Texture;
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

    MonsterEngine::FTexture2D* texture = StreamingTexture->Texture;
    uint32 currentMips = StreamingTexture->ResidentMips;
    uint32 targetMips = StreamingTexture->RequestedMips;

    if (currentMips <= targetMips) return;

    // Free memory for removed mips
    for (uint32 mip = targetMips; mip < currentMips; ++mip) {
        void* mipData = texture->getMipData(mip);
        if (mipData) {
            TexturePool->Free(mipData);
        }
    }

    StreamingTexture->ResidentMips = targetMips;
    // Update texture's resident mips
    texture->updateResidentMips(targetMips, nullptr);

    MR_LOG(LogTextureStreaming, Log, "Streamed out mips: %s (Mips %u -> %u)", 
           texture->getFilePath().c_str(), currentMips, targetMips);
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

void FTextureStreamingManager::StartAsyncMipLoad(MonsterEngine::FTexture2D* Texture, uint32 StartMip, uint32 EndMip, void* DestMemory) {
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

void FTextureStreamingManager::OnMipLoadComplete(MonsterEngine::FTexture2D* Texture, bool bSuccess, void* LoadedData, SIZE_T DataSize) {
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
            uint32 startMip = st.ResidentMips;
            uint32 endMip = st.RequestedMips;
            
            // Prepare mip data array
            uint32 numMips = endMip - startMip;
            void** mipDataArray = static_cast<void**>(MonsterRender::FMemory::Malloc(sizeof(void*) * numMips));
            
            // Calculate mip data pointers from loaded data
            uint8* dataPtr = static_cast<uint8*>(LoadedData);
            for (uint32 i = 0; i < numMips; ++i) {
                mipDataArray[i] = dataPtr;
                SIZE_T mipSize = Texture->getMipSize(startMip + i);
                dataPtr += mipSize;
            }
            
            // Upload to GPU asynchronously if enabled
            if (bUseAsyncUpload) {
                TArray<uint64> fenceValues;
                bool uploadSuccess = Texture->uploadMipDataAsync(startMip, endMip, mipDataArray, &fenceValues);
                
                if (uploadSuccess) {
                    // Track async upload
                    st.bHasPendingAsyncUpload = true;
                    st.PendingFenceValues = fenceValues;
                    st.PendingUploadStartMip = startMip;
                    st.PendingUploadEndMip = endMip;
                    
                    MR_LOG(LogTextureStreaming, Verbose, "Started async GPU upload for %s (Mips %u -> %u, %u fences)", 
                           Texture->getFilePath().c_str(), startMip, endMip, static_cast<uint32>(fenceValues.size()));
                } else {
                    MR_LOG(LogTextureStreaming, Error, "Failed to start async GPU upload for %s", 
                           Texture->getFilePath().c_str());
                }
            } else {
                // Synchronous upload
                bool uploadSuccess = Texture->uploadMipData(startMip, endMip, mipDataArray);
                
                if (uploadSuccess) {
                    st.ResidentMips = endMip;
                    Texture->updateResidentMips(endMip, mipDataArray);
                    
                    MR_LOG(LogTextureStreaming, Log, "Sync GPU upload completed for %s (ResidentMips: %u)", 
                           Texture->getFilePath().c_str(), st.ResidentMips);
                } else {
                    MR_LOG(LogTextureStreaming, Error, "Failed to upload mips for %s", 
                           Texture->getFilePath().c_str());
                }
            }
            
            MonsterRender::FMemory::Free(mipDataArray);
            return;
        }
    }

    MR_LOG(LogTextureStreaming, Warning, "Texture not found in streaming list during load completion");
}

float FTextureStreamingManager::CalculateScreenSize(MonsterEngine::FTexture2D* Texture) {
    if (!Texture) return 0.0f;

    // Simplified screen size calculation
    // TODO: Implement proper screen space projection
    return std::min(1.0f, (float)Texture->getWidth() / 1920.0f);
}

SIZE_T FTextureStreamingManager::CalculateMipSize(MonsterEngine::FTexture2D* Texture, uint32 StartMip, uint32 EndMip) {
    // Add safety check
    if (!Texture) {
        MR_LOG(LogTextureStreaming, Warning, "CalculateMipSize: Texture pointer is null");
        return 0;
    }

    uint32 totalMips = Texture->getTotalMips();
    // Validate pointer is still valid (basic check)
    if (totalMips == 0 || totalMips > 16) {
        MR_LOG(LogTextureStreaming, Error, "CalculateMipSize: Invalid texture (TotalMipLevels = %u)", 
               totalMips);
        return 0;
    }

    SIZE_T totalSize = 0;
    for (uint32 mip = StartMip; mip < EndMip && mip < totalMips; ++mip) {
        totalSize += Texture->getMipSize(mip);
    }
    return totalSize;
}

} // namespace MonsterRender

