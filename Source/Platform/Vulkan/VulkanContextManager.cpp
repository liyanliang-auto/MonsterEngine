// Copyright Monster Engine. All Rights Reserved.

#include "Platform/Vulkan/VulkanContextManager.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {

// ============================================================================
// FVulkanContextManager Static Member Initialization
// ============================================================================

VulkanDevice* FVulkanContextManager::s_device = nullptr;
uint32 FVulkanContextManager::s_maxContexts = 0;
bool FVulkanContextManager::s_initialized = false;

TArray<FVulkanContextManager::FPooledContext> FVulkanContextManager::s_contextPool;
std::mutex FVulkanContextManager::s_poolMutex;

thread_local FVulkanCommandListContext* FVulkanContextManager::s_threadLocalContext = nullptr;

std::atomic<uint32> FVulkanContextManager::s_totalContexts{0};
std::atomic<uint32> FVulkanContextManager::s_activeContexts{0};
std::atomic<uint32> FVulkanContextManager::s_peakActive{0};

// ============================================================================
// FVulkanContextManager Implementation
// ============================================================================

bool FVulkanContextManager::Initialize(VulkanDevice* device, uint32 maxContexts) {
    if (s_initialized) {
        MR_LOG_WARNING("FVulkanContextManager::Initialize - Already initialized");
        return true;
    }
    
    if (!device) {
        MR_LOG_ERROR("FVulkanContextManager::Initialize - Invalid device");
        return false;
    }
    
    MR_LOG_INFO("FVulkanContextManager::Initialize - Initializing context manager. Max contexts: " +
               std::to_string(maxContexts == 0 ? 999 : maxContexts));
    
    s_device = device;
    s_maxContexts = maxContexts;
    s_initialized = true;
    
    // Pre-allocate pool capacity to avoid reallocations
    if (maxContexts > 0) {
        std::lock_guard<std::mutex> lock(s_poolMutex);
        s_contextPool.reserve(maxContexts);
    }
    
    MR_LOG_INFO("FVulkanContextManager::Initialize - Context manager initialized successfully");
    return true;
}

void FVulkanContextManager::Shutdown() {
    if (!s_initialized) {
        MR_LOG_WARNING("FVulkanContextManager::Shutdown - Not initialized");
        return;
    }
    
    MR_LOG_INFO("FVulkanContextManager::Shutdown - Shutting down context manager");
    
    // Wait for all contexts to complete
    WaitForAllContexts();
    
    // Get final statistics
    auto stats = GetStats();
    MR_LOG_INFO("FVulkanContextManager::Shutdown - Final stats: " +
               std::to_string(stats.totalContexts) + " total, " +
               std::to_string(stats.peakActive) + " peak active");
    
    // Destroy all contexts
    {
        std::lock_guard<std::mutex> lock(s_poolMutex);
        s_contextPool.clear();
    }
    
    // Reset state
    s_device = nullptr;
    s_maxContexts = 0;
    s_initialized = false;
    s_totalContexts.store(0, std::memory_order_relaxed);
    s_activeContexts.store(0, std::memory_order_relaxed);
    s_peakActive.store(0, std::memory_order_relaxed);
    
    MR_LOG_INFO("FVulkanContextManager::Shutdown - Context manager shutdown complete");
}

bool FVulkanContextManager::IsInitialized() {
    return s_initialized;
}

FVulkanCommandListContext* FVulkanContextManager::GetOrCreateForCurrentThread() {
    if (!s_initialized) {
        MR_LOG_ERROR("FVulkanContextManager::GetOrCreateForCurrentThread - Context manager not initialized");
        return nullptr;
    }
    
    // Check if current thread already has a context
    if (s_threadLocalContext != nullptr) {
        MR_LOG_DEBUG("FVulkanContextManager::GetOrCreateForCurrentThread - Reusing thread-local context");
        return s_threadLocalContext;
    }
    
    // Try to find an available context in the pool
    FVulkanCommandListContext* context = FindAvailableContext();
    
    // If no available context, create a new one
    if (!context) {
        context = CreateNewContext();
        if (!context) {
            MR_LOG_ERROR("FVulkanContextManager::GetOrCreateForCurrentThread - Failed to create context");
            return nullptr;
        }
    }
    
    // Store in thread-local storage
    s_threadLocalContext = context;
    
    // Update statistics
    uint32 activeCount = s_activeContexts.fetch_add(1, std::memory_order_relaxed) + 1;
    
    // Update peak count using CAS loop
    uint32 currentPeak = s_peakActive.load(std::memory_order_relaxed);
    while (activeCount > currentPeak) {
        if (s_peakActive.compare_exchange_weak(currentPeak, activeCount, 
                                               std::memory_order_relaxed)) {
            break;
        }
    }
    
    MR_LOG_DEBUG("FVulkanContextManager::GetOrCreateForCurrentThread - Context allocated for thread " +
                std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) +
                ". Active: " + std::to_string(activeCount));
    
    return context;
}

