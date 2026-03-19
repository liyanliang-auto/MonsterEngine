// Copyright Monster Engine. All Rights Reserved.

#include "Core/CoreMinimal.h"
#include "Core/Log.h"
#include "Core/FTaskGraph.h"
#include "RHI/FRHICommandListPool.h"
#include "RHI/FRHICommandListParallelTranslator.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace MonsterEngine;
using namespace MonsterEngine::RHI;

/**
 * Test basic parallel translation functionality
 */
void TestBasicParallelTranslation() {
    MR_LOG_INFO("=== Test 1: Basic Parallel Translation ===");
    
    // Initialize systems
    FTaskGraph::Initialize(4);
    FRHICommandListPool::Initialize(8);
    FRHICommandListParallelTranslator::Initialize(true, 50);
    
    // Allocate command lists
    auto* cmdList1 = FRHICommandListPool::AllocateCommandList();
    auto* cmdList2 = FRHICommandListPool::AllocateCommandList();
    auto* cmdList3 = FRHICommandListPool::AllocateCommandList();
    
    if (cmdList1 && cmdList2 && cmdList3) {
        // Simulate recording commands
        cmdList1->begin();
        cmdList1->end();
        
        cmdList2->begin();
        cmdList2->end();
        
        cmdList3->begin();
        cmdList3->end();
        
        // Create queued command lists
        TArray<FQueuedCommandList> queuedLists;
        queuedLists.emplace_back(cmdList1, 100);  // 100 draws
        queuedLists.emplace_back(cmdList2, 150);  // 150 draws
        queuedLists.emplace_back(cmdList3, 200);  // 200 draws
        
        // Submit for parallel translation
        MR_LOG_INFO("Submitting 3 command lists for parallel translation");
        auto completionEvent = FRHICommandListParallelTranslator::QueueParallelTranslate(
            TSpan<FQueuedCommandList>(queuedLists),
            ETranslatePriority::Normal,
            50
        );
        
        // Wait for completion
        if (completionEvent) {
            MR_LOG_INFO("Waiting for parallel translation to complete...");
            completionEvent->Wait();
            MR_LOG_INFO("Parallel translation completed");
        }
        
        // Recycle command lists
        FRHICommandListPool::RecycleCommandList(cmdList1);
        FRHICommandListPool::RecycleCommandList(cmdList2);
        FRHICommandListPool::RecycleCommandList(cmdList3);
    }
    
    // Get statistics
    auto stats = FRHICommandListParallelTranslator::GetStats();
    MR_LOG_INFO("Translation stats - Total: " + std::to_string(stats.totalTranslations) +
               ", Parallel: " + std::to_string(stats.parallelTranslations) +
               ", Serial: " + std::to_string(stats.serialTranslations));
    
    // Shutdown
    FRHICommandListParallelTranslator::Shutdown();
    FRHICommandListPool::Shutdown();
    FTaskGraph::Shutdown();
    
    MR_LOG_INFO("=== Test 1 Complete ===\n");
}

/**
 * Test serial translation (disabled parallel)
 */
void TestSerialTranslation() {
    MR_LOG_INFO("=== Test 2: Serial Translation ===");
    
    // Initialize systems
    FTaskGraph::Initialize(4);
    FRHICommandListPool::Initialize(4);
    FRHICommandListParallelTranslator::Initialize(true, 50);
    
    // Allocate command lists
    auto* cmdList1 = FRHICommandListPool::AllocateCommandList();
    auto* cmdList2 = FRHICommandListPool::AllocateCommandList();
    
    if (cmdList1 && cmdList2) {
        cmdList1->begin();
        cmdList1->end();
        
        cmdList2->begin();
        cmdList2->end();
        
        // Create queued command lists with low draw count
        TArray<FQueuedCommandList> queuedLists;
        queuedLists.emplace_back(cmdList1, 10);  // Only 10 draws
        queuedLists.emplace_back(cmdList2, 20);  // Only 20 draws
        
        // Submit - should use serial translation due to low draw count
        MR_LOG_INFO("Submitting 2 command lists with low draw count (should use serial)");
        auto completionEvent = FRHICommandListParallelTranslator::QueueParallelTranslate(
            TSpan<FQueuedCommandList>(queuedLists),
            ETranslatePriority::Normal,
            50  // Min draws threshold
        );
        
        if (completionEvent) {
            completionEvent->Wait();
            MR_LOG_INFO("Serial translation completed");
        }
        
        FRHICommandListPool::RecycleCommandList(cmdList1);
        FRHICommandListPool::RecycleCommandList(cmdList2);
    }
    
    auto stats = FRHICommandListParallelTranslator::GetStats();
    MR_LOG_INFO("Translation stats - Serial: " + std::to_string(stats.serialTranslations));
    
    FRHICommandListParallelTranslator::Shutdown();
    FRHICommandListPool::Shutdown();
    FTaskGraph::Shutdown();
    
    MR_LOG_INFO("=== Test 2 Complete ===\n");
}

/**
 * Test FParallelCommandListSet helper
 */
