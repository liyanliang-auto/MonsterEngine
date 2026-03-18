// Copyright Monster Engine. All Rights Reserved.

#include "Core/FGraphEvent.h"
#include "Core/Log.h"
#include <chrono>

namespace MonsterEngine {

FGraphEvent::FGraphEvent() {
}

FGraphEvent::~FGraphEvent() {
    // Ensure event is complete before destruction
    if (!m_isComplete.load(std::memory_order_acquire)) {
        MR_LOG_WARNING("FGraphEvent::~FGraphEvent - Event destroyed before completion");
    }
}

void FGraphEvent::Wait() {
    // Fast path: check if already complete
    if (m_isComplete.load(std::memory_order_acquire)) {
        return;
    }
    
    // Slow path: wait for completion
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() {
        return m_isComplete.load(std::memory_order_acquire);
    });
}

bool FGraphEvent::WaitFor(uint32 TimeoutMs) {
    // Fast path: check if already complete
    if (m_isComplete.load(std::memory_order_acquire)) {
        return true;
    }
    
    // Slow path: wait with timeout
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_cv.wait_for(lock, std::chrono::milliseconds(TimeoutMs), [this]() {
        return m_isComplete.load(std::memory_order_acquire);
    });
}

bool FGraphEvent::IsComplete() const {
    return m_isComplete.load(std::memory_order_acquire);
}

void FGraphEvent::DispatchSubsequents() {
    TArray<TSharedPtr<FGraphEvent>> subsequentsToDispatch;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        subsequentsToDispatch = m_subsequents;
        m_subsequents.clear();
    }
    
    // Dispatch all subsequent events
    for (auto& Subsequent : subsequentsToDispatch) {
        if (Subsequent) {
            Subsequent->Complete();
        }
    }
}

void FGraphEvent::AddSubsequent(TSharedPtr<FGraphEvent> Subsequent) {
    if (!Subsequent) {
        return;
    }
    
    bool shouldDispatchImmediately = false;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // If already complete, dispatch immediately
        if (m_isComplete.load(std::memory_order_acquire)) {
            shouldDispatchImmediately = true;
        } else {
            m_subsequents.push_back(Subsequent);
        }
    }
    
    if (shouldDispatchImmediately) {
        Subsequent->Complete();
    }
}

void FGraphEvent::Complete() {
    // Mark as complete
    bool wasAlreadyComplete = m_isComplete.exchange(true, std::memory_order_acq_rel);
    
    if (wasAlreadyComplete) {
        // Already completed, nothing to do
        return;
    }
    
    // Notify all waiting threads
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cv.notify_all();
    }
    
    // Dispatch subsequent events
    DispatchSubsequents();
}

uint32 FGraphEvent::GetNumSubsequents() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<uint32>(m_subsequents.size());
}

} // namespace MonsterEngine
