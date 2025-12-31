#include "Platform/Vulkan/VulkanUtils.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Core/Log.h"

#if PLATFORM_WINDOWS
    #include <windows.h>
#elif PLATFORM_LINUX
    #include <X11/Xlib.h>
#endif

namespace MonsterRender::RHI::Vulkan {
    
    namespace VulkanUtils {
        
        VkFormat getRHIFormatToVulkan(EPixelFormat format) {
            switch (format) {
                case EPixelFormat::R8G8B8A8_UNORM:
                    return VK_FORMAT_R8G8B8A8_UNORM;
                case EPixelFormat::B8G8R8A8_UNORM:
                    return VK_FORMAT_B8G8R8A8_UNORM;
                case EPixelFormat::R8G8B8A8_SRGB:
                    return VK_FORMAT_R8G8B8A8_SRGB;
                case EPixelFormat::B8G8R8A8_SRGB:
                    return VK_FORMAT_B8G8R8A8_SRGB;
                case EPixelFormat::R32G32B32A32_FLOAT:
                    return VK_FORMAT_R32G32B32A32_SFLOAT;
                case EPixelFormat::R32G32B32_FLOAT:
                    return VK_FORMAT_R32G32B32_SFLOAT;
                case EPixelFormat::R32G32_FLOAT:
                    return VK_FORMAT_R32G32_SFLOAT;
                case EPixelFormat::R32_FLOAT:
                    return VK_FORMAT_R32_SFLOAT;
                case EPixelFormat::D32_FLOAT:
                    return VK_FORMAT_D32_SFLOAT;
                case EPixelFormat::D24_UNORM_S8_UINT:
                    return VK_FORMAT_D24_UNORM_S8_UINT;
                default:
                    MR_LOG_WARNING("Unknown pixel format, using R8G8B8A8_UNORM as default");
                    return VK_FORMAT_R8G8B8A8_UNORM;
            }
        }
        
