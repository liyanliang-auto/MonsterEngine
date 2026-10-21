#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIDevice.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "RHI/RHI.h"
#include "Profiling/MonsterProfiler.h"

#include <vector>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace MonsterRender::RHI {
    class FVulkanMemoryManager;
}

namespace MonsterRender::RHI::Vulkan {
    
    // Forward declarations
    class VulkanPipelineState;
    class VulkanPipelineCache;
    class VulkanDescriptorPoolManager;
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
	VulkanDevice（继承 IRHIDevice）
		├── VkInstance / VkPhysicalDevice / VkDevice
		├── VkQueue（Graphics / Present）
		├── VkSurface / VkSwapchain
		│
		├── VulkanPipelineCache          // 管线状态对象缓存（哈希查找）
		│     └── TMap<uint64, TSharedPtr<VulkanPipelineState>>
		│
		├── FVulkanMemoryManager         // 显存池（大块分配 + 子分配）
		│     ├── VkDeviceMemory 大块（256MB / 块）
		│     └── 子分配器（按类型分池）
		│
		├── FVulkanCommandBufferManager  // 命令缓冲区管理（环形复用）
		│
		├── FVulkanCommandListContext    // 帧同步上下文
		│     ├── imageAvailableSemaphore[MAX_FRAMES_IN_FLIGHT]
		│     ├── renderFinishedSemaphore[MAX_FRAMES_IN_FLIGHT]
			│     └── inFlightFence[MAX_FRAMES_IN_FLIGHT]
				│
					├── FVulkanPendingState          // 延迟绑定状态机
					│     ├── 待提交 Pipeline / Viewport / Scissor
					│     ├── 待提交 VertexBuffer / IndexBuffer
					│     └── 待提交 DescriptorSet
					│
					├── VulkanRenderPassCache        // RenderPass 缓存（哈希查找）
					├── VulkanFramebufferCache       // Framebuffer 缓存
					├── FVulkanDescriptorSetLayoutCache
					├── FVulkanDescriptorSetCache
					└── 延迟销毁队列
					├── m_deferredBufferDestructions[]
					└── m_deferredImageDestructions[]

     */


    /**
     * Vulkan device implementation
     */
    class VulkanDevice : public IRHIDevice {
    public:
        /** Owner-thread diagnostic identities; zero means unavailable. */
        void SetProfilingContext(const MonsterEngine::Profiling::Context& value) { m_profilingContext = value; }
        /** Copy explicit owner context for stage observations. */
        MonsterEngine::Profiling::Context GetProfilingContext() const { return m_profilingContext; }
        // Constants
        static constexpr uint32 MAX_FRAMES_IN_FLIGHT = 2;
        
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
        bool getBufferAllocationRequirements(const BufferDesc& desc, uint64& bytes, uint64& alignment) const override;
        TSharedPtr<IRHITexture> createTexture(const TextureDesc& desc) override;
        bool getTextureAllocationRequirements(const TextureDesc& desc, uint64& bytes, uint64& alignment) const override;
        TSharedPtr<IRHIVertexShader> createVertexShader(TSpan<const uint8> bytecode) override;
        TSharedPtr<IRHIPixelShader> createPixelShader(TSpan<const uint8> bytecode) override;
        TSharedPtr<IRHIComputeShader> createComputeShader(TSpan<const uint8> bytecode) override;
        TSharedPtr<IRHIPipelineState> createPipelineState(const PipelineStateDesc& desc) override;
        TSharedPtr<IRHIPipelineState> createComputePipelineState(const ComputePipelineStateDesc& desc) override;
        TSharedPtr<IRHISampler> createSampler(const SamplerDesc& desc) override;
        
        // Descriptor set management (Multi-descriptor set support)
        TSharedPtr<IRHIDescriptorSetLayout> createDescriptorSetLayout(
            const FDescriptorSetLayoutDesc& desc) override;
        
        TSharedPtr<IRHIPipelineLayout> createPipelineLayout(
            const FPipelineLayoutDesc& desc) override;
        
