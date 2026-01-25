// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Async Texture Load Request (UE5-style)
//
// Reference: UE5 Engine/Source/Runtime/Engine/Private/Streaming/TextureStreamIn.h

#pragma once

#include "Core/CoreTypes.h"
#include "Core/Templates/UniquePtr.h"
#include "Containers/Array.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>

namespace MonsterRender {

// Use MonsterEngine containers
using MonsterEngine::TArray;
using MonsterEngine::TUniquePtr;

// Forward declarations
namespace MonsterEngine {
    class FTexture2D;
}

/**
 * FAsyncTextureLoadRequest - Async texture mip loading request
 * 
 * Reference: UE5 FTextureStreamIn / FTextureMipDataProvider
 * 
 * Responsibilities:
 * - Async file IO for texture mips
 * - Thread-safe completion callback
 * - Cancellation support
 * - Resource lifetime management
 */
class FAsyncTextureLoadRequest {
public:
    /**
     * Load completion callback
     * @param bSuccess Whether load succeeded
     * @param LoadedData Loaded mip data (caller takes ownership)
     * @param DataSize Size of loaded data
     */
    using FCompletionCallback = std::function<void(bool bSuccess, void* LoadedData, SIZE_T DataSize)>;

    /**
     * Constructor
     * @param InTexture Texture to load mips for (weak reference, must remain valid)
     * @param InStartMip Starting mip level
     * @param InEndMip Ending mip level (exclusive)
     * @param InDestMemory Destination memory (from texture pool)
     * @param InCallback Completion callback (called on main thread)
     */
    FAsyncTextureLoadRequest(
        MonsterEngine::FTexture2D* InTexture,
        uint32 InStartMip,
        uint32 InEndMip,
        void* InDestMemory,
        FCompletionCallback InCallback
    );

    ~FAsyncTextureLoadRequest();

    // Non-copyable
    FAsyncTextureLoadRequest(const FAsyncTextureLoadRequest&) = delete;
    FAsyncTextureLoadRequest& operator=(const FAsyncTextureLoadRequest&) = delete;

    /**
     * Start async load operation
     * Spawns worker thread to load mip data from disk
     */
    void StartAsync();

    /**
     * Cancel pending load operation
     * Thread-safe, can be called from any thread
     */
    void Cancel();

    /**
     * Check if load is complete
     * @return true if load finished (success or failure)
     */
    bool IsComplete() const { return bIsComplete.load(std::memory_order_acquire); }

    /**
     * Check if load was cancelled
     * @return true if cancelled
     */
    bool IsCancelled() const { return bIsCancelled.load(std::memory_order_acquire); }

    /**
     * Get load result
     * @param OutSuccess Whether load succeeded
     * @param OutData Loaded data pointer
     * @param OutSize Data size
     * @return true if result is available
     */
    bool GetResult(bool& OutSuccess, void*& OutData, SIZE_T& OutSize);

    /**
     * Invoke completion callback (called on main thread)
     * Should be called by FAsyncTextureLoadManager
     */
    void InvokeCallback();

private:
    // Worker thread function
    void _workerThreadFunc();

    // Load mip data from disk (blocking IO)
    bool _loadMipDataFromDisk();

    MonsterEngine::FTexture2D* Texture;  // Weak reference to texture
    uint32 StartMip;                // Starting mip level
    uint32 EndMip;                  // Ending mip level
    void* DestMemory;               // Destination memory
    FCompletionCallback Callback;   // Completion callback

    std::atomic<bool> bIsComplete;  // Whether load is complete
    std::atomic<bool> bIsCancelled; // Whether load was cancelled
    std::atomic<bool> bSuccess;     // Whether load succeeded

    void* LoadedData;               // Loaded data (if success)
    SIZE_T LoadedSize;              // Size of loaded data

    TUniquePtr<std::thread> WorkerThread; // Worker thread
    std::mutex RequestMutex;        // Protects request state
};

/**
 * FAsyncTextureLoadManager - Manages async texture load requests
 * 
 * Reference: UE5 FRenderAssetStreamingManager
 * 
 * Responsibilities:
 * - Queue async load requests
 * - Process completed requests on main thread
 * - Cancel pending requests on shutdown
 */
class FAsyncTextureLoadManager {
public:
    static FAsyncTextureLoadManager& Get();

    /**
     * Initialize manager
     * @param MaxConcurrentLoads Maximum concurrent load operations
     */
    bool Initialize(uint32 MaxConcurrentLoads = 4);

    /**
     * Shutdown manager
     * Cancels all pending requests
     */
    void Shutdown();

    /**
     * Queue async load request
     * @param Request Load request (manager takes ownership)
     */
    void QueueLoadRequest(TUniquePtr<FAsyncTextureLoadRequest> Request);

    /**
     * Process completed requests (call on main thread)
     * Invokes completion callbacks for finished loads
     * @param MaxCallbacksPerFrame Maximum callbacks to invoke per frame
     */
    void ProcessCompletedRequests(uint32 MaxCallbacksPerFrame = 10);

    /**
     * Get number of pending requests
     */
    uint32 GetPendingRequestCount() const;

    /**
     * Get number of active (loading) requests
     */
    uint32 GetActiveRequestCount() const;

private:
    FAsyncTextureLoadManager();
    ~FAsyncTextureLoadManager();

    FAsyncTextureLoadManager(const FAsyncTextureLoadManager&) = delete;
    FAsyncTextureLoadManager& operator=(const FAsyncTextureLoadManager&) = delete;

    // Move completed requests to completed queue
    void _updateRequestQueues();

    TArray<TUniquePtr<FAsyncTextureLoadRequest>> PendingRequests;   // Pending requests
    TArray<TUniquePtr<FAsyncTextureLoadRequest>> ActiveRequests;    // Active (loading) requests
    TArray<TUniquePtr<FAsyncTextureLoadRequest>> CompletedRequests; // Completed requests

    std::mutex QueueMutex;          // Protects request queues
    uint32 MaxConcurrentLoads;      // Max concurrent loads
    bool bInitialized;              // Whether initialized
};

} // namespace MonsterRender
