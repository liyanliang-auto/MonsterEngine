#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIDevice.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "RHI/RHI.h"

namespace MonsterRender::RHI::Vulkan {
    
    // Forward declarations
    class VulkanCommandList;
    
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
        
        TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc) override;
        TSharedPtr<IRHITexture> createTexture(const TextureDesc& desc) override;
        TSharedPtr<IRHIVertexShader> createVertexShader(TSpan<const uint8> bytecode) override;
        TSharedPtr<IRHIPixelShader> createPixelShader(TSpan<const uint8> bytecode) override;
        TSharedPtr<IRHIPipelineState> createPipelineState(const PipelineStateDesc& desc) override;
        
        TSharedPtr<IRHICommandList> createCommandList() override;
        void executeCommandLists(TSpan<TSharedPtr<IRHICommandList>> commandLists) override;
        IRHICommandList* getImmediateCommandList() override;
        
        void waitForIdle() override;
        void present() override;
        
        void getMemoryStats(uint64& usedBytes, uint64& availableBytes) override;
        void collectGarbage() override;
        
        void setDebugName(const String& name) override;
        void setValidationEnabled(bool enabled) override;
        
        // Vulkan-specific getters
        VkInstance getInstance() const { return m_instance; }
        VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
        VkDevice getDevice() const { return m_device; }
        VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
        VkQueue getPresentQueue() const { return m_presentQueue; }
        VkCommandPool getCommandPool() const { return m_commandPool; }
        VkSurfaceKHR getSurface() const { return m_surface; }
        VkSwapchainKHR getSwapchain() const { return m_swapchain; }
        
        const QueueFamily& getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
        const QueueFamily& getPresentQueueFamily() const { return m_presentQueueFamily; }
        const VkPhysicalDeviceMemoryProperties& getMemoryProperties() const { return m_memoryProperties; }
        
    private:
        // Initialization helper functions
        bool createInstance(const RHICreateInfo& createInfo);
        bool setupDebugMessenger();
        bool createSurface(const RHICreateInfo& createInfo);
        bool selectPhysicalDevice();
        bool createLogicalDevice();
        bool createSwapchain(const RHICreateInfo& createInfo);
        bool createCommandPool();
        bool createSyncObjects();
        
        // Helper functions
        bool checkValidationLayerSupport();
        TArray<const char*> getRequiredExtensions(bool enableValidation);
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
        
        // Swapchain
        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        TArray<VkImage> m_swapchainImages;
        TArray<VkImageView> m_swapchainImageViews;
        VkFormat m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D m_swapchainExtent = {0, 0};
        
        // Command handling
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        TUniquePtr<VulkanCommandList> m_immediateCommandList;
        
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
        
        // Required extensions and layers
        static const TArray<const char*> s_deviceExtensions;
        static const TArray<const char*> s_validationLayers;
        
        // Constants
        static constexpr uint32 MAX_FRAMES_IN_FLIGHT = 2;
    };
}
