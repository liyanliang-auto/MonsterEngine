// Copyright Monster Engine. All Rights Reserved.

#pragma once

#include "Core/CoreMinimal.h"
#include <thread>
#include <atomic>

namespace MonsterEngine {

class FRunnable;

/**
 * Thread priority enumeration
 * Based on UE5's EThreadPriority
 */
enum class EThreadPriority : uint8 {
    Normal,
    AboveNormal,
    BelowNormal,
    Highest,
    Lowest,
    SlightlyBelowNormal,
    TimeCritical
};

/**
 * Interface for runnable threads
 * 
 * This interface specifies the methods used to manage a thread's life cycle.
 * Based on UE5's FRunnableThread
 * Reference: Engine/Source/Runtime/Core/Public/HAL/RunnableThread.h
 */
class FRunnableThread {
public:
    /**
     * Factory method to create a thread with the specified parameters
     * 
     * @param InRunnable - The runnable object to execute
     * @param ThreadName - Name of the thread
     * @param InStackSize - The size of the stack to create (0 = default)
     * @param InThreadPri - Thread priority
     * @return The newly created thread or nullptr if it failed
     */
    static TUniquePtr<FRunnableThread> Create(
        FRunnable* InRunnable,
        const String& ThreadName,
        uint32 InStackSize = 0,
        EThreadPriority InThreadPri = EThreadPriority::Normal
    );
    
    /**
     * Constructor
     */
    FRunnableThread();
    
    /**
     * Virtual destructor
     */
    virtual ~FRunnableThread();
    
    /**
     * Changes the thread priority of the currently running thread
     * 
     * @param NewPriority - The thread priority to change to
     */
    virtual void SetThreadPriority(EThreadPriority NewPriority);
    
    /**
     * Tells the thread to either pause execution or resume
     * 
     * @param bShouldPause - Whether to pause the thread (true) or resume (false)
     */
    virtual void Suspend(bool bShouldPause = true);
    
    /**
     * Tells the thread to exit
     * 
     * The kill method calls Stop() on the runnable to kill the thread gracefully.
     * 
     * @param bShouldWait - If true, the call will wait for the thread to exit
     * @return Always true
     */
    virtual bool Kill(bool bShouldWait = true);
    
    /**
     * Halts the caller until this thread has completed its work
     */
    virtual void WaitForCompletion();
    
    /**
     * Thread ID for this thread
     * 
     * @return ID that was set by Create
     */
    uint32 GetThreadID() const {
        return m_threadID;
    }
    
    /**
     * Retrieves the given name of the thread
     * 
     * @return Name that was set by Create
     */
    const String& GetThreadName() const {
        return m_threadName;
    }
    
    /**
     * Returns the thread priority
     */
    EThreadPriority GetThreadPriority() const {
        return m_threadPriority;
    }
    
    /**
     * Check if thread is currently running
     */
    bool IsRunning() const {
        return m_isRunning.load(std::memory_order_acquire);
    }
    
protected:
    /**
     * The thread entry point
     * This is where the thread execution begins
     */
    uint32 Run();
    
    /** The runnable object to execute on this thread */
    FRunnable* m_runnable = nullptr;
    
    /** The actual thread object */
    TUniquePtr<std::thread> m_thread;
    
    /** Thread ID */
    uint32 m_threadID = 0;
    
    /** Thread name for debugging */
    String m_threadName;
    
    /** Thread priority */
    EThreadPriority m_threadPriority = EThreadPriority::Normal;
    
    /** Whether the thread is currently running */
    std::atomic<bool> m_isRunning{false};
    
    /** Whether the thread should stop */
    std::atomic<bool> m_shouldStop{false};
    
    /** Whether the thread is suspended */
    std::atomic<bool> m_isSuspended{false};
    
    /** Next thread ID to assign */
    static std::atomic<uint32> s_nextThreadID;
};

} // namespace MonsterEngine
