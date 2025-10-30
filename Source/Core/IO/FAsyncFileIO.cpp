// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Async File IO Implementation

#include "Core/IO/FAsyncFileIO.h"
#include "Core/Log.h"
#include <fstream>
#include <chrono>

namespace MonsterRender {

FAsyncFileIO& FAsyncFileIO::Get() {
    static FAsyncFileIO Instance;
    return Instance;
}

FAsyncFileIO::FAsyncFileIO()
    : NextRequestID(1)
    , bShuttingDown(false)
    , bInitialized(false)
{
}

FAsyncFileIO::~FAsyncFileIO() {
    Shutdown();
}

bool FAsyncFileIO::Initialize(uint32 NumWorkerThreads) {
    if (bInitialized) {
        MR_LOG_WARNING("FAsyncFileIO already initialized");
        return true;
    }

    MR_LOG_INFO("Initializing FAsyncFileIO with " + std::to_string(NumWorkerThreads) + " worker threads");

    bShuttingDown = false;

    // Create worker threads
    for (uint32 i = 0; i < NumWorkerThreads; ++i) {
        WorkerThreads.emplace_back(&FAsyncFileIO::WorkerThreadFunc, this);
    }

    bInitialized = true;
    MR_LOG_INFO("FAsyncFileIO initialized successfully");
    return true;
}

void FAsyncFileIO::Shutdown() {
    if (!bInitialized) return;

    MR_LOG_INFO("Shutting down FAsyncFileIO...");

    // Signal shutdown
    bShuttingDown = true;
    QueueCV.notify_all();

    // Wait for all worker threads to finish
    for (auto& thread : WorkerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    WorkerThreads.clear();

    // Clear pending requests
    {
        std::scoped_lock lock(QueueMutex);
        while (!RequestQueue.empty()) {
            auto* request = RequestQueue.front();
            RequestQueue.pop();
            delete request;
        }
    }

    // Clear active requests
    {
        std::scoped_lock lock(ActiveRequestsMutex);
        for (auto& pair : ActiveRequests) {
            delete pair.second;
        }
        ActiveRequests.clear();
    }

    bInitialized = false;
    MR_LOG_INFO("FAsyncFileIO shut down");
}

uint64 FAsyncFileIO::ReadAsync(const FReadRequest& Request) {
    if (!bInitialized) {
        MR_LOG_ERROR("FAsyncFileIO not initialized");
        return 0;
    }

    // Create internal request
    auto* internalReq = new FInternalRequest();
    internalReq->RequestID = NextRequestID++;
    internalReq->Request = Request;
    internalReq->bCompleted = false;

    // Add to active requests
    {
        std::scoped_lock lock(ActiveRequestsMutex);
        ActiveRequests[internalReq->RequestID] = internalReq;
    }

    // Add to queue
    {
        std::scoped_lock lock(QueueMutex);
        RequestQueue.push(internalReq);
    }

    // Notify worker thread
    QueueCV.notify_one();

    TotalRequests.fetch_add(1, std::memory_order_relaxed);

    return internalReq->RequestID;
}

bool FAsyncFileIO::WaitForRequest(uint64 RequestID) {
    if (RequestID == 0) return false;

    FInternalRequest* request = nullptr;

    // Find request
    {
        std::scoped_lock lock(ActiveRequestsMutex);
        auto it = ActiveRequests.find(RequestID);
        if (it == ActiveRequests.end()) {
            return false;  // Already completed or invalid
        }
        request = it->second;
    }

    // Wait for completion
    auto future = request->Promise.get_future();
    return future.get();
}

void FAsyncFileIO::WaitForAll() {
    while (true) {
        bool hasActive = false;
        
        {
            std::scoped_lock lock(ActiveRequestsMutex);
            hasActive = !ActiveRequests.empty();
        }

        if (!hasActive) break;

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool FAsyncFileIO::IsRequestComplete(uint64 RequestID) {
    std::scoped_lock lock(ActiveRequestsMutex);
    auto it = ActiveRequests.find(RequestID);
    return (it == ActiveRequests.end() || it->second->bCompleted);
}

void FAsyncFileIO::GetStats(FIOStats& OutStats) {
    OutStats.TotalRequests = TotalRequests.load(std::memory_order_relaxed);
    OutStats.CompletedRequests = CompletedRequests.load(std::memory_order_relaxed);
    OutStats.FailedRequests = FailedRequests.load(std::memory_order_relaxed);
    OutStats.TotalBytesRead = TotalBytesRead.load(std::memory_order_relaxed);

    {
        std::scoped_lock lock(QueueMutex);
        OutStats.PendingRequests = RequestQueue.size();
    }

    // Calculate bandwidth (simplified)
    if (OutStats.CompletedRequests > 0) {
        OutStats.AverageBandwidthMBps = (float)(OutStats.TotalBytesRead / 1024.0 / 1024.0) / 
                                        (float)OutStats.CompletedRequests;
    } else {
        OutStats.AverageBandwidthMBps = 0.0f;
    }
}

// Private methods

void FAsyncFileIO::WorkerThreadFunc() {
    MR_LOG_INFO("FAsyncFileIO worker thread started");

    while (!bShuttingDown) {
        FInternalRequest* request = nullptr;

        // Wait for request
        {
            std::unique_lock<std::mutex> lock(QueueMutex);
            QueueCV.wait(lock, [this]() {
                return !RequestQueue.empty() || bShuttingDown;
            });

            if (bShuttingDown) break;

            if (!RequestQueue.empty()) {
                request = RequestQueue.front();
                RequestQueue.pop();
            }
        }

        if (!request) continue;

        // Process request
        bool success = ProcessRequest(*request);

        // Set result
        request->Promise.set_value(success);
        request->bCompleted = true;

        // Call completion callback if provided
        if (request->Request.OnComplete) {
            SIZE_T bytesRead = success ? request->Request.Size : 0;
            request->Request.OnComplete(success, bytesRead);
        }

        // Remove from active requests
        {
            std::scoped_lock lock(ActiveRequestsMutex);
            ActiveRequests.erase(request->RequestID);
        }

        // Update stats
        if (success) {
            CompletedRequests.fetch_add(1, std::memory_order_relaxed);
        } else {
            FailedRequests.fetch_add(1, std::memory_order_relaxed);
        }

        delete request;
    }

    MR_LOG_INFO("FAsyncFileIO worker thread stopped");
}

bool FAsyncFileIO::ProcessRequest(FInternalRequest& InternalRequest) {
    const auto& req = InternalRequest.Request;

    auto startTime = std::chrono::high_resolution_clock::now();

    // Open file
    std::ifstream file(req.FilePath, std::ios::binary);
    if (!file.is_open()) {
        MR_LOG_ERROR("Failed to open file: " + req.FilePath);
        return false;
    }

    // Seek to offset
    file.seekg(req.Offset, std::ios::beg);
    if (!file.good()) {
        MR_LOG_ERROR("Failed to seek in file: " + req.FilePath);
        return false;
    }

    // Read data
    file.read(reinterpret_cast<char*>(req.DestBuffer), req.Size);
    SIZE_T bytesRead = file.gcount();

    file.close();

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    if (bytesRead != req.Size) {
        MR_LOG_WARNING("Partial read: " + std::to_string(bytesRead) + "/" + std::to_string(req.Size) + " bytes");
    }

    TotalBytesRead.fetch_add(bytesRead, std::memory_order_relaxed);

    MR_LOG_DEBUG("Async read completed: " + req.FilePath + 
                 " (" + std::to_string(bytesRead / 1024) + "KB in " + 
                 std::to_string(duration.count()) + "ms)");

    return (bytesRead == req.Size);
}

} // namespace MonsterRender

