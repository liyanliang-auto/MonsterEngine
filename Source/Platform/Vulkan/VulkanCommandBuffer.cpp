#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {
    
    // ========================================================================
    // FVulkanCmdBuffer Implementation
    // ========================================================================
    
    FVulkanCmdBuffer::FVulkanCmdBuffer(VulkanDevice* device, FVulkanCommandBufferManager* manager)
        : m_device(device)
        , m_manager(manager)
        , m_commandBuffer(VK_NULL_HANDLE)
        , m_fence(VK_NULL_HANDLE)
        , m_state(EState::NotAllocated)
        , m_insideRenderPass(false)
        , m_fenceSignaledCounter(0)
        , m_submittedCounter(0) {
    }
    
    FVulkanCmdBuffer::~FVulkanCmdBuffer() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        
        // Free command buffer
        if (m_commandBuffer != VK_NULL_HANDLE && m_manager) {
            VkCommandPool commandPool = m_manager->getCommandPool();
            if (commandPool != VK_NULL_HANDLE) {
                functions.vkFreeCommandBuffers(device, commandPool, 1, &m_commandBuffer);
            }
            m_commandBuffer = VK_NULL_HANDLE;
        }
        
        // Destroy fence
        if (m_fence != VK_NULL_HANDLE) {
            functions.vkDestroyFence(device, m_fence, nullptr);
            m_fence = VK_NULL_HANDLE;
        }
    }
    
    bool FVulkanCmdBuffer::initialize() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        VkCommandPool commandPool = m_manager->getCommandPool();
        
        // Allocate command buffer
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        
        VkResult result = functions.vkAllocateCommandBuffers(device, &allocInfo, &m_commandBuffer);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to allocate command buffer: " + std::to_string(result));
            return false;
        }
        
        // Create fence for this command buffer
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start in signaled state
        
        result = functions.vkCreateFence(device, &fenceInfo, nullptr, &m_fence);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create fence for command buffer: " + std::to_string(result));
            return false;
        }
        
        m_state = EState::ReadyForBegin;
        
        MR_LOG_DEBUG("FVulkanCmdBuffer initialized successfully");
        return true;
    }
    
    void FVulkanCmdBuffer::begin() {
        if (m_state != EState::ReadyForBegin) {
            MR_LOG_ERROR("Command buffer not in ReadyForBegin state");
            return;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Begin command buffer recording
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = nullptr;
        
        VkResult result = functions.vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to begin command buffer: " + std::to_string(result));
            return;
        }
        
        m_state = EState::Recording;
        m_insideRenderPass = false;
        
        MR_LOG_DEBUG("Command buffer began recording");
    }
    
    void FVulkanCmdBuffer::end() {
        if (m_state != EState::Recording) {
            MR_LOG_ERROR("Command buffer not in Recording state");
            return;
        }
        
        // End render pass if still active
        if (m_insideRenderPass) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdEndRenderPass(m_commandBuffer);
            m_insideRenderPass = false;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        VkResult result = functions.vkEndCommandBuffer(m_commandBuffer);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to end command buffer: " + std::to_string(result));
            return;
        }
        
        m_state = EState::Ended;
        
        MR_LOG_DEBUG("Command buffer ended recording");
    }
    
    void FVulkanCmdBuffer::refreshFenceStatus() {
        if (m_state != EState::Submitted) {
            return;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        
        // Check fence status without blocking
        VkResult result = functions.vkGetFenceStatus(device, m_fence);
        if (result == VK_SUCCESS) {
            // Fence signaled - GPU finished execution
            m_fenceSignaledCounter++;
            m_state = EState::ReadyForBegin;
            MR_LOG_DEBUG("Command buffer fence signaled, ready for reuse");
        }
    }
    
    // ========================================================================
    // FVulkanCommandBufferManager Implementation
    // ========================================================================
    
    FVulkanCommandBufferManager::FVulkanCommandBufferManager(VulkanDevice* device)
        : m_device(device)
        , m_commandPool(VK_NULL_HANDLE)
        , m_currentBufferIndex(0)
        , m_activeCmdBuffer(nullptr) {
    }
    
    FVulkanCommandBufferManager::~FVulkanCommandBufferManager() {
        // Destroy command buffers first (they need the command pool)
        m_cmdBuffers.clear();
        
        // Destroy command pool
        if (m_commandPool != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice device = m_device->getLogicalDevice();
            functions.vkDestroyCommandPool(device, m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }
    }
    
    bool FVulkanCommandBufferManager::initialize() {
        if (!createCommandPool()) {
            MR_LOG_ERROR("Failed to create command pool");
            return false;
        }
        
        if (!createCommandBuffers()) {
            MR_LOG_ERROR("Failed to create command buffers");
            return false;
        }
        
        MR_LOG_INFO("FVulkanCommandBufferManager initialized with " + 
                   std::to_string(NUM_FRAMES_IN_FLIGHT) + " command buffers");
        return true;
    }
    
    bool FVulkanCommandBufferManager::createCommandPool() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        
        // Get graphics queue family index
        uint32 queueFamilyIndex = m_device->getGraphicsQueueFamilyIndex();
        
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Allow individual reset
        
        VkResult result = functions.vkCreateCommandPool(device, &poolInfo, nullptr, &m_commandPool);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create command pool: " + std::to_string(result));
            return false;
        }
        
        return true;
    }
    
    bool FVulkanCommandBufferManager::createCommandBuffers() {
        m_cmdBuffers.reserve(NUM_FRAMES_IN_FLIGHT);
        
        for (uint32 i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
            auto cmdBuffer = MakeUnique<FVulkanCmdBuffer>(m_device, this);
            if (!cmdBuffer->initialize()) {
                MR_LOG_ERROR("Failed to initialize command buffer " + std::to_string(i));
                return false;
            }
            m_cmdBuffers.push_back(std::move(cmdBuffer));
        }
        
        // Set first buffer as active
        m_activeCmdBuffer = m_cmdBuffers[0].get();
        
        return true;
    }
    
    FVulkanCmdBuffer* FVulkanCommandBufferManager::getActiveCmdBuffer() {
        return m_activeCmdBuffer;
    }
    
    void FVulkanCommandBufferManager::submitActiveCmdBuffer(
        VkSemaphore* waitSemaphores, uint32 waitSemaphoreCount,
        VkSemaphore* signalSemaphores, uint32 signalSemaphoreCount) {
        
        if (!m_activeCmdBuffer || !m_activeCmdBuffer->hasEnded()) {
            MR_LOG_ERROR("Cannot submit command buffer - not ended");
            return;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        VkQueue queue = getQueue();
        
        // Reset fence before submission
        VkFence fence = m_activeCmdBuffer->getFence();
        functions.vkResetFences(m_device->getLogicalDevice(), 1, &fence);
        
        // Prepare submit info
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        
        // Wait semaphores
        TArray<VkPipelineStageFlags> waitStages;
        if (waitSemaphoreCount > 0) {
            waitStages.resize(waitSemaphoreCount, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            submitInfo.waitSemaphoreCount = waitSemaphoreCount;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages.data();
        }
        
        // Command buffers
        VkCommandBuffer cmdBuffer = m_activeCmdBuffer->getHandle();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;
        
        // Signal semaphores
        if (signalSemaphoreCount > 0) {
            submitInfo.signalSemaphoreCount = signalSemaphoreCount;
            submitInfo.pSignalSemaphores = signalSemaphores;
        }
        
        // Submit to queue with fence
        VkResult result = functions.vkQueueSubmit(queue, 1, &submitInfo, fence);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to submit command buffer: " + std::to_string(result));
            return;
        }
        
        m_activeCmdBuffer->markSubmitted();
        
        MR_LOG_DEBUG("Command buffer submitted to GPU");
    }
    
    void FVulkanCommandBufferManager::waitForCmdBuffer(FVulkanCmdBuffer* cmdBuffer, float timeInSecondsToWait) {
        if (!cmdBuffer) {
            return;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        
        uint64 timeoutNs = static_cast<uint64>(timeInSecondsToWait * 1000000000.0);
        VkFence fence = cmdBuffer->getFence();
        
        VkResult result = functions.vkWaitForFences(device, 1, &fence, VK_TRUE, timeoutNs);
        if (result == VK_TIMEOUT) {
            MR_LOG_WARNING("Wait for command buffer timed out");
        } else if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Wait for command buffer failed: " + std::to_string(result));
        }
    }
    
    void FVulkanCommandBufferManager::prepareForNewActiveCommandBuffer() {
        // Advance to next frame in ring buffer
        m_currentBufferIndex = (m_currentBufferIndex + 1) % NUM_FRAMES_IN_FLIGHT;
        m_activeCmdBuffer = m_cmdBuffers[m_currentBufferIndex].get();
        
        // Wait for this command buffer to finish if still executing
        if (m_activeCmdBuffer->isSubmitted()) {
            waitForCmdBuffer(m_activeCmdBuffer, 1.0f);
        }
        
        // Refresh fence status
        m_activeCmdBuffer->refreshFenceStatus();
        
        // Reset command buffer for reuse
        const auto& functions = VulkanAPI::getFunctions();
        VkCommandBuffer cmdBuffer = m_activeCmdBuffer->getHandle();
        functions.vkResetCommandBuffer(cmdBuffer, 0);
        
        MR_LOG_DEBUG("Prepared command buffer for frame " + std::to_string(m_currentBufferIndex));
    }
    
    VkQueue FVulkanCommandBufferManager::getQueue() const {
        return m_device->getGraphicsQueue();
    }
}