        TSharedPtr<IRHIDescriptorSet> allocateDescriptorSet(
            TSharedPtr<IRHIDescriptorSetLayout> layout) override;
        
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
        
        // Vulkan-specific accessors
        VkDevice getLogicalDevice() const { return m_device; }
        IRHICommandList* getImmediateCommandList() override;
        
        void waitForIdle() override;
        FGPUCompletionToken getLastSubmissionToken() const override;
        bool isSubmissionComplete(FGPUCompletionToken token) const override;
        bool supportsSubmissionCompletion() const override { return m_device != VK_NULL_HANDLE; }
        bool supportsDispatchIndirect() const override { return m_device != VK_NULL_HANDLE; }
        bool supportsGPUTimestamps() const override;
        std::vector<FRHIGPUTimestampSample> pollGPUTimestamps() override;
        /** Called only after successful graphics queue submission. */
        void trackSubmission(VkCommandBuffer commandBuffer, VkFence fence);
        /** Must be called before resetting or destroying any submission fence. */
        bool prepareSubmissionFenceReset(VkFence fence);
        /** Records query commands owned by this native command buffer. */
        void recordGPUTimestamp(VkCommandBuffer commandBuffer, uint64_t frame,
                                const std::string& label, bool begin);
        /** Discard query resources for an unsubmitted recording before reset. */
        void discardUnsubmittedTimestamps(VkCommandBuffer commandBuffer);
        void present() override;
        FFrameSubmitResult beginFrame(uint64 timeoutNs = 1000000) override;
        FFrameSubmitResult presentFrame() override;
        FFrameSubmitResult cancelFrame() override;
        /** Context acquisition, retained across a bounded image-fence timeout. */
        FFrameSubmitResult acquireFrame(uint64 timeoutNs = 1000000);
        const FFrameSubmitResult& getLastFrameResult() const { return m_frameResult; }
        static EFrameResult ClassifyFrameResult(VkResult result);
        void RecordSubmitFailure(VkResult result, VkFence fence);
        VkResult getLastSubmitResult() const { return m_lastSubmitResult; }
        const VkPhysicalDeviceFeatures& getEnabledFeatures() const { return m_enabledFeatures; }
        const VkPhysicalDeviceFeatures& getRequestedFeatures() const { return m_requestedFeatures; }
        const VkPhysicalDeviceFeatures& getSupportedFeatures() const { return m_deviceFeatures; }
        bool IsPresentMaintenanceSupported() const { return m_presentMaintenanceSupported; }
        bool IsPresentMaintenanceEnabled() const { return m_presentMaintenanceEnabled; }
        /** False means teardown uses the documented unextended Vulkan idle assumption. */
        bool HasPrecisePresentCompletion() const { return m_presentMaintenanceEnabled; }
        bool AttachExternalSurface(void* nativeWindow, uint64 generation, uint32 width, uint32 height, String& error);
        FFrameSubmitResult DetachExternalSurface(uint64 generation);
        /** Optional platform account. Callback reserves a documented conservative
         * WSI envelope, NOT measured opaque driver backing; null rejects admission.
         * Set before attaching. Shared lease retires after presentation resources. */
        void SetPresentationBudget(std::function<std::shared_ptr<void>(uint64)> reserve) { m_presentationBudget = std::move(reserve); }
        uint64 GetPresentationReservationEstimate() const { return m_presentationReservationEstimate; }
        
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
        /** Recreate presentation with uncapped IMMEDIATE preferred when VSync is disabled. */
        bool ConfigurePresentationVSync(bool enabled);
        /** Mode recorded by the last configured/recreated swapchain. */
        VkPresentModeKHR GetConfiguredPresentMode() const { return m_configuredPresentMode; }
        /** Teardown-only GPU-idle proof followed by draining retired backing allocations. */
        bool DrainCompletedDestructions();
        uint32 getGraphicsQueueFamilyIndex() const { return m_graphicsQueueFamily.familyIndex; }
        
        // UE5-style per-frame command buffer management
        FVulkanCommandBufferManager* getCommandBufferManager() const { return m_commandBufferManager.get(); }
        FVulkanCommandListContext* getCommandListContext() const { return m_commandListContext.get(); }
        
