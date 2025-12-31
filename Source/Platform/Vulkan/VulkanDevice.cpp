#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Vulkan/VulkanDescriptorSetLayoutCache.h"
#include "Platform/Vulkan/VulkanRenderTargetCache.h"
#include "Platform/Vulkan/VulkanSampler.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Platform/Vulkan/VulkanUtils.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Core/Log.h"

#include <set>
#include <algorithm>
#include <vector>
#include <unordered_set>

namespace MonsterRender::RHI::Vulkan {
    
    // Required device extensions - use C arrays to avoid static initialization order issues
    static const char* const g_deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    static constexpr uint32 g_deviceExtensionCount = sizeof(g_deviceExtensions) / sizeof(g_deviceExtensions[0]);
    
    // Validation layers - use C arrays to avoid static initialization order issues
    static const char* const g_validationLayers[] = {
        "VK_LAYER_KHRONOS_validation"
    };
    static constexpr uint32 g_validationLayerCount = sizeof(g_validationLayers) / sizeof(g_validationLayers[0]);
    
    VulkanDevice::VulkanDevice() {
        MR_LOG_INFO("Creating Vulkan device...");
        
        // Initialize capabilities with default values
        // Will be properly filled during initialization
        m_capabilities = {};
        m_capabilities.deviceName = "Unknown Vulkan Device";
        m_capabilities.vendorName = "Unknown Vendor";
        m_capabilities.dedicatedVideoMemory = 0;
        m_capabilities.dedicatedSystemMemory = 0;
        m_capabilities.sharedSystemMemory = 0;
        
        // Initialize other fields with safe defaults
        m_capabilities.supportsGeometryShader = false;
        m_capabilities.supportsTessellation = false;
        m_capabilities.supportsComputeShader = false;
        m_capabilities.supportsMultiDrawIndirect = false;
        m_capabilities.supportsTimestampQuery = false;
    }
    
    VulkanDevice::~VulkanDevice() {
        MR_LOG_INFO("Destroying Vulkan device...");
        shutdown();
    }
    
    bool VulkanDevice::initialize(const RHICreateInfo& createInfo) {
        MR_LOG_INFO("Initializing Vulkan device...");
        
        // Initialize Vulkan API first
        if (!VulkanAPI::initialize()) {
            MR_LOG_ERROR("Failed to initialize Vulkan API");
            return false;
        }
        
        // Step 1: Create Vulkan instance
        if (!createInstance(createInfo)) {
            MR_LOG_ERROR("Failed to create Vulkan instance");
            return false;
        }
        
        // Step 2: Setup debug messenger (if validation enabled)
        if (createInfo.enableValidation && !setupDebugMessenger()) {
            MR_LOG_ERROR("Failed to setup debug messenger");
            return false;
        }
        
        // Step 3: Create window surface
        if (!createSurface(createInfo)) {
            MR_LOG_ERROR("Failed to create window surface");
            return false;
        }
        
        // Step 4: Select physical device
        if (!selectPhysicalDevice()) {
            MR_LOG_ERROR("Failed to select suitable physical device");
            return false;
        }
        
        // Step 5: Create logical device
        if (!createLogicalDevice()) {
            MR_LOG_ERROR("Failed to create logical device");
            return false;
        }
        
        // Step 6: Create swapchain
        if (!createSwapchain(createInfo)) {
            MR_LOG_ERROR("Failed to create swapchain");
            return false;
        }
        
        // Step 6b: Create depth resources (UE5-style)
        // Reference: UE5 FVulkanDynamicRHI::CreateSwapChain creates depth buffer with swapchain
        if (!createDepthResources()) {
            MR_LOG_ERROR("Failed to create depth resources");
            return false;
        }
        
        // Step 7: Create render pass (now includes depth attachment)
        if (!createRenderPass()) {
            MR_LOG_ERROR("Failed to create render pass");
            return false;
        }
        
        // Step 8: Create framebuffers (now includes depth view)
        if (!createFramebuffers()) {
            MR_LOG_ERROR("Failed to create framebuffers");
            return false;
        }
        
        // Step 9: Create command pool
        if (!createCommandPool()) {
            MR_LOG_ERROR("Failed to create command pool");
            return false;
        }
        
        // Step 10: Create synchronization objects
        if (!createSyncObjects()) {
            MR_LOG_ERROR("Failed to create synchronization objects");
            return false;
        }
        
        // Step 11: Create pipeline cache
        m_pipelineCache = MakeUnique<VulkanPipelineCache>(this);
        if (!m_pipelineCache) {
            MR_LOG_ERROR("Failed to create pipeline cache");
            return false;
        }
        
        // Step 12: Create descriptor set allocator
        m_descriptorSetAllocator = MakeUnique<VulkanDescriptorSetAllocator>(this);
        if (!m_descriptorSetAllocator) {
            MR_LOG_ERROR("Failed to create descriptor set allocator");
            return false;
        }
        
        // Step 12b: Create descriptor set layout cache (UE5-style)
        m_descriptorSetLayoutCache = MakeUnique<FVulkanDescriptorSetLayoutCache>(this);
        if (!m_descriptorSetLayoutCache) {
            MR_LOG_ERROR("Failed to create descriptor set layout cache");
            return false;
        }
        MR_LOG_INFO("Descriptor set layout cache initialized (UE5-style caching)");
        
        // Step 12c: Create descriptor set cache for frame-local reuse
        m_descriptorSetCache = MakeUnique<FVulkanDescriptorSetCache>(this);
        if (!m_descriptorSetCache) {
            MR_LOG_ERROR("Failed to create descriptor set cache");
            return false;
        }
        MR_LOG_INFO("Descriptor set cache initialized (frame-local reuse)");
        
        // Step 12d: Create render pass cache for RTT support (UE5-style)
        m_renderPassCache = MakeUnique<FVulkanRenderPassCache>(this);
        if (!m_renderPassCache) {
            MR_LOG_ERROR("Failed to create render pass cache");
            return false;
        }
        MR_LOG_INFO("Render pass cache initialized (RTT support)");
        
        // Step 12e: Create framebuffer cache for RTT support (UE5-style)
        m_framebufferCache = MakeUnique<FVulkanFramebufferCache>(this);
        if (!m_framebufferCache) {
            MR_LOG_ERROR("Failed to create framebuffer cache");
            return false;
        }
        MR_LOG_INFO("Framebuffer cache initialized (RTT support)");
        
        // Step 13: Create memory manager (UE5-style sub-allocation)
        m_memoryManager = MakeUnique<FVulkanMemoryManager>(m_device, m_physicalDevice);
        if (!m_memoryManager) {
            MR_LOG_ERROR("Failed to create memory manager");
            return false;
        }
        MR_LOG_INFO("FVulkanMemoryManager initialized (UE5-style sub-allocation enabled)");
        
        // Step 14: Create per-frame command buffer manager (UE5 pattern)
        m_commandBufferManager = MakeUnique<FVulkanCommandBufferManager>(this);
        if (!m_commandBufferManager || !m_commandBufferManager->initialize()) {
            MR_LOG_ERROR("Failed to create command buffer manager");
            return false;
        }
        MR_LOG_INFO("Per-frame command buffer manager initialized (3 buffers)");
        
        // Step 15: Create command list context (UE5 pattern)
        m_commandListContext = MakeUnique<FVulkanCommandListContext>(this, m_commandBufferManager.get());
        if (!m_commandListContext || !m_commandListContext->initialize()) {
            MR_LOG_ERROR("Failed to create command list context");
            return false;
        }
        MR_LOG_INFO("Command list context initialized");
        
        // Step 16: Query device capabilities
        queryCapabilities();
        
        MR_LOG_INFO("Vulkan device initialized successfully");
        return true;
    }
    
