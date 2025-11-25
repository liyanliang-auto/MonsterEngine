// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - RHI Resource Base Classes (UE5-style)
//
// Reference UE5: Engine/Source/Runtime/RHI/Public/RHIResources.h

#pragma once

#include "Core/CoreTypes.h"
#include "RHI/RHIDefinitions.h"
#include <atomic>

namespace MonsterRender {
namespace RHI {

/**
 * FRHIResource - RHI Resource Base Class
 * 
 * Base class for all RHI resources (Buffer, Texture, Shader, etc.)
 * Provides reference counting and lifecycle management
 * 
 * Reference UE5: FRHIResource
 */
class FRHIResource {
public:
    FRHIResource()
        : RefCount(1)
        , bCommitted(false)
    {}
    
    virtual ~FRHIResource() = default;
    
    // Reference counting
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
    
    // Resource commit status (for deferred initialization)
    bool IsCommitted() const { return bCommitted; }
    void SetCommitted(bool committed) { bCommitted = committed; }
    
    // Debug info
    void SetDebugName(const String& name) { DebugName = name; }
    const String& GetDebugName() const { return DebugName; }

protected:
    mutable std::atomic<uint32> RefCount;  // Thread-safe reference count
    bool bCommitted;                       // Whether committed to GPU
    String DebugName;                      // Debug name
};

/**
 * TRefCountPtr - Reference counting smart pointer
 * 
 * Automatically manages FRHIResource reference counting
 * Reference UE5: TRefCountPtr
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
 * FRHIBuffer - RHI Buffer Base Class
 * 
 * Represents GPU buffer (Vertex Buffer, Index Buffer, Uniform Buffer, etc.)
 * Reference UE5: FRHIBuffer
 */
class FRHIBuffer : public FRHIResource {
public:
    FRHIBuffer(uint32 InSize, EResourceUsage InUsage, uint32 InStride = 0)
        : Size(InSize)
        , Usage(InUsage)
        , Stride(InStride)
    {}
    
    virtual ~FRHIBuffer() = default;
    
    // Buffer properties
    uint32 GetSize() const { return Size; }
    EResourceUsage GetUsage() const { return Usage; }
    uint32 GetStride() const { return Stride; }
    
    // Map/Unmap (must be implemented by subclass)
    virtual void* Lock(uint32 Offset, uint32 InSize) = 0;
    virtual void Unlock() = 0;
    
    // GPU virtual address (for shader direct access)
    virtual uint64 GetGPUVirtualAddress() const { return 0; }

protected:
    uint32 Size;              // Buffer size (bytes)
    EResourceUsage Usage;     // Usage flags
    uint32 Stride;            // Element stride (for structured buffers)
};

/**
 * FRHITexture - RHI Texture Base Class
 * 
 * Represents GPU texture (Texture2D, Texture3D, TextureCube, etc.)
 * Reference UE5: FRHITexture
 */
class FRHITexture : public FRHIResource {
public:
    FRHITexture(const TextureDesc& InDesc)
        : Desc(InDesc)
    {}
    
    virtual ~FRHITexture() = default;
    
    // Texture properties
    uint32 GetWidth() const { return Desc.width; }
    uint32 GetHeight() const { return Desc.height; }
    uint32 GetDepth() const { return Desc.depth; }
    uint32 GetMipLevels() const { return Desc.mipLevels; }
    uint32 GetArraySize() const { return Desc.arraySize; }
    EPixelFormat GetFormat() const { return Desc.format; }
    EResourceUsage GetUsage() const { return Desc.usage; }
    const TextureDesc& GetDesc() const { return Desc; }

protected:
    TextureDesc Desc;  // Texture descriptor
};

/**
 * FRHIVertexBuffer - Vertex Buffer
 */
class FRHIVertexBuffer : public FRHIBuffer {
public:
    FRHIVertexBuffer(uint32 InSize, uint32 InStride)
        : FRHIBuffer(InSize, EResourceUsage::VertexBuffer, InStride)
    {}
};

/**
 * FRHIIndexBuffer - Index Buffer
 */
class FRHIIndexBuffer : public FRHIBuffer {
public:
    FRHIIndexBuffer(uint32 InSize, bool bIn32Bit)
        : FRHIBuffer(InSize, EResourceUsage::IndexBuffer, bIn32Bit ? 4 : 2)
        , b32Bit(bIn32Bit)
    {}
    
    bool Is32Bit() const { return b32Bit; }

private:
    bool b32Bit;  // true = 32-bit index, false = 16-bit index
};

/**
 * FRHIUniformBuffer - Uniform Buffer
 */
class FRHIUniformBuffer : public FRHIBuffer {
public:
    FRHIUniformBuffer(uint32 InSize)
        : FRHIBuffer(InSize, EResourceUsage::UniformBuffer, 0)
    {}
};

/**
 * FRHITexture2D - 2D Texture
 */
class FRHITexture2D : public FRHITexture {
public:
    FRHITexture2D(const TextureDesc& InDesc)
        : FRHITexture(InDesc)
    {
        // Ensure 2D texture
        Desc.depth = 1;
        // Use parentheses to avoid Windows max macro conflict
        Desc.arraySize = (std::max)(Desc.arraySize, 1u);
    }
};

/**
 * FRHITextureCube - Cube Texture
 */
class FRHITextureCube : public FRHITexture {
public:
    FRHITextureCube(const TextureDesc& InDesc)
        : FRHITexture(InDesc)
    {
        // Cube texture has 6 faces
        Desc.arraySize = 6;
    }
};

/**
 * Sampler filter mode
 * Reference UE5: ESamplerFilter
 */
enum class ESamplerFilter : uint8 {
    Point,      // Nearest neighbor
    Bilinear,   // Linear filtering
    Trilinear,  // Linear with mipmaps
    Anisotropic // Anisotropic filtering
};

/**
 * Sampler address mode  
 * Reference UE5: ESamplerAddressMode
 */
enum class ESamplerAddressMode : uint8 {
    Wrap,       // Repeat texture
    Clamp,      // Clamp to edge
    Mirror,     // Mirror repeat
    Border      // Use border color
};

/**
 * Sampler descriptor
 * Reference UE5: FSamplerStateInitializerRHI
 */
struct SamplerDesc {
    ESamplerFilter filter = ESamplerFilter::Bilinear;
    ESamplerAddressMode addressU = ESamplerAddressMode::Wrap;
    ESamplerAddressMode addressV = ESamplerAddressMode::Wrap;
    ESamplerAddressMode addressW = ESamplerAddressMode::Wrap;
    float32 mipLODBias = 0.0f;
    uint32 maxAnisotropy = 1;
    EComparisonFunc comparisonFunc = EComparisonFunc::Never;
    float32 borderColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float32 minLOD = 0.0f;
    float32 maxLOD = 1000.0f;
    String debugName;
};

/**
 * FRHISampler - Sampler State
 * Reference UE5: FRHISamplerState
 */
class FRHISampler : public FRHIResource {
public:
    FRHISampler(const SamplerDesc& InDesc)
        : Desc(InDesc)
    {}
    
    virtual ~FRHISampler() = default;
    
    const SamplerDesc& GetDesc() const { return Desc; }
    
protected:
    SamplerDesc Desc;
};

// ============================================================================
// RHI Resource reference types (for convenience)
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
