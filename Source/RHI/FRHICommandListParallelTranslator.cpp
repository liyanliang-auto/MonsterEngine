// Copyright Monster Engine. All Rights Reserved.

#include "RHI/FRHICommandListParallelTranslator.h"
#include "RHI/FTranslatedCommandBufferCollection.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanContextManager.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanRHICommandListRecorder.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"

namespace MonsterRender {
namespace RHI {

// Initialize static members
TUniquePtr<FRHICommandListParallelTranslator> FRHICommandListParallelTranslator::s_instance;

FRHICommandListParallelTranslator::FRHICommandListParallelTranslator() {
    MR_LOG_INFO("FRHICommandListParallelTranslator::FRHICommandListParallelTranslator - Parallel translator created");
}

FRHICommandListParallelTranslator::~FRHICommandListParallelTranslator() {
    MR_LOG_INFO("FRHICommandListParallelTranslator::~FRHICommandListParallelTranslator - Destroying parallel translator");
    
    // Wait for all pending translations
    WaitForAllTranslations();
    
    // Clean up active contexts
    {
        std::lock_guard<std::mutex> lock(m_contextMutex);
        m_activeContexts.clear();
    }
    
    MR_LOG_INFO("FRHICommandListParallelTranslator::~FRHICommandListParallelTranslator - Parallel translator destroyed. " +
               std::to_string(m_totalTranslations.load()) + " total translations, " +
               std::to_string(m_parallelTranslations.load()) + " parallel, " +
               std::to_string(m_serialTranslations.load()) + " serial");
}

bool FRHICommandListParallelTranslator::Initialize(bool bEnableParallelTranslate, uint32 MinDrawsPerTranslate) {
    if (s_instance) {
        MR_LOG_WARNING("FRHICommandListParallelTranslator::Initialize - Parallel translator already initialized");
        return true;
    }
    
    MR_LOG_INFO("FRHICommandListParallelTranslator::Initialize - Initializing parallel translator. " +
               String("Parallel translate: ") + (bEnableParallelTranslate ? "enabled" : "disabled") +
               ", Min draws: " + std::to_string(MinDrawsPerTranslate));
    
    s_instance = MakeUnique<FRHICommandListParallelTranslator>();
    s_instance->m_bEnableParallelTranslate = bEnableParallelTranslate;
    s_instance->m_minDrawsPerTranslate = MinDrawsPerTranslate;
    
    MR_LOG_INFO("FRHICommandListParallelTranslator::Initialize - Parallel translator initialized successfully");
    return true;
}

void FRHICommandListParallelTranslator::Shutdown() {
    if (!s_instance) {
        MR_LOG_WARNING("FRHICommandListParallelTranslator::Shutdown - Parallel translator not initialized");
        return;
    }
    
    MR_LOG_INFO("FRHICommandListParallelTranslator::Shutdown - Shutting down parallel translator");
    
    // Get final statistics
    auto stats = s_instance->GetStatsInternal();
    MR_LOG_INFO("FRHICommandListParallelTranslator::Shutdown - Final stats: " +
               std::to_string(stats.totalTranslations) + " total, " +
               std::to_string(stats.parallelTranslations) + " parallel, " +
               std::to_string(stats.serialTranslations) + " serial, " +
               std::to_string(stats.peakActiveTasks) + " peak active tasks");
    
    s_instance.reset();
    
    MR_LOG_INFO("FRHICommandListParallelTranslator::Shutdown - Parallel translator shutdown complete");
}

bool FRHICommandListParallelTranslator::IsInitialized() {
    return s_instance != nullptr;
}

MonsterEngine::FGraphEventRef FRHICommandListParallelTranslator::QueueParallelTranslate(
    TSpan<FQueuedCommandList> CommandLists,
    ETranslatePriority Priority,
    uint32 MinDrawsPerTranslate
) {
    if (!s_instance) {
        MR_LOG_ERROR("FRHICommandListParallelTranslator::QueueParallelTranslate - Parallel translator not initialized");
        return nullptr;
    }
    
    return s_instance->QueueParallelTranslateInternal(CommandLists, Priority, MinDrawsPerTranslate);
}

MonsterEngine::FGraphEventRef FRHICommandListParallelTranslator::QueueParallelTranslate(
    FQueuedCommandList CommandList,
    ETranslatePriority Priority
) {
    TArray<FQueuedCommandList> cmdLists = { CommandList };
    return QueueParallelTranslate(TSpan<FQueuedCommandList>(cmdLists), Priority, 0);
}

void FRHICommandListParallelTranslator::WaitForAllTranslations() {
    if (!s_instance) {
        return;
    }
    
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::WaitForAllTranslations - Waiting for all translations");
    
    TArray<MonsterEngine::FGraphEventRef> eventsToWait;
    
    {
        std::lock_guard<std::mutex> lock(s_instance->m_contextMutex);
        
        for (const auto& context : s_instance->m_activeContexts) {
            if (context && context->completionEvent) {
                eventsToWait.push_back(context->completionEvent);
            }
        }
    }
    
    // Wait for all events
    for (auto& event : eventsToWait) {
        if (event) {
            event->Wait();
        }
    }
    
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::WaitForAllTranslations - All translations complete");
}

FRHICommandListParallelTranslator::FStats FRHICommandListParallelTranslator::GetStats() {
    if (!s_instance) {
        return FStats{};
    }
    
    return s_instance->GetStatsInternal();
}

FRHICommandListParallelTranslator& FRHICommandListParallelTranslator::Get() {
    return *s_instance;
}

MonsterEngine::FGraphEventRef FRHICommandListParallelTranslator::QueueParallelTranslateInternal(
    TSpan<FQueuedCommandList> CommandLists,
    ETranslatePriority Priority,
    uint32 MinDrawsPerTranslate
) {
    if (CommandLists.empty()) {
        MR_LOG_WARNING("FRHICommandListParallelTranslator::QueueParallelTranslateInternal - Empty command list array");
        return nullptr;
    }
    
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::QueueParallelTranslateInternal - Queueing " +
               std::to_string(CommandLists.size()) + " command lists for translation");
    
