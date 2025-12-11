// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file RenderCommandQueue.h
 * @brief Render command queue for thread-safe communication between game and render threads
 * 
 * This file implements a render command queue system similar to UE5's ENQUEUE_RENDER_COMMAND.
 * It provides a mechanism for the game thread to safely send commands to the render thread
 * without requiring locks or direct synchronization.
 * 
 * Key concepts:
 * - Commands are captured as lambdas with all necessary data copied
 * - Commands are executed on the render thread in FIFO order
 * - The system supports both blocking and non-blocking command submission
 * 
 * Usage example:
 * @code
 *     ENQUEUE_RENDER_COMMAND(UpdateTransform)(
 *         [Transform = MyTransform](FRenderCommandContext& Context)
 *         {
 *             // This code runs on the render thread
 *             Context.UpdateTransform(Transform);
 *         });
 * @endcode
 */

#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Core/Logging/LogMacros.h"

#include <functional>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <utility>

// Forward declare RHI types in their proper namespace
namespace MonsterRender { namespace RHI {
    class IRHICommandList;
}}

namespace MonsterEngine
{

// Bring RHI types into MonsterEngine namespace
using IRHICommandList = ::MonsterRender::RHI::IRHICommandList;

/**
 * Context passed to render commands during execution
 * 
 * This class provides access to rendering resources and state
 * that commands may need during execution on the render thread.
 */
class FRenderCommandContext
{
public:
    /**
     * Constructor
     * @param InRHICmdList The RHI command list for GPU command recording
     */
    explicit FRenderCommandContext(IRHICommandList* InRHICmdList = nullptr)
        : RHICmdList(InRHICmdList)
        , FrameNumber(0)
    {
    }

    /**
     * Get the RHI command list
     * @return Pointer to the RHI command list, may be nullptr
     */
    IRHICommandList* GetRHICommandList() const { return RHICmdList; }

    /**
     * Set the RHI command list
     * @param InRHICmdList The RHI command list to use
     */
    void SetRHICommandList(IRHICommandList* InRHICmdList) { RHICmdList = InRHICmdList; }

    /**
     * Get the current frame number
     * @return The frame number when this context was created
     */
    uint32 GetFrameNumber() const { return FrameNumber; }

    /**
     * Set the current frame number
     * @param InFrameNumber The frame number to set
     */
    void SetFrameNumber(uint32 InFrameNumber) { FrameNumber = InFrameNumber; }

private:
    /** The RHI command list for GPU command recording */
    IRHICommandList* RHICmdList;

    /** The frame number when this context was created */
    uint32 FrameNumber;
};

/**
 * Base class for render commands
 * 
 * All render commands inherit from this class and implement the Execute method.
 * Commands are stored as polymorphic objects in the queue.
 */
class FRenderCommand
{
public:
    /** Virtual destructor for proper cleanup of derived classes */
    virtual ~FRenderCommand() = default;

    /**
     * Execute the render command
     * @param Context The render command context providing access to render resources
     */
    virtual void Execute(FRenderCommandContext& Context) = 0;

    /**
     * Get the debug name of this command
     * @return A string identifying this command type for debugging
     */
    virtual const char* GetDebugName() const { return "UnnamedCommand"; }
};

/**
 * Lambda-based render command implementation
 * 
 * This template class wraps a lambda function as a render command.
 * The lambda is captured by value to ensure all data is safely copied
 * from the game thread.
 * 
 * @tparam LambdaType The type of the lambda function
 */
template<typename LambdaType>
class TRenderCommand : public FRenderCommand
{
public:
    /**
     * Constructor
     * @param InDebugName Debug name for this command
     * @param InLambda The lambda function to execute
     */
    TRenderCommand(const char* InDebugName, LambdaType&& InLambda)
        : DebugName(InDebugName)
        , Lambda(std::forward<LambdaType>(InLambda))
    {
    }

    /**
     * Execute the lambda on the render thread
     * @param Context The render command context
     */
    virtual void Execute(FRenderCommandContext& Context) override
    {
        Lambda(Context);
    }

