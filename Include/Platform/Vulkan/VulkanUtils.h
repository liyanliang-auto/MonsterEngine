#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHIDefinitions.h"

// Vulkan headers
#if PLATFORM_WINDOWS
    #define VK_USE_PLATFORM_WIN32_KHR
#elif PLATFORM_LINUX
    #define VK_USE_PLATFORM_XLIB_KHR
#endif

#include <vulkan/vulkan.h>

namespace MonsterRender::RHI::Vulkan {

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
        
        /**
         * Get correct aspect mask for image format
         * Automatically determines COLOR, DEPTH, or DEPTH|STENCIL based on format
         */
        VkImageAspectFlags getImageAspectMask(VkFormat format);
    }
}
