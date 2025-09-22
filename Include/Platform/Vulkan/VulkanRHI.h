#pragma once

#include "Core/CoreMinimal.h"

// Vulkan headers
#if PLATFORM_WINDOWS
    #define VK_USE_PLATFORM_WIN32_KHR
#elif PLATFORM_LINUX
    #define VK_USE_PLATFORM_XLIB_KHR
#endif

#include <vulkan/vulkan.h>
#include <RHI/RHIDefinitions.h>

namespace MonsterRender::RHI::Vulkan {
    
    // Vulkan function pointers for dynamic loading
    struct VulkanFunctions {
        // Instance functions
        PFN_vkCreateInstance vkCreateInstance = nullptr;
        PFN_vkDestroyInstance vkDestroyInstance = nullptr;
        PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
        PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;
        
        // Device functions
        PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
        PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
        PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures = nullptr;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
        PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = nullptr;
        PFN_vkCreateDevice vkCreateDevice = nullptr;
        PFN_vkDestroyDevice vkDestroyDevice = nullptr;
        PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
        
        // Command functions
        PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
        PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
        PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
        PFN_vkFreeCommandBuffers vkFreeCommandBuffers = nullptr;
        PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
        PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
        PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
        
        // Queue functions
        PFN_vkQueueSubmit vkQueueSubmit = nullptr;
        PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;
        PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
        
        // Synchronization functions
        PFN_vkCreateSemaphore vkCreateSemaphore = nullptr;
        PFN_vkDestroySemaphore vkDestroySemaphore = nullptr;
        PFN_vkCreateFence vkCreateFence = nullptr;
        PFN_vkDestroyFence vkDestroyFence = nullptr;
        PFN_vkWaitForFences vkWaitForFences = nullptr;
        PFN_vkResetFences vkResetFences = nullptr;
        
        // Memory functions
        PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
        PFN_vkAllocateMemory vkAllocateMemory = nullptr;
        PFN_vkFreeMemory vkFreeMemory = nullptr;
        PFN_vkMapMemory vkMapMemory = nullptr;
        PFN_vkUnmapMemory vkUnmapMemory = nullptr;
        
        // Buffer functions
        PFN_vkCreateBuffer vkCreateBuffer = nullptr;
        PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
        PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
        PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
        
        // Image functions
        PFN_vkCreateImage vkCreateImage = nullptr;
        PFN_vkDestroyImage vkDestroyImage = nullptr;
        PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
        PFN_vkBindImageMemory vkBindImageMemory = nullptr;
        PFN_vkCreateImageView vkCreateImageView = nullptr;
        PFN_vkDestroyImageView vkDestroyImageView = nullptr;
        
        // Shader module functions
        PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
        PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
        
        // Swapchain functions (loaded dynamically based on extensions)
        PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = nullptr;
        PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR = nullptr;
        PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = nullptr;
        PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = nullptr;
        PFN_vkQueuePresentKHR vkQueuePresentKHR = nullptr;
        
        // Surface functions
#if PLATFORM_WINDOWS
        PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR = nullptr;
#elif PLATFORM_LINUX
        PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR = nullptr;
#endif
        PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
        PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
        
        // Debug functions
        PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
        PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
    };
    
    /**
     * Vulkan API wrapper and function loader
     */
    class VulkanAPI {
    public:
        /**
         * Initialize Vulkan API and load function pointers
         */
        static bool initialize();
        
        /**
         * Shutdown Vulkan API
         */
        static void shutdown();
        
        /**
         * Get the global Vulkan functions
         */
        static const VulkanFunctions& getFunctions() { return s_functions; }
        
        /**
         * Load instance-level functions
         */
        static void loadInstanceFunctions(VkInstance instance);
        
        /**
         * Load device-level functions
         */
        static void loadDeviceFunctions(VkDevice device);
        
        /**
         * Check if Vulkan is available on this system
         */
        static bool isAvailable();
        
    private:
        static VulkanFunctions s_functions;
        static void* s_vulkanLibrary;
        static bool s_initialized;
    };
    
    /**
     * Vulkan utility functions
     */
    namespace VulkanUtils {
        
        /**
         * Convert RHI format to Vulkan format
         */
        VkFormat getRHIFormatToVulkan(EPixelFormat format);
        
        /**
         * Convert Vulkan format to RHI format
         */
        EPixelFormat getVulkanToRHIFormat(VkFormat format);
        
        /**
         * Convert RHI usage to Vulkan usage flags
         */
        VkBufferUsageFlags getBufferUsageFlags(EResourceUsage usage);
        
        /**
         * Convert RHI usage to Vulkan image usage flags
         */
        VkImageUsageFlags getImageUsageFlags(EResourceUsage usage);
        
        /**
         * Convert RHI primitive topology to Vulkan
         */
        VkPrimitiveTopology getPrimitiveTopology(EPrimitiveTopology topology);
        
        /**
         * Find suitable memory type for requirements
         */
        uint32 findMemoryType(const VkPhysicalDeviceMemoryProperties& memProperties, 
                            uint32 typeFilter, VkMemoryPropertyFlags properties);
        
        /**
         * Create platform-specific surface
         */
        VkResult createSurface(VkInstance instance, void* windowHandle, VkSurfaceKHR* surface);
        
        /**
         * Create Vulkan debug messenger
         */
        VkResult createDebugMessenger(VkInstance instance, 
                                    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
                                    VkDebugUtilsMessengerEXT* debugMessenger);
        
        /**
         * Destroy Vulkan debug messenger
         */
        void destroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger);
        
        /**
         * Vulkan debug callback
         */
        VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
            void* userData);
    }
}
