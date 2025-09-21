#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"

#if PLATFORM_WINDOWS
    #include <Windows.h>
#elif PLATFORM_LINUX
    #include <dlfcn.h>
#endif

namespace MonsterRender::RHI::Vulkan {
    
    // Static member definitions
    VulkanFunctions VulkanAPI::s_functions{};
    void* VulkanAPI::s_vulkanLibrary = nullptr;
    bool VulkanAPI::s_initialized = false;
    
    bool VulkanAPI::initialize() {
        if (s_initialized) {
            return true;
        }
        
        MR_LOG_INFO("Initializing Vulkan API...");
        
        // Load Vulkan library
        #if PLATFORM_WINDOWS
            s_vulkanLibrary = LoadLibrary(TEXT("vulkan-1.dll"));
        #elif PLATFORM_LINUX
            s_vulkanLibrary = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
            if (!s_vulkanLibrary) {
                s_vulkanLibrary = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
            }
        #else
            #error "Unsupported platform for Vulkan"
        #endif
        
        if (!s_vulkanLibrary) {
            MR_LOG_ERROR("Failed to load Vulkan library");
            return false;
        }
        
        // Load vkGetInstanceProcAddr
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
        #if PLATFORM_WINDOWS
            vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress((HMODULE)s_vulkanLibrary, "vkGetInstanceProcAddr");
        #elif PLATFORM_LINUX
            vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(s_vulkanLibrary, "vkGetInstanceProcAddr");
        #endif
        
        if (!vkGetInstanceProcAddr) {
            MR_LOG_ERROR("Failed to load vkGetInstanceProcAddr");
            shutdown();
            return false;
        }
        
        // Load global functions
        // 创建Vulkan实例
        s_functions.vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(nullptr, "vkCreateInstance");
        // 枚举实例扩展
        s_functions.vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties");
        // 枚举验证层
        s_functions.vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties");
        
        if (!s_functions.vkCreateInstance || 
            !s_functions.vkEnumerateInstanceExtensionProperties ||
            !s_functions.vkEnumerateInstanceLayerProperties) {
            MR_LOG_ERROR("Failed to load required Vulkan global functions");
            shutdown();
            return false;
        }
        
        s_initialized = true;
        MR_LOG_INFO("Vulkan API initialized successfully");
        return true;
    }
    
    void VulkanAPI::shutdown() {
        if (!s_initialized) {
            return;
        }
        
        MR_LOG_INFO("Shutting down Vulkan API...");
        
        // Clear function pointers
        s_functions = {};
        
        // Unload library
        if (s_vulkanLibrary) {
            #if PLATFORM_WINDOWS
                FreeLibrary((HMODULE)s_vulkanLibrary);
            #elif PLATFORM_LINUX
                dlclose(s_vulkanLibrary);
            #endif
            s_vulkanLibrary = nullptr;
        }
        
        s_initialized = false;
        MR_LOG_INFO("Vulkan API shutdown complete");
    }
    
    void VulkanAPI::loadInstanceFunctions(VkInstance instance) {
        MR_ASSERT(instance != VK_NULL_HANDLE);
        MR_ASSERT(s_initialized);
        
        // Get vkGetInstanceProcAddr first
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
        #if PLATFORM_WINDOWS
            vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress((HMODULE)s_vulkanLibrary, "vkGetInstanceProcAddr");
        #elif PLATFORM_LINUX
            vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(s_vulkanLibrary, "vkGetInstanceProcAddr");
        #endif
        
        // Load instance functions
        s_functions.vkDestroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(instance, "vkDestroyInstance");
        s_functions.vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
        s_functions.vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
        s_functions.vkGetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures");
        s_functions.vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
        s_functions.vkCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr(instance, "vkCreateDevice");
        s_functions.vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties");
        
        // Surface functions
#if PLATFORM_WINDOWS
        s_functions.vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");
#elif PLATFORM_LINUX
        s_functions.vkCreateXlibSurfaceKHR = (PFN_vkCreateXlibSurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR");
#endif
        s_functions.vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");
        s_functions.vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        s_functions.vkGetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
        s_functions.vkGetPhysicalDeviceSurfacePresentModesKHR = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
        
        // Debug functions
        s_functions.vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        s_functions.vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    }
    
    void VulkanAPI::loadDeviceFunctions(VkDevice device) {
        MR_ASSERT(device != VK_NULL_HANDLE);
        MR_ASSERT(s_initialized);
        
        // Get vkGetDeviceProcAddr
        PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
        #if PLATFORM_WINDOWS
            vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)GetProcAddress((HMODULE)s_vulkanLibrary, "vkGetDeviceProcAddr");
        #elif PLATFORM_LINUX
            vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)dlsym(s_vulkanLibrary, "vkGetDeviceProcAddr");
        #endif
        
