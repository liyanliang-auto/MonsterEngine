// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Sampler Implementation
//
// Reference UE5: Engine/Source/Runtime/VulkanRHI/Public/VulkanResources.h

#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIResource.h"
#include "RHI/RHIResources.h"
#include "Platform/Vulkan/VulkanRHI.h"

namespace MonsterRender::RHI::Vulkan {
    
    // Forward declarations
    class VulkanDevice;
    
    /**
     * VulkanSampler - Vulkan implementation of IRHISampler
     * 
     * Wraps VkSampler and provides sampler state management.
     * Reference UE5: FVulkanSamplerState
     */
    class VulkanSampler : public IRHISampler {
    public:
        /**
         * Constructor
         * @param device Vulkan device
         * @param desc Sampler descriptor
         */
        VulkanSampler(VulkanDevice* device, const SamplerDesc& desc);
        
        /**
         * Destructor - releases Vulkan sampler
         */
        virtual ~VulkanSampler();
        
        // Non-copyable
        VulkanSampler(const VulkanSampler&) = delete;
        VulkanSampler& operator=(const VulkanSampler&) = delete;
        
        /**
         * Get native Vulkan sampler handle
         */
        VkSampler getSampler() const { return m_sampler; }
        
        /**
         * Get sampler descriptor
         */
        const SamplerDesc& getDesc() const { return m_desc; }
        
        /**
         * Check if sampler is valid
         */
        bool isValid() const { return m_sampler != VK_NULL_HANDLE; }
        
    private:
        /**
         * Initialize the sampler
         */
        bool initialize();
        
        /**
         * Convert RHI filter to Vulkan filter
         */
        static VkFilter getVulkanFilter(ESamplerFilter filter);
        
        /**
         * Convert RHI filter to Vulkan mipmap mode
         */
        static VkSamplerMipmapMode getVulkanMipmapMode(ESamplerFilter filter);
        
        /**
         * Convert RHI address mode to Vulkan address mode
         */
        static VkSamplerAddressMode getVulkanAddressMode(ESamplerAddressMode mode);
        
        /**
         * Convert RHI comparison func to Vulkan compare op
         */
        static VkCompareOp getVulkanCompareOp(EComparisonFunc func);
        
    private:
        VulkanDevice* m_device;
        SamplerDesc m_desc;
        VkSampler m_sampler = VK_NULL_HANDLE;
    };
    
} // namespace MonsterRender::RHI::Vulkan
