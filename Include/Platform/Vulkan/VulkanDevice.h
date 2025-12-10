#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIDevice.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "RHI/RHI.h"

#include <vector>

namespace MonsterRender::RHI {
    class FVulkanMemoryManager;
}

namespace MonsterRender::RHI::Vulkan {
    
    // Forward declarations
    class VulkanCommandList;
    class VulkanPipelineState;
    class VulkanPipelineCache;
    class VulkanDescriptorSetAllocator;
    class FVulkanDescriptorSetLayoutCache;
    class FVulkanDescriptorSetCache;
    class FVulkanCommandBufferManager;
    class FVulkanCommandListContext;
    class FVulkanRHICommandListImmediate;
    class FVulkanRenderPassCache;
    class FVulkanFramebufferCache;
    
    /**
     * Queue family information
     */
    struct QueueFamily {
        uint32 familyIndex = VK_QUEUE_FAMILY_IGNORED;
        uint32 queueCount = 0;
        VkQueueFlags flags = 0;
        bool supportsPresentToSurface = false;
    };
    
    /**
     * Vulkan device implementation
     */
    class VulkanDevice : public IRHIDevice {
    public:
        VulkanDevice();
        virtual ~VulkanDevice();
        
        /**
         * Initialize the Vulkan device
         */
        bool initialize(const RHICreateInfo& createInfo);
        
        /**
         * Shutdown the device
         */
        void shutdown();
        
        // IRHIDevice interface
        const RHIDeviceCapabilities& getCapabilities() const override;
        ERHIBackend getBackendType() const override { return ERHIBackend::Vulkan; }
        
        TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc) override;
        TSharedPtr<IRHITexture> createTexture(const TextureDesc& desc) override;
        TSharedPtr<IRHIVertexShader> createVertexShader(TSpan<const uint8> bytecode) override;
        TSharedPtr<IRHIPixelShader> createPixelShader(TSpan<const uint8> bytecode) override;
        TSharedPtr<IRHIPipelineState> createPipelineState(const PipelineStateDesc& desc) override;
        TSharedPtr<IRHISampler> createSampler(const SamplerDesc& desc) override;
        
        // UE5-style vertex and index buffer creation
        TSharedPtr<FRHIVertexBuffer> CreateVertexBuffer(
            uint32 Size,
            EBufferUsageFlags Usage,
            FRHIResourceCreateInfo& CreateInfo) override;
        
        TSharedPtr<FRHIIndexBuffer> CreateIndexBuffer(
            uint32 Stride,
            uint32 Size,
            EBufferUsageFlags Usage,
            FRHIResourceCreateInfo& CreateInfo) override;
        
        TSharedPtr<IRHICommandList> createCommandList() override;
        void executeCommandLists(TSpan<TSharedPtr<IRHICommandList>> commandLists) override;
        
        // Vulkan-specific accessors
        VkDevice getLogicalDevice() const { return m_device; }
        IRHICommandList* getImmediateCommandList() override;
        
        void waitForIdle() override;
        void present() override;
        
        void getMemoryStats(uint64& usedBytes, uint64& availableBytes) override;
        void collectGarbage() override;
        
        void setDebugName(const String& name) override;
        void setValidationEnabled(bool enabled) override;
        
        // SwapChain and RHI type
        TSharedPtr<IRHISwapChain> createSwapChain(const SwapChainDesc& desc) override;
        ERHIBackend getRHIBackend() const override { return ERHIBackend::Vulkan; }
        EPixelFormat getSwapChainFormat() const override;
        EPixelFormat getDepthFormat() const override;
        
        // Vulkan-specific getters
        VkInstance getInstance() const { return m_instance; }
        VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
        VkDevice getDevice() const { return m_device; }
        VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
        VkQueue getPresentQueue() const { return m_presentQueue; }
        VkCommandPool getCommandPool() const { return m_commandPool; }
        VkSurfaceKHR getSurface() const { return m_surface; }
        VkSwapchainKHR getSwapchain() const { return m_swapchain; }
        uint32 getGraphicsQueueFamilyIndex() const { return m_graphicsQueueFamily.familyIndex; }
        
        // UE5-style per-frame command buffer management
        FVulkanCommandBufferManager* getCommandBufferManager() const { return m_commandBufferManager.get(); }
        FVulkanCommandListContext* getCommandListContext() const { return m_commandListContext.get(); }
        
        const QueueFamily& getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
        const QueueFamily& getPresentQueueFamily() const { return m_presentQueueFamily; }
        const VkPhysicalDeviceMemoryProperties& getMemoryProperties() const { return m_memoryProperties; }
        
        VulkanDescriptorSetAllocator* getDescriptorSetAllocator() const { return m_descriptorSetAllocator.get(); }
        
        // Descriptor set layout and cache (UE5-style)
        FVulkanDescriptorSetLayoutCache* GetDescriptorSetLayoutCache() const { return m_descriptorSetLayoutCache.get(); }
        FVulkanDescriptorSetCache* GetDescriptorSetCache() const { return m_descriptorSetCache.get(); }
        
        // Memory manager (UE5-style)
        FVulkanMemoryManager* getMemoryManager() const { return m_memoryManager.get(); }
        FVulkanMemoryManager* GetMemoryManager() const { return m_memoryManager.get(); } // UE5 naming style
        
        // Render pass and framebuffer accessors
        VkRenderPass getRenderPass() const { return m_renderPass; }
        
