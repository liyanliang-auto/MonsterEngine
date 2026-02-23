// Copyright MonsterEngine Team. All Rights Reserved.

// VulkanRenderTargetCache.cpp - RenderPass and Framebuffer caching implementation

// Reference: UE5 VulkanRenderPass.cpp, VulkanFramebuffer.cpp



#include "Platform/Vulkan/VulkanRenderTargetCache.h"

#include "Platform/Vulkan/VulkanDevice.h"

#include "Platform/Vulkan/VulkanTexture.h"

#include "Platform/Vulkan/VulkanUtils.h"

#include "Core/Log.h"



namespace MonsterRender::RHI::Vulkan {



    // ============================================================================

    // FNV-1a Hash Helper (consistent with other cache implementations)

    // ============================================================================

    

    namespace {

        constexpr uint64 FNV1A_OFFSET_BASIS = 14695981039346656037ULL;

        constexpr uint64 FNV1A_PRIME = 1099511628211ULL;



        inline uint64 HashCombine(uint64 hash, uint64 value) {

            hash ^= value;

            hash *= FNV1A_PRIME;

            return hash;

        }



        inline uint64 HashCombine(uint64 hash, const void* data, size_t size) {

            const uint8* bytes = static_cast<const uint8*>(data);

            for (size_t i = 0; i < size; ++i) {

                hash ^= bytes[i];

                hash *= FNV1A_PRIME;

            }

            return hash;

        }

    }



    // ============================================================================

    // FVulkanRenderTargetLayout Implementation

    // ============================================================================



    uint64 FVulkanRenderTargetLayout::GetHash() const {

        uint64 hash = FNV1A_OFFSET_BASIS;

        

        // Hash color formats

        hash = HashCombine(hash, NumColorAttachments);

        for (uint32 i = 0; i < NumColorAttachments; ++i) {

            hash = HashCombine(hash, static_cast<uint64>(ColorFormats[i]));

        }

        

        // Hash depth format

        hash = HashCombine(hash, static_cast<uint64>(DepthStencilFormat));

        hash = HashCombine(hash, bHasDepthStencil ? 1ULL : 0ULL);

        

        // Hash sample count

        hash = HashCombine(hash, static_cast<uint64>(SampleCount));

        

        // Hash load/store ops

        hash = HashCombine(hash, static_cast<uint64>(ColorLoadOp));

        hash = HashCombine(hash, static_cast<uint64>(ColorStoreOp));

        hash = HashCombine(hash, static_cast<uint64>(DepthLoadOp));

        hash = HashCombine(hash, static_cast<uint64>(DepthStoreOp));

        hash = HashCombine(hash, static_cast<uint64>(StencilLoadOp));

        hash = HashCombine(hash, static_cast<uint64>(StencilStoreOp));

        

        // Hash final layout

        hash = HashCombine(hash, static_cast<uint64>(ColorFinalLayout));

        

        return hash;

    }



    bool FVulkanRenderTargetLayout::operator==(const FVulkanRenderTargetLayout& Other) const {

        if (NumColorAttachments != Other.NumColorAttachments) return false;

        

        for (uint32 i = 0; i < NumColorAttachments; ++i) {

            if (ColorFormats[i] != Other.ColorFormats[i]) return false;

        }

        

        return DepthStencilFormat == Other.DepthStencilFormat &&

               bHasDepthStencil == Other.bHasDepthStencil &&

               SampleCount == Other.SampleCount &&

               ColorLoadOp == Other.ColorLoadOp &&

               ColorStoreOp == Other.ColorStoreOp &&

               DepthLoadOp == Other.DepthLoadOp &&

               DepthStoreOp == Other.DepthStoreOp &&

               StencilLoadOp == Other.StencilLoadOp &&

               StencilStoreOp == Other.StencilStoreOp &&

               ColorFinalLayout == Other.ColorFinalLayout;

    }



    // ============================================================================

    // FVulkanRenderPassCache Implementation

    // ============================================================================



    FVulkanRenderPassCache::FVulkanRenderPassCache(VulkanDevice* InDevice)

        : m_device(InDevice) {

        MR_LOG_INFO("FVulkanRenderPassCache: Created");

    }



    FVulkanRenderPassCache::~FVulkanRenderPassCache() {

        Clear();

        MR_LOG_INFO("FVulkanRenderPassCache: Destroyed");

    }



