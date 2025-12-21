#pragma once

#include "Core/CoreMinimal.h"
#include "RDG/RDGDefinitions.h"

namespace MonsterRender {
namespace RHI {

using namespace MonsterRender::RDG;

/**
 * Barrier utilities for resource state transitions
 * Reference: UE5 VulkanBarriers.cpp
 */

/**
 * Convert ERHIAccess to Vulkan pipeline stage flags
 * @param access RHI access flags
 * @param bIsSourceState Whether this is the source state of a transition
 * @return Vulkan pipeline stage flags
 */
uint32 getVulkanStageFlags(ERHIAccess access, bool bIsSourceState = false);

/**
 * Convert ERHIAccess to Vulkan access flags
 * @param access RHI access flags
 * @return Vulkan access flags
 */
uint32 getVulkanAccessFlags(ERHIAccess access);

/**
 * Convert ERHIAccess to Vulkan image layout
 * @param access RHI access flags
 * @param bIsDepthStencil Whether the resource is depth/stencil
 * @param bIsSourceState Whether this is the source state of a transition
 * @return Vulkan image layout
 */
uint32 getVulkanImageLayout(ERHIAccess access, bool bIsDepthStencil, bool bIsSourceState = false);

}} // namespace MonsterRender::RHI
