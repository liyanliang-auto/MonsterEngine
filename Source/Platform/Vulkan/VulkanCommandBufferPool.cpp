// Copyright Monster Engine. All Rights Reserved.

#include "Platform/Vulkan/VulkanCommandBufferPool.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {

// ============================================================================
// FVulkanCommandBufferPool Implementation
// ============================================================================

FVulkanCommandBufferPool::FVulkanCommandBufferPool(VulkanDevice* device, uint32 queueFamilyIndex)
    : m_device(device)
    , m_queueFamilyIndex(queueFamilyIndex)
    , m_commandPool(VK_NULL_HANDLE)
{
    MR_LOG_DEBUG("FVulkanCommandBufferPool::FVulkanCommandBufferPool - Created command buffer pool for queue family " +
                std::to_string(queueFamilyIndex));
}

FVulkanCommandBufferPool::~FVulkanCommandBufferPool() {
    MR_LOG_DEBUG("FVulkanCommandBufferPool::~FVulkanCommandBufferPool - Destroying command buffer pool");
    
    const auto& functions = VulkanAPI::getFunctions();
    VkDevice device = m_device->getLogicalDevice();
    
    // Free all pooled command buffers
    if (m_commandPool != VK_NULL_HANDLE && !m_pooledBuffers.empty()) {
        TArray<VkCommandBuffer> buffersToFree;
        buffersToFree.reserve(m_pooledBuffers.size());
        
        for (const auto& entry : m_pooledBuffers) {
            if (entry.commandBuffer != VK_NULL_HANDLE) {
                buffersToFree.push_back(entry.commandBuffer);
            }
        }
        
        if (!buffersToFree.empty()) {
            functions.vkFreeCommandBuffers(device, m_commandPool, 
                                          static_cast<uint32>(buffersToFree.size()), 
                                          buffersToFree.data());
        }
    }
    
    // Destroy command pool
    if (m_commandPool != VK_NULL_HANDLE) {
        functions.vkDestroyCommandPool(device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    
    MR_LOG_DEBUG("FVulkanCommandBufferPool::~FVulkanCommandBufferPool - Command buffer pool destroyed. " +
                std::to_string(m_totalAllocated.load()) + " total allocated");
}

bool FVulkanCommandBufferPool::Initialize() {
    MR_LOG_DEBUG("FVulkanCommandBufferPool::Initialize - Initializing command buffer pool");
    
    if (!CreateCommandPool()) {
        MR_LOG_ERROR("FVulkanCommandBufferPool::Initialize - Failed to create command pool");
        return false;
    }
    
    MR_LOG_DEBUG("FVulkanCommandBufferPool::Initialize - Command buffer pool initialized successfully");
    return true;
}

bool FVulkanCommandBufferPool::CreateCommandPool() {
    const auto& functions = VulkanAPI::getFunctions();
    VkDevice device = m_device->getLogicalDevice();
    
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_queueFamilyIndex;
    
    // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT:
    // Allows individual command buffers to be reset (vkResetCommandBuffer)
    // This is essential for command buffer reuse in the pool
    //
    // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT:
    // Hints that command buffers are short-lived and frequently reset
    // Optimizes memory allocation for this usage pattern
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | 
                     VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    
    VkResult result = functions.vkCreateCommandPool(device, &poolInfo, nullptr, &m_commandPool);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanCommandBufferPool::CreateCommandPool - Failed to create command pool: " + 
                    std::to_string(result));
        return false;
    }
    
    MR_LOG_DEBUG("FVulkanCommandBufferPool::CreateCommandPool - Command pool created successfully");
    return true;
}

