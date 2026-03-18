// Copyright Monster Engine. All Rights Reserved.

#pragma once

#include "Core/CoreMinimal.h"
#include "Core/FGraphEvent.h"
#include "RHI/IRHICommandList.h"
#include <atomic>
#include <mutex>
#include <queue>

namespace MonsterEngine {
namespace RHI {

// Forward declarations
class IRHICommandContext;

/**
 * FRHICommandListExecutor
 * 
 * Manages the execution of RHI command lists, coordinating between the render thread
 * and the RHI thread. This is the central hub for command list submission and execution.
 * 
 * Key responsibilities:
 * - Queue command lists for execution on the RHI thread
 * - Manage the immediate command list (used by render thread)
 * - Coordinate parallel command list translation and submission
 * - Handle synchronization between render and RHI threads
 * 
 * Based on UE5's FRHICommandListExecutor design.
 * Reference: UE5 RHICommandList.h, RHICommandList.cpp
 */
class FRHICommandListExecutor {
public:
    /**
     * Get the singleton instance
     */
    static FRHICommandListExecutor& Get();

    /**
     * Initialize the command list executor
     * @param bUseRHIThread Whether to use a dedicated RHI thread
     * @return True if initialization succeeded
     */
    static bool Initialize(bool bUseRHIThread = true);

    /**
     * Shutdown the command list executor
     */
    static void Shutdown();

    /**
     * Check if executor is initialized
     */
    static bool IsInitialized();

    /**
     * Get the immediate command list (used by render thread)
     * This is the main command list for recording rendering commands
     */
    IRHICommandList* GetImmediateCommandList();

    /**
     * Execute and reset the immediate command list
     * Submits all recorded commands to the RHI thread for execution
     */
    void ExecuteAndResetImmediate();

    /**
     * Queue a command list for asynchronous execution
     * @param CommandList The command list to execute
     * @param Prerequisites Events that must complete before this command list executes
     * @return Event that will be signaled when command list completes
     */
    FGraphEventRef QueueAsyncCommandListSubmit(
        IRHICommandList* CommandList,
        const FGraphEventArray& Prerequisites = FGraphEventArray()
    );

    /**
     * Wait for all pending command lists to complete on the RHI thread
     */
    void WaitForRHIThread();

    /**
     * Check if the RHI thread is idle
     */
    bool IsRHIThreadIdle();

    /**
     * Flush all pending commands and wait for completion
     * @param bWaitForCompletion If true, blocks until all commands complete
     */
    void Flush(bool bWaitForCompletion = true);

    /**
     * Get statistics
     */
    struct FStats {
        uint32 totalCommandListsQueued = 0;
        uint32 totalCommandListsExecuted = 0;
        uint32 pendingCommandLists = 0;
    };

    FStats GetStats() const;

private:
    FRHICommandListExecutor();
    ~FRHICommandListExecutor();

    /**
     * Execute a command list on the RHI thread
     */
    void ExecuteCommandList(IRHICommandList* CommandList);

    /**
     * Internal helper to submit command list to RHI thread
     */
    FGraphEventRef SubmitCommandListToRHIThread(IRHICommandList* CommandList);

private:
    /** Singleton instance */
    static TUniquePtr<FRHICommandListExecutor> s_instance;

    /** The immediate command list (owned by executor) */
    TUniquePtr<IRHICommandList> m_immediateCommandList;

    /** Whether to use RHI thread */
    bool m_bUseRHIThread;

    /** Command list queue entry */
    struct FCommandListEntry {
        IRHICommandList* commandList;
        FGraphEventRef completionEvent;
        FGraphEventArray prerequisites;

        FCommandListEntry() : commandList(nullptr) {}
        FCommandListEntry(IRHICommandList* InCmdList, FGraphEventRef InEvent, const FGraphEventArray& InPrereqs)
            : commandList(InCmdList)
            , completionEvent(InEvent)
            , prerequisites(InPrereqs)
        {}
    };

    /** Pending command lists */
    std::queue<FCommandListEntry> m_commandListQueue;
    std::mutex m_queueMutex;

    /** Statistics */
    std::atomic<uint32> m_totalCommandListsQueued;
    std::atomic<uint32> m_totalCommandListsExecuted;
};

/**
 * Scoped helper for flushing RHI commands
 * Automatically flushes on destruction
 */
class FRHICommandListScopedFlush {
public:
    FRHICommandListScopedFlush(bool bWaitForCompletion = true)
        : m_bWaitForCompletion(bWaitForCompletion)
    {}

    ~FRHICommandListScopedFlush() {
        if (FRHICommandListExecutor::IsInitialized()) {
            FRHICommandListExecutor::Get().Flush(m_bWaitForCompletion);
        }
    }

private:
    bool m_bWaitForCompletion;
};

} // namespace RHI
} // namespace MonsterEngine