        const QueueFamily& getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
        const QueueFamily& getPresentQueueFamily() const { return m_presentQueueFamily; }
        const VkPhysicalDeviceMemoryProperties& getMemoryProperties() const { return m_memoryProperties; }
        
        VulkanDescriptorPoolManager* getDescriptorPoolManager() const { return m_descriptorPoolManager.get(); }
        
        // Descriptor set layout and cache (UE5-style)
        FVulkanDescriptorSetLayoutCache* GetDescriptorSetLayoutCache() const { return m_descriptorSetLayoutCache.get(); }
        FVulkanDescriptorSetCache* GetDescriptorSetCache() const { return m_descriptorSetCache.get(); }
        
        // Memory manager (UE5-style)
        FVulkanMemoryManager* getMemoryManager() const { return m_memoryManager.get(); }
        FVulkanMemoryManager* GetMemoryManager() const { return m_memoryManager.get(); } // UE5 naming style
        
        // Texture layout tracking for per-command-buffer transitions
        void registerTextureForLayoutTransition(VkImage image, uint32 mipLevels, uint32 arrayLayers);
        void executeTextureLayoutTransitions(VkCommandBuffer cmdBuffer);
        void clearTransitionedTexturesForCmdBuffer(VkCommandBuffer cmdBuffer);
        
        // Deferred resource destruction (UE5-style FDeferredDeletionQueue)
        void deferBufferDestruction(VkBuffer buffer, VkDeviceMemory memory);
        void deferImageDestruction(VkImage image, VkImageView imageView, VkDeviceMemory memory);
        void processDeferredDestructions();
        
        // Render pass and framebuffer accessors
        VkRenderPass getRenderPass() const { return m_renderPass; }
        VkRenderPass getRTTRenderPass() const { return m_rttRenderPass; }
        
        // Depth buffer accessors (UE5-style)
        VkFormat getVulkanDepthFormat() const { return m_depthFormat; }
        VkImageView getDepthImageView() const { return m_depthImageView; }
        bool hasDepthBuffer() const { return m_depthImage != VK_NULL_HANDLE; }
        
        // MSAA accessors
        VkSampleCountFlagBits getMSAASampleCount() const { return m_msaaSampleCount; }
        bool isMSAAEnabled() const { return m_msaaSampleCount != VK_SAMPLE_COUNT_1_BIT; }
        
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
        // Get per-image render finished semaphore (indexed by image index, not frame)
        VkSemaphore getRenderFinishedSemaphore(uint32 imageIndex) const {
            return imageIndex < m_perImageRenderFinishedSemaphores.size() ? m_perImageRenderFinishedSemaphores[imageIndex] : VK_NULL_HANDLE;
        }
        VkFence getInFlightFence(uint32 frame) const {
            return frame < m_inFlightFences.size() ? m_inFlightFences[frame] : VK_NULL_HANDLE;
        }
        
        // Per-image fence tracking for better parallelism
        VkFence getImageInFlightFence(uint32 imageIndex) const {
            return imageIndex < m_imagesInFlight.size() ? m_imagesInFlight[imageIndex] : VK_NULL_HANDLE;
        }
        void setImageInFlightFence(uint32 imageIndex, VkFence fence) {
            if (imageIndex < m_imagesInFlight.size()) {
                m_imagesInFlight[imageIndex] = fence;
            }
        }
        
        VkFramebuffer getCurrentFramebuffer() const;
        VkImageView getCurrentSwapchainImageView() const;
        /** Current resolved presentation image, available for bounded GPU capture. */
        VkImage getCurrentSwapchainImage() const {
            return m_currentImageIndex < m_swapchainImages.size() ? m_swapchainImages[m_currentImageIndex] : VK_NULL_HANDLE;
        }
        bool supportsSwapchainReadback() const { return m_swapchainReadbackSupported; }
        const VkExtent2D& getSwapchainExtent() const { return m_swapchainExtent; }
        VkFormat getSwapchainFormat() const { return m_swapchainImageFormat; }
        /** Actual WSI color space; meaningful while a swapchain is attached. */
        VkColorSpaceKHR getSwapchainColorSpace() const { return m_swapchainColorSpace; }
        
