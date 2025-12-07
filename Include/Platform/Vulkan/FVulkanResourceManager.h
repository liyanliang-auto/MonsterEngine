// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Resource Manager
//
// Reference UE5: Engine/Source/Runtime/VulkanRHI/Private/VulkanResources.h

#pragma once

#include "Core/CoreTypes.h"
#include "RHI/RHIResources.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Platform/Vulkan/FVulkanMemoryManager.h"
#include "Containers/Array.h"
#include "Containers/Deque.h"
#include <mutex>

namespace MonsterRender {
namespace RHI {

// Forward declarations
namespace Vulkan {
    class VulkanDevice;
}

/**
 * FVulkanResourceMultiBuffer - Multi-buffered Vulkan buffer
 * 
 * Implements triple-buffering for frequently updated buffers (e.g., Uniform Buffers).
 * Avoids CPU-GPU synchronization by maintaining multiple buffer instances.
 * 
 * Reference UE5: FVulkanResourceMultiBuffer
 */
class FVulkanResourceMultiBuffer : public FRHIBuffer {
public:
    /**
     * Constructor
     * @param Device Vulkan device
     * @param InSize Buffer size per frame
     * @param InUsage Buffer usage flags
     * @param InMemoryFlags Memory property flags
     * @param NumBuffers Number of buffers (default 3 for triple buffering)
     */
    FVulkanResourceMultiBuffer(
        Vulkan::VulkanDevice* Device,
        uint32 InSize,
        EResourceUsage InUsage,
        VkMemoryPropertyFlags InMemoryFlags,
        uint32 NumBuffers = 3
    );
    
    virtual ~FVulkanResourceMultiBuffer();
    
    // FRHIBuffer interface
    virtual void* Lock(uint32 Offset, uint32 InSize) override;
    virtual void Unlock() override;
    virtual uint64 GetGPUVirtualAddress() const override;
    
    /**
     * Advance to next buffer (call at frame boundary)
     */
    void AdvanceFrame();
    
    /**
     * Get current frame's buffer handle
     */
    VkBuffer GetCurrentHandle() const;
    
    /**
     * Get current frame's allocation
     */
    const FVulkanAllocation& GetCurrentAllocation() const;
    
    /**
     * Get specific buffer handle
     * @param Index Buffer index
     */
    VkBuffer GetHandle(uint32 Index) const;
    
    /**
     * Initialize all buffers
     */
    bool Initialize();
    
    /**
     * Destroy all buffers
     */
    void Destroy();

private:
    /**
     * Single buffer instance
     */
    struct FBufferInstance {
        VkBuffer Buffer;
        FVulkanAllocation Allocation;
        void* MappedPtr;
        
        FBufferInstance()
            : Buffer(VK_NULL_HANDLE)
            , MappedPtr(nullptr)
        {}
    };
    
    Vulkan::VulkanDevice* Device;           // Vulkan device
    VkMemoryPropertyFlags MemoryFlags;      // Memory properties
    uint32 NumBuffers;                      // Number of buffer instances
    uint32 CurrentBufferIndex;              // Current active buffer
    
    TArray<FBufferInstance> Buffers;        // Buffer instances
    std::mutex Mutex;                        // Thread safety
};

/**
 * FVulkanTexture - Vulkan texture implementation
 * 
 * Implements FRHITexture interface, manages Vulkan image resources.
 * Uses FVulkanMemoryManager for memory allocation.
 * 
 * Reference UE5: FVulkanTexture
 */
class FVulkanTexture : public FRHITexture {
public:
    /**
     * Constructor
     * @param Device Vulkan device
     * @param InDesc Texture descriptor
     * @param InMemoryFlags Memory property flags
     */
    FVulkanTexture(
        Vulkan::VulkanDevice* Device,
        const TextureDesc& InDesc,
        VkMemoryPropertyFlags InMemoryFlags
    );
    
    virtual ~FVulkanTexture();
    
    // Vulkan-specific interface
    VkImage GetHandle() const { return Image; }
    VkImageView GetView() const { return ImageView; }
    const FVulkanAllocation& GetAllocation() const { return Allocation; }
    VkImageLayout GetLayout() const { return CurrentLayout; }
    
    /**
     * Set image layout
     * @param NewLayout New layout
     */
    void SetLayout(VkImageLayout NewLayout) { CurrentLayout = NewLayout; }
    
    /**
     * Initialize texture (create VkImage and allocate memory)
     * @return Success status
     */
    bool Initialize();
    
    /**
     * Destroy texture
     */
    void Destroy();
    
