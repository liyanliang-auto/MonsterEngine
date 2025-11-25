// Copyright MonsterEngine Team. All Rights Reserved.
// VulkanDescriptorSetLayoutCache - Implementation
//
// Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanDescriptorSets.cpp

#include "Platform/Vulkan/VulkanDescriptorSetLayoutCache.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanDescriptorSet.h"
#include "Core/Log.h"
#include <algorithm>

namespace MonsterRender::RHI::Vulkan {

    // ============================================================================
    // FVulkanDescriptorSetLayoutInfo Implementation
    // ============================================================================

    uint64 FVulkanDescriptorSetLayoutInfo::GetHash() const {
        // FNV-1a hash algorithm for consistent hashing
        uint64 Hash = 14695981039346656037ULL;
        
        for (const auto& Binding : Bindings) {
            // Mix in binding properties
            Hash ^= static_cast<uint64>(Binding.Binding);
            Hash *= 1099511628211ULL;
            Hash ^= static_cast<uint64>(Binding.DescriptorType);
            Hash *= 1099511628211ULL;
            Hash ^= static_cast<uint64>(Binding.DescriptorCount);
            Hash *= 1099511628211ULL;
            Hash ^= static_cast<uint64>(Binding.StageFlags);
            Hash *= 1099511628211ULL;
        }
        
        return Hash;
    }

    bool FVulkanDescriptorSetLayoutInfo::operator==(const FVulkanDescriptorSetLayoutInfo& Other) const {
        if (Bindings.size() != Other.Bindings.size()) {
            return false;
        }
        
        for (size_t i = 0; i < Bindings.size(); ++i) {
            if (!(Bindings[i] == Other.Bindings[i])) {
                return false;
            }
        }
        
        return true;
    }

    void FVulkanDescriptorSetLayoutInfo::AddBinding(uint32 Binding, VkDescriptorType Type, 
                                                     uint32 Count, VkShaderStageFlags Stages) {
        FVulkanDescriptorSetLayoutBinding NewBinding;
        NewBinding.Binding = Binding;
        NewBinding.DescriptorType = Type;
        NewBinding.DescriptorCount = Count;
        NewBinding.StageFlags = Stages;
        Bindings.push_back(NewBinding);
    }

    void FVulkanDescriptorSetLayoutInfo::SortBindings() {
        std::sort(Bindings.begin(), Bindings.end(), 
            [](const FVulkanDescriptorSetLayoutBinding& A, const FVulkanDescriptorSetLayoutBinding& B) {
                return A.Binding < B.Binding;
            });
    }

    // ============================================================================
    // FVulkanDescriptorSetLayoutCache Implementation
    // ============================================================================

    FVulkanDescriptorSetLayoutCache::FVulkanDescriptorSetLayoutCache(VulkanDevice* InDevice)
        : Device(InDevice) {
        MR_LOG_INFO("FVulkanDescriptorSetLayoutCache: Initialized descriptor set layout cache");
    }

    FVulkanDescriptorSetLayoutCache::~FVulkanDescriptorSetLayoutCache() {
        Clear();
        MR_LOG_INFO("FVulkanDescriptorSetLayoutCache: Destroyed, stats - Hits: " + 
                   std::to_string(Stats.CacheHits) + ", Misses: " + std::to_string(Stats.CacheMisses));
    }