        /**
         * Recreate swapchain when window is resized
         * Reference: UE5 FVulkanDynamicRHI::RecreateSwapChain
         */
        bool recreateSwapchain(uint32 newWidth, uint32 newHeight);
        
    private:
        MonsterEngine::Profiling::Context m_profilingContext{};
        VkResult queueCurrentPresent();
        VkResult waitPresentationCompletion(uint64 timeoutNs);
        void destroyPresentationResources();
        bool createImageSynchronization();
        FFrameSubmitResult m_frameResult{};
        VkResult m_lastSubmitResult = VK_NOT_READY;
        bool m_frameAcquired = false;
        bool m_presentationStopped = false;
        bool m_deviceLost = false;
        bool m_presentMaintenanceSupported = false;
        bool m_presentMaintenanceEnabled = false;
        bool m_surfaceMaintenanceEnabled = false;
        std::vector<VkFence> m_presentFences;
        std::vector<bool> m_presentFencePending;
        std::vector<VkCommandBuffer> m_cancelCommandBuffers;
        VkPhysicalDeviceFeatures m_enabledFeatures{};
        VkPhysicalDeviceFeatures m_requestedFeatures{};
        void* m_externalWindow = nullptr;
        std::function<std::shared_ptr<void>(uint64)> m_presentationBudget;
        std::shared_ptr<void> m_presentationLease;
        uint64 m_presentationReservationEstimate = 0;
        uint32 m_presentationReservedImages = 0;
        uint64 m_surfaceGeneration = 0;
        struct SubmissionRecord { uint64_t value; VkFence fence; };
        struct TimestampRecord {
            VkCommandBuffer commandBuffer;
            VkQueryPool pool;
            uint64_t frame;
            std::string label;
            uint64_t submission = 0;
            bool ended = false;
        };
        void refreshSubmissionCompletionLocked() const;
        void destroyGPUTimestampResources();
        mutable std::mutex m_submissionMutex;
        mutable std::vector<SubmissionRecord> m_submissions;
        uint64_t m_lastSubmission = 0;
        mutable uint64_t m_completedSubmission = 0;
        std::vector<TimestampRecord> m_timestampRecords;
        std::vector<VkQueryPool> m_availableTimestampPools;
        uint32_t m_timestampValidBits = 0;
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
        
        // MSAA support
        VkSampleCountFlagBits getMaxUsableSampleCount();
        bool createMSAAResources();
        void destroyMSAAResources();
        
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
        bool m_presentationVSync = true;
        VkPresentModeKHR m_configuredPresentMode = VK_PRESENT_MODE_MAX_ENUM_KHR;
        std::vector<VkImage> m_swapchainImages;
        std::vector<VkImageView> m_swapchainImageViews;
        VkFormat m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
        VkColorSpaceKHR m_swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        bool m_swapchainReadbackSupported = false;
        VkExtent2D m_swapchainExtent = {0, 0};
        
        // Render pass and framebuffers
        VkRenderPass m_renderPass = VK_NULL_HANDLE;
        VkRenderPass m_rttRenderPass = VK_NULL_HANDLE;  // RTT-specific render pass with SHADER_READ_ONLY initial layout
        std::vector<VkFramebuffer> m_swapchainFramebuffers;
        
        // Depth buffer resources (UE5-style)
        VkImage m_depthImage = VK_NULL_HANDLE;
        VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
        VkImageView m_depthImageView = VK_NULL_HANDLE;
        VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
        
        // MSAA resources
        VkSampleCountFlagBits m_msaaSampleCount = VK_SAMPLE_COUNT_1_BIT;
        VkImage m_msaaColorImage = VK_NULL_HANDLE;
        VkDeviceMemory m_msaaColorImageMemory = VK_NULL_HANDLE;
        VkImageView m_msaaColorImageView = VK_NULL_HANDLE;
        VkImage m_msaaDepthImage = VK_NULL_HANDLE;
        VkDeviceMemory m_msaaDepthImageMemory = VK_NULL_HANDLE;
        VkImageView m_msaaDepthImageView = VK_NULL_HANDLE;
        