    // Create parallel context
    auto context = MakeUnique<FParallelContext>();
    context->priority = Priority;
    context->numCommandLists = static_cast<uint32>(CommandLists.size());
    
    // Copy command lists
    for (const auto& queuedCmdList : CommandLists) {
        context->commandLists.push_back(queuedCmdList);
        context->totalDraws += queuedCmdList.numDraws;
    }
    
    // Determine if we should use parallel translation
    bool bUseParallel = ShouldUseParallelTranslation(CommandLists, Priority, MinDrawsPerTranslate);
    
    if (bUseParallel) {
        MR_LOG_DEBUG("FRHICommandListParallelTranslator::QueueParallelTranslateInternal - Using parallel translation");
        
        // Create translation tasks
        TArray<MonsterEngine::FGraphEventRef> translationEvents;
        auto tasks = CreateTranslationTasks(CommandLists, translationEvents);
        
        context->translationEvents = translationEvents;
        
        // Dispatch translation tasks to worker threads
        for (auto& task : tasks) {
            // Determine task priority based on translation priority
            String taskName = "ParallelTranslate_" + std::to_string(task.taskIndex);
            
            MonsterEngine::FGraphEventRef taskEvent = MonsterEngine::FTaskGraph::QueueNamedTask(
                taskName,
                [this, task]() {
                    TranslateCommandList(task);
                },
                {} // No prerequisites for now
            );
            
            // Store the task event
            if (task.taskIndex < translationEvents.size()) {
                translationEvents[task.taskIndex] = taskEvent;
            }
        }
        
        // Create completion event that waits for all translation tasks
        context->completionEvent = MonsterEngine::MakeGraphEvent();
        
        // Dispatch a task to mark completion when all translations are done
        MonsterEngine::FTaskGraph::QueueNamedTask(
            "ParallelTranslateComplete",
            [completionEvent = context->completionEvent]() {
                completionEvent->Complete();
            },
            translationEvents // Wait for all translation tasks
        );
        
        m_parallelTranslations.fetch_add(1, std::memory_order_relaxed);
    }
    else {
        MR_LOG_DEBUG("FRHICommandListParallelTranslator::QueueParallelTranslateInternal - Using serial translation");
        
        // Serial translation - execute on current thread or RHI thread
        context->completionEvent = MonsterEngine::MakeGraphEvent();
        
        for (const auto& queuedCmdList : CommandLists) {
            if (queuedCmdList.cmdList) {
                // For now, just mark as complete
                // In production, this would submit to RHI thread
                MR_LOG_DEBUG("FRHICommandListParallelTranslator::QueueParallelTranslateInternal - "
                           "Serial translate command list (placeholder)");
            }
        }
        
        context->completionEvent->Complete();
        m_serialTranslations.fetch_add(1, std::memory_order_relaxed);
    }
    
