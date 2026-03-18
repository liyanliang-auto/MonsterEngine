// Copyright Monster Engine. All Rights Reserved.

#include "RHI/FRHIThread.h"
#include "Core/Log.h"
#include <thread>

namespace MonsterEngine {

// Initialize static members
TUniquePtr<FRHIThread> FRHIThread::s_instance = nullptr;

FRHIThread::FRHIThread()
    : m_bUseRHIThread(false)
    , m_bIsShuttingDown(false)
    , m_totalTasksQueued(0)
    , m_totalTasksCompleted(0)
    , m_activeTasks(0)
    , m_rhiThreadID(0)
{
}

FRHIThread::~FRHIThread() {
}

FRHIThread& FRHIThread::Get() {
    if (!s_instance) {
        MR_LOG_ERROR("FRHIThread::Get - RHI thread not initialized");
        static FRHIThread dummy;
        return dummy;
    }
    return *s_instance;
}

bool FRHIThread::Initialize(bool bUseRHIThread) {
    if (s_instance) {
        MR_LOG_WARNING("FRHIThread::Initialize - RHI thread already initialized");
        return true;
    }

    s_instance = MakeUnique<FRHIThread>();
    s_instance->m_bUseRHIThread = bUseRHIThread;

    if (bUseRHIThread) {
        MR_LOG_INFO("FRHIThread::Initialize - Creating dedicated RHI thread");

        // Create the RHI thread
        s_instance->m_thread = FRunnableThread::Create(
            s_instance.get(),
            "RHIThread",
            0,
            EThreadPriority::AboveNormal  // RHI thread runs at high priority
        );

        if (!s_instance->m_thread) {
            MR_LOG_ERROR("FRHIThread::Initialize - Failed to create RHI thread");
            s_instance.reset();
            return false;
        }

        MR_LOG_INFO("FRHIThread::Initialize - RHI thread created successfully");
    } else {
        MR_LOG_INFO("FRHIThread::Initialize - RHI commands will execute on render thread");
    }

    return true;
}

void FRHIThread::Shutdown() {
    if (!s_instance) {
        return;
    }

    MR_LOG_INFO("FRHIThread::Shutdown - Shutting down RHI thread");

    // Signal shutdown
    s_instance->m_bIsShuttingDown.store(true, std::memory_order_release);

    // Wake up the thread
    s_instance->m_queueCV.notify_all();

    // Wait for thread to complete
    if (s_instance->m_thread) {
        s_instance->m_thread->WaitForCompletion();
        s_instance->m_thread.reset();
    }

    MR_LOG_INFO("FRHIThread::Shutdown - RHI thread shutdown complete. " +
               std::to_string(s_instance->m_totalTasksCompleted.load()) + " tasks completed");

    s_instance.reset();
}

bool FRHIThread::IsInitialized() {
    return s_instance != nullptr;
}

bool FRHIThread::IsInRHIThread() {
    if (!s_instance || !s_instance->m_bUseRHIThread) {
        // No dedicated RHI thread, so we're always "on" the RHI thread
        return true;
    }

    // Check if current thread ID matches RHI thread ID
    uint32 currentThreadID = static_cast<uint32>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    return currentThreadID == s_instance->m_rhiThreadID.load(std::memory_order_acquire);
}

FGraphEventRef FRHIThread::QueueTask(FRHIThreadTask&& Task) {
    if (!s_instance) {
        MR_LOG_ERROR("FRHIThread::QueueTask - RHI thread not initialized");
        return nullptr;
    }

    if (s_instance->m_bIsShuttingDown.load(std::memory_order_acquire)) {
        MR_LOG_WARNING("FRHIThread::QueueTask - Cannot queue task during shutdown");
        return nullptr;
    }

    // If not using RHI thread, execute immediately
    if (!s_instance->m_bUseRHIThread) {
        Task();
        auto event = MakeGraphEvent();
        event->Complete();
        return event;
    }

    // Create completion event
    auto completionEvent = MakeGraphEvent();

    // Queue the task
    {
        std::lock_guard<std::mutex> lock(s_instance->m_queueMutex);
        s_instance->m_taskQueue.emplace(std::move(Task), completionEvent);
        s_instance->m_totalTasksQueued.fetch_add(1, std::memory_order_relaxed);
    }

    // Wake up the RHI thread
    s_instance->m_queueCV.notify_one();

    MR_LOG_DEBUG("FRHIThread::QueueTask - Task queued to RHI thread");

    return completionEvent;
}

void FRHIThread::WaitForTasks() {
    if (!s_instance) {
        return;
    }

    MR_LOG_DEBUG("FRHIThread::WaitForTasks - Waiting for RHI thread to complete all tasks");

    // Wait until queue is empty and no active tasks
    while (true) {
        {
            std::lock_guard<std::mutex> lock(s_instance->m_queueMutex);
            if (s_instance->m_taskQueue.empty() && 
                s_instance->m_activeTasks.load(std::memory_order_acquire) == 0) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    MR_LOG_DEBUG("FRHIThread::WaitForTasks - All RHI tasks completed");
}

bool FRHIThread::IsIdle() {
    if (!s_instance) {
        return true;
    }

    std::lock_guard<std::mutex> lock(s_instance->m_queueMutex);
    return s_instance->m_taskQueue.empty() && 
           s_instance->m_activeTasks.load(std::memory_order_acquire) == 0;
}

bool FRHIThread::Init() {
    // Store the RHI thread ID
    m_rhiThreadID.store(static_cast<uint32>(std::hash<std::thread::id>{}(std::this_thread::get_id())), 
                       std::memory_order_release);

    MR_LOG_INFO("FRHIThread::Init - RHI thread initialized (ThreadID: " + 
               std::to_string(m_rhiThreadID.load()) + ")");
    return true;
}

uint32 FRHIThread::Run() {
    MR_LOG_INFO("FRHIThread::Run - RHI thread started");

    ProcessTasks();

    MR_LOG_INFO("FRHIThread::Run - RHI thread finished");
    return 0;
}

void FRHIThread::Stop() {
    MR_LOG_INFO("FRHIThread::Stop - RHI thread stopping");
}

void FRHIThread::Exit() {
    MR_LOG_INFO("FRHIThread::Exit - RHI thread exited");
}

void FRHIThread::ProcessTasks() {
    while (!m_bIsShuttingDown.load(std::memory_order_acquire)) {
        FRHIThreadTask task;
        FGraphEventRef event;

        if (TryGetNextTask(task, event)) {
            // Execute the task
            m_activeTasks.fetch_add(1, std::memory_order_relaxed);

            try {
                MR_LOG_DEBUG("FRHIThread::ProcessTasks - Executing RHI task");

                task();

                // Mark completion event as complete
                if (event) {
                    event->Complete();
                }

                m_totalTasksCompleted.fetch_add(1, std::memory_order_relaxed);
            }
            catch (const std::exception& e) {
                MR_LOG_ERROR("FRHIThread::ProcessTasks - Exception in RHI task: " + String(e.what()));

                // Still mark as complete to avoid deadlocks
                if (event) {
                    event->Complete();
                }
            }

            m_activeTasks.fetch_sub(1, std::memory_order_relaxed);
        } else {
            // No tasks available, wait
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCV.wait_for(lock, std::chrono::milliseconds(10), [this]() {
                return !m_taskQueue.empty() || m_bIsShuttingDown.load(std::memory_order_acquire);
            });
        }
    }

    MR_LOG_INFO("FRHIThread::ProcessTasks - Processing remaining tasks before shutdown");

    // Process remaining tasks
    FRHIThreadTask task;
    FGraphEventRef event;
    while (TryGetNextTask(task, event)) {
        try {
            task();
            if (event) {
                event->Complete();
            }
            m_totalTasksCompleted.fetch_add(1, std::memory_order_relaxed);
        }
        catch (const std::exception& e) {
            MR_LOG_ERROR("FRHIThread::ProcessTasks - Exception during shutdown: " + String(e.what()));
            if (event) {
                event->Complete();
            }
        }
    }
}

bool FRHIThread::TryGetNextTask(FRHIThreadTask& OutTask, FGraphEventRef& OutEvent) {
    std::lock_guard<std::mutex> lock(m_queueMutex);

    if (m_taskQueue.empty()) {
        return false;
    }

    // Get task from queue
    FTaskEntry entry = std::move(m_taskQueue.front());
    m_taskQueue.pop();

    OutTask = std::move(entry.task);
    OutEvent = entry.completionEvent;

    return true;
}

} // namespace MonsterEngine
