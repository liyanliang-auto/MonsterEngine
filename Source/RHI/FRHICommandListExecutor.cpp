// Copyright Monster Engine. All Rights Reserved.

#include "RHI/FRHICommandListExecutor.h"
#include "RHI/FRHIThread.h"
#include "Core/Log.h"

namespace MonsterEngine {
namespace RHI {

// Initialize static members
TUniquePtr<FRHICommandListExecutor> FRHICommandListExecutor::s_instance = nullptr;

FRHICommandListExecutor::FRHICommandListExecutor()
    : m_bUseRHIThread(false)
    , m_totalCommandListsQueued(0)
    , m_totalCommandListsExecuted(0)
{
}

FRHICommandListExecutor::~FRHICommandListExecutor() {
}

FRHICommandListExecutor& FRHICommandListExecutor::Get() {
    if (!s_instance) {
        MR_LOG_ERROR("FRHICommandListExecutor::Get - Executor not initialized");
        static FRHICommandListExecutor dummy;
        return dummy;
    }
    return *s_instance;
}

bool FRHICommandListExecutor::Initialize(bool bUseRHIThread) {
    if (s_instance) {
        MR_LOG_WARNING("FRHICommandListExecutor::Initialize - Executor already initialized");
        return true;
    }

    MR_LOG_INFO("FRHICommandListExecutor::Initialize - Initializing command list executor");

    s_instance = MakeUnique<FRHICommandListExecutor>();
    s_instance->m_bUseRHIThread = bUseRHIThread;

    // Initialize RHI thread if requested
    if (bUseRHIThread) {
        if (!FRHIThread::Initialize(true)) {
            MR_LOG_ERROR("FRHICommandListExecutor::Initialize - Failed to initialize RHI thread");
            s_instance.reset();
            return false;
        }
    }

    // TODO: Create immediate command list based on current RHI
    // For now, we'll create it when needed
    // s_instance->m_immediateCommandList = CreateImmediateCommandList();

    MR_LOG_INFO("FRHICommandListExecutor::Initialize - Command list executor initialized successfully");

    return true;
}

void FRHICommandListExecutor::Shutdown() {
    if (!s_instance) {
        return;
    }

    MR_LOG_INFO("FRHICommandListExecutor::Shutdown - Shutting down command list executor");

    // Flush all pending commands
    s_instance->Flush(true);

    // Shutdown RHI thread
    if (s_instance->m_bUseRHIThread) {
        FRHIThread::Shutdown();
    }

    // Clear immediate command list
    s_instance->m_immediateCommandList.reset();

    MR_LOG_INFO("FRHICommandListExecutor::Shutdown - Command list executor shutdown complete. " +
               std::to_string(s_instance->m_totalCommandListsExecuted.load()) + " command lists executed");

    s_instance.reset();
}

bool FRHICommandListExecutor::IsInitialized() {
    return s_instance != nullptr;
}

IRHICommandList* FRHICommandListExecutor::GetImmediateCommandList() {
    if (!m_immediateCommandList) {
        MR_LOG_WARNING("FRHICommandListExecutor::GetImmediateCommandList - Immediate command list not created yet");
        // TODO: Create immediate command list based on current RHI
        return nullptr;
    }
    return m_immediateCommandList.get();
}

void FRHICommandListExecutor::ExecuteAndResetImmediate() {
    if (!m_immediateCommandList) {
        MR_LOG_WARNING("FRHICommandListExecutor::ExecuteAndResetImmediate - No immediate command list");
        return;
    }

    MR_LOG_DEBUG("FRHICommandListExecutor::ExecuteAndResetImmediate - Executing immediate command list");

    // Submit to RHI thread
    auto event = SubmitCommandListToRHIThread(m_immediateCommandList.get());

    // TODO: Reset the immediate command list for next frame
    // m_immediateCommandList->Reset();

    MR_LOG_DEBUG("FRHICommandListExecutor::ExecuteAndResetImmediate - Immediate command list submitted");
}

FGraphEventRef FRHICommandListExecutor::QueueAsyncCommandListSubmit(
    IRHICommandList* CommandList,
    const FGraphEventArray& Prerequisites
) {
    if (!CommandList) {
        MR_LOG_ERROR("FRHICommandListExecutor::QueueAsyncCommandListSubmit - CommandList is null");
        return nullptr;
    }

    // Create completion event
    auto completionEvent = MakeGraphEvent();

    // Queue the command list
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_commandListQueue.emplace(CommandList, completionEvent, Prerequisites);
        m_totalCommandListsQueued.fetch_add(1, std::memory_order_relaxed);
    }