        // Depth buffer accessors (UE5-style)
        VkFormat getVulkanDepthFormat() const { return m_depthFormat; }
        VkImageView getDepthImageView() const { return m_depthImageView; }
        bool hasDepthBuffer() const { return m_depthImage != VK_NULL_HANDLE; }
        
        // Render target caches (UE5-style RTT support)
        FVulkanRenderPassCache* GetRenderPassCache() const { return m_renderPassCache.get(); }
        FVulkanFramebufferCache* GetFramebufferCache() const { return m_framebufferCache.get(); }
        
        // Frame management for swapchain image acquisition
        uint32 getCurrentFrame() const { return m_currentFrame; }
        uint32 getCurrentImageIndex() const { return m_currentImageIndex; }
        void setCurrentImageIndex(uint32 index) { m_currentImageIndex = index; }
        VkSemaphore getImageAvailableSemaphore(uint32 frame) const { 
            return frame < m_imageAvailableSemaphores.size() ? m_imageAvailableSemaphores[frame] : VK_NULL_HANDLE; 
        }
        VkSemaphore getRenderFinishedSemaphore(uint32 frame) const {
            return frame < m_renderFinishedSemaphores.size() ? m_renderFinishedSemaphores[frame] : VK_NULL_HANDLE;
        }
        VkFramebuffer getCurrentFramebuffer() const;
        const VkExtent2D& getSwapchainExtent() const { return m_swapchainExtent; }
        VkFormat getSwapchainFormat() const { return m_swapchainImageFormat; }
        
    private:
        // Initialization helper functions
        bool createInstance(const RHICreateInfo& createInfo);
        bool setupDebugMessenger();
        bool createSurface(const RHICreateInfo& createInfo);
        bool selectPhysicalDevice();
        bool createLogicalDevice();
        bool createSwapchain(const RHICreateInfo& createInfo);
        bool createRenderPass();
        bool createFramebuffers();
        bool createCommandPool();
        bool createSyncObjects();
        
        // Depth buffer creation (UE5-style)
        bool createDepthResources();
        void destroyDepthResources();
        VkFormat findDepthFormat();
        VkFormat findSupportedFormat(const TArray<VkFormat>& candidates, 
                                     VkImageTiling tiling, 
                                     VkFormatFeatureFlags features);
        bool hasStencilComponent(VkFormat format);
        
        // Helper functions
        bool checkValidationLayerSupport();
        std::vector<const char*> getRequiredExtensions(bool enableValidation);
        bool isDeviceSuitable(VkPhysicalDevice device);
        QueueFamily findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface, VkQueueFlags flags);
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        void queryCapabilities();
        
        // Vulkan objects
        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        
        // Queues
        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        VkQueue m_presentQueue = VK_NULL_HANDLE;
        QueueFamily m_graphicsQueueFamily;
        QueueFamily m_presentQueueFamily;
        
        // Swapchain - use std::vector for Vulkan API compatibility
        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> m_swapchainImages;
        std::vector<VkImageView> m_swapchainImageViews;
        VkFormat m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D m_swapchainExtent = {0, 0};
        
        // Render pass and framebuffers
        VkRenderPass m_renderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_swapchainFramebuffers;
        
        // Depth buffer resources (UE5-style)
        VkImage m_depthImage = VK_NULL_HANDLE;
        VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
        VkImageView m_depthImageView = VK_NULL_HANDLE;
        VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
        
        // Command handling (Legacy, kept for backward compatibility if needed)
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        
        // UE5-style Immediate Command List (replaces legacy VulkanCommandList)
        TUniquePtr<FVulkanRHICommandListImmediate> m_immediateCommandList;
        
        // Per-frame command buffer management (UE5 pattern)
        TUniquePtr<FVulkanCommandBufferManager> m_commandBufferManager;
        TUniquePtr<FVulkanCommandListContext> m_commandListContext;
        
        // Pipeline cache
        TUniquePtr<VulkanPipelineCache> m_pipelineCache;
        
        // Descriptor set allocator
        TUniquePtr<VulkanDescriptorSetAllocator> m_descriptorSetAllocator;
        
        // Descriptor set layout cache (UE5-style)
        TUniquePtr<FVulkanDescriptorSetLayoutCache> m_descriptorSetLayoutCache;
        
        // Descriptor set cache for frame-local reuse (UE5-style)
        TUniquePtr<FVulkanDescriptorSetCache> m_descriptorSetCache;
        
        // Render target caches for RTT support (UE5-style)
        TUniquePtr<FVulkanRenderPassCache> m_renderPassCache;
        TUniquePtr<FVulkanFramebufferCache> m_framebufferCache;
        
        // Memory manager (UE5-style sub-allocation)
        TUniquePtr<FVulkanMemoryManager> m_memoryManager;
        
        // Synchronization
        TArray<VkSemaphore> m_imageAvailableSemaphores;
        TArray<VkSemaphore> m_renderFinishedSemaphores;
        TArray<VkFence> m_inFlightFences;
        
        // Memory properties
        VkPhysicalDeviceMemoryProperties m_memoryProperties{};
        VkPhysicalDeviceProperties m_deviceProperties{};
        VkPhysicalDeviceFeatures m_deviceFeatures{};
        
        // Settings
        bool m_validationEnabled = false;
        bool m_debugMarkersEnabled = true;
        uint32 m_currentFrame = 0;
        uint32 m_currentImageIndex = 0;
        
        // Constants
        static constexpr uint32 MAX_FRAMES_IN_FLIGHT = 2;
    };
}
