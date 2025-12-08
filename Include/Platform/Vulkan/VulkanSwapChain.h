#pragma once

/**
 * VulkanSwapChain.h
 * 
 * Vulkan SwapChain implementation
 * Reference: UE5 VulkanViewport
 */

#include "Core/CoreMinimal.h"
#include "RHI/IRHISwapChain.h"
#include "Platform/Vulkan/VulkanRHI.h"

namespace MonsterRender::RHI::Vulkan {

// Forward declarations
class VulkanDevice;

/**
 * Vulkan SwapChain implementation
 * Wraps VkSwapchainKHR and manages backbuffer textures
 */
class FVulkanSwapChain : public IRHISwapChain
{
public:
    FVulkanSwapChain(VulkanDevice* device, const SwapChainDesc& desc);
    virtual ~FVulkanSwapChain();
    
    // IRHISwapChain interface
    virtual TSharedPtr<IRHITexture> GetCurrentBackBuffer() override;
    virtual uint32 GetCurrentBackBufferIndex() const override;
    virtual uint32 GetBackBufferCount() const override;
    virtual EPixelFormat GetBackBufferFormat() const override;
    virtual void GetDimensions(uint32& outWidth, uint32& outHeight) const override;
    
    virtual ESwapChainStatus AcquireNextImage() override;
    virtual ESwapChainStatus Present() override;
    virtual bool Resize(uint32 newWidth, uint32 newHeight) override;
    
    virtual void SetVSync(bool enabled) override;
    virtual bool IsVSyncEnabled() const override;
    virtual void SetPresentMode(EPresentMode mode) override;
    virtual EPresentMode GetPresentMode() const override;
    
    virtual bool IsValid() const override;
    virtual TSharedPtr<IRHITexture> GetDepthStencilTexture() override;
    virtual const SwapChainDesc& GetDesc() const override { return m_desc; }
    virtual void SetDebugName(const FString& name) override;
    
    // Vulkan-specific accessors
    VkSwapchainKHR GetVkSwapchain() const { return m_swapchain; }
    VkSemaphore GetImageAvailableSemaphore() const;
    VkSemaphore GetRenderFinishedSemaphore() const;
    VkFence GetInFlightFence() const;
    
    /**
     * Get the current swapchain image view
     */
    VkImageView GetCurrentImageView() const;
    
    /**
     * Get the current swapchain image
     */
    VkImage GetCurrentImage() const;
    
    /**
     * Get the depth image view
     */
    VkImageView GetDepthImageView() const { return m_depthImageView; }
    
    /**
     * Get the render pass compatible with this swapchain
     */
    VkRenderPass GetRenderPass() const { return m_renderPass; }
    
    /**
     * Get the current framebuffer
     */
    VkFramebuffer GetCurrentFramebuffer() const;
    
private:
    /**
     * Create the Vulkan swapchain
     */
    bool CreateSwapchain();
    
    /**
     * Destroy the swapchain and associated resources
     */
    void DestroySwapchain();
    
    /**
     * Create swapchain image views
     */
    bool CreateImageViews();
    
    /**
     * Create depth buffer
     */
    bool CreateDepthBuffer();
    
    /**
     * Create render pass
     */
    bool CreateRenderPass();
    
    /**
     * Create framebuffers
     */
    bool CreateFramebuffers();
    
    /**
     * Create synchronization objects
     */
    bool CreateSyncObjects();
    
    /**
     * Choose surface format
     */
    VkSurfaceFormatKHR ChooseSurfaceFormat();
    
    /**
     * Choose present mode
     */
    VkPresentModeKHR ChoosePresentMode();
    
    /**
     * Choose swap extent
     */
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    
    /**
     * Convert present mode to Vulkan
     */
    VkPresentModeKHR ConvertPresentMode(EPresentMode mode) const;
    
    /**
     * Convert Vulkan present mode
     */
    EPresentMode ConvertVkPresentMode(VkPresentModeKHR mode) const;
    
private:
    VulkanDevice* m_device = nullptr;
    
    // Swapchain
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent = { 0, 0 };
    
    // Swapchain images
    TArray<VkImage> m_images;
    TArray<VkImageView> m_imageViews;
    TArray<TSharedPtr<IRHITexture>> m_backBufferTextures;
    
    // Depth buffer
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;
    TSharedPtr<IRHITexture> m_depthTexture;
    
    // Render pass and framebuffers
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    TArray<VkFramebuffer> m_framebuffers;
    
    // Synchronization
    static constexpr uint32 MaxFramesInFlight = 2;
    TArray<VkSemaphore> m_imageAvailableSemaphores;
    TArray<VkSemaphore> m_renderFinishedSemaphores;
    TArray<VkFence> m_inFlightFences;
    
    // State
    uint32 m_currentImageIndex = 0;
    uint32 m_currentFrame = 0;
    bool m_vsyncEnabled = true;
    EPresentMode m_presentMode = EPresentMode::VSync;
    bool m_needsRecreate = false;
};

} // namespace MonsterRender::RHI::Vulkan
