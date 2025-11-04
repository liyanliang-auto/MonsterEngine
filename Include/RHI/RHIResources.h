// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - RHI Resource Base Classes (UE5-style)
//
// 参考 UE5: Engine/Source/Runtime/RHI/Public/RHIResources.h

#pragma once

#include "Core/CoreTypes.h"
#include "RHI/RHIDefinitions.h"
#include <atomic>

namespace MonsterRender {
namespace RHI {

/**
 * FRHIResource - RHI 资源基类
 * 
 * 所有 RHI 资源（Buffer、Texture、Shader 等）的基类
 * 提供引用计数、生命周期管理
 * 
 * 参考 UE5: FRHIResource
 */
class FRHIResource {
public:
    FRHIResource()
        : RefCount(1)
        , bCommitted(false)
    {}
    
    virtual ~FRHIResource() = default;
    
    // 引用计数管理
    uint32 AddRef() const {
        return RefCount.fetch_add(1) + 1;
    }
    
    uint32 Release() const {
        uint32 refs = RefCount.fetch_sub(1) - 1;
        if (refs == 0) {
            delete this;
        }
        return refs;
    }
    
    uint32 GetRefCount() const {
        return RefCount.load();
    }
    
    // 资源提交状态（用于延迟初始化）
    bool IsCommitted() const { return bCommitted; }
    void SetCommitted(bool committed) { bCommitted = committed; }
    
    // 调试信息
    void SetDebugName(const String& name) { DebugName = name; }
    const String& GetDebugName() const { return DebugName; }

protected:
    mutable std::atomic<uint32> RefCount;  // 引用计数（线程安全）
    bool bCommitted;                       // 是否已提交到 GPU
    String DebugName;                      // 调试名称
};

/**
 * TRefCountPtr - 引用计数智能指针
 * 
 * 自动管理 FRHIResource 的引用计数
 * 参考 UE5: TRefCountPtr
 */
template<typename T>
class TRefCountPtr {
public:
    TRefCountPtr() : Pointer(nullptr) {}
    
    TRefCountPtr(T* InPointer) : Pointer(InPointer) {
        if (Pointer) {
            Pointer->AddRef();
        }
    }
    
    TRefCountPtr(const TRefCountPtr& Other) : Pointer(Other.Pointer) {
        if (Pointer) {
            Pointer->AddRef();
        }
    }
    
    TRefCountPtr(TRefCountPtr&& Other) noexcept : Pointer(Other.Pointer) {
        Other.Pointer = nullptr;
    }
    
    ~TRefCountPtr() {
        if (Pointer) {
            Pointer->Release();
        }
    }
    
    TRefCountPtr& operator=(T* InPointer) {
        if (Pointer != InPointer) {
            if (Pointer) {
                Pointer->Release();
            }
            Pointer = InPointer;
            if (Pointer) {
                Pointer->AddRef();
            }
        }
        return *this;
    }
    
    TRefCountPtr& operator=(const TRefCountPtr& Other) {
        return *this = Other.Pointer;
    }
    
    TRefCountPtr& operator=(TRefCountPtr&& Other) noexcept {
        if (this != &Other) {
            if (Pointer) {
                Pointer->Release();
            }
            Pointer = Other.Pointer;
            Other.Pointer = nullptr;
        }
        return *this;
    }
    
    T* operator->() const { return Pointer; }
    T& operator*() const { return *Pointer; }
    operator T*() const { return Pointer; }
    T* Get() const { return Pointer; }
    
    bool IsValid() const { return Pointer != nullptr; }
    
    void SafeRelease() {
        if (Pointer) {
            Pointer->Release();
            Pointer = nullptr;
        }
    }

private:
    T* Pointer;
};

/**
 * FRHIBuffer - RHI 缓冲区基类
 * 
 * 表示 GPU 缓冲区（Vertex Buffer、Index Buffer、Uniform Buffer 等）
 * 参考 UE5: FRHIBuffer
 */
class FRHIBuffer : public FRHIResource {
public:
    FRHIBuffer(uint32 InSize, EResourceUsage InUsage, uint32 InStride = 0)
        : Size(InSize)
        , Usage(InUsage)
        , Stride(InStride)
    {}
    
    virtual ~FRHIBuffer() = default;
    