void TestParallelCommandListSet() {
    MR_LOG_INFO("=== Test 3: Parallel Command List Set ===");
    
    // Initialize systems
    FTaskGraph::Initialize(4);
    FRHICommandListPool::Initialize(8);
    FRHICommandListParallelTranslator::Initialize(true, 50);
    
    {
        // Create parallel command list set
        FParallelCommandListSet parallelSet;
        
        // Allocate command lists
        auto* cmdList1 = parallelSet.AllocateCommandList();
        auto* cmdList2 = parallelSet.AllocateCommandList();
        auto* cmdList3 = parallelSet.AllocateCommandList();
        
        MR_LOG_INFO("Allocated " + std::to_string(parallelSet.GetNumCommandLists()) + " command lists");
        
        // Simulate parallel recording on different threads
        if (cmdList1 && cmdList2 && cmdList3) {
            // Record commands
            cmdList1->begin();
            cmdList1->end();
            
            cmdList2->begin();
            cmdList2->end();
            
            cmdList3->begin();
            cmdList3->end();
            
            // Submit all for parallel translation
            MR_LOG_INFO("Submitting parallel command list set");
            auto completionEvent = parallelSet.Submit(ETranslatePriority::High);
            
            // Wait for completion
            parallelSet.Wait();
            MR_LOG_INFO("Parallel command list set completed");
        }
        
        // Command lists will be automatically recycled when parallelSet goes out of scope
    }
    
    auto stats = FRHICommandListParallelTranslator::GetStats();
    MR_LOG_INFO("Translation stats - Total: " + std::to_string(stats.totalTranslations));
    
    FRHICommandListParallelTranslator::Shutdown();
    FRHICommandListPool::Shutdown();
    FTaskGraph::Shutdown();
    
    MR_LOG_INFO("=== Test 3 Complete ===\n");
}

/**
 * Test high priority translation
 */
void TestHighPriorityTranslation() {
    MR_LOG_INFO("=== Test 4: High Priority Translation ===");
    
    FTaskGraph::Initialize(4);
    FRHICommandListPool::Initialize(8);
    FRHICommandListParallelTranslator::Initialize(true, 50);
    
    auto* cmdList1 = FRHICommandListPool::AllocateCommandList();
    auto* cmdList2 = FRHICommandListPool::AllocateCommandList();
    
    if (cmdList1 && cmdList2) {
        cmdList1->begin();
        cmdList1->end();
        
        cmdList2->begin();
        cmdList2->end();
        
        TArray<FQueuedCommandList> queuedLists;
        queuedLists.emplace_back(cmdList1, 200);
        queuedLists.emplace_back(cmdList2, 300);
        
        // Submit with high priority (e.g., for prepass)
        MR_LOG_INFO("Submitting with HIGH priority");
        auto completionEvent = FRHICommandListParallelTranslator::QueueParallelTranslate(
            TSpan<FQueuedCommandList>(queuedLists),
            ETranslatePriority::High,
            50
        );
        
        if (completionEvent) {
            completionEvent->Wait();
            MR_LOG_INFO("High priority translation completed");
        }
        
        FRHICommandListPool::RecycleCommandList(cmdList1);
        FRHICommandListPool::RecycleCommandList(cmdList2);
    }
    
    FRHICommandListParallelTranslator::Shutdown();
    FRHICommandListPool::Shutdown();
    FTaskGraph::Shutdown();
    
    MR_LOG_INFO("=== Test 4 Complete ===\n");
}

/**
 * Test multiple parallel translation batches
 */
void TestMultipleBatches() {
    MR_LOG_INFO("=== Test 5: Multiple Parallel Batches ===");
    
    FTaskGraph::Initialize(4);
    FRHICommandListPool::Initialize(16);
    FRHICommandListParallelTranslator::Initialize(true, 50);
    
    TArray<FGraphEventRef> batchEvents;
    
    // Submit multiple batches
    for (int batch = 0; batch < 3; ++batch) {
        MR_LOG_INFO("Submitting batch " + std::to_string(batch + 1));
        
        auto* cmdList1 = FRHICommandListPool::AllocateCommandList();
        auto* cmdList2 = FRHICommandListPool::AllocateCommandList();
        
        if (cmdList1 && cmdList2) {
            cmdList1->begin();
            cmdList1->end();
            
            cmdList2->begin();
            cmdList2->end();
            
            TArray<FQueuedCommandList> queuedLists;
            queuedLists.emplace_back(cmdList1, 100 + batch * 50);
            queuedLists.emplace_back(cmdList2, 150 + batch * 50);
            
            auto completionEvent = FRHICommandListParallelTranslator::QueueParallelTranslate(
                TSpan<FQueuedCommandList>(queuedLists),
                ETranslatePriority::Normal,
                50
            );
            
            if (completionEvent) {
                batchEvents.push_back(completionEvent);
            }
            
            // Note: Command lists will be recycled after translation
            // In production, the translator would handle this
        }
    }
    
    // Wait for all batches
    MR_LOG_INFO("Waiting for all batches to complete...");
    for (auto& event : batchEvents) {
        if (event) {
            event->Wait();
        }
    }
    MR_LOG_INFO("All batches completed");
    
    auto stats = FRHICommandListParallelTranslator::GetStats();
    MR_LOG_INFO("Final stats - Total: " + std::to_string(stats.totalTranslations) +
               ", Parallel: " + std::to_string(stats.parallelTranslations) +
               ", Peak active tasks: " + std::to_string(stats.peakActiveTasks));
    
    FRHICommandListParallelTranslator::Shutdown();
    FRHICommandListPool::Shutdown();
    FTaskGraph::Shutdown();
    
    MR_LOG_INFO("=== Test 5 Complete ===\n");
}

/**
 * Main test entry point
 */
int RunParallelTranslatorTests() {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Parallel Translator Test Suite");
    MR_LOG_INFO("========================================\n");
    
    try {
        // Run all tests
        TestBasicParallelTranslation();
        TestSerialTranslation();
        TestParallelCommandListSet();
        TestHighPriorityTranslation();
        TestMultipleBatches();
        
        MR_LOG_INFO("========================================");
        MR_LOG_INFO("All tests completed successfully!");
        MR_LOG_INFO("========================================");
        
        return 0;
    }
    catch (const std::exception& e) {
        MR_LOG_ERROR("Test failed with exception: " + String(e.what()));
        return 1;
    }
}
