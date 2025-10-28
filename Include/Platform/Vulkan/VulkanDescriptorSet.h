#pragma once

#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include <mutex>

namespace MonsterRender::RHI::Vulkan {
    
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
     * Ring buffer allocator for descriptor sets
     * Based on UE5's FVulkanDescriptorSetRingBuffer
     * Provides efficient frame-based allocation with automatic recycling
     */
    class VulkanDescriptorSetAllocator {
    public:
        VulkanDescriptorSetAllocator(VulkanDevice* device);
        ~VulkanDescriptorSetAllocator();
        
        // Non-copyable, non-movable
        VulkanDescriptorSetAllocator(const VulkanDescriptorSetAllocator&) = delete;
        VulkanDescriptorSetAllocator& operator=(const VulkanDescriptorSetAllocator&) = delete;
        
        /**
         * Allocate a descriptor set for the current frame
         * @param layout Descriptor set layout
         * @param bindings Binding information for updating the set
         * @return Allocated descriptor set handle
         */
        VkDescriptorSet allocate(VkDescriptorSetLayout layout, const TArray<VkDescriptorSetLayoutBinding>& bindings);
        
        /**
         * Update descriptor set with buffers and textures
         */
        void updateDescriptorSet(VkDescriptorSet descriptorSet, 
                                const TArray<VkDescriptorSetLayoutBinding>& bindings,
                                const TMap<uint32, TSharedPtr<IRHIBuffer>>& buffers,
                                const TMap<uint32, TSharedPtr<IRHITexture>>& textures);
        
        /**
         * Begin new frame - recycle old descriptor sets
         */
        void beginFrame(uint64 frameNumber);
        
        /**
         * Get statistics
         */
        struct Stats {
            uint32 totalPools;
            uint32 totalSetsAllocated;
            uint32 totalSetsRecycled;
            uint32 currentFrameAllocations;
        };
        Stats getStats() const { return m_stats; }
        
    private:
        VulkanDevice* m_device;
        
        // Pool management (ring buffer of pools)
        static constexpr uint32 MAX_SETS_PER_POOL = 256;
        static constexpr uint32 FRAME_LAG = 3; // Triple buffering
        TArray<TUniquePtr<VulkanDescriptorPool>> m_pools;
        uint32 m_currentPoolIndex = 0;
        
        // Frame tracking
        uint64 m_currentFrame = 0;
        TArray<uint64> m_poolFrameNumbers; // Frame number when each pool was last used
        
        // Statistics
        mutable Stats m_stats;
        mutable std::mutex m_mutex;
        
        // Helper methods
        VulkanDescriptorPool* getCurrentPool();
        VulkanDescriptorPool* createNewPool();
        TArray<VkDescriptorPoolSize> getDefaultPoolSizes() const;
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

