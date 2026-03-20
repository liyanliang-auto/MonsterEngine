#pragma once

#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "RHI/RHIDefinitions.h"
#include "RDG/RDGDefinitions.h"
#include "Containers/Map.h"
#include "Containers/Array.h"

namespace MonsterRender::RHI::Vulkan {

// Forward declarations
class VulkanTexture;
class VulkanBuffer;
class FVulkanCmdBuffer;

/**
 * Vulkan Image Layout Tracker
 * Tracks the current layout of each subresource (mip level + array layer) of a Vulkan image
 * 
 * Reference: UE5 FVulkanImageLayout
 * Engine/Source/Runtime/VulkanRHI/Private/VulkanBarriers.h
 */
struct FVulkanImageLayout {
    /**
     * Default constructor (required for TMap)
     */
    FVulkanImageLayout()
        : NumMips(1)
        , NumLayers(1)
        , NumPlanes(1)
        , MainLayout(VK_IMAGE_LAYOUT_UNDEFINED) {
    }
    
    /**
     * Constructor
     * @param InitialLayout Initial layout for all subresources
     * @param InNumMips Number of mip levels
     * @param InNumLayers Number of array layers
     * @param Aspect Image aspect flags (color, depth, stencil)
     */
    FVulkanImageLayout(VkImageLayout InitialLayout, uint32 InNumMips, uint32 InNumLayers, VkImageAspectFlags Aspect)
        : NumMips(InNumMips)
        , NumLayers(InNumLayers)
        , NumPlanes((Aspect == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) ? 2 : 1)
        , MainLayout(InitialLayout) {
    }
    
    uint32 NumMips;      // Number of mip levels
    uint32 NumLayers;    // Number of array layers
    uint32 NumPlanes;    // Number of planes (1 for color, 2 for depth-stencil)
    
    // The layout when all subresources are in the same state
    VkImageLayout MainLayout;
    
    // Explicit subresource layouts (only allocated when subresources differ)
    // Layout: [Plane][Layer][Mip]
    MonsterEngine::TArray<VkImageLayout> SubresLayouts;
    
    /**
     * Check if all subresources have the same layout
     */
    bool AreAllSubresourcesSameLayout() const {
        return SubresLayouts.empty();
    }
    
    /**
     * Get layout for a specific subresource
     * @param Layer Array layer index
     * @param Mip Mip level index
     * @param Aspect Image aspect (color, depth, or stencil)
     * @return Current layout of the subresource
     */
    VkImageLayout GetSubresLayout(uint32 Layer, uint32 Mip, VkImageAspectFlagBits Aspect) const {
        return GetSubresLayout(Layer, Mip, (Aspect == VK_IMAGE_ASPECT_STENCIL_BIT) ? NumPlanes - 1 : 0);
    }
    
    /**
     * Get layout for a specific subresource by plane index
     */
    VkImageLayout GetSubresLayout(uint32 Layer, uint32 Mip, uint32 Plane) const {
        if (SubresLayouts.empty()) {
            return MainLayout;
        }
        
        if (Layer == static_cast<uint32>(-1)) {
            Layer = 0;
        }
        
        MR_ASSERT(Plane < NumPlanes && Layer < NumLayers && Mip < NumMips);
        return SubresLayouts[(Plane * NumLayers * NumMips) + (Layer * NumMips) + Mip];
    }
    
    /**
     * Set layout for a range of subresources
     * @param Layout New layout
     * @param SubresourceRange Range of subresources to update
     */
    void Set(VkImageLayout Layout, const VkImageSubresourceRange& SubresourceRange);
    
    /**
     * Collapse subresource layouts to MainLayout if they're all the same
     * This optimizes memory usage
     */
    void CollapseSubresLayoutsIfSame();
    
    /**
     * Check if all subresources in a range have the same layout
     */
    bool AreSubresourcesSameLayout(VkImageLayout Layout, const VkImageSubresourceRange& SubresourceRange) const;
};

/**
 * Vulkan Pipeline Barrier Builder
 * Accumulates memory, buffer, and image barriers for efficient batch submission
 * 
 * Reference: UE5 FVulkanPipelineBarrier
 * Engine/Source/Runtime/VulkanRHI/Private/VulkanBarriers.h
 */
struct FVulkanPipelineBarrier {
    FVulkanPipelineBarrier() = default;
    