    VkDescriptorSetLayout FVulkanDescriptorSetLayoutCache::GetOrCreateLayout(
        const FVulkanDescriptorSetLayoutInfo& LayoutInfo) {
        
        std::lock_guard<std::mutex> Lock(CacheMutex);
        
        // Calculate hash for lookup
        uint64 Hash = LayoutInfo.GetHash();
        
        // Check cache
        FVulkanDescriptorSetLayoutEntry* Entry = FindByHash(Hash);
        if (Entry) {
            // Cache hit - verify it's actually the same layout (hash collision check)
            if (Entry->LayoutInfo == LayoutInfo) {
                Entry->RefCount++;
                Stats.CacheHits++;
                MR_LOG_DEBUG("FVulkanDescriptorSetLayoutCache: Cache hit for layout hash " + 
                            std::to_string(Hash));
                return Entry->Handle;
            }
            // Hash collision - need to create new entry with different hash
            MR_LOG_WARNING("FVulkanDescriptorSetLayoutCache: Hash collision detected");
        }
        
        // Cache miss - create new layout
        Stats.CacheMisses++;
        
        VkDescriptorSetLayout NewLayout = CreateLayout(LayoutInfo);
        if (NewLayout == VK_NULL_HANDLE) {
            MR_LOG_ERROR("FVulkanDescriptorSetLayoutCache: Failed to create layout");
            return VK_NULL_HANDLE;
        }
        
        // Add to cache
        FVulkanDescriptorSetLayoutEntry NewEntry;
        NewEntry.Handle = NewLayout;
        NewEntry.LayoutInfo = LayoutInfo;
        NewEntry.Hash = Hash;
        NewEntry.RefCount = 1;
        NewEntry.LastUsedFrame = 0;
        
        LayoutCache[Hash] = NewEntry;
        HandleToHash[NewLayout] = Hash;
        Stats.TotalLayouts++;
        
        MR_LOG_DEBUG("FVulkanDescriptorSetLayoutCache: Created new layout, hash=" + 
                    std::to_string(Hash) + ", bindings=" + std::to_string(LayoutInfo.GetBindingCount()));
        
        return NewLayout;
    }

    VkDescriptorSetLayout FVulkanDescriptorSetLayoutCache::GetOrCreateLayout(
        const TArray<VkDescriptorSetLayoutBinding>& VkBindings) {
        
        // Convert Vulkan bindings to our format
        FVulkanDescriptorSetLayoutInfo LayoutInfo;
        for (const auto& VkBinding : VkBindings) {
            LayoutInfo.AddBinding(VkBinding.binding, VkBinding.descriptorType, 
                                  VkBinding.descriptorCount, VkBinding.stageFlags);
        }
        LayoutInfo.SortBindings();
        
        return GetOrCreateLayout(LayoutInfo);
    }

    void FVulkanDescriptorSetLayoutCache::ReleaseLayout(VkDescriptorSetLayout Layout) {
        std::lock_guard<std::mutex> Lock(CacheMutex);
        
        auto HashIt = HandleToHash.find(Layout);
        if (HashIt == HandleToHash.end()) {
            MR_LOG_WARNING("FVulkanDescriptorSetLayoutCache: Attempted to release unknown layout");
            return;
        }
        
        uint64 Hash = HashIt->second;
        auto EntryIt = LayoutCache.find(Hash);
        if (EntryIt != LayoutCache.end() && EntryIt->second.RefCount > 0) {
            EntryIt->second.RefCount--;
        }
    }

    void FVulkanDescriptorSetLayoutCache::GarbageCollect(uint64 CurrentFrame, uint32 MaxAge) {
        std::lock_guard<std::mutex> Lock(CacheMutex);
        
        TArray<uint64> ToRemove;
        
        for (auto& [Hash, Entry] : LayoutCache) {
            // Update last used frame if still referenced
            if (Entry.RefCount > 0) {
                Entry.LastUsedFrame = CurrentFrame;
                continue;
            }
            
            // Check age - remove if unused for too long
            if (CurrentFrame - Entry.LastUsedFrame > MaxAge) {
                ToRemove.push_back(Hash);
            }
        }
        
        // Remove stale entries
        for (uint64 Hash : ToRemove) {
            auto It = LayoutCache.find(Hash);
            if (It != LayoutCache.end()) {
                // Destroy Vulkan layout
                const auto& Functions = VulkanAPI::getFunctions();
                VkDevice VkDev = Device->getLogicalDevice();
                Functions.vkDestroyDescriptorSetLayout(VkDev, It->second.Handle, nullptr);
                
                // Remove from reverse lookup
                HandleToHash.erase(It->second.Handle);
                
                // Remove from cache
                LayoutCache.erase(It);
                Stats.TotalLayouts--;
                
                MR_LOG_DEBUG("FVulkanDescriptorSetLayoutCache: GC removed stale layout");
            }
        }
        
        if (!ToRemove.empty()) {
            MR_LOG_DEBUG("FVulkanDescriptorSetLayoutCache: GC removed " + 
                        std::to_string(ToRemove.size()) + " stale layouts");
        }
    }