VkCommandBuffer FVulkanCommandBufferPool::AllocateCommandBuffer(ELevel level) {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    // Try to find an available command buffer in the pool
    for (auto& entry : m_pooledBuffers) {
        if (!entry.isInUse && entry.level == level) {
            entry.isInUse = true;
            
            // Update statistics
            uint32 activeCount = m_activeBuffers.fetch_add(1, std::memory_order_relaxed) + 1;
            
            // Update peak count
            uint32 currentPeak = m_peakActive.load(std::memory_order_relaxed);
            while (activeCount > currentPeak) {
                if (m_peakActive.compare_exchange_weak(currentPeak, activeCount, 
                                                       std::memory_order_relaxed)) {
                    break;
                }
            }
            
            MR_LOG_DEBUG("FVulkanCommandBufferPool::AllocateCommandBuffer - Reusing command buffer from pool. Active: " +
                        std::to_string(activeCount));
            
            return entry.commandBuffer;
        }
    }
    
    // No available buffer found, allocate a new one
    VkCommandBuffer newBuffer = AllocateNewCommandBuffer(level);
    if (newBuffer != VK_NULL_HANDLE) {
        // Add to pool
        FPooledCommandBuffer entry(newBuffer, level);
        entry.isInUse = true;
        m_pooledBuffers.push_back(entry);
        
        // Update statistics
        m_totalAllocated.fetch_add(1, std::memory_order_relaxed);
        uint32 activeCount = m_activeBuffers.fetch_add(1, std::memory_order_relaxed) + 1;
        
        // Update peak count
        uint32 currentPeak = m_peakActive.load(std::memory_order_relaxed);
        while (activeCount > currentPeak) {
            if (m_peakActive.compare_exchange_weak(currentPeak, activeCount, 
                                                   std::memory_order_relaxed)) {
                break;
            }
        }
        
        MR_LOG_DEBUG("FVulkanCommandBufferPool::AllocateCommandBuffer - Allocated new command buffer. " +
                    String("Total: ") + std::to_string(m_totalAllocated.load()) +
                    ", Active: " + std::to_string(activeCount));
    }
    
    return newBuffer;
}

VkCommandBuffer FVulkanCommandBufferPool::AllocateNewCommandBuffer(ELevel level) {
    const auto& functions = VulkanAPI::getFunctions();
    VkDevice device = m_device->getLogicalDevice();
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = (level == ELevel::Primary) ? 
                      VK_COMMAND_BUFFER_LEVEL_PRIMARY : 
                      VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkResult result = functions.vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanCommandBufferPool::AllocateNewCommandBuffer - Failed to allocate command buffer: " +
                    std::to_string(result));
        return VK_NULL_HANDLE;
    }
    
    MR_LOG_DEBUG("FVulkanCommandBufferPool::AllocateNewCommandBuffer - New " +
                String(level == ELevel::Primary ? "primary" : "secondary") +
                " command buffer allocated");
    
    return commandBuffer;
}

void FVulkanCommandBufferPool::RecycleCommandBuffer(VkCommandBuffer commandBuffer) {
    if (commandBuffer == VK_NULL_HANDLE) {
        MR_LOG_WARNING("FVulkanCommandBufferPool::RecycleCommandBuffer - Null command buffer");
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    // Find the command buffer in the pool
    for (auto& entry : m_pooledBuffers) {
        if (entry.commandBuffer == commandBuffer) {
            if (!entry.isInUse) {
                MR_LOG_WARNING("FVulkanCommandBufferPool::RecycleCommandBuffer - Command buffer already recycled");
                return;
            }
            
            // Reset the command buffer for reuse
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice device = m_device->getLogicalDevice();
            
            VkResult result = functions.vkResetCommandBuffer(commandBuffer, 0);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("FVulkanCommandBufferPool::RecycleCommandBuffer - Failed to reset command buffer: " +
                            std::to_string(result));
            }
            
            // Mark as available
            entry.isInUse = false;
            
            // Update statistics
            uint32 activeCount = m_activeBuffers.fetch_sub(1, std::memory_order_relaxed) - 1;
            
            MR_LOG_DEBUG("FVulkanCommandBufferPool::RecycleCommandBuffer - Command buffer recycled. Active: " +
                        std::to_string(activeCount));
            
            return;
        }
    }
    
    MR_LOG_WARNING("FVulkanCommandBufferPool::RecycleCommandBuffer - Command buffer not found in pool");
}

