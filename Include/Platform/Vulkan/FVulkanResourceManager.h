// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Resource Manager
//
// 参考 UE5: Engine/Source/Runtime/VulkanRHI/Private/VulkanResources.h

#pragma once

#include "Core/CoreTypes.h"
#include "RHI/RHIResources.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Platform/Vulkan/FVulkanMemoryManager.h"
#include <mutex>
#include <vector>

namespace MonsterRender {
namespace RHI {

// 前向声明
class FVulkanResourceManager;
class VulkanDevice;

/**
 * FVulkanBuffer - Vulkan 缓冲区实现
 * 
 * 实现 FRHIBuffer 接口，管理 Vulkan 缓冲区资源。
 * 使用 FVulkanMemoryManager 进行内存分配。
 * 
 * 参考 UE5: FVulkanResourceMultiBuffer
 */
class FVulkanBuffer : public FRHIBuffer {
public:
    /**
     * 构造函数
     * @param Device Vulkan 设备
     * @param InSize 缓冲区大小
     * @param InUsage 用途标志
     * @param InMemoryFlags 内存属性标志
     * @param InStride 步长
     */
    FVulkanBuffer(VulkanDevice* Device, uint32 InSize, EResourceUsage InUsage,
                  VkMemoryPropertyFlags InMemoryFlags, uint32 InStride = 0);
    
    virtual ~FVulkanBuffer();
    
    // FRHIBuffer 接口实现
    virtual void* Map(uint32 Offset, uint32 Size) override;
    virtual void Unmap() override;
    virtual uint64 GetGPUVirtualAddress() const override;
    
    // Vulkan 特定接口
    VkBuffer GetHandle() const { return Buffer; }
    const FVulkanAllocation& GetAllocation() const { return Allocation; }
    VkDeviceSize GetOffset() const { return Allocation.Offset; }
    
    /**
     * 初始化缓冲区（创建 VkBuffer 并分配内存）
     * @return 是否成功
     */
    bool Initialize();
    
    /**
     * 销毁缓冲区
     */
    void Destroy();

private:
    VulkanDevice* Device;                   // 所属设备
    VkBuffer Buffer;                        // Vulkan 缓冲区句柄
    FVulkanAllocation Allocation;           // 内存分配
    VkMemoryPropertyFlags MemoryFlags;      // 内存属性
    void* MappedPtr;                        // 映射指针
    bool bPersistentMapped;                 // 是否持久映射
};

/**
 * FVulkanTexture - Vulkan 纹理实现
 * 
 * 实现 FRHITexture 接口，管理 Vulkan 纹理资源。
 * 使用 FVulkanMemoryManager 进行内存分配。
 * 
 * 参考 UE5: FVulkanTexture
 */
class FVulkanTexture : public FRHITexture {
public:
    /**
     * 构造函数
     * @param Device Vulkan 设备
     * @param InDesc 纹理描述符
     * @param InMemoryFlags 内存属性标志
     */
    FVulkanTexture(VulkanDevice* Device, const TextureDesc& InDesc,
                   VkMemoryPropertyFlags InMemoryFlags);
    
    virtual ~FVulkanTexture();
    
    // Vulkan 特定接口
    VkImage GetHandle() const { return Image; }
    VkImageView GetView() const { return ImageView; }
    const FVulkanAllocation& GetAllocation() const { return Allocation; }
    VkImageLayout GetLayout() const { return CurrentLayout; }
    
    /**
     * 设置图像布局
     * @param NewLayout 新布局
     */
    void SetLayout(VkImageLayout NewLayout) { CurrentLayout = NewLayout; }
    
    /**
     * 初始化纹理（创建 VkImage 并分配内存）
     * @return 是否成功
     */
    bool Initialize();
    
    /**
     * 销毁纹理
     */
    void Destroy();
    