    MR_LOG_DEBUG("FRHICommandListExecutor::QueueAsyncCommandListSubmit - Command list queued");

    // Submit to RHI thread
    if (m_bUseRHIThread) {
        // Queue task to RHI thread that waits for prerequisites then executes
        FRHIThread::QueueTask([this, CommandList, Prerequisites, completionEvent]() {
            // Wait for prerequisites
            if (!Prerequisites.empty()) {
                WaitForEvents(Prerequisites);
            }

            // Execute the command list
            ExecuteCommandList(CommandList);

            // Mark as complete
            completionEvent->Complete();

            m_totalCommandListsExecuted.fetch_add(1, std::memory_order_relaxed);
        });
    } else {
        // Execute immediately on render thread
        if (!Prerequisites.empty()) {
            WaitForEvents(Prerequisites);
        }

        ExecuteCommandList(CommandList);
        completionEvent->Complete();

        m_totalCommandListsExecuted.fetch_add(1, std::memory_order_relaxed);
    }

    return completionEvent;
}

void FRHICommandListExecutor::WaitForRHIThread() {
    if (m_bUseRHIThread) {
        MR_LOG_DEBUG("FRHICommandListExecutor::WaitForRHIThread - Waiting for RHI thread");
        FRHIThread::WaitForTasks();
        MR_LOG_DEBUG("FRHICommandListExecutor::WaitForRHIThread - RHI thread idle");
    }
}

bool FRHICommandListExecutor::IsRHIThreadIdle() {
    if (m_bUseRHIThread) {
        return FRHIThread::IsIdle();
    }
    return true;
}

void FRHICommandListExecutor::Flush(bool bWaitForCompletion) {
    MR_LOG_DEBUG("FRHICommandListExecutor::Flush - Flushing RHI commands (Wait: " + 
                std::to_string(bWaitForCompletion) + ")");

    // Execute immediate command list if it has commands
    if (m_immediateCommandList) {
        ExecuteAndResetImmediate();
    }

    // Wait for all pending commands if requested
    if (bWaitForCompletion) {
        WaitForRHIThread();
    }

    MR_LOG_DEBUG("FRHICommandListExecutor::Flush - Flush complete");
}

FRHICommandListExecutor::FStats FRHICommandListExecutor::GetStats() const {
    FStats stats;
    stats.totalCommandListsQueued = m_totalCommandListsQueued.load(std::memory_order_relaxed);
    stats.totalCommandListsExecuted = m_totalCommandListsExecuted.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(m_queueMutex);
    stats.pendingCommandLists = static_cast<uint32>(m_commandListQueue.size());

    return stats;
}

void FRHICommandListExecutor::ExecuteCommandList(IRHICommandList* CommandList) {
    if (!CommandList) {
        return;
    }

    MR_LOG_DEBUG("FRHICommandListExecutor::ExecuteCommandList - Executing command list");

    // TODO: Implement actual command list execution
    // This will involve:
    // 1. Translating high-level RHI commands to platform-specific API calls
    // 2. Submitting to the GPU via the platform RHI (Vulkan, OpenGL, etc.)
    // 3. Managing synchronization and resource transitions
    //
    // For now, this is a placeholder that will be implemented when we
    // integrate with the actual command list recording system

    MR_LOG_DEBUG("FRHICommandListExecutor::ExecuteCommandList - Command list executed");
}

FGraphEventRef FRHICommandListExecutor::SubmitCommandListToRHIThread(IRHICommandList* CommandList) {
    if (!CommandList) {
        return nullptr;
    }

    // Create completion event
    auto completionEvent = MakeGraphEvent();

    if (m_bUseRHIThread) {
        // Queue to RHI thread
        FRHIThread::QueueTask([this, CommandList, completionEvent]() {
            ExecuteCommandList(CommandList);
            completionEvent->Complete();
            m_totalCommandListsExecuted.fetch_add(1, std::memory_order_relaxed);
        });
    } else {
        // Execute immediately
        ExecuteCommandList(CommandList);
        completionEvent->Complete();
        m_totalCommandListsExecuted.fetch_add(1, std::memory_order_relaxed);
    }

    return completionEvent;
}

} // namespace RHI
} // namespace MonsterEngine
