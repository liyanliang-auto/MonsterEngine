// Copyright Monster Engine. All Rights Reserved.

#include "Core/CoreMinimal.h"
#include "Core/FTaskGraph.h"
#include "Core/FGraphEvent.h"
#include "Core/Log.h"
#include <iostream>

using namespace MonsterEngine;

/**
 * Test program for task graph system
 * Validates FTaskGraph, FGraphEvent, FRunnable, and FRunnableThread
 */
int main() {
    MR_LOG_INFO("=== Task Graph System Test ===");
    
    // Test 1: Initialize task graph
    MR_LOG_INFO("Test 1: Initializing task graph with 4 worker threads");
    FTaskGraph::Initialize(4);
    
    if (!FTaskGraph::IsInitialized()) {
        MR_LOG_ERROR("Failed to initialize task graph");
        return 1;
    }
    
    MR_LOG_INFO("Task graph initialized successfully with " + 
               std::to_string(FTaskGraph::GetNumWorkerThreads()) + " worker threads");
    
    // Test 2: Simple task execution
    MR_LOG_INFO("\nTest 2: Simple task execution");
    {
        std::atomic<int> counter{0};
        auto event = FTaskGraph::QueueNamedTask("SimpleTask", [&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
            MR_LOG_INFO("SimpleTask executed");
        });
        
        event->Wait();
        
        if (counter.load() == 1) {
            MR_LOG_INFO("Test 2 PASSED: Task executed successfully");
        } else {
            MR_LOG_ERROR("Test 2 FAILED: Task did not execute");
        }
    }
    
    // Test 3: Multiple parallel tasks
    MR_LOG_INFO("\nTest 3: Multiple parallel tasks");
    {
        const int numTasks = 10;
        std::atomic<int> counter{0};
        FGraphEventArray events;
        
        for (int i = 0; i < numTasks; ++i) {
            auto event = FTaskGraph::QueueNamedTask(
                "ParallelTask_" + std::to_string(i),
                [&counter, i]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                    MR_LOG_DEBUG("ParallelTask_" + std::to_string(i) + " executed");
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            );
            events.push_back(event);
        }
        
        // Wait for all tasks
        WaitForEvents(events);
        
        if (counter.load() == numTasks) {
            MR_LOG_INFO("Test 3 PASSED: All " + std::to_string(numTasks) + " tasks executed");
        } else {
            MR_LOG_ERROR("Test 3 FAILED: Only " + std::to_string(counter.load()) + 
                        " of " + std::to_string(numTasks) + " tasks executed");
        }
    }
    
    // Test 4: Task with prerequisites
    MR_LOG_INFO("\nTest 4: Task with prerequisites");
    {
        std::atomic<int> executionOrder{0};
        int firstTaskOrder = 0;
        int secondTaskOrder = 0;
        
        // First task
        auto firstEvent = FTaskGraph::QueueNamedTask("FirstTask", [&executionOrder, &firstTaskOrder]() {
            firstTaskOrder = executionOrder.fetch_add(1, std::memory_order_relaxed);
            MR_LOG_INFO("FirstTask executed (order: " + std::to_string(firstTaskOrder) + ")");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });
        
        // Second task depends on first
        FGraphEventArray prerequisites = {firstEvent};
        auto secondEvent = FTaskGraph::QueueNamedTask("SecondTask", [&executionOrder, &secondTaskOrder]() {
            secondTaskOrder = executionOrder.fetch_add(1, std::memory_order_relaxed);
            MR_LOG_INFO("SecondTask executed (order: " + std::to_string(secondTaskOrder) + ")");
        }, prerequisites);
        
        secondEvent->Wait();
        
        if (firstTaskOrder < secondTaskOrder) {
            MR_LOG_INFO("Test 4 PASSED: Tasks executed in correct order");
        } else {
            MR_LOG_ERROR("Test 4 FAILED: Tasks executed in wrong order");
        }
    }
    
    // Test 5: Complex dependency graph
    MR_LOG_INFO("\nTest 5: Complex dependency graph");
    {
        std::atomic<int> counter{0};
        
        // Create root task
        auto rootEvent = FTaskGraph::QueueNamedTask("RootTask", [&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
            MR_LOG_INFO("RootTask executed");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        });
        
        // Create child tasks that depend on root
        FGraphEventArray childEvents;
        for (int i = 0; i < 3; ++i) {
            auto childEvent = FTaskGraph::QueueNamedTask(
                "ChildTask_" + std::to_string(i),
                [&counter, i]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                    MR_LOG_INFO("ChildTask_" + std::to_string(i) + " executed");
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                },
                {rootEvent}
            );
            childEvents.push_back(childEvent);
        }
        
        // Create final task that depends on all children
        auto finalEvent = FTaskGraph::QueueNamedTask("FinalTask", [&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
            MR_LOG_INFO("FinalTask executed");
        }, childEvents);
        
        finalEvent->Wait();
        
        if (counter.load() == 5) { // 1 root + 3 children + 1 final
            MR_LOG_INFO("Test 5 PASSED: Complex dependency graph executed correctly");
        } else {
            MR_LOG_ERROR("Test 5 FAILED: Expected 5 tasks, got " + std::to_string(counter.load()));
        }
    }
    
    // Test 6: Graph event timeout
    MR_LOG_INFO("\nTest 6: Graph event timeout");
    {
        auto event = FTaskGraph::QueueNamedTask("SlowTask", []() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            MR_LOG_INFO("SlowTask executed");
        });
        
        // Try to wait with short timeout
        bool timedOut = !event->WaitFor(10); // 10ms timeout
        
        if (timedOut) {
            MR_LOG_INFO("Test 6 PASSED: Timeout worked correctly");
        } else {
            MR_LOG_ERROR("Test 6 FAILED: Timeout did not work");
        }
        
        // Wait for actual completion
        event->Wait();
    }
    
    // Test 7: Wait for all tasks
    MR_LOG_INFO("\nTest 7: Wait for all tasks");
    {
        const int numTasks = 20;
        for (int i = 0; i < numTasks; ++i) {
            FTaskGraph::QueueNamedTask(
                "BulkTask_" + std::to_string(i),
                [i]() {
                    MR_LOG_DEBUG("BulkTask_" + std::to_string(i) + " executed");
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            );
        }
        
        MR_LOG_INFO("Queued " + std::to_string(numTasks) + " tasks, waiting for completion...");
        FTaskGraph::WaitForAllTasks();
        
        if (FTaskGraph::GetNumPendingTasks() == 0) {
            MR_LOG_INFO("Test 7 PASSED: All tasks completed");
        } else {
            MR_LOG_ERROR("Test 7 FAILED: " + std::to_string(FTaskGraph::GetNumPendingTasks()) + 
                        " tasks still pending");
        }
    }
    
    // Shutdown task graph
    MR_LOG_INFO("\n=== Shutting down task graph ===");
    FTaskGraph::Shutdown();
    
    MR_LOG_INFO("\n=== All tests completed successfully ===");
    
    return 0;
}
