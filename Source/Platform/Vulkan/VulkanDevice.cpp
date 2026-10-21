#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Vulkan/VulkanDescriptorSetLayout.h"
#include "Platform/Vulkan/VulkanDescriptorPoolManager.h"
#include "Platform/Vulkan/VulkanDescriptorSetLayoutCache.h"
#include "Platform/Vulkan/VulkanRenderTargetCache.h"
#include "Platform/Vulkan/VulkanSampler.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Platform/Vulkan/VulkanUtils.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanContextManager.h"
#include "Core/Log.h"
#include "Core/Logging/LogMacros.h"

#include <set>
#include <algorithm>
#include <vector>
#include <unordered_set>
#if PLATFORM_ANDROID
#include <android/native_window.h>
#endif

// GPU-Assisted Validation control:
//   1 = enable GPU-AV (per-instruction shader validation, VERY slow on heavy workloads)
//   0 = disable GPU-AV (Core Check validation only, fast)
// Once all validation errors are resolved, set to 1 for targeted debugging.
#define MONSTER_GPU_AV_ENABLED 0

DEFINE_LOG_CATEGORY_STATIC(LogVulkanRHI, Log, All);

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
        if (m_validationEnabled && !setupDebugMessenger()) {
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
        
        // Step 6c: Create MSAA resources (must be before render pass)
        if (!createMSAAResources()) {
            MR_LOG_ERROR("Failed to create MSAA resources");
            return false;
        }
        
        // Step 7: Create render pass (now includes depth attachment and MSAA)
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
        
        // Step 12: Create descriptor pool manager (multi-descriptor set support, UE5-style)
        m_descriptorPoolManager = MakeUnique<VulkanDescriptorPoolManager>(this);
        if (!m_descriptorPoolManager) {
            MR_LOG_ERROR("Failed to create descriptor pool manager");
            return false;
        }
        MR_LOG_INFO("Descriptor pool manager initialized (multi-descriptor set support)");
        
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
        
        // Step 16: Initialize context manager for parallel rendering (UE5 pattern)
        if (!FVulkanContextManager::Initialize(this, 32)) {
            MR_LOG_ERROR("Failed to initialize Vulkan context manager");
            return false;
        }
        MR_LOG_INFO("Vulkan context manager initialized (max 32 contexts)");
        
        // Step 17: Query device capabilities
        queryCapabilities();
        
        MR_LOG_INFO("Vulkan device initialized successfully");
        return true;
    }
    
    void VulkanDevice::shutdown() {
        if (m_device != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            const VkResult idleResult = functions.vkDeviceWaitIdle(m_device);
            if (idleResult != VK_SUCCESS) {
                MR_LOG_ERROR("Device idle failed: " + std::to_string(idleResult));
            } else {
                std::lock_guard<std::mutex> lock(m_submissionMutex);
                m_completedSubmission = m_lastSubmission;
                m_submissions.clear();
            }
            
            if (idleResult == VK_ERROR_DEVICE_LOST) m_deviceLost = true;
            const VkResult presentation = waitPresentationCompletion(UINT64_MAX); // Final teardown only.
            if (presentation != VK_SUCCESS) MR_LOG_WARNING("Presentation teardown completion unavailable");
            destroyPresentationResources();
            destroyGPUTimestampResources();
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
            
            // Shutdown context manager for parallel rendering
            FVulkanContextManager::Shutdown();
            
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
            
            // Destroy MSAA resources (must be after framebuffers)
            destroyMSAAResources();
            
            // Destroy render pass
            if (m_renderPass != VK_NULL_HANDLE) {
                functions.vkDestroyRenderPass(m_device, m_renderPass, nullptr);
                m_renderPass = VK_NULL_HANDLE;
            }
            if (m_rttRenderPass != VK_NULL_HANDLE) {
                functions.vkDestroyRenderPass(m_device, m_rttRenderPass, nullptr);
                m_rttRenderPass = VK_NULL_HANDLE;
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
            m_descriptorPoolManager.reset();
            
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
            
            // Flush deferred destruction queue before destroying the device.
            // Buffers/images added via deferBufferDestruction() / deferImageDestruction()
            // use DEFERRED_DESTRUCTION_FRAMES (3) frames before actual vkDestroy*.
            // Since the frame loop has stopped, the queue won't drain automatically.
            // Call processDeferredDestructions() enough times to fully drain all entries,
            // otherwise vkDestroyDevice will report VUID-vkDestroyDevice-device-05137.
            for (uint32 i = 0; i < DEFERRED_DESTRUCTION_FRAMES + 1; ++i) {
                processDeferredDestructions();
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
        
        // Explicit shutdown followed by the destructor must be harmless.
#if PLATFORM_ANDROID
        if (m_externalWindow) ANativeWindow_release(static_cast<ANativeWindow*>(m_externalWindow));
#endif
        m_externalWindow = nullptr;
        m_device = VK_NULL_HANDLE; m_instance = VK_NULL_HANDLE; m_surface = VK_NULL_HANDLE;
        m_debugMessenger = VK_NULL_HANDLE; m_commandPool = VK_NULL_HANDLE;
        m_imageAvailableSemaphores.clear(); m_inFlightFences.clear(); m_cancelCommandBuffers.clear();
        m_swapchainImageViews.clear(); m_swapchainImages.clear();
        m_enabledFeatures = {};
        VulkanAPI::shutdown();
    }
    
    const RHIDeviceCapabilities& VulkanDevice::getCapabilities() const {
        return m_capabilities;
    }
    
    // Resource creation methods - stub implementations for now
    bool VulkanDevice::getBufferAllocationRequirements(const BufferDesc& desc, uint64& bytes, uint64& alignment) const {
        if (m_device == VK_NULL_HANDLE || !desc.size) return false;
        const auto& functions = VulkanAPI::getFunctions();
        VkBufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        createInfo.size = desc.size;
        createInfo.usage = VulkanUtils::getBufferUsageFlags(desc.usage);
        if ((desc.usage & EResourceUsage::IndirectArgs) != EResourceUsage::None)
            createInfo.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer buffer = VK_NULL_HANDLE;
        if (functions.vkCreateBuffer(m_device, &createInfo, nullptr, &buffer) != VK_SUCCESS) return false;
        VkMemoryRequirements requirements{};
        functions.vkGetBufferMemoryRequirements(m_device, buffer, &requirements);
        functions.vkDestroyBuffer(m_device, buffer, nullptr);
        bytes = requirements.size;
        alignment = requirements.alignment;
        return bytes >= desc.size && alignment != 0;
    }

    bool VulkanDevice::getTextureAllocationRequirements(const TextureDesc& desc, uint64& bytes, uint64& alignment) const {
        if(m_device==VK_NULL_HANDLE||!desc.width||!desc.height||!desc.depth||!desc.mipLevels||!desc.arraySize)return false;
        const auto& functions=VulkanAPI::getFunctions();
        VkImageCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType=desc.depth>1?VK_IMAGE_TYPE_3D:(desc.height==1?VK_IMAGE_TYPE_1D:VK_IMAGE_TYPE_2D);
        info.format=VulkanUtils::getRHIFormatToVulkan(desc.format);info.extent={desc.width,desc.height,desc.depth};
        info.mipLevels=desc.mipLevels;info.arrayLayers=desc.arraySize;info.samples=VK_SAMPLE_COUNT_1_BIT;
        info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=VulkanUtils::getImageUsageFlags(desc.usage);
        info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        VkImage image=VK_NULL_HANDLE;if(functions.vkCreateImage(m_device,&info,nullptr,&image)!=VK_SUCCESS)return false;
        VkMemoryRequirements requirements{};functions.vkGetImageMemoryRequirements(m_device,image,&requirements);
        functions.vkDestroyImage(m_device,image,nullptr);bytes=requirements.size;alignment=requirements.alignment;
        return bytes!=0&&alignment!=0;
    }

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
    
    TSharedPtr<IRHIComputeShader> VulkanDevice::createComputeShader(TSpan<const uint8> bytecode) {
        auto vulkanShader = MakeShared<VulkanComputeShader>(this, bytecode);
        if (!vulkanShader->isValid()) {
            MR_LOG_ERROR("Failed to create Vulkan compute shader");
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
    
    TSharedPtr<IRHIPipelineState> VulkanDevice::createComputePipelineState(const ComputePipelineStateDesc& desc) {
        MR_LOG_INFO("Creating Vulkan compute pipeline state: " + desc.debugName);
        
        if (!desc.computeShader) {
            MR_LOG_ERROR("createComputePipelineState: Null compute shader");
            return nullptr;
        }
        
        auto vulkanShader = dynamic_cast<VulkanComputeShader*>(desc.computeShader.get());
        if (!vulkanShader || !vulkanShader->isValid()) {
            MR_LOG_ERROR("createComputePipelineState: Invalid compute shader");
            return nullptr;
        }
        
        auto vulkanLayout = dynamic_cast<VulkanPipelineLayout*>(desc.pipelineLayout.get());
        if (!vulkanLayout || !vulkanLayout->isValid()) {
            MR_LOG_ERROR("createComputePipelineState: Invalid pipeline layout");
            return nullptr;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice vkDevice = m_device;
        
        // Setup compute pipeline create info
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = vulkanShader->getPipelineStageCreateInfo();
        pipelineInfo.layout = vulkanLayout->getHandle();
        
        VkPipeline vkPipeline = VK_NULL_HANDLE;
        VkResult result = functions.vkCreateComputePipelines(
            vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vkPipeline);
        
        if (result != VK_SUCCESS || vkPipeline == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Failed to create compute pipeline: " + desc.debugName + 
                        " (VkResult=" + std::to_string(result) + ")");
            return nullptr;
        }
        
        // Wrap in VulkanComputePipelineState
        auto pipelineState = MakeShared<VulkanComputePipelineState>(this, desc);
        pipelineState->initializeWithHandles(vkPipeline, vulkanLayout->getHandle());
        
        MR_LOG_INFO("Compute pipeline state created successfully: " + desc.debugName);
        return pipelineState;
    }
    TSharedPtr<IRHISampler> VulkanDevice::createSampler(const SamplerDesc& desc) {
        MR_LOG_DEBUG("Creating Vulkan sampler: " + desc.debugName);
        
        auto sampler = MakeShared<VulkanSampler>(this, desc);
        
        return sampler;
    }
    
    // ========================================================================
    // Descriptor set management (Multi-descriptor set support)
    // ========================================================================
    
    TSharedPtr<IRHIDescriptorSetLayout> VulkanDevice::createDescriptorSetLayout(
        const FDescriptorSetLayoutDesc& desc) {
        
        MR_LOG(LogVulkanRHI, Verbose, "Creating descriptor set layout for set %u with %llu bindings",
               desc.setIndex, static_cast<uint64>(desc.bindings.size()));
        
        auto layout = MakeShared<VulkanDescriptorSetLayout>(this, desc);
        
        if (!layout->isValid()) {
            MR_LOG(LogVulkanRHI, Error, "Failed to create descriptor set layout");
            return nullptr;
        }
        
        return layout;
    }
    
    TSharedPtr<IRHIPipelineLayout> VulkanDevice::createPipelineLayout(
        const FPipelineLayoutDesc& desc) {
        
        MR_LOG(LogVulkanRHI, Verbose, "Creating pipeline layout with %llu descriptor sets",
               static_cast<uint64>(desc.setLayouts.size()));
        
        auto layout = MakeShared<VulkanPipelineLayout>(this, desc);
        
        if (!layout->isValid()) {
            MR_LOG(LogVulkanRHI, Error, "Failed to create pipeline layout");
            return nullptr;
        }
        
        return layout;
    }
    
    TSharedPtr<IRHIDescriptorSet> VulkanDevice::allocateDescriptorSet(
        TSharedPtr<IRHIDescriptorSetLayout> layout) {
        
        if (!layout) {
            MR_LOG(LogVulkanRHI, Error, "allocateDescriptorSet: Invalid layout");
            return nullptr;
        }
        
        auto vulkanLayout = dynamic_cast<VulkanDescriptorSetLayout*>(layout.get());
        if (!vulkanLayout) {
            MR_LOG(LogVulkanRHI, Error, "allocateDescriptorSet: Layout is not a Vulkan layout");
            return nullptr;
        }
        
        if (!m_descriptorPoolManager) {
            MR_LOG(LogVulkanRHI, Error, "allocateDescriptorSet: Descriptor pool manager not initialized");
            return nullptr;
        }
        
        // Allocate descriptor set from pool manager
        auto descriptorSet = m_descriptorPoolManager->allocateDescriptorSet(
            TSharedPtr<VulkanDescriptorSetLayout>(vulkanLayout, [](VulkanDescriptorSetLayout*){}));
        
        if (!descriptorSet) {
            MR_LOG(LogVulkanRHI, Error, "Failed to allocate descriptor set");
            return nullptr;
        }
        
        MR_LOG(LogVulkanRHI, VeryVerbose, "Allocated descriptor set for set %u",
               vulkanLayout->getSetIndex());
        
        return descriptorSet;
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
    
    FGPUCompletionToken VulkanDevice::getLastSubmissionToken() const {
        std::lock_guard<std::mutex> lock(m_submissionMutex);
        return {m_lastSubmission};
    }

    void VulkanDevice::refreshSubmissionCompletionLocked() const {
        // All tracked submissions belong to the same graphics queue. A later
        // signaled fence proves completion of every earlier queue submission.
        for (const auto& submission : m_submissions) {
            const VkResult result = vkGetFenceStatus(m_device, submission.fence);
            if (result == VK_SUCCESS)
                m_completedSubmission = std::max(m_completedSubmission, submission.value);
            else if (result != VK_NOT_READY)
                MR_LOG_ERROR("Submission completion query failed: " + std::to_string(result));
        }
        m_submissions.erase(std::remove_if(m_submissions.begin(), m_submissions.end(),
            [this](const SubmissionRecord& record) { return record.value <= m_completedSubmission; }),
            m_submissions.end());
    }

    bool VulkanDevice::isSubmissionComplete(FGPUCompletionToken token) const {
        std::lock_guard<std::mutex> lock(m_submissionMutex);
        if (!m_device || token.value == 0 || token.value > m_lastSubmission) return false;
        refreshSubmissionCompletionLocked();
        return token.value <= m_completedSubmission;
    }

    bool VulkanDevice::prepareSubmissionFenceReset(VkFence fence) {
        std::lock_guard<std::mutex> lock(m_submissionMutex);
        refreshSubmissionCompletionLocked();
        for (const auto& submission : m_submissions) {
            if (submission.fence == fence) {
                MR_LOG_ERROR("Attempt to reset a fence with an incomplete submission");
                return false;
            }
        }
        return true;
    }

    void VulkanDevice::trackSubmission(VkCommandBuffer commandBuffer, VkFence fence) {
        std::lock_guard<std::mutex> lock(m_submissionMutex);
        if (fence == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Completion tracking requires a real submission fence");
            return;
        }
        const uint64_t value = ++m_lastSubmission;
        m_lastSubmitResult = VK_SUCCESS;
        m_submissions.push_back({value, fence});
        for (auto& record : m_timestampRecords) {
            if (record.commandBuffer == commandBuffer && record.submission == 0)
                record.submission = value;
        }
    }

    bool VulkanDevice::supportsGPUTimestamps() const {
        return m_device != VK_NULL_HANDLE && m_timestampValidBits > 0 &&
               (m_graphicsQueueFamily.flags & VK_QUEUE_COMPUTE_BIT) != 0 &&
               m_deviceProperties.limits.timestampPeriod > 0;
    }

    void VulkanDevice::recordGPUTimestamp(VkCommandBuffer commandBuffer, uint64_t frame,
                                          const std::string& label, bool begin) {
        if (!supportsGPUTimestamps() || commandBuffer == VK_NULL_HANDLE) return;
        std::lock_guard<std::mutex> lock(m_submissionMutex);
        if (!begin) {
            for (auto it = m_timestampRecords.rbegin(); it != m_timestampRecords.rend(); ++it) {
                if (it->commandBuffer == commandBuffer && it->submission == 0 &&
                    it->frame == frame && it->label == label && !it->ended) {
                    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, it->pool, 1);
                    it->ended = true;
                    return;
                }
            }
            MR_LOG_WARNING("Unmatched GPU timestamp end: " + label);
            return;
        }
        // Bounded even if consumers stop polling. Exhaustion means unavailable
        // measurements, not a fabricated CPU fallback or unbounded allocation.
        if (m_timestampRecords.size() >= 4096) {
            MR_LOG_WARNING("GPU timestamp capacity exhausted");
            return;
        }
        VkQueryPool pool = VK_NULL_HANDLE;
        if (!m_availableTimestampPools.empty()) {
            pool = m_availableTimestampPools.back();
            m_availableTimestampPools.pop_back();
        } else {
            VkQueryPoolCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = 2;
            const VkResult result = vkCreateQueryPool(m_device, &info, nullptr, &pool);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("GPU timestamp query pool allocation failed: " + std::to_string(result));
                return;
            }
        }
        m_timestampRecords.push_back({commandBuffer, pool, frame, label, 0, false});
        vkCmdResetQueryPool(commandBuffer, pool, 0, 2);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
    }

    std::vector<FRHIGPUTimestampSample> VulkanDevice::pollGPUTimestamps() {
        std::vector<FRHIGPUTimestampSample> samples;
        if (!supportsGPUTimestamps()) return samples;
        std::lock_guard<std::mutex> lock(m_submissionMutex);
        refreshSubmissionCompletionLocked();
        for (auto it = m_timestampRecords.begin(); it != m_timestampRecords.end();) {
            if (it->submission == 0 || it->submission > m_completedSubmission) { ++it; continue; }
            // An unmatched begin is unavailable. Its query commands have finished,
            // so the pool may now be recycled safely.
            if (it->ended) {
                struct Result { uint64_t ticks; uint64_t available; } results[2]{};
                const VkResult result = vkGetQueryPoolResults(m_device, it->pool, 0, 2,
                    sizeof(results), results, sizeof(Result),
                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
                if (result == VK_NOT_READY || !results[0].available || !results[1].available) {
                    if (result == VK_SUCCESS || result == VK_NOT_READY) { ++it; continue; }
                }
                if (result == VK_SUCCESS && results[0].available && results[1].available) {
                    const uint64_t mask = m_timestampValidBits == 64 ? UINT64_MAX :
                        ((uint64_t(1) << m_timestampValidBits) - 1);
                    const uint64_t delta = (results[1].ticks - results[0].ticks) & mask;
                    samples.push_back({it->frame, it->label,
                        double(delta) * double(m_deviceProperties.limits.timestampPeriod) / 1000000.0});
                } else {
                    MR_LOG_ERROR("GPU timestamp resolve failed: " + std::to_string(result));
                }
            }
            m_availableTimestampPools.push_back(it->pool);
            it = m_timestampRecords.erase(it);
        }
        return samples;
    }

    void VulkanDevice::discardUnsubmittedTimestamps(VkCommandBuffer commandBuffer) {
        std::lock_guard<std::mutex> lock(m_submissionMutex);
        for (auto it = m_timestampRecords.begin(); it != m_timestampRecords.end();) {
            if (it->commandBuffer == commandBuffer && it->submission == 0) {
                m_availableTimestampPools.push_back(it->pool);
                it = m_timestampRecords.erase(it);
            } else ++it;
        }
    }

    void VulkanDevice::destroyGPUTimestampResources() {
        // Called only after device idle at shutdown.
        std::lock_guard<std::mutex> lock(m_submissionMutex);
        for (const auto& record : m_timestampRecords) vkDestroyQueryPool(m_device, record.pool, nullptr);
        for (const auto pool : m_availableTimestampPools) vkDestroyQueryPool(m_device, pool, nullptr);
        m_timestampRecords.clear();
        m_availableTimestampPools.clear();
        m_submissions.clear();
        m_timestampValidBits = 0;
    }

    void VulkanDevice::waitForIdle() {
        if (m_device != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            const VkResult idleResult = functions.vkDeviceWaitIdle(m_device);
            if (idleResult != VK_SUCCESS) {
                MR_LOG_ERROR("Device idle failed: " + std::to_string(idleResult));
            } else {
                std::lock_guard<std::mutex> lock(m_submissionMutex);
                m_completedSubmission = m_lastSubmission;
                m_submissions.clear();
            }
        }
    }
    
    EFrameResult VulkanDevice::ClassifyFrameResult(VkResult result) {
        switch (result) {
            case VK_SUCCESS: return EFrameResult::Success;
            case VK_NOT_READY: return EFrameResult::NotReady;
            case VK_TIMEOUT: return EFrameResult::Timeout;
            case VK_ERROR_OUT_OF_DATE_KHR: return EFrameResult::OutOfDate;
            case VK_SUBOPTIMAL_KHR: return EFrameResult::Suboptimal;
            case VK_ERROR_SURFACE_LOST_KHR: return EFrameResult::SurfaceLost;
            case VK_ERROR_DEVICE_LOST: return EFrameResult::DeviceLost;
            default: return EFrameResult::Error;
        }
    }

    FFrameSubmitResult VulkanDevice::acquireFrame(uint64 timeoutNs) {
        const auto& f = VulkanAPI::getFunctions();
        timeoutNs = std::min<uint64>(timeoutNs, 5000000);
        if (!m_frameAcquired) {
            m_frameResult = {};
            m_frameResult.surfaceGeneration = m_surfaceGeneration;
            if (m_deviceLost) { m_frameResult.status = EFrameResult::DeviceLost; return m_frameResult; }
            if (!m_swapchain || m_presentationStopped) return m_frameResult;
            const VkFence frameFence = getInFlightFence(m_currentFrame);
            const VkResult wait = [&] { MonsterEngine::Profiling::Scope scope("vulkan.frame_fence", m_profilingContext); const VkResult observed = f.vkWaitForFences(m_device, 1, &frameFence, VK_TRUE, timeoutNs); scope.SetValue(MonsterEngine::Profiling::EncodeSignedValue(observed)); return observed; }();
            if (wait != VK_SUCCESS) {
                m_frameResult.acquireResult = wait;
                m_frameResult.status = m_frameResult.acquireStatus = ClassifyFrameResult(wait);
                if (wait == VK_ERROR_DEVICE_LOST) m_deviceLost = true;
                return m_frameResult;
            }
            // Reset belongs immediately before submit, never before acquisition.
            const VkResult acquire = [&] { MonsterEngine::Profiling::Scope scope("vulkan.acquire", m_profilingContext); const VkResult observed = f.vkAcquireNextImageKHR(m_device, m_swapchain, timeoutNs,
                getImageAvailableSemaphore(m_currentFrame), VK_NULL_HANDLE, &m_currentImageIndex); scope.SetValue(MonsterEngine::Profiling::EncodeSignedValue(observed)); return observed; }();
            m_frameResult.acquireResult = acquire;
            m_frameResult.status = m_frameResult.acquireStatus = ClassifyFrameResult(acquire);
            if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
                if (acquire == VK_ERROR_DEVICE_LOST) m_deviceLost = true;
                return m_frameResult;
            }
            m_frameAcquired = true;
            m_frameResult.acquired = true;
        }
        // An acquired semaphore/image remains owned when this bounded wait times out.
        if (m_currentImageIndex >= m_imagesInFlight.size()) {
            m_frameResult.status = EFrameResult::Error; return m_frameResult;
        }
        const VkFence imageFence = m_imagesInFlight[m_currentImageIndex];
        if (imageFence) {
            const VkResult wait = [&] { MonsterEngine::Profiling::Scope scope("vulkan.image_fence", m_profilingContext); const VkResult observed = f.vkWaitForFences(m_device, 1, &imageFence, VK_TRUE, timeoutNs); scope.SetValue(MonsterEngine::Profiling::EncodeSignedValue(observed)); return observed; }();
            if (wait != VK_SUCCESS) {
                if (wait == VK_ERROR_DEVICE_LOST) m_deviceLost = true;
                m_frameResult.status = ClassifyFrameResult(wait); return m_frameResult;
            }
        }
        if (m_presentMaintenanceEnabled && m_currentImageIndex < m_presentFences.size() &&
            m_presentFencePending[m_currentImageIndex]) {
            const VkResult wait = [&] { MonsterEngine::Profiling::Scope scope("vulkan.present_fence", m_profilingContext); const VkResult observed = f.vkWaitForFences(m_device, 1, &m_presentFences[m_currentImageIndex], VK_TRUE, timeoutNs); scope.SetValue(MonsterEngine::Profiling::EncodeSignedValue(observed)); return observed; }();
            if (wait != VK_SUCCESS) {
                if (wait == VK_ERROR_DEVICE_LOST) m_deviceLost = true;
                m_frameResult.status = ClassifyFrameResult(wait); return m_frameResult;
            }
            m_presentFencePending[m_currentImageIndex] = false;
        }
        m_frameResult.status = m_frameResult.acquireStatus;
        // Only now is frame-local descriptor reuse safe.
        if (m_descriptorPoolManager) m_descriptorPoolManager->beginFrame(m_currentFrame);
        if (m_descriptorSetCache) m_descriptorSetCache->Reset(m_currentFrame);
        return m_frameResult;
    }

    FFrameSubmitResult VulkanDevice::beginFrame(uint64 timeoutNs) {
        const auto acquired = acquireFrame(timeoutNs);
        if (acquired.status != EFrameResult::Success && acquired.status != EFrameResult::Suboptimal) return acquired;
        if (!m_commandListContext || !m_commandListContext->prepareForNewFrame()) {
            m_frameResult.status = EFrameResult::NotReady;
        }
        return m_frameResult;
    }

    void VulkanDevice::RecordSubmitFailure(VkResult result, VkFence fence) {
        MonsterEngine::Profiling::EventWithContext("vulkan.submit_status",m_profilingContext,MonsterEngine::Profiling::EncodeSignedValue(result));
        m_lastSubmitResult = result;
        if (result == VK_ERROR_DEVICE_LOST) { m_deviceLost = true; m_presentationStopped = true; return; }
        // A failed submit attaches no work to the reset fence. Replace frame fences
        // with signaled fences, so a failed recording cannot strand a ring slot.
        if (!fence || !prepareSubmissionFenceReset(fence)) return;
        const auto& f = VulkanAPI::getFunctions();
        for (size_t i = 0; i < m_inFlightFences.size(); ++i) {
            if (m_inFlightFences[i] != fence) continue;
            VkFence replacement = VK_NULL_HANDLE;
            VkFenceCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if (f.vkCreateFence(m_device, &info, nullptr, &replacement) != VK_SUCCESS) { m_presentationStopped = true; return; }
            for (auto& imageFence : m_imagesInFlight) if (imageFence == fence) imageFence = VK_NULL_HANDLE;
            f.vkDestroyFence(m_device, fence, nullptr);
            m_inFlightFences[i] = replacement;
            return;
        }
    }

    VkResult VulkanDevice::queueCurrentPresent() {
        const auto& f = VulkanAPI::getFunctions();
        const VkSemaphore signal = getRenderFinishedSemaphore(m_currentImageIndex);
        VkPresentInfoKHR info{}; info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1; info.pWaitSemaphores = &signal;
        info.swapchainCount = 1; info.pSwapchains = &m_swapchain;
        info.pImageIndices = &m_currentImageIndex;
#ifdef VK_EXT_swapchain_maintenance1
        VkSwapchainPresentFenceInfoEXT fenceInfo{};
        if (m_presentMaintenanceEnabled) {
            VkFence& fence = m_presentFences[m_currentImageIndex];
            const VkResult reset = [&] { MonsterEngine::Profiling::Scope scope("vulkan.present_fence_reset",m_profilingContext);const auto observed=f.vkResetFences(m_device, 1, &fence);scope.SetValue(MonsterEngine::Profiling::EncodeSignedValue(observed));return observed;}();
            if (reset != VK_SUCCESS) { m_presentationStopped = true; m_frameAcquired = false; return reset; }
            fenceInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
            fenceInfo.swapchainCount = 1; fenceInfo.pFences = &fence;
            info.pNext = &fenceInfo;
        }
#endif
        const VkResult result = [&] { MonsterEngine::Profiling::Scope scope("vulkan.queue_present", m_profilingContext); const VkResult observed = f.vkQueuePresentKHR(m_presentQueue, &info); scope.SetValue(MonsterEngine::Profiling::EncodeSignedValue(observed)); return observed; }();
        // Even OUT_OF_DATE/SURFACE_LOST queue the semaphore wait as specified by WSI.
        if (m_presentMaintenanceEnabled && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR ||
            result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR))
            m_presentFencePending[m_currentImageIndex] = true;
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) m_presentationStopped = true;
        if (result == VK_ERROR_DEVICE_LOST) m_deviceLost = true;
        m_frameAcquired = false;
        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        return result;
    }

    void VulkanDevice::present() { (void)presentFrame(); }

    FFrameSubmitResult VulkanDevice::presentFrame() {
        if (!m_frameAcquired || m_deviceLost || m_presentationStopped || !m_commandListContext) {
            m_frameResult.submitted = false; m_frameResult.token = {};
            m_frameResult.status = m_deviceLost ? EFrameResult::DeviceLost : EFrameResult::NotReady;
            return m_frameResult;
        }
        VkSemaphore wait = getImageAvailableSemaphore(m_currentFrame);
        VkSemaphore signal = getRenderFinishedSemaphore(m_currentImageIndex);
        const auto before = getLastSubmissionToken();
        if (!m_commandListContext->submitCommands(&wait, 1, &signal, 1)) {
            m_frameResult.submitted = false; m_frameResult.token = {};
            m_frameResult.submitResult = m_lastSubmitResult;
            m_frameResult.status = m_frameResult.submitStatus = ClassifyFrameResult(m_lastSubmitResult);
            return m_frameResult;
        }
        const auto token = getLastSubmissionToken();
        if (token.value <= before.value) { m_frameResult.status = EFrameResult::Error; return m_frameResult; }
        m_lastSubmitResult = VK_SUCCESS;
        m_frameResult.submitResult = VK_SUCCESS;
        m_frameResult.submitStatus = EFrameResult::Success;
        m_frameResult.submitted = true; m_frameResult.token = token;
        m_profilingContext.submissionId = token.value;
        m_imagesInFlight[m_currentImageIndex] = getInFlightFence(m_currentFrame);
        const VkResult result = queueCurrentPresent();
        m_frameResult.presentResult = result;
        m_frameResult.status = m_frameResult.presentStatus = ClassifyFrameResult(result);
        if (m_descriptorSetLayoutCache) m_descriptorSetLayoutCache->GarbageCollect(m_currentFrame, 120);
        processDeferredDestructions();
        return m_frameResult;
    }

    FFrameSubmitResult VulkanDevice::cancelFrame() {
        if (!m_frameAcquired || m_deviceLost || m_cancelCommandBuffers.size() <= m_currentFrame) {
            FFrameSubmitResult result; result.status = m_deviceLost ? EFrameResult::DeviceLost : EFrameResult::NotReady; return result;
        }
        const auto ready = acquireFrame(1000000);
        if (ready.status != EFrameResult::Success && ready.status != EFrameResult::Suboptimal) return ready;
        const auto& f = VulkanAPI::getFunctions();
        const VkCommandBuffer command = m_cancelCommandBuffers[m_currentFrame];
        VkResult result = f.vkResetCommandBuffer(command, 0);
        VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (result == VK_SUCCESS) result = f.vkBeginCommandBuffer(command, &begin);
        if (result == VK_SUCCESS) {
            VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_swapchainImages[m_currentImageIndex];
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            f.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            result = f.vkEndCommandBuffer(command);
        }
        VkFence fence = getInFlightFence(m_currentFrame);
        if (result == VK_SUCCESS && !prepareSubmissionFenceReset(fence)) result = VK_NOT_READY;
        VkSemaphore wait = getImageAvailableSemaphore(m_currentFrame), signal = getRenderFinishedSemaphore(m_currentImageIndex);
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &wait; submit.pWaitDstStageMask = &stage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &signal;
        if (result == VK_SUCCESS) result = f.vkResetFences(m_device, 1, &fence);
        if (result == VK_SUCCESS) result = [&] { MonsterEngine::Profiling::Scope scope("vulkan.cancel_submit", m_profilingContext); const auto observed=f.vkQueueSubmit(m_graphicsQueue, 1, &submit, fence);scope.SetValue(MonsterEngine::Profiling::EncodeSignedValue(observed));return observed;}();
        m_frameResult.submitResult = result;
        m_frameResult.submitStatus = ClassifyFrameResult(result);
        m_frameResult.token = {}; m_frameResult.submitted = false;
        if (result != VK_SUCCESS) {
            RecordSubmitFailure(result, fence); m_frameResult.status = ClassifyFrameResult(result); return m_frameResult;
        }
        trackSubmission(command, fence);
        m_frameResult.submitted = true; m_frameResult.token = getLastSubmissionToken();
        m_profilingContext.submissionId=m_frameResult.token.value;
        MonsterEngine::Profiling::EventWithContext("vulkan.cancel_submitted",m_profilingContext);
        m_imagesInFlight[m_currentImageIndex] = fence;
        const VkResult presented = queueCurrentPresent();
        m_frameResult.presentResult = presented;
        m_frameResult.status = m_frameResult.presentStatus = ClassifyFrameResult(presented);
        return m_frameResult;
    }
    void VulkanDevice::getMemoryStats(uint64& usedBytes, uint64& availableBytes) {
        usedBytes = 0;
        availableBytes = 0;
        if (m_memoryManager) {
            FVulkanMemoryManager::FMemoryStats stats;
            m_memoryManager->GetMemoryStats(stats);
            usedBytes = static_cast<uint64>(stats.TotalAllocated);
            availableBytes = stats.PooledReserved > stats.PooledUsed
                ? static_cast<uint64>(stats.PooledReserved - stats.PooledUsed) : 0;
        }
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
        const auto& functions = VulkanAPI::getFunctions();
        uint32 loaderVersion = VK_API_VERSION_1_0;
        if (VulkanAPI::enumerateInstanceVersion(functions, loaderVersion) != VK_SUCCESS ||
            loaderVersion < VK_API_VERSION_1_1) return false;
        m_validationEnabled = createInfo.enableValidation;
        m_runningUnderRenderDoc = false;
#if PLATFORM_WINDOWS
        m_runningUnderRenderDoc = GetModuleHandleA("renderdoc.dll") != nullptr;
        for (const char* name : {"DISABLE_VK_LAYER_TENCENT_wegame_cross_overlay_1",
             "DISABLE_VK_LAYER_VALVE_steam_overlay_1", "DISABLE_VK_LAYER_VALVE_steam_fossilize_1",
             "DISABLE_VK_LAYER_EOS_Overlay_1", "DISABLE_VK_LAYER_ROCKSTAR_GAMES_social_club_1"})
            _putenv_s(name, "1");
#endif
        const bool validation = m_validationEnabled && !m_runningUnderRenderDoc;
        if (validation && !checkValidationLayerSupport()) return false;
        m_validationEnabled = validation;
        auto extensions = getRequiredExtensions(validation);
        uint32 count = 0;
        if (functions.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS || count > 4096) return false;
        std::vector<VkExtensionProperties> available(count);
        if (functions.vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data()) != VK_SUCCESS) return false;
        auto supported = [&](const char* name) {
            return std::any_of(available.begin(), available.end(), [&](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
        };
        for (const char* extension : extensions) if (!supported(extension)) return false;
#ifdef VK_EXT_surface_maintenance1
        if (supported(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) && supported(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME)) {
            extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
            extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
            m_surfaceMaintenanceEnabled = true;
        }
#endif
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = createInfo.applicationName.c_str();
        app.applicationVersion = createInfo.applicationVersion;
        app.pEngineName = "MonsterRender";
        app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<uint32>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        if (validation) {
            info.enabledLayerCount = g_validationLayerCount;
            info.ppEnabledLayerNames = g_validationLayers;
        }
        if (functions.vkCreateInstance(&info, nullptr, &m_instance) != VK_SUCCESS) return false;
        return VulkanAPI::loadInstanceFunctions(m_instance);
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
        if (!createInfo.windowHandle) return true;
        const VkResult result = VulkanUtils::createSurface(m_instance, createInfo.windowHandle, &m_surface);
        if (result != VK_SUCCESS) return false;
#if PLATFORM_ANDROID
        m_externalWindow = createInfo.windowHandle;
        ANativeWindow_acquire(static_cast<ANativeWindow*>(m_externalWindow));
#endif
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
        const auto& f = VulkanAPI::getFunctions();
        m_graphicsQueueFamily = findQueueFamilies(m_physicalDevice, VK_NULL_HANDLE, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
        m_presentQueueFamily = m_surface ? findQueueFamilies(m_physicalDevice, m_surface, 0) : m_graphicsQueueFamily;
        if (m_graphicsQueueFamily.familyIndex == VK_QUEUE_FAMILY_IGNORED ||
            m_presentQueueFamily.familyIndex == VK_QUEUE_FAMILY_IGNORED) return false;
        std::vector<VkDeviceQueueCreateInfo> queues;
        const float priority = 1.0f;
        for (uint32 family : std::set<uint32>{m_graphicsQueueFamily.familyIndex, m_presentQueueFamily.familyIndex}) {
            VkDeviceQueueCreateInfo q{};
            q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &priority;
            queues.push_back(q);
        }
        VkPhysicalDeviceFeatures requested{};
        // Sort keys use two uint32 words. Preserve optional desktop shader support
        // without imposing shaderInt64 on the shared renderer.
        requested.shaderInt64 = m_deviceFeatures.shaderInt64;
        requested.samplerAnisotropy = m_deviceFeatures.samplerAnisotropy;
        requested.fillModeNonSolid = m_deviceFeatures.fillModeNonSolid;
        requested.geometryShader = m_deviceFeatures.geometryShader;
        requested.tessellationShader = m_deviceFeatures.tessellationShader;
        uint32 count = 0;
        if (f.vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, nullptr) != VK_SUCCESS || count > 4096) return false;
        std::vector<VkExtensionProperties> available(count);
        if (f.vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, available.data()) != VK_SUCCESS) return false;
        std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        void* chain = nullptr;
#ifdef VK_EXT_swapchain_maintenance1
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{};
        maintenance.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        const bool extensionSupported = std::any_of(available.begin(), available.end(), [](const auto& e) {
            return std::strcmp(e.extensionName, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) == 0;
        });
        if (extensionSupported && f.vkGetPhysicalDeviceFeatures2) {
            VkPhysicalDeviceFeatures2 query{};
            query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            query.pNext = &maintenance;
            f.vkGetPhysicalDeviceFeatures2(m_physicalDevice, &query);
            m_presentMaintenanceSupported = maintenance.swapchainMaintenance1 == VK_TRUE;
            if (m_presentMaintenanceSupported && m_surfaceMaintenanceEnabled) {
                extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
                chain = &maintenance;
            }
        }
#endif
        VkDeviceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.pNext = chain;
        info.queueCreateInfoCount = static_cast<uint32>(queues.size());
        info.pQueueCreateInfos = queues.data();
        info.pEnabledFeatures = &requested;
        info.enabledExtensionCount = static_cast<uint32>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        m_requestedFeatures = requested;
        if (f.vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device) != VK_SUCCESS) return false;
        m_enabledFeatures = requested;
        m_presentMaintenanceEnabled = chain != nullptr;
        if (!VulkanAPI::loadDeviceFunctions(m_device)) return false;
        f.vkGetDeviceQueue(m_device, m_graphicsQueueFamily.familyIndex, 0, &m_graphicsQueue);
        f.vkGetDeviceQueue(m_device, m_presentQueueFamily.familyIndex, 0, &m_presentQueue);
        return m_graphicsQueue && m_presentQueue;
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
        VkSurfaceCapabilitiesKHR capabilities{};
        if (functions.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities) != VK_SUCCESS) return false;
        
        // Use std::vector for Vulkan API compatibility
        uint32 formatCount = 0;
        if (functions.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr) != VK_SUCCESS || !formatCount || formatCount > 4096) return false;
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        if (functions.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data()) != VK_SUCCESS) return false;
        
        uint32 presentModeCount = 0;
        if (functions.vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr) != VK_SUCCESS || !presentModeCount || presentModeCount > 4096) return false;
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        if (functions.vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data()) != VK_SUCCESS) return false;
        
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
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (!m_presentationVSync && std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != presentModes.end()) presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        for (const auto& availablePresentMode : presentModes) {
            if (presentMode != VK_PRESENT_MODE_IMMEDIATE_KHR && availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = availablePresentMode;
                break;
            }
        }
        
        // Choose swap extent
        // Reference: UE5 FVulkanSwapChain::Create - always use requested size, clamped to capabilities
        VkExtent2D extent;
        extent.width = createInfo.windowWidth;
        extent.height = createInfo.windowHeight;
        
        // Log surface capabilities for debugging
        MR_LOG_INFO("Surface capabilities - currentExtent: " + std::to_string(capabilities.currentExtent.width) + "x" + std::to_string(capabilities.currentExtent.height) +
                    ", minExtent: " + std::to_string(capabilities.minImageExtent.width) + "x" + std::to_string(capabilities.minImageExtent.height) +
                    ", maxExtent: " + std::to_string(capabilities.maxImageExtent.width) + "x" + std::to_string(capabilities.maxImageExtent.height));
        MR_LOG_INFO("Requested window size: " + std::to_string(createInfo.windowWidth) + "x" + std::to_string(createInfo.windowHeight));
        
        if (capabilities.currentExtent.width != UINT32_MAX) extent = capabilities.currentExtent;
        // Clamp to valid range
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        
        MR_LOG_INFO("Final swapchain extent: " + std::to_string(extent.width) + "x" + std::to_string(extent.height));
        
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
        m_swapchainReadbackSupported = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        if (m_swapchainReadbackSupported) createInfoSwapchain.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        
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
        
        MR_LOG_INFO("Surface transform: current=" + std::to_string(capabilities.currentTransform) + ", supported=" + std::to_string(capabilities.supportedTransforms));
        // The renderer records upright pixels. Ask WSI to apply the Surface's
        // display rotation instead of claiming the app already pre-rotated them.
        if (!(capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
            MR_LOG_ERROR("Surface does not support upright identity presentation; renderer pre-rotation unavailable");
            return false;
        }
        createInfoSwapchain.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        createInfoSwapchain.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
            if (capabilities.supportedCompositeAlpha & alpha) { createInfoSwapchain.compositeAlpha = alpha; break; }
        }
        m_configuredPresentMode = presentMode;
        createInfoSwapchain.presentMode = presentMode;
        createInfoSwapchain.clipped = VK_TRUE;
        createInfoSwapchain.oldSwapchain = VK_NULL_HANDLE;
        
        VkResult result = functions.vkCreateSwapchainKHR(m_device, &createInfoSwapchain, nullptr, &m_swapchain);
        
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create swap chain! Result: " + std::to_string(result));
            return false;
        }
        
        // Get swapchain images
        if (functions.vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr) != VK_SUCCESS || !imageCount || imageCount > 64) return false;
        if (m_presentationBudget && imageCount > m_presentationReservedImages) {
            MR_LOG_ERROR("WSI image count exceeds admitted policy: actual=" + std::to_string(imageCount) + ", reserved=" + std::to_string(m_presentationReservedImages));
            functions.vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
            return false;
        }
        m_swapchainImages.resize(imageCount);
        if (functions.vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data()) != VK_SUCCESS) return false;
        
        m_swapchainImageFormat = surfaceFormat.format;
        m_swapchainColorSpace = surfaceFormat.colorSpace;
        MR_LOG_INFO("Selected swapchain format=" + std::to_string(surfaceFormat.format) +
            ", colorSpace=" + std::to_string(surfaceFormat.colorSpace));
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
        // Attachment Descriptions (UE5-style with MSAA support)
        // Reference: UE5 FVulkanRenderPass::Create
        // ============================================================================
        
        // Use std::vector for Vulkan API compatibility
        std::vector<VkAttachmentDescription> attachments;
        
        // Check if MSAA is enabled
        bool msaaEnabled = (m_msaaSampleCount != VK_SAMPLE_COUNT_1_BIT);
        
        if (msaaEnabled) {
            // MSAA path: 3 attachments (MSAA color, MSAA depth, resolve)
            
            // Attachment 0: MSAA color attachment
            VkAttachmentDescription msaaColorAttachment{};
            msaaColorAttachment.format = m_swapchainImageFormat;
            msaaColorAttachment.samples = m_msaaSampleCount;
            msaaColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            msaaColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // MSAA samples don't need to be stored
            msaaColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            msaaColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            msaaColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            msaaColorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments.push_back(msaaColorAttachment);
            
            // Attachment 1: MSAA depth attachment
            VkAttachmentDescription msaaDepthAttachment{};
            msaaDepthAttachment.format = m_depthFormat;
            msaaDepthAttachment.samples = m_msaaSampleCount;
            msaaDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            msaaDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            msaaDepthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            msaaDepthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            msaaDepthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            msaaDepthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments.push_back(msaaDepthAttachment);
            
            // Attachment 2: Resolve attachment (swapchain image)
            VkAttachmentDescription resolveAttachment{};
            resolveAttachment.format = m_swapchainImageFormat;
            resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            attachments.push_back(resolveAttachment);
            
            MR_LOG_INFO("Creating MSAA render pass with " + std::to_string(m_msaaSampleCount) + "x samples");
        } else {
            // Non-MSAA path: 2 attachments (color, depth)
            
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
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments.push_back(depthAttachment);
        }
        
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
        
        // Resolve attachment reference (only for MSAA)
        VkAttachmentReference resolveAttachmentRef{};
        resolveAttachmentRef.attachment = 2;
        resolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        
        // ============================================================================
        // Subpass Description
        // ============================================================================
        
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;
        subpass.pResolveAttachments = msaaEnabled ? &resolveAttachmentRef : nullptr;
        
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
        
        // Check if MSAA is enabled
        bool msaaEnabled = (m_msaaSampleCount != VK_SAMPLE_COUNT_1_BIT);
        
        // Create one framebuffer for each swapchain image view
        // Reference: UE5 FVulkanFramebuffer::Create
        m_swapchainFramebuffers.resize(m_swapchainImageViews.size());
        
        for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
            std::vector<VkImageView> attachments;
            
            if (msaaEnabled) {
                // MSAA path: [0] MSAA color, [1] MSAA depth, [2] resolve (swapchain)
                attachments = {
                    m_msaaColorImageView,      // Attachment 0: MSAA Color
                    m_msaaDepthImageView,      // Attachment 1: MSAA Depth
                    m_swapchainImageViews[i]   // Attachment 2: Resolve (swapchain)
                };
            } else {
                // Non-MSAA path: [0] color (swapchain), [1] depth
                attachments = {
                    m_swapchainImageViews[i],  // Attachment 0: Color
                    m_depthImageView           // Attachment 1: Depth
                };
            }
            
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
                   " framebuffers successfully" + (msaaEnabled ? " (with MSAA)" : " (with depth attachment)"));
        return true;
    }
    
    VkFramebuffer VulkanDevice::getCurrentFramebuffer() const {
        if (m_currentImageIndex >= m_swapchainFramebuffers.size()) {
            MR_LOG_ERROR("Current image index out of range: " + std::to_string(m_currentImageIndex));
            return VK_NULL_HANDLE;
        }
        return m_swapchainFramebuffers[m_currentImageIndex];
    }
    
    VkImageView VulkanDevice::getCurrentSwapchainImageView() const {
        if (m_currentImageIndex >= m_swapchainImageViews.size()) {
            MR_LOG_ERROR("Current image index out of range: " + std::to_string(m_currentImageIndex));
            return VK_NULL_HANDLE;
        }
        return m_swapchainImageViews[m_currentImageIndex];
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
        
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = m_commandPool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
        m_cancelCommandBuffers.assign(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
        if (functions.vkAllocateCommandBuffers(m_device, &allocate, m_cancelCommandBuffers.data()) != VK_SUCCESS) return false;
        return createImageSynchronization();
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
        #if PLATFORM_ANDROID
            extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
            extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
        #elif PLATFORM_WINDOWS
            extensions.push_back("VK_KHR_surface");
            extensions.push_back("VK_KHR_win32_surface");
        #elif PLATFORM_LINUX
            extensions.push_back("VK_KHR_surface");
            extensions.push_back("VK_KHR_xlib_surface");
        #endif
        
        // Debug extensions
        if (enableValidation) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            // VK_EXT_validation_features is REQUIRED to request GPU-Assisted Validation
            // (VkValidationFeaturesEXT) at device creation. It is provided by the validation
            // layer, so we only add it when the validation layer will actually be loaded.

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
        QueueFamily graphicsFamily = findQueueFamilies(device, VK_NULL_HANDLE, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
        bool queueFamilySupport = graphicsFamily.familyIndex != VK_QUEUE_FAMILY_IGNORED;
        if (m_surface) queueFamilySupport = queueFamilySupport &&
            findQueueFamilies(device, m_surface, 0).familyIndex != VK_QUEUE_FAMILY_IGNORED;
        
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
        
        VkFormatProperties rgba{};
        functions.vkGetPhysicalDeviceFormatProperties(device, VK_FORMAT_R8G8B8A8_UNORM, &rgba);
        const VkFormatFeatureFlags requiredFormat = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        const auto& limits = deviceProperties.limits;
        return deviceProperties.apiVersion >= VK_API_VERSION_1_1 &&
            limits.maxComputeWorkGroupInvocations >= 256 && limits.maxComputeWorkGroupSize[0] >= 256 &&
            limits.maxComputeWorkGroupSize[1] >= 16 &&
            (rgba.optimalTilingFeatures & requiredFormat) == requiredFormat &&
            queueFamilySupport && extensionSupport && swapChainAdequate;
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
            if (queueFamilyProperties.queueCount > 0 && (queueFamilyProperties.queueFlags & flags) == flags) {
                family.familyIndex = i;
                family.queueCount = queueFamilyProperties.queueCount;
                family.flags = queueFamilyProperties.queueFlags;
                
                // Check present support if we have a surface
                if (surface != VK_NULL_HANDLE) {
                    VkBool32 presentSupport = false;
                    const VkResult supported = functions.vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
                    family.supportsPresentToSurface = supported == VK_SUCCESS && presentSupport == VK_TRUE;
                    if (!family.supportsPresentToSurface) { family = {}; continue; }
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
        m_capabilities.supportsMultiDrawIndirect = m_enabledFeatures.multiDrawIndirect == VK_TRUE;
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, queues.data());
        m_timestampValidBits = m_graphicsQueueFamily.familyIndex < queues.size() ?
            queues[m_graphicsQueueFamily.familyIndex].timestampValidBits : 0;
        m_capabilities.supportsTimestampQuery = supportsGPUTimestamps();
        
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
    
    VkSampleCountFlagBits VulkanDevice::getMaxUsableSampleCount() {
        VkSampleCountFlags counts = m_deviceProperties.limits.framebufferColorSampleCounts & 
                                    m_deviceProperties.limits.framebufferDepthSampleCounts;
        
        // Prefer 8x MSAA, fall back to lower if not supported
        if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
        if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
        if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }
        
        return VK_SAMPLE_COUNT_1_BIT;
    }
    
    bool VulkanDevice::createMSAAResources() {
        const auto& functions = VulkanAPI::getFunctions();
        
        // Get max supported sample count
        m_msaaSampleCount = getMaxUsableSampleCount();
#if PLATFORM_ANDROID
        m_msaaSampleCount = VK_SAMPLE_COUNT_1_BIT; // Mobile splat resolve does not require multisampling.
#endif
        
        if (m_msaaSampleCount == VK_SAMPLE_COUNT_1_BIT) {
            MR_LOG_INFO("MSAA not supported or disabled");
            return true;
        }
        
        MR_LOG_INFO("Creating MSAA resources with " + std::to_string(m_msaaSampleCount) + "x samples");
        
        // Create MSAA color image
        VkImageCreateInfo colorImageInfo{};
        colorImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorImageInfo.imageType = VK_IMAGE_TYPE_2D;
        colorImageInfo.format = m_swapchainImageFormat;
        colorImageInfo.extent.width = m_swapchainExtent.width;
        colorImageInfo.extent.height = m_swapchainExtent.height;
        colorImageInfo.extent.depth = 1;
        colorImageInfo.mipLevels = 1;
        colorImageInfo.arrayLayers = 1;
        colorImageInfo.samples = m_msaaSampleCount;
        colorImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        colorImageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        colorImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        colorImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        VkResult result = functions.vkCreateImage(m_device, &colorImageInfo, nullptr, &m_msaaColorImage);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create MSAA color image: " + std::to_string(result));
            return false;
        }
        
        // Allocate memory for MSAA color image
        VkMemoryRequirements memRequirements;
        functions.vkGetImageMemoryRequirements(m_device, m_msaaColorImage, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        
        // Find suitable memory type
        uint32 memoryTypeIndex = UINT32_MAX;
        for (uint32 i = 0; i < m_memoryProperties.memoryTypeCount; i++) {
            if ((memRequirements.memoryTypeBits & (1 << i)) &&
                (m_memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memoryTypeIndex = i;
                break;
            }
        }
        
        if (memoryTypeIndex == UINT32_MAX) {
            MR_LOG_ERROR("Failed to find suitable memory type for MSAA color image");
            return false;
        }
        allocInfo.memoryTypeIndex = memoryTypeIndex;
        
        result = functions.vkAllocateMemory(m_device, &allocInfo, nullptr, &m_msaaColorImageMemory);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to allocate MSAA color image memory");
            return false;
        }
        
        functions.vkBindImageMemory(m_device, m_msaaColorImage, m_msaaColorImageMemory, 0);
        
        // Create MSAA color image view
        VkImageViewCreateInfo colorViewInfo{};
        colorViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        colorViewInfo.image = m_msaaColorImage;
        colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorViewInfo.format = m_swapchainImageFormat;
        colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorViewInfo.subresourceRange.baseMipLevel = 0;
        colorViewInfo.subresourceRange.levelCount = 1;
        colorViewInfo.subresourceRange.baseArrayLayer = 0;
        colorViewInfo.subresourceRange.layerCount = 1;
        
        result = functions.vkCreateImageView(m_device, &colorViewInfo, nullptr, &m_msaaColorImageView);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create MSAA color image view");
            return false;
        }
        
        // Create MSAA depth image
        VkImageCreateInfo depthImageInfo{};
        depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
        depthImageInfo.format = m_depthFormat;
        depthImageInfo.extent.width = m_swapchainExtent.width;
        depthImageInfo.extent.height = m_swapchainExtent.height;
        depthImageInfo.extent.depth = 1;
        depthImageInfo.mipLevels = 1;
        depthImageInfo.arrayLayers = 1;
        depthImageInfo.samples = m_msaaSampleCount;
        depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        result = functions.vkCreateImage(m_device, &depthImageInfo, nullptr, &m_msaaDepthImage);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create MSAA depth image");
            return false;
        }
        
        // Allocate memory for MSAA depth image
        functions.vkGetImageMemoryRequirements(m_device, m_msaaDepthImage, &memRequirements);
        allocInfo.allocationSize = memRequirements.size;
        
        memoryTypeIndex = UINT32_MAX;
        for (uint32 i = 0; i < m_memoryProperties.memoryTypeCount; i++) {
            if ((memRequirements.memoryTypeBits & (1 << i)) &&
                (m_memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memoryTypeIndex = i;
                break;
            }
        }
        allocInfo.memoryTypeIndex = memoryTypeIndex;
        
        result = functions.vkAllocateMemory(m_device, &allocInfo, nullptr, &m_msaaDepthImageMemory);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to allocate MSAA depth image memory");
            return false;
        }
        
        functions.vkBindImageMemory(m_device, m_msaaDepthImage, m_msaaDepthImageMemory, 0);
        
        // Create MSAA depth image view
        VkImageViewCreateInfo depthViewInfo{};
        depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewInfo.image = m_msaaDepthImage;
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format = m_depthFormat;
        depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthViewInfo.subresourceRange.baseMipLevel = 0;
        depthViewInfo.subresourceRange.levelCount = 1;
        depthViewInfo.subresourceRange.baseArrayLayer = 0;
        depthViewInfo.subresourceRange.layerCount = 1;
        
        result = functions.vkCreateImageView(m_device, &depthViewInfo, nullptr, &m_msaaDepthImageView);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create MSAA depth image view");
            return false;
        }
        
        MR_LOG_INFO("MSAA resources created successfully (" + std::to_string(m_msaaSampleCount) + "x)");
        return true;
    }
    
    void VulkanDevice::destroyMSAAResources() {
        const auto& functions = VulkanAPI::getFunctions();
        
        if (m_msaaColorImageView != VK_NULL_HANDLE) {
            functions.vkDestroyImageView(m_device, m_msaaColorImageView, nullptr);
            m_msaaColorImageView = VK_NULL_HANDLE;
        }
        if (m_msaaColorImage != VK_NULL_HANDLE) {
            functions.vkDestroyImage(m_device, m_msaaColorImage, nullptr);
            m_msaaColorImage = VK_NULL_HANDLE;
        }
        if (m_msaaColorImageMemory != VK_NULL_HANDLE) {
            functions.vkFreeMemory(m_device, m_msaaColorImageMemory, nullptr);
            m_msaaColorImageMemory = VK_NULL_HANDLE;
        }
        
        if (m_msaaDepthImageView != VK_NULL_HANDLE) {
            functions.vkDestroyImageView(m_device, m_msaaDepthImageView, nullptr);
            m_msaaDepthImageView = VK_NULL_HANDLE;
        }
        if (m_msaaDepthImage != VK_NULL_HANDLE) {
            functions.vkDestroyImage(m_device, m_msaaDepthImage, nullptr);
            m_msaaDepthImage = VK_NULL_HANDLE;
        }
        if (m_msaaDepthImageMemory != VK_NULL_HANDLE) {
            functions.vkFreeMemory(m_device, m_msaaDepthImageMemory, nullptr);
            m_msaaDepthImageMemory = VK_NULL_HANDLE;
        }
        
        m_msaaSampleCount = VK_SAMPLE_COUNT_1_BIT;
        MR_LOG_DEBUG("MSAA resources destroyed");
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
