// Copyright Monster Engine. All Rights Reserved.

#pragma once

#include "Core/CoreMinimal.h"
#include "Core/FGraphEvent.h"
#include "Core/FTaskGraph.h"
#include "RHI/IRHICommandList.h"
#include "RHI/FRHICommandListPool.h"
#include <atomic>
#include <mutex>

namespace MonsterEngine {
namespace RHI {

/**
 * Parallel translate priority
 * Determines task scheduling priority for parallel command list translation
 * 
 * Based on UE5's ETranslatePriority
 * Reference: Engine/Source/Runtime/RHI/Public/RHICommandList.h
 */
enum class ETranslatePriority : uint8 {
    Disabled,  // Parallel translate disabled, execute on RHI thread
    Normal,    // Normal priority task thread
    High       // High priority task thread (e.g., for prepass)
};

/**
 * Queued command list for parallel submission
 * Wraps a command list with optional metadata
 * 
 * Based on UE5's FQueuedCommandList
 */
struct FQueuedCommandList {
    MonsterRender::RHI::IRHICommandList* cmdList = nullptr;
    uint32 numDraws = 0;  // Optional: number of draw calls for load balancing
    
    FQueuedCommandList() = default;
    
    FQueuedCommandList(MonsterRender::RHI::IRHICommandList* InCmdList, uint32 InNumDraws = 0)
        : cmdList(InCmdList)
        , numDraws(InNumDraws)
    {}
};

/**
 * FRHICommandListParallelTranslator
 * 
 * Manages parallel translation of RHI command lists to platform-specific commands.
 * Coordinates multi-threaded command list recording and merging.
 * 
 * Key responsibilities:
 * - Queue multiple command lists for parallel translation
 * - Dispatch translation tasks to worker threads
 * - Merge translated command lists in correct order
 * - Handle dependencies and synchronization
 * - Support different translation priorities
 * 
 * Based on UE5's parallel command list submission system.
 * Reference: UE5 FRHICommandListImmediate::QueueAsyncCommandListSubmit
 */
class FRHICommandListParallelTranslator {
public:
    /**
     * Parallel translation context
     * Tracks state for a batch of parallel command lists
     */
    struct FParallelContext {
        TArray<FQueuedCommandList> commandLists;
        TArray<FGraphEventRef> translationEvents;
        FGraphEventRef completionEvent;
        ETranslatePriority priority = ETranslatePriority::Normal;
        uint32 numCommandLists = 0;
        uint32 totalDraws = 0;
        
        FParallelContext() = default;
    };
    
    /**
     * Translation task data
     * Contains all information needed to translate a single command list
     */
    struct FTranslationTask {
        MonsterRender::RHI::IRHICommandList* cmdList = nullptr;
        FGraphEventRef completionEvent;
        uint32 taskIndex = 0;
        
        FTranslationTask() = default;
        
        FTranslationTask(MonsterRender::RHI::IRHICommandList* InCmdList, 
                        FGraphEventRef InEvent, 
                        uint32 InIndex)
            : cmdList(InCmdList)
            , completionEvent(InEvent)
            , taskIndex(InIndex)
        {}
    };
    
    /**
     * Initialize the parallel translator
     * @param bEnableParallelTranslate Enable parallel translation
     * @param MinDrawsPerTranslate Minimum draws to trigger parallel translation
     * @return True if initialization succeeded
     */
    static bool Initialize(bool bEnableParallelTranslate = true, uint32 MinDrawsPerTranslate = 100);
    
    /**
     * Shutdown the parallel translator
     */
    static void Shutdown();
    
    /**
     * Check if parallel translator is initialized
     */
    static bool IsInitialized();
    
    /**
     * Queue command lists for parallel translation and submission
     * 
     * @param CommandLists Array of command lists to submit
     * @param Priority Translation priority (Normal/High/Disabled)
     * @param MinDrawsPerTranslate Minimum draws to enable parallel translation
     * @return Completion event for all translations
     */
    static FGraphEventRef QueueParallelTranslate(
        TSpan<FQueuedCommandList> CommandLists,
        ETranslatePriority Priority = ETranslatePriority::Normal,
        uint32 MinDrawsPerTranslate = 100
    );
    
    /**
     * Queue a single command list for translation
     * Convenience method for single command list
     */
    static FGraphEventRef QueueParallelTranslate(
        FQueuedCommandList CommandList,
        ETranslatePriority Priority = ETranslatePriority::Normal
    );
    
    /**
     * Wait for all pending translations to complete
     */
    static void WaitForAllTranslations();
    
    /**
     * Get statistics
     */
    struct FStats {
        uint32 totalTranslations = 0;
        uint32 parallelTranslations = 0;
        uint32 serialTranslations = 0;
        uint32 activeTasks = 0;
        uint32 peakActiveTasks = 0;
    };
    
    static FStats GetStats();
    