    /**
     * 创建 Image View
     * @return 是否成功
     */
    bool CreateImageView();

private:
    VulkanDevice* Device;                   // 所属设备
    VkImage Image;                          // Vulkan 图像句柄
    VkImageView ImageView;                  // 图像视图
    FVulkanAllocation Allocation;           // 内存分配
    VkMemoryPropertyFlags MemoryFlags;      // 内存属性
    VkImageLayout CurrentLayout;            // 当前布局
};

/**
 * FVulkanResourceManager - Vulkan 资源管理器
 * 
 * 管理所有 Vulkan 资源（缓冲区、纹理等）的创建、销毁和生命周期。
 * 提供资源池和复用机制。
 * 
 * 参考 UE5: FVulkanResourceHeapManager + FVulkanResourceManager
 */
class FVulkanResourceManager {
public:
    /**
     * 构造函数
     * @param Device Vulkan 设备
     * @param MemoryManager 内存管理器
     */
    FVulkanResourceManager(VulkanDevice* Device, FVulkanMemoryManager* MemoryManager);
    ~FVulkanResourceManager();
    
    // 不可拷贝
    FVulkanResourceManager(const FVulkanResourceManager&) = delete;
    FVulkanResourceManager& operator=(const FVulkanResourceManager&) = delete;
    
    /**
     * 创建缓冲区
     * @param Size 大小
     * @param Usage 用途
     * @param MemoryFlags 内存属性
     * @param Stride 步长
     * @return 缓冲区引用
     */
    FRHIBufferRef CreateBuffer(uint32 Size, EResourceUsage Usage,
                                VkMemoryPropertyFlags MemoryFlags, uint32 Stride = 0);
    
    /**
     * 创建纹理
     * @param Desc 纹理描述符
     * @param MemoryFlags 内存属性
     * @return 纹理引用
     */
    FRHITextureRef CreateTexture(const TextureDesc& Desc,
                                   VkMemoryPropertyFlags MemoryFlags);
    
    /**
     * 释放资源（延迟释放，等待 GPU 完成使用）
     * @param Resource 要释放的资源
     * @param FrameNumber 当前帧号
     */
    void DeferredRelease(FRHIResource* Resource, uint64 FrameNumber);
    
    /**
     * 处理延迟释放（每帧调用）
     * @param CompletedFrameNumber 已完成的帧号
     */
    void ProcessDeferredReleases(uint64 CompletedFrameNumber);
    
    /**
     * 获取统计信息
     */
    struct FResourceStats {
        uint32 NumBuffers;              // 缓冲区数量
        uint32 NumTextures;             // 纹理数量
        uint64 BufferMemory;            // 缓冲区内存
        uint64 TextureMemory;           // 纹理内存
        uint32 PendingReleases;         // 待释放资源数量
        
        FResourceStats()
            : NumBuffers(0), NumTextures(0), BufferMemory(0)
            , TextureMemory(0), PendingReleases(0) {}
    };
    
    void GetResourceStats(FResourceStats& OutStats);
    
    /**
     * 立即释放所有未使用的资源
     */
    void ReleaseUnusedResources();

private:
    /**
     * 延迟释放条目
     */
    struct FDeferredReleaseEntry {
        FRHIResource* Resource;         // 资源指针
        uint64 FrameNumber;             // 提交帧号
        
        FDeferredReleaseEntry(FRHIResource* InResource, uint64 InFrameNumber)
            : Resource(InResource), FrameNumber(InFrameNumber) {}
    };
    
    VulkanDevice* Device;                       // Vulkan 设备
    FVulkanMemoryManager* MemoryManager;        // 内存管理器
    
    // 资源追踪
    std::vector<FVulkanBuffer*> ActiveBuffers;
    std::vector<FVulkanTexture*> ActiveTextures;
    std::mutex ResourcesMutex;
    
    // 延迟释放队列
    std::vector<FDeferredReleaseEntry> DeferredReleases;
    std::mutex DeferredReleasesMutex;
    
    // 统计
    std::atomic<uint32> TotalBufferCount;
    std::atomic<uint32> TotalTextureCount;
    std::atomic<uint64> TotalBufferMemory;
    std::atomic<uint64> TotalTextureMemory;
    
    // 配置
    static constexpr uint64 DEFERRED_RELEASE_FRAMES = 3;  // 延迟 3 帧释放
};

} // namespace RHI
} // namespace MonsterRender


