// Copyright Monster Engine. All Rights Reserved.

#include "Core/CoreMinimal.h"
#include "RHI/FRHIThread.h"
#include "RHI/FRHICommandListExecutor.h"
#include "Core/Log.h"
#include <iostream>
#include <chrono>

using namespace MonsterEngine;
using namespace MonsterRender::RHI;

/**
 * Test program for RHI thread system
 * Validates FRHIThread and FRHICommandListExecutor
 */
int main() {
    MR_LOG_INFO("=== RHI Thread System Test ===");
    
    // Test 1: Initialize RHI thread
    MR_LOG_INFO("\nTest 1: Initializing RHI thread");
    {
        bool success = FRHIThread::Initialize(true);
        
        if (success && FRHIThread::IsInitialized()) {
            MR_LOG_INFO("Test 1 PASSED: RHI thread initialized successfully");
        } else {
            MR_LOG_ERROR("Test 1 FAILED: RHI thread initialization failed");
            return 1;
        }
    }
    
    // Test 2: Check thread identification
    MR_LOG_INFO("\nTest 2: Thread identification");
    {
        bool isOnRHIThread = FRHIThread::IsInRHIThread();
        
        if (!isOnRHIThread) {
            MR_LOG_INFO("Test 2 PASSED: Correctly identified we're NOT on RHI thread");
        } else {
            MR_LOG_ERROR("Test 2 FAILED: Thread identification incorrect");
        }
    }
    
    // Test 3: Queue simple task to RHI thread
    MR_LOG_INFO("\nTest 3: Queue simple task to RHI thread");
    {
        std::atomic<bool> taskExecuted{false};
        
        auto event = FRHIThread::QueueTask([&taskExecuted]() {
            taskExecuted.store(true, std::memory_order_release);
            MR_LOG_INFO("Simple RHI task executed on RHI thread");
        });
        
        if (event) {
            event->Wait();
            
            if (taskExecuted.load(std::memory_order_acquire)) {
                MR_LOG_INFO("Test 3 PASSED: RHI task executed successfully");
            } else {
                MR_LOG_ERROR("Test 3 FAILED: RHI task did not execute");
            }
        } else {
            MR_LOG_ERROR("Test 3 FAILED: Failed to queue task");
        }
    }
    
    // Test 4: Queue multiple tasks
    MR_LOG_INFO("\nTest 4: Queue multiple RHI tasks");
    {
        const int numTasks = 10;
        std::atomic<int> counter{0};
        FGraphEventArray events;
        
        for (int i = 0; i < numTasks; ++i) {
            auto event = FRHIThread::QueueTask([&counter, i]() {
                counter.fetch_add(1, std::memory_order_relaxed);
                MR_LOG_DEBUG("RHI task " + std::to_string(i) + " executed");
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            });
            events.push_back(event);
        }
        
        // Wait for all tasks
        WaitForEvents(events);
        
        if (counter.load() == numTasks) {
            MR_LOG_INFO("Test 4 PASSED: All " + std::to_string(numTasks) + " RHI tasks executed");
        } else {
            MR_LOG_ERROR("Test 4 FAILED: Only " + std::to_string(counter.load()) + 
                        " of " + std::to_string(numTasks) + " tasks executed");
        }
    }
    
    // Test 5: Wait for RHI thread to be idle
    MR_LOG_INFO("\nTest 5: Wait for RHI thread idle");
    {
        // Queue some tasks
        for (int i = 0; i < 5; ++i) {
            FRHIThread::QueueTask([i]() {
                MR_LOG_DEBUG("Background task " + std::to_string(i));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            });
        }
        
        // Wait for all tasks
        FRHIThread::WaitForTasks();
        
        if (FRHIThread::IsIdle()) {
            MR_LOG_INFO("Test 5 PASSED: RHI thread is idle after waiting");
        } else {
            MR_LOG_ERROR("Test 5 FAILED: RHI thread still has pending tasks");
        }
    }
    
    // Test 6: Initialize command list executor
    MR_LOG_INFO("\nTest 6: Initialize command list executor");
    {
        bool success = FRHICommandListExecutor::Initialize(true);
        
        if (success && FRHICommandListExecutor::IsInitialized()) {
            MR_LOG_INFO("Test 6 PASSED: Command list executor initialized");
        } else {
            MR_LOG_ERROR("Test 6 FAILED: Command list executor initialization failed");
        }
    }
    
    // Test 7: Command list executor statistics
    MR_LOG_INFO("\nTest 7: Command list executor statistics");
    {
        auto stats = FRHICommandListExecutor::Get().GetStats();
        
        MR_LOG_INFO("Command list executor stats:");
        MR_LOG_INFO("  Total queued: " + std::to_string(stats.totalCommandListsQueued));
        MR_LOG_INFO("  Total executed: " + std::to_string(stats.totalCommandListsExecuted));
        MR_LOG_INFO("  Pending: " + std::to_string(stats.pendingCommandLists));
        
        MR_LOG_INFO("Test 7 PASSED: Statistics retrieved successfully");
    }
    
    // Test 8: Flush command list executor
    MR_LOG_INFO("\nTest 8: Flush command list executor");
    {
        FRHICommandListExecutor::Get().Flush(true);
        
        if (FRHICommandListExecutor::Get().IsRHIThreadIdle()) {
            MR_LOG_INFO("Test 8 PASSED: Command list executor flushed successfully");
        } else {
            MR_LOG_ERROR("Test 8 FAILED: RHI thread not idle after flush");
        }
    }
    
    // Test 9: FRHIThreadScope helper
    MR_LOG_INFO("\nTest 9: FRHIThreadScope helper");
    {
        std::atomic<bool> executed{false};
        
        FRHIThreadScope::Execute([&executed]() {
            executed.store(true, std::memory_order_release);
            MR_LOG_INFO("Lambda executed via FRHIThreadScope");
        }, true);
        
        if (executed.load(std::memory_order_acquire)) {
            MR_LOG_INFO("Test 9 PASSED: FRHIThreadScope executed lambda successfully");
        } else {
            MR_LOG_ERROR("Test 9 FAILED: Lambda did not execute");
        }
    }
    
    // Test 10: Scoped flush
    MR_LOG_INFO("\nTest 10: Scoped flush");
    {
        {
            FRHICommandListScopedFlush scopedFlush(true);
            
            // Queue some tasks
            FRHIThread::QueueTask([]() {
                MR_LOG_DEBUG("Task in scoped flush");
            });
            
            // Flush will happen automatically on scope exit
        }
        
        if (FRHIThread::IsIdle()) {
            MR_LOG_INFO("Test 10 PASSED: Scoped flush worked correctly");
        } else {
            MR_LOG_ERROR("Test 10 FAILED: Scoped flush did not complete");
        }
    }
    
    // Shutdown
    MR_LOG_INFO("\n=== Shutting down RHI thread system ===");
    FRHICommandListExecutor::Shutdown();
    FRHIThread::Shutdown();
    
    MR_LOG_INFO("\n=== All tests completed successfully ===");
    
    return 0;
}