    /**
     * Create image view
     * @return Success status
     */
    bool CreateImageView();

private:
    Vulkan::VulkanDevice* Device;           // Vulkan device
    VkImage Image;                          // Vulkan image handle
    VkImageView ImageView;                  // Image view
    FVulkanAllocation Allocation;           // Memory allocation
    VkMemoryPropertyFlags MemoryFlags;      // Memory properties
    VkImageLayout CurrentLayout;            // Current layout
};

/**
 * FVulkanResourceManager - Vulkan resource manager
 * 
 * Manages all Vulkan resources (buffers, textures, etc.) lifecycle.
 * Provides resource pooling and reuse mechanisms.
 * 
 * Reference UE5: FVulkanResourceHeapManager + FVulkanResourceManager
 */
class FVulkanResourceManager {
public:
    /**
     * Constructor
     * @param Device Vulkan device
     * @param MemoryManager Memory manager
     */
    FVulkanResourceManager(Vulkan::VulkanDevice* Device, FVulkanMemoryManager* MemoryManager);
    ~FVulkanResourceManager();
    
    // Non-copyable
    FVulkanResourceManager(const FVulkanResourceManager&) = delete;
    FVulkanResourceManager& operator=(const FVulkanResourceManager&) = delete;
    
    /**
     * Create buffer
     * @param Size Buffer size
     * @param Usage Usage flags
     * @param MemoryFlags Memory properties
     * @param Stride Element stride
     * @return Buffer reference
     */
    FRHIBufferRef CreateBuffer(
        uint32 Size,
        EResourceUsage Usage,
        VkMemoryPropertyFlags MemoryFlags,
        uint32 Stride = 0
    );
    
    /**
     * Create multi-buffer (for dynamic updates)
     * @param Size Buffer size per frame
     * @param Usage Usage flags
     * @param MemoryFlags Memory properties
     * @param NumBuffers Number of buffers
     * @return Multi-buffer reference
     */
    TRefCountPtr<FVulkanResourceMultiBuffer> CreateMultiBuffer(
        uint32 Size,
        EResourceUsage Usage,
        VkMemoryPropertyFlags MemoryFlags,
        uint32 NumBuffers = 3
    );
    
    /**
     * Create texture
     * @param Desc Texture descriptor
     * @param MemoryFlags Memory properties
     * @return Texture reference
     */
    FRHITextureRef CreateTexture(
        const TextureDesc& Desc,
        VkMemoryPropertyFlags MemoryFlags
    );
    
    /**
     * Deferred resource release (waits for GPU to finish using)
     * @param Resource Resource to release
     * @param FrameNumber Current frame number
     */
    void DeferredRelease(FRHIResource* Resource, uint64 FrameNumber);
    
    /**
     * Process deferred releases (call per frame)
     * @param CompletedFrameNumber Completed frame number
     */
    void ProcessDeferredReleases(uint64 CompletedFrameNumber);
    
    /**
     * Advance frame (for multi-buffers)
     */
    void AdvanceFrame();
    
    /**
     * Resource statistics
     */
    struct FResourceStats {
        uint32 NumBuffers;              // Buffer count
        uint32 NumMultiBuffers;         // Multi-buffer count
        uint32 NumTextures;             // Texture count
        uint64 BufferMemory;            // Buffer memory
        uint64 TextureMemory;           // Texture memory
        uint32 PendingReleases;         // Pending release count
        
        FResourceStats()
            : NumBuffers(0), NumMultiBuffers(0), NumTextures(0)
            , BufferMemory(0), TextureMemory(0), PendingReleases(0)
        {}
    };
    
    /**
     * Get resource statistics
     */
    void GetResourceStats(FResourceStats& OutStats);
    
    /**
     * Release all unused resources immediately
     */
    void ReleaseUnusedResources();

private:
    /**
     * Deferred release entry
     */
    struct FDeferredReleaseEntry {
        FRHIResource* Resource;         // Resource pointer
        uint64 FrameNumber;             // Submission frame number
        
        FDeferredReleaseEntry(FRHIResource* InResource, uint64 InFrameNumber)
            : Resource(InResource), FrameNumber(InFrameNumber)
        {}
    };
    
    Vulkan::VulkanDevice* Device;                           // Vulkan device
    FVulkanMemoryManager* MemoryManager;                    // Memory manager
    
    // Resource tracking
    TArray<FRHIBuffer*> ActiveBuffers;                      // Active buffers
    TArray<FVulkanResourceMultiBuffer*> ActiveMultiBuffers; // Active multi-buffers
    TArray<FVulkanTexture*> ActiveTextures;                 // Active textures
    std::mutex ResourcesMutex;                              // Resource lock
    
    // Deferred release queue
    TDeque<FDeferredReleaseEntry> DeferredReleases;         // Release queue
    std::mutex DeferredReleasesMutex;                       // Release queue lock
    
    // Statistics
    std::atomic<uint32> TotalBufferCount;
    std::atomic<uint32> TotalMultiBufferCount;
    std::atomic<uint32> TotalTextureCount;
    std::atomic<uint64> TotalBufferMemory;
    std::atomic<uint64> TotalTextureMemory;
    
    // Configuration
    static constexpr uint64 DEFERRED_RELEASE_FRAMES = 3;    // Wait 3 frames before release
};

} // namespace RHI
} // namespace MonsterRender

