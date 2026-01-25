// Copyright Monster Engine. All Rights Reserved.
// Async Texture Upload Manager Implementation

#include "RHI/FAsyncTextureUploadManager.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHIResource.h"
#include "Core/Log.h"
#include "Core/HAL/FMemory.h"
#include "Core/HAL/Runnable.h"
#include "Core/HAL/RunnableThread.h"
#include <chrono>
#include <thread>

DEFINE_LOG_CATEGORY_STATIC(LogAsyncTextureUpload, Log, All);

namespace MonsterRender::RHI {

// Static member initialization
std::atomic<uint64> FAsyncTextureUploadFence::s_nextFenceID{1};
std::atomic<uint64> FAsyncTextureUploadManager::s_nextRequestID{1};

// ============================================================================
// FAsyncTextureUploadFence Implementation
// ============================================================================

FAsyncTextureUploadFence::FAsyncTextureUploadFence()
    : m_isComplete(false)
    , m_fenceID(s_nextFenceID.fetch_add(1))
{
}

FAsyncTextureUploadFence::~FAsyncTextureUploadFence()
{
}

bool FAsyncTextureUploadFence::isComplete() const
{
    return m_isComplete.load(std::memory_order_acquire);
}

bool FAsyncTextureUploadFence::wait(uint32 timeoutMs)
{
    if (isComplete()) {
        return true;
    }
    
    if (timeoutMs == 0) {
        // Infinite wait
        while (!isComplete()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    } else {
        // Timed wait
        auto startTime = std::chrono::steady_clock::now();
        while (!isComplete()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= timeoutMs) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
}

void FAsyncTextureUploadFence::signal()
{
    m_isComplete.store(true, std::memory_order_release);
}

void FAsyncTextureUploadFence::reset()
{
    m_isComplete.store(false, std::memory_order_release);
}

// ============================================================================
// Worker Thread Runnable
// ============================================================================

class FAsyncTextureUploadWorker : public FRunnable
{
public:
    FAsyncTextureUploadWorker(FAsyncTextureUploadManager* manager)
        : m_manager(manager)
    {}
    
    virtual uint32 Run() override
    {
        MR_LOG(LogAsyncTextureUpload, Log, "Async texture upload worker thread started");
        
        // Worker thread will be controlled by manager
        // This is just a placeholder for thread creation
        
        MR_LOG(LogAsyncTextureUpload, Log, "Async texture upload worker thread stopped");
        return 0;
    }
    
private:
    FAsyncTextureUploadManager* m_manager;
};

// ============================================================================
// FAsyncTextureUploadManager Implementation
// ============================================================================

FAsyncTextureUploadManager::FAsyncTextureUploadManager(IRHIDevice* device)
    : m_device(device)
    , m_initialized(false)
    , m_shouldExit(false)
    , m_totalUploadsProcessed(0)
    , m_totalUploadsFailed(0)
{
}

FAsyncTextureUploadManager::~FAsyncTextureUploadManager()
{
    shutdown();
}

bool FAsyncTextureUploadManager::initialize()
{
    if (m_initialized) {
        MR_LOG(LogAsyncTextureUpload, Warning, "Async texture upload manager already initialized");
        return true;
    }
    
    if (!m_device) {
        MR_LOG(LogAsyncTextureUpload, Error, "Cannot initialize: null device");
        return false;
    }
    
    m_shouldExit.store(false);
    
    // Create worker thread
    FAsyncTextureUploadWorker* worker = new FAsyncTextureUploadWorker(this);
    m_workerThread = MakeUnique<FRunnableThread>(worker, "AsyncTextureUpload");
    
    if (!m_workerThread) {
        MR_LOG(LogAsyncTextureUpload, Error, "Failed to create worker thread");
        delete worker;
        return false;
    }
    
    m_initialized = true;
    MR_LOG(LogAsyncTextureUpload, Log, "Async texture upload manager initialized");
    return true;
}

void FAsyncTextureUploadManager::shutdown()
{
    if (!m_initialized) {
        return;
    }
    
    MR_LOG(LogAsyncTextureUpload, Log, "Shutting down async texture upload manager");
    
    // Signal worker thread to exit
    m_shouldExit.store(true);
    
    // Wait for all pending uploads
    waitForAll();
    
    // Stop worker thread
    if (m_workerThread) {
        m_workerThread->WaitForCompletion();
        m_workerThread.reset();
    }
    
    // Clear queues
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingRequests.clear();
        m_fenceMap.clear();
    }
    
    MR_LOG(LogAsyncTextureUpload, Log, "Async texture upload manager shutdown complete. Processed: %u, Failed: %u",
           m_totalUploadsProcessed.load(), m_totalUploadsFailed.load());
    
    m_initialized = false;
}

bool FAsyncTextureUploadManager::submitUpload(
    const FAsyncTextureUploadRequest& request,
    TSharedPtr<FAsyncTextureUploadFence>& outFence)
{
    if (!m_initialized) {
        MR_LOG(LogAsyncTextureUpload, Error, "Cannot submit upload: manager not initialized");
        return false;
    }
    
    if (!request.Texture || !request.Data || request.DataSize == 0) {
        MR_LOG(LogAsyncTextureUpload, Error, "Invalid upload request");
        return false;
    }
    
    // Create fence
    outFence = _createFence();
    
    // Add to queue
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingRequests.push_back(request);
        m_fenceMap.insert({request.RequestID, outFence});
    }
    
    MR_LOG(LogAsyncTextureUpload, VeryVerbose, "Submitted async upload request %llu (mip %u, %llu bytes)",
           request.RequestID, request.MipLevel, static_cast<uint64>(request.DataSize));
    
    return true;
}

bool FAsyncTextureUploadManager::submitBatchUpload(
    const TArray<FAsyncTextureUploadRequest>& requests,
    TSharedPtr<FAsyncTextureUploadFence>& outFence)
{
    if (!m_initialized) {
        MR_LOG(LogAsyncTextureUpload, Error, "Cannot submit batch upload: manager not initialized");
        return false;
    }
    
    if (requests.size() == 0) {
        MR_LOG(LogAsyncTextureUpload, Warning, "Empty batch upload request");
        return false;
    }
    
    // Create shared fence for all requests
    outFence = _createFence();
    
    // Add all requests to queue
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (const auto& request : requests) {
            if (!request.Texture || !request.Data || request.DataSize == 0) {
                MR_LOG(LogAsyncTextureUpload, Warning, "Skipping invalid request in batch");
                continue;
            }
            m_pendingRequests.push_back(request);
            m_fenceMap.insert({request.RequestID, outFence});
        }
    }
    
