// Copyright Monster Engine. All Rights Reserved.

#include "RHI/FRHICommandListPool.h"
#include "Core/Log.h"

// Define USE_MOCK_COMMAND_LIST for testing
#define USE_MOCK_COMMAND_LIST

#ifdef USE_MOCK_COMMAND_LIST
#include "RHI/MockCommandList.h"
#else
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/OpenGL/OpenGLCommandList.h"
#endif

namespace MonsterEngine {
namespace RHI {

// Initialize static members
TUniquePtr<FRHICommandListPool> FRHICommandListPool::s_instance;

FRHICommandListPool::FRHICommandListPool() {
    MR_LOG_INFO("FRHICommandListPool::FRHICommandListPool - Command list pool created");
}

FRHICommandListPool::~FRHICommandListPool() {
    MR_LOG_INFO("FRHICommandListPool::~FRHICommandListPool - Destroying command list pool");
    
    // Clean up all pooled command lists
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    while (!m_commandListPool.empty()) {
        FPoolEntry entry = m_commandListPool.front();
        m_commandListPool.pop();
        
        if (entry.commandList) {
            delete entry.commandList;
        }
    }
    
    MR_LOG_INFO("FRHICommandListPool::~FRHICommandListPool - Command list pool destroyed. " +
               std::to_string(m_totalAllocated.load()) + " total allocations, " +
               std::to_string(m_totalRecycled.load()) + " total recycled");
}

bool FRHICommandListPool::Initialize(uint32 InitialPoolSize) {
    if (s_instance) {
        MR_LOG_WARNING("FRHICommandListPool::Initialize - Pool already initialized");
        return true;
    }
    
    MR_LOG_INFO("FRHICommandListPool::Initialize - Initializing command list pool with " +
               std::to_string(InitialPoolSize) + " initial command lists");
    
    s_instance = MakeUnique<FRHICommandListPool>();
    
    // Pre-allocate initial pool of command lists
    for (uint32 i = 0; i < InitialPoolSize; ++i) {
        auto* cmdList = s_instance->CreateCommandList(
            ECommandListType::Graphics,
            MonsterRender::RHI::ERecordingThread::Render
        );
        
        if (cmdList) {
            std::lock_guard<std::mutex> lock(s_instance->m_poolMutex);
            s_instance->m_commandListPool.emplace(cmdList, ECommandListType::Graphics);
        }
    }
    
    MR_LOG_INFO("FRHICommandListPool::Initialize - Command list pool initialized successfully");
    return true;
}

void FRHICommandListPool::Shutdown() {
    if (!s_instance) {
        MR_LOG_WARNING("FRHICommandListPool::Shutdown - Pool not initialized");
        return;
    }
    
    MR_LOG_INFO("FRHICommandListPool::Shutdown - Shutting down command list pool");
    
    // Get final statistics
    auto stats = s_instance->GetStatsInternal();
    MR_LOG_INFO("FRHICommandListPool::Shutdown - Final stats: " +
               std::to_string(stats.totalAllocated) + " allocated, " +
               std::to_string(stats.totalRecycled) + " recycled, " +
               std::to_string(stats.activeCommandLists) + " active, " +
               std::to_string(stats.pooledCommandLists) + " pooled, " +
               std::to_string(stats.peakActiveCount) + " peak active");
    
    s_instance.reset();
    
    MR_LOG_INFO("FRHICommandListPool::Shutdown - Command list pool shutdown complete");
}

bool FRHICommandListPool::IsInitialized() {
    return s_instance != nullptr;
}

MonsterRender::RHI::IRHICommandList* FRHICommandListPool::AllocateCommandList(
    ECommandListType Type,
    MonsterRender::RHI::ERecordingThread RecordingThread
) {
    if (!s_instance) {
        MR_LOG_ERROR("FRHICommandListPool::AllocateCommandList - Pool not initialized");
        return nullptr;
    }
    
    return s_instance->AllocateCommandListInternal(Type, RecordingThread);
}

void FRHICommandListPool::RecycleCommandList(MonsterRender::RHI::IRHICommandList* CommandList) {
    if (!s_instance) {
        MR_LOG_ERROR("FRHICommandListPool::RecycleCommandList - Pool not initialized");
        return;
    }
    
    if (!CommandList) {
        MR_LOG_WARNING("FRHICommandListPool::RecycleCommandList - Null command list");
        return;
    }
    
    s_instance->RecycleCommandListInternal(CommandList);
}

FRHICommandListPool::FPoolStats FRHICommandListPool::GetStats() {
    if (!s_instance) {
        return FPoolStats{};
    }
    
    return s_instance->GetStatsInternal();
}

void FRHICommandListPool::TrimPool(uint32 TargetPoolSize) {
    if (!s_instance) {
        MR_LOG_WARNING("FRHICommandListPool::TrimPool - Pool not initialized");
        return;
    }
    
    s_instance->TrimPoolInternal(TargetPoolSize);
}

FRHICommandListPool& FRHICommandListPool::Get() {
    return *s_instance;
}

MonsterRender::RHI::IRHICommandList* FRHICommandListPool::AllocateCommandListInternal(
    ECommandListType Type,
    MonsterRender::RHI::ERecordingThread RecordingThread
) {
    MonsterRender::RHI::IRHICommandList* cmdList = nullptr;
    
    // Try to get from pool first
    {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        
        if (!m_commandListPool.empty()) {
            FPoolEntry entry = m_commandListPool.front();
            m_commandListPool.pop();
            
            cmdList = entry.commandList;
            
            MR_LOG_DEBUG("FRHICommandListPool::AllocateCommandListInternal - Reusing command list from pool");
        }
    }
    
    // Create new if pool is empty
    if (!cmdList) {
        cmdList = CreateCommandList(Type, RecordingThread);
        
        if (!cmdList) {
            MR_LOG_ERROR("FRHICommandListPool::AllocateCommandListInternal - Failed to create command list");
            return nullptr;
        }
        
        MR_LOG_DEBUG("FRHICommandListPool::AllocateCommandListInternal - Created new command list");
    }
    
    // Reset command list state
    cmdList->reset();
    
    // Update statistics
    m_totalAllocated.fetch_add(1, std::memory_order_relaxed);
    uint32 activeCount = m_activeCommandLists.fetch_add(1, std::memory_order_relaxed) + 1;
    
    // Update peak count
    uint32 currentPeak = m_peakActiveCount.load(std::memory_order_relaxed);
    while (activeCount > currentPeak) {
        if (m_peakActiveCount.compare_exchange_weak(currentPeak, activeCount, std::memory_order_relaxed)) {
            break;
        }
    }
    
    MR_LOG_DEBUG("FRHICommandListPool::AllocateCommandListInternal - Allocated command list. Active: " +
               std::to_string(activeCount));
    
    return cmdList;
}

void FRHICommandListPool::RecycleCommandListInternal(MonsterRender::RHI::IRHICommandList* CommandList) {
    if (!CommandList) {
        return;
    }
    
    // Reset command list state before returning to pool
    CommandList->reset();
    
    // Return to pool
    {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        m_commandListPool.emplace(CommandList, ECommandListType::Graphics);
    }
    
    // Update statistics
    m_totalRecycled.fetch_add(1, std::memory_order_relaxed);
    uint32 activeCount = m_activeCommandLists.fetch_sub(1, std::memory_order_relaxed) - 1;
    
    MR_LOG_DEBUG("FRHICommandListPool::RecycleCommandListInternal - Recycled command list. Active: " +
               std::to_string(activeCount));
}

MonsterRender::RHI::IRHICommandList* FRHICommandListPool::CreateCommandList(
    ECommandListType Type,
    MonsterRender::RHI::ERecordingThread RecordingThread
) {
    MR_LOG_DEBUG("FRHICommandListPool::CreateCommandList - Creating new command list");
    
    // Create platform-specific command list
    // TODO: In production, determine which RHI backend to use (Vulkan/OpenGL)
    // For now, use MockCommandList for testing
    
    MonsterRender::RHI::IRHICommandList* cmdList = nullptr;
    
    // For testing purposes, create a mock command list
    // In production, this would create VulkanRHICommandList or OpenGLCommandList
    // based on the current RHI backend
    
#ifdef USE_MOCK_COMMAND_LIST
    cmdList = new MockCommandList(RecordingThread);
    MR_LOG_DEBUG("FRHICommandListPool::CreateCommandList - Created mock command list for testing");
#else
    // Production path - integrate with actual RHI backend
    // This will be implemented when we integrate with VulkanRHICommandList
    MR_LOG_WARNING("FRHICommandListPool::CreateCommandList - Command list creation not yet implemented. "
                  "Need to integrate with platform-specific RHI backend");
#endif
    
    return cmdList;
}

FRHICommandListPool::FPoolStats FRHICommandListPool::GetStatsInternal() const {
    FPoolStats stats;
    
    stats.totalAllocated = m_totalAllocated.load(std::memory_order_relaxed);
    stats.totalRecycled = m_totalRecycled.load(std::memory_order_relaxed);
    stats.activeCommandLists = m_activeCommandLists.load(std::memory_order_relaxed);
    stats.peakActiveCount = m_peakActiveCount.load(std::memory_order_relaxed);
    
    {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        stats.pooledCommandLists = static_cast<uint32>(m_commandListPool.size());
    }
    
    return stats;
}

void FRHICommandListPool::TrimPoolInternal(uint32 TargetPoolSize) {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    uint32 currentSize = static_cast<uint32>(m_commandListPool.size());
    
    if (currentSize <= TargetPoolSize) {
        MR_LOG_DEBUG("FRHICommandListPool::TrimPoolInternal - Pool size " +
                   std::to_string(currentSize) + " already at or below target " +
                   std::to_string(TargetPoolSize));
        return;
    }
    
    uint32 toRemove = currentSize - TargetPoolSize;
    
    MR_LOG_INFO("FRHICommandListPool::TrimPoolInternal - Trimming pool from " +
               std::to_string(currentSize) + " to " + std::to_string(TargetPoolSize) +
               " (removing " + std::to_string(toRemove) + " command lists)");
    
    for (uint32 i = 0; i < toRemove; ++i) {
        if (m_commandListPool.empty()) {
            break;
        }
        
        FPoolEntry entry = m_commandListPool.front();
        m_commandListPool.pop();
        
        if (entry.commandList) {
            delete entry.commandList;
        }
    }
    
    MR_LOG_INFO("FRHICommandListPool::TrimPoolInternal - Pool trimmed to " +
               std::to_string(m_commandListPool.size()) + " command lists");
}

} // namespace RHI
} // namespace MonsterEngine
