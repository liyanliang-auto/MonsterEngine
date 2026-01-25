// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Async Texture Load Request Implementation

#include "Renderer/FAsyncTextureLoadRequest.h"
#include "Renderer/FTextureStreamingManager.h"
#include "Renderer/FTextureFileReader.h"
#include "Engine/Texture/Texture2D.h"
#include "Core/HAL/FMemory.h"
#include "Core/Log.h"
#include "Math/MathUtility.h"
#include <algorithm>
#include <cstring>

namespace MonsterRender {

// ===== FAsyncTextureLoadRequest Implementation =====

FAsyncTextureLoadRequest::FAsyncTextureLoadRequest(
    MonsterEngine::FTexture2D* InTexture,
    uint32 InStartMip,
    uint32 InEndMip,
    void* InDestMemory,
    FCompletionCallback InCallback
)
    : Texture(InTexture)
    , StartMip(InStartMip)
    , EndMip(InEndMip)
    , DestMemory(InDestMemory)
    , Callback(InCallback)
    , bIsComplete(false)
    , bIsCancelled(false)
    , bSuccess(false)
    , LoadedData(nullptr)
    , LoadedSize(0)
{
}

FAsyncTextureLoadRequest::~FAsyncTextureLoadRequest() {
    // Cancel if still running
    if (!bIsComplete.load(std::memory_order_acquire)) {
        Cancel();
    }

    // Wait for worker thread to finish
    if (WorkerThread && WorkerThread->joinable()) {
        WorkerThread->join();
    }
}

void FAsyncTextureLoadRequest::StartAsync() {
    if (bIsComplete.load(std::memory_order_acquire) || bIsCancelled.load(std::memory_order_acquire)) {
        MR_LOG(LogTextureStreaming, Warning, "Cannot start async load: already complete or cancelled");
        return;
    }

    // Spawn worker thread
    WorkerThread = MakeUnique<std::thread>(&FAsyncTextureLoadRequest::_workerThreadFunc, this);

    MR_LOG(LogTextureStreaming, VeryVerbose, "Started async texture load (Mips %u -> %u)", StartMip, EndMip);
}

void FAsyncTextureLoadRequest::Cancel() {
    bool expected = false;
    if (bIsCancelled.compare_exchange_strong(expected, true, std::memory_order_release, std::memory_order_relaxed)) {
        MR_LOG(LogTextureStreaming, VeryVerbose, "Cancelled async texture load");
    }
}

bool FAsyncTextureLoadRequest::GetResult(bool& OutSuccess, void*& OutData, SIZE_T& OutSize) {
    if (!bIsComplete.load(std::memory_order_acquire)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(RequestMutex);
    OutSuccess = bSuccess.load(std::memory_order_acquire);
    OutData = LoadedData;
    OutSize = LoadedSize;
    return true;
}

void FAsyncTextureLoadRequest::InvokeCallback() {
    if (!bIsComplete.load(std::memory_order_acquire)) {
        MR_LOG(LogTextureStreaming, Warning, "Cannot invoke callback: load not complete");
        return;
    }

    if (Callback) {
        bool success = bSuccess.load(std::memory_order_acquire);
        Callback(success, LoadedData, LoadedSize);
    }
}

void FAsyncTextureLoadRequest::_workerThreadFunc() {
    // Check for cancellation before starting
    if (bIsCancelled.load(std::memory_order_acquire)) {
        bIsComplete.store(true, std::memory_order_release);
        return;
    }

    // Perform blocking IO
    bool loadSuccess = _loadMipDataFromDisk();

    // Store result
    {
        std::lock_guard<std::mutex> lock(RequestMutex);
        bSuccess.store(loadSuccess, std::memory_order_release);
    }

    // Mark as complete
    bIsComplete.store(true, std::memory_order_release);

    MR_LOG(LogTextureStreaming, VeryVerbose, "Async texture load completed: %s", 
           loadSuccess ? "success" : "failed");
}

bool FAsyncTextureLoadRequest::_loadMipDataFromDisk() {
    // Check for cancellation
    if (bIsCancelled.load(std::memory_order_acquire)) {
        return false;
    }

    // Validate inputs
    if (!Texture || !DestMemory) {
        MR_LOG(LogTextureStreaming, Error, "Invalid texture or destination memory");
        return false;
    }

    // Load texture file
    // Reference: UE5 FTexture2DMipDataProvider_IO::GetMips()
    FTextureFileData fileData;
    bool loadSuccess = FTextureFileReaderFactory::LoadTextureFromFile(Texture->getFilePath().c_str(), fileData);
    
    if (!loadSuccess) {
        MR_LOG(LogTextureStreaming, Error, "Failed to load texture file: %s", Texture->getFilePath().c_str());
        return false;
    }

    // Check for cancellation after loading
    if (bIsCancelled.load(std::memory_order_acquire)) {
        MR_LOG(LogTextureStreaming, VeryVerbose, "Load cancelled after file read");
        fileData.FreeData();
        return false;
    }

    // Validate mip count
    if (EndMip > fileData.MipCount) {
        MR_LOG(LogTextureStreaming, Error, "Requested mip range exceeds available mips (requested: %u-%u, available: %u)", 
               StartMip, EndMip, fileData.MipCount);
        fileData.FreeData();
        return false;
    }

    // Copy requested mip data to destination memory
    SIZE_T totalSize = 0;
    uint8* destPtr = static_cast<uint8*>(DestMemory);
    
    for (uint32 mipLevel = StartMip; mipLevel < EndMip; ++mipLevel) {
        // Check for cancellation during copy
        if (bIsCancelled.load(std::memory_order_acquire)) {
            MR_LOG(LogTextureStreaming, VeryVerbose, "Load cancelled during mip copy");
            fileData.FreeData();
            return false;
        }

        const FTextureMipData& mip = fileData.Mips[mipLevel];
        
        // Copy mip data
        std::memcpy(destPtr, mip.Data, mip.DataSize);
        destPtr += mip.DataSize;
        totalSize += mip.DataSize;
        
        // Note: Mip data pointers will be updated by streaming manager
        // via updateResidentMips() call
    }

    // Store loaded data info
    {
        std::lock_guard<std::mutex> lock(RequestMutex);
        LoadedData = DestMemory;
        LoadedSize = totalSize;
    }

    // Free temporary file data
    fileData.FreeData();

    MR_LOG(LogTextureStreaming, Log, "Loaded mips from disk: %s (Mips %u -> %u, %llu bytes)", 
           Texture->getFilePath().c_str(), StartMip, EndMip, static_cast<uint64>(totalSize));
    return true;
}

// ===== FAsyncTextureLoadManager Implementation =====

FAsyncTextureLoadManager& FAsyncTextureLoadManager::Get() {
    static FAsyncTextureLoadManager Instance;
    return Instance;
}

FAsyncTextureLoadManager::FAsyncTextureLoadManager()
    : MaxConcurrentLoads(4)
    , bInitialized(false)
{
}

FAsyncTextureLoadManager::~FAsyncTextureLoadManager() {
    Shutdown();
}

bool FAsyncTextureLoadManager::Initialize(uint32 InMaxConcurrentLoads) {
    if (bInitialized) {
        MR_LOG(LogTextureStreaming, Warning, "FAsyncTextureLoadManager already initialized");
        return true;
    }

    MaxConcurrentLoads = InMaxConcurrentLoads;
    bInitialized = true;

    MR_LOG(LogTextureStreaming, Log, "FAsyncTextureLoadManager initialized (MaxConcurrentLoads: %u)", 
           MaxConcurrentLoads);
    return true;
}

void FAsyncTextureLoadManager::Shutdown() {
    if (!bInitialized) return;

    MR_LOG(LogTextureStreaming, Log, "Shutting down FAsyncTextureLoadManager...");

    std::lock_guard<std::mutex> lock(QueueMutex);

    // Cancel all pending requests
    for (auto& request : PendingRequests) {
        if (request) {
            request->Cancel();
        }
    }

    // Cancel all active requests
    for (auto& request : ActiveRequests) {
        if (request) {
            request->Cancel();
        }
    }

    // Wait for all requests to complete
    // Note: Destructors will wait for worker threads
    PendingRequests.Empty();
    ActiveRequests.Empty();
    CompletedRequests.Empty();

    bInitialized = false;
    MR_LOG(LogTextureStreaming, Log, "FAsyncTextureLoadManager shut down");
}

void FAsyncTextureLoadManager::QueueLoadRequest(TUniquePtr<FAsyncTextureLoadRequest> Request) {
    if (!bInitialized) {
        MR_LOG(LogTextureStreaming, Error, "Cannot queue request: manager not initialized");
        return;
    }

    if (!Request) {
        MR_LOG(LogTextureStreaming, Error, "Cannot queue null request");
        return;
    }

    std::lock_guard<std::mutex> lock(QueueMutex);
    PendingRequests.Add(std::move(Request));

    MR_LOG(LogTextureStreaming, VeryVerbose, "Queued async load request (Pending: %d)", 
           PendingRequests.Num());
}

void FAsyncTextureLoadManager::ProcessCompletedRequests(uint32 MaxCallbacksPerFrame) {
    if (!bInitialized) return;

    // Update request queues (move completed to completed queue)
    _updateRequestQueues();

    // Process completed requests
    uint32 callbacksInvoked = 0;
    TArray<TUniquePtr<FAsyncTextureLoadRequest>> requestsToProcess;

    {
        std::lock_guard<std::mutex> lock(QueueMutex);
        
        // Take up to MaxCallbacksPerFrame completed requests
        uint32 numToProcess = std::min(MaxCallbacksPerFrame, static_cast<uint32>(CompletedRequests.Num()));
        for (uint32 i = 0; i < numToProcess; ++i) {
            requestsToProcess.Add(std::move(CompletedRequests[i]));
        }
        
        // Remove processed requests from queue
        if (numToProcess > 0) {
            CompletedRequests.RemoveAt(0, numToProcess);
        }
    }

    // Invoke callbacks (outside lock to avoid deadlock)
    for (auto& request : requestsToProcess) {
        if (request && !request->IsCancelled()) {
            request->InvokeCallback();
            ++callbacksInvoked;
        }
    }

    if (callbacksInvoked > 0) {
        MR_LOG(LogTextureStreaming, VeryVerbose, "Processed %u completed load requests", callbacksInvoked);
    }
}

uint32 FAsyncTextureLoadManager::GetPendingRequestCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(QueueMutex));
    return static_cast<uint32>(PendingRequests.Num());
}

uint32 FAsyncTextureLoadManager::GetActiveRequestCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(QueueMutex));
    return static_cast<uint32>(ActiveRequests.Num());
}

void FAsyncTextureLoadManager::_updateRequestQueues() {
    std::lock_guard<std::mutex> lock(QueueMutex);

    // Move completed active requests to completed queue
    for (int32 i = ActiveRequests.Num() - 1; i >= 0; --i) {
        if (ActiveRequests[i] && ActiveRequests[i]->IsComplete()) {
            CompletedRequests.Add(std::move(ActiveRequests[i]));
            ActiveRequests.RemoveAt(i);
        }
    }

    // Start pending requests if we have capacity
    while (ActiveRequests.Num() < static_cast<int32>(MaxConcurrentLoads) && PendingRequests.Num() > 0) {
        auto request = std::move(PendingRequests[0]);
        PendingRequests.RemoveAt(0);

        if (request) {
            request->StartAsync();
            ActiveRequests.Add(std::move(request));
        }
    }
}

} // namespace MonsterRender
