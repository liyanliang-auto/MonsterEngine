// Copyright Monster Engine. All Rights Reserved.

#include "Core/FTaskGraph.h"
#include "Core/Log.h"
#include <thread>

namespace MonsterEngine {

// Initialize static members
TUniquePtr<FTaskGraph> FTaskGraph::s_instance = nullptr;

void FTaskGraph::Initialize(uint32 NumThreads) {
    if (s_instance) {
        MR_LOG_WARNING("FTaskGraph::Initialize - Task graph already initialized");
        return;
    }
    
    // Auto-detect number of threads if not specified
    if (NumThreads == 0) {
        NumThreads = std::thread::hardware_concurrency();
        if (NumThreads == 0) {
            NumThreads = 4; // Fallback to 4 threads
        }
        // Reserve one core for main thread
        if (NumThreads > 1) {
            NumThreads -= 1;
        }
    }
    
    s_instance = MakeUnique<FTaskGraph>();
    
    MR_LOG_INFO("FTaskGraph::Initialize - Creating task graph with " + 
               std::to_string(NumThreads) + " worker threads");
    
    // Create worker threads
    s_instance->m_workers.reserve(NumThreads);
    s_instance->m_workerThreads.reserve(NumThreads);
    
    for (uint32 i = 0; i < NumThreads; ++i) {
        auto worker = MakeUnique<FTaskWorker>(s_instance.get(), i);
        auto thread = FRunnableThread::Create(
            worker.get(),
            "TaskWorker_" + std::to_string(i),
            0,
            EThreadPriority::Normal
        );
        
        if (thread) {
            s_instance->m_workers.push_back(std::move(worker));
            s_instance->m_workerThreads.push_back(std::move(thread));
        } else {
            MR_LOG_ERROR("FTaskGraph::Initialize - Failed to create worker thread " + std::to_string(i));
        }
    }
    
    MR_LOG_INFO("FTaskGraph::Initialize - Task graph initialized with " + 
               std::to_string(s_instance->m_workerThreads.size()) + " worker threads");
}

void FTaskGraph::Shutdown() {
    if (!s_instance) {
        return;
    }
    
    MR_LOG_INFO("FTaskGraph::Shutdown - Shutting down task graph");
    
    // Signal shutdown
    s_instance->m_isShuttingDown.store(true, std::memory_order_release);
    
    // Wake up all worker threads
    s_instance->m_queueCV.notify_all();
    
    // Wait for all worker threads to complete
    for (auto& thread : s_instance->m_workerThreads) {
        if (thread) {
            thread->WaitForCompletion();
        }
    }
    
    s_instance->m_workerThreads.clear();
    s_instance->m_workers.clear();
    
    MR_LOG_INFO("FTaskGraph::Shutdown - Task graph shutdown complete. " +
               std::to_string(s_instance->m_totalTasksCompleted.load()) + " tasks completed");
    
    s_instance.reset();
}

bool FTaskGraph::IsInitialized() {
    return s_instance != nullptr;
}

FGraphEventRef FTaskGraph::QueueTask(
    FTaskDelegate&& Task,
    const FGraphEventArray& Prerequisites
) {
    return QueueNamedTask("UnnamedTask", std::move(Task), Prerequisites);
}

FGraphEventRef FTaskGraph::QueueNamedTask(
    const String& TaskName,
    FTaskDelegate&& Task,
    const FGraphEventArray& Prerequisites
) {
    if (!s_instance) {
        MR_LOG_ERROR("FTaskGraph::QueueNamedTask - Task graph not initialized");
        return nullptr;
    }
    
    if (s_instance->m_isShuttingDown.load(std::memory_order_acquire)) {
        MR_LOG_WARNING("FTaskGraph::QueueNamedTask - Cannot queue task during shutdown: " + TaskName);
        return nullptr;
    }
    
    // Create completion event
    auto completionEvent = MakeGraphEvent();
    
    // If prerequisites are already complete, queue immediately
    if (Prerequisites.empty() || AreEventsComplete(Prerequisites)) {
        std::lock_guard<std::mutex> lock(s_instance->m_queueMutex);
        s_instance->m_taskQueue.emplace(std::move(Task), completionEvent, Prerequisites, TaskName);
        s_instance->m_totalTasksQueued.fetch_add(1, std::memory_order_relaxed);
        s_instance->m_queueCV.notify_one();
        
        MR_LOG_DEBUG("FTaskGraph::QueueNamedTask - Queued task: " + TaskName);
    } else {
        // Prerequisites not complete, create a wrapper task that waits
        auto wrappedTask = [Task = std::move(Task), Prerequisites, TaskName]() mutable {
            // Wait for prerequisites
            WaitForEvents(Prerequisites);
            // Execute the actual task
            Task();
        };
        
        std::lock_guard<std::mutex> lock(s_instance->m_queueMutex);
        s_instance->m_taskQueue.emplace(std::move(wrappedTask), completionEvent, FGraphEventArray{}, TaskName);
        s_instance->m_totalTasksQueued.fetch_add(1, std::memory_order_relaxed);
        s_instance->m_queueCV.notify_one();
        
        MR_LOG_DEBUG("FTaskGraph::QueueNamedTask - Queued task with prerequisites: " + TaskName);
    }
    
    return completionEvent;
}

void FTaskGraph::WaitForAllTasks() {
    if (!s_instance) {
        return;
    }
    
    MR_LOG_DEBUG("FTaskGraph::WaitForAllTasks - Waiting for all tasks to complete");
    
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
    
    MR_LOG_DEBUG("FTaskGraph::WaitForAllTasks - All tasks completed");
}

uint32 FTaskGraph::GetNumWorkerThreads() {
    if (!s_instance) {
        return 0;
    }
    return static_cast<uint32>(s_instance->m_workerThreads.size());
}

uint32 FTaskGraph::GetNumPendingTasks() {
    if (!s_instance) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(s_instance->m_queueMutex);
    return static_cast<uint32>(s_instance->m_taskQueue.size());
}

FTaskGraph::FTaskGraph() {
}

FTaskGraph::~FTaskGraph() {
}

FTaskGraph& FTaskGraph::Get() {
    return *s_instance;
}

void FTaskGraph::ProcessTasks(uint32 WorkerIndex) {
    MR_LOG_DEBUG("FTaskGraph::ProcessTasks - Worker " + std::to_string(WorkerIndex) + " started");
    
    while (!m_isShuttingDown.load(std::memory_order_acquire)) {
        FTaskEntry taskEntry("", nullptr, {}, "");
        
        if (TryGetNextTask(taskEntry)) {
            // Execute the task
            m_activeTasks.fetch_add(1, std::memory_order_relaxed);
            
            try {
                MR_LOG_DEBUG("FTaskGraph::ProcessTasks - Worker " + std::to_string(WorkerIndex) + 
                           " executing task: " + taskEntry.taskName);
                
                taskEntry.task();
                
                // Mark completion event as complete
                if (taskEntry.completionEvent) {
                    taskEntry.completionEvent->Complete();
                }
                
                m_totalTasksCompleted.fetch_add(1, std::memory_order_relaxed);
            }
            catch (const std::exception& e) {
                MR_LOG_ERROR("FTaskGraph::ProcessTasks - Exception in task " + 
                           taskEntry.taskName + ": " + e.what());
                
                // Still mark as complete to avoid deadlocks
                if (taskEntry.completionEvent) {
                    taskEntry.completionEvent->Complete();
                }
            }
            
            m_activeTasks.fetch_sub(1, std::memory_order_relaxed);
        } else {
            // No tasks available, wait
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCV.wait_for(lock, std::chrono::milliseconds(10), [this]() {
                return !m_taskQueue.empty() || m_isShuttingDown.load(std::memory_order_acquire);
            });
        }
    }
    
    MR_LOG_DEBUG("FTaskGraph::ProcessTasks - Worker " + std::to_string(WorkerIndex) + " stopped");
}

bool FTaskGraph::TryGetNextTask(FTaskEntry& OutTask) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    
    if (m_taskQueue.empty()) {
        return false;
    }
    
