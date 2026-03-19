// Copyright Monster Engine. All Rights Reserved.

#include "Core/FRunnableThread.h"
#include "Core/FRunnable.h"
#include "Core/Log.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <processthreadsapi.h>
#endif

namespace MonsterEngine {

// Initialize static member
std::atomic<uint32> FRunnableThread::s_nextThreadID{1};

TUniquePtr<FRunnableThread> FRunnableThread::Create(
    FRunnable* InRunnable,
    const String& ThreadName,
    uint32 InStackSize,
    EThreadPriority InThreadPri
) {
    if (!InRunnable) {
        MR_LOG_ERROR("FRunnableThread::Create - InRunnable is null");
        return nullptr;
    }
    
    auto NewThread = MakeUnique<FRunnableThread>();
    NewThread->m_runnable = InRunnable;
    NewThread->m_threadName = ThreadName;
    NewThread->m_threadPriority = InThreadPri;
    NewThread->m_threadID = s_nextThreadID.fetch_add(1, std::memory_order_relaxed);
    
    // Initialize the runnable
    if (!InRunnable->Init()) {
        MR_LOG_ERROR("FRunnableThread::Create - Runnable Init() failed for thread: " + ThreadName);
        return nullptr;
    }
    
    // Create the actual thread
    try {
        NewThread->m_thread = MakeUnique<std::thread>([NewThread = NewThread.get()]() {
            NewThread->Run();
        });
        
        NewThread->m_isRunning.store(true, std::memory_order_release);
        
        MR_LOG_INFO("FRunnableThread::Create - Created thread: " + ThreadName + 
                   " (ID: " + std::to_string(NewThread->m_threadID) + ")");
    }
    catch (const std::exception& e) {
        MR_LOG_ERROR("FRunnableThread::Create - Failed to create thread: " + 
                    ThreadName + ", Error: " + e.what());
        return nullptr;
    }
    
    return NewThread;
}

FRunnableThread::FRunnableThread() {
}

FRunnableThread::~FRunnableThread() {
    if (m_isRunning.load(std::memory_order_acquire)) {
        Kill(true);
    }
}

void FRunnableThread::SetThreadPriority(EThreadPriority NewPriority) {
    m_threadPriority = NewPriority;
    
#ifdef PLATFORM_WINDOWS
    if (m_thread && m_thread->joinable()) {
        HANDLE threadHandle = m_thread->native_handle();
        int priority = THREAD_PRIORITY_NORMAL;
        
        switch (NewPriority) {
            case EThreadPriority::Highest:
                priority = THREAD_PRIORITY_HIGHEST;
                break;
            case EThreadPriority::AboveNormal:
                priority = THREAD_PRIORITY_ABOVE_NORMAL;
                break;
            case EThreadPriority::Normal:
                priority = THREAD_PRIORITY_NORMAL;
                break;
            case EThreadPriority::BelowNormal:
            case EThreadPriority::SlightlyBelowNormal:
                priority = THREAD_PRIORITY_BELOW_NORMAL;
                break;
            case EThreadPriority::Lowest:
                priority = THREAD_PRIORITY_LOWEST;
                break;
            case EThreadPriority::TimeCritical:
                priority = THREAD_PRIORITY_TIME_CRITICAL;
                break;
        }
        
        ::SetThreadPriority(threadHandle, priority);
        MR_LOG_DEBUG("FRunnableThread::SetThreadPriority - Set priority for thread: " + 
                    m_threadName + " to " + std::to_string(static_cast<int>(NewPriority)));
    }
#endif
}

void FRunnableThread::Suspend(bool bShouldPause) {
    m_isSuspended.store(bShouldPause, std::memory_order_release);
    
    if (bShouldPause) {
        MR_LOG_DEBUG("FRunnableThread::Suspend - Suspending thread: " + m_threadName);
    } else {
        MR_LOG_DEBUG("FRunnableThread::Suspend - Resuming thread: " + m_threadName);
    }
}

bool FRunnableThread::Kill(bool bShouldWait) {
    if (!m_isRunning.load(std::memory_order_acquire)) {
        return true;
    }
    
    MR_LOG_INFO("FRunnableThread::Kill - Stopping thread: " + m_threadName);
    
    // Signal the thread to stop
    m_shouldStop.store(true, std::memory_order_release);
    
    // Call Stop() on the runnable
    if (m_runnable) {
        m_runnable->Stop();
    }
    
    // Wait for thread completion if requested
    if (bShouldWait) {
        WaitForCompletion();
    }
    
    return true;
}

void FRunnableThread::WaitForCompletion() {
    if (m_thread && m_thread->joinable()) {
        MR_LOG_DEBUG("FRunnableThread::WaitForCompletion - Waiting for thread: " + m_threadName);
        m_thread->join();
        MR_LOG_DEBUG("FRunnableThread::WaitForCompletion - Thread completed: " + m_threadName);
    }
}

uint32 FRunnableThread::Run() {
    MR_LOG_INFO("FRunnableThread::Run - Thread started: " + m_threadName);
    
    uint32 exitCode = 0;
    
    if (m_runnable) {
        // Run the runnable
        exitCode = m_runnable->Run();
        
        // Call Exit() for cleanup
        m_runnable->Exit();
    }
    
    m_isRunning.store(false, std::memory_order_release);
    
    MR_LOG_INFO("FRunnableThread::Run - Thread finished: " + m_threadName + 
               " (Exit code: " + std::to_string(exitCode) + ")");
    
    return exitCode;
}

} // namespace MonsterEngine
