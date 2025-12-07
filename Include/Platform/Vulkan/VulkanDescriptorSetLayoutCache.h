#pragma once

// Copyright MonsterEngine Team. All Rights Reserved.
// VulkanDescriptorSetLayoutCache - Caches descriptor set layouts for efficient reuse
//
// Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanDescriptorSets.h
//            FVulkanDescriptorSetsLayoutInfo, FVulkanDescriptorSetLayoutEntry

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

    /**
     * FVulkanDescriptorSetLayoutBinding - Single binding info for descriptor set layout
     * Reference: UE5 FVulkanDescriptorSetLayoutBinding
     */
    struct FVulkanDescriptorSetLayoutBinding {
        uint32 Binding = 0;
        VkDescriptorType DescriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        uint32 DescriptorCount = 0;
        VkShaderStageFlags StageFlags = 0;
        
        // Comparison for hashing
        bool operator==(const FVulkanDescriptorSetLayoutBinding& Other) const {
            return Binding == Other.Binding &&
                   DescriptorType == Other.DescriptorType &&
                   DescriptorCount == Other.DescriptorCount &&
                   StageFlags == Other.StageFlags;
        }
    };

    /**
     * FVulkanDescriptorSetLayoutInfo - Complete layout information for a descriptor set
     * Contains all bindings for a single descriptor set
     * Reference: UE5 FVulkanDescriptorSetLayoutInfo
     */
    struct FVulkanDescriptorSetLayoutInfo {
        TArray<FVulkanDescriptorSetLayoutBinding> Bindings;
        
        // Calculate hash for cache lookup
        uint64 GetHash() const;
        
        // Comparison for cache
        bool operator==(const FVulkanDescriptorSetLayoutInfo& Other) const;
        
        // Add binding
        void AddBinding(uint32 Binding, VkDescriptorType Type, uint32 Count, VkShaderStageFlags Stages);
        
        // Sort bindings by binding index (required for consistent hashing)
        void SortBindings();
        
        // Check if empty
        bool IsEmpty() const { return Bindings.empty(); }
        
        // Get binding count
        uint32 GetBindingCount() const { return static_cast<uint32>(Bindings.size()); }
    };

    /**
     * FVulkanDescriptorSetLayoutEntry - Cached descriptor set layout with usage tracking
     * Reference: UE5 FVulkanDescriptorSetLayoutEntry
     */
    struct FVulkanDescriptorSetLayoutEntry {
        VkDescriptorSetLayout Handle = VK_NULL_HANDLE;
        FVulkanDescriptorSetLayoutInfo LayoutInfo;
        uint64 Hash = 0;
        uint32 RefCount = 0;
        uint64 LastUsedFrame = 0;
    };

    /**
     * FVulkanDescriptorSetLayoutCache - Caches descriptor set layouts for reuse
     * Prevents duplicate layouts and provides efficient lookup
     * 
     * Reference: UE5 FVulkanLayoutManager
     * 
     * Usage:
     * 1. Call GetOrCreateLayout() with binding info to get/create a cached layout
     * 2. Layouts are reference counted and released when no longer used
     * 3. Call GarbageCollect() periodically to clean up unused layouts
     */
    class FVulkanDescriptorSetLayoutCache {
    public:
        FVulkanDescriptorSetLayoutCache(VulkanDevice* InDevice);
        ~FVulkanDescriptorSetLayoutCache();
        
        // Non-copyable
        FVulkanDescriptorSetLayoutCache(const FVulkanDescriptorSetLayoutCache&) = delete;
        FVulkanDescriptorSetLayoutCache& operator=(const FVulkanDescriptorSetLayoutCache&) = delete;
        
        /**
         * Get or create a descriptor set layout from binding info
         * Thread-safe with internal mutex
         * @param LayoutInfo Layout information containing bindings
         * @return Vulkan descriptor set layout handle
         */
        VkDescriptorSetLayout GetOrCreateLayout(const FVulkanDescriptorSetLayoutInfo& LayoutInfo);
        
        /**
         * Get or create layout from Vulkan bindings array
         * Convenience wrapper that builds LayoutInfo internally
         */
        VkDescriptorSetLayout GetOrCreateLayout(const TArray<VkDescriptorSetLayoutBinding>& VkBindings);
        
        /**
         * Release a layout reference (call when done using)
         * Layout will be destroyed when refcount reaches zero during GC
         */
        void ReleaseLayout(VkDescriptorSetLayout Layout);
        
        /**
         * Garbage collect unused layouts
         * Should be called at end of frame or periodically
         * @param CurrentFrame Current frame number for age tracking
         * @param MaxAge Maximum frames a layout can be unused before deletion
         */
        void GarbageCollect(uint64 CurrentFrame, uint32 MaxAge = 120);
        
        /**
         * Clear all cached layouts (call on shutdown)
         */
        void Clear();
        
        /**
         * Get cache statistics
         */
        struct FStats {
            uint32 TotalLayouts = 0;
            uint32 CacheHits = 0;
            uint32 CacheMisses = 0;
            uint32 TotalRefCount = 0;
        };
        FStats GetStats() const;
        
    private:
        /**
         * Create a new Vulkan descriptor set layout
         */
        VkDescriptorSetLayout CreateLayout(const FVulkanDescriptorSetLayoutInfo& LayoutInfo);
        
        /**
         * Find existing layout by hash
         */
        FVulkanDescriptorSetLayoutEntry* FindByHash(uint64 Hash);
        
    private:
        VulkanDevice* Device;
        
        // Layout cache - maps hash to layout entry
        TMap<uint64, FVulkanDescriptorSetLayoutEntry> LayoutCache;
        
        // Reverse lookup - maps handle to hash
        TMap<VkDescriptorSetLayout, uint64> HandleToHash;
        
        // Thread safety
        mutable std::mutex CacheMutex;
        
        // Statistics
        mutable FStats Stats;
    };

    /**
     * FVulkanDescriptorSetKey - Key for descriptor set cache lookup
     * Identifies a unique descriptor set by its contents
     * Reference: UE5 descriptor set caching in FVulkanPendingComputeState
     */
    struct FVulkanDescriptorSetKey {
        VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
        
        // Buffer bindings: slot -> (buffer, offset, range)
        struct FBufferBinding {
            VkBuffer Buffer = VK_NULL_HANDLE;
            VkDeviceSize Offset = 0;
            VkDeviceSize Range = 0;
            
            bool operator==(const FBufferBinding& Other) const {
                return Buffer == Other.Buffer && Offset == Other.Offset && Range == Other.Range;
            }
        };
        TMap<uint32, FBufferBinding> BufferBindings;
        
        // Image bindings: slot -> (imageView, sampler, layout)
        struct FImageBinding {
            VkImageView ImageView = VK_NULL_HANDLE;
            VkSampler Sampler = VK_NULL_HANDLE;
            VkImageLayout ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            
            bool operator==(const FImageBinding& Other) const {
                return ImageView == Other.ImageView && 
                       Sampler == Other.Sampler && 
                       ImageLayout == Other.ImageLayout;
            }
        };
        TMap<uint32, FImageBinding> ImageBindings;
        
        // Calculate hash for cache lookup
        uint64 GetHash() const;
        
        // Comparison
        bool operator==(const FVulkanDescriptorSetKey& Other) const;
    };

    /**
     * FVulkanDescriptorSetCache - Caches descriptor sets for reuse within a frame
     * Avoids redundant descriptor set allocations and updates
     * 
     * Reference: UE5 descriptor set caching pattern
     * 
     * Usage:
     * 1. Build a FVulkanDescriptorSetKey with current bindings
     * 2. Call GetOrAllocate() to get a cached or new descriptor set
     * 3. Call Reset() at the start of each frame
     */
    class FVulkanDescriptorSetCache {
    public:
        FVulkanDescriptorSetCache(VulkanDevice* InDevice);
        ~FVulkanDescriptorSetCache();
        
        // Non-copyable
        FVulkanDescriptorSetCache(const FVulkanDescriptorSetCache&) = delete;
        FVulkanDescriptorSetCache& operator=(const FVulkanDescriptorSetCache&) = delete;
        
        /**
         * Get or allocate a descriptor set matching the key
         * If a matching set exists in cache, returns it
         * Otherwise allocates and updates a new set
         * @param Key Descriptor set key with layout and bindings
         * @return Descriptor set handle ready for binding
         */
        VkDescriptorSet GetOrAllocate(const FVulkanDescriptorSetKey& Key);
        
        /**
         * Reset cache for new frame
         * Should be called at the start of each frame
         * @param FrameNumber Current frame number
         */
        void Reset(uint64 FrameNumber);
        
        /**
         * Get cache statistics
         */
        struct FStats {
            uint32 CacheHits = 0;
            uint32 CacheMisses = 0;
            uint32 TotalAllocations = 0;
            uint32 CurrentCacheSize = 0;
        };
        FStats GetStats() const;
        
    private:
        /**
         * Allocate and update a new descriptor set
         */
        VkDescriptorSet AllocateAndUpdate(const FVulkanDescriptorSetKey& Key);
        
        /**
         * Update descriptor set with bindings from key
         */
        void UpdateDescriptorSet(VkDescriptorSet Set, const FVulkanDescriptorSetKey& Key);
        
    private:
        VulkanDevice* Device;
        
        // Frame-local cache: hash -> descriptor set
        TMap<uint64, VkDescriptorSet> FrameCache;
        
        // Current frame number
        uint64 CurrentFrame = 0;
        
        // Statistics
        mutable FStats Stats;
        
        // Thread safety
        mutable std::mutex CacheMutex;
    };

} // namespace MonsterRender::RHI::Vulkan
