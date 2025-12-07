// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Async File IO System

#pragma once

#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/Queue.h"
#include "Containers/Map.h"
#include <functional>
#include <future>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace MonsterRender {

// Use MonsterEngine containers
using MonsterEngine::TArray;
using MonsterEngine::TQueue;
using MonsterEngine::TMap;

/**
 * FAsyncFileIO - Asynchronous file I/O system
 * 
 * Reference: UE5 Engine/Source/Runtime/Core/Public/Async/AsyncFileHandle.h
 * 
 * Provides non-blocking file operations for texture streaming
 */
class FAsyncFileIO {
public:
    static FAsyncFileIO& Get();

    // Initialize with worker thread count
    bool Initialize(uint32 NumWorkerThreads = 2);
    void Shutdown();

    // Async read request
    struct FReadRequest {
        String FilePath;
        SIZE_T Offset;        // File offset to read from
        SIZE_T Size;          // Bytes to read
        void* DestBuffer;     // Destination buffer (must be pre-allocated)
        
        std::function<void(bool Success, SIZE_T BytesRead)> OnComplete;
    };

    // Submit async read request (returns request ID)
    uint64 ReadAsync(const FReadRequest& Request);

    // Wait for specific request to complete
    bool WaitForRequest(uint64 RequestID);

    // Wait for all pending requests
    void WaitForAll();

    // Query request status
    bool IsRequestComplete(uint64 RequestID);

    // Statistics
    struct FIOStats {
        uint64 TotalRequests;
        uint64 CompletedRequests;
        uint64 PendingRequests;
        uint64 FailedRequests;
        uint64 TotalBytesRead;
        float AverageBandwidthMBps;
    };

    void GetStats(FIOStats& OutStats);

private:
    FAsyncFileIO();
    ~FAsyncFileIO();

    FAsyncFileIO(const FAsyncFileIO&) = delete;
    FAsyncFileIO& operator=(const FAsyncFileIO&) = delete;

    // Internal request with tracking
    struct FInternalRequest {
        uint64 RequestID;
        FReadRequest Request;
        std::promise<bool> Promise;
        bool bCompleted;
    };

    // Worker thread function
    void WorkerThreadFunc();

    // Process a single request
    bool ProcessRequest(FInternalRequest& InternalRequest);

    TArray<std::thread> WorkerThreads;
    TQueue<FInternalRequest*> RequestQueue;
    std::mutex QueueMutex;
    std::condition_variable QueueCV;
    
    TMap<uint64, FInternalRequest*> ActiveRequests;
    std::mutex ActiveRequestsMutex;

    uint64 NextRequestID;
    bool bShuttingDown;
    bool bInitialized;

    // Statistics
    std::atomic<uint64> TotalRequests{0};
    std::atomic<uint64> CompletedRequests{0};
    std::atomic<uint64> FailedRequests{0};
    std::atomic<uint64> TotalBytesRead{0};
};

} // namespace MonsterRender