    // Barrier arrays with inline allocators for common cases
    MonsterEngine::TArray<VkMemoryBarrier> MemoryBarriers;
    MonsterEngine::TArray<VkImageMemoryBarrier> ImageBarriers;
    MonsterEngine::TArray<VkBufferMemoryBarrier> BufferBarriers;
    
    // Pipeline stage masks
    VkPipelineStageFlags SrcStageMask = 0;
    VkPipelineStageFlags DstStageMask = 0;
    
    /**
     * Add a memory barrier
     * @param SrcAccessMask Source access mask
     * @param DstAccessMask Destination access mask
     * @param SrcStage Source pipeline stage
     * @param DstStage Destination pipeline stage
     */
    void AddMemoryBarrier(VkAccessFlags SrcAccessMask, VkAccessFlags DstAccessMask,
                         VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage);
    
    /**
     * Add an image layout transition barrier
     * @param Image Vulkan image handle
     * @param SrcLayout Source layout
     * @param DstLayout Destination layout
     * @param SubresourceRange Range of subresources to transition
     * @param SrcAccessMask Source access mask
     * @param DstAccessMask Destination access mask
     * @param SrcStage Source pipeline stage
     * @param DstStage Destination pipeline stage
     */
    void AddImageLayoutTransition(VkImage Image, VkImageLayout SrcLayout, VkImageLayout DstLayout,
                                  const VkImageSubresourceRange& SubresourceRange,
                                  VkAccessFlags SrcAccessMask, VkAccessFlags DstAccessMask,
                                  VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage);
    
    /**
     * Add a buffer memory barrier
     * @param Buffer Vulkan buffer handle
     * @param SrcAccessMask Source access mask
     * @param DstAccessMask Destination access mask
     * @param SrcStage Source pipeline stage
     * @param DstStage Destination pipeline stage
     * @param Offset Offset in buffer
     * @param Size Size of buffer region (VK_WHOLE_SIZE for entire buffer)
     */
    void AddBufferBarrier(VkBuffer Buffer, VkAccessFlags SrcAccessMask, VkAccessFlags DstAccessMask,
                         VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage,
                         VkDeviceSize Offset = 0, VkDeviceSize Size = VK_WHOLE_SIZE);
    
    /**
     * Execute all accumulated barriers
     * @param CmdBuffer Vulkan command buffer
     */
    void Execute(VkCommandBuffer CmdBuffer);
    
    /**
     * Execute all accumulated barriers (FVulkanCmdBuffer version)
     * @param CmdBuffer Vulkan command buffer wrapper
     */
    void Execute(FVulkanCmdBuffer* CmdBuffer);
    
    /**
     * Check if there are any barriers to execute
     */
    bool HasBarriers() const {
        return !MemoryBarriers.empty() || !ImageBarriers.empty() || !BufferBarriers.empty();
    }
    
    /**
     * Clear all barriers
     */
    void Reset() {
        MemoryBarriers.clear();
        ImageBarriers.clear();
        BufferBarriers.clear();
        SrcStageMask = 0;
        DstStageMask = 0;
    }
    
    /**
     * Create a subresource range for an image
     * @param AspectMask Image aspect flags
     * @param FirstMip First mip level
     * @param NumMips Number of mip levels (VK_REMAINING_MIP_LEVELS for all)
     * @param FirstLayer First array layer
     * @param NumLayers Number of layers (VK_REMAINING_ARRAY_LAYERS for all)
     */
    static VkImageSubresourceRange MakeSubresourceRange(VkImageAspectFlags AspectMask,
                                                        uint32 FirstMip = 0,
                                                        uint32 NumMips = VK_REMAINING_MIP_LEVELS,
                                                        uint32 FirstLayer = 0,
                                                        uint32 NumLayers = VK_REMAINING_ARRAY_LAYERS);
};

/**
 * Vulkan Resource State Tracker
 * Tracks the current state of all Vulkan resources and automatically inserts barriers
 * 
 * This class maintains the current layout/access state of all textures and buffers,
 * and generates the necessary pipeline barriers when resources transition between states.
 * 
 * Reference: UE5 FVulkanLayoutManager
 * Engine/Source/Runtime/VulkanRHI/Private/VulkanBarriers.h
 */
class FVulkanResourceStateTracker {
public:
    FVulkanResourceStateTracker();
    ~FVulkanResourceStateTracker();
    