    VkRenderPass FVulkanRenderPassCache::GetOrCreateRenderPass(const FVulkanRenderTargetLayout& Layout) {

        uint64 hash = Layout.GetHash();

        

        // Check cache

        VkRenderPass* cachedPass = m_cache.Find(hash);

        if (cachedPass) {

            MR_LOG_DEBUG("FVulkanRenderPassCache: Cache hit for hash " + std::to_string(hash));

            return *cachedPass;

        }

        

        // Create new render pass

        VkRenderPass renderPass = CreateRenderPass(Layout);

        if (renderPass != VK_NULL_HANDLE) {

            m_cache.Add(hash, renderPass);

            MR_LOG_DEBUG("FVulkanRenderPassCache: Created and cached render pass, hash=" + 

                        std::to_string(hash) + ", cache size=" + std::to_string(m_cache.Num()));

        }

        

        return renderPass;

    }



    void FVulkanRenderPassCache::Clear() {

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getLogicalDevice();

        

        for (auto It = m_cache.CreateIterator(); It; ++It) {

            if (It->Value != VK_NULL_HANDLE) {

                functions.vkDestroyRenderPass(device, It->Value, nullptr);

            }

        }

        m_cache.Empty();

        

        MR_LOG_DEBUG("FVulkanRenderPassCache: Cleared all cached render passes");

    }



    VkRenderPass FVulkanRenderPassCache::CreateRenderPass(const FVulkanRenderTargetLayout& Layout) {

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getLogicalDevice();

        

        MR_LOG(LogTemp, Log, "CreateRenderPass: NumColorAttachments=%u, bHasDepthStencil=%d, "

                   "DepthFormat=%u, DepthLoadOp=%u, DepthStoreOp=%u",

                   Layout.NumColorAttachments, Layout.bHasDepthStencil ? 1 : 0,

                   static_cast<uint32>(Layout.DepthStencilFormat),

                   static_cast<uint32>(Layout.DepthLoadOp),

                   static_cast<uint32>(Layout.DepthStoreOp));

        

        TArray<VkAttachmentDescription> attachments;

        TArray<VkAttachmentReference> colorRefs;

        VkAttachmentReference depthRef{};

        

        // ============================================================================

        // Color Attachments

        // ============================================================================

        

        for (uint32 i = 0; i < Layout.NumColorAttachments; ++i) {

            VkAttachmentDescription colorAttachment{};

            colorAttachment.format = Layout.ColorFormats[i];

            colorAttachment.samples = Layout.SampleCount;

            colorAttachment.loadOp = Layout.ColorLoadOp;

            colorAttachment.storeOp = Layout.ColorStoreOp;

            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;

            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

            

            // UE5 Pattern: initialLayout must match loadOp

            // LOAD requires valid layout (preserve existing content)

            // CLEAR can use UNDEFINED (discard existing content)

            colorAttachment.initialLayout = (Layout.ColorLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD) ?

                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;

            colorAttachment.finalLayout = Layout.ColorFinalLayout;

            

            attachments.push_back(colorAttachment);

            

            VkAttachmentReference colorRef{};

            colorRef.attachment = static_cast<uint32>(attachments.size() - 1);

            colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            colorRefs.push_back(colorRef);

        }

        

        // ============================================================================

        // Depth Attachment

        // ============================================================================

        

        if (Layout.bHasDepthStencil) {

            VkAttachmentDescription depthAttachment{};

            depthAttachment.format = Layout.DepthStencilFormat;

            depthAttachment.samples = Layout.SampleCount;

            depthAttachment.loadOp = Layout.DepthLoadOp;

            depthAttachment.storeOp = Layout.DepthStoreOp;

            depthAttachment.stencilLoadOp = Layout.StencilLoadOp;

            depthAttachment.stencilStoreOp = Layout.StencilStoreOp;

            

            // UE5 Pattern: initialLayout must match loadOp

            // LOAD requires valid layout (preserve existing content)

            // CLEAR can use UNDEFINED (discard existing content)

            depthAttachment.initialLayout = (Layout.DepthLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD) ?

                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;

            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            

            attachments.push_back(depthAttachment);

            

            depthRef.attachment = static_cast<uint32>(attachments.size() - 1);

            depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            

            MR_LOG(LogTemp, Log, "CreateRenderPass: Added depth attachment at index %u, format=%u, "

                       "loadOp=%u, storeOp=%u, initialLayout=%u, finalLayout=%u",

                       depthRef.attachment, static_cast<uint32>(depthAttachment.format),

                       static_cast<uint32>(depthAttachment.loadOp),

                       static_cast<uint32>(depthAttachment.storeOp),

                       static_cast<uint32>(depthAttachment.initialLayout),

                       static_cast<uint32>(depthAttachment.finalLayout));

        }

        

        // ============================================================================

        // Subpass Description

        // ============================================================================

        

        VkSubpassDescription subpass{};

        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

        subpass.colorAttachmentCount = static_cast<uint32>(colorRefs.size());

        subpass.pColorAttachments = colorRefs.data();

        subpass.pDepthStencilAttachment = Layout.bHasDepthStencil ? &depthRef : nullptr;

        

        // ============================================================================

        // Subpass Dependencies

        // Reference: UE5 FVulkanRenderPass handles synchronization

        // UE5 Pattern: Adjust dependency based on attachment types

        // ============================================================================

        

        VkSubpassDependency dependency{};

        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;

        dependency.dstSubpass = 0;

        

        // UE5 Pattern: Different dependencies for color vs depth-only passes

        if (Layout.NumColorAttachments > 0) {

            // Has color attachments - standard color+depth pass

            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            dependency.srcAccessMask = 0;

            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        } else {

            // Depth-only pass (e.g., shadow mapping) - no color attachment stages

            dependency.srcStageMask = 0;

            dependency.dstStageMask = 0;

            dependency.srcAccessMask = 0;

            dependency.dstAccessMask = 0;

        }

        

        if (Layout.bHasDepthStencil) {

            // UE5 uses both EARLY and LATE fragment tests for depth

            dependency.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

            dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

            dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        }

        

        // ============================================================================

        // Create Render Pass

        // ============================================================================

        

        VkRenderPassCreateInfo renderPassInfo{};

        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

        renderPassInfo.attachmentCount = static_cast<uint32>(attachments.size());

        renderPassInfo.pAttachments = attachments.data();

        renderPassInfo.subpassCount = 1;

        renderPassInfo.pSubpasses = &subpass;

        renderPassInfo.dependencyCount = 1;

        renderPassInfo.pDependencies = &dependency;

        

        VkRenderPass renderPass = VK_NULL_HANDLE;

        VkResult result = functions.vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);

        

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("FVulkanRenderPassCache: Failed to create render pass, result=" + 

                        std::to_string(result));