    m_totalTranslations.fetch_add(1, std::memory_order_relaxed);
    
    // Store context
    MonsterEngine::FGraphEventRef completionEvent = context->completionEvent;
    {
        std::lock_guard<std::mutex> lock(m_contextMutex);
        m_activeContexts.push_back(std::move(context));
    }
    
    return completionEvent;
}

void FRHICommandListParallelTranslator::TranslateCommandList(FTranslationTask Task) {
    if (!Task.cmdList) {
        MR_LOG_WARNING("FRHICommandListParallelTranslator::TranslateCommandList - Null command list");
        return;
    }
    
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::TranslateCommandList - Translating command list " +
               std::to_string(Task.taskIndex));
    
    // Update active task count
    uint32 activeCount = m_activeTasks.fetch_add(1, std::memory_order_relaxed) + 1;
    
    // Update peak count using CAS loop
    uint32 currentPeak = m_peakActiveTasks.load(std::memory_order_relaxed);
    while (activeCount > currentPeak) {
        if (m_peakActiveTasks.compare_exchange_weak(currentPeak, activeCount, std::memory_order_relaxed)) {
            break;
        }
    }
    
    // ========================================================================
    // Perform translation - Core parallel translation logic
    // ========================================================================
    // This integrates all components:
    // - VulkanContextManager: Thread-local context management
    // - VulkanRHICommandListRecorder: Recorded RHI commands
    // - VulkanResourceStateTracker: Automatic barrier insertion
    // - Secondary command buffers: Parallel command recording
    
    // Step 1: Get or create Vulkan context for current thread
    // Each worker thread gets its own context to avoid synchronization
    auto* vulkanContext = MonsterRender::RHI::Vulkan::FVulkanContextManager::GetOrCreateForCurrentThread();
    if (!vulkanContext) {
        MR_LOG_ERROR("FRHICommandListParallelTranslator::TranslateCommandList - "
                   "Failed to get Vulkan context for thread " + 
                   std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
        return;
    }
    
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::TranslateCommandList - "
               "Got Vulkan context for thread " + 
               std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    
    // Step 2: Allocate secondary command buffer for parallel recording
    // Secondary command buffers can be recorded in parallel and executed within a primary buffer
    // This is the key to true parallel command recording
    // 
    // Note: For now, we pass VK_NULL_HANDLE for render pass inheritance
    // In a full implementation, we would get the current render pass from the context
    auto* secondaryCmdBuffer = vulkanContext->AllocateSecondaryCommandBuffer();
    if (!secondaryCmdBuffer) {
        MR_LOG_ERROR("FRHICommandListParallelTranslator::TranslateCommandList - "
                   "Failed to allocate secondary command buffer");
        return;
    }
    
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::TranslateCommandList - "
               "Allocated secondary command buffer for parallel recording");
    