    FRHICommandListParallelTranslator();
    ~FRHICommandListParallelTranslator();
    
private:
    /**
     * Get singleton instance
     */
    static FRHICommandListParallelTranslator& Get();
    
    /**
     * Internal implementation of parallel translate
     */
    FGraphEventRef QueueParallelTranslateInternal(
        TSpan<FQueuedCommandList> CommandLists,
        ETranslatePriority Priority,
        uint32 MinDrawsPerTranslate
    );
    
    /**
     * Translate a single command list on a worker thread
     * This is the actual translation task that runs in parallel
     */
    void TranslateCommandList(FTranslationTask Task);
    
    /**
     * Determine if parallel translation should be used
     */
    bool ShouldUseParallelTranslation(
        TSpan<FQueuedCommandList> CommandLists,
        ETranslatePriority Priority,
        uint32 MinDrawsPerTranslate
    ) const;
    
    /**
     * Create translation tasks for command lists
     */
    TArray<FTranslationTask> CreateTranslationTasks(
        TSpan<FQueuedCommandList> CommandLists,
        TArray<FGraphEventRef>& OutEvents
    );
    
    /**
     * Get statistics (internal)
     */
    FStats GetStatsInternal() const;
    
    /**
     * Get the current command buffer collection
     * This collection stores all translated secondary command buffers
     */
    class FTranslatedCommandBufferCollection* GetCommandBufferCollection() const {
        return m_commandBufferCollection.get();
    }
    
    /**
     * Set the command buffer collection for this frame
     * Should be called at the beginning of each frame
     */
    void SetCommandBufferCollection(TUniquePtr<FTranslatedCommandBufferCollection> collection);
    
    /**
     * Reset the command buffer collection
     * Called at the end of frame to prepare for next frame
     */
    void ResetCommandBufferCollection();
    
public:
    /**
     * Translate a single command list asynchronously
     * This is a convenience method for testing
     * 
     * @param cmdList Command list to translate
     * @param taskIndex Index of this task
     * @return Completion event
     */
    FGraphEventRef TranslateCommandListAsync(
        MonsterRender::RHI::IRHICommandList* cmdList,
        uint32 taskIndex
    );
    
private:
    /** Singleton instance */
    static TUniquePtr<FRHICommandListParallelTranslator> s_instance;
    
    /** Enable parallel translation */
    bool m_bEnableParallelTranslate = true;
    
    /** Minimum draws to trigger parallel translation */
    uint32 m_minDrawsPerTranslate = 100;
    
    /** Active parallel contexts */
    TArray<TUniquePtr<FParallelContext>> m_activeContexts;
    
    /** Mutex for context management */
    mutable std::mutex m_contextMutex;
    
    /** Command buffer collection for current frame */
    TUniquePtr<FTranslatedCommandBufferCollection> m_commandBufferCollection;
    
    /** Statistics */
    std::atomic<uint32> m_totalTranslations{0};
    std::atomic<uint32> m_parallelTranslations{0};
    std::atomic<uint32> m_serialTranslations{0};
    std::atomic<uint32> m_activeTasks{0};
    std::atomic<uint32> m_peakActiveTasks{0};
};

/**
 * Parallel command list set helper
 * RAII wrapper for managing a set of parallel command lists
 * 
 * Usage:
 *   FParallelCommandListSet parallelSet;
 *   
 *   // Allocate command lists for parallel recording
 *   auto* cmdList1 = parallelSet.AllocateCommandList();
 *   auto* cmdList2 = parallelSet.AllocateCommandList();
 *   
 *   // Record commands in parallel on different threads
 *   TaskGraph::QueueTask([cmdList1]() {
 *       cmdList1->begin();
 *       // Record commands...
 *       cmdList1->end();
 *   });
 *   
 *   // Submit all command lists for parallel translation
 *   parallelSet.Submit(ETranslatePriority::Normal);
 */
class FParallelCommandListSet {
public:
    FParallelCommandListSet();
    ~FParallelCommandListSet();
    
    /**
     * Allocate a command list from the pool for parallel recording
     */
    MonsterRender::RHI::IRHICommandList* AllocateCommandList(
        FRHICommandListPool::ECommandListType Type = FRHICommandListPool::ECommandListType::Graphics
    );
    
    /**
     * Submit all allocated command lists for parallel translation
     * @param Priority Translation priority
     * @return Completion event
     */
    FGraphEventRef Submit(ETranslatePriority Priority = ETranslatePriority::Normal);
    
    /**
     * Wait for all command lists to complete translation
     */
    void Wait();
    
    /**
     * Get number of command lists in this set
     */
    uint32 GetNumCommandLists() const { return static_cast<uint32>(m_commandLists.size()); }
    
private:
    TArray<MonsterRender::RHI::IRHICommandList*> m_commandLists;
    FGraphEventRef m_completionEvent;
    bool m_bSubmitted = false;
};

} // namespace RHI
} // namespace MonsterEngine
