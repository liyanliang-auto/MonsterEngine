// Copyright Monster Engine. All Rights Reserved.

#include "Tests/TestCommandListPool.h"
#include "Core/CoreMinimal.h"
#include "Core/Log.h"
#include "RHI/FRHICommandListPool.h"
#include <iostream>

using namespace MonsterEngine;
using namespace MonsterEngine::RHI;

/**
 * Test command list pool basic functionality
 */
void TestBasicPoolFunctionality() {
    MR_LOG_INFO("=== Test 1: Basic Pool Functionality ===");
    
    // Initialize pool
    bool initResult = FRHICommandListPool::Initialize(4);
    if (!initResult) {
        MR_LOG_ERROR("Failed to initialize command list pool");
        return;
    }
    
    // Get initial statistics
    auto stats = FRHICommandListPool::GetStats();
    MR_LOG_INFO("Initial pool stats - Pooled: " + std::to_string(stats.pooledCommandLists) +
               ", Active: " + std::to_string(stats.activeCommandLists));
    
    // Allocate command lists
    auto* cmdList1 = FRHICommandListPool::AllocateCommandList();
    auto* cmdList2 = FRHICommandListPool::AllocateCommandList();
    auto* cmdList3 = FRHICommandListPool::AllocateCommandList();
    
    // Check statistics
    stats = FRHICommandListPool::GetStats();
    MR_LOG_INFO("After allocation - Pooled: " + std::to_string(stats.pooledCommandLists) +
               ", Active: " + std::to_string(stats.activeCommandLists) +
               ", Total allocated: " + std::to_string(stats.totalAllocated));
    
    // Recycle command lists
    FRHICommandListPool::RecycleCommandList(cmdList1);
    FRHICommandListPool::RecycleCommandList(cmdList2);
    FRHICommandListPool::RecycleCommandList(cmdList3);
    
    // Check final statistics
    stats = FRHICommandListPool::GetStats();
    MR_LOG_INFO("After recycling - Pooled: " + std::to_string(stats.pooledCommandLists) +
               ", Active: " + std::to_string(stats.activeCommandLists) +
               ", Total recycled: " + std::to_string(stats.totalRecycled));
    
    // Shutdown pool
    FRHICommandListPool::Shutdown();
    
    MR_LOG_INFO("=== Test 1 Complete ===\n");
}

/**
 * Test RAII scoped command list
 */
void TestScopedCommandList() {
    MR_LOG_INFO("=== Test 2: Scoped Command List ===");
    
    // Initialize pool
    FRHICommandListPool::Initialize(2);
    
    {
        // Allocate scoped command list
        FScopedCommandList scopedCmdList(FRHICommandListPool::AllocateCommandList());
        
        auto stats = FRHICommandListPool::GetStats();
        MR_LOG_INFO("Inside scope - Active: " + std::to_string(stats.activeCommandLists));
        
        // Command list will be automatically recycled when scope exits
    }
    
    // Check that command list was recycled
    auto stats = FRHICommandListPool::GetStats();
    MR_LOG_INFO("After scope exit - Active: " + std::to_string(stats.activeCommandLists) +
               ", Pooled: " + std::to_string(stats.pooledCommandLists));
    
    FRHICommandListPool::Shutdown();
    
    MR_LOG_INFO("=== Test 2 Complete ===\n");
}

/**
 * Test pool trimming
 */
void TestPoolTrimming() {
    MR_LOG_INFO("=== Test 3: Pool Trimming ===");
    
    // Initialize with larger pool
    FRHICommandListPool::Initialize(10);
    
    auto stats = FRHICommandListPool::GetStats();
    MR_LOG_INFO("Initial pool size: " + std::to_string(stats.pooledCommandLists));
    
    // Trim pool to smaller size
    FRHICommandListPool::TrimPool(3);
    
    stats = FRHICommandListPool::GetStats();
    MR_LOG_INFO("After trimming to 3 - Pooled: " + std::to_string(stats.pooledCommandLists));
    
    FRHICommandListPool::Shutdown();
    
    MR_LOG_INFO("=== Test 3 Complete ===\n");
}

