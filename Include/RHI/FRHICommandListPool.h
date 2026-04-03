// Copyright Monster Engine. All Rights Reserved.

#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHICommandList.h"
#include <atomic>
#include <mutex>
#include <queue>

namespace MonsterRender {
namespace RHI {

/**
 * FRHICommandListPool
 * 
 * Manages a pool of RHI command list objects for efficient reuse.
 * Reduces memory allocation overhead by recycling command lists.
 * 
 * Key features:
 * - Thread-safe allocation and recycling
 * - Automatic pool expansion when needed
 * - Support for different command list types (Graphics/Compute/Transfer)
 * - Statistics tracking for pool usage
 * - RAII-style automatic recycling
 * 
 * Based on UE5's command list pooling strategy.
 * Reference: UE5 RHI command list management
 */
class FRHICommandListPool {
public:
    /**
     * Command list type enumeration
     * Determines which queue family the command list will use
     */
    enum class ECommandListType : uint8 {
        Graphics,    // Graphics queue (rendering, compute, transfer)
        Compute,     // Async compute queue
        Transfer     // Dedicated transfer queue
    };
    
    /**
     * Pool statistics for monitoring and debugging
     */
    struct FPoolStats {
        uint32 totalAllocated = 0;      // Total command lists allocated
        uint32 totalRecycled = 0;       // Total command lists recycled
        uint32 activeCommandLists = 0;  // Currently in-use command lists
        uint32 pooledCommandLists = 0;  // Command lists available in pool
        uint32 peakActiveCount = 0;     // Peak number of active command lists
    };
    
    /**
     * Initialize the command list pool
     * @param InitialPoolSize Initial number of command lists to pre-allocate
     * @return True if initialization succeeded
     */
    static bool Initialize(uint32 InitialPoolSize = 8);
    
    /**
     * Shutdown the command list pool
     * Releases all pooled command lists
     */
    static void Shutdown();
    
    /**
     * Check if pool is initialized
     */
    static bool IsInitialized();
    
    /**
     * Allocate a command list from the pool
     * If pool is empty, creates a new command list
     * 
     * @param Type Command list type (Graphics/Compute/Transfer)
     * @param RecordingThread Thread that will record commands
     * @return Pointer to allocated command list, nullptr on failure
     */
    static MonsterRender::RHI::IRHICommandList* AllocateCommandList(
        ECommandListType Type = ECommandListType::Graphics,
        MonsterRender::RHI::ERecordingThread RecordingThread = MonsterRender::RHI::ERecordingThread::Render
    );
    
    /**
     * Recycle a command list back to the pool
     * Resets the command list state before returning to pool
     * 
     * @param CommandList Command list to recycle
     */
    static void RecycleCommandList(MonsterRender::RHI::IRHICommandList* CommandList);
    
    /**
     * Get pool statistics
     */
    static FPoolStats GetStats();
    
    /**
     * Trim the pool to reduce memory usage
     * Releases excess pooled command lists
     * 
     * @param TargetPoolSize Desired pool size after trimming
     */
    static void TrimPool(uint32 TargetPoolSize = 4);
    
    FRHICommandListPool();
    ~FRHICommandListPool();
    
private:
    /**
     * Pool entry for a command list
     */
    struct FPoolEntry {
        MonsterRender::RHI::IRHICommandList* commandList = nullptr;
        ECommandListType type = ECommandListType::Graphics;
        
        FPoolEntry() = default;
        FPoolEntry(MonsterRender::RHI::IRHICommandList* InCmdList, ECommandListType InType)
            : commandList(InCmdList)
            , type(InType)
        {}
    };
    
    /**
     * Get singleton instance
     */
    static FRHICommandListPool& Get();
    
    /**
     * Internal allocation implementation
     */
    MonsterRender::RHI::IRHICommandList* AllocateCommandListInternal(
        ECommandListType Type,
        MonsterRender::RHI::ERecordingThread RecordingThread
    );
    
    /**
     * Internal recycling implementation
     */
    void RecycleCommandListInternal(MonsterRender::RHI::IRHICommandList* CommandList);
    
    /**
     * Create a new command list
     */
    MonsterRender::RHI::IRHICommandList* CreateCommandList(
        ECommandListType Type,
        MonsterRender::RHI::ERecordingThread RecordingThread
    );
    
    /**
     * Get pool statistics (internal)
     */
    FPoolStats GetStatsInternal() const;
    
    /**
     * Trim pool (internal)
     */
    void TrimPoolInternal(uint32 TargetPoolSize);
    
private:
    /** Singleton instance */
    static TUniquePtr<FRHICommandListPool> s_instance;
    
    /** Pool of available command lists */
    std::queue<FPoolEntry> m_commandListPool;
    
    /** Mutex for thread-safe pool access */
    mutable std::mutex m_poolMutex;
    
    /** Statistics */
    std::atomic<uint32> m_totalAllocated{0};
    std::atomic<uint32> m_totalRecycled{0};
    std::atomic<uint32> m_activeCommandLists{0};
    std::atomic<uint32> m_peakActiveCount{0};
};

/**
 * RAII helper for automatic command list recycling
 * Ensures command list is returned to pool when scope exits
 * 
 * Usage:
 *   {
 *       FScopedCommandList cmdList(FRHICommandListPool::AllocateCommandList());
 *       cmdList->begin();
 *       // Record commands...
 *       cmdList->end();
 *   } // Automatically recycled here
 */
class FScopedCommandList {
public:
    FScopedCommandList(MonsterRender::RHI::IRHICommandList* InCommandList)
        : m_commandList(InCommandList)
    {}
    
    ~FScopedCommandList() {
        if (m_commandList) {
            FRHICommandListPool::RecycleCommandList(m_commandList);
        }
    }
    
    // Non-copyable
    FScopedCommandList(const FScopedCommandList&) = delete;
    FScopedCommandList& operator=(const FScopedCommandList&) = delete;
    
    // Movable
    FScopedCommandList(FScopedCommandList&& Other) noexcept
        : m_commandList(Other.m_commandList)
    {
        Other.m_commandList = nullptr;
    }
    
    FScopedCommandList& operator=(FScopedCommandList&& Other) noexcept {
        if (this != &Other) {
            if (m_commandList) {
                FRHICommandListPool::RecycleCommandList(m_commandList);
            }
            m_commandList = Other.m_commandList;
            Other.m_commandList = nullptr;
        }
        return *this;
    }
    
    MonsterRender::RHI::IRHICommandList* operator->() const { return m_commandList; }
    MonsterRender::RHI::IRHICommandList* Get() const { return m_commandList; }
    
    /**
     * Release ownership without recycling
     */
    MonsterRender::RHI::IRHICommandList* Release() {
        MonsterRender::RHI::IRHICommandList* temp = m_commandList;
        m_commandList = nullptr;
        return temp;
    }
    
private:
    MonsterRender::RHI::IRHICommandList* m_commandList;
};

} // namespace RHI
} // namespace MonsterRender