    void VulkanDevice::shutdown() {
        if (m_device != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkDeviceWaitIdle(m_device);
            
            // Destroy synchronization objects
            // Destroy per-frame sync objects
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                if (i < m_imageAvailableSemaphores.size() && m_imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
                    functions.vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
                }
                if (i < m_inFlightFences.size() && m_inFlightFences[i] != VK_NULL_HANDLE) {
                    functions.vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
                }
            }
            
            // Destroy per-image render finished semaphores
            for (size_t i = 0; i < m_perImageRenderFinishedSemaphores.size(); i++) {
                if (m_perImageRenderFinishedSemaphores[i] != VK_NULL_HANDLE) {
                    functions.vkDestroySemaphore(m_device, m_perImageRenderFinishedSemaphores[i], nullptr);
                }
            }
            m_perImageRenderFinishedSemaphores.clear();
            m_imagesInFlight.clear();
            
            // Clean up command lists and managers BEFORE destroying command pool
            // (command buffers must be freed before the pool is destroyed)
            m_commandListContext.reset();
            m_commandBufferManager.reset();
            m_immediateCommandList.reset();
            
            // Destroy command pool
            if (m_commandPool != VK_NULL_HANDLE) {
                functions.vkDestroyCommandPool(m_device, m_commandPool, nullptr);
            }
            
            // Destroy framebuffers
            for (auto framebuffer : m_swapchainFramebuffers) {
                if (framebuffer != VK_NULL_HANDLE) {
                    functions.vkDestroyFramebuffer(m_device, framebuffer, nullptr);
                }
            }
            m_swapchainFramebuffers.clear();
            
            // Destroy depth resources (must be after framebuffers)
            destroyDepthResources();
            
            // Destroy render pass
            if (m_renderPass != VK_NULL_HANDLE) {
                functions.vkDestroyRenderPass(m_device, m_renderPass, nullptr);
                m_renderPass = VK_NULL_HANDLE;
            }
            
            // Destroy swapchain
            if (m_swapchain != VK_NULL_HANDLE) {
                functions.vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
            }
            
            // Destroy image views
            for (auto imageView : m_swapchainImageViews) {
                functions.vkDestroyImageView(m_device, imageView, nullptr);
            }
            
            // Clean up high-level systems before device destruction
            // (m_immediateCommandList already reset before command pool destruction)
            m_pipelineCache.reset();
            m_descriptorSetAllocator.reset();
            
            // Clean up render target caches
            m_framebufferCache.reset();
            m_renderPassCache.reset();
            m_descriptorSetLayoutCache.reset();
            m_descriptorSetCache.reset();
            
            // Destroy memory manager (must be before device destruction)
            if (m_memoryManager) {
                MR_LOG_INFO("Destroying FVulkanMemoryManager...");
                m_memoryManager.reset();
            }
            
            // Destroy logical device
            functions.vkDestroyDevice(m_device, nullptr);
        }
        
        // Destroy surface
        if (m_surface != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        }
        
        // Destroy debug messenger
        if (m_debugMessenger != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
            VulkanUtils::destroyDebugMessenger(m_instance, m_debugMessenger);
        }
        
        // Destroy instance
        if (m_instance != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkDestroyInstance(m_instance, nullptr);
        }
        