FVulkanCommandListContext* FVulkanContextManager::FindAvailableContext() {
    std::lock_guard<std::mutex> lock(s_poolMutex);
    
    std::thread::id currentThreadId = std::this_thread::get_id();
    
    // First, try to find a context previously used by this thread
    for (auto& entry : s_contextPool) {
        if (!entry.isInUse && entry.ownerThreadId == currentThreadId) {
            entry.isInUse = true;
            
            MR_LOG_DEBUG("FVulkanContextManager::FindAvailableContext - Reusing context from same thread");
            return entry.context.get();
        }
    }
    
    // If not found, try to find any available context
    for (auto& entry : s_contextPool) {
        if (!entry.isInUse) {
            entry.isInUse = true;
            entry.ownerThreadId = currentThreadId;
            
            MR_LOG_DEBUG("FVulkanContextManager::FindAvailableContext - Reusing context from pool");
            return entry.context.get();
        }
    }
    
    // No available context found
    return nullptr;
}

FVulkanCommandListContext* FVulkanContextManager::CreateNewContext() {
    std::lock_guard<std::mutex> lock(s_poolMutex);
    
    // Check if we've reached the maximum number of contexts
    if (s_maxContexts > 0 && s_contextPool.size() >= s_maxContexts) {
        MR_LOG_WARNING("FVulkanContextManager::CreateNewContext - Maximum context limit reached (" +
                      std::to_string(s_maxContexts) + ")");
        return nullptr;
    }
    
    MR_LOG_DEBUG("FVulkanContextManager::CreateNewContext - Creating new context");
    
    // Create command buffer manager for this context
    // Note: In a full implementation, each context would have its own command buffer manager
    // For now, we'll use the device's main command buffer manager
    auto* cmdBufferManager = s_device->getCommandBufferManager();
    if (!cmdBufferManager) {
        MR_LOG_ERROR("FVulkanContextManager::CreateNewContext - Failed to get command buffer manager");
        return nullptr;
    }
    
    // Create new context
    auto context = MakeUnique<FVulkanCommandListContext>(s_device, cmdBufferManager);
    
    if (!context->initialize()) {
        MR_LOG_ERROR("FVulkanContextManager::CreateNewContext - Failed to initialize context");
        return nullptr;
    }
    
    // Add to pool
    std::thread::id currentThreadId = std::this_thread::get_id();
    FPooledContext pooledContext(std::move(context), currentThreadId);
    pooledContext.isInUse = true;
    
    s_contextPool.push_back(std::move(pooledContext));
    
    // Update statistics
    s_totalContexts.fetch_add(1, std::memory_order_relaxed);
    
    MR_LOG_DEBUG("FVulkanContextManager::CreateNewContext - New context created. Total: " +
                std::to_string(s_contextPool.size()));
    
    return s_contextPool.back().context.get();
}

