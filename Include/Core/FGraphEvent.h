// Copyright Monster Engine. All Rights Reserved.

#pragma once

#include "Core/CoreMinimal.h"
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace MonsterEngine {

/**
 * Graph event for task synchronization
 * 
 * A graph event is used to track the completion of asynchronous tasks.
 * Tasks can wait on events, and events can have subsequent tasks that
 * are triggered when the event completes.
 * 
 * Based on UE5's FGraphEvent
 * Reference: Engine/Source/Runtime/Core/Public/Async/TaskGraphInterfaces.h
 */
class FGraphEvent {
public:
    /**
     * Constructor
     */
    FGraphEvent();
    
    /**
     * Destructor
     */
    ~FGraphEvent();
    
    /**
     * Wait for event to complete
     * Blocks the calling thread until this event is completed
     */
    void Wait();
    
    /**
     * Wait for event to complete with timeout
     * 
     * @param TimeoutMs - Timeout in milliseconds
     * @return True if event completed, false if timeout occurred
     */
    bool WaitFor(uint32 TimeoutMs);
    
    /**
     * Check if event is complete
     * Non-blocking check
     * 
     * @return True if the event has completed
     */
    bool IsComplete() const;
    
    /**
     * Dispatch subsequent events
     * Called when this event completes to trigger all dependent events
     */
    void DispatchSubsequents();
    
    /**
     * Add subsequent event
     * The subsequent event will be triggered when this event completes
     * 
     * @param Subsequent - Event to trigger after this event completes
     */
    void AddSubsequent(TSharedPtr<FGraphEvent> Subsequent);
    
    /**
     * Mark event as complete
     * This will dispatch all subsequent events
     */
    void Complete();
    
    /**
     * Get number of subsequent events
     */
    uint32 GetNumSubsequents() const;
    
private:
    /** Whether the event has completed */
    std::atomic<bool> m_isComplete{false};
    
    /** List of subsequent events to trigger when this completes */
    TArray<TSharedPtr<FGraphEvent>> m_subsequents;
    
    /** Mutex for protecting subsequent list */
    mutable std::mutex m_mutex;
    
    /** Condition variable for waiting */
    std::condition_variable m_cv;
};

/**
 * Convenience typedef for reference counted pointer to a graph event
 * Matches UE5's FGraphEventRef pattern
 */
using FGraphEventRef = TSharedPtr<FGraphEvent>;

/**
 * Array of graph event references
 */
using FGraphEventArray = TArray<FGraphEventRef>;

/**
 * Helper function to create a graph event
 * 
 * @return New graph event reference
 */
inline FGraphEventRef MakeGraphEvent() {
    return MakeShared<FGraphEvent>();
}

/**
 * Helper function to wait for multiple events
 * 
 * @param Events - Array of events to wait for
 */
inline void WaitForEvents(const FGraphEventArray& Events) {
    for (const auto& Event : Events) {
        if (Event) {
            Event->Wait();
        }
    }
}

/**
 * Helper function to check if all events are complete
 * 
 * @param Events - Array of events to check
 * @return True if all events are complete
 */
inline bool AreEventsComplete(const FGraphEventArray& Events) {
    for (const auto& Event : Events) {
        if (Event && !Event->IsComplete()) {
            return false;
        }
    }
    return true;
}

} // namespace MonsterEngine