    // Non-copyable
    FVulkanResourceStateTracker(const FVulkanResourceStateTracker&) = delete;
    FVulkanResourceStateTracker& operator=(const FVulkanResourceStateTracker&) = delete;
    
    /**
     * Notify that an image has been deleted
     * Removes tracking information for the image
     */
    void NotifyDeletedImage(VkImage Image);
    
    /**
     * Notify that a buffer has been deleted
     * Removes tracking information for the buffer
     */
    void NotifyDeletedBuffer(VkBuffer Buffer);
    
    /**
     * Get the current layout of an image
     * @param Image Vulkan image handle
     * @return Pointer to layout info, or nullptr if not tracked
     */
    const FVulkanImageLayout* GetImageLayout(VkImage Image) const;
    
    /**
     * Get or create layout tracking for an image
     * @param Image Vulkan image handle
     * @param InitialLayout Initial layout if creating new tracking
     * @param NumMips Number of mip levels
     * @param NumLayers Number of array layers
     * @param Aspect Image aspect flags
     * @return Pointer to layout info
     */
    FVulkanImageLayout* GetOrCreateImageLayout(VkImage Image, VkImageLayout InitialLayout,
                                               uint32 NumMips, uint32 NumLayers,
                                               VkImageAspectFlags Aspect);
    
    /**
     * Transition an image to a new layout
     * Automatically generates and records the necessary pipeline barrier
     * 
     * @param CmdBuffer Command buffer to record barrier into
     * @param Image Vulkan image handle
     * @param OldLayout Current layout
     * @param NewLayout Desired layout
     * @param SubresourceRange Range of subresources to transition
     * @return true if barrier was inserted, false if already in desired layout
     */
    bool TransitionImageLayout(FVulkanCmdBuffer* CmdBuffer, VkImage Image,
                               VkImageLayout OldLayout, VkImageLayout NewLayout,
                               const VkImageSubresourceRange& SubresourceRange);
    
    /**
     * Transition a buffer access mode
     * Automatically generates and records the necessary pipeline barrier
     * 
     * @param CmdBuffer Command buffer to record barrier into
     * @param Buffer Vulkan buffer handle
     * @param SrcAccess Source access flags
     * @param DstAccess Destination access flags
     * @param SrcStage Source pipeline stage
     * @param DstStage Destination pipeline stage
     */
    void TransitionBufferAccess(FVulkanCmdBuffer* CmdBuffer, VkBuffer Buffer,
                                VkAccessFlags SrcAccess, VkAccessFlags DstAccess,
                                VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage);
    
    /**
     * Convert ERHIAccess to Vulkan access flags
     * Reference: UE5 GetVkAccessMaskForLayout
     */
    static VkAccessFlags GetAccessMaskFromRHIAccess(MonsterRender::RDG::ERHIAccess Access);
    
    /**
     * Convert ERHIAccess to Vulkan pipeline stage flags
     * Reference: UE5 GetVkStageFlagsForLayout
     */
    static VkPipelineStageFlags GetStageMaskFromRHIAccess(MonsterRender::RDG::ERHIAccess Access);
    
    /**
     * Convert ERHIAccess to Vulkan image layout
     * Reference: UE5 GetVkImageLayoutFromRHIAccess
     */
    static VkImageLayout GetImageLayoutFromRHIAccess(MonsterRender::RDG::ERHIAccess Access);
    
    /**
     * Get default layout for a resource usage
     * Reference: UE5 GetDefaultLayout
     */
    static VkImageLayout GetDefaultLayoutForUsage(EResourceUsage Usage);
    
private:
    // Image layout tracking
    MonsterEngine::TMap<VkImage, FVulkanImageLayout> m_imageLayouts;
    
    // Buffer access tracking (stores last access flags)
    struct FBufferAccessState {
        VkAccessFlags LastAccess = 0;
        VkPipelineStageFlags LastStage = 0;
    };
    MonsterEngine::TMap<VkBuffer, FBufferAccessState> m_bufferStates;
    
    // Mutex for thread-safe access
    mutable std::mutex m_mutex;
};

} // namespace MonsterRender::RHI::Vulkan
