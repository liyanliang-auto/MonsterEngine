# Vulkan 后端概述

MonsterRender的Vulkan后端实现了RHI接口，提供了高性能、低开销的现代图形渲染能力。Vulkan后端是引擎的主要目标平台，充分利用了Vulkan的显式控制特性。

## 设计特点

### 1. 显式资源管理
- 手动内存分配和同步
- 精确的资源生命周期控制
- 最小的驱动开销

### 2. 多线程友好
- 并行命令缓冲区记录
- 线程安全的资源创建
- 分离的命令池管理

### 3. 现代GPU特性支持
- 描述符集管理
- 动态状态支持
- 扩展功能集成

## 核心组件

### VulkanAPI - API加载器
负责动态加载Vulkan函数和管理API生命周期：

```cpp
class VulkanAPI {
public:
    static bool initialize();                    // 初始化API
    static void shutdown();                      // 关闭API
    static bool isAvailable();                   // 检查可用性
    static const VulkanFunctions& getFunctions(); // 获取函数指针
    
    // 动态加载函数
    static void loadInstanceFunctions(VkInstance instance);
    static void loadDeviceFunctions(VkDevice device);
};
```

**API加载策略**：
- 运行时动态加载vulkan-1.dll/libvulkan.so
- 分级加载：全局→实例→设备函数
- 失败时优雅降级

### VulkanDevice - 设备实现
实现IRHIDevice接口，管理Vulkan设备和资源：

```cpp
class VulkanDevice : public IRHIDevice {
private:
    // Vulkan对象
    VkInstance m_instance;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VkSurfaceKHR m_surface;
    VkSwapchainKHR m_swapchain;
    
    // 队列管理
    VkQueue m_graphicsQueue;
    VkQueue m_presentQueue;
    QueueFamily m_graphicsQueueFamily;
    QueueFamily m_presentQueueFamily;
    
    // 命令管理
    VkCommandPool m_commandPool;
    TUniquePtr<VulkanCommandList> m_immediateCommandList;
    
    // 同步对象
    TArray<VkSemaphore> m_imageAvailableSemaphores;
    TArray<VkSemaphore> m_renderFinishedSemaphores;
    TArray<VkFence> m_inFlightFences;
};
```

### 初始化流程

#### 1. 实例创建
```cpp
bool VulkanDevice::createInstance(const RHICreateInfo& createInfo) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = createInfo.applicationName.c_str();
    appInfo.applicationVersion = createInfo.applicationVersion;
    appInfo.pEngineName = "MonsterRender";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;
    
    VkInstanceCreateInfo createInfoVk{};
    createInfoVk.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfoVk.pApplicationInfo = &appInfo;
    
    // 扩展和验证层配置
    auto extensions = getRequiredExtensions(createInfo.enableValidation);
    createInfoVk.enabledExtensionCount = static_cast<uint32>(extensions.size());
    createInfoVk.ppEnabledExtensionNames = extensions.data();
    
    if (createInfo.enableValidation && checkValidationLayerSupport()) {
        createInfoVk.enabledLayerCount = static_cast<uint32>(s_validationLayers.size());
        createInfoVk.ppEnabledLayerNames = s_validationLayers.data();
    }
    
    return vkCreateInstance(&createInfoVk, nullptr, &m_instance) == VK_SUCCESS;
}
```

#### 2. 物理设备选择
```cpp
bool VulkanDevice::selectPhysicalDevice() {
    uint32 deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    
    TArray<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
    
    // 选择最佳设备
    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            m_physicalDevice = device;
            return true;
        }
    }
    
    return false;
}

bool VulkanDevice::isDeviceSuitable(VkPhysicalDevice device) {
    // 检查队列族支持
    auto graphicsFamily = findQueueFamilies(device, m_surface, VK_QUEUE_GRAPHICS_BIT);
    auto presentFamily = findQueueFamilies(device, m_surface, 0);
    
    // 检查扩展支持
    bool extensionsSupported = checkDeviceExtensionSupport(device);
    
    // 检查交换链支持
    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && 
                           !swapChainSupport.presentModes.empty();
    }
    
    return graphicsFamily.familyIndex != VK_QUEUE_FAMILY_IGNORED &&
           presentFamily.familyIndex != VK_QUEUE_FAMILY_IGNORED &&
           extensionsSupported && swapChainAdequate;
}
```