        // Shutdown API
        VulkanAPI::shutdown();
    }
    
    const RHIDeviceCapabilities& VulkanDevice::getCapabilities() const {
        return m_capabilities;
    }
    
    // Resource creation methods - stub implementations for now
    TSharedPtr<IRHIBuffer> VulkanDevice::createBuffer(const BufferDesc& desc) {
        auto vulkanBuffer = MakeShared<VulkanBuffer>(this, desc);
        if (!vulkanBuffer->isValid()) {
            MR_LOG_ERROR("Failed to create Vulkan buffer: " + desc.debugName);
            return nullptr;
        }
        return vulkanBuffer;
    }
    
    TSharedPtr<IRHITexture> VulkanDevice::createTexture(const TextureDesc& desc) {
        auto vulkanTexture = MakeShared<VulkanTexture>(this, desc);
        if (!vulkanTexture->isValid()) {
            MR_LOG_ERROR("Failed to create Vulkan texture: " + desc.debugName);
            return nullptr;
        }
        return vulkanTexture;
    }
    
    TSharedPtr<IRHIVertexShader> VulkanDevice::createVertexShader(TSpan<const uint8> bytecode) {
        auto vulkanShader = MakeShared<VulkanVertexShader>(this, bytecode);
        if (!vulkanShader->isValid()) {
            MR_LOG_ERROR("Failed to create Vulkan vertex shader");
            return nullptr;
        }
        return vulkanShader;
    }
    
    TSharedPtr<IRHIPixelShader> VulkanDevice::createPixelShader(TSpan<const uint8> bytecode) {
        auto vulkanShader = MakeShared<VulkanPixelShader>(this, bytecode);
        if (!vulkanShader->isValid()) {
            MR_LOG_ERROR("Failed to create Vulkan pixel shader");
            return nullptr;
        }
        return vulkanShader;
    }
    
    TSharedPtr<IRHIPipelineState> VulkanDevice::createPipelineState(const PipelineStateDesc& desc) {
        MR_LOG_INFO("Creating Vulkan pipeline state: " + desc.debugName);
        
        if (!m_pipelineCache) {
            MR_LOG_ERROR("Pipeline cache not initialized");
            return nullptr;
        }
        
        // Use pipeline cache to get or create pipeline state
        auto pipelineState = m_pipelineCache->getOrCreatePipelineState(desc);
        if (!pipelineState) {
            MR_LOG_ERROR("Failed to create pipeline state: " + desc.debugName);
            return nullptr;
        }
        
        MR_LOG_INFO("Pipeline state created successfully: " + desc.debugName);
        return pipelineState;
    }
    
    TSharedPtr<IRHISampler> VulkanDevice::createSampler(const SamplerDesc& desc) {
        MR_LOG_DEBUG("Creating Vulkan sampler: " + desc.debugName);
        
        auto sampler = MakeShared<VulkanSampler>(this, desc);
        if (!sampler->isValid()) {
            MR_LOG_ERROR("Failed to create sampler: " + desc.debugName);
            return nullptr;
        }
        
        return sampler;
    }
    
    // ========================================================================
    // UE5-style Vertex and Index Buffer Creation
    // ========================================================================
    
    TSharedPtr<FRHIVertexBuffer> VulkanDevice::CreateVertexBuffer(
        uint32 Size,
        EBufferUsageFlags Usage,
        FRHIResourceCreateInfo& CreateInfo)
    {
        MR_LOG_DEBUG("Creating vertex buffer: " + CreateInfo.DebugName + 
                     " (size: " + std::to_string(Size) + " bytes)");
        
        // Create the Vulkan vertex buffer
        auto vertexBuffer = MakeShared<FVulkanVertexBuffer>(this, Size, 0, Usage);
        
        // Initialize with optional initial data
        if (!vertexBuffer->Initialize(CreateInfo.BulkData, CreateInfo.BulkDataSize)) {
            MR_LOG_ERROR("Failed to create vertex buffer: " + CreateInfo.DebugName);
            return nullptr;
        }
        
        // Set debug name
        vertexBuffer->SetDebugName(CreateInfo.DebugName);
        
        MR_LOG_DEBUG("Successfully created vertex buffer: " + CreateInfo.DebugName);
        return vertexBuffer;
    }
    
    TSharedPtr<FRHIIndexBuffer> VulkanDevice::CreateIndexBuffer(
        uint32 Stride,
        uint32 Size,
        EBufferUsageFlags Usage,
        FRHIResourceCreateInfo& CreateInfo)
    {
        MR_LOG_DEBUG("Creating index buffer: " + CreateInfo.DebugName + 
                     " (size: " + std::to_string(Size) + " bytes, " +
                     (Stride == 4 ? "32-bit" : "16-bit") + " indices)");
        
        // Create the Vulkan index buffer
        auto indexBuffer = MakeShared<FVulkanIndexBuffer>(this, Stride, Size, Usage);
        
        // Initialize with optional initial data
        if (!indexBuffer->Initialize(CreateInfo.BulkData, CreateInfo.BulkDataSize)) {
            MR_LOG_ERROR("Failed to create index buffer: " + CreateInfo.DebugName);
            return nullptr;
        }
        
        // Set debug name
        indexBuffer->SetDebugName(CreateInfo.DebugName);
        
        MR_LOG_DEBUG("Successfully created index buffer: " + CreateInfo.DebugName);
        return indexBuffer;
    }
    
    IRHICommandList* VulkanDevice::getImmediateCommandList() {
        // In UE5 pattern, we return the immediate command list which automatically delegates
        // to the current per-frame command buffer context. No manual binding needed.
        return m_immediateCommandList.get();
    }
    
    void VulkanDevice::waitForIdle() {
        if (m_device != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkDeviceWaitIdle(m_device);
        }
    }
    
    void VulkanDevice::present() {
        MR_LOG_INFO("===== VulkanDevice::present() START =====");
        // Reference: UE5 FVulkanDynamicRHI::RHIEndDrawingViewport
        // Image acquisition is now done in FVulkanCommandListContext::prepareForNewFrame()
        
        if (m_swapchain == VK_NULL_HANDLE) {
            MR_LOG_WARNING("Cannot present: no swapchain available");
            return;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Submit command buffer using per-image semaphores (Vulkan best practice)
        // Each swapchain image has its own render finished semaphore to avoid conflicts
        MR_LOG_INFO("  Submitting command buffer...");
        if (m_commandListContext && m_currentImageIndex < m_perImageRenderFinishedSemaphores.size()) {
            VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
            VkSemaphore signalSemaphores[] = {m_perImageRenderFinishedSemaphores[m_currentImageIndex]};
            
            m_commandListContext->submitCommands(
                waitSemaphores, 1,
                signalSemaphores, 1
            );
            MR_LOG_INFO("  Command buffer submitted");
        } else {
            MR_LOG_WARNING("  Cannot submit: context or semaphore issue");
        }
        
        // Present using the per-image semaphore
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        
        VkSemaphore waitSemaphoresPresent[] = {m_perImageRenderFinishedSemaphores[m_currentImageIndex]};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = waitSemaphoresPresent;
        
        VkSwapchainKHR swapChains[] = {m_swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &m_currentImageIndex;
        presentInfo.pResults = nullptr;
        
        VkResult result = functions.vkQueuePresentKHR(m_presentQueue, &presentInfo);
        
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            // Should recreate swapchain
            MR_LOG_WARNING("Swapchain suboptimal or out of date");
        } else if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to present swapchain image! Result: " + std::to_string(result));
        }
        
        // Per-image fence tracking now handles synchronization
        // No need for vkQueueWaitIdle - better parallelism achieved
        
        // Advance frame index AFTER present for next frame
        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        
        // Begin new frame for descriptor set allocator (for next frame)
        if (m_descriptorSetAllocator) {
            m_descriptorSetAllocator->beginFrame(m_currentFrame);
        }
        
        // Reset descriptor set cache for new frame (UE5-style frame-local caching)
        if (m_descriptorSetCache) {
            m_descriptorSetCache->Reset(m_currentFrame);
        }
        
        // Garbage collect stale descriptor set layouts
        if (m_descriptorSetLayoutCache) {
            m_descriptorSetLayoutCache->GarbageCollect(m_currentFrame, 120);
        }
        
        // Process deferred resource destructions
        processDeferredDestructions();
    }
    
    void VulkanDevice::getMemoryStats(uint64& usedBytes, uint64& availableBytes) {
        // TODO: Implement proper memory tracking
        usedBytes = 0;
        availableBytes = 0;
    }
    
    void VulkanDevice::collectGarbage() {
        // TODO: Implement resource cleanup
    }
    
    void VulkanDevice::setDebugName(const String& name) {
        // TODO: Set device debug name using VK_EXT_debug_utils
    }
    
    void VulkanDevice::setValidationEnabled(bool enabled) {
        m_validationEnabled = enabled;
    }
    
    TSharedPtr<IRHISwapChain> VulkanDevice::createSwapChain(const SwapChainDesc& desc) {
        // For now, Vulkan device manages its own swapchain internally
        // Return nullptr as the internal swapchain is used
        MR_LOG_WARNING("VulkanDevice::createSwapChain - Using internal swapchain management");
        return nullptr;
    }
    
    EPixelFormat VulkanDevice::getSwapChainFormat() const {
        // Convert VkFormat to EPixelFormat
        switch (m_swapchainImageFormat) {
            case VK_FORMAT_B8G8R8A8_SRGB:   return EPixelFormat::B8G8R8A8_SRGB;
            case VK_FORMAT_B8G8R8A8_UNORM:  return EPixelFormat::B8G8R8A8_UNORM;
            case VK_FORMAT_R8G8B8A8_SRGB:   return EPixelFormat::R8G8B8A8_SRGB;
            case VK_FORMAT_R8G8B8A8_UNORM:  return EPixelFormat::R8G8B8A8_UNORM;
            default:                         return EPixelFormat::R8G8B8A8_SRGB;
        }
    }
    
    EPixelFormat VulkanDevice::getDepthFormat() const {
        // Convert VkFormat to EPixelFormat
        switch (m_depthFormat) {
            case VK_FORMAT_D32_SFLOAT:           return EPixelFormat::D32_FLOAT;
            case VK_FORMAT_D24_UNORM_S8_UINT:    return EPixelFormat::D24_UNORM_S8_UINT;
            case VK_FORMAT_D32_SFLOAT_S8_UINT:   return EPixelFormat::D32_FLOAT_S8_UINT;
            default:                              return EPixelFormat::D32_FLOAT;
        }
    }
    
    // Private implementation methods - full implementations
    bool VulkanDevice::createInstance(const RHICreateInfo& createInfo) {
        MR_LOG_INFO("Creating Vulkan instance...");
        
        m_validationEnabled = createInfo.enableValidation;
        
        // ============================================================================
        // RenderDoc Detection and Layer Conflict Prevention
        // Disable problematic implicit layers that conflict with RenderDoc
        // ============================================================================
        
        // Check if running under RenderDoc
        bool runningUnderRenderDoc = false;
        HMODULE renderDocModule = GetModuleHandleA("renderdoc.dll");
        if (renderDocModule != nullptr) {
            runningUnderRenderDoc = true;
            MR_LOG_INFO("RenderDoc detected - using minimal layer configuration");
        }
        
        // Disable problematic implicit layers via environment variables
        // These layers can conflict with RenderDoc's capture mechanism
        const char* problematicLayers[] = {
            "DISABLE_VK_LAYER_TENCENT_wegame_cross_overlay_1",
            "DISABLE_VK_LAYER_VALVE_steam_overlay_1",
            "DISABLE_VK_LAYER_VALVE_steam_fossilize_1",
            "DISABLE_VK_LAYER_EOS_Overlay_1",
            "DISABLE_VK_LAYER_ROCKSTAR_GAMES_social_club_1"
        };
        
        for (const char* envVar : problematicLayers) {
            _putenv_s(envVar, "1");
        }
        MR_LOG_INFO("Disabled problematic implicit Vulkan layers for RenderDoc compatibility");
        
        // Check validation layers if requested
        if (m_validationEnabled && !checkValidationLayerSupport()) {
            MR_LOG_ERROR("Validation layers requested, but not available!");
            return false;
        }
        
        // Application info
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = createInfo.applicationName.c_str();
        appInfo.applicationVersion = createInfo.applicationVersion;
        appInfo.pEngineName = "MonsterRender";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;
        
        // Instance create info
        VkInstanceCreateInfo instanceCreateInfo{};
        instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceCreateInfo.pApplicationInfo = &appInfo;
        
        // Get required extensions
        auto extensions = getRequiredExtensions(m_validationEnabled);
        instanceCreateInfo.enabledExtensionCount = static_cast<uint32>(extensions.size());
        instanceCreateInfo.ppEnabledExtensionNames = extensions.data();
        
        // Set validation layers if enabled
        // When running under RenderDoc, disable validation layer to reduce conflicts
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        bool useValidation = m_validationEnabled && !runningUnderRenderDoc;
        
        if (useValidation) {
            instanceCreateInfo.enabledLayerCount = g_validationLayerCount;
            instanceCreateInfo.ppEnabledLayerNames = g_validationLayers;
            
            // Setup debug messenger for instance creation/destruction
            debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugCreateInfo.pfnUserCallback = VulkanUtils::debugCallback;
            debugCreateInfo.pUserData = nullptr;
            instanceCreateInfo.pNext = &debugCreateInfo;
            MR_LOG_INFO("Validation layers enabled");
        } else {
            instanceCreateInfo.enabledLayerCount = 0;
            instanceCreateInfo.pNext = nullptr;
            if (runningUnderRenderDoc && m_validationEnabled) {
                MR_LOG_INFO("Validation layers disabled for RenderDoc compatibility");
            }
        }
        
        // Create instance
        const auto& functions = VulkanAPI::getFunctions();
        VkResult result = functions.vkCreateInstance(&instanceCreateInfo, nullptr, &m_instance);
        
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create Vulkan instance! Result: " + std::to_string(result));
            return false;
        }
        
        // Load instance-level functions
        VulkanAPI::loadInstanceFunctions(m_instance);
        
        MR_LOG_INFO("Vulkan instance created successfully");
        return true;
    }
    
    bool VulkanDevice::setupDebugMessenger() {
        MR_LOG_INFO("Setting up Vulkan debug messenger...");
        
        if (!m_validationEnabled) {
            return true; // Not needed
        }
        
        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        
        if (m_debugMarkersEnabled) {
            createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        }
        
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = VulkanUtils::debugCallback;
        createInfo.pUserData = nullptr;
        
        VkResult result = VulkanUtils::createDebugMessenger(m_instance, &createInfo, &m_debugMessenger);
        
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to set up debug messenger! Result: " + std::to_string(result));
            return false;
        }
        
        MR_LOG_INFO("Debug messenger set up successfully");
        return true;
    }
    
    bool VulkanDevice::createSurface(const RHICreateInfo& createInfo) {
        MR_LOG_INFO("Creating window surface...");
        
        // For now, create a mock surface since we don't have proper windowing yet
        // In a real implementation, this would use the windowHandle parameter
        VkResult result = VulkanUtils::createSurface(m_instance, createInfo.windowHandle, &m_surface);
        
        if (result != VK_SUCCESS) {
            // For development, we'll skip surface creation and log a warning
            MR_LOG_WARNING("Surface creation skipped (no proper windowing system yet)");
            m_surface = VK_NULL_HANDLE;
            return true; // Continue without surface for now
        }
        
        MR_LOG_INFO("Window surface created successfully");
        return true;
    }
    
    bool VulkanDevice::selectPhysicalDevice() {
        MR_LOG_INFO("Selecting physical device...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Get physical device count
        uint32 deviceCount = 0;
        functions.vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
        
        if (deviceCount == 0) {
            MR_LOG_ERROR("Failed to find GPUs with Vulkan support!");
            return false;
        }
        
        MR_LOG_INFO("Found " + std::to_string(deviceCount) + " Vulkan-capable device(s)");
        
        // Get all physical devices - use std::vector for Vulkan API compatibility
        std::vector<VkPhysicalDevice> devices(deviceCount);
        functions.vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
        
        // Score and select the best device (prefer discrete GPU)
        VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
        int32 bestScore = -1;
        
        for (size_t i = 0; i < devices.size(); ++i) {
            const auto& device = devices[i];
            if (!isDeviceSuitable(device)) {
                continue;
            }
            
            VkPhysicalDeviceProperties props;
            functions.vkGetPhysicalDeviceProperties(device, &props);
            
            // Score the device: discrete GPU > integrated GPU > others
            int32 score = 0;
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                score = 1000;  // Strongly prefer discrete GPU
            } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                score = 100;   // Integrated GPU as fallback
            } else {
                score = 10;    // Other types
            }
            
            MR_LOG_INFO("Device " + std::to_string(i) + ": " + String(props.deviceName) + 
                       " (type=" + std::to_string(props.deviceType) + ", score=" + std::to_string(score) + ")");
            
            if (score > bestScore) {
                bestScore = score;
                bestDevice = device;
            }
        }
        
        if (bestDevice == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Failed to find a suitable GPU!");
            return false;
        }
        
        m_physicalDevice = bestDevice;
        
        // Get device properties
        functions.vkGetPhysicalDeviceProperties(bestDevice, &m_deviceProperties);
        functions.vkGetPhysicalDeviceFeatures(bestDevice, &m_deviceFeatures);
        functions.vkGetPhysicalDeviceMemoryProperties(bestDevice, &m_memoryProperties);
        
        MR_LOG_INFO("Selected GPU: " + String(m_deviceProperties.deviceName));
        MR_LOG_INFO("GPU Type: " + std::to_string(m_deviceProperties.deviceType));
        MR_LOG_INFO("API Version: " + std::to_string(VK_VERSION_MAJOR(m_deviceProperties.apiVersion)) +
                   "." + std::to_string(VK_VERSION_MINOR(m_deviceProperties.apiVersion)) +
                   "." + std::to_string(VK_VERSION_PATCH(m_deviceProperties.apiVersion)));
        
        return true;
    }
    
    bool VulkanDevice::createLogicalDevice() {
        MR_LOG_INFO("Creating logical device...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Find queue families
        m_graphicsQueueFamily = findQueueFamilies(m_physicalDevice, m_surface, VK_QUEUE_GRAPHICS_BIT);
        if (m_surface != VK_NULL_HANDLE) {
            // Note: Using VK_QUEUE_GRAPHICS_BIT here. Typically present queue should be queried
            // using vkGetPhysicalDeviceSurfaceSupportKHR for proper surface support detection.
            m_presentQueueFamily = findQueueFamilies(m_physicalDevice, m_surface, VK_QUEUE_GRAPHICS_BIT);
        } else {
            m_presentQueueFamily = m_graphicsQueueFamily; // Use graphics queue for present when no surface
        }
        
        if (m_graphicsQueueFamily.familyIndex == VK_QUEUE_FAMILY_IGNORED) {
            MR_LOG_ERROR("Failed to find graphics queue family!");
            return false;
        }
        
        // Create queue create infos
        // Use std::vector for Vulkan API compatibility
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32> uniqueQueueFamilies = {m_graphicsQueueFamily.familyIndex};
        
        if (m_surface != VK_NULL_HANDLE && m_presentQueueFamily.familyIndex != m_graphicsQueueFamily.familyIndex) {
            uniqueQueueFamilies.insert(m_presentQueueFamily.familyIndex);
        }
        
        float queuePriority = 1.0f;
        for (uint32 queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }
        
        // Device features (enable what we need)
        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = m_deviceFeatures.samplerAnisotropy;
        deviceFeatures.fillModeNonSolid = m_deviceFeatures.fillModeNonSolid;
        deviceFeatures.geometryShader = m_deviceFeatures.geometryShader;
        deviceFeatures.tessellationShader = m_deviceFeatures.tessellationShader;
        
        // ============================================================================
        // Device Extension Handling (RenderDoc-compatible)
        // Standard pattern: Query -> Filter -> Enable
        // Reference: Vulkan best practice for extension handling
        // ============================================================================
        
        // Step 1: Query supported device extensions
        std::unordered_set<std::string> supportedExtensions;
        uint32 extensionCount = 0;
        functions.vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        functions.vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, availableExtensions.data());
        
        MR_LOG_INFO("Available device extensions: " + std::to_string(extensionCount));
        for (const auto& ext : availableExtensions) {
            supportedExtensions.insert(ext.extensionName);
        }
        
        // Step 2: Build list of extensions to enable (filter by support)
        std::vector<const char*> enabledExtensions;
        
        // Helper lambda: enable extension only if supported
        auto enableIfSupported = [&](const char* extensionName) -> bool {
            if (supportedExtensions.count(extensionName)) {
                enabledExtensions.push_back(extensionName);
                return true;
            }
            MR_LOG_WARNING("Device extension not supported: " + String(extensionName));
            return false;
        };
        
        // Required extensions (must be supported)
        if (!enableIfSupported(VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            MR_LOG_ERROR("Required extension VK_KHR_swapchain not supported!");
            return false;
        }
        
        // ============================================================================
        // Optional advanced extensions
        // Note: In Vulkan 1.1+, many of these features are core and don't need extensions.
        // We use API version 1.0 for maximum compatibility, so we need extensions.
        // However, the dependency extensions (VK_KHR_get_physical_device_properties2, etc.)
        // are INSTANCE extensions, not device extensions. They should be enabled at
        // instance creation time, not device creation time.
        // 
        // For now, we only enable extensions that don't have complex dependencies,
        // or we rely on Vulkan 1.1+ where these are core features.
        // ============================================================================
        
        // Check Vulkan API version - if 1.1+, these features are core
        uint32 apiVersion = m_deviceProperties.apiVersion;
        bool isVulkan11OrHigher = VK_VERSION_MAJOR(apiVersion) > 1 || 
                                   (VK_VERSION_MAJOR(apiVersion) == 1 && VK_VERSION_MINOR(apiVersion) >= 1);
        bool isVulkan12OrHigher = VK_VERSION_MAJOR(apiVersion) > 1 || 
                                   (VK_VERSION_MAJOR(apiVersion) == 1 && VK_VERSION_MINOR(apiVersion) >= 2);
        
        MR_LOG_INFO("Device Vulkan API version: " + std::to_string(VK_VERSION_MAJOR(apiVersion)) + "." +
                   std::to_string(VK_VERSION_MINOR(apiVersion)) + "." +
                   std::to_string(VK_VERSION_PATCH(apiVersion)));
        
        // VK_KHR_maintenance1: core in Vulkan 1.1, required for negative viewport height
        // This extension enables Y-axis flipping via negative viewport height
        if (!isVulkan11OrHigher) {
            if (enableIfSupported(VK_KHR_MAINTENANCE1_EXTENSION_NAME)) {
                MR_LOG_INFO("VK_KHR_maintenance1: enabled (required for viewport Y-flip)");
            } else {
                MR_LOG_WARNING("VK_KHR_maintenance1 not supported - viewport Y-flip may not work");
            }
        } else {
            MR_LOG_INFO("VK_KHR_maintenance1: using Vulkan 1.1+ core feature");
        }
        
        // Timeline semaphores: core in Vulkan 1.2, extension in 1.1
        if (isVulkan12OrHigher) {
            MR_LOG_INFO("Timeline semaphores: using Vulkan 1.2 core feature");
        } else if (isVulkan11OrHigher) {
            enableIfSupported(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        }
        
        // Descriptor indexing: core in Vulkan 1.2
        if (isVulkan12OrHigher) {
            MR_LOG_INFO("Descriptor indexing: using Vulkan 1.2 core feature");
        } else if (isVulkan11OrHigher) {
            enableIfSupported(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        }
        
        // Buffer device address: core in Vulkan 1.2
        bool hasBufferDeviceAddress = false;
        if (isVulkan12OrHigher) {
            MR_LOG_INFO("Buffer device address: using Vulkan 1.2 core feature");
            hasBufferDeviceAddress = true;
        } else if (isVulkan11OrHigher) {
            hasBufferDeviceAddress = enableIfSupported(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        }
        
        // Suppress unused variable warning
        (void)hasBufferDeviceAddress;
        
        // Step 3: Log all requested extensions before device creation
        MR_LOG_INFO("=== Requested Device Extensions ===");
        for (const auto& ext : enabledExtensions) {
            MR_LOG_INFO("  Request Device Extension: " + String(ext));
        }
        MR_LOG_INFO("Total: " + std::to_string(enabledExtensions.size()) + " extensions");
        
        // Create device
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        
        // Device extensions (filtered list)
        createInfo.enabledExtensionCount = static_cast<uint32>(enabledExtensions.size());
        createInfo.ppEnabledExtensionNames = enabledExtensions.data();
        
        // Validation layers (deprecated for devices, but needed for older Vulkan)
        if (m_validationEnabled) {
            createInfo.enabledLayerCount = g_validationLayerCount;
            createInfo.ppEnabledLayerNames = g_validationLayers;
        } else {
            createInfo.enabledLayerCount = 0;
        }
        
        VkResult result = functions.vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
        
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create logical device! Result: " + std::to_string(result));
            return false;
        }
        
        // Load device-level functions
        VulkanAPI::loadDeviceFunctions(m_device);
        
        // Get queue handles
        functions.vkGetDeviceQueue(m_device, m_graphicsQueueFamily.familyIndex, 0, &m_graphicsQueue);
        if (m_surface != VK_NULL_HANDLE) {
            functions.vkGetDeviceQueue(m_device, m_presentQueueFamily.familyIndex, 0, &m_presentQueue);
        } else {
            m_presentQueue = m_graphicsQueue;
        }
        
        MR_LOG_INFO("Logical device created successfully");
        MR_LOG_INFO("Graphics queue family: " + std::to_string(m_graphicsQueueFamily.familyIndex));
        MR_LOG_INFO("Present queue family: " + std::to_string(m_presentQueueFamily.familyIndex));
        
        return true;
    }
    
    bool VulkanDevice::createSwapchain(const RHICreateInfo& createInfo) {
        MR_LOG_INFO("Creating swapchain...");
        
        // Skip swapchain creation if we don't have a surface
        if (m_surface == VK_NULL_HANDLE) {
            MR_LOG_WARNING("Skipping swapchain creation (no surface available)");
            m_swapchain = VK_NULL_HANDLE;
            m_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM; // Default format
            m_swapchainExtent = {createInfo.windowWidth, createInfo.windowHeight};
            return true;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Query swapchain support
        VkSurfaceCapabilitiesKHR capabilities;
        functions.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);
        
        // Use std::vector for Vulkan API compatibility
        uint32 formatCount;
        functions.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        functions.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
        
        uint32 presentModeCount;
        functions.vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        functions.vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());
        
        if (formats.empty() || presentModes.empty()) {
            MR_LOG_ERROR("Swapchain support is inadequate!");
            return false;
        }
        
        // Choose surface format
        VkSurfaceFormatKHR surfaceFormat = formats[0]; // Default fallback
        for (const auto& availableFormat : formats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && 
                availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surfaceFormat = availableFormat;
                break;
            }
        }
        
        // Choose present mode (prefer mailbox for reduced latency)
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // Guaranteed to be available
        for (const auto& availablePresentMode : presentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = availablePresentMode;
                break;
            }
        }
        
        // Choose swap extent
        VkExtent2D extent;
        if (capabilities.currentExtent.width != UINT32_MAX) {
            extent = capabilities.currentExtent;
        } else {
            extent = {createInfo.windowWidth, createInfo.windowHeight};
            extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }
        
        // Choose image count
        uint32 imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }
        
        // Create swapchain
        VkSwapchainCreateInfoKHR createInfoSwapchain{};
        createInfoSwapchain.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfoSwapchain.surface = m_surface;
        createInfoSwapchain.minImageCount = imageCount;
        createInfoSwapchain.imageFormat = surfaceFormat.format;
        createInfoSwapchain.imageColorSpace = surfaceFormat.colorSpace;
        createInfoSwapchain.imageExtent = extent;
        createInfoSwapchain.imageArrayLayers = 1;
        createInfoSwapchain.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        
        uint32 queueFamilyIndices[] = {m_graphicsQueueFamily.familyIndex, m_presentQueueFamily.familyIndex};
        
        if (m_graphicsQueueFamily.familyIndex != m_presentQueueFamily.familyIndex) {
            createInfoSwapchain.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfoSwapchain.queueFamilyIndexCount = 2;
            createInfoSwapchain.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfoSwapchain.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfoSwapchain.queueFamilyIndexCount = 0;
            createInfoSwapchain.pQueueFamilyIndices = nullptr;
        }
        
        createInfoSwapchain.preTransform = capabilities.currentTransform;
        createInfoSwapchain.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfoSwapchain.presentMode = presentMode;
        createInfoSwapchain.clipped = VK_TRUE;
        createInfoSwapchain.oldSwapchain = VK_NULL_HANDLE;
        
        VkResult result = functions.vkCreateSwapchainKHR(m_device, &createInfoSwapchain, nullptr, &m_swapchain);
        
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create swap chain! Result: " + std::to_string(result));
            return false;
        }
        
        // Get swapchain images
        functions.vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
        m_swapchainImages.resize(imageCount);
        functions.vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());
        
        m_swapchainImageFormat = surfaceFormat.format;
        m_swapchainExtent = extent;
        
        // Create image views
        m_swapchainImageViews.resize(m_swapchainImages.size());
        for (size_t i = 0; i < m_swapchainImages.size(); i++) {
            VkImageViewCreateInfo createInfoImageView{};
            createInfoImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfoImageView.image = m_swapchainImages[i];
            createInfoImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfoImageView.format = m_swapchainImageFormat;
            createInfoImageView.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfoImageView.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfoImageView.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfoImageView.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfoImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfoImageView.subresourceRange.baseMipLevel = 0;
            createInfoImageView.subresourceRange.levelCount = 1;
            createInfoImageView.subresourceRange.baseArrayLayer = 0;
            createInfoImageView.subresourceRange.layerCount = 1;
            
            result = functions.vkCreateImageView(m_device, &createInfoImageView, nullptr, &m_swapchainImageViews[i]);
            
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to create image view! Result: " + std::to_string(result));
                return false;
            }
        }
        
        MR_LOG_INFO("Swapchain created successfully with " + std::to_string(imageCount) + " images");
        MR_LOG_INFO("Swapchain format: " + std::to_string(m_swapchainImageFormat));
        MR_LOG_INFO("Swapchain extent: " + std::to_string(m_swapchainExtent.width) + "x" + std::to_string(m_swapchainExtent.height));
        
        return true;
    }
    
    bool VulkanDevice::createRenderPass() {
        MR_LOG_INFO("Creating render pass with depth attachment...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // ============================================================================
        // Attachment Descriptions (UE5-style)
        // Reference: UE5 FVulkanRenderPass::Create
        // ============================================================================
        
        // Use std::vector for Vulkan API compatibility
        std::vector<VkAttachmentDescription> attachments;
        
        // Attachment 0: Color attachment (swapchain image)
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_swapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachments.push_back(colorAttachment);
        
        // Attachment 1: Depth attachment
        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = m_depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // Don't need to store depth after rendering
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments.push_back(depthAttachment);
        
        // ============================================================================
        // Attachment References
        // ============================================================================
        
        // Color attachment reference
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        
        // Depth attachment reference
        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        
        // ============================================================================
        // Subpass Description
        // ============================================================================
        
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;
        
        // ============================================================================
        // Subpass Dependencies (UE5-style synchronization)
        // Reference: UE5 FVulkanRenderPass handles implicit synchronization
        // ============================================================================
        
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        // Wait for color attachment output and early fragment tests (for depth)
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | 
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        
        // ============================================================================
        // Create Render Pass
        // ============================================================================
        
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        
        VkResult result = functions.vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create render pass! Result: " + std::to_string(result));
            return false;
        }
        
        MR_LOG_INFO("Render pass created successfully with depth attachment");
        MR_LOG_INFO("  Color format: " + std::to_string(m_swapchainImageFormat));
        MR_LOG_INFO("  Depth format: " + std::to_string(m_depthFormat));
        
        // ============================================================================
        // Create RTT-specific render pass
        // Key difference: initialLayout = SHADER_READ_ONLY_OPTIMAL (not UNDEFINED)
        // This matches the layout after ImGui sampling in the previous frame
        // ============================================================================
        MR_LOG_INFO("Creating RTT render pass...");
        
        std::vector<VkAttachmentDescription> rttAttachments;
        
        // RTT Color attachment
        // Use UNDEFINED initial layout to handle both first frame and subsequent frames
        // Vulkan spec allows UNDEFINED->any transition, content is discarded (which is fine since we CLEAR)
        VkAttachmentDescription rttColorAttachment{};
        rttColorAttachment.format = m_swapchainImageFormat;
        rttColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        rttColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        rttColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        rttColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        rttColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        rttColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // Works for any previous layout
        rttColorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;    // Ready for ImGui sampling
        rttAttachments.push_back(rttColorAttachment);
        
        // RTT Depth attachment
        // For shadow mapping: storeOp = STORE (need to read depth later)
        // finalLayout = DEPTH_STENCIL_READ_ONLY_OPTIMAL (correct layout for depth texture sampling)
        VkAttachmentDescription rttDepthAttachment{};
        rttDepthAttachment.format = m_depthFormat;
        rttDepthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        rttDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        rttDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // Store depth for shadow mapping
        rttDepthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        rttDepthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        rttDepthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        rttDepthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;  // Correct for depth sampling
        rttAttachments.push_back(rttDepthAttachment);
        
        VkAttachmentReference rttColorRef{};
        rttColorRef.attachment = 0;
        rttColorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        
        VkAttachmentReference rttDepthRef{};
        rttDepthRef.attachment = 1;
        rttDepthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        
        VkSubpassDescription rttSubpass{};
        rttSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        rttSubpass.colorAttachmentCount = 1;
        rttSubpass.pColorAttachments = &rttColorRef;
        rttSubpass.pDepthStencilAttachment = &rttDepthRef;
        
        VkSubpassDependency rttDependency{};
        rttDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        rttDependency.dstSubpass = 0;
        rttDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        rttDependency.srcAccessMask = 0;
        rttDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        rttDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | 
                                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        
        VkRenderPassCreateInfo rttRenderPassInfo{};
        rttRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rttRenderPassInfo.attachmentCount = static_cast<uint32>(rttAttachments.size());
        rttRenderPassInfo.pAttachments = rttAttachments.data();
        rttRenderPassInfo.subpassCount = 1;
        rttRenderPassInfo.pSubpasses = &rttSubpass;
        rttRenderPassInfo.dependencyCount = 1;
        rttRenderPassInfo.pDependencies = &rttDependency;
        
        result = functions.vkCreateRenderPass(m_device, &rttRenderPassInfo, nullptr, &m_rttRenderPass);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create RTT render pass! Result: " + std::to_string(result));
            return false;
        }
        
        MR_LOG_INFO("RTT render pass created successfully");
        
        return true;
    }
    
    bool VulkanDevice::createFramebuffers() {
        MR_LOG_INFO("Creating framebuffers with depth attachment...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Create one framebuffer for each swapchain image view
        // Each framebuffer has: [0] color attachment, [1] depth attachment
        // Reference: UE5 FVulkanFramebuffer::Create
        m_swapchainFramebuffers.resize(m_swapchainImageViews.size());
        
        for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
            // Attachment array: color view (per-swapchain) + depth view (shared)
            // The depth view is shared because we only render to one framebuffer at a time
            // Use std::vector for Vulkan API compatibility
            std::vector<VkImageView> attachments = {
                m_swapchainImageViews[i],  // Attachment 0: Color
                m_depthImageView           // Attachment 1: Depth
            };
            
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = m_swapchainExtent.width;
            framebufferInfo.height = m_swapchainExtent.height;
            framebufferInfo.layers = 1;
            
            VkResult result = functions.vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to create framebuffer " + std::to_string(i) + "! Result: " + std::to_string(result));
                return false;
            }
        }
        
        MR_LOG_INFO("Created " + std::to_string(m_swapchainFramebuffers.size()) + 
                   " framebuffers successfully (with depth attachment)");
        return true;
    }
    
    VkFramebuffer VulkanDevice::getCurrentFramebuffer() const {
        if (m_currentImageIndex >= m_swapchainFramebuffers.size()) {
            MR_LOG_ERROR("Current image index out of range: " + std::to_string(m_currentImageIndex));
            return VK_NULL_HANDLE;
        }
        return m_swapchainFramebuffers[m_currentImageIndex];
    }
    
    bool VulkanDevice::createCommandPool() {
        MR_LOG_INFO("Creating command pool...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Allow individual command buffer reset
        poolInfo.queueFamilyIndex = m_graphicsQueueFamily.familyIndex;
        
        VkResult result = functions.vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);
        
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create command pool! Result: " + std::to_string(result));
            return false;
        }
        
        // Create immediate command list
        m_immediateCommandList = MakeUnique<FVulkanRHICommandListImmediate>(this);
        // FVulkanRHICommandListImmediate doesn't need initialization - it just wraps the context
        
        MR_LOG_INFO("Immediate command list created successfully (UE5 style)");
        return true;
    }
    
    bool VulkanDevice::createSyncObjects() {
        MR_LOG_INFO("Creating synchronization objects...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Resize per-frame vectors
        m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start in signaled state
        
        // Create per-frame synchronization objects
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkResult result;
            
            result = functions.vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to create image available semaphore! Result: " + std::to_string(result));
                return false;
            }
            
            result = functions.vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to create in-flight fence! Result: " + std::to_string(result));
                return false;
            }
        }
        
        // Create per-image render finished semaphores (one per swapchain image)
        // This ensures each image has its own semaphore to avoid conflicts
        m_perImageRenderFinishedSemaphores.resize(m_swapchainImages.size());
        for (size_t i = 0; i < m_swapchainImages.size(); i++) {
            VkResult result = functions.vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_perImageRenderFinishedSemaphores[i]);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to create per-image render finished semaphore! Result: " + std::to_string(result));
                return false;
            }
        }
        
        // Initialize per-image fence tracking array
        m_imagesInFlight.resize(m_swapchainImages.size(), VK_NULL_HANDLE);
        
        MR_LOG_INFO("Synchronization objects created successfully");
        return true;
    }
    
    bool VulkanDevice::checkValidationLayerSupport() {
        const auto& functions = VulkanAPI::getFunctions();
        
        uint32 layerCount = 0;
        functions.vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        
        // No layers available - validation layers not supported
        if (layerCount == 0) {
            MR_LOG_WARNING("No Vulkan layers available, validation layers not supported");
            return false;
        }
        
        // Use std::vector to avoid TArray issues during static initialization
        std::vector<VkLayerProperties> availableLayers(layerCount);
        functions.vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        
        for (uint32 i = 0; i < g_validationLayerCount; ++i) {
            const char* layerName = g_validationLayers[i];
            bool layerFound = false;
            
            for (size_t j = 0; j < availableLayers.size(); ++j) {
                if (strcmp(layerName, availableLayers[j].layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }
            
            if (!layerFound) {
                MR_LOG_WARNING("Validation layer not found: " + String(layerName));
                return false;
            }
        }
        
        return true;
    }
    
    std::vector<const char*> VulkanDevice::getRequiredExtensions(bool enableValidation) {
        std::vector<const char*> extensions;
        
        // Surface extensions (platform-specific)
        #if PLATFORM_WINDOWS
            extensions.push_back("VK_KHR_surface");
            extensions.push_back("VK_KHR_win32_surface");
        #elif PLATFORM_LINUX
            extensions.push_back("VK_KHR_surface");
            extensions.push_back("VK_KHR_xlib_surface");
        #endif
        
        // Debug extensions
        if (enableValidation) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        
        // Log extensions
        MR_LOG_INFO("Required Vulkan extensions:");
        for (size_t i = 0; i < extensions.size(); ++i) {
            MR_LOG_INFO("  - " + String(extensions[i]));
        }
        
        return extensions;
    }
    
    bool VulkanDevice::isDeviceSuitable(VkPhysicalDevice device) {
        const auto& functions = VulkanAPI::getFunctions();
        
        // Get device properties
        VkPhysicalDeviceProperties deviceProperties;
        functions.vkGetPhysicalDeviceProperties(device, &deviceProperties);
        
        // Get device features
        VkPhysicalDeviceFeatures deviceFeatures;
        functions.vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
        
        // Check queue families
        QueueFamily graphicsFamily = findQueueFamilies(device, m_surface, VK_QUEUE_GRAPHICS_BIT);
        bool queueFamilySupport = graphicsFamily.familyIndex != VK_QUEUE_FAMILY_IGNORED;
        
        // Check device extension support
        bool extensionSupport = checkDeviceExtensionSupport(device);
        
        // Check swapchain support (if we have a surface)
        bool swapChainAdequate = true;
        if (m_surface != VK_NULL_HANDLE && extensionSupport) {
            uint32 formatCount, presentModeCount;
            functions.vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
            functions.vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
            swapChainAdequate = (formatCount > 0) && (presentModeCount > 0);
        }
        
        MR_LOG_DEBUG("Evaluating device: " + String(deviceProperties.deviceName));
        MR_LOG_DEBUG("  Queue family support: " + String(queueFamilySupport ? "YES" : "NO"));
        MR_LOG_DEBUG("  Extension support: " + String(extensionSupport ? "YES" : "NO"));
        MR_LOG_DEBUG("  Swapchain adequate: " + String(swapChainAdequate ? "YES" : "NO"));
        
        return queueFamilySupport && extensionSupport && swapChainAdequate;
    }
    
    QueueFamily VulkanDevice::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface, VkQueueFlags flags) {
        const auto& functions = VulkanAPI::getFunctions();
        
        QueueFamily family;
        
        uint32 queueFamilyCount = 0;
        functions.vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        
        // Use std::vector for Vulkan API compatibility
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        functions.vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
        
        for (uint32 i = 0; i < static_cast<uint32>(queueFamilies.size()); i++) {
            const auto& queueFamilyProperties = queueFamilies[i];
            
            // Check if this family supports the required flags
            if ((queueFamilyProperties.queueFlags & flags) == flags) {
                family.familyIndex = i;
                family.queueCount = queueFamilyProperties.queueCount;
                family.flags = queueFamilyProperties.queueFlags;
                
                // Check present support if we have a surface
                if (surface != VK_NULL_HANDLE) {
                    VkBool32 presentSupport = false;
                    functions.vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
                    family.supportsPresentToSurface = presentSupport == VK_TRUE;
                } else {
                    family.supportsPresentToSurface = true; // No surface, so present support is not relevant
                }
                
                break;
            }
        }
        
        return family;
    }
    
    bool VulkanDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) {
        const auto& functions = VulkanAPI::getFunctions();
        
        // Get device name for logging
        VkPhysicalDeviceProperties props;
        functions.vkGetPhysicalDeviceProperties(device, &props);
        
        uint32 extensionCount = 0;
        functions.vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        
        // No extensions available
        if (extensionCount == 0) {
            MR_LOG_WARNING("No device extensions available for " + String(props.deviceName));
            return g_deviceExtensionCount == 0;
        }
        
        // Use std::vector to avoid TArray issues
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        functions.vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
        
        // Log available extensions for debugging
        MR_LOG_DEBUG("Device " + String(props.deviceName) + " has " + std::to_string(extensionCount) + " extensions");
        
        std::set<String> requiredExtensions;
        for (uint32 i = 0; i < g_deviceExtensionCount; ++i) {
            requiredExtensions.insert(g_deviceExtensions[i]);
        }
        
        // Check if VK_KHR_swapchain is available
        bool hasSwapchain = false;
        for (size_t i = 0; i < availableExtensions.size(); ++i) {
            if (String(availableExtensions[i].extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
                hasSwapchain = true;
            }
            requiredExtensions.erase(availableExtensions[i].extensionName);
        }
        
        MR_LOG_DEBUG("  VK_KHR_swapchain: " + String(hasSwapchain ? "YES" : "NO"));
        
        if (!requiredExtensions.empty()) {
            MR_LOG_WARNING("Missing required extensions for " + String(props.deviceName) + ":");
            for (const auto& ext : requiredExtensions) {
                MR_LOG_WARNING("  - " + ext);
            }
        }
        
        return requiredExtensions.empty();
    }
    
    void VulkanDevice::queryCapabilities() {
        // Fill in device name and vendor
        m_capabilities.deviceName = String(m_deviceProperties.deviceName);
        
        // Map vendor ID to vendor name
        switch (m_deviceProperties.vendorID) {
            case 0x1002: m_capabilities.vendorName = "AMD"; break;
            case 0x10DE: m_capabilities.vendorName = "NVIDIA"; break;
            case 0x8086: m_capabilities.vendorName = "Intel"; break;
            case 0x13B5: m_capabilities.vendorName = "ARM"; break;
            case 0x5143: m_capabilities.vendorName = "Qualcomm"; break;
            default: 
                m_capabilities.vendorName = "Unknown (ID: 0x" + 
                    std::to_string(m_deviceProperties.vendorID) + ")"; 
                break;
        }
        
        // Query actual device limits
        const auto& limits = m_deviceProperties.limits;
        m_capabilities.maxTexture1DSize = limits.maxImageDimension1D;
        m_capabilities.maxTexture2DSize = limits.maxImageDimension2D;
        m_capabilities.maxTexture3DSize = limits.maxImageDimension3D;
        m_capabilities.maxTextureCubeSize = limits.maxImageDimensionCube;
        m_capabilities.maxTextureArrayLayers = limits.maxImageArrayLayers;
        m_capabilities.maxRenderTargets = limits.maxColorAttachments;
        m_capabilities.maxVertexInputBindings = limits.maxVertexInputBindings;
        m_capabilities.maxVertexInputAttributes = limits.maxVertexInputAttributes;
        
        // Calculate memory info
        uint64 totalDeviceMemory = 0;
        uint64 totalHostMemory = 0;
        
        for (uint32 i = 0; i < m_memoryProperties.memoryHeapCount; i++) {
            const auto& heap = m_memoryProperties.memoryHeaps[i];
            if (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                totalDeviceMemory += heap.size;
            } else {
                totalHostMemory += heap.size;
            }
        }
        
        m_capabilities.dedicatedVideoMemory = totalDeviceMemory;
        m_capabilities.dedicatedSystemMemory = totalHostMemory;
        m_capabilities.sharedSystemMemory = totalHostMemory; // Approximation
        
        // Feature support based on actual device features
        m_capabilities.supportsGeometryShader = m_deviceFeatures.geometryShader == VK_TRUE;
        m_capabilities.supportsTessellation = m_deviceFeatures.tessellationShader == VK_TRUE;
        m_capabilities.supportsComputeShader = true; // Vulkan 1.0 core feature
        m_capabilities.supportsMultiDrawIndirect = m_deviceFeatures.multiDrawIndirect == VK_TRUE;
        m_capabilities.supportsTimestampQuery = limits.timestampComputeAndGraphics == VK_TRUE;
        
        // Log capabilities
        MR_LOG_INFO("Device Capabilities:");
        MR_LOG_INFO("  Device: " + m_capabilities.deviceName);
        MR_LOG_INFO("  Vendor: " + m_capabilities.vendorName);
        MR_LOG_INFO("  Video Memory: " + std::to_string(m_capabilities.dedicatedVideoMemory / (1024*1024)) + " MB");
        MR_LOG_INFO("  Max Texture 2D: " + std::to_string(m_capabilities.maxTexture2DSize));
        MR_LOG_INFO("  Max Render Targets: " + std::to_string(m_capabilities.maxRenderTargets));
        MR_LOG_INFO("  Geometry Shader: " + String(m_capabilities.supportsGeometryShader ? "Yes" : "No"));
        MR_LOG_INFO("  Tessellation: " + String(m_capabilities.supportsTessellation ? "Yes" : "No"));
        MR_LOG_INFO("  Compute Shader: " + String(m_capabilities.supportsComputeShader ? "Yes" : "No"));
    }
    
    // ============================================================================
    // Depth Buffer Creation (UE5-style)
    // Reference: UE5 FVulkanDynamicRHI::CreateDepthStencilSurface
    // ============================================================================
    
    bool VulkanDevice::createDepthResources() {
        MR_LOG_INFO("Creating depth resources...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Find a suitable depth format
        m_depthFormat = findDepthFormat();
        if (m_depthFormat == VK_FORMAT_UNDEFINED) {
            MR_LOG_ERROR("Failed to find suitable depth format");
            return false;
        }
        
        MR_LOG_INFO("Using depth format: " + std::to_string(m_depthFormat));
        
        // ============================================================================
        // Step 1: Create depth image
        // Reference: UE5 FVulkanSurface::CreateVkImage
        // ============================================================================
        
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_swapchainExtent.width;
        imageInfo.extent.height = m_swapchainExtent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_depthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        VkResult result = functions.vkCreateImage(m_device, &imageInfo, nullptr, &m_depthImage);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create depth image: " + std::to_string(result));
            return false;
        }
        
        // ============================================================================
        // Step 2: Allocate memory for depth image
        // Reference: UE5 FVulkanMemoryManager::Alloc for GPU-only memory
        // ============================================================================
        
        VkMemoryRequirements memRequirements;
        functions.vkGetImageMemoryRequirements(m_device, m_depthImage, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        
        // Find memory type that is device local (GPU-only, fastest)
        uint32 memoryTypeIndex = UINT32_MAX;
        VkMemoryPropertyFlags desiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        
        for (uint32 i = 0; i < m_memoryProperties.memoryTypeCount; i++) {
            if ((memRequirements.memoryTypeBits & (1 << i)) &&
                (m_memoryProperties.memoryTypes[i].propertyFlags & desiredFlags) == desiredFlags) {
                memoryTypeIndex = i;
                break;
            }
        }
        
        if (memoryTypeIndex == UINT32_MAX) {
            MR_LOG_ERROR("Failed to find suitable memory type for depth buffer");
            functions.vkDestroyImage(m_device, m_depthImage, nullptr);
            m_depthImage = VK_NULL_HANDLE;
            return false;
        }
        
        allocInfo.memoryTypeIndex = memoryTypeIndex;
        
        result = functions.vkAllocateMemory(m_device, &allocInfo, nullptr, &m_depthImageMemory);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to allocate depth image memory: " + std::to_string(result));
            functions.vkDestroyImage(m_device, m_depthImage, nullptr);
            m_depthImage = VK_NULL_HANDLE;
            return false;
        }
        
        // Bind image to memory
        result = functions.vkBindImageMemory(m_device, m_depthImage, m_depthImageMemory, 0);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to bind depth image memory: " + std::to_string(result));
            functions.vkFreeMemory(m_device, m_depthImageMemory, nullptr);
            functions.vkDestroyImage(m_device, m_depthImage, nullptr);
            m_depthImage = VK_NULL_HANDLE;
            m_depthImageMemory = VK_NULL_HANDLE;
            return false;
        }
        
        // ============================================================================
        // Step 3: Create depth image view
        // Reference: UE5 FVulkanSurface::CreateViewAlias
        // ============================================================================
        
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        
        // If format has stencil component, include it in aspect mask
        if (hasStencilComponent(m_depthFormat)) {
            viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        
        result = functions.vkCreateImageView(m_device, &viewInfo, nullptr, &m_depthImageView);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create depth image view: " + std::to_string(result));
            destroyDepthResources();
            return false;
        }
        
        MR_LOG_INFO("Depth resources created successfully");
        MR_LOG_INFO("  Size: " + std::to_string(m_swapchainExtent.width) + "x" + 
                   std::to_string(m_swapchainExtent.height));
        MR_LOG_INFO("  Format: " + std::to_string(m_depthFormat));
        MR_LOG_INFO("  Memory: " + std::to_string(memRequirements.size / 1024) + " KB");
        
        return true;
    }
    
    void VulkanDevice::destroyDepthResources() {
        const auto& functions = VulkanAPI::getFunctions();
        
        if (m_depthImageView != VK_NULL_HANDLE) {
            functions.vkDestroyImageView(m_device, m_depthImageView, nullptr);
            m_depthImageView = VK_NULL_HANDLE;
        }
        
        if (m_depthImage != VK_NULL_HANDLE) {
            functions.vkDestroyImage(m_device, m_depthImage, nullptr);
            m_depthImage = VK_NULL_HANDLE;
        }
        
        if (m_depthImageMemory != VK_NULL_HANDLE) {
            functions.vkFreeMemory(m_device, m_depthImageMemory, nullptr);
            m_depthImageMemory = VK_NULL_HANDLE;
        }
        
        MR_LOG_DEBUG("Depth resources destroyed");
    }
    
    VkFormat VulkanDevice::findDepthFormat() {
        // Preferred depth formats in order of preference
        // Reference: UE5 uses D24S8 or D32_FLOAT depending on platform
        TArray<VkFormat> candidates = {
            VK_FORMAT_D32_SFLOAT,           // 32-bit float depth, no stencil
            VK_FORMAT_D32_SFLOAT_S8_UINT,   // 32-bit float depth, 8-bit stencil
            VK_FORMAT_D24_UNORM_S8_UINT     // 24-bit depth, 8-bit stencil (most common)
        };
        
        return findSupportedFormat(
            candidates,
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }
    
    VkFormat VulkanDevice::findSupportedFormat(const TArray<VkFormat>& candidates,
                                               VkImageTiling tiling,
                                               VkFormatFeatureFlags features) {
        const auto& functions = VulkanAPI::getFunctions();
        
        for (VkFormat format : candidates) {
            VkFormatProperties props;
            functions.vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);
            
            if (tiling == VK_IMAGE_TILING_LINEAR && 
                (props.linearTilingFeatures & features) == features) {
                return format;
            } else if (tiling == VK_IMAGE_TILING_OPTIMAL && 
                       (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }
        
        return VK_FORMAT_UNDEFINED;
    }
    
    bool VulkanDevice::hasStencilComponent(VkFormat format) {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || 
               format == VK_FORMAT_D24_UNORM_S8_UINT;
    }
    
    void VulkanDevice::deferBufferDestruction(VkBuffer buffer, VkDeviceMemory memory) {
        if (buffer == VK_NULL_HANDLE) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(m_deferredDestructionMutex);
        
        DeferredBufferDestruction deferred;
        deferred.buffer = buffer;
        deferred.memory = memory;
        deferred.frameCount = DEFERRED_DESTRUCTION_FRAMES;
        m_deferredBufferDestructions.Add(deferred);
        
        MR_LOG_DEBUG("Deferred buffer destruction: " + std::to_string(reinterpret_cast<uint64>(buffer)));
    }
    
    void VulkanDevice::deferImageDestruction(VkImage image, VkImageView imageView, VkDeviceMemory memory) {
        if (image == VK_NULL_HANDLE && imageView == VK_NULL_HANDLE) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(m_deferredDestructionMutex);
        
        DeferredImageDestruction deferred;
        deferred.image = image;
        deferred.imageView = imageView;
        deferred.memory = memory;
        deferred.frameCount = DEFERRED_DESTRUCTION_FRAMES;
        m_deferredImageDestructions.Add(deferred);
        
        MR_LOG_DEBUG("Deferred image destruction: " + std::to_string(reinterpret_cast<uint64>(image)));
    }
    
    void VulkanDevice::processDeferredDestructions() {
        std::lock_guard<std::mutex> lock(m_deferredDestructionMutex);
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Process buffer destructions
        for (int32 i = m_deferredBufferDestructions.Num() - 1; i >= 0; --i) {
            auto& deferred = m_deferredBufferDestructions[i];
            if (deferred.frameCount == 0) {
                // Safe to destroy now
                if (deferred.buffer != VK_NULL_HANDLE) {
                    functions.vkDestroyBuffer(m_device, deferred.buffer, nullptr);
                }
                if (deferred.memory != VK_NULL_HANDLE) {
                    functions.vkFreeMemory(m_device, deferred.memory, nullptr);
                }
                m_deferredBufferDestructions.RemoveAt(i);
            } else {
                deferred.frameCount--;
            }
        }
        
        // Process image destructions
        for (int32 i = m_deferredImageDestructions.Num() - 1; i >= 0; --i) {
            auto& deferred = m_deferredImageDestructions[i];
            if (deferred.frameCount == 0) {
                // Safe to destroy now
                if (deferred.imageView != VK_NULL_HANDLE) {
                    functions.vkDestroyImageView(m_device, deferred.imageView, nullptr);
                }
                if (deferred.image != VK_NULL_HANDLE) {
                    functions.vkDestroyImage(m_device, deferred.image, nullptr);
                }
                if (deferred.memory != VK_NULL_HANDLE) {
                    functions.vkFreeMemory(m_device, deferred.memory, nullptr);
                }
                m_deferredImageDestructions.RemoveAt(i);
            } else {
                deferred.frameCount--;
            }
        }
    }
    
    void VulkanDevice::registerTextureForLayoutTransition(VkImage image, uint32 mipLevels, uint32 arrayLayers) {
        std::lock_guard<std::mutex> lock(m_textureTransitionMutex);
        
        // Check if already registered
        for (const auto& info : m_texturesNeedingTransition) {
            if (info.image == image) {
                return; // Already registered
            }
        }
        
        TextureLayoutInfo info;
        info.image = image;
        info.mipLevels = mipLevels;
        info.arrayLayers = arrayLayers;
        m_texturesNeedingTransition.Add(info);
        
        MR_LOG_DEBUG("Registered texture for layout transition: " + 
                    std::to_string(reinterpret_cast<uint64>(image)));
    }
    
    void VulkanDevice::clearTransitionedTexturesForCmdBuffer(VkCommandBuffer cmdBuffer) {
        std::lock_guard<std::mutex> lock(m_textureTransitionMutex);
        m_transitionedTexturesPerCmdBuffer.erase(cmdBuffer);
    }
    
    void VulkanDevice::executeTextureLayoutTransitions(VkCommandBuffer cmdBuffer) {
        std::lock_guard<std::mutex> lock(m_textureTransitionMutex);
        
        if (m_texturesNeedingTransition.IsEmpty() || cmdBuffer == VK_NULL_HANDLE) {
            return;
        }
        
        // Get or create the set of already-transitioned textures for this command buffer
        std::unordered_set<VkImage>& transitionedSet = m_transitionedTexturesPerCmdBuffer[cmdBuffer];
        
        const auto& functions = VulkanAPI::getFunctions();
        
        TArray<VkImageMemoryBarrier> barriers;
        barriers.Reserve(m_texturesNeedingTransition.Num());
        
        for (const auto& info : m_texturesNeedingTransition) {
            // Skip if already transitioned in this command buffer
            if (transitionedSet.count(info.image) > 0) {
                continue;
            }
            
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            // Use UNDEFINED as oldLayout to tell Vulkan we don't care about previous contents.
            // This is required because Vulkan validation layer tracks layout per-command-buffer,
            // and textures uploaded in a different command buffer need explicit transitions.
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = info.image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = info.mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = info.arrayLayers;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            
            barriers.Add(barrier);
            transitionedSet.insert(info.image);
        }
        
        if (!barriers.IsEmpty()) {
            functions.vkCmdPipelineBarrier(
                cmdBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                static_cast<uint32>(barriers.Num()), barriers.GetData()
            );
            
            MR_LOG_INFO("Executed layout transitions for " + 
                       std::to_string(barriers.Num()) + " textures");
        }
    }
}