void FVulkanCommandBufferPool::ResetPool() {
    const auto& functions = VulkanAPI::getFunctions();
    VkDevice device = m_device->getLogicalDevice();
    
    // Reset the entire command pool
    // This is more efficient than resetting individual command buffers
    VkResult result = functions.vkResetCommandPool(device, m_commandPool, 0);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanCommandBufferPool::ResetPool - Failed to reset command pool: " +
                    std::to_string(result));
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    // Mark all buffers as available
    for (auto& entry : m_pooledBuffers) {
        entry.isInUse = false;
    }
    
    // Reset active count
    m_activeBuffers.store(0, std::memory_order_relaxed);
    
    MR_LOG_DEBUG("FVulkanCommandBufferPool::ResetPool - Command pool reset. " +
                std::to_string(m_pooledBuffers.size()) + " buffers available");
}

void FVulkanCommandBufferPool::TrimPool(uint64 currentFrame, uint32 framesToKeep) {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    TArray<VkCommandBuffer> buffersToFree;
    TArray<FPooledCommandBuffer> remainingBuffers;
    
    for (auto& entry : m_pooledBuffers) {
        // Keep buffers that are in use or recently used
        if (entry.isInUse || (currentFrame - entry.lastUsedFrame) < framesToKeep) {
            remainingBuffers.push_back(entry);
        } else {
            buffersToFree.push_back(entry.commandBuffer);
        }
    }
    
    // Free unused buffers
    if (!buffersToFree.empty()) {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        
        functions.vkFreeCommandBuffers(device, m_commandPool, 
                                      static_cast<uint32>(buffersToFree.size()), 
                                      buffersToFree.data());
        
        MR_LOG_DEBUG("FVulkanCommandBufferPool::TrimPool - Freed " +
                    std::to_string(buffersToFree.size()) + " unused command buffers. " +
                    std::to_string(remainingBuffers.size()) + " remaining");
    }
    
    m_pooledBuffers = std::move(remainingBuffers);
}

FVulkanCommandBufferPool::FStats FVulkanCommandBufferPool::GetStats() const {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    FStats stats;
    stats.totalAllocated = m_totalAllocated.load(std::memory_order_relaxed);
    stats.activeBuffers = m_activeBuffers.load(std::memory_order_relaxed);
    stats.pooledBuffers = static_cast<uint32>(m_pooledBuffers.size());
    stats.peakActive = m_peakActive.load(std::memory_order_relaxed);
    
    return stats;
}

// ============================================================================
// FVulkanThreadLocalCommandPool Implementation
// ============================================================================

// Static member initialization
thread_local TUniquePtr<FVulkanCommandBufferPool> FVulkanThreadLocalCommandPool::s_threadLocalPool;
TArray<FVulkanCommandBufferPool*> FVulkanThreadLocalCommandPool::s_allPools;
std::mutex FVulkanThreadLocalCommandPool::s_registryMutex;
VulkanDevice* FVulkanThreadLocalCommandPool::s_device = nullptr;
uint32 FVulkanThreadLocalCommandPool::s_queueFamilyIndex = 0;