            return VK_NULL_HANDLE;

        }

        

        MR_LOG(LogTemp, Log, "CreateRenderPass: SUCCESS - renderPass=0x%llx, totalAttachments=%u, "

                   "colorAttachments=%u, hasDepth=%d",

                   reinterpret_cast<uint64>(renderPass),

                   static_cast<uint32>(attachments.size()),

                   Layout.NumColorAttachments,

                   Layout.bHasDepthStencil ? 1 : 0);

        

        return renderPass;

    }



    // ============================================================================

    // FVulkanFramebufferKey Implementation

    // ============================================================================



    uint64 FVulkanFramebufferKey::GetHash() const {

        uint64 hash = FNV1A_OFFSET_BASIS;

        

        hash = HashCombine(hash, reinterpret_cast<uint64>(RenderPass));

        hash = HashCombine(hash, static_cast<uint64>(Width));

        hash = HashCombine(hash, static_cast<uint64>(Height));

        hash = HashCombine(hash, static_cast<uint64>(Layers));

        hash = HashCombine(hash, static_cast<uint64>(NumAttachments));

        

        for (uint32 i = 0; i < NumAttachments; ++i) {

            hash = HashCombine(hash, reinterpret_cast<uint64>(Attachments[i]));

        }

        

        return hash;

    }



    bool FVulkanFramebufferKey::operator==(const FVulkanFramebufferKey& Other) const {

        if (RenderPass != Other.RenderPass) return false;

        if (Width != Other.Width) return false;

        if (Height != Other.Height) return false;

        if (Layers != Other.Layers) return false;

        if (NumAttachments != Other.NumAttachments) return false;

        

        for (uint32 i = 0; i < NumAttachments; ++i) {

            if (Attachments[i] != Other.Attachments[i]) return false;

        }

        

        return true;

    }



    // ============================================================================

    // FVulkanFramebufferCache Implementation

    // ============================================================================



    FVulkanFramebufferCache::FVulkanFramebufferCache(VulkanDevice* InDevice)

        : m_device(InDevice) {

        MR_LOG_INFO("FVulkanFramebufferCache: Created");

    }



    FVulkanFramebufferCache::~FVulkanFramebufferCache() {

        Clear();

        MR_LOG_INFO("FVulkanFramebufferCache: Destroyed");

    }



    VkFramebuffer FVulkanFramebufferCache::GetOrCreateFramebuffer(const FVulkanFramebufferKey& Key) {

        uint64 hash = Key.GetHash();

        

        // Check cache

        VkFramebuffer* cachedFB = m_cache.Find(hash);

        if (cachedFB) {

            MR_LOG_DEBUG("FVulkanFramebufferCache: Cache hit for hash " + std::to_string(hash));

            return *cachedFB;

        }

        

        // Create new framebuffer

        VkFramebuffer framebuffer = CreateFramebuffer(Key);

        if (framebuffer != VK_NULL_HANDLE) {

            m_cache.Add(hash, framebuffer);

            MR_LOG_DEBUG("FVulkanFramebufferCache: Created and cached framebuffer, hash=" + 

                        std::to_string(hash) + ", cache size=" + std::to_string(m_cache.Num()));

        }

        

        return framebuffer;

    }



    void FVulkanFramebufferCache::Clear() {

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getLogicalDevice();

        

        for (auto It = m_cache.CreateIterator(); It; ++It) {

            if (It->Value != VK_NULL_HANDLE) {

                functions.vkDestroyFramebuffer(device, It->Value, nullptr);

            }

        }

        m_cache.Empty();

        

        MR_LOG_DEBUG("FVulkanFramebufferCache: Cleared all cached framebuffers");

    }



    VkFramebuffer FVulkanFramebufferCache::CreateFramebuffer(const FVulkanFramebufferKey& Key) {

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getLogicalDevice();

        

        MR_LOG_INFO("CreateFramebuffer: " + std::to_string(Key.Width) + "x" + std::to_string(Key.Height) +

                   ", attachments=" + std::to_string(Key.NumAttachments) +

                   ", colorView=" + std::to_string(reinterpret_cast<uint64>(Key.Attachments[0])) +

                   ", depthView=" + std::to_string(reinterpret_cast<uint64>(Key.NumAttachments > 1 ? Key.Attachments[1] : VK_NULL_HANDLE)) +

                   ", renderPass=" + std::to_string(reinterpret_cast<uint64>(Key.RenderPass)));

        

        VkFramebufferCreateInfo framebufferInfo{};

        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;

        framebufferInfo.renderPass = Key.RenderPass;

        framebufferInfo.attachmentCount = Key.NumAttachments;

        framebufferInfo.pAttachments = Key.Attachments;

        framebufferInfo.width = Key.Width;

        framebufferInfo.height = Key.Height;

        framebufferInfo.layers = Key.Layers;

        

        VkFramebuffer framebuffer = VK_NULL_HANDLE;

        VkResult result = functions.vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer);

        

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("FVulkanFramebufferCache: Failed to create framebuffer, result=" + 

                        std::to_string(result));

            return VK_NULL_HANDLE;

        }

        

        MR_LOG_DEBUG("FVulkanFramebufferCache: Created framebuffer " + 

                    std::to_string(Key.Width) + "x" + std::to_string(Key.Height) +

                    " with " + std::to_string(Key.NumAttachments) + " attachment(s)");

        

        return framebuffer;

    }



    // ============================================================================

    // FVulkanRenderTargetInfo Implementation

    // ============================================================================



    FVulkanRenderTargetLayout FVulkanRenderTargetInfo::BuildLayout(VulkanDevice* Device) const {

        FVulkanRenderTargetLayout layout;

        

        // Color attachments

        layout.NumColorAttachments = NumColorTargets;

        for (uint32 i = 0; i < NumColorTargets; ++i) {

            if (ColorTargets[i]) {

                // Use VulkanTexture's native Vulkan format

                layout.ColorFormats[i] = ColorTargets[i]->getVulkanFormat();

            } else if (bIsSwapchain && Device) {

                // Swapchain mode - get format from device

                // This handles the case where we render to swapchain with custom depth

                layout.ColorFormats[i] = Device->getSwapchainFormat();

            }

        }

        

        // Depth attachment

        if (DepthStencilTarget) {

            layout.bHasDepthStencil = true;

            // Use VulkanTexture's native Vulkan format

            layout.DepthStencilFormat = DepthStencilTarget->getVulkanFormat();

        }

        

        // Load/store ops based on clear flags

        layout.ColorLoadOp = bClearColor[0] ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;

        layout.ColorStoreOp = VK_ATTACHMENT_STORE_OP_STORE;

        layout.DepthLoadOp = bClearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;

        

        // UE5 Pattern: Shadow mapping (depth-only RTT) needs to store depth for later sampling

        // Regular depth buffers can use DONT_CARE since they're not read after rendering

        layout.DepthStoreOp = (NumColorTargets == 0 && DepthStencilTarget) ? 

            VK_ATTACHMENT_STORE_OP_STORE :      // Shadow map - need to read depth later

            VK_ATTACHMENT_STORE_OP_DONT_CARE;   // Regular depth - don't need after rendering

        

        layout.StencilLoadOp = bClearStencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        layout.StencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        

        // Final layout

        // For RTT, use SHADER_READ_ONLY_OPTIMAL since the texture will be sampled later

        // For swapchain, use PRESENT_SRC_KHR for presentation

        layout.ColorFinalLayout = bIsSwapchain ? 

            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : 

            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        

        return layout;

    }



    FVulkanFramebufferKey FVulkanRenderTargetInfo::BuildFramebufferKey(VkRenderPass RenderPass,

                                                                       VkImageView DepthView) const {

        FVulkanFramebufferKey key;

        key.RenderPass = RenderPass;

        key.NumAttachments = 0;

        key.Width = 0;

        key.Height = 0;

        

        // Determine dimensions - priority: explicit RenderArea > ColorTarget > DepthTarget

        if (RenderAreaWidth > 0 && RenderAreaHeight > 0) {

            // Use explicitly set render area

            key.Width = RenderAreaWidth;

            key.Height = RenderAreaHeight;

        } else if (NumColorTargets > 0 && ColorTargets[0]) {

            // Use first color target dimensions

            auto& desc = ColorTargets[0]->getDesc();

            key.Width = desc.width;

            key.Height = desc.height;

        } else if (DepthStencilTarget) {

            // Depth-only case: use depth target dimensions

            auto& desc = DepthStencilTarget->getDesc();

            key.Width = desc.width;

            key.Height = desc.height;

        }

        

        MR_LOG(LogTemp, Log, "BuildFramebufferKey: NumColorTargets=%u, bIsSwapchain=%d, HasDepthTarget=%d, "

                   "RenderArea=%ux%u, FinalSize=%ux%u",

                   NumColorTargets, bIsSwapchain ? 1 : 0, DepthStencilTarget ? 1 : 0,

                   RenderAreaWidth, RenderAreaHeight, key.Width, key.Height);

        

        // Color attachments - check for swapchain image view first

        if (bIsSwapchain && SwapchainImageView != VK_NULL_HANDLE) {

            // Use swapchain image view as color attachment

            key.Attachments[key.NumAttachments++] = SwapchainImageView;

            MR_LOG(LogTemp, Log, "BuildFramebufferKey: Added swapchain color attachment, view=0x%llx",

                       reinterpret_cast<uint64>(SwapchainImageView));

        } else {

            for (uint32 i = 0; i < NumColorTargets; ++i) {

                if (ColorTargets[i]) {

                    key.Attachments[key.NumAttachments++] = ColorTargets[i]->getImageView();

                    MR_LOG(LogTemp, Log, "BuildFramebufferKey: Added color attachment %u, view=0x%llx",

                               i, reinterpret_cast<uint64>(ColorTargets[i]->getImageView()));

                }

            }

        }

        

        // Depth attachment

        if (DepthStencilTarget) {

            VkImageView depthImageView = DepthStencilTarget->getImageView();

            key.Attachments[key.NumAttachments++] = depthImageView;

            MR_LOG(LogTemp, Log, "BuildFramebufferKey: Added DepthStencilTarget depth attachment, view=0x%llx",

                       reinterpret_cast<uint64>(depthImageView));

        } else if (DepthView != VK_NULL_HANDLE) {

            // Use provided depth view (e.g., device's default depth buffer)

            key.Attachments[key.NumAttachments++] = DepthView;

            MR_LOG(LogTemp, Log, "BuildFramebufferKey: Added provided DepthView, view=0x%llx",

                       reinterpret_cast<uint64>(DepthView));

        }

        

        MR_LOG(LogTemp, Log, "BuildFramebufferKey: Final NumAttachments=%u", key.NumAttachments);

        

        key.Layers = 1;

        

        return key;

    }



} // namespace MonsterRender::RHI::Vulkan

