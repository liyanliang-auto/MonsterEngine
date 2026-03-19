// Copyright Monster Engine. All Rights Reserved.

#pragma once

#include "Core/CoreMinimal.h"
#include "Core/FRunnable.h"
#include "Core/FRunnableThread.h"
#include "Core/FTaskGraph.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace MonsterEngine {

/**
 * FRHIThread
 * 
 * Manages the RHI thread, which is responsible for translating RHI commands
 * into platform-specific graphics API calls (Vulkan, OpenGL, etc.).
 * 
 * This design is based on UE5's RHI thread architecture, where:
 * - Render thread records high-level RHI commands
 * - RHI thread translates and submits them to the GPU
 * - This separation allows parallel work and better CPU utilization
 * 
 * Reference: UE5 RHICommandList.h, RHICommandList.cpp
 */
class FRHIThread : public FRunnable {
public:
    /**
     * Task delegate type for RHI thread tasks
     */
    using FRHIThreadTask = TFunction<void()>;

    /**
     * Get the singleton instance
     */
    static FRHIThread& Get();

    /**
     * Initialize the RHI thread
     * @param bUseRHIThread If true, creates a dedicated RHI thread. If false, RHI commands execute on render thread.
     * @return True if initialization succeeded
     */
    static bool Initialize(bool bUseRHIThread = true);

    /**
     * Shutdown the RHI thread
     */
    static void Shutdown();

    /**
     * Check if RHI thread is initialized
     */
    static bool IsInitialized();

    /**
     * Check if currently executing on the RHI thread
     */
    static bool IsInRHIThread();

    /**
     * Queue a task to execute on the RHI thread
     * @param Task The task to execute
     * @return Graph event that will be signaled when task completes
     */
    static FGraphEventRef QueueTask(FRHIThreadTask&& Task);

    /**
     * Wait for the RHI thread to complete all pending tasks
     */
    static void WaitForTasks();

    /**
     * Check if RHI thread is idle (no pending tasks)
     */
    static bool IsIdle();

    // FRunnable interface
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;

    FRHIThread();
    ~FRHIThread();
    
private:

    /**
     * Process tasks from the queue
     */
    void ProcessTasks();

    /**
     * Try to get the next task from the queue
     */
    bool TryGetNextTask(FRHIThreadTask& OutTask, FGraphEventRef& OutEvent);

private:
    /** Singleton instance */
    static TUniquePtr<FRHIThread> s_instance;

    /** The RHI thread */
    TUniquePtr<FRunnableThread> m_thread;

    /** Whether to use a dedicated RHI thread */
    bool m_bUseRHIThread;

    /** Whether the thread is shutting down */
    std::atomic<bool> m_bIsShuttingDown;

    /** Task queue */
    struct FTaskEntry {
        FRHIThreadTask task;
        FGraphEventRef completionEvent;

        FTaskEntry() = default;
        FTaskEntry(FRHIThreadTask&& InTask, FGraphEventRef InEvent)
            : task(std::move(InTask))
            , completionEvent(InEvent)
        {}
    };

    std::queue<FTaskEntry> m_taskQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;

    /** Statistics */
    std::atomic<uint32> m_totalTasksQueued;
    std::atomic<uint32> m_totalTasksCompleted;
    std::atomic<uint32> m_activeTasks;

    /** Thread ID for checking if we're on RHI thread */
    std::atomic<uint32> m_rhiThreadID;
};

/**
 * Helper to check if we're running RHI commands in a separate thread
 */
inline bool IsRunningRHIInSeparateThread() {
    return FRHIThread::IsInitialized();
}

/**
 * Scoped helper to execute code on the RHI thread
 * If already on RHI thread, executes immediately
 * Otherwise, queues the task and optionally waits
 */
class FRHIThreadScope {
public:
    template<typename LAMBDA>
    static void Execute(LAMBDA&& Lambda, bool bWait = true) {
        if (FRHIThread::IsInRHIThread()) {
            // Already on RHI thread, execute immediately
            Lambda();
        } else {
            // Queue to RHI thread
            auto event = FRHIThread::QueueTask([Lambda = Forward<LAMBDA>(Lambda)]() mutable {
                Lambda();
            });

            if (bWait && event) {
                event->Wait();
            }
        }
    }
};

} // namespace MonsterEngine
