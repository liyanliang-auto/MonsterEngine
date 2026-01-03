#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIDescriptorSet.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

namespace MonsterRender::RHI::Vulkan {
    
    using MonsterEngine::TArray;
    using MonsterEngine::TMap;
    
    // Forward declarations
    class VulkanDevice;
    
    /**
     * Vulkan descriptor set layout implementation
     * Represents the layout/schema of a descriptor set
     * Reference: UE5 FVulkanDescriptorSetLayout
     */
    class VulkanDescriptorSetLayout : public IRHIDescriptorSetLayout {
    public:
        VulkanDescriptorSetLayout(VulkanDevice* device, const FDescriptorSetLayoutDesc& desc);
        virtual ~VulkanDescriptorSetLayout();
        
        // Non-copyable, non-movable
        VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout&) = delete;
        VulkanDescriptorSetLayout& operator=(const VulkanDescriptorSetLayout&) = delete;
        
        // IRHIResource interface
        ERHIBackend getBackendType() const override { return ERHIBackend::Vulkan; }
        
        // IRHIDescriptorSetLayout interface
        uint32 getSetIndex() const override { return m_setIndex; }
        const TArray<FDescriptorSetLayoutBinding>& getBindings() const override { return m_bindings; }
        
        /**
         * Get Vulkan descriptor set layout handle
         */
        VkDescriptorSetLayout getHandle() const { return m_layout; }
        
        /**
         * Get Vulkan bindings (for pool size calculation)
         */
        const TArray<VkDescriptorSetLayoutBinding>& getVulkanBindings() const { return m_vulkanBindings; }
        
        /**
         * Check if layout is valid
         */
        bool isValid() const { return m_layout != VK_NULL_HANDLE; }
        
    private:
        VulkanDevice* m_device;
        VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
        
        uint32 m_setIndex;
        TArray<FDescriptorSetLayoutBinding> m_bindings;
        TArray<VkDescriptorSetLayoutBinding> m_vulkanBindings;
        
        // Helper methods
        bool _createLayout();
        void _destroyLayout();
        VkDescriptorType _convertDescriptorType(EDescriptorType type) const;
        VkShaderStageFlags _convertShaderStages(EShaderStage stages) const;
    };
    
    /**
     * Vulkan pipeline layout implementation
     * Represents the complete layout of all descriptor sets
     * Reference: UE5 FVulkanPipelineLayout
     */
    class VulkanPipelineLayout : public IRHIPipelineLayout {
    public:
        VulkanPipelineLayout(VulkanDevice* device, const FPipelineLayoutDesc& desc);
        virtual ~VulkanPipelineLayout();
        
        // Non-copyable, non-movable
        VulkanPipelineLayout(const VulkanPipelineLayout&) = delete;
        VulkanPipelineLayout& operator=(const VulkanPipelineLayout&) = delete;
        
        // IRHIResource interface
        ERHIBackend getBackendType() const override { return ERHIBackend::Vulkan; }
        
        // IRHIPipelineLayout interface
        const TArray<TSharedPtr<IRHIDescriptorSetLayout>>& getSetLayouts() const override { 
            return m_setLayouts; 
        }
        const TArray<FPushConstantRange>& getPushConstantRanges() const override { 
            return m_pushConstantRanges; 
        }
        
        /**
         * Get Vulkan pipeline layout handle
         */
        VkPipelineLayout getHandle() const { return m_layout; }
        
        /**
         * Get Vulkan descriptor set layouts
         */
        const TArray<VkDescriptorSetLayout>& getVulkanSetLayouts() const { 
            return m_vulkanSetLayouts; 
        }
        
        /**
         * Check if layout is valid
         */
        bool isValid() const { return m_layout != VK_NULL_HANDLE; }
        
    private:
        VulkanDevice* m_device;
        VkPipelineLayout m_layout = VK_NULL_HANDLE;
        
        TArray<TSharedPtr<IRHIDescriptorSetLayout>> m_setLayouts;
        TArray<VkDescriptorSetLayout> m_vulkanSetLayouts;
        TArray<FPushConstantRange> m_pushConstantRanges;
        TArray<VkPushConstantRange> m_vulkanPushConstantRanges;
        
        // Helper methods
        bool _createLayout();
        void _destroyLayout();
        VkShaderStageFlags _convertShaderStages(EShaderStage stages) const;
    };
    
    /**
     * Vulkan descriptor set implementation
     * Represents an allocated descriptor set that can be bound to a pipeline
     * Reference: UE5 FVulkanDescriptorSet
     */
    class VulkanDescriptorSet : public IRHIDescriptorSet {
    public:
        VulkanDescriptorSet(VulkanDevice* device, 
                           TSharedPtr<VulkanDescriptorSetLayout> layout,
                           VkDescriptorSet descriptorSet);
        virtual ~VulkanDescriptorSet();
        
        // Non-copyable, non-movable
        VulkanDescriptorSet(const VulkanDescriptorSet&) = delete;
        VulkanDescriptorSet& operator=(const VulkanDescriptorSet&) = delete;
        
        // IRHIResource interface
        ERHIBackend getBackendType() const override { return ERHIBackend::Vulkan; }
        
        // IRHIDescriptorSet interface
        void updateUniformBuffer(uint32 binding, TSharedPtr<IRHIBuffer> buffer, 
                                uint32 offset = 0, uint32 range = 0) override;
        
        void updateTexture(uint32 binding, TSharedPtr<IRHITexture> texture) override;
        
        void updateSampler(uint32 binding, TSharedPtr<IRHISampler> sampler) override;
        
        void updateCombinedTextureSampler(uint32 binding, TSharedPtr<IRHITexture> texture,
                                         TSharedPtr<IRHISampler> sampler) override;
        
        TSharedPtr<IRHIDescriptorSetLayout> getLayout() const override { 
            return m_layout; 
        }
        
        /**
         * Get Vulkan descriptor set handle
         */
        VkDescriptorSet getHandle() const { return m_descriptorSet; }
        
        /**
         * Check if descriptor set is valid
         */
        bool isValid() const { return m_descriptorSet != VK_NULL_HANDLE; }
        
    private:
        VulkanDevice* m_device;
        VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
        TSharedPtr<VulkanDescriptorSetLayout> m_layout;
        
        // Cached resources to keep them alive
        TMap<uint32, TSharedPtr<IRHIBuffer>> m_boundBuffers;
        TMap<uint32, TSharedPtr<IRHITexture>> m_boundTextures;
        TMap<uint32, TSharedPtr<IRHISampler>> m_boundSamplers;
        
        // Helper methods
        void _writeDescriptor(uint32 binding, VkDescriptorType type, 
                            const VkDescriptorBufferInfo* bufferInfo,
                            const VkDescriptorImageInfo* imageInfo);
    };
}