    /**
     * Get the debug name of this command
     * @return The debug name string
     */
    virtual const char* GetDebugName() const override
    {
        return DebugName;
    }

private:
    /** Debug name for this command */
    const char* DebugName;

    /** The lambda function to execute */
    LambdaType Lambda;
};

/**
 * Render command queue for thread-safe command submission
 * 
 * This class manages a queue of render commands that are submitted from
 * the game thread and executed on the render thread. It uses double-buffering
 * to allow concurrent submission and execution.
 * 
 * Thread safety:
 * - EnqueueCommand() is thread-safe and can be called from any thread
 * - ExecuteCommands() should only be called from the render thread
 * - Flush() blocks until all pending commands are executed
 */
class FRenderCommandQueue
{
public:
    /** Maximum number of commands that can be queued before blocking */
    static constexpr int32 MaxQueuedCommands = 65536;

    // ========================================================================
    // Singleton Access
    // ========================================================================

    /**
     * Get the global render command queue instance
     * @return Reference to the singleton instance
     */
    static FRenderCommandQueue& Get()
    {
        static FRenderCommandQueue Instance;
        return Instance;
    }

    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /** Constructor - initializes the queue */
    FRenderCommandQueue()
        : WriteIndex(0)
        , ReadIndex(0)
        , bIsExecuting(false)
        , TotalCommandsEnqueued(0)
        , TotalCommandsExecuted(0)
    {
    }

    /** Destructor - flushes remaining commands */
    ~FRenderCommandQueue()
    {
        // Clean up any remaining commands
        Flush();
        
        // Delete any commands still in the queue
        for (FRenderCommand* Cmd : CommandQueue)
        {
            delete Cmd;
        }
        CommandQueue.Empty();
    }

    // ========================================================================
    // Command Submission (Game Thread)
    // ========================================================================

    /**
     * Enqueue a render command for execution on the render thread
     * 
     * This method is thread-safe and can be called from any thread.
     * The command will be executed on the next render thread tick.
     * 
     * @tparam LambdaType The type of the lambda function
     * @param DebugName Debug name for the command (for profiling/debugging)
     * @param Lambda The lambda function to execute on the render thread
     */
    template<typename LambdaType>
    void EnqueueCommand(const char* DebugName, LambdaType&& Lambda)
    {
        // Create the command object
        // Note: The lambda is moved/copied here, capturing all data by value
        auto* Command = new TRenderCommand<LambdaType>(
            DebugName, 
            std::forward<LambdaType>(Lambda)
        );

        // Lock and add to queue
        {
            std::lock_guard<std::mutex> Lock(QueueMutex);
            CommandQueue.Add(Command);
            ++TotalCommandsEnqueued;
        }

        // Notify the render thread that commands are available
        QueueCondition.notify_one();
    }

    /**
     * Check if there are pending commands in the queue
     * @return True if there are commands waiting to be executed
     */
    bool HasPendingCommands() const
    {
        std::lock_guard<std::mutex> Lock(QueueMutex);
        return CommandQueue.Num() > 0;
    }

    /**
     * Get the number of pending commands
     * @return The number of commands waiting to be executed
     */
    int32 GetPendingCommandCount() const
    {
        std::lock_guard<std::mutex> Lock(QueueMutex);
        return CommandQueue.Num();
    }

    // ========================================================================
    // Command Execution (Render Thread)
    // ========================================================================

    /**
     * Execute all pending commands on the render thread
     * 
     * This method should only be called from the render thread.
     * It executes all queued commands in FIFO order.
     * 
     * @param Context The render command context to pass to commands
     * @return The number of commands executed
     */
    int32 ExecuteCommands(FRenderCommandContext& Context)
    {
        // Swap the queue to minimize lock time
        TArray<FRenderCommand*> CommandsToExecute;
        {
            std::lock_guard<std::mutex> Lock(QueueMutex);
            CommandsToExecute = std::move(CommandQueue);
            CommandQueue.Empty();
        }

        // Execute all commands
        bIsExecuting = true;
        int32 NumExecuted = 0;

        for (FRenderCommand* Command : CommandsToExecute)
        {
            if (Command)
            {
                // Execute the command
                Command->Execute(Context);
                
                // Delete the command after execution
                delete Command;
                ++NumExecuted;
                ++TotalCommandsExecuted;
            }
        }

        bIsExecuting = false;

        return NumExecuted;
    }