        // Command handling (Legacy, kept for backward compatibility if needed)
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        
        // UE5-style Immediate Command List (replaces legacy VulkanCommandList)
        TUniquePtr<FVulkanRHICommandListImmediate> m_immediateCommandList;
        
        // Per-frame command buffer management (UE5 pattern)
        TUniquePtr<FVulkanCommandBufferManager> m_commandBufferManager;
        TUniquePtr<FVulkanCommandListContext> m_commandListContext;
        
        // Pipeline cache
        TUniquePtr<VulkanPipelineCache> m_pipelineCache;
        
        // Descriptor pool manager (multi-descriptor set support, UE5-style)
        TUniquePtr<VulkanDescriptorPoolManager> m_descriptorPoolManager;
        
        // Descriptor set layout cache (UE5-style)
        TUniquePtr<FVulkanDescriptorSetLayoutCache> m_descriptorSetLayoutCache;
        
        // Descriptor set cache for frame-local reuse (UE5-style)
        TUniquePtr<FVulkanDescriptorSetCache> m_descriptorSetCache;
        
        // Render target caches for RTT support (UE5-style)
        TUniquePtr<FVulkanRenderPassCache> m_renderPassCache;
        TUniquePtr<FVulkanFramebufferCache> m_framebufferCache;
        
        // Memory manager (UE5-style sub-allocation)
        TUniquePtr<FVulkanMemoryManager> m_memoryManager;
        
        // Synchronization - per-frame resources (MAX_FRAMES_IN_FLIGHT)
        TArray<VkSemaphore> m_imageAvailableSemaphores;
        TArray<VkFence> m_inFlightFences;
        
        // Per-image semaphores - indexed by swapchain image index (not frame index)
        // This ensures each image has its own render finished semaphore
        std::vector<VkSemaphore> m_perImageRenderFinishedSemaphores;
        
        // Per-image fence tracking - maps swapchain image index to the fence that's rendering to it
        std::vector<VkFence> m_imagesInFlight;
        
        // Memory properties
        VkPhysicalDeviceMemoryProperties m_memoryProperties{};
        VkPhysicalDeviceProperties m_deviceProperties{};
        VkPhysicalDeviceFeatures m_deviceFeatures{};
        
        // Settings
        bool m_validationEnabled = false;
        bool m_runningUnderRenderDoc = false;   // set in createInstance; gate GPU-AV to avoid RenderDoc conflicts
        bool m_debugMarkersEnabled = true;
        uint32 m_currentFrame = 0;
        uint32 m_currentImageIndex = 0;
        
        // Texture layout tracking for per-command-buffer transitions
        struct TextureLayoutInfo {
            VkImage image;
            uint32 mipLevels;
            uint32 arrayLayers;
        };
        TArray<TextureLayoutInfo> m_texturesNeedingTransition;
        std::mutex m_textureTransitionMutex;
        
        // Track which textures have been transitioned for each command buffer
        // This avoids redundant transitions within the same command buffer
        std::unordered_map<VkCommandBuffer, std::unordered_set<VkImage>> m_transitionedTexturesPerCmdBuffer;
        
        // Deferred resource destruction queue (UE5-style FDeferredDeletionQueue)
        struct DeferredBufferDestruction {
            VkBuffer buffer;
            VkDeviceMemory memory;
            uint32 frameCount;  // Frames to wait before destruction
        };
        struct DeferredImageDestruction {
            VkImage image;
            VkImageView imageView;
            VkDeviceMemory memory;
            uint32 frameCount;
        };
        TArray<DeferredBufferDestruction> m_deferredBufferDestructions;
        TArray<DeferredImageDestruction> m_deferredImageDestructions;
        std::mutex m_deferredDestructionMutex;
        
        // Constants
        static constexpr uint32 DEFERRED_DESTRUCTION_FRAMES = 3;  // Wait 3 frames before destruction
    };
}