    // Get task from queue
    OutTask = std::move(m_taskQueue.front());
    m_taskQueue.pop();
    
    return true;
}

bool FTaskGraph::ArePrerequisitesComplete(const FGraphEventArray& Prerequisites) {
    return AreEventsComplete(Prerequisites);
}

// FTaskWorker implementation
FTaskGraph::FTaskWorker::FTaskWorker(FTaskGraph* InTaskGraph, uint32 InWorkerIndex)
    : m_taskGraph(InTaskGraph)
    , m_workerIndex(InWorkerIndex)
{
}

FTaskGraph::FTaskWorker::~FTaskWorker() {
}

bool FTaskGraph::FTaskWorker::Init() {
    MR_LOG_DEBUG("FTaskWorker::Init - Worker " + std::to_string(m_workerIndex) + " initialized");
    return true;
}

uint32 FTaskGraph::FTaskWorker::Run() {
    if (m_taskGraph) {
        m_taskGraph->ProcessTasks(m_workerIndex);
    }
    return 0;
}

void FTaskGraph::FTaskWorker::Stop() {
    MR_LOG_DEBUG("FTaskWorker::Stop - Worker " + std::to_string(m_workerIndex) + " stopping");
}

void FTaskGraph::FTaskWorker::Exit() {
    MR_LOG_DEBUG("FTaskWorker::Exit - Worker " + std::to_string(m_workerIndex) + " exited");
}

} // namespace MonsterEngine
