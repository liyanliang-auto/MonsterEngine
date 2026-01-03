#pragma once

#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Platform/Vulkan/VulkanDescriptorSetLayout.h"
#include "Containers/Array.h"
#include <mutex>

namespace MonsterRender::RHI::Vulkan {
    
    using MonsterEngine::TArray;
    
    // Forward declarations
    class VulkanDevice;
    
    /**
     * Descriptor pool for allocating descriptor sets
     * Reference: UE5 FVulkanDescriptorPool
     */
    class VulkanDescriptorPool {
    public:
        VulkanDescriptorPool(VulkanDevice* device, uint32 maxSets);
        ~VulkanDescriptorPool();
        
        // Non-copyable, non-movable
        VulkanDescriptorPool(const VulkanDescriptorPool&) = delete;
        VulkanDescriptorPool& operator=(const VulkanDescriptorPool&) = delete;
        
        /**
         * Allocate a descriptor set from this pool
         * @param layout Descriptor set layout
         * @return Allocated descriptor set handle, or VK_NULL_HANDLE on failure
         */
        VkDescriptorSet allocate(VkDescriptorSetLayout layout);
        
        /**
         * Reset the pool (frees all allocated descriptor sets)
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
         * Get maximum sets capacity
         */
        uint32 getMaxSets() const { return m_maxSets; }
        
        /**
         * Get Vulkan pool handle
         */
        VkDescriptorPool getHandle() const { return m_pool; }
        
    private:
        VulkanDevice* m_device;
        VkDescriptorPool m_pool = VK_NULL_HANDLE;
        uint32 m_maxSets;
        uint32 m_allocatedSets = 0;
        
        bool _createPool();
        void _destroyPool();
        TArray<VkDescriptorPoolSize> _getDefaultPoolSizes() const;
    };
    
    /**
     * Descriptor pool manager with automatic pool creation and recycling
     * Manages multiple pools and automatically creates new ones when needed
     * Reference: UE5 FVulkanDescriptorPoolsManager
     */
    class VulkanDescriptorPoolManager {
    public:
        VulkanDescriptorPoolManager(VulkanDevice* device);
        ~VulkanDescriptorPoolManager();
        
        // Non-copyable, non-movable
        VulkanDescriptorPoolManager(const VulkanDescriptorPoolManager&) = delete;
        VulkanDescriptorPoolManager& operator=(const VulkanDescriptorPoolManager&) = delete;
        
        /**
         * Allocate a descriptor set (high-level API)
         * Automatically creates new pools if current pool is full
         * @param layout Descriptor set layout
         * @return Allocated descriptor set, or nullptr on failure
         */
        TSharedPtr<VulkanDescriptorSet> allocateDescriptorSet(
            TSharedPtr<VulkanDescriptorSetLayout> layout);
        
        /**
         * Allocate a raw Vulkan descriptor set (low-level API)
         * Used by descriptor set cache for direct Vulkan allocation
         * Automatically creates new pools if current pool is full
         * @param layout Vulkan descriptor set layout handle
         * @return Allocated descriptor set handle, or VK_NULL_HANDLE on failure
         */
        VkDescriptorSet allocateRawDescriptorSet(VkDescriptorSetLayout layout);
        
        /**
         * Begin new frame - reset old pools for reuse
         * @param frameNumber Current frame number
         */
        void beginFrame(uint64 frameNumber);
        
        /**
         * Get statistics
         */
        struct Stats {
            uint32 totalPools;
            uint32 activePools;
            uint32 totalSetsAllocated;
            uint32 currentFrameAllocations;
            uint64 totalMemoryUsed;
        };
        Stats getStats() const;
        
        /**
         * Force reset all pools (for cleanup or testing)
         */
        void resetAll();
        
    private:
        VulkanDevice* m_device;
        
        // Pool management
        static constexpr uint32 SETS_PER_POOL = 256;
        static constexpr uint32 FRAME_LAG = 3; // Triple buffering
        
        TArray<TUniquePtr<VulkanDescriptorPool>> m_pools;
        uint32 m_currentPoolIndex = 0;
        
        // Frame tracking for pool recycling
        uint64 m_currentFrame = 0;
        TArray<uint64> m_poolFrameNumbers; // Frame number when each pool was last used
        
        // Statistics
        mutable Stats m_stats;
        mutable std::mutex m_mutex;
        
        // Helper methods
        VulkanDescriptorPool* _getCurrentPool();
        VulkanDescriptorPool* _createNewPool();
        void _recycleOldPools(uint64 frameNumber);
    };
}