        // Load device functions
        s_functions.vkDestroyDevice = (PFN_vkDestroyDevice)vkGetDeviceProcAddr(device, "vkDestroyDevice");
        s_functions.vkGetDeviceQueue = (PFN_vkGetDeviceQueue)vkGetDeviceProcAddr(device, "vkGetDeviceQueue");
        s_functions.vkCreateCommandPool = (PFN_vkCreateCommandPool)vkGetDeviceProcAddr(device, "vkCreateCommandPool");
        s_functions.vkDestroyCommandPool = (PFN_vkDestroyCommandPool)vkGetDeviceProcAddr(device, "vkDestroyCommandPool");
        s_functions.vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)vkGetDeviceProcAddr(device, "vkAllocateCommandBuffers");
        s_functions.vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)vkGetDeviceProcAddr(device, "vkFreeCommandBuffers");
        s_functions.vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)vkGetDeviceProcAddr(device, "vkBeginCommandBuffer");
        s_functions.vkEndCommandBuffer = (PFN_vkEndCommandBuffer)vkGetDeviceProcAddr(device, "vkEndCommandBuffer");
        s_functions.vkResetCommandBuffer = (PFN_vkResetCommandBuffer)vkGetDeviceProcAddr(device, "vkResetCommandBuffer");
        s_functions.vkQueueSubmit = (PFN_vkQueueSubmit)vkGetDeviceProcAddr(device, "vkQueueSubmit");
        s_functions.vkQueueWaitIdle = (PFN_vkQueueWaitIdle)vkGetDeviceProcAddr(device, "vkQueueWaitIdle");
        s_functions.vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)vkGetDeviceProcAddr(device, "vkDeviceWaitIdle");
        
        // Synchronization functions
        s_functions.vkCreateSemaphore = (PFN_vkCreateSemaphore)vkGetDeviceProcAddr(device, "vkCreateSemaphore");
        s_functions.vkDestroySemaphore = (PFN_vkDestroySemaphore)vkGetDeviceProcAddr(device, "vkDestroySemaphore");
        s_functions.vkCreateFence = (PFN_vkCreateFence)vkGetDeviceProcAddr(device, "vkCreateFence");
        s_functions.vkDestroyFence = (PFN_vkDestroyFence)vkGetDeviceProcAddr(device, "vkDestroyFence");
        s_functions.vkWaitForFences = (PFN_vkWaitForFences)vkGetDeviceProcAddr(device, "vkWaitForFences");
        s_functions.vkResetFences = (PFN_vkResetFences)vkGetDeviceProcAddr(device, "vkResetFences");
        s_functions.vkAllocateMemory = (PFN_vkAllocateMemory)vkGetDeviceProcAddr(device, "vkAllocateMemory");
        s_functions.vkFreeMemory = (PFN_vkFreeMemory)vkGetDeviceProcAddr(device, "vkFreeMemory");
        s_functions.vkMapMemory = (PFN_vkMapMemory)vkGetDeviceProcAddr(device, "vkMapMemory");
        s_functions.vkUnmapMemory = (PFN_vkUnmapMemory)vkGetDeviceProcAddr(device, "vkUnmapMemory");
        s_functions.vkCreateBuffer = (PFN_vkCreateBuffer)vkGetDeviceProcAddr(device, "vkCreateBuffer");
        s_functions.vkDestroyBuffer = (PFN_vkDestroyBuffer)vkGetDeviceProcAddr(device, "vkDestroyBuffer");
        s_functions.vkGetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)vkGetDeviceProcAddr(device, "vkGetBufferMemoryRequirements");
        s_functions.vkBindBufferMemory = (PFN_vkBindBufferMemory)vkGetDeviceProcAddr(device, "vkBindBufferMemory");
        s_functions.vkCreateImage = (PFN_vkCreateImage)vkGetDeviceProcAddr(device, "vkCreateImage");
        s_functions.vkDestroyImage = (PFN_vkDestroyImage)vkGetDeviceProcAddr(device, "vkDestroyImage");
        s_functions.vkGetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)vkGetDeviceProcAddr(device, "vkGetImageMemoryRequirements");
        s_functions.vkBindImageMemory = (PFN_vkBindImageMemory)vkGetDeviceProcAddr(device, "vkBindImageMemory");
        s_functions.vkCreateImageView = (PFN_vkCreateImageView)vkGetDeviceProcAddr(device, "vkCreateImageView");
        s_functions.vkDestroyImageView = (PFN_vkDestroyImageView)vkGetDeviceProcAddr(device, "vkDestroyImageView");
        
        // Shader module functions
        s_functions.vkCreateShaderModule = (PFN_vkCreateShaderModule)vkGetDeviceProcAddr(device, "vkCreateShaderModule");
        s_functions.vkDestroyShaderModule = (PFN_vkDestroyShaderModule)vkGetDeviceProcAddr(device, "vkDestroyShaderModule");
        
        // Swapchain functions
        s_functions.vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
        s_functions.vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR");
        s_functions.vkGetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR");
        s_functions.vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR");
        s_functions.vkQueuePresentKHR = (PFN_vkQueuePresentKHR)vkGetDeviceProcAddr(device, "vkQueuePresentKHR");
    }
    
    bool VulkanAPI::isAvailable() {
        if (s_initialized) {
            return true;
        }
        
        // Try to load Vulkan library temporarily to check availability
        void* tempLibrary = nullptr;
        #if PLATFORM_WINDOWS
            tempLibrary = LoadLibrary(TEXT("vulkan-1.dll"));
        #elif PLATFORM_LINUX
            tempLibrary = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
            if (!tempLibrary) {
                tempLibrary = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
            }
        #endif
        
        if (tempLibrary) {
            #if PLATFORM_WINDOWS
                FreeLibrary((HMODULE)tempLibrary);
            #elif PLATFORM_LINUX
                dlclose(tempLibrary);
            #endif
            return true;
        }
        
        return false;
    }
}