    // Step 3: Cast to VulkanRHICommandListRecorder and prepare for replay
    // The recorder has stored all RHI commands, now we translate them to Vulkan
    auto* recorder = static_cast<MonsterRender::RHI::Vulkan::VulkanRHICommandListRecorder*>(Task.cmdList);
    if (!recorder) {
        MR_LOG_ERROR("FRHICommandListParallelTranslator::TranslateCommandList - "
                   "Failed to cast command list to VulkanRHICommandListRecorder");
        return;
    }
    
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::TranslateCommandList - "
               "Replaying " + std::to_string(recorder->GetCommandCount()) + " commands, " +
               std::to_string(recorder->GetDrawCallCount()) + " draw calls");
    
    // Step 4: Begin recording the secondary command buffer
    // Secondary command buffers must be begun with inheritance info
    // For now, we use a simple begin without render pass inheritance
    secondaryCmdBuffer->begin();
    
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::TranslateCommandList - "
               "Began recording secondary command buffer");
    
    // Step 5: Replay all recorded commands to the secondary command buffer
    // This translates high-level RHI commands to low-level Vulkan API calls
    // The resource state tracker will automatically insert barriers as needed
    bool replaySuccess = recorder->ReplayToVulkanCommandBuffer(
        secondaryCmdBuffer, 
        reinterpret_cast<MonsterRender::RHI::Vulkan::FVulkanContext*>(vulkanContext)
    );
    
    if (!replaySuccess) {
        MR_LOG_ERROR("FRHICommandListParallelTranslator::TranslateCommandList - "
                   "Failed to replay commands to Vulkan command buffer");
        secondaryCmdBuffer->end();
        vulkanContext->FreeSecondaryCommandBuffer(secondaryCmdBuffer);
        return;
    }
    
    // Step 6: End recording the secondary command buffer
    secondaryCmdBuffer->end();
    
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::TranslateCommandList - "
               "Ended recording secondary command buffer");
    
    // Step 7: Add the translated secondary command buffer to the collection
    // The main thread will execute all collected buffers using vkCmdExecuteCommands
    // 
    // Check if we have a command buffer collection available
    // If not, we free the buffer to avoid memory leaks (fallback behavior)
    {
        std::lock_guard<std::mutex> lock(m_contextMutex);
        
        if (m_commandBufferCollection) {
            // Add the secondary command buffer to the collection
            // The collection will manage the buffer lifetime and execute it later
            m_commandBufferCollection->AddSecondaryCommandBuffer(
                secondaryCmdBuffer,
                vulkanContext,
                Task.taskIndex,
                recorder->GetCommandCount(),
                recorder->GetDrawCallCount()
            );
            
            MR_LOG_DEBUG("FRHICommandListParallelTranslator::TranslateCommandList - "
                       "Added secondary command buffer to collection for task " + 
                       std::to_string(Task.taskIndex));
        } else {
            // No collection available - free the buffer to avoid memory leak
            // This is a fallback path for when collection is not set up
            vulkanContext->FreeSecondaryCommandBuffer(secondaryCmdBuffer);
            
            MR_LOG_WARNING("FRHICommandListParallelTranslator::TranslateCommandList - "
                         "No command buffer collection available, freeing buffer immediately. "
                         "This indicates the collection was not set up properly.");
        }
    }
    
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::TranslateCommandList - "
               "Translation complete for task " + std::to_string(Task.taskIndex) +
               ". Thread: " + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) +
               ". Commands: " + std::to_string(recorder->GetCommandCount()) +
               ", Draws: " + std::to_string(recorder->GetDrawCallCount()));
    
    // Mark task complete
    if (Task.completionEvent) {
        Task.completionEvent->Complete();
    }
    
    // Update active task count
    m_activeTasks.fetch_sub(1, std::memory_order_relaxed);
}