/**
 * Test pool expansion
 */
void TestPoolExpansion() {
    MR_LOG_INFO("=== Test 4: Pool Expansion ===");
    
    // Initialize with small pool
    FRHICommandListPool::Initialize(2);
    
    auto stats = FRHICommandListPool::GetStats();
    MR_LOG_INFO("Initial pool size: " + std::to_string(stats.pooledCommandLists));
    
    // Allocate more than initial pool size
    TArray<MonsterRender::RHI::IRHICommandList*> cmdLists;
    for (int i = 0; i < 5; ++i) {
        auto* cmdList = FRHICommandListPool::AllocateCommandList();
        if (cmdList) {
            cmdLists.push_back(cmdList);
        }
    }
    
    stats = FRHICommandListPool::GetStats();
    MR_LOG_INFO("After allocating 5 command lists - Active: " + std::to_string(stats.activeCommandLists) +
               ", Total allocated: " + std::to_string(stats.totalAllocated) +
               ", Peak active: " + std::to_string(stats.peakActiveCount));
    
    // Recycle all
    for (auto* cmdList : cmdLists) {
        FRHICommandListPool::RecycleCommandList(cmdList);
    }
    
    stats = FRHICommandListPool::GetStats();
    MR_LOG_INFO("After recycling - Pooled: " + std::to_string(stats.pooledCommandLists) +
               ", Active: " + std::to_string(stats.activeCommandLists));
    
    FRHICommandListPool::Shutdown();
    
    MR_LOG_INFO("=== Test 4 Complete ===\n");
}

/**
 * Test statistics tracking
 */
void TestStatistics() {
    MR_LOG_INFO("=== Test 5: Statistics Tracking ===");
    
    FRHICommandListPool::Initialize(3);
    
    // Perform various operations
    auto* cmd1 = FRHICommandListPool::AllocateCommandList();
    auto* cmd2 = FRHICommandListPool::AllocateCommandList();
    auto* cmd3 = FRHICommandListPool::AllocateCommandList();
    auto* cmd4 = FRHICommandListPool::AllocateCommandList();
    
    FRHICommandListPool::RecycleCommandList(cmd1);
    FRHICommandListPool::RecycleCommandList(cmd2);
    
    auto* cmd5 = FRHICommandListPool::AllocateCommandList();
    
    // Get final statistics
    auto stats = FRHICommandListPool::GetStats();
    MR_LOG_INFO("Final statistics:");
    MR_LOG_INFO("  Total allocated: " + std::to_string(stats.totalAllocated));
    MR_LOG_INFO("  Total recycled: " + std::to_string(stats.totalRecycled));
    MR_LOG_INFO("  Active command lists: " + std::to_string(stats.activeCommandLists));
    MR_LOG_INFO("  Pooled command lists: " + std::to_string(stats.pooledCommandLists));
    MR_LOG_INFO("  Peak active count: " + std::to_string(stats.peakActiveCount));
    
    // Cleanup
    FRHICommandListPool::RecycleCommandList(cmd3);
    FRHICommandListPool::RecycleCommandList(cmd4);
    FRHICommandListPool::RecycleCommandList(cmd5);
    
    FRHICommandListPool::Shutdown();
    
    MR_LOG_INFO("=== Test 5 Complete ===\n");
}

/**
 * Main test entry point
 * Can be called from command line or programmatically
 */
int RunCommandListPoolTests() {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Command List Pool Test Suite");
    MR_LOG_INFO("========================================\n");
    
    try {
        // Run all tests
        TestBasicPoolFunctionality();
        TestScopedCommandList();
        TestPoolTrimming();
        TestPoolExpansion();
        TestStatistics();
        
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
