// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Resource State Tracker Implementation
//
// Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanBarriers.cpp
//            UE5 resource state tracking and barrier insertion

#include "Platform/Vulkan/VulkanResourceStateTracker.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Core/Log.h"

DEFINE_LOG_CATEGORY_STATIC(LogVulkanStateTracker, Log, All);

namespace MonsterRender::RHI::Vulkan {

using namespace MonsterRender::RDG;

// ============================================================================
// FVulkanImageLayout Implementation
// ============================================================================

void FVulkanImageLayout::Set(VkImageLayout Layout, const VkImageSubresourceRange& SubresourceRange) {
    // Calculate the number of layers and mips in the range
    uint32 LayerCount = (SubresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS) 
        ? (NumLayers - SubresourceRange.baseArrayLayer) 
        : SubresourceRange.layerCount;
    
    uint32 MipCount = (SubresourceRange.levelCount == VK_REMAINING_MIP_LEVELS)
        ? (NumMips - SubresourceRange.baseMipLevel)
        : SubresourceRange.levelCount;
    
    // If setting all subresources to the same layout, we can use MainLayout
    if (SubresourceRange.baseArrayLayer == 0 && LayerCount == NumLayers &&
        SubresourceRange.baseMipLevel == 0 && MipCount == NumMips) {
        MainLayout = Layout;
        SubresLayouts.clear();
        return;
    }
    
    // Need to track individual subresource layouts
    if (SubresLayouts.empty()) {
        // Allocate subresource layout array
        uint32 TotalSubresources = NumPlanes * NumLayers * NumMips;
        SubresLayouts.resize(TotalSubresources);
        
        // Initialize all to MainLayout
        for (uint32 i = 0; i < TotalSubresources; ++i) {
            SubresLayouts[i] = MainLayout;
        }
    }
    
    // Update the specified range
    uint32 PlaneCount = (SubresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) ? 2 : 1;
    
    for (uint32 Plane = 0; Plane < PlaneCount; ++Plane) {
        for (uint32 Layer = SubresourceRange.baseArrayLayer; Layer < SubresourceRange.baseArrayLayer + LayerCount; ++Layer) {
            for (uint32 Mip = SubresourceRange.baseMipLevel; Mip < SubresourceRange.baseMipLevel + MipCount; ++Mip) {
                uint32 Index = (Plane * NumLayers * NumMips) + (Layer * NumMips) + Mip;
                SubresLayouts[Index] = Layout;
            }
        }
    }
}

void FVulkanImageLayout::CollapseSubresLayoutsIfSame() {
    if (SubresLayouts.empty()) {
        return;
    }
    
    // Check if all subresources have the same layout
    VkImageLayout FirstLayout = SubresLayouts[0];
    bool bAllSame = true;
    
    for (const VkImageLayout& Layout : SubresLayouts) {
        if (Layout != FirstLayout) {
            bAllSame = false;
            break;
        }
    }
    
    // If all same, collapse to MainLayout
    if (bAllSame) {
        MainLayout = FirstLayout;
        SubresLayouts.clear();
    }
}

bool FVulkanImageLayout::AreSubresourcesSameLayout(VkImageLayout Layout, const VkImageSubresourceRange& SubresourceRange) const {
    if (SubresLayouts.empty()) {
        return MainLayout == Layout;
    }
    
    uint32 LayerCount = (SubresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS)
        ? (NumLayers - SubresourceRange.baseArrayLayer)
        : SubresourceRange.layerCount;
    
    uint32 MipCount = (SubresourceRange.levelCount == VK_REMAINING_MIP_LEVELS)
        ? (NumMips - SubresourceRange.baseMipLevel)
        : SubresourceRange.levelCount;
    
    uint32 PlaneCount = (SubresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) ? 2 : 1;
    
    for (uint32 Plane = 0; Plane < PlaneCount; ++Plane) {
        for (uint32 Layer = SubresourceRange.baseArrayLayer; Layer < SubresourceRange.baseArrayLayer + LayerCount; ++Layer) {
            for (uint32 Mip = SubresourceRange.baseMipLevel; Mip < SubresourceRange.baseMipLevel + MipCount; ++Mip) {
                uint32 Index = (Plane * NumLayers * NumMips) + (Layer * NumMips) + Mip;
                if (SubresLayouts[Index] != Layout) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

// ============================================================================
// FVulkanPipelineBarrier Implementation
// ============================================================================

void FVulkanPipelineBarrier::AddMemoryBarrier(VkAccessFlags SrcAccessMask, VkAccessFlags DstAccessMask,
                                              VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage) {
    VkMemoryBarrier Barrier = {};
    Barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Barrier.srcAccessMask = SrcAccessMask;
    Barrier.dstAccessMask = DstAccessMask;
    
    MemoryBarriers.push_back(Barrier);
    
    SrcStageMask |= SrcStage;
    DstStageMask |= DstStage;
}

void FVulkanPipelineBarrier::AddImageLayoutTransition(VkImage Image, VkImageLayout SrcLayout, VkImageLayout DstLayout,
                                                      const VkImageSubresourceRange& SubresourceRange,
                                                      VkAccessFlags SrcAccessMask, VkAccessFlags DstAccessMask,
                                                      VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage) {
    VkImageMemoryBarrier Barrier = {};
    Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.srcAccessMask = SrcAccessMask;
    Barrier.dstAccessMask = DstAccessMask;
    Barrier.oldLayout = SrcLayout;
    Barrier.newLayout = DstLayout;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image = Image;
    Barrier.subresourceRange = SubresourceRange;
    
    ImageBarriers.push_back(Barrier);
    
    SrcStageMask |= SrcStage;
    DstStageMask |= DstStage;
}

void FVulkanPipelineBarrier::AddBufferBarrier(VkBuffer Buffer, VkAccessFlags SrcAccessMask, VkAccessFlags DstAccessMask,
                                              VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage,
                                              VkDeviceSize Offset, VkDeviceSize Size) {
    VkBufferMemoryBarrier Barrier = {};
    Barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    Barrier.srcAccessMask = SrcAccessMask;
    Barrier.dstAccessMask = DstAccessMask;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.buffer = Buffer;
    Barrier.offset = Offset;
    Barrier.size = Size;
    
    BufferBarriers.push_back(Barrier);
    
    SrcStageMask |= SrcStage;
    DstStageMask |= DstStage;
}

void FVulkanPipelineBarrier::Execute(VkCommandBuffer CmdBuffer) {
    if (!HasBarriers()) {
        return;
    }
    
    // Use TOP_OF_PIPE if no specific stage was set
    VkPipelineStageFlags SrcStages = (SrcStageMask != 0) ? SrcStageMask : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags DstStages = (DstStageMask != 0) ? DstStageMask : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    
    vkCmdPipelineBarrier(
        CmdBuffer,
        SrcStages,
        DstStages,
        0, // Dependency flags
        static_cast<uint32>(MemoryBarriers.size()),
        MemoryBarriers.empty() ? nullptr : MemoryBarriers.data(),
        static_cast<uint32>(BufferBarriers.size()),
        BufferBarriers.empty() ? nullptr : BufferBarriers.data(),
        static_cast<uint32>(ImageBarriers.size()),
        ImageBarriers.empty() ? nullptr : ImageBarriers.data()
    );
    
    MR_LOG_DEBUG("FVulkanPipelineBarrier::Execute - Executed " +
                std::to_string(MemoryBarriers.size()) + " memory, " +
                std::to_string(BufferBarriers.size()) + " buffer, " +
                std::to_string(ImageBarriers.size()) + " image barriers");
}

void FVulkanPipelineBarrier::Execute(FVulkanCmdBuffer* CmdBuffer) {
    if (CmdBuffer) {
        Execute(CmdBuffer->getHandle());
    }
}

VkImageSubresourceRange FVulkanPipelineBarrier::MakeSubresourceRange(VkImageAspectFlags AspectMask,
                                                                     uint32 FirstMip, uint32 NumMips,
                                                                     uint32 FirstLayer, uint32 NumLayers) {
    VkImageSubresourceRange Range = {};
    Range.aspectMask = AspectMask;
    Range.baseMipLevel = FirstMip;
    Range.levelCount = NumMips;
    Range.baseArrayLayer = FirstLayer;
    Range.layerCount = NumLayers;
    return Range;
}

// ============================================================================
// FVulkanResourceStateTracker Implementation
// ============================================================================

FVulkanResourceStateTracker::FVulkanResourceStateTracker() {
    MR_LOG_DEBUG("FVulkanResourceStateTracker: Created resource state tracker");
}

FVulkanResourceStateTracker::~FVulkanResourceStateTracker() {
    MR_LOG_DEBUG("FVulkanResourceStateTracker: Destroyed resource state tracker, tracked " +
                std::to_string(m_imageLayouts.size()) + " images, " +
                std::to_string(m_bufferStates.size()) + " buffers");
}

void FVulkanResourceStateTracker::NotifyDeletedImage(VkImage Image) {
    std::lock_guard<std::mutex> Lock(m_mutex);
    m_imageLayouts.erase(Image);
}

void FVulkanResourceStateTracker::NotifyDeletedBuffer(VkBuffer Buffer) {
    std::lock_guard<std::mutex> Lock(m_mutex);
    m_bufferStates.erase(Buffer);
}

const FVulkanImageLayout* FVulkanResourceStateTracker::GetImageLayout(VkImage Image) const {
    std::lock_guard<std::mutex> Lock(m_mutex);
    auto* Layout = m_imageLayouts.find(Image);
    return Layout;
}

FVulkanImageLayout* FVulkanResourceStateTracker::GetOrCreateImageLayout(VkImage Image, VkImageLayout InitialLayout,
                                                                        uint32 NumMips, uint32 NumLayers,
                                                                        VkImageAspectFlags Aspect) {
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    auto* Layout = m_imageLayouts.find(Image);
    if (Layout) {
        return Layout;
    }
    
    // Create new layout tracking - use Add with default constructor then modify
    FVulkanImageLayout& AddedLayout = m_imageLayouts.Add(Image);
    AddedLayout = FVulkanImageLayout(InitialLayout, NumMips, NumLayers, Aspect);
    return &AddedLayout;
}

bool FVulkanResourceStateTracker::TransitionImageLayout(FVulkanCmdBuffer* CmdBuffer, VkImage Image,
                                                        VkImageLayout OldLayout, VkImageLayout NewLayout,
                                                        const VkImageSubresourceRange& SubresourceRange) {
    if (OldLayout == NewLayout) {
        return false; // No transition needed
    }
    
    // Determine access masks and pipeline stages based on layouts
    VkAccessFlags SrcAccessMask = 0;
    VkAccessFlags DstAccessMask = 0;
    VkPipelineStageFlags SrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags DstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    
    // Source layout access masks
    switch (OldLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            SrcAccessMask = 0;
            SrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            SrcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            SrcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            SrcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            SrcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            SrcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            SrcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            SrcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            SrcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            SrcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            SrcStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            SrcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            SrcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;
        default:
            SrcAccessMask = 0;
            SrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;
    }
    
    // Destination layout access masks
    switch (NewLayout) {
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            DstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            DstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            DstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            DstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            DstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            DstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            DstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            DstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            DstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            DstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            DstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            DstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;
        default:
            DstAccessMask = 0;
            DstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;
    }
    
    // Create and execute barrier
    FVulkanPipelineBarrier Barrier;
    Barrier.AddImageLayoutTransition(Image, OldLayout, NewLayout, SubresourceRange,
                                    SrcAccessMask, DstAccessMask, SrcStage, DstStage);
    Barrier.Execute(CmdBuffer);
    
    MR_LOG_DEBUG("FVulkanResourceStateTracker::TransitionImageLayout - Transitioned image from layout " +
                std::to_string(static_cast<uint32>(OldLayout)) + " to " +
                std::to_string(static_cast<uint32>(NewLayout)));
    
    return true;
}

void FVulkanResourceStateTracker::TransitionBufferAccess(FVulkanCmdBuffer* CmdBuffer, VkBuffer Buffer,
                                                         VkAccessFlags SrcAccess, VkAccessFlags DstAccess,
                                                         VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage) {
    if (SrcAccess == DstAccess) {
        return; // No transition needed
    }
    
    // Create and execute barrier
    FVulkanPipelineBarrier Barrier;
    Barrier.AddBufferBarrier(Buffer, SrcAccess, DstAccess, SrcStage, DstStage);
    Barrier.Execute(CmdBuffer);
    
    // Update tracked state
    {
        std::lock_guard<std::mutex> Lock(m_mutex);
        FBufferAccessState& State = m_bufferStates[Buffer];
        State.LastAccess = DstAccess;
        State.LastStage = DstStage;
    }
    
    MR_LOG_DEBUG("FVulkanResourceStateTracker::TransitionBufferAccess - Transitioned buffer access");
}

// ============================================================================
// Helper Functions: ERHIAccess to Vulkan Conversion
// Reference: UE5 VulkanRHI.cpp
// ============================================================================

VkAccessFlags FVulkanResourceStateTracker::GetAccessMaskFromRHIAccess(ERHIAccess Access) {
    VkAccessFlags AccessFlags = 0;
    
    if (enumHasAnyFlags(Access, ERHIAccess::VertexOrIndexBuffer)) {
        AccessFlags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::SRVGraphics | ERHIAccess::SRVCompute)) {
        AccessFlags |= VK_ACCESS_SHADER_READ_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::UAVGraphics | ERHIAccess::UAVCompute)) {
        AccessFlags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::RTV)) {
        AccessFlags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::DSVRead)) {
        AccessFlags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::DSVWrite)) {
        AccessFlags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::CopySrc)) {
        AccessFlags |= VK_ACCESS_TRANSFER_READ_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::CopyDest)) {
        AccessFlags |= VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::Present)) {
        AccessFlags |= VK_ACCESS_MEMORY_READ_BIT;
    }
    
    return AccessFlags;
}

