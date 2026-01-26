#pragma once

/**
 * FAsyncTextureUploadManager.h
 * 
 * Asynchronous texture upload manager for streaming texture system
 * Supports parallel texture uploads without blocking the main thread
 * 
 * Reference: UE5 FRHIAsyncComputeCommandListImmediate
 */

#include "Core/CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Core/Templates/SharedPointer.h"
#include "RHI/RHIDefinitions.h"
#include <atomic>
#include <mutex>

// Forward declarations
namespace MonsterRender::RHI {
    class IRHIDevice;
    class IRHITexture;
}

namespace MonsterRender::RHI {

/**
 * Async texture upload request
 * Represents a single texture mip upload operation
 */
struct FAsyncTextureUploadRequest
{
    TSharedPtr<IRHITexture> Texture;
    uint32 MipLevel = 0;
    void* Data = nullptr;
    SIZE_T DataSize = 0;
    uint64 RequestID = 0;
    
    // Callback on completion (optional)
    TFunction<void(bool)> OnComplete;
    
    FAsyncTextureUploadRequest() = default;
    
    FAsyncTextureUploadRequest(
        TSharedPtr<IRHITexture> InTexture,
        uint32 InMipLevel,
        void* InData,
        SIZE_T InDataSize,
        uint64 InRequestID)
        : Texture(InTexture)
        , MipLevel(InMipLevel)
        , Data(InData)
        , DataSize(InDataSize)
        , RequestID(InRequestID)
    {}
};

/**
 * Upload fence for synchronization
 * Tracks completion status of async upload operations
 */
class FAsyncTextureUploadFence
{
public:
    FAsyncTextureUploadFence();
    ~FAsyncTextureUploadFence();
    
    /**
     * Check if the upload is complete
     */
    bool isComplete() const;
    
    /**
     * Wait for upload to complete (blocking)
     * @param timeoutMs Timeout in milliseconds (0 = infinite)
     * @return true if completed within timeout
     */
    bool wait(uint32 timeoutMs = 0);
    
    /**
     * Signal completion
     */
    void signal();
    
    /**
     * Reset fence for reuse
     */
    void reset();
    
    /**
     * Get fence ID
     */
    uint64 getFenceID() const { return m_fenceID; }
    
private:
    std::atomic<bool> m_isComplete;
    uint64 m_fenceID;
    static std::atomic<uint64> s_nextFenceID;
};

/**
 * Async texture upload manager
 * Manages asynchronous texture uploads with fence synchronization
 * Reference: UE5 FRHIAsyncComputeCommandListImmediate
 */
class FAsyncTextureUploadManager
{
public:
    FAsyncTextureUploadManager(IRHIDevice* device);
    ~FAsyncTextureUploadManager();
    
    /**
     * Initialize the upload manager
     */
    bool initialize();
    
    /**
     * Shutdown the upload manager
     */
    void shutdown();
    
    /**
     * Submit an async texture upload request
     * @param request Upload request
     * @param outFence Output fence for synchronization
     * @return true if submitted successfully
     */
    bool submitUpload(
        const FAsyncTextureUploadRequest& request,
        TSharedPtr<FAsyncTextureUploadFence>& outFence);
    
    /**
     * Submit multiple uploads in batch
     * @param requests Array of upload requests
     * @param outFence Output fence for all uploads
     * @return true if all submitted successfully
     */
    bool submitBatchUpload(
        const TArray<FAsyncTextureUploadRequest>& requests,
        TSharedPtr<FAsyncTextureUploadFence>& outFence);
    
    /**
     * Process pending uploads (call from render thread)
     * @param maxUploadsPerFrame Maximum uploads to process this frame
     */
    void processPendingUploads(uint32 maxUploadsPerFrame = 8);
    
    /**
     * Wait for all pending uploads to complete
     */
    void waitForAll();
    
    /**
     * Get number of pending uploads
     */
    uint32 getPendingUploadCount() const;
    
    /**
     * Check if manager is busy
     */
    bool isBusy() const;
    
private:
    /**
     * Worker thread function
     */
    void _workerThreadFunc();
    
    /**
     * Process a single upload request
     */
    bool _processUpload(const FAsyncTextureUploadRequest& request);
    
    /**
     * Create fence
     */
    TSharedPtr<FAsyncTextureUploadFence> _createFence();
    
private:
    IRHIDevice* m_device;
    bool m_initialized;
    
    // Request queue (thread-safe)
    TArray<FAsyncTextureUploadRequest> m_pendingRequests;
    ::MonsterEngine::TMap<uint64, TSharedPtr<FAsyncTextureUploadFence>> m_fenceMap;
    std::mutex m_queueMutex;
    
    // Worker thread
    TUniquePtr<class FRunnableThread> m_workerThread;
    std::atomic<bool> m_shouldExit;
    
    // Statistics
    std::atomic<uint32> m_totalUploadsProcessed;
    std::atomic<uint32> m_totalUploadsFailed;
    
    // Request ID counter
    static std::atomic<uint64> s_nextRequestID;
};

} // namespace MonsterRender::RHI