    // 缓冲区属性
    uint32 GetSize() const { return Size; }
    EResourceUsage GetUsage() const { return Usage; }
    uint32 GetStride() const { return Stride; }
    
    // 映射/取消映射（需要子类实现）
    virtual void* Lock(uint32 Offset, uint32 InSize) = 0;
    virtual void Unlock() = 0;
    
    // GPU 虚拟地址（用于 Shader 直接访问）
    virtual uint64 GetGPUVirtualAddress() const { return 0; }

protected:
    uint32 Size;              // 缓冲区大小（字节）
    EResourceUsage Usage;     // 使用标志
    uint32 Stride;            // 元素步长（用于结构化缓冲区）
};

/**
 * FRHITexture - RHI 纹理基类
 * 
 * 表示 GPU 纹理（Texture2D、Texture3D、TextureCube 等）
 * 参考 UE5: FRHITexture
 */
class FRHITexture : public FRHIResource {
public:
    FRHITexture(const TextureDesc& InDesc)
        : Desc(InDesc)
    {}
    
    virtual ~FRHITexture() = default;
    
    // 纹理属性
    uint32 GetWidth() const { return Desc.width; }
    uint32 GetHeight() const { return Desc.height; }
    uint32 GetDepth() const { return Desc.depth; }
    uint32 GetMipLevels() const { return Desc.mipLevels; }
    uint32 GetArraySize() const { return Desc.arraySize; }
    EPixelFormat GetFormat() const { return Desc.format; }
    EResourceUsage GetUsage() const { return Desc.usage; }
    const TextureDesc& GetDesc() const { return Desc; }

protected:
    TextureDesc Desc;  // 纹理描述符
};

/**
 * FRHIVertexBuffer - 顶点缓冲区
 */
class FRHIVertexBuffer : public FRHIBuffer {
public:
    FRHIVertexBuffer(uint32 InSize, uint32 InStride)
        : FRHIBuffer(InSize, EResourceUsage::VertexBuffer, InStride)
    {}
};

/**
 * FRHIIndexBuffer - 索引缓冲区
 */
class FRHIIndexBuffer : public FRHIBuffer {
public:
    FRHIIndexBuffer(uint32 InSize, bool bIn32Bit)
        : FRHIBuffer(InSize, EResourceUsage::IndexBuffer, bIn32Bit ? 4 : 2)
        , b32Bit(bIn32Bit)
    {}
    
    bool Is32Bit() const { return b32Bit; }

private:
    bool b32Bit;  // true = 32位索引，false = 16位索引
};

/**
 * FRHIUniformBuffer - Uniform 缓冲区
 */
class FRHIUniformBuffer : public FRHIBuffer {
public:
    FRHIUniformBuffer(uint32 InSize)
        : FRHIBuffer(InSize, EResourceUsage::UniformBuffer, 0)
    {}
};

/**
 * FRHITexture2D - 2D 纹理
 */
class FRHITexture2D : public FRHITexture {
public:
    FRHITexture2D(const TextureDesc& InDesc)
        : FRHITexture(InDesc)
    {
        // 确保是 2D 纹理
        Desc.depth = 1;
        Desc.arraySize = std::max(Desc.arraySize, 1u);
    }
};

/**
 * FRHITextureCube - Cube 纹理
 */
class FRHITextureCube : public FRHITexture {
public:
    FRHITextureCube(const TextureDesc& InDesc)
        : FRHITexture(InDesc)
    {
        // Cube 纹理固定为 6 个面
        Desc.arraySize = 6;
    }
};

// ============================================================================
// RHI 资源引用类型定义（方便使用）
// ============================================================================

using FRHIResourceRef = TRefCountPtr<FRHIResource>;
using FRHIBufferRef = TRefCountPtr<FRHIBuffer>;
using FRHITextureRef = TRefCountPtr<FRHITexture>;
using FRHIVertexBufferRef = TRefCountPtr<FRHIVertexBuffer>;
using FRHIIndexBufferRef = TRefCountPtr<FRHIIndexBuffer>;
using FRHIUniformBufferRef = TRefCountPtr<FRHIUniformBuffer>;
using FRHITexture2DRef = TRefCountPtr<FRHITexture2D>;
using FRHITextureCubeRef = TRefCountPtr<FRHITextureCube>;

} // namespace RHI
} // namespace MonsterRender