        EPixelFormat getVulkanToRHIFormat(VkFormat format) {
            switch (format) {
                case VK_FORMAT_R8G8B8A8_UNORM:
                    return EPixelFormat::R8G8B8A8_UNORM;
                case VK_FORMAT_B8G8R8A8_UNORM:
                    return EPixelFormat::B8G8R8A8_UNORM;
                case VK_FORMAT_R8G8B8A8_SRGB:
                    return EPixelFormat::R8G8B8A8_SRGB;
                case VK_FORMAT_B8G8R8A8_SRGB:
                    return EPixelFormat::B8G8R8A8_SRGB;
                case VK_FORMAT_R32G32B32A32_SFLOAT:
                    return EPixelFormat::R32G32B32A32_FLOAT;
                case VK_FORMAT_R32G32B32_SFLOAT:
                    return EPixelFormat::R32G32B32_FLOAT;
                case VK_FORMAT_R32G32_SFLOAT:
                    return EPixelFormat::R32G32_FLOAT;
                case VK_FORMAT_R32_SFLOAT:
                    return EPixelFormat::R32_FLOAT;
                case VK_FORMAT_D32_SFLOAT:
                    return EPixelFormat::D32_FLOAT;
                case VK_FORMAT_D24_UNORM_S8_UINT:
                    return EPixelFormat::D24_UNORM_S8_UINT;
                default:
                    MR_LOG_WARNING("Unknown Vulkan format, using R8G8B8A8_UNORM as default");
                    return EPixelFormat::R8G8B8A8_UNORM;
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
            
            // Always add transfer bits for copying data
            flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            
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
            
            // Always add transfer bits for copying data
            flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            
            return flags;
        }
        
        VkPrimitiveTopology getPrimitiveTopology(EPrimitiveTopology topology) {
            switch (topology) {
                case EPrimitiveTopology::PointList:
                    return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                case EPrimitiveTopology::LineList:
                    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                case EPrimitiveTopology::LineStrip:
                    return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                case EPrimitiveTopology::TriangleList:
                    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                case EPrimitiveTopology::TriangleStrip:
                    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                default:
                    MR_LOG_WARNING("Unknown primitive topology, using triangle list as default");
                    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
            return UINT32_MAX;
        }
        
        VkResult createSurface(VkInstance instance, void* windowHandle, VkSurfaceKHR* surface) {
            if (!instance || !surface) {
                MR_LOG_ERROR("Invalid parameters for surface creation");
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            
            const auto& functions = VulkanAPI::getFunctions();
            
            #if PLATFORM_WINDOWS
                if (functions.vkCreateWin32SurfaceKHR && windowHandle) {
                    VkWin32SurfaceCreateInfoKHR createInfo{};
                    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                    createInfo.hinstance = GetModuleHandle(nullptr);
                    createInfo.hwnd = static_cast<HWND>(windowHandle);
                    
                    VkResult result = functions.vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, surface);
                    if (result == VK_SUCCESS) {
                        MR_LOG_INFO("Vulkan Win32 surface created successfully");
                    } else {
                        MR_LOG_ERROR("Failed to create Vulkan Win32 surface: " + std::to_string(result));
                    }
                    return result;
                } else {
                    MR_LOG_ERROR("Win32 surface creation function not available or invalid window handle");
                    return VK_ERROR_FEATURE_NOT_PRESENT;
                }
            #elif PLATFORM_LINUX
                if (functions.vkCreateXlibSurfaceKHR && windowHandle) {
                    // For X11/Xlib surface creation
                    VkXlibSurfaceCreateInfoKHR createInfo{};
                    createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
                    // Note: glfwGetX11Display() requires GLFW to be included
                    // For now, pass nullptr - will be fixed when GLFW is properly integrated
                    createInfo.dpy = nullptr; // static_cast<Display*>(glfwGetX11Display());
                    createInfo.window = reinterpret_cast<Window>(windowHandle);
                    
                    VkResult result = functions.vkCreateXlibSurfaceKHR(instance, &createInfo, nullptr, surface);
                    if (result == VK_SUCCESS) {
                        MR_LOG_INFO("Vulkan X11 surface created successfully");
                    } else {
                        MR_LOG_ERROR("Failed to create Vulkan X11 surface: " + std::to_string(result));
                    }
                    return result;
                } else {
                    MR_LOG_ERROR("X11 surface creation function not available or invalid window handle");
                    return VK_ERROR_FEATURE_NOT_PRESENT;
                }
            #else
                MR_LOG_ERROR("Surface creation not supported on this platform");
                return VK_ERROR_FEATURE_NOT_PRESENT;
            #endif
        }
        
        VkResult createDebugMessenger(VkInstance instance, 
                                    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
                                    VkDebugUtilsMessengerEXT* debugMessenger) {
            const auto& functions = VulkanAPI::getFunctions();
            if (functions.vkCreateDebugUtilsMessengerEXT != nullptr) {
                return functions.vkCreateDebugUtilsMessengerEXT(instance, createInfo, nullptr, debugMessenger);
            } else {
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            }
        }
        
        void destroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger) {
            const auto& functions = VulkanAPI::getFunctions();
            if (functions.vkDestroyDebugUtilsMessengerEXT != nullptr) {
                functions.vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
            }
        }
        
        VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
            void* userData) {
            
            // Convert Vulkan severity to our log level
            ELogLevel logLevel = ELogLevel::Debug;
            if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
                logLevel = ELogLevel::Error;
            } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
                logLevel = ELogLevel::Warning;
            } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
                logLevel = ELogLevel::Info;
            }
            
            // Format message type
            String typeString = "GENERAL";
            if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
                typeString = "VALIDATION";
            } else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
                typeString = "PERFORMANCE";
            }
            
            Logger::getInstance().log(logLevel, String("VULKAN [") + typeString + "]: " + callbackData->pMessage);
            
            return VK_FALSE;
        }
        
        VkImageAspectFlags getImageAspectMask(VkFormat format) {
            // Determine aspect mask based on format
            // Reference: UE5 FVulkanBarrierManager::GetAspectMask()
            
            switch (format) {
                // Depth-only formats
                case VK_FORMAT_D16_UNORM:
                case VK_FORMAT_D32_SFLOAT:
                case VK_FORMAT_X8_D24_UNORM_PACK32:
                    return VK_IMAGE_ASPECT_DEPTH_BIT;
                
                // Depth-stencil formats
                case VK_FORMAT_D16_UNORM_S8_UINT:
                case VK_FORMAT_D24_UNORM_S8_UINT:
                case VK_FORMAT_D32_SFLOAT_S8_UINT:
                    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                
                // Stencil-only formats
                case VK_FORMAT_S8_UINT:
                    return VK_IMAGE_ASPECT_STENCIL_BIT;
                
                // All other formats are color formats
                default:
                    return VK_IMAGE_ASPECT_COLOR_BIT;
            }
        }
    }
}