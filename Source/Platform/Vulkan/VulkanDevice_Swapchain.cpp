// Copyright Monster Engine. All Rights Reserved.

#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Core/Logging/LogMacros.h"
#include <algorithm>

DEFINE_LOG_CATEGORY_STATIC(LogVulkanSwapchain, Log, All);

namespace MonsterRender {
namespace RHI {
namespace Vulkan {

    bool VulkanDevice::recreateSwapchain(uint32 newWidth, uint32 newHeight)
    {
        // Reference: UE5 FVulkanDynamicRHI::RecreateSwapChain
        MR_LOG(LogVulkanSwapchain, Log, "Recreating swapchain for new size: %ux%u", newWidth, newHeight);
        
        // Handle minimization - don't recreate with 0 size
        if (newWidth == 0 || newHeight == 0)
        {
            MR_LOG(LogVulkanSwapchain, Warning, "Window minimized, skipping swapchain recreation");
            return false;
        }
        
        // Wait for device to finish all operations
        waitForIdle();
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // ============================================================================
        // Step 1: Destroy old framebuffers
        // Reference: UE5 FVulkanSwapChain::Destroy
        // ============================================================================
        for (auto framebuffer : m_swapchainFramebuffers)
        {
            if (framebuffer != VK_NULL_HANDLE)
            {
                functions.vkDestroyFramebuffer(m_device, framebuffer, nullptr);
            }
        }
        m_swapchainFramebuffers.clear();
        
        // ============================================================================
        // Step 2: Destroy old depth resources
        // ============================================================================
        destroyDepthResources();
        
        // ============================================================================
        // Step 3: Destroy old image views
        // ============================================================================
        for (auto imageView : m_swapchainImageViews)
        {
            if (imageView != VK_NULL_HANDLE)
            {
                functions.vkDestroyImageView(m_device, imageView, nullptr);
            }
        }
        m_swapchainImageViews.clear();
        
        // ============================================================================
        // Step 4: Query new swapchain capabilities
        // Reference: UE5 FVulkanSwapChain::Create
        // ============================================================================
        VkSurfaceCapabilitiesKHR capabilities;
        functions.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);
        
        // Get surface formats
        uint32 formatCount;
        functions.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        functions.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
        
        // Get present modes
        uint32 presentModeCount;
        functions.vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        functions.vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());
        
        // Choose surface format (prefer SRGB)
        VkSurfaceFormatKHR surfaceFormat = formats[0];
        for (const auto& availableFormat : formats)
        {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && 
                availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                surfaceFormat = availableFormat;
                break;
            }
        }
        
        // Choose present mode (prefer mailbox for reduced latency)
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (const auto& availablePresentMode : presentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                presentMode = availablePresentMode;
                break;
            }
        }
        
        // Choose swap extent
        VkExtent2D extent;
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            extent = capabilities.currentExtent;
        }
        else
        {
            extent = {newWidth, newHeight};
            extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }
        
        // Choose image count
        uint32 imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        {
            imageCount = capabilities.maxImageCount;
        }
        
        // ============================================================================
        // Step 5: Create new swapchain with old swapchain as reference
        // Reference: UE5 FVulkanSwapChain::Create with oldSwapchain parameter
        // ============================================================================
        VkSwapchainKHR oldSwapchain = m_swapchain;
        
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        
        uint32 queueFamilyIndices[] = {m_graphicsQueueFamily.familyIndex, m_presentQueueFamily.familyIndex};
        
        if (m_graphicsQueueFamily.familyIndex != m_presentQueueFamily.familyIndex)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }
        
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = oldSwapchain;  // Reuse old swapchain for efficiency
        
        VkResult result = functions.vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain);
        
        // Destroy old swapchain after new one is created
        if (oldSwapchain != VK_NULL_HANDLE)
        {
            functions.vkDestroySwapchainKHR(m_device, oldSwapchain, nullptr);
        }
        
        if (result != VK_SUCCESS)
        {
            MR_LOG(LogVulkanSwapchain, Error, "Failed to recreate swapchain! Result: %d", result);
            return false;
        }
        
        // ============================================================================
        // Step 6: Get new swapchain images
        // ============================================================================
        functions.vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
        m_swapchainImages.resize(imageCount);
        functions.vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());
        
        m_swapchainImageFormat = surfaceFormat.format;
        m_swapchainExtent = extent;
        
        // ============================================================================
        // Step 7: Create new image views
        // ============================================================================
        m_swapchainImageViews.resize(m_swapchainImages.size());
        for (size_t i = 0; i < m_swapchainImages.size(); i++)
        {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_swapchainImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_swapchainImageFormat;
            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            
            result = functions.vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainImageViews[i]);
            
            if (result != VK_SUCCESS)
            {
                MR_LOG(LogVulkanSwapchain, Error, "Failed to create image view! Result: %d", result);
                return false;
            }
        }
        
        // ============================================================================
        // Step 8: Recreate depth resources with new extent
        // ============================================================================
        if (!createDepthResources())
        {
            MR_LOG(LogVulkanSwapchain, Error, "Failed to recreate depth resources");
            return false;
        }
        
        // ============================================================================
        // Step 9: Recreate framebuffers with new extent
        // ============================================================================
        if (!createFramebuffers())
        {
            MR_LOG(LogVulkanSwapchain, Error, "Failed to recreate framebuffers");
            return false;
        }
        
        // Reset per-image fence tracking
        m_imagesInFlight.resize(m_swapchainImages.size(), VK_NULL_HANDLE);
        
        MR_LOG(LogVulkanSwapchain, Log, "Swapchain recreated successfully: %ux%u, %u images", 
               m_swapchainExtent.width, m_swapchainExtent.height, imageCount);
        
        return true;
    }

} // namespace Vulkan
} // namespace RHI
} // namespace MonsterRender