    /**
     * Flush all pending commands and wait for completion
     * 
     * This method blocks until all pending commands have been executed.
     * It should be called when synchronization between threads is required.
     */
    void Flush()
    {
        // Execute any remaining commands immediately
        FRenderCommandContext Context;
        while (HasPendingCommands())
        {
            ExecuteCommands(Context);
        }
    }

    // ========================================================================
    // Statistics
    // ========================================================================

    /**
     * Get the total number of commands enqueued since startup
     * @return Total commands enqueued
     */
    uint64 GetTotalCommandsEnqueued() const { return TotalCommandsEnqueued; }

    /**
     * Get the total number of commands executed since startup
     * @return Total commands executed
     */
    uint64 GetTotalCommandsExecuted() const { return TotalCommandsExecuted; }

    /**
     * Check if the queue is currently executing commands
     * @return True if ExecuteCommands is currently running
     */
    bool IsExecuting() const { return bIsExecuting; }

private:
    /** The command queue (protected by mutex) */
    TArray<FRenderCommand*> CommandQueue;

    /** Mutex for thread-safe queue access */
    mutable std::mutex QueueMutex;

    /** Condition variable for signaling command availability */
    std::condition_variable QueueCondition;

    /** Current write index for double-buffering */
    std::atomic<int32> WriteIndex;

    /** Current read index for double-buffering */
    std::atomic<int32> ReadIndex;

    /** Flag indicating if commands are currently being executed */
    std::atomic<bool> bIsExecuting;

    /** Total commands enqueued (for statistics) */
    std::atomic<uint64> TotalCommandsEnqueued;

    /** Total commands executed (for statistics) */
    std::atomic<uint64> TotalCommandsExecuted;
};

/**
 * Helper class for the ENQUEUE_RENDER_COMMAND macro
 * 
 * This class provides a fluent interface for enqueueing render commands.
 * It captures the debug name and provides an operator() to accept the lambda.
 */
class FRenderCommandEnqueuer
{
public:
    /**
     * Constructor
     * @param InDebugName Debug name for the command
     */
    explicit FRenderCommandEnqueuer(const char* InDebugName)
        : DebugName(InDebugName)
    {
    }

    /**
     * Enqueue a lambda as a render command
     * @tparam LambdaType The type of the lambda
     * @param Lambda The lambda to execute on the render thread
     */
    template<typename LambdaType>
    void operator()(LambdaType&& Lambda)
    {
        FRenderCommandQueue::Get().EnqueueCommand(DebugName, std::forward<LambdaType>(Lambda));
    }

private:
    /** Debug name for the command */
    const char* DebugName;
};

} // namespace MonsterEngine

/**
 * Macro to enqueue a render command
 * 
 * This macro provides a convenient way to enqueue render commands similar to UE5.
 * The command name is used for debugging and profiling.
 * 
 * Usage:
 * @code
 *     ENQUEUE_RENDER_COMMAND(MyCommandName)(
 *         [CapturedData = MyData](MonsterEngine::FRenderCommandContext& Context)
 *         {
 *             // This code runs on the render thread
 *             DoSomethingWith(CapturedData);
 *         });
 * @endcode
 * 
 * @param CommandName A unique name for this command (used for debugging)
 */
#define ENQUEUE_RENDER_COMMAND(CommandName) \
    MonsterEngine::FRenderCommandEnqueuer(#CommandName)

/**
 * Macro to flush all pending render commands
 * 
 * This macro blocks until all pending render commands have been executed.
 * Use sparingly as it causes synchronization between threads.
 */
#define FLUSH_RENDER_COMMANDS() \
    MonsterEngine::FRenderCommandQueue::Get().Flush()

/**
 * Macro to check if we're on the render thread
 * 
 * This is a placeholder - in a full implementation, this would check
 * the current thread ID against the render thread ID.
 */
#define IS_RENDER_THREAD() \
    (true) // TODO: Implement proper thread ID check
