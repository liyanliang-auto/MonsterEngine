// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file AsyncTextureLoader.h
 * @brief Asynchronous texture loading system for background texture loading
 * 
 * Provides thread-safe asynchronous texture loading with worker thread pool.
 * Supports loading texture data and generating mipmaps on background threads,
 * with callback mechanism for GPU upload on main thread.
 * 
 * Reference:
 * - UE5: Engine/Source/Runtime/Engine/Public/TextureResource.h
 * - UE5: Engine/Source/Runtime/RenderCore/Public/RenderingThread.h
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Templates/SharedPointer.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <functional>

namespace MonsterEngine
{

// Forward declare callback type
class FAsyncTextureLoader;

/**
 * @struct FTextureLoadResult
 * @brief Result of asynchronous texture loading
 */
struct FTextureLoadResult
{
    /** Source file path */
    FString FilePath;
    
    /** Texture width */
    uint32 Width = 0;
    
    /** Texture height */
    uint32 Height = 0;
    
    /** Number of channels in source image */
    uint32 Channels = 0;
    
    /** Number of mip levels */
    uint32 MipLevels = 0;
    
    /** Mip level data (RGBA8 format) - caller must free */
    TArray<unsigned char*> MipData;
    
    /** Size of each mip level in bytes */
    TArray<uint32> MipSizes;
    
    /** Whether loading succeeded */
    bool bSuccess = false;
    
    /** Error message if loading failed */
    FString ErrorMessage;
    
    /** Callback function to invoke when processing this result */
    std::function<void(const FTextureLoadResult&)> Callback;
    
    /**
     * Clean up allocated mip data
     */
    void Cleanup();
};

/**
 * @class FAsyncTextureLoader
 * @brief Asynchronous texture loader with worker thread pool
 * 
 * Manages background loading of textures and mipmap generation.
 * Uses a thread pool to process multiple texture loads concurrently.
 * Provides callback mechanism for GPU upload on main thread.
 * 
 * Thread-safe for concurrent load requests.
 * 
 * Reference: UE5 FTextureCompilingManager
 */
class FAsyncTextureLoader
{
public:
    /**
     * Callback function type for texture load completion
     * Called on main thread after texture data is ready
     * @param Result Load result containing texture data
     */
    using FLoadCompleteCallback = std::function<void(const FTextureLoadResult&)>;
    
    /**
     * Constructor
     * @param NumWorkerThreads Number of worker threads (default: 2)
     */
    explicit FAsyncTextureLoader(uint32 NumWorkerThreads = 2);
    
    /**
     * Destructor - shuts down worker threads
     */
    ~FAsyncTextureLoader();
    
    // Non-copyable
    FAsyncTextureLoader(const FAsyncTextureLoader&) = delete;
    FAsyncTextureLoader& operator=(const FAsyncTextureLoader&) = delete;
    
    /**
     * Load texture asynchronously
     * @param FilePath Path to texture file
     * @param bGenerateMips Whether to generate mipmaps
     * @param Callback Callback function called when loading completes
     */
    void loadTextureAsync(
        const FString& FilePath,
        bool bGenerateMips,
        FLoadCompleteCallback Callback);
    
    /**
     * Process completed loads and invoke callbacks
     * Must be called on main thread each frame
     */
    void processCompletedLoads();
    
    /**
     * Shutdown the loader and wait for all pending loads
     */
    void shutdown();
    
    /**
     * Check if loader is currently processing any loads
     * @return True if there are pending or in-progress loads
     */
    bool isBusy() const;
    
    /**
     * Get number of pending load requests
     * @return Number of requests in queue
     */
    uint32 getPendingLoadCount() const;
    
private:
    /**
     * @struct FLoadRequest
     * @brief Internal load request structure
     */
    struct FLoadRequest
    {
        FString FilePath;
        bool bGenerateMips;
        FLoadCompleteCallback Callback;
        
        FLoadRequest() : bGenerateMips(false) {}
        
        FLoadRequest(const FString& InPath, bool bInGenerateMips, FLoadCompleteCallback InCallback)
            : FilePath(InPath)
            , bGenerateMips(bInGenerateMips)
            , Callback(InCallback)
        {}
    };
    
    /**
     * Worker thread function
     * @param ThreadIndex Index of this worker thread
     */
    void _workerThreadFunc(uint32 ThreadIndex);
    
    /**
     * Process a single load request
     * @param Request Load request to process
     */
    void _processLoadRequest(const FLoadRequest& Request);
    
    /**
     * Load texture from file
     * @param FilePath Path to texture file
     * @param OutWidth Output texture width
     * @param OutHeight Output texture height
     * @param OutChannels Output number of channels
     * @return Loaded image data (RGBA8), nullptr on failure
     */
    unsigned char* _loadTextureFromFile(
        const FString& FilePath,
        int32& OutWidth,
        int32& OutHeight,
        int32& OutChannels);
    
    /**
     * Generate mipmaps for texture data
     * @param SourceData Source image data (RGBA8)
     * @param Width Image width
     * @param Height Image height
     * @param MipLevels Number of mip levels to generate
     * @param OutMipData Output mip level data
     * @param OutMipSizes Output mip level sizes
     * @return True if successful
     */
    bool _generateMipmaps(
        const unsigned char* SourceData,
        uint32 Width,
        uint32 Height,
        uint32 MipLevels,
        TArray<unsigned char*>& OutMipData,
        TArray<uint32>& OutMipSizes);
    
    /** Worker threads */
    TArray<std::thread> m_workerThreads;
    
    /** Pending load requests queue */
    std::queue<FLoadRequest> m_loadQueue;
    
    /** Mutex for load queue */
    mutable std::mutex m_queueMutex;
    
    /** Condition variable for worker threads */
    std::condition_variable m_queueCondition;
    
    /** Completed loads queue (for main thread processing) */
    std::queue<FTextureLoadResult> m_completedQueue;
    
    /** Mutex for completed queue */
    mutable std::mutex m_completedMutex;
    
    /** Atomic flag for shutdown */
    std::atomic<bool> m_bShutdown;
    
    /** Atomic counter for active loads */
    std::atomic<uint32> m_activeLoadCount;
};

} // namespace MonsterEngine