    void FVulkanDescriptorSetLayoutCache::Clear() {
        std::lock_guard<std::mutex> Lock(CacheMutex);
        
        const auto& Functions = VulkanAPI::getFunctions();
        VkDevice VkDev = Device->getLogicalDevice();
        
        for (auto& [Hash, Entry] : LayoutCache) {
            if (Entry.Handle != VK_NULL_HANDLE) {
                Functions.vkDestroyDescriptorSetLayout(VkDev, Entry.Handle, nullptr);
            }
        }
        
        LayoutCache.clear();
        HandleToHash.clear();
        Stats.TotalLayouts = 0;
        
        MR_LOG_INFO("FVulkanDescriptorSetLayoutCache: Cleared all cached layouts");
    }

    FVulkanDescriptorSetLayoutCache::FStats FVulkanDescriptorSetLayoutCache::GetStats() const {
        std::lock_guard<std::mutex> Lock(CacheMutex);
        
        FStats Result = Stats;
        Result.TotalRefCount = 0;
        for (const auto& [Hash, Entry] : LayoutCache) {
            Result.TotalRefCount += Entry.RefCount;
        }
        return Result;
    }

    VkDescriptorSetLayout FVulkanDescriptorSetLayoutCache::CreateLayout(
        const FVulkanDescriptorSetLayoutInfo& LayoutInfo) {
        
        const auto& Functions = VulkanAPI::getFunctions();
        VkDevice VkDev = Device->getLogicalDevice();
        
        // Convert to Vulkan format
        TArray<VkDescriptorSetLayoutBinding> VkBindings;
        VkBindings.reserve(LayoutInfo.Bindings.size());
        
        for (const auto& Binding : LayoutInfo.Bindings) {
            VkDescriptorSetLayoutBinding VkBinding{};
            VkBinding.binding = Binding.Binding;
            VkBinding.descriptorType = Binding.DescriptorType;
            VkBinding.descriptorCount = Binding.DescriptorCount;
            VkBinding.stageFlags = Binding.StageFlags;
            VkBinding.pImmutableSamplers = nullptr;
            VkBindings.push_back(VkBinding);
        }
        
        // Create layout
        VkDescriptorSetLayoutCreateInfo CreateInfo{};
        CreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        CreateInfo.bindingCount = static_cast<uint32>(VkBindings.size());
        CreateInfo.pBindings = VkBindings.data();
        
        VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
        VkResult Result = Functions.vkCreateDescriptorSetLayout(VkDev, &CreateInfo, nullptr, &Layout);
        
        if (Result != VK_SUCCESS) {
            MR_LOG_ERROR("FVulkanDescriptorSetLayoutCache: vkCreateDescriptorSetLayout failed: " + 
                        std::to_string(Result));
            return VK_NULL_HANDLE;
        }
        
        return Layout;
    }

    FVulkanDescriptorSetLayoutEntry* FVulkanDescriptorSetLayoutCache::FindByHash(uint64 Hash) {
        auto It = LayoutCache.find(Hash);
        if (It != LayoutCache.end()) {
            return &It->second;
        }
        return nullptr;
    }

    // ============================================================================
    // FVulkanDescriptorSetKey Implementation
    // ============================================================================

