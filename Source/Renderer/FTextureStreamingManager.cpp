// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Texture Streaming Manager Implementation

#include "Renderer/FTextureStreamingManager.h"
#include "Core/HAL/FMemory.h"
#include "Core/Log.h"
#include <algorithm>
#include <thread>
#include <future>

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
        MR_LOG_WARNING("FTextureStreamingManager already initialized");
        return true;
    }

    MR_LOG_INFO("Initializing FTextureStreamingManager with pool size: " + 
                std::to_string(TexturePoolSizeBytes / 1024 / 1024) + "MB");

    // Create texture pool
    TexturePool = MakeUnique<FTexturePool>(TexturePoolSizeBytes);
    PoolSize = TexturePoolSizeBytes;

    bInitialized = true;
    MR_LOG_INFO("FTextureStreamingManager initialized successfully");
    return true;
}

void FTextureStreamingManager::Shutdown() {
    if (!bInitialized) return;

    MR_LOG_INFO("Shutting down FTextureStreamingManager...");

    std::scoped_lock lock(StreamingMutex);

    // Clear all streaming textures
    StreamingTextures.clear();

    // Release texture pool
    TexturePool.reset();

    bInitialized = false;
    MR_LOG_INFO("FTextureStreamingManager shut down");
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

    StreamingTextures.push_back(st);

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
        StreamingTextures.erase(it, StreamingTextures.end());
        MR_LOG_INFO("Texture unregistered: " + Texture->FilePath);
    }
}

void FTextureStreamingManager::UpdateResourceStreaming(float DeltaTime) {
    if (!bInitialized) return;

    std::scoped_lock lock(StreamingMutex);

    // Step 1: Update priorities based on distance, importance, etc.
    UpdatePriorities();

    // Step 2: Process streaming requests (load/unload mips)
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
        // 添加安全检查
        if (!st.Texture) {
            MR_LOG_WARNING("GetStreamingStats: Found null texture pointer");
            continue;
        }
        
        // 验证纹理对象是否有效
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
        // 添加更严格的安全检查
        if (!st.Texture) {
            MR_LOG_WARNING("UpdatePriorities: Found null texture pointer");
            continue;
        }
        
        // 验证纹理对象是否有效
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
    std::vector<FStreamingTexture*> sortedTextures;
    for (auto& st : StreamingTextures) {
        sortedTextures.push_back(&st);
    }

    std::sort(sortedTextures.begin(), sortedTextures.end(),
        [](const FStreamingTexture* a, const FStreamingTexture* b) {
            return a->Priority > b->Priority;
        });

    // Process requests in priority order
    for (auto* st : sortedTextures) {
        // 添加安全检查
        if (!st || !st->Texture) {
            MR_LOG_WARNING("ProcessStreamingRequests: Found null pointer");
            continue;
        }
        
        // 验证纹理对象是否有效
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
            MR_LOG_WARNING("Cannot stream in mips: insufficient memory");
            return;
        }
    }

    // Allocate memory from pool
    void* mipMemory = TexturePool->Allocate(sizeNeeded, 256);
    if (!mipMemory) {
        MR_LOG_WARNING("Failed to allocate memory for mip streaming");
        return;
    }

    // FIXED: 使用同步加载避免野指针访问
    // 原因：detached thread 可能在 texture 对象销毁后仍在运行
    // TODO: 使用 FAsyncFileIO 实现真正的异步加载（带生命周期管理）
    LoadMipsFromDisk(texture, currentMips, targetMips, mipMemory);
    
    // 更新 resident mips
    StreamingTexture->ResidentMips = targetMips;

    MR_LOG_INFO("Streaming in mips: " + texture->FilePath + 
                " (Mips " + std::to_string(currentMips) + " -> " + std::to_string(targetMips) + ")");
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
    std::vector<FStreamingTexture*> evictionCandidates;
    
    for (auto& st : StreamingTextures) {
        if (st.Priority < 0.5f && st.ResidentMips > 1) {
            evictionCandidates.push_back(&st);
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

void FTextureStreamingManager::LoadMipsFromDisk(FTexture* Texture, uint32 StartMip, uint32 EndMip, void* DestMemory) {
    // Reference: UE5's FTextureStreamIn::DoLoadNewMipsFromDisk()
    
    // TODO: Implement actual file IO
    // For now, just simulate loading with delay
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Update texture's resident mips
    Texture->ResidentMips = EndMip;

    MR_LOG_INFO("Loaded mips from disk: " + Texture->FilePath + 
                " (Mips " + std::to_string(StartMip) + " -> " + std::to_string(EndMip) + ")");
}

float FTextureStreamingManager::CalculateScreenSize(FTexture* Texture) {
    if (!Texture) return 0.0f;

    // Simplified screen size calculation
    // TODO: Implement proper screen space projection
    return std::min(1.0f, (float)Texture->Width / 1920.0f);
}

SIZE_T FTextureStreamingManager::CalculateMipSize(FTexture* Texture, uint32 StartMip, uint32 EndMip) {
    // 添加安全检查
    if (!Texture) {
        MR_LOG_WARNING("CalculateMipSize: Texture pointer is null");
        return 0;
    }

    // 验证指针是否仍然有效（基础检查）
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