VkPipelineStageFlags FVulkanResourceStateTracker::GetStageMaskFromRHIAccess(ERHIAccess Access) {
    VkPipelineStageFlags StageFlags = 0;
    
    if (enumHasAnyFlags(Access, ERHIAccess::VertexOrIndexBuffer)) {
        StageFlags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::SRVGraphics)) {
        StageFlags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::SRVCompute)) {
        StageFlags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::UAVGraphics)) {
        StageFlags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::UAVCompute)) {
        StageFlags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::RTV)) {
        StageFlags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::DSVRead | ERHIAccess::DSVWrite)) {
        StageFlags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::CopySrc | ERHIAccess::CopyDest)) {
        StageFlags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::Present)) {
        StageFlags |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    
    if (StageFlags == 0) {
        StageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    
    return StageFlags;
}

VkImageLayout FVulkanResourceStateTracker::GetImageLayoutFromRHIAccess(ERHIAccess Access) {
    // Determine the best layout based on access pattern
    if (enumHasAnyFlags(Access, ERHIAccess::RTV)) {
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::DSVWrite)) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::DSVRead)) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::SRVGraphics | ERHIAccess::SRVCompute)) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::CopySrc)) {
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::CopyDest)) {
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    if (enumHasAnyFlags(Access, ERHIAccess::Present)) {
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    
    return VK_IMAGE_LAYOUT_GENERAL;
}

VkImageLayout FVulkanResourceStateTracker::GetDefaultLayoutForUsage(EResourceUsage Usage) {
    using namespace MonsterRender::RHI;
    
    // Use bitwise AND to check flags
    if ((Usage & EResourceUsage::RenderTarget) != EResourceUsage::None) {
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if ((Usage & EResourceUsage::DepthStencil) != EResourceUsage::None) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    if ((Usage & EResourceUsage::ShaderResource) != EResourceUsage::None) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if ((Usage & EResourceUsage::TransferSrc) != EResourceUsage::None) {
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    if ((Usage & EResourceUsage::TransferDst) != EResourceUsage::None) {
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    
    return VK_IMAGE_LAYOUT_GENERAL;
}

} // namespace MonsterRender::RHI::Vulkan