    uint64 FVulkanDescriptorSetKey::GetHash() const {
        // FNV-1a hash
        uint64 Hash = 14695981039346656037ULL;
        
        // Hash layout handle
        Hash ^= reinterpret_cast<uint64>(Layout);
        Hash *= 1099511628211ULL;
        
        // Hash buffer bindings
        for (const auto& [Slot, Binding] : BufferBindings) {
            Hash ^= static_cast<uint64>(Slot);
            Hash *= 1099511628211ULL;
            Hash ^= reinterpret_cast<uint64>(Binding.Buffer);
            Hash *= 1099511628211ULL;
            Hash ^= static_cast<uint64>(Binding.Offset);
            Hash *= 1099511628211ULL;
            Hash ^= static_cast<uint64>(Binding.Range);
            Hash *= 1099511628211ULL;
        }
        
        // Hash image bindings
        for (const auto& [Slot, Binding] : ImageBindings) {
            Hash ^= static_cast<uint64>(Slot);
            Hash *= 1099511628211ULL;
            Hash ^= reinterpret_cast<uint64>(Binding.ImageView);
            Hash *= 1099511628211ULL;
            Hash ^= reinterpret_cast<uint64>(Binding.Sampler);
            Hash *= 1099511628211ULL;
            Hash ^= static_cast<uint64>(Binding.ImageLayout);
            Hash *= 1099511628211ULL;
        }
        
        return Hash;
    }

    bool FVulkanDescriptorSetKey::operator==(const FVulkanDescriptorSetKey& Other) const {
        if (Layout != Other.Layout) return false;
        if (BufferBindings.size() != Other.BufferBindings.size()) return false;
        if (ImageBindings.size() != Other.ImageBindings.size()) return false;
        
        for (const auto& [Slot, Binding] : BufferBindings) {
            auto It = Other.BufferBindings.find(Slot);
            if (It == Other.BufferBindings.end() || !(It->second == Binding)) {
                return false;
            }
        }
        
        for (const auto& [Slot, Binding] : ImageBindings) {
            auto It = Other.ImageBindings.find(Slot);
            if (It == Other.ImageBindings.end() || !(It->second == Binding)) {
                return false;
            }
        }
        
        return true;
    }

    // ============================================================================
    // FVulkanDescriptorSetCache Implementation
    // ============================================================================

    FVulkanDescriptorSetCache::FVulkanDescriptorSetCache(VulkanDevice* InDevice)
        : Device(InDevice)
        , CurrentFrame(0) {
        MR_LOG_INFO("FVulkanDescriptorSetCache: Initialized frame-local descriptor set cache");
    }

    FVulkanDescriptorSetCache::~FVulkanDescriptorSetCache() {
        MR_LOG_INFO("FVulkanDescriptorSetCache: Destroyed, total allocations: " + 
                   std::to_string(Stats.TotalAllocations));
    }

    VkDescriptorSet FVulkanDescriptorSetCache::GetOrAllocate(const FVulkanDescriptorSetKey& Key) {
        std::lock_guard<std::mutex> Lock(CacheMutex);
        
        uint64 Hash = Key.GetHash();
        
        // Check frame cache
        auto It = FrameCache.find(Hash);
        if (It != FrameCache.end()) {
            Stats.CacheHits++;
            MR_LOG_DEBUG("FVulkanDescriptorSetCache: Cache hit for descriptor set");
            return It->second;
        }
        
        // Cache miss - allocate and update
        Stats.CacheMisses++;
        VkDescriptorSet Set = AllocateAndUpdate(Key);
        
        if (Set != VK_NULL_HANDLE) {
            FrameCache[Hash] = Set;
            Stats.CurrentCacheSize = static_cast<uint32>(FrameCache.size());
        }
        
        return Set;
    }

    void FVulkanDescriptorSetCache::Reset(uint64 FrameNumber) {
        std::lock_guard<std::mutex> Lock(CacheMutex);
        
        // Clear frame cache - descriptor sets will be recycled by the allocator
        FrameCache.clear();
        CurrentFrame = FrameNumber;
        Stats.CurrentCacheSize = 0;
        
        MR_LOG_DEBUG("FVulkanDescriptorSetCache: Reset for frame " + std::to_string(FrameNumber));
    }