bool FRHICommandListParallelTranslator::ShouldUseParallelTranslation(
    TSpan<FQueuedCommandList> CommandLists,
    ETranslatePriority Priority,
    uint32 MinDrawsPerTranslate
) const {
    // Check if parallel translation is disabled
    if (Priority == ETranslatePriority::Disabled) {
        return false;
    }
    
    // Check if parallel translation is globally disabled
    if (!m_bEnableParallelTranslate) {
        return false;
    }
    
    // Need at least 2 command lists for parallel translation
    if (CommandLists.size() < 2) {
        return false;
    }
    
    // Check if we have enough draws to justify parallel translation
    uint32 totalDraws = 0;
    for (const auto& cmdList : CommandLists) {
        totalDraws += cmdList.numDraws;
    }
    
    uint32 minDraws = MinDrawsPerTranslate > 0 ? MinDrawsPerTranslate : m_minDrawsPerTranslate;
    if (totalDraws < minDraws) {
        MR_LOG_DEBUG("FRHICommandListParallelTranslator::ShouldUseParallelTranslation - "
                   "Not enough draws (" + std::to_string(totalDraws) + " < " + 
                   std::to_string(minDraws) + "), using serial translation");
        return false;
    }
    
    return true;
}

TArray<FRHICommandListParallelTranslator::FTranslationTask> 
FRHICommandListParallelTranslator::CreateTranslationTasks(
    TSpan<FQueuedCommandList> CommandLists,
    TArray<MonsterEngine::FGraphEventRef>& OutEvents
) {
    TArray<FTranslationTask> tasks;
    tasks.reserve(CommandLists.size());
    OutEvents.reserve(CommandLists.size());
    
    for (uint32 i = 0; i < CommandLists.size(); ++i) {
        auto& queuedCmdList = CommandLists[i];
        
        if (!queuedCmdList.cmdList) {
            MR_LOG_WARNING("FRHICommandListParallelTranslator::CreateTranslationTasks - "
                         "Null command list at index " + std::to_string(i));
            continue;
        }
        
        // Create completion event for this task
        MonsterEngine::FGraphEventRef taskEvent = MonsterEngine::MakeGraphEvent();
        OutEvents.push_back(taskEvent);
        
        // Create translation task
        FTranslationTask task(queuedCmdList.cmdList, taskEvent, i);
        tasks.push_back(task);
        
        MR_LOG_DEBUG("FRHICommandListParallelTranslator::CreateTranslationTasks - "
                   "Created task " + std::to_string(i) + " with " + 
                   std::to_string(queuedCmdList.numDraws) + " draws");
    }
    
    return tasks;
}

FRHICommandListParallelTranslator::FStats FRHICommandListParallelTranslator::GetStatsInternal() const {
    FStats stats;
    
    stats.totalTranslations = m_totalTranslations.load(std::memory_order_relaxed);
    stats.parallelTranslations = m_parallelTranslations.load(std::memory_order_relaxed);
    stats.serialTranslations = m_serialTranslations.load(std::memory_order_relaxed);
    stats.activeTasks = m_activeTasks.load(std::memory_order_relaxed);
    stats.peakActiveTasks = m_peakActiveTasks.load(std::memory_order_relaxed);
    
    return stats;
}

// ============================================================================
// FParallelCommandListSet Implementation
// ============================================================================

FParallelCommandListSet::FParallelCommandListSet() {
    MR_LOG_DEBUG("FParallelCommandListSet::FParallelCommandListSet - Created parallel command list set");
}

FParallelCommandListSet::~FParallelCommandListSet() {
    MR_LOG_DEBUG("FParallelCommandListSet::~FParallelCommandListSet - Destroying parallel command list set");
    
    // Wait for completion if submitted
    if (m_bSubmitted && m_completionEvent) {
        Wait();
    }
    
    // Recycle all command lists back to pool
    for (auto* cmdList : m_commandLists) {
        if (cmdList) {
            MonsterRender::RHI::FRHICommandListPool::RecycleCommandList(cmdList);
        }
    }
    
    m_commandLists.clear();
}