#### 3. 逻辑设备创建
```cpp
bool VulkanDevice::createLogicalDevice() {
    TArray<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32> uniqueQueueFamilies = {
        m_graphicsQueueFamily.familyIndex,
        m_presentQueueFamily.familyIndex
    };
    
    float queuePriority = 1.0f;
    for (uint32 queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }
    
    VkPhysicalDeviceFeatures deviceFeatures{};
    
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32>(s_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = s_deviceExtensions.data();
    
    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        return false;
    }
    
    // 获取队列句柄
    vkGetDeviceQueue(m_device, m_graphicsQueueFamily.familyIndex, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentQueueFamily.familyIndex, 0, &m_presentQueue);
    
    return true;
}
```

### 交换链管理

#### 交换链创建
```cpp
bool VulkanDevice::createSwapchain(const RHICreateInfo& createInfo) {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_physicalDevice);
    
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, createInfo);
    
    uint32 imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && 
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR createInfoSwap{};
    createInfoSwap.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfoSwap.surface = m_surface;
    createInfoSwap.minImageCount = imageCount;
    createInfoSwap.imageFormat = surfaceFormat.format;
    createInfoSwap.imageColorSpace = surfaceFormat.colorSpace;
    createInfoSwap.imageExtent = extent;
    createInfoSwap.imageArrayLayers = 1;
    createInfoSwap.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    
    uint32 queueFamilyIndices[] = {
        m_graphicsQueueFamily.familyIndex,
        m_presentQueueFamily.familyIndex
    };
    
    if (m_graphicsQueueFamily.familyIndex != m_presentQueueFamily.familyIndex) {
        createInfoSwap.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfoSwap.queueFamilyIndexCount = 2;
        createInfoSwap.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfoSwap.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    createInfoSwap.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfoSwap.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfoSwap.presentMode = presentMode;
    createInfoSwap.clipped = VK_TRUE;
    createInfoSwap.oldSwapchain = VK_NULL_HANDLE;
    
    if (vkCreateSwapchainKHR(m_device, &createInfoSwap, nullptr, &m_swapchain) != VK_SUCCESS) {
        return false;
    }
    
    // 获取交换链图像
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());
    
    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;
    
    return true;
}
```

## 资源实现

### VulkanBuffer - 缓冲区实现
```cpp
class VulkanBuffer : public IRHIBuffer {
public:
    VulkanBuffer(VulkanDevice* device, const BufferDesc& desc);
    ~VulkanBuffer();
    
    // IRHIBuffer接口
    void* map() override;
    void unmap() override;
    
    // Vulkan特定接口
    VkBuffer getBuffer() const { return m_buffer; }
    VkDeviceMemory getMemory() const { return m_memory; }
    
private:
    VulkanDevice* m_device;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    void* m_mappedData = nullptr;
    bool m_persistentlyMapped = false;
};

// 缓冲区创建实现
VulkanBuffer::VulkanBuffer(VulkanDevice* device, const BufferDesc& desc) 
    : IRHIBuffer(desc), m_device(device) {
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = VulkanUtils::getBufferUsageFlags(desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VkResult result = vkCreateBuffer(m_device->getDevice(), &bufferInfo, nullptr, &m_buffer);
    MR_CHECK(result == VK_SUCCESS);
    
    // 分配内存
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device->getDevice(), m_buffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = VulkanUtils::findMemoryType(
        m_device->getMemoryProperties(),
        memRequirements.memoryTypeBits,
        desc.cpuAccessible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                           : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    result = vkAllocateMemory(m_device->getDevice(), &allocInfo, nullptr, &m_memory);
    MR_CHECK(result == VK_SUCCESS);
    
    vkBindBufferMemory(m_device->getDevice(), m_buffer, m_memory, 0);
    
    // 持久映射CPU可访问缓冲区
    if (desc.cpuAccessible) {
        result = vkMapMemory(m_device->getDevice(), m_memory, 0, desc.size, 0, &m_mappedData);
        MR_CHECK(result == VK_SUCCESS);
        m_persistentlyMapped = true;
    }
}
```

