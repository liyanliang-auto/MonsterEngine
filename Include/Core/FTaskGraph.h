// Copyright Monster Engine. All Rights Reserved.

#pragma once

#include "Core/CoreMinimal.h"
#include "Core/FGraphEvent.h"
#include "Core/FRunnable.h"
#include "Core/FRunnableThread.h"
#include <queue>
#include <functional>

namespace MonsterEngine {

/**
 * Task delegate type
 * Function that will be executed by the task graph
 */
using FTaskDelegate = TFunction<void()>;

/**
 * Task graph system for parallel task execution
 * 
 * Manages a pool of worker threads that execute tasks asynchronously.
 * Tasks can have prerequisites (other tasks that must complete first)
 * and can return graph events for tracking completion.
 * 
 * Based on UE5's FTaskGraphInterface
 * Reference: Engine/Source/Runtime/Core/Public/Async/TaskGraphInterfaces.h
 */
class FTaskGraph {
public:
    /**
     * Initialize task graph with worker threads
     * 
     * @param NumThreads - Number of worker threads (0 = auto-detect based on CPU cores)
     */
    static void Initialize(uint32 NumThreads = 0);
    
    /**
     * Shutdown task graph and wait for all tasks
     * Blocks until all pending tasks complete
     */
    static void Shutdown();
    
    /**
     * Check if task graph is initialized
     */
    static bool IsInitialized();
    
    /**
     * Queue a task for execution
     * 
     * @param Task - Task delegate to execute
     * @param Prerequisites - Tasks that must complete before this task
     * @return Graph event for tracking completion
     */
    static FGraphEventRef QueueTask(
        FTaskDelegate&& Task,
        const FGraphEventArray& Prerequisites = {}
    );
    
    /**
     * Queue a task for execution with a name (for debugging)
     * 
     * @param TaskName - Name for debugging
     * @param Task - Task delegate to execute
     * @param Prerequisites - Tasks that must complete before this task
     * @return Graph event for tracking completion
     */
    static FGraphEventRef QueueNamedTask(
        const String& TaskName,
        FTaskDelegate&& Task,
        const FGraphEventArray& Prerequisites = {}
    );
    
    /**
     * Wait for all queued tasks to complete
     * Blocks until the task queue is empty
     */
    static void WaitForAllTasks();
    
    /**
     * Get number of worker threads
     */
    static uint32 GetNumWorkerThreads();
    
    /**
     * Get number of pending tasks
     */
    static uint32 GetNumPendingTasks();
    
private:
    /**
     * Task worker thread
     * Continuously processes tasks from the queue
     */
    class FTaskWorker : public FRunnable {
    public:
        FTaskWorker(FTaskGraph* InTaskGraph, uint32 InWorkerIndex);
        virtual ~FTaskWorker() override;
        
        virtual bool Init() override;
        virtual uint32 Run() override;
        virtual void Stop() override;
        virtual void Exit() override;
        
    private:
        FTaskGraph* m_taskGraph;
        uint32 m_workerIndex;
    };
    
    /**
     * Task entry in the queue
     */
    struct FTaskEntry {
        FTaskDelegate task;
        FGraphEventRef completionEvent;
        FGraphEventArray prerequisites;
        String taskName;
        
        FTaskEntry(FTaskDelegate&& InTask, FGraphEventRef InEvent, 
                  const FGraphEventArray& InPrereqs, const String& InName)
            : task(std::move(InTask))
            , completionEvent(InEvent)
            , prerequisites(InPrereqs)
            , taskName(InName)
        {}
    };
    
    /**
     * Constructor (private - singleton pattern)
     */
    FTaskGraph();
    
    /**
     * Destructor
     */
    ~FTaskGraph();
    
    /**
     * Get singleton instance
     */
    static FTaskGraph& Get();
    
    /**
     * Worker thread function
     * Processes tasks from the queue
     */
    void ProcessTasks(uint32 WorkerIndex);
    
    /**
     * Try to get next task from queue
     * 
     * @param OutTask - Output task entry
     * @return True if a task was retrieved
     */
    bool TryGetNextTask(FTaskEntry& OutTask);
    
    /**
     * Check if task prerequisites are complete
     */
    bool ArePrerequisitesComplete(const FGraphEventArray& Prerequisites);
    
    /** Singleton instance */
    static TUniquePtr<FTaskGraph> s_instance;
    
    /** Worker threads */
    TArray<TUniquePtr<FRunnableThread>> m_workerThreads;
    
    /** Worker runnable objects */
    TArray<TUniquePtr<FTaskWorker>> m_workers;
    
    /** Task queue */
    std::queue<FTaskEntry> m_taskQueue;
    
    /** Mutex for task queue */
    std::mutex m_queueMutex;
    
    /** Condition variable for task availability */
    std::condition_variable m_queueCV;
    
    /** Whether the task graph is shutting down */
    std::atomic<bool> m_isShuttingDown{false};
    
    /** Number of active tasks being processed */
    std::atomic<uint32> m_activeTasks{0};
    
    /** Total tasks queued */
    std::atomic<uint32> m_totalTasksQueued{0};
    
    /** Total tasks completed */
    std::atomic<uint32> m_totalTasksCompleted{0};
};

} // namespace MonsterEngine