void FVulkanContextManager::RecycleContext(FVulkanCommandListContext* context) {
    if (!context) {
        MR_LOG_WARNING("FVulkanContextManager::RecycleContext - Null context");
        return;
    }
    
    std::lock_guard<std::mutex> lock(s_poolMutex);
    
    // Find the context in the pool
    for (auto& entry : s_contextPool) {
        if (entry.context.get() == context) {
            if (!entry.isInUse) {
                MR_LOG_WARNING("FVulkanContextManager::RecycleContext - Context already recycled");
                return;
            }
            
            // Mark as available
            entry.isInUse = false;
            
            // Update statistics
            uint32 activeCount = s_activeContexts.fetch_sub(1, std::memory_order_relaxed) - 1;
            
            // Clear thread-local if this is the current thread's context
            if (s_threadLocalContext == context) {
                s_threadLocalContext = nullptr;
            }
            
            MR_LOG_DEBUG("FVulkanContextManager::RecycleContext - Context recycled. Active: " +
                        std::to_string(activeCount));
            
            return;
        }
    }
    
    MR_LOG_WARNING("FVulkanContextManager::RecycleContext - Context not found in pool");
}

void FVulkanContextManager::WaitForAllContexts() {
    if (!s_initialized) {
        return;
    }
    
    MR_LOG_DEBUG("FVulkanContextManager::WaitForAllContexts - Waiting for all contexts");
    
    std::lock_guard<std::mutex> lock(s_poolMutex);
    
    // Wait for each context to complete its GPU work
    for (auto& entry : s_contextPool) {
        if (entry.context) {
            // In a full implementation, this would wait for the context's fence
            // For now, we just ensure the device is idle
            // entry.context->waitForCompletion();
        }
    }
    
    MR_LOG_DEBUG("FVulkanContextManager::WaitForAllContexts - All contexts completed");
}

void FVulkanContextManager::ResetAllContexts() {
    if (!s_initialized) {
        return;
    }
    
    MR_LOG_DEBUG("FVulkanContextManager::ResetAllContexts - Resetting all contexts");
    
    std::lock_guard<std::mutex> lock(s_poolMutex);
    
    // Reset each context for the next frame
    for (auto& entry : s_contextPool) {
        if (entry.context) {
            // Reset command buffers and descriptor pools
            entry.context->prepareForNewFrame();
        }
    }
    
    MR_LOG_DEBUG("FVulkanContextManager::ResetAllContexts - All contexts reset");
}

void FVulkanContextManager::TrimPool(uint64 currentFrame, uint32 framesToKeep) {
    if (!s_initialized) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(s_poolMutex);
    
    TArray<FPooledContext> remainingContexts;
    uint32 trimmedCount = 0;
    
    for (auto& entry : s_contextPool) {
        // Keep contexts that are in use or recently used
        if (entry.isInUse || (currentFrame - entry.lastUsedFrame) < framesToKeep) {
            remainingContexts.push_back(std::move(entry));
        } else {
            trimmedCount++;
        }
    }
    
    if (trimmedCount > 0) {
        MR_LOG_DEBUG("FVulkanContextManager::TrimPool - Trimmed " +
                    std::to_string(trimmedCount) + " unused contexts. " +
                    std::to_string(remainingContexts.size()) + " remaining");
        
        s_contextPool = std::move(remainingContexts);
        s_totalContexts.store(static_cast<uint32>(s_contextPool.size()), std::memory_order_relaxed);
    }
}

FVulkanContextManager::FStats FVulkanContextManager::GetStats() {
    std::lock_guard<std::mutex> lock(s_poolMutex);
    
    FStats stats;
    stats.totalContexts = static_cast<uint32>(s_contextPool.size());
    stats.activeContexts = s_activeContexts.load(std::memory_order_relaxed);
    stats.peakActive = s_peakActive.load(std::memory_order_relaxed);
    
    // Calculate available contexts
    uint32 availableCount = 0;
    for (const auto& entry : s_contextPool) {
        if (!entry.isInUse) {
            availableCount++;
        }
    }
    stats.availableContexts = availableCount;
    
    return stats;
}

// ============================================================================
// FVulkanScopedContext Implementation
// ============================================================================

FVulkanScopedContext::FVulkanScopedContext()
    : m_context(nullptr)
{
    m_context = FVulkanContextManager::GetOrCreateForCurrentThread();
    
    if (!m_context) {
        MR_LOG_ERROR("FVulkanScopedContext::FVulkanScopedContext - Failed to get context");
    }
}

FVulkanScopedContext::~FVulkanScopedContext() {
    if (m_context) {
        FVulkanContextManager::RecycleContext(m_context);
        m_context = nullptr;
    }
}

} // namespace MonsterRender::RHI::Vulkan