MonsterRender::RHI::IRHICommandList* FParallelCommandListSet::AllocateCommandList(
    MonsterRender::RHI::FRHICommandListPool::ECommandListType Type)
{
    if (m_bSubmitted) {
        MR_LOG_ERROR("FParallelCommandListSet::AllocateCommandList - Cannot allocate after submission");
        return nullptr;
    }
    
    auto* cmdList = MonsterRender::RHI::FRHICommandListPool::AllocateCommandList(Type);
    if (cmdList) {
        m_commandLists.push_back(cmdList);
        MR_LOG_DEBUG("FParallelCommandListSet::AllocateCommandList - Allocated command list " +
                   std::to_string(m_commandLists.size()));
    }
    
    return cmdList;
}

MonsterEngine::FGraphEventRef FParallelCommandListSet::Submit(ETranslatePriority Priority) {
    if (m_bSubmitted) {
        MR_LOG_WARNING("FParallelCommandListSet::Submit - Already submitted");
        return m_completionEvent;
    }
    
    if (m_commandLists.empty()) {
        MR_LOG_WARNING("FParallelCommandListSet::Submit - No command lists to submit");
        return nullptr;
    }
    
    MR_LOG_DEBUG("FParallelCommandListSet::Submit - Submitting " +
               std::to_string(m_commandLists.size()) + " command lists");
    
    // Create queued command list array
    TArray<FQueuedCommandList> queuedLists;
    queuedLists.reserve(m_commandLists.size());
    
    for (auto* cmdList : m_commandLists) {
        if (cmdList) {
            queuedLists.emplace_back(cmdList, 0);
        }
    }
    
    // Submit for parallel translation
    m_completionEvent = FRHICommandListParallelTranslator::QueueParallelTranslate(
        TSpan<FQueuedCommandList>(queuedLists),
        Priority
    );
    
    m_bSubmitted = true;
    
    return m_completionEvent;
}

void FParallelCommandListSet::Wait() {
    if (!m_bSubmitted) {
        MR_LOG_WARNING("FParallelCommandListSet::Wait - Not yet submitted");
        return;
    }
    
    if (m_completionEvent) {
        MR_LOG_DEBUG("FParallelCommandListSet::Wait - Waiting for completion");
        m_completionEvent->Wait();
        MR_LOG_DEBUG("FParallelCommandListSet::Wait - Completion event signaled");
    }
}

// ============================================================================
// Command Buffer Collection Management
// ============================================================================

void FRHICommandListParallelTranslator::SetCommandBufferCollection(
    TUniquePtr<FTranslatedCommandBufferCollection> collection) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    m_commandBufferCollection = std::move(collection);
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::SetCommandBufferCollection - "
               "Set new command buffer collection");
}

void FRHICommandListParallelTranslator::ResetCommandBufferCollection() {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    if (m_commandBufferCollection) {
        m_commandBufferCollection->Reset();
        MR_LOG_DEBUG("FRHICommandListParallelTranslator::ResetCommandBufferCollection - "
                   "Reset command buffer collection");
    }
}

MonsterEngine::FGraphEventRef FRHICommandListParallelTranslator::TranslateCommandListAsync(
    MonsterRender::RHI::IRHICommandList* cmdList,
    uint32 taskIndex) {
    
    if (!cmdList) {
        MR_LOG_ERROR("FRHICommandListParallelTranslator::TranslateCommandListAsync - "
                   "Null command list");
        return nullptr;
    }
    
    // Create completion event
    auto completionEvent = MonsterEngine::MakeShared<MonsterEngine::FGraphEvent>();
    
    // Create translation task
    FTranslationTask task(cmdList, completionEvent, taskIndex);
    
    // Queue task on task graph
    MonsterEngine::FTaskGraph::QueueTask(
        [this, task]() {
            this->TranslateCommandList(task);
        }
    );
    
    MR_LOG_DEBUG("FRHICommandListParallelTranslator::TranslateCommandListAsync - "
               "Queued translation task " + std::to_string(taskIndex));
    
    return completionEvent;
}

} // namespace RHI
} // namespace MonsterRender
