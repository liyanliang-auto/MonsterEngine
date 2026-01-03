#pragma once

#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include <mutex>

namespace MonsterRender::RHI::Vulkan {

// Use MonsterEngine containers
using MonsterEngine::TArray;
using MonsterEngine::TMap;
    
    // Forward declarations
    class VulkanDevice;
    class VulkanPipelineState;
    
    /**
     * Descriptor set allocation entry
     */
    struct VulkanDescriptorSetEntry {
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        uint64 frameNumber = 0;
        bool isUsed = false;
    };
    
    /**
     * Descriptor pool with fixed capacity
     * Similar to UE5's FVulkanDescriptorPool
     */
    class VulkanDescriptorPool {
    public:
        VulkanDescriptorPool(VulkanDevice* device, uint32 maxSets, const TArray<VkDescriptorPoolSize>& poolSizes);
        ~VulkanDescriptorPool();
        
        // Non-copyable, non-movable
        VulkanDescriptorPool(const VulkanDescriptorPool&) = delete;
        VulkanDescriptorPool& operator=(const VulkanDescriptorPool&) = delete;
        
        /**
         * Allocate a descriptor set
         */
        VkDescriptorSet allocate(VkDescriptorSetLayout layout);
        
        /**
         * Reset the pool (called when all sets are no longer used)
         */
        void reset();
        
        /**
         * Check if pool is full
         */
        bool isFull() const { return m_allocatedSets >= m_maxSets; }
        
        /**
         * Get current allocation count
         */
        uint32 getAllocatedCount() const { return m_allocatedSets; }
        
        /**
         * Get Vulkan pool handle
         */
        VkDescriptorPool getPool() const { return m_pool; }
        
    private:
        VulkanDevice* m_device;
        VkDescriptorPool m_pool = VK_NULL_HANDLE;
        uint32 m_maxSets;
        uint32 m_allocatedSets = 0;
    };
    
    /**
     * Descriptor set write info builder
     * Helper for constructing VkWriteDescriptorSet
     */
    class VulkanDescriptorSetWriter {
    public:
        VulkanDescriptorSetWriter() = default;
        
        /**
         * Add uniform buffer write
         */
        void addUniformBuffer(uint32 binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
        
        /**
         * Add combined image sampler write
         */
        void addCombinedImageSampler(uint32 binding, VkImageView imageView, VkSampler sampler, VkImageLayout layout);
        
        /**
         * Get write descriptor sets
         */
        const TArray<VkWriteDescriptorSet>& getWrites() const { return m_writes; }
        
        /**
         * Apply writes to descriptor set
         */
        void applyWrites(VkDescriptorSet descriptorSet);
        
        /**
         * Clear all writes
         */
        void clear();
        
    private:
        TArray<VkWriteDescriptorSet> m_writes;
        TArray<VkDescriptorBufferInfo> m_bufferInfos;
        TArray<VkDescriptorImageInfo> m_imageInfos;
    };
}

