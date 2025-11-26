// Copyright MonsterEngine Team. All Rights Reserved.
// VulkanRenderTargetCache.h - RenderPass and Framebuffer caching for RTT support
// Reference: UE5 FVulkanRenderPassCache, FVulkanFramebufferCache

#pragma once

#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "RHI/RHIDefinitions.h"

namespace MonsterRender::RHI::Vulkan {

    class VulkanDevice;
    class VulkanTexture;

    // ============================================================================
    // FVulkanRenderTargetLayout - Describes render target configuration
    // Reference: UE5 FVulkanRenderTargetLayout
    // ============================================================================

    /**
     * Describes the layout of render targets for a render pass
     * Used as a key for caching render passes
     */
    struct FVulkanRenderTargetLayout {
        // Color attachment formats (up to 8 MRT)
        static constexpr uint32 MaxColorAttachments = 8;
        VkFormat ColorFormats[MaxColorAttachments] = {};
        uint32 NumColorAttachments = 0;
        
        // Depth/stencil format
        VkFormat DepthStencilFormat = VK_FORMAT_UNDEFINED;
        bool bHasDepthStencil = false;
        
        // Sample count for MSAA
        VkSampleCountFlagBits SampleCount = VK_SAMPLE_COUNT_1_BIT;
        
        // Load/store operations
        VkAttachmentLoadOp ColorLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp ColorStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkAttachmentLoadOp DepthLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp DepthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkAttachmentLoadOp StencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkAttachmentStoreOp StencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        
        // Final layout for color attachments
        // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for swapchain, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL for RTT
        VkImageLayout ColorFinalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        
        // Compute hash for cache lookup
        uint64 GetHash() const;
        
        // Comparison operator
        bool operator==(const FVulkanRenderTargetLayout& Other) const;
        bool operator!=(const FVulkanRenderTargetLayout& Other) const { return !(*this == Other); }
    };

    // ============================================================================
    // FVulkanRenderPassCache - Caches VkRenderPass objects
    // Reference: UE5 FVulkanRenderPassCache
    // ============================================================================

    /**
     * Caches render passes based on their layout
     * RenderPasses are expensive to create, so we cache them for reuse
     */
    class FVulkanRenderPassCache {
    public:
        FVulkanRenderPassCache(VulkanDevice* InDevice);
        ~FVulkanRenderPassCache();

        // Non-copyable
        FVulkanRenderPassCache(const FVulkanRenderPassCache&) = delete;
        FVulkanRenderPassCache& operator=(const FVulkanRenderPassCache&) = delete;

        /**
         * Get or create a render pass for the given layout
         * @param Layout - The render target layout
         * @return The cached or newly created render pass
         */
        VkRenderPass GetOrCreateRenderPass(const FVulkanRenderTargetLayout& Layout);

        /**
         * Clear all cached render passes
         */
        void Clear();

        /**
         * Get statistics about the cache
         */
        uint32 GetCacheSize() const { return static_cast<uint32>(m_cache.size()); }

    private:
        VkRenderPass CreateRenderPass(const FVulkanRenderTargetLayout& Layout);

        VulkanDevice* m_device;
        TMap<uint64, VkRenderPass> m_cache;
    };

    // ============================================================================
    // FVulkanFramebufferKey - Key for framebuffer cache lookup
    // Reference: UE5 FVulkanFramebuffer
    // ============================================================================

    /**
     * Key used for framebuffer cache lookup
     */
    struct FVulkanFramebufferKey {
        VkRenderPass RenderPass = VK_NULL_HANDLE;
        uint32 Width = 0;
        uint32 Height = 0;
        uint32 Layers = 1;
        
        // Image views for attachments
        static constexpr uint32 MaxAttachments = 9; // 8 color + 1 depth
        VkImageView Attachments[MaxAttachments] = {};
        uint32 NumAttachments = 0;

        uint64 GetHash() const;
        bool operator==(const FVulkanFramebufferKey& Other) const;
    };

    // ============================================================================
    // FVulkanFramebufferCache - Caches VkFramebuffer objects
    // Reference: UE5 FVulkanFramebufferCache
    // ============================================================================

    /**
     * Caches framebuffers based on their configuration
     * Framebuffers must match the render pass and attachment dimensions
     */
    class FVulkanFramebufferCache {
    public:
        FVulkanFramebufferCache(VulkanDevice* InDevice);
        ~FVulkanFramebufferCache();

        // Non-copyable
        FVulkanFramebufferCache(const FVulkanFramebufferCache&) = delete;
        FVulkanFramebufferCache& operator=(const FVulkanFramebufferCache&) = delete;

        /**
         * Get or create a framebuffer for the given key
         * @param Key - The framebuffer configuration
         * @return The cached or newly created framebuffer
         */
        VkFramebuffer GetOrCreateFramebuffer(const FVulkanFramebufferKey& Key);

        /**
         * Clear all cached framebuffers
         */
        void Clear();

        /**
         * Get statistics about the cache
         */
        uint32 GetCacheSize() const { return static_cast<uint32>(m_cache.size()); }

    private:
        VkFramebuffer CreateFramebuffer(const FVulkanFramebufferKey& Key);

        VulkanDevice* m_device;
        TMap<uint64, VkFramebuffer> m_cache;
    };

    // ============================================================================
    // FVulkanRenderTargetInfo - Runtime render target information
    // Reference: UE5 FRHIRenderPassInfo
    // ============================================================================

    /**
     * Runtime information about render targets being used
     * Used when calling setRenderTargets
     */
    struct FVulkanRenderTargetInfo {
        // Color render targets
        static constexpr uint32 MaxColorTargets = 8;
        TSharedPtr<VulkanTexture> ColorTargets[MaxColorTargets] = {};
        uint32 NumColorTargets = 0;
        
        // Depth/stencil target
        TSharedPtr<VulkanTexture> DepthStencilTarget;
        
        // Clear values
        VkClearColorValue ClearColors[MaxColorTargets] = {};
        VkClearDepthStencilValue ClearDepthStencil = {1.0f, 0};
        
        // Whether to clear each target
        bool bClearColor[MaxColorTargets] = {};
        bool bClearDepth = true;
        bool bClearStencil = false;
        
        // Render area (0 means use full texture size)
        uint32 RenderAreaWidth = 0;
        uint32 RenderAreaHeight = 0;
        
        // Whether this renders to swapchain (affects final layout)
        bool bIsSwapchain = false;
        
        /**
         * Build layout from this info
         */
        FVulkanRenderTargetLayout BuildLayout() const;
        
        /**
         * Build framebuffer key from this info
         * @param RenderPass - The render pass to use
         * @param DepthView - Optional depth image view (for swapchain depth)
         */
        FVulkanFramebufferKey BuildFramebufferKey(VkRenderPass RenderPass, 
                                                  VkImageView DepthView = VK_NULL_HANDLE) const;
    };

} // namespace MonsterRender::RHI::Vulkan
