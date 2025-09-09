#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"

#if PLATFORM_WINDOWS
    #include <Windows.h>
#elif PLATFORM_LINUX
    #include <X11/Xlib.h>
#endif
#include "RHI/RHIDefinitions.h"

namespace MonsterRender::RHI::Vulkan::VulkanUtils {
    
    VkFormat getRHIFormatToVulkan(EPixelFormat format) {
        switch (format) {
            case EPixelFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
            case EPixelFormat::B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
            case EPixelFormat::R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
            case EPixelFormat::B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
            case EPixelFormat::R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case EPixelFormat::R32G32B32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
            case EPixelFormat::R32G32_FLOAT: return VK_FORMAT_R32G32_SFLOAT;
            case EPixelFormat::R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
            case EPixelFormat::D32_FLOAT: return VK_FORMAT_D32_SFLOAT;
            case EPixelFormat::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
            case EPixelFormat::BC1_UNORM: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
            case EPixelFormat::BC1_SRGB: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
            case EPixelFormat::BC3_UNORM: return VK_FORMAT_BC3_UNORM_BLOCK;
            case EPixelFormat::BC3_SRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
            default: return VK_FORMAT_UNDEFINED;
        }
    }
    
    EPixelFormat getVulkanToRHIFormat(VkFormat format) {
        switch (format) {
            case VK_FORMAT_R8G8B8A8_UNORM: return EPixelFormat::R8G8B8A8_UNORM;
            case VK_FORMAT_B8G8R8A8_UNORM: return EPixelFormat::B8G8R8A8_UNORM;
            case VK_FORMAT_R8G8B8A8_SRGB: return EPixelFormat::R8G8B8A8_SRGB;
            case VK_FORMAT_B8G8R8A8_SRGB: return EPixelFormat::B8G8R8A8_SRGB;
            case VK_FORMAT_R32G32B32A32_SFLOAT: return EPixelFormat::R32G32B32A32_FLOAT;
            case VK_FORMAT_R32G32B32_SFLOAT: return EPixelFormat::R32G32B32_FLOAT;
            case VK_FORMAT_R32G32_SFLOAT: return EPixelFormat::R32G32_FLOAT;
            case VK_FORMAT_R32_SFLOAT: return EPixelFormat::R32_FLOAT;
            case VK_FORMAT_D32_SFLOAT: return EPixelFormat::D32_FLOAT;
            case VK_FORMAT_D24_UNORM_S8_UINT: return EPixelFormat::D24_UNORM_S8_UINT;
            case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return EPixelFormat::BC1_UNORM;
            case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return EPixelFormat::BC1_SRGB;
            case VK_FORMAT_BC3_UNORM_BLOCK: return EPixelFormat::BC3_UNORM;
            case VK_FORMAT_BC3_SRGB_BLOCK: return EPixelFormat::BC3_SRGB;
            default: return EPixelFormat::Unknown;
        }
    }
    
    VkBufferUsageFlags getBufferUsageFlags(EResourceUsage usage) {
        VkBufferUsageFlags flags = 0;
        
        if (hasResourceUsage(usage, EResourceUsage::VertexBuffer)) {
            flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        }
        if (hasResourceUsage(usage, EResourceUsage::IndexBuffer)) {
            flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        }
        if (hasResourceUsage(usage, EResourceUsage::UniformBuffer)) {
            flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        }
        if (hasResourceUsage(usage, EResourceUsage::StorageBuffer)) {
            flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        }
        if (hasResourceUsage(usage, EResourceUsage::TransferSrc)) {
            flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        }
        if (hasResourceUsage(usage, EResourceUsage::TransferDst)) {
            flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }
        
        return flags;
    }
    
    VkImageUsageFlags getImageUsageFlags(EResourceUsage usage) {
        VkImageUsageFlags flags = 0;
        
        if (hasResourceUsage(usage, EResourceUsage::RenderTarget)) {
            flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        if (hasResourceUsage(usage, EResourceUsage::DepthStencil)) {
            flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        if (hasResourceUsage(usage, EResourceUsage::ShaderResource)) {
            flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        if (hasResourceUsage(usage, EResourceUsage::UnorderedAccess)) {
            flags |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
        if (hasResourceUsage(usage, EResourceUsage::TransferSrc)) {
            flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        if (hasResourceUsage(usage, EResourceUsage::TransferDst)) {
            flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        
        return flags;
    }
    
    VkPrimitiveTopology getPrimitiveTopology(EPrimitiveTopology topology) {
        switch (topology) {
            case EPrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case EPrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case EPrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case EPrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case EPrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
    }
    
    uint32 findMemoryType(const VkPhysicalDeviceMemoryProperties& memProperties, 
                         uint32 typeFilter, VkMemoryPropertyFlags properties) {
        for (uint32 i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && 
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        
        MR_LOG_ERROR("Failed to find suitable memory type");
        return 0;
    }
    
    VkResult createSurface(VkInstance instance, void* windowHandle, VkSurfaceKHR* surface) {
        const auto& functions = VulkanAPI::getFunctions();
        
#if PLATFORM_WINDOWS
        VkWin32SurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd = static_cast<HWND>(windowHandle);
        createInfo.hinstance = GetModuleHandle(nullptr);
        
        if (functions.vkCreateWin32SurfaceKHR) {
            return functions.vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, surface);
        } else {
            MR_LOG_ERROR("vkCreateWin32SurfaceKHR not loaded");
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
        
#elif PLATFORM_LINUX
        // 注意：这里假设使用Xlib，实际项目中可能需要支持Wayland
        VkXlibSurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        createInfo.window = reinterpret_cast<Window>(windowHandle);
        createInfo.dpy = XOpenDisplay(nullptr); // 简化实现，实际应该传入Display*
        
        if (functions.vkCreateXlibSurfaceKHR) {
            return functions.vkCreateXlibSurfaceKHR(instance, &createInfo, nullptr, surface);
        } else {
            MR_LOG_ERROR("vkCreateXlibSurfaceKHR not loaded");
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
        
#else
        MR_LOG_ERROR("Unsupported platform for surface creation");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
    }
    
    VkResult createDebugMessenger(VkInstance instance, 
                                const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
                                VkDebugUtilsMessengerEXT* debugMessenger) {
        const auto& functions = VulkanAPI::getFunctions();
        
        if (functions.vkCreateDebugUtilsMessengerEXT) {
            return functions.vkCreateDebugUtilsMessengerEXT(instance, createInfo, nullptr, debugMessenger);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    
    void destroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger) {
        const auto& functions = VulkanAPI::getFunctions();
        
        if (functions.vkDestroyDebugUtilsMessengerEXT) {
            functions.vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }
    }
    
    VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData) {
        
        // 将Vulkan验证层消息转换为引擎日志
        if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            MR_LOG_ERROR("Vulkan Validation: " + String(callbackData->pMessage));
        } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            MR_LOG_WARNING("Vulkan Validation: " + String(callbackData->pMessage));
        } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
            MR_LOG_INFO("Vulkan Validation: " + String(callbackData->pMessage));
        } else {
            MR_LOG_DEBUG("Vulkan Validation: " + String(callbackData->pMessage));
        }
        
        return VK_FALSE; // 不中断Vulkan调用
    }
}