### VulkanTexture - 纹理实现
```cpp
class VulkanTexture : public IRHITexture {
public:
    VulkanTexture(VulkanDevice* device, const TextureDesc& desc);
    ~VulkanTexture();
    
    // Vulkan特定接口
    VkImage getImage() const { return m_image; }
    VkImageView getImageView() const { return m_imageView; }
    VkFormat getVulkanFormat() const { return m_format; }
    
private:
    VulkanDevice* m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
};
```

## 命令列表实现

### VulkanCommandList
```cpp
class VulkanCommandList : public IRHICommandList {
public:
    VulkanCommandList(VulkanDevice* device);
    ~VulkanCommandList();
    
    // IRHICommandList接口
    void begin() override;
    void end() override;
    void reset() override;
    
    void setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) override;
    void setVertexBuffers(uint32 startSlot, TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) override;
    void setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit = true) override;
    
    void draw(uint32 vertexCount, uint32 startVertexLocation = 0) override;
    void drawIndexed(uint32 indexCount, uint32 startIndexLocation = 0, 
                    int32 baseVertexLocation = 0) override;
    
    // Vulkan特定
    VkCommandBuffer getCommandBuffer() const { return m_commandBuffer; }
    
private:
    VulkanDevice* m_device;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    bool m_recording = false;
};

// 绘制命令实现
void VulkanCommandList::draw(uint32 vertexCount, uint32 startVertexLocation) {
    MR_ASSERT(m_recording);
    MR_SCOPED_DEBUG_EVENT(this, "Draw");
    
    vkCmdDraw(m_commandBuffer, vertexCount, 1, startVertexLocation, 0);
}

void VulkanCommandList::drawIndexed(uint32 indexCount, uint32 startIndexLocation, 
                                  int32 baseVertexLocation) {
    MR_ASSERT(m_recording);
    MR_SCOPED_DEBUG_EVENT(this, "DrawIndexed");
    
    vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, startIndexLocation, baseVertexLocation, 0);
}
```

## 同步机制

### 帧同步
```cpp
// 多帧缓冲支持
constexpr uint32 MAX_FRAMES_IN_FLIGHT = 2;

class VulkanDevice {
    uint32 m_currentFrame = 0;
    TArray<VkSemaphore> m_imageAvailableSemaphores;
    TArray<VkSemaphore> m_renderFinishedSemaphores;
    TArray<VkFence> m_inFlightFences;
};

void VulkanDevice::present() {
    // 等待前一帧完成
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
    
    // 获取下一个交换链图像
    uint32 imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                           m_imageAvailableSemaphores[m_currentFrame], 
                                           VK_NULL_HANDLE, &imageIndex);
    
    // 提交渲染命令
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    
    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);
    
    // 呈现图像
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    
    VkSwapchainKHR swapChains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    
    vkQueuePresentKHR(m_presentQueue, &presentInfo);
    
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
```

## 调试支持

### 验证层集成
```cpp
// 验证层配置
const TArray<const char*> VulkanDevice::s_validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// 调试回调
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanUtils::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData) {
    
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        MR_LOG_WARNING("Vulkan Validation Layer: " + String(callbackData->pMessage));
    }
    
    return VK_FALSE;
}
```

### 调试标记
```cpp
void VulkanCommandList::beginEvent(const String& name) {
    if (!m_device->isDebugMarkersEnabled()) return;
    
    VkDebugUtilsLabelEXT labelInfo{};
    labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    labelInfo.pLabelName = name.c_str();
    
    const auto& functions = VulkanAPI::getFunctions();
    if (functions.vkCmdBeginDebugUtilsLabelEXT) {
        functions.vkCmdBeginDebugUtilsLabelEXT(m_commandBuffer, &labelInfo);
    }
}
```

## 性能优化

### 内存分配策略
- 使用大块内存分配器减少分配次数
- 子分配管理避免内存碎片
- 基于用途的内存类型选择

### 命令缓冲区重用
- 命令池管理减少分配开销
- 二级命令缓冲区支持并行记录
- 重置而非重新分配

### 描述符管理
- 描述符池预分配
- 描述符集缓存和重用
- 无绑定渲染支持（如果硬件支持）

---

## 相关文档
- [API加载器详解](APILoader.md)
- [设备初始化详解](DeviceInitialization.md)
- [资源实现详解](ResourceImplementation.md)
- [RHI接口设计](../RHI/RHIOverview.md)
