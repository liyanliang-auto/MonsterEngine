// Copyright Monster Engine. All Rights Reserved.

#pragma once

#include "Core/CoreMinimal.h"

namespace MonsterEngine {

/**
 * Interface for "runnable" objects
 * 
 * A runnable object is an object that is "run" on an arbitrary thread.
 * The call usage pattern is Init(), Run(), Exit().
 * The thread that is going to "run" this object always uses those calling semantics.
 * 
 * Based on UE5's FRunnable interface
 * Reference: Engine/Source/Runtime/Core/Public/HAL/Runnable.h
 */
class FRunnable {
public:
    /**
     * Initializes the runnable object
     * 
     * This method is called in the context of the thread object that aggregates this,
     * not the thread that passes this runnable to a new thread.
     * 
     * @return True if initialization was successful, false otherwise
     */
    virtual bool Init() {
        return true;
    }
    
    /**
     * Runs the runnable object
     * 
     * This is where all per object thread work is done.
     * This is only called if the initialization was successful.
     * 
     * @return The exit code of the runnable object
     */
    virtual uint32 Run() = 0;
    
    /**
     * Stops the runnable object
     * 
     * This is called if a thread is requested to terminate early.
     */
    virtual void Stop() {
    }
    
    /**
     * Exits the runnable object
     * 
     * Called in the context of the aggregating thread to perform any cleanup.
     */
    virtual void Exit() {
    }
    
    /** Virtual destructor */
    virtual ~FRunnable() = default;
};

} // namespace MonsterEngine
