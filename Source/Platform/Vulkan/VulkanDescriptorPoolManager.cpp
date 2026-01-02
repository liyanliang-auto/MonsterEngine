#include "Platform/Vulkan/VulkanDescriptorPoolManager.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {
    
    // ========================================================================
    // VulkanDescriptorPool Implementation
    // ========================================================================
    
    VulkanDescriptorPool::VulkanDescriptorPool(VulkanDevice* device, uint32 maxSets)
        : m_device(device)
        , m_maxSets(maxSets)
    {
        if (!_createPool()) {
            MR_LOG(LogVulkanRHI, Error, "Failed to create Vulkan descriptor pool");
        }
    }
    
    VulkanDescriptorPool::~VulkanDescriptorPool() {
        _destroyPool();
    }
    
    VkDescriptorSet VulkanDescriptorPool::allocate(VkDescriptorSetLayout layout) {
        if (isFull()) {
            MR_LOG(LogVulkanRHI, Warning, "Descriptor pool is full, cannot allocate more sets");
            return VK_NULL_HANDLE;
        }
        
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;
        
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkResult result = vkAllocateDescriptorSets(m_device->getDevice(), &allocInfo, &descriptorSet);
        
        if (result != VK_SUCCESS) {
            MR_LOG(LogVulkanRHI, Error, "vkAllocateDescriptorSets failed with result %d", result);
            return VK_NULL_HANDLE;
        }
        
        m_allocatedSets++;
        
        MR_LOG(LogVulkanRHI, VeryVerbose, "Allocated descriptor set from pool (count: %u/%u)",
               m_allocatedSets, m_maxSets);
        
        return descriptorSet;
    }
    
    void VulkanDescriptorPool::reset() {
        if (m_pool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(m_device->getDevice(), m_pool, 0);
            m_allocatedSets = 0;
            
            MR_LOG(LogVulkanRHI, VeryVerbose, "Reset descriptor pool");
        }
    }
    
    bool VulkanDescriptorPool::_createPool() {
        TArray<VkDescriptorPoolSize> poolSizes = _getDefaultPoolSizes();
        
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = m_maxSets;
        poolInfo.poolSizeCount = static_cast<uint32>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        
        VkResult result = vkCreateDescriptorPool(m_device->getDevice(), &poolInfo, 
                                                nullptr, &m_pool);
        
        if (result != VK_SUCCESS) {
            MR_LOG(LogVulkanRHI, Error, "vkCreateDescriptorPool failed with result %d", result);
            return false;
        }
        
        MR_LOG(LogVulkanRHI, Verbose, "Created descriptor pool with capacity for %u sets", m_maxSets);
        
        return true;
    }
    
    void VulkanDescriptorPool::_destroyPool() {
        if (m_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device->getDevice(), m_pool, nullptr);
            m_pool = VK_NULL_HANDLE;
        }
    }
    
    TArray<VkDescriptorPoolSize> VulkanDescriptorPool::_getDefaultPoolSizes() const {
        // Allocate pool sizes for common descriptor types
        // These ratios are based on typical usage patterns
        TArray<VkDescriptorPoolSize> poolSizes;
        
        // Uniform buffers (most common)
        VkDescriptorPoolSize uniformBufferSize = {};
        uniformBufferSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformBufferSize.descriptorCount = m_maxSets * 4; // 4 UBOs per set on average
        poolSizes.push_back(uniformBufferSize);
        
        // Combined image samplers (textures)
        VkDescriptorPoolSize samplerSize = {};
        samplerSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerSize.descriptorCount = m_maxSets * 8; // 8 textures per set on average
        poolSizes.push_back(samplerSize);
        
        // Sampled images (separate textures)
        VkDescriptorPoolSize imageSize = {};
        imageSize.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        imageSize.descriptorCount = m_maxSets * 4;
        poolSizes.push_back(imageSize);
        
        // Samplers (separate samplers)
        VkDescriptorPoolSize separateSamplerSize = {};
        separateSamplerSize.type = VK_DESCRIPTOR_TYPE_SAMPLER;
        separateSamplerSize.descriptorCount = m_maxSets * 2;
        poolSizes.push_back(separateSamplerSize);
        
        // Storage buffers
        VkDescriptorPoolSize storageBufferSize = {};
        storageBufferSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        storageBufferSize.descriptorCount = m_maxSets * 2;
        poolSizes.push_back(storageBufferSize);
        
        // Storage images
        VkDescriptorPoolSize storageImageSize = {};
        storageImageSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageImageSize.descriptorCount = m_maxSets * 2;
        poolSizes.push_back(storageImageSize);
        
        return poolSizes;
    }
    
    // ========================================================================
    // VulkanDescriptorPoolManager Implementation
    // ========================================================================
    
    VulkanDescriptorPoolManager::VulkanDescriptorPoolManager(VulkanDevice* device)
        : m_device(device)
    {
        // Create initial pool
        _createNewPool();
        
        MR_LOG(LogVulkanRHI, Log, "Initialized descriptor pool manager");
    }
    
    VulkanDescriptorPoolManager::~VulkanDescriptorPoolManager() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_pools.clear();
        
        MR_LOG(LogVulkanRHI, Log, "Destroyed descriptor pool manager");
    }
    
    TSharedPtr<VulkanDescriptorSet> VulkanDescriptorPoolManager::allocateDescriptorSet(
        TSharedPtr<VulkanDescriptorSetLayout> layout) {
        
        if (!layout || !layout->isValid()) {
            MR_LOG(LogVulkanRHI, Error, "Invalid descriptor set layout");
            return nullptr;
        }
        
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // Get current pool or create new one if full
        VulkanDescriptorPool* pool = _getCurrentPool();
        if (!pool) {
            MR_LOG(LogVulkanRHI, Error, "Failed to get descriptor pool");
            return nullptr;
        }
        
        // Try to allocate from current pool
        VkDescriptorSet vkSet = pool->allocate(layout->getHandle());
        
        // If allocation failed due to full pool, create new pool and retry
        if (vkSet == VK_NULL_HANDLE && pool->isFull()) {
            MR_LOG(LogVulkanRHI, Verbose, "Current pool is full, creating new pool");
            
            pool = _createNewPool();
            if (!pool) {
                MR_LOG(LogVulkanRHI, Error, "Failed to create new descriptor pool");
                return nullptr;
            }
            
            vkSet = pool->allocate(layout->getHandle());
        }
        
        if (vkSet == VK_NULL_HANDLE) {
            MR_LOG(LogVulkanRHI, Error, "Failed to allocate descriptor set");
            return nullptr;
        }
        
        // Create descriptor set wrapper
        auto descriptorSet = MakeShared<VulkanDescriptorSet>(m_device, layout, vkSet);
        
        // Update statistics
        m_stats.totalSetsAllocated++;
        m_stats.currentFrameAllocations++;
        
        // Mark pool as used in current frame
        if (m_currentPoolIndex < m_poolFrameNumbers.size()) {
            m_poolFrameNumbers[m_currentPoolIndex] = m_currentFrame;
        }
        
        return descriptorSet;
    }
    
    void VulkanDescriptorPoolManager::beginFrame(uint64 frameNumber) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_currentFrame = frameNumber;
        m_stats.currentFrameAllocations = 0;
        
        // Recycle old pools that haven't been used recently
        _recycleOldPools(frameNumber);
        
        MR_LOG(LogVulkanRHI, VeryVerbose, "Begin frame %llu for descriptor pool manager", frameNumber);
    }
    
    VulkanDescriptorPoolManager::Stats VulkanDescriptorPoolManager::getStats() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        Stats stats = m_stats;
        stats.totalPools = static_cast<uint32>(m_pools.size());
        stats.activePools = 0;
        
        // Count active pools
        for (const auto& pool : m_pools) {
            if (pool && pool->getAllocatedCount() > 0) {
                stats.activePools++;
            }
        }
        
        // Estimate memory usage (rough approximation)
        stats.totalMemoryUsed = stats.totalPools * SETS_PER_POOL * 1024; // ~1KB per set
        
        return stats;
    }
    
    void VulkanDescriptorPoolManager::resetAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        for (auto& pool : m_pools) {
            if (pool) {
                pool->reset();
            }
        }
        
        m_currentPoolIndex = 0;
        m_stats.totalSetsAllocated = 0;
        m_stats.currentFrameAllocations = 0;
        
        MR_LOG(LogVulkanRHI, Log, "Reset all descriptor pools");
    }
    
    VulkanDescriptorPool* VulkanDescriptorPoolManager::_getCurrentPool() {
        if (m_pools.empty()) {
            return _createNewPool();
        }
        
        VulkanDescriptorPool* pool = m_pools[m_currentPoolIndex].get();
        
        // If current pool is full, move to next pool or create new one
        if (pool->isFull()) {
            m_currentPoolIndex++;
            
            if (m_currentPoolIndex >= m_pools.size()) {
                return _createNewPool();
            }
            
            pool = m_pools[m_currentPoolIndex].get();
        }
        
        return pool;
    }
    
    VulkanDescriptorPool* VulkanDescriptorPoolManager::_createNewPool() {
        auto pool = MakeUnique<VulkanDescriptorPool>(m_device, SETS_PER_POOL);
        
        if (!pool || pool->getHandle() == VK_NULL_HANDLE) {
            MR_LOG(LogVulkanRHI, Error, "Failed to create descriptor pool");
            return nullptr;
        }
        
        m_pools.push_back(std::move(pool));
        m_poolFrameNumbers.push_back(m_currentFrame);
        m_currentPoolIndex = static_cast<uint32>(m_pools.size() - 1);
        
        MR_LOG(LogVulkanRHI, Verbose, "Created new descriptor pool (total pools: %llu)",
               static_cast<uint64>(m_pools.size()));
        
        return m_pools[m_currentPoolIndex].get();
    }
    
    void VulkanDescriptorPoolManager::_recycleOldPools(uint64 frameNumber) {
        // Reset pools that haven't been used in FRAME_LAG frames
        for (uint32 i = 0; i < m_pools.size(); ++i) {
            if (i == m_currentPoolIndex) {
                continue; // Don't reset current pool
            }
            
            uint64 poolFrameNumber = (i < m_poolFrameNumbers.size()) ? m_poolFrameNumbers[i] : 0;
            
            if (frameNumber > poolFrameNumber + FRAME_LAG) {
                if (m_pools[i]) {
                    m_pools[i]->reset();
                    
                    MR_LOG(LogVulkanRHI, VeryVerbose, "Recycled descriptor pool %u", i);
                }
            }
        }
    }
}