    MR_LOG(LogAsyncTextureUpload, Verbose, "Submitted batch upload with %u requests", 
           static_cast<uint32>(requests.size()));
    
    return true;
}

void FAsyncTextureUploadManager::processPendingUploads(uint32 maxUploadsPerFrame)
{
    if (!m_initialized || m_shouldExit.load()) {
        return;
    }
    
    TArray<FAsyncTextureUploadRequest> requestsToProcess;
    
    // Get requests to process
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        uint32 count = std::min(maxUploadsPerFrame, static_cast<uint32>(m_pendingRequests.size()));
        if (count == 0) {
            return;
        }
        
        // Take first N requests
        for (uint32 i = 0; i < count; ++i) {
            requestsToProcess.push_back(m_pendingRequests[i]);
        }
        
        // Remove from pending queue
        m_pendingRequests.erase(m_pendingRequests.begin(), m_pendingRequests.begin() + count);
    }
    
    // Process uploads
    for (const auto& request : requestsToProcess) {
        bool success = _processUpload(request);
        
        if (success) {
            m_totalUploadsProcessed.fetch_add(1);
        } else {
            m_totalUploadsFailed.fetch_add(1);
        }
        
        // Signal fence
        TSharedPtr<FAsyncTextureUploadFence> fence;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            auto it = m_fenceMap.find(request.RequestID);
            if (it != m_fenceMap.end()) {
                fence = it->second;
                m_fenceMap.erase(it);
            }
        }
        
        if (fence) {
            fence->signal();
        }
        
        // Call completion callback
        if (request.OnComplete) {
            request.OnComplete(success);
        }
    }
}

void FAsyncTextureUploadManager::waitForAll()
{
    if (!m_initialized) {
        return;
    }
    
    MR_LOG(LogAsyncTextureUpload, Verbose, "Waiting for all pending uploads to complete");
    
    // Process all remaining uploads
    while (getPendingUploadCount() > 0) {
        processPendingUploads(32); // Process in larger batches when waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    MR_LOG(LogAsyncTextureUpload, Verbose, "All pending uploads completed");
}

uint32 FAsyncTextureUploadManager::getPendingUploadCount() const
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_queueMutex));
    return static_cast<uint32>(m_pendingRequests.size());
}

bool FAsyncTextureUploadManager::isBusy() const
{
    return getPendingUploadCount() > 0;
}

bool FAsyncTextureUploadManager::_processUpload(const FAsyncTextureUploadRequest& request)
{
    if (!m_device || !request.Texture || !request.Data) {
        return false;
    }
    
    // Call device's updateTextureSubresource
    bool success = m_device->updateTextureSubresource(
        request.Texture,
        request.MipLevel,
        request.Data,
        request.DataSize
    );
    
    if (!success) {
        MR_LOG(LogAsyncTextureUpload, Error, "Failed to upload texture mip %u (request %llu)",
               request.MipLevel, request.RequestID);
    } else {
        MR_LOG(LogAsyncTextureUpload, VeryVerbose, "Successfully uploaded texture mip %u (request %llu)",
               request.MipLevel, request.RequestID);
    }
    
    return success;
}

TSharedPtr<FAsyncTextureUploadFence> FAsyncTextureUploadManager::_createFence()
{
    return MakeShared<FAsyncTextureUploadFence>();
}

} // namespace MonsterRender::RHI
