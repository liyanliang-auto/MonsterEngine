#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandList.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Platform/Vulkan/VulkanUtils.h"
#include "Core/Log.h"

#include <set>
#include <algorithm>

namespace MonsterRender::RHI::Vulkan {
    
    // Required device extensions
    const TArray<const char*> VulkanDevice::s_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    
    // Validation layers
    const TArray<const char*> VulkanDevice::s_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
    
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
        
        // Step 7: Create render pass
        if (!createRenderPass()) {
            MR_LOG_ERROR("Failed to create render pass");
            return false;
        }
        
        // Step 8: Create framebuffers
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
        
        // Step 13: Create memory manager (UE5-style sub-allocation)
        m_memoryManager = MakeUnique<FVulkanMemoryManager>(m_device, m_physicalDevice);
        if (!m_memoryManager) {
            MR_LOG_ERROR("Failed to create memory manager");
            return false;
        }
        MR_LOG_INFO("FVulkanMemoryManager initialized (UE5-style sub-allocation enabled)");
        
        // Step 14: Query device capabilities
        queryCapabilities();
        
        MR_LOG_INFO("Vulkan device initialized successfully");
        return true;
    }
    
    void VulkanDevice::shutdown() {
        if (m_device != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkDeviceWaitIdle(m_device);
            
            // Destroy synchronization objects
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                if (m_imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
                    functions.vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
                }
                if (m_renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
                    functions.vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
                }
                if (m_inFlightFences[i] != VK_NULL_HANDLE) {
                    functions.vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
                }
            }
            
            // Clean up command lists BEFORE destroying command pool
            // (command buffers must be freed before the pool is destroyed)
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
    
    TSharedPtr<IRHICommandList> VulkanDevice::createCommandList() {
        auto commandList = MakeShared<VulkanCommandList>(this);
        if (!commandList->initialize()) {
            MR_LOG_ERROR("Failed to initialize Vulkan command list");
            return nullptr;
        }
        return commandList;
    }
    
    void VulkanDevice::executeCommandLists(TSpan<TSharedPtr<IRHICommandList>> commandLists) {
        if (commandLists.empty()) {
            return;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Convert to Vulkan command buffers
        TArray<VkCommandBuffer> vkCommandBuffers;
        vkCommandBuffers.reserve(commandLists.size());
        
        for (const auto& cmdList : commandLists) {
            if (auto vulkanCmdList = std::static_pointer_cast<VulkanCommandList>(cmdList)) {
                vkCommandBuffers.push_back(vulkanCmdList->getVulkanCommandBuffer());
            }
        }
        
        if (vkCommandBuffers.empty()) {
            MR_LOG_WARNING("No valid Vulkan command buffers to execute");
            return;
        }
        
        // Submit to graphics queue
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = static_cast<uint32>(vkCommandBuffers.size());
        submitInfo.pCommandBuffers = vkCommandBuffers.data();
        
        VkResult result = functions.vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to submit command buffers! Result: " + std::to_string(result));
        }
    }
    
    IRHICommandList* VulkanDevice::getImmediateCommandList() {
        return m_immediateCommandList.get();
    }
    
    void VulkanDevice::waitForIdle() {
        if (m_device != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkDeviceWaitIdle(m_device);
        }
    }
    
    void VulkanDevice::present() {
        if (m_swapchain == VK_NULL_HANDLE) {
            MR_LOG_WARNING("Cannot present: no swapchain available");
            return;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Begin new frame for descriptor set allocator
        if (m_descriptorSetAllocator) {
            m_descriptorSetAllocator->beginFrame(m_currentFrame);
        }
        
        // Wait for previous frame
        functions.vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
        functions.vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
        
        // Acquire next image
        uint32 imageIndex;
        VkResult result = functions.vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                                          m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
        
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            // Recreate swapchain - for now just return
            MR_LOG_WARNING("Swapchain out of date, should recreate");
            return;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            MR_LOG_ERROR("Failed to acquire swapchain image! Result: " + std::to_string(result));
            return;
        }
        
        m_currentImageIndex = imageIndex;
        
        // Present
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        
        VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        
        VkSwapchainKHR swapChains[] = {m_swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pResults = nullptr;
        
        result = functions.vkQueuePresentKHR(m_presentQueue, &presentInfo);
        
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            // Should recreate swapchain
            MR_LOG_WARNING("Swapchain suboptimal or out of date");
        } else if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to present swapchain image! Result: " + std::to_string(result));
        }
        
        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
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
    
    // Private implementation methods - full implementations
    bool VulkanDevice::createInstance(const RHICreateInfo& createInfo) {
        MR_LOG_INFO("Creating Vulkan instance...");
        
        m_validationEnabled = createInfo.enableValidation;
        
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
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (m_validationEnabled) {
            instanceCreateInfo.enabledLayerCount = static_cast<uint32>(s_validationLayers.size());
            instanceCreateInfo.ppEnabledLayerNames = s_validationLayers.data();
            
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
        } else {
            instanceCreateInfo.enabledLayerCount = 0;
            instanceCreateInfo.pNext = nullptr;
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
        
        // Get all physical devices
        TArray<VkPhysicalDevice> devices(deviceCount);
        functions.vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
        
        // Find the first suitable device
        for (const auto& device : devices) {
            if (isDeviceSuitable(device)) {
                m_physicalDevice = device;
                
                // Get device properties
                functions.vkGetPhysicalDeviceProperties(device, &m_deviceProperties);
                functions.vkGetPhysicalDeviceFeatures(device, &m_deviceFeatures);
                functions.vkGetPhysicalDeviceMemoryProperties(device, &m_memoryProperties);
                
                MR_LOG_INFO("Selected GPU: " + String(m_deviceProperties.deviceName));
                MR_LOG_INFO("GPU Type: " + std::to_string(m_deviceProperties.deviceType));
                MR_LOG_INFO("API Version: " + std::to_string(VK_VERSION_MAJOR(m_deviceProperties.apiVersion)) +
                           "." + std::to_string(VK_VERSION_MINOR(m_deviceProperties.apiVersion)) +
                           "." + std::to_string(VK_VERSION_PATCH(m_deviceProperties.apiVersion)));
                
                return true;
            }
        }
        
        MR_LOG_ERROR("Failed to find a suitable GPU!");
        return false;
    }
    
    bool VulkanDevice::createLogicalDevice() {
        MR_LOG_INFO("Creating logical device...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Find queue families
        m_graphicsQueueFamily = findQueueFamilies(m_physicalDevice, m_surface, VK_QUEUE_GRAPHICS_BIT); // 
        if (m_surface != VK_NULL_HANDLE) {
            // 这里参数使用了 VK_QUEUE_GRAPHICS_BIT，可能是个笔误，通常呈现队列有专门的查询方式，例如使用 vkGetPhysicalDeviceSurfaceSupportKHR
            m_presentQueueFamily = findQueueFamilies(m_physicalDevice, m_surface, VK_QUEUE_GRAPHICS_BIT);
        } else {
            m_presentQueueFamily = m_graphicsQueueFamily; // Use graphics queue for present when no surface
        }
        
        if (m_graphicsQueueFamily.familyIndex == VK_QUEUE_FAMILY_IGNORED) {
            MR_LOG_ERROR("Failed to find graphics queue family!");
            return false;
        }
        
        // Create queue create infos
        TArray<VkDeviceQueueCreateInfo> queueCreateInfos;
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
        
        // Create device
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        
        // Device extensions
        createInfo.enabledExtensionCount = static_cast<uint32>(s_deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = s_deviceExtensions.data();
        
        // Validation layers (deprecated for devices, but needed for older Vulkan)
        if (m_validationEnabled) {
            createInfo.enabledLayerCount = static_cast<uint32>(s_validationLayers.size());
            createInfo.ppEnabledLayerNames = s_validationLayers.data();
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
        
        uint32 formatCount;
        functions.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
        TArray<VkSurfaceFormatKHR> formats(formatCount);
        functions.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
        
        uint32 presentModeCount;
        functions.vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
        TArray<VkPresentModeKHR> presentModes(presentModeCount);
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
        MR_LOG_INFO("Creating render pass...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Color attachment (swapchain image)
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_swapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear before rendering
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Store result to memory
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Don't care about initial layout
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Ready for presentation
        
        // Attachment reference for subpass
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0; // Index in attachment descriptions array
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        
        // Subpass description
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        
        // Subpass dependency (for automatic layout transitions)
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL; // Implicit subpass before render pass
        dependency.dstSubpass = 0; // Our first (and only) subpass
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        
        // Create render pass
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        
        VkResult result = functions.vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create render pass! Result: " + std::to_string(result));
            return false;
        }
        
        MR_LOG_INFO("Render pass created successfully");
        return true;
    }
    
    bool VulkanDevice::createFramebuffers() {
        MR_LOG_INFO("Creating framebuffers...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Create one framebuffer for each swapchain image view
        m_swapchainFramebuffers.resize(m_swapchainImageViews.size());
        
        for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
            VkImageView attachments[] = {
                m_swapchainImageViews[i]
            };
            
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = m_swapchainExtent.width;
            framebufferInfo.height = m_swapchainExtent.height;
            framebufferInfo.layers = 1;
            
            VkResult result = functions.vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to create framebuffer " + std::to_string(i) + "! Result: " + std::to_string(result));
                return false;
            }
        }
        
        MR_LOG_INFO("Created " + std::to_string(m_swapchainFramebuffers.size()) + " framebuffers successfully");
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
        m_immediateCommandList = MakeUnique<VulkanCommandList>(this);
        if (!m_immediateCommandList->initialize()) {
            MR_LOG_ERROR("Failed to initialize immediate command list");
            return false;
        }
        
        MR_LOG_INFO("Command pool created successfully");
        return true;
    }
    
    bool VulkanDevice::createSyncObjects() {
        MR_LOG_INFO("Creating synchronization objects...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Resize vectors
        m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start in signaled state
        
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkResult result;
            
            result = functions.vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to create image available semaphore! Result: " + std::to_string(result));
                return false;
            }
            
            result = functions.vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to create render finished semaphore! Result: " + std::to_string(result));
                return false;
            }
            
            result = functions.vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to create in-flight fence! Result: " + std::to_string(result));
                return false;
            }
        }
        
        MR_LOG_INFO("Synchronization objects created successfully");
        return true;
    }
    
    bool VulkanDevice::checkValidationLayerSupport() {
        const auto& functions = VulkanAPI::getFunctions();
        
        uint32 layerCount;
        functions.vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        
        TArray<VkLayerProperties> availableLayers(layerCount);
        functions.vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        
        for (const char* layerName : s_validationLayers) {
            bool layerFound = false;
            
            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }
            
            if (!layerFound) {
                return false;
            }
        }
        
        return true;
    }
    
    TArray<const char*> VulkanDevice::getRequiredExtensions(bool enableValidation) {
        TArray<const char*> extensions;
        
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
        for (const auto& extension : extensions) {
            MR_LOG_INFO("  - " + String(extension));
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
        
        TArray<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        functions.vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
        
        for (uint32 i = 0; i < queueFamilies.size(); i++) {
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
        
        uint32 extensionCount;
        functions.vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        
        TArray<VkExtensionProperties> availableExtensions(extensionCount);
        functions.vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
        
        std::set<String> requiredExtensions(s_deviceExtensions.begin(), s_deviceExtensions.end());
        
        for (const auto& extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
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
}
