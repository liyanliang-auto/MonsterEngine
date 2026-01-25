// Copyright Monster Engine. All Rights Reserved.
// Texture Streaming Manager - Async Upload Implementation

#include "Renderer/FTextureStreamingManager.h"
#include "Renderer/FAsyncTextureLoadRequest.h"
#include "Engine/Texture/Texture2D.h"
#include "Core/Log.h"
#include <algorithm>

DEFINE_LOG_CATEGORY_STATIC(LogTextureStreamingAsync, Log, All);

namespace MonsterRender {

using namespace MonsterEngine;

/**
 * Stream in mips asynchronously (non-blocking)
 * Reference: UE5 async texture streaming
 */
void FTextureStreamingManager::StreamInMipsAsync(FStreamingTexture* StreamingTexture)
{
    if (!StreamingTexture || !StreamingTexture->Texture) {
        return;
    }

    FTexture2D* texture = StreamingTexture->Texture;
    uint32 currentMips = StreamingTexture->ResidentMips;
    uint32 targetMips = StreamingTexture->RequestedMips;

    if (currentMips >= targetMips) {
        return;
    }

    // Calculate size needed for new mips
    SIZE_T sizeNeeded = CalculateMipSize(texture, currentMips, targetMips);

    // Check if we have enough space in pool
    if (TexturePool->GetFreeSize() < sizeNeeded) {
        // Try to evict low-priority textures
        if (!EvictLowPriorityTextures(sizeNeeded)) {
            MR_LOG(LogTextureStreamingAsync, Warning, "Cannot async stream in mips: insufficient memory");
            return;
        }
    }

    // Allocate memory from pool
    void* mipMemory = TexturePool->Allocate(sizeNeeded, 256);
    if (!mipMemory) {
        MR_LOG(LogTextureStreamingAsync, Warning, "Failed to allocate memory for async mip streaming");
        return;
    }

    // Start async load (this will load mip data from disk)
    StartAsyncMipLoad(texture, currentMips, targetMips, mipMemory);
    
    // Note: The actual GPU upload will happen in OnMipLoadComplete
    // which will call uploadMipDataAsync on the texture
    
    MR_LOG(LogTextureStreamingAsync, Verbose, "Started async streaming in mips: %s (Mips %u -> %u)", 
           texture->getFilePath().c_str(), currentMips, targetMips);
}

/**
 * Update pending async uploads
 * Checks fence status and completes uploads
 */
void FTextureStreamingManager::UpdatePendingAsyncUploads()
{
    for (auto& st : StreamingTextures) {
        if (!st.bHasPendingAsyncUpload) {
            continue;
        }
        
        if (!st.Texture) {
            st.bHasPendingAsyncUpload = false;
            continue;
        }
        
        // Check if all fences are complete
        if (IsAsyncUploadComplete(&st)) {
            // Upload complete, update resident mips
            st.ResidentMips = st.PendingUploadEndMip;
            st.bHasPendingAsyncUpload = false;
            st.PendingFenceValues.clear();
            
            MR_LOG(LogTextureStreamingAsync, Verbose, "Async upload completed: %s (%u mips resident)", 
                   st.Texture->getFilePath().c_str(), st.ResidentMips);
        }
    }
}

/**
 * Check if async upload is complete
 */
bool FTextureStreamingManager::IsAsyncUploadComplete(FStreamingTexture* StreamingTexture)
{
    if (!StreamingTexture || !StreamingTexture->Texture) {
        return true;
    }
    
    if (!StreamingTexture->bHasPendingAsyncUpload) {
        return true;
    }
    
    // Check all fence values
    for (uint64 fenceValue : StreamingTexture->PendingFenceValues) {
        if (!StreamingTexture->Texture->isAsyncUploadComplete(fenceValue)) {
            return false; // At least one upload still pending
        }
    }
    
    return true; // All uploads complete
}

/**
 * Wait for async upload to complete (blocking)
 */
void FTextureStreamingManager::WaitForAsyncUpload(FStreamingTexture* StreamingTexture)
{
    if (!StreamingTexture || !StreamingTexture->Texture) {
        return;
    }
    
    if (!StreamingTexture->bHasPendingAsyncUpload) {
        return;
    }
    
    MR_LOG(LogTextureStreamingAsync, Verbose, "Waiting for async upload: %s", 
           StreamingTexture->Texture->getFilePath().c_str());
    
    // Wait for all fences
    for (uint64 fenceValue : StreamingTexture->PendingFenceValues) {
        StreamingTexture->Texture->waitForAsyncUpload(fenceValue);
    }
    
    // Mark as complete
    StreamingTexture->ResidentMips = StreamingTexture->PendingUploadEndMip;
    StreamingTexture->bHasPendingAsyncUpload = false;
    StreamingTexture->PendingFenceValues.clear();
    
    MR_LOG(LogTextureStreamingAsync, Verbose, "Async upload wait completed: %s", 
           StreamingTexture->Texture->getFilePath().c_str());
}

} // namespace MonsterRender