    FVulkanDescriptorSetCache::FStats FVulkanDescriptorSetCache::GetStats() const {
        std::lock_guard<std::mutex> Lock(CacheMutex);
        return Stats;
    }

    VkDescriptorSet FVulkanDescriptorSetCache::AllocateAndUpdate(const FVulkanDescriptorSetKey& Key) {
        // Get descriptor set allocator from device
        auto* Allocator = Device->getDescriptorSetAllocator();
        if (!Allocator) {
            MR_LOG_ERROR("FVulkanDescriptorSetCache: No descriptor set allocator available");
            return VK_NULL_HANDLE;
        }
        
        // Allocate descriptor set
        VkDescriptorSet Set = Allocator->allocate(Key.Layout);
        if (Set == VK_NULL_HANDLE) {
            MR_LOG_ERROR("FVulkanDescriptorSetCache: Failed to allocate descriptor set");
            return VK_NULL_HANDLE;
        }
        
        // Update with bindings
        UpdateDescriptorSet(Set, Key);
        
        Stats.TotalAllocations++;
        MR_LOG_DEBUG("FVulkanDescriptorSetCache: Allocated and updated new descriptor set");
        
        return Set;
    }

    void FVulkanDescriptorSetCache::UpdateDescriptorSet(VkDescriptorSet Set, 
                                                         const FVulkanDescriptorSetKey& Key) {
        const auto& Functions = VulkanAPI::getFunctions();
        VkDevice VkDev = Device->getLogicalDevice();
        
        TArray<VkWriteDescriptorSet> Writes;
        TArray<VkDescriptorBufferInfo> BufferInfos;
        TArray<VkDescriptorImageInfo> ImageInfos;
        
        // Reserve to prevent reallocation
        BufferInfos.reserve(Key.BufferBindings.size());
        ImageInfos.reserve(Key.ImageBindings.size());
        
        // Add buffer writes
        for (const auto& [Slot, Binding] : Key.BufferBindings) {
            if (Binding.Buffer == VK_NULL_HANDLE) continue;
            
            VkDescriptorBufferInfo& BufferInfo = BufferInfos.emplace_back();
            BufferInfo.buffer = Binding.Buffer;
            BufferInfo.offset = Binding.Offset;
            BufferInfo.range = Binding.Range > 0 ? Binding.Range : VK_WHOLE_SIZE;
            
            VkWriteDescriptorSet Write{};
            Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Write.dstSet = Set;
            Write.dstBinding = Slot;
            Write.dstArrayElement = 0;
            Write.descriptorCount = 1;
            Write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            Write.pBufferInfo = &BufferInfos.back();
            Writes.push_back(Write);
        }
        
        // Add image writes
        for (const auto& [Slot, Binding] : Key.ImageBindings) {
            if (Binding.ImageView == VK_NULL_HANDLE) continue;
            
            VkDescriptorImageInfo& ImageInfo = ImageInfos.emplace_back();
            ImageInfo.imageView = Binding.ImageView;
            ImageInfo.sampler = Binding.Sampler;
            ImageInfo.imageLayout = Binding.ImageLayout;
            
            VkWriteDescriptorSet Write{};
            Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Write.dstSet = Set;
            Write.dstBinding = Slot;
            Write.dstArrayElement = 0;
            Write.descriptorCount = 1;
            Write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            Write.pImageInfo = &ImageInfos.back();
            Writes.push_back(Write);
        }
        
        // Execute updates
        if (!Writes.empty()) {
            Functions.vkUpdateDescriptorSets(VkDev, static_cast<uint32>(Writes.size()), 
                                             Writes.data(), 0, nullptr);
        }
    }

} // namespace MonsterRender::RHI::Vulkan
