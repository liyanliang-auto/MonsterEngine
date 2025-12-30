// Copyright Monster Engine. All Rights Reserved.

#include "RHI/RHIBarriers.h"
#include "Core/Logging/LogMacros.h"

// Vulkan headers
#include <vulkan/vulkan.h>

DEFINE_LOG_CATEGORY_STATIC(LogRHIBarriers, Log, All);

namespace MonsterRender {
namespace RHI {

/**
 * Convert ERHIAccess to Vulkan pipeline stage flags
 * Reference: UE5 VulkanBarriers.cpp GetVkStageAndAccessFlags
 */
uint32 getVulkanStageFlags(ERHIAccess access, bool bIsSourceState)
{
    // Handle single-state cases first
    switch (access)
    {
        case ERHIAccess::Unknown:
        case ERHIAccess::Discard:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            
        case ERHIAccess::CPURead:
            return VK_PIPELINE_STAGE_HOST_BIT;
            
        case ERHIAccess::Present:
            return bIsSourceState ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            
        case ERHIAccess::RTV:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            
        case ERHIAccess::UAVMask:
        case ERHIAccess::UAVGraphics:
        case ERHIAccess::UAVCompute:
            return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            
        case ERHIAccess::CopySrc:
        case ERHIAccess::CopyDest:
        case ERHIAccess::ResolveSrc:
        case ERHIAccess::ResolveDst:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
            
        case ERHIAccess::DSVRead:
        case ERHIAccess::DSVWrite:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            
        default:
            break;
    }
    
    // Handle combined states
    uint32 stageFlags = 0;
    
    if (enumHasAnyFlags(access, ERHIAccess::IndirectArgs))
    {
        stageFlags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    }
    
    if (enumHasAnyFlags(access, ERHIAccess::VertexOrIndexBuffer))
    {
        stageFlags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    }
    
    if (enumHasAnyFlags(access, ERHIAccess::SRVGraphics))
    {
        stageFlags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | 
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    
    if (enumHasAnyFlags(access, ERHIAccess::SRVCompute))
    {
        stageFlags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    
    if (enumHasAnyFlags(access, ERHIAccess::UAVGraphics))
    {
        stageFlags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    
    if (enumHasAnyFlags(access, ERHIAccess::UAVCompute))
    {
        stageFlags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    
    // Default to all commands if no specific stage was set
    if (stageFlags == 0)
    {
        stageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    
    return stageFlags;
}

/**
 * Convert ERHIAccess to Vulkan access flags
 * Reference: UE5 VulkanBarriers.cpp GetVkStageAndAccessFlags
 */
uint32 getVulkanAccessFlags(ERHIAccess access)
{
    // Handle single-state cases first
    switch (access)
    {
        case ERHIAccess::Unknown:
        case ERHIAccess::Discard:
            return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            
        case ERHIAccess::CPURead:
            return VK_ACCESS_HOST_READ_BIT;
            
        case ERHIAccess::Present:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            
        case ERHIAccess::RTV:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            
        case ERHIAccess::CopySrc:
        case ERHIAccess::ResolveSrc:
            return VK_ACCESS_TRANSFER_READ_BIT;
            
        case ERHIAccess::CopyDest:
        case ERHIAccess::ResolveDst:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
            
        case ERHIAccess::DSVRead:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            
        case ERHIAccess::DSVWrite:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            
        default:
            break;
    }
    
    // Handle combined states
    uint32 accessFlags = 0;
    
    if (enumHasAnyFlags(access, ERHIAccess::IndirectArgs))
    {
        accessFlags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }
    
    if (enumHasAnyFlags(access, ERHIAccess::VertexOrIndexBuffer))
    {
        accessFlags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    }
    
    if (enumHasAnyFlags(access, ERHIAccess::SRVMask))
    {
        accessFlags |= VK_ACCESS_SHADER_READ_BIT;
    }
    
    if (enumHasAnyFlags(access, ERHIAccess::UAVMask))
    {
        accessFlags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    
    // Default to memory read/write if no specific access was set
    if (accessFlags == 0)
    {
        accessFlags = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }
    
    return accessFlags;
}

/**
 * Convert ERHIAccess to Vulkan image layout
 * Reference: UE5 VulkanBarriers.cpp GetVkStageAndAccessFlags
 */
uint32 getVulkanImageLayout(ERHIAccess access, bool bIsDepthStencil, bool bIsSourceState)
{
    // Handle single-state cases first
    switch (access)
    {
        case ERHIAccess::Unknown:
            // IMPORTANT: VK_IMAGE_LAYOUT_UNDEFINED is only valid for oldLayout (source state)
            // For newLayout (destination state), we must use a valid layout
            if (bIsSourceState)
            {
                return VK_IMAGE_LAYOUT_UNDEFINED;
            }
            else
            {
                // For unknown destination state, use GENERAL layout as fallback
                return VK_IMAGE_LAYOUT_GENERAL;
            }
            
        case ERHIAccess::Discard:
            return bIsSourceState ? VK_IMAGE_LAYOUT_UNDEFINED : 
                   (bIsDepthStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            
        case ERHIAccess::CPURead:
            return VK_IMAGE_LAYOUT_GENERAL;
            
        case ERHIAccess::Present:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            
        case ERHIAccess::RTV:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            
        case ERHIAccess::CopySrc:
        case ERHIAccess::ResolveSrc:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            
        case ERHIAccess::CopyDest:
        case ERHIAccess::ResolveDst:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            
        case ERHIAccess::DSVRead:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            
        case ERHIAccess::DSVWrite:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            
        default:
            break;
    }
    
    // Handle combined states - prefer read-only layouts for SRV
    if (enumHasAnyFlags(access, ERHIAccess::SRVMask))
    {
        if (bIsDepthStencil)
        {
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        else
        {
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }
    
    if (enumHasAnyFlags(access, ERHIAccess::UAVMask))
    {
        return VK_IMAGE_LAYOUT_GENERAL;
    }
    
    // Default to general layout
    return VK_IMAGE_LAYOUT_GENERAL;
}

}} // namespace MonsterRender::RHI