FVulkanCommandBufferPool* FVulkanThreadLocalCommandPool::GetOrCreateForCurrentThread(
    VulkanDevice* device,
    uint32 queueFamilyIndex
) {
    // Store device and queue family for future thread-local pool creation
    if (s_device == nullptr) {
        s_device = device;
        s_queueFamilyIndex = queueFamilyIndex;
    }
    
    // Check if thread-local pool already exists
    if (!s_threadLocalPool) {
        MR_LOG_DEBUG("FVulkanThreadLocalCommandPool::GetOrCreateForCurrentThread - Creating thread-local pool for thread " +
                    std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
        
        // Create new thread-local pool
        s_threadLocalPool = MakeUnique<FVulkanCommandBufferPool>(device, queueFamilyIndex);
        
        if (!s_threadLocalPool->Initialize()) {
            MR_LOG_ERROR("FVulkanThreadLocalCommandPool::GetOrCreateForCurrentThread - Failed to initialize thread-local pool");
            s_threadLocalPool.reset();
            return nullptr;
        }
        
        // Register in global registry
        {
            std::lock_guard<std::mutex> lock(s_registryMutex);
            s_allPools.push_back(s_threadLocalPool.get());
        }
        
        MR_LOG_DEBUG("FVulkanThreadLocalCommandPool::GetOrCreateForCurrentThread - Thread-local pool created successfully");
    }
    
    return s_threadLocalPool.get();
}

void FVulkanThreadLocalCommandPool::DestroyAllPools() {
    MR_LOG_DEBUG("FVulkanThreadLocalCommandPool::DestroyAllPools - Destroying all thread-local pools");
    
    std::lock_guard<std::mutex> lock(s_registryMutex);
    
    // Clear registry (pools will be destroyed by thread_local destructors)
    s_allPools.clear();
    
    MR_LOG_DEBUG("FVulkanThreadLocalCommandPool::DestroyAllPools - All pools destroyed");
}

void FVulkanThreadLocalCommandPool::ResetAllPools() {
    std::lock_guard<std::mutex> lock(s_registryMutex);
    
    for (auto* pool : s_allPools) {
        if (pool) {
            pool->ResetPool();
        }
    }
    
    MR_LOG_DEBUG("FVulkanThreadLocalCommandPool::ResetAllPools - All pools reset");
}

void FVulkanThreadLocalCommandPool::TrimAllPools(uint64 currentFrame, uint32 framesToKeep) {
    std::lock_guard<std::mutex> lock(s_registryMutex);
    
    for (auto* pool : s_allPools) {
        if (pool) {
            pool->TrimPool(currentFrame, framesToKeep);
        }
    }
    
    MR_LOG_DEBUG("FVulkanThreadLocalCommandPool::TrimAllPools - All pools trimmed");
}

// ============================================================================
// FVulkanSecondaryCommandBuffer Implementation
// ============================================================================

FVulkanSecondaryCommandBuffer::FVulkanSecondaryCommandBuffer(VulkanDevice* device, VkCommandBuffer commandBuffer)
    : m_device(device)
    , m_commandBuffer(commandBuffer)
    , m_hasBegun(false)
{
    MR_LOG_DEBUG("FVulkanSecondaryCommandBuffer::FVulkanSecondaryCommandBuffer - Created secondary command buffer");
}

FVulkanSecondaryCommandBuffer::~FVulkanSecondaryCommandBuffer() {
    // Command buffer is owned by the pool, don't free it here
    MR_LOG_DEBUG("FVulkanSecondaryCommandBuffer::~FVulkanSecondaryCommandBuffer - Destroyed secondary command buffer");
}

void FVulkanSecondaryCommandBuffer::Begin(VkRenderPass renderPass, uint32 subpass, VkFramebuffer framebuffer) {
    const auto& functions = VulkanAPI::getFunctions();
    
    // Setup inheritance info for secondary command buffer
    VkCommandBufferInheritanceInfo inheritanceInfo{};
    inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritanceInfo.renderPass = renderPass;
    inheritanceInfo.subpass = subpass;
    inheritanceInfo.framebuffer = framebuffer;
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    beginInfo.pInheritanceInfo = &inheritanceInfo;
    
    VkResult result = functions.vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanSecondaryCommandBuffer::Begin - Failed to begin command buffer: " +
                    std::to_string(result));
        return;
    }
    
    m_hasBegun = true;
    
    MR_LOG_DEBUG("FVulkanSecondaryCommandBuffer::Begin - Secondary command buffer recording started");
}

void FVulkanSecondaryCommandBuffer::End() {
    if (!m_hasBegun) {
        MR_LOG_WARNING("FVulkanSecondaryCommandBuffer::End - Command buffer not begun");
        return;
    }
    
    const auto& functions = VulkanAPI::getFunctions();
    
    VkResult result = functions.vkEndCommandBuffer(m_commandBuffer);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanSecondaryCommandBuffer::End - Failed to end command buffer: " +
                    std::to_string(result));
        return;
    }
    
    m_hasBegun = false;
    
    MR_LOG_DEBUG("FVulkanSecondaryCommandBuffer::End - Secondary command buffer recording ended");
}

} // namespace MonsterRender::RHI::Vulkan
