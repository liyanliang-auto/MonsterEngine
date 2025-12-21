#pragma once

#include "Core/CoreMinimal.h"
#include "RDG/RDGDefinitions.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/IRHIResource.h"

/**
 * RDG Resource definitions
 * Reference: UE5 RenderGraphResources.h
 */

namespace MonsterRender {
namespace RDG {

// Import commonly used types into RDG namespace
using MonsterEngine::FString;
using MonsterEngine::TArray;

// Forward declarations
class FRDGBuilder;
class FRDGPass;

/**
 * Handle type for tracking pass execution order
 */
struct FRDGPassHandle
{
    uint16 index = 0xFFFF;
    
    FRDGPassHandle() = default;
    explicit FRDGPassHandle(uint16 inIndex) : index(inIndex) {}
    
    bool isValid() const { return index != 0xFFFF; }
    bool operator==(FRDGPassHandle other) const { return index == other.index; }
    bool operator!=(FRDGPassHandle other) const { return index != other.index; }
    bool operator<(FRDGPassHandle other) const { return index < other.index; }
    
    // Friend hash function for TMap support
    friend uint32 GetTypeHash(const FRDGPassHandle& handle)
    {
        return MonsterEngine::GetTypeHash(handle.index);
    }
};

}} // namespace MonsterRender::RDG

namespace MonsterRender {
namespace RDG {

/**
 * Subresource state tracking for individual mip levels and array slices
 * Reference: UE5 FRDGSubresourceState
 */
struct FRDGSubresourceState
{
    // Current access state
    ERHIAccess access = ERHIAccess::Unknown;
    
    // First pass that uses this subresource
    FRDGPassHandle firstPass;
    
    // Last pass that uses this subresource
    FRDGPassHandle lastPass;
    
    FRDGSubresourceState() = default;
    
    explicit FRDGSubresourceState(ERHIAccess inAccess)
        : access(inAccess)
    {}
    
    // Set the pass handle for this state
    void setPass(FRDGPassHandle passHandle)
    {
        if (!firstPass.isValid())
        {
            firstPass = passHandle;
        }
        lastPass = passHandle;
    }
    
    // Check if transition is required between two states
    static bool isTransitionRequired(const FRDGSubresourceState& previous, 
                                     const FRDGSubresourceState& next)
    {
        // Always transition if previous state is unknown
        if (previous.access == ERHIAccess::Unknown)
        {
            return true;
        }
        
        // No transition needed if states match
        if (previous.access == next.access)
        {
            return false;
        }
        
        // Transition required if moving from write to read or vice versa
        bool prevWritable = isWritableAccess(previous.access);
        bool nextWritable = isWritableAccess(next.access);
        
        if (prevWritable || nextWritable)
        {
            return true;
        }
        
        // Multiple read-only states can coexist without transition
        return false;
    }
};

/**
 * Base class for all RDG resources
 * Reference: UE5 FRDGResource
 */
class FRDGResource
{
public:
    FRDGResource(const FRDGResource&) = delete;
    FRDGResource& operator=(const FRDGResource&) = delete;
    virtual ~FRDGResource() = default;
    
    // Get resource name for debugging
    const FString& getName() const { return m_name; }
    
    // Get the underlying RHI resource (only valid during pass execution)
    RHI::IRHIResource* getRHI() const
    {
        IF_RDG_ENABLE_DEBUG(validateRHIAccess());
        return m_resourceRHI;
    }
    
    // Check if RHI resource has been allocated
    bool hasRHI() const { return m_resourceRHI != nullptr; }
    
protected:
    FRDGResource(const FString& inName)
        : m_name(inName)
    {}
    
    // Resource name for debugging
    FString m_name;
    
    // Underlying RHI resource (allocated during graph execution)
    RHI::IRHIResource* m_resourceRHI = nullptr;
    
#if RDG_ENABLE_DEBUG
    void validateRHIAccess() const;
    bool m_bAllowRHIAccess = false;
#endif
    
    friend class FRDGBuilder;
};

/**
 * RDG Texture descriptor
 * Reference: UE5 FRDGTextureDesc
 */
struct FRDGTextureDesc
{
    uint32 width = 1;
    uint32 height = 1;
    uint32 depth = 1;
    uint32 arraySize = 1;
    uint32 mipLevels = 1;
    uint32 sampleCount = 1;
    
    RHI::EPixelFormat format = RHI::EPixelFormat::R8G8B8A8_UNORM;
    RHI::EResourceUsage usage = RHI::EResourceUsage::None;
    ERDGTextureFlags flags = ERDGTextureFlags::None;
    
    FString debugName;
    
    // Clear value for render targets
    float32 clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float32 clearDepth = 1.0f;
    uint8 clearStencil = 0;
    
    FRDGTextureDesc() = default;
    
    // Helper: Create 2D texture descriptor
    static FRDGTextureDesc create2D(
        uint32 inWidth,
        uint32 inHeight,
        RHI::EPixelFormat inFormat,
        RHI::EResourceUsage inUsage = RHI::EResourceUsage::ShaderResource,
        ERDGTextureFlags inFlags = ERDGTextureFlags::None)
    {
        FRDGTextureDesc desc;
        desc.width = inWidth;
        desc.height = inHeight;
        desc.depth = 1;
        desc.arraySize = 1;
        desc.mipLevels = 1;
        desc.format = inFormat;
        desc.usage = inUsage;
        desc.flags = inFlags;
        return desc;
    }
    
    // Helper: Create depth texture descriptor
    static FRDGTextureDesc createDepth(
        uint32 inWidth,
        uint32 inHeight,
        RHI::EPixelFormat inFormat = RHI::EPixelFormat::D32_FLOAT,
        ERDGTextureFlags inFlags = ERDGTextureFlags::None)
    {
        FRDGTextureDesc desc;
        desc.width = inWidth;
        desc.height = inHeight;
        desc.depth = 1;
        desc.arraySize = 1;
        desc.mipLevels = 1;
        desc.format = inFormat;
        desc.usage = RHI::EResourceUsage::DepthStencil;
        desc.flags = inFlags;
        return desc;
    }
    
    // Check if this is a depth-stencil texture
    bool isDepthStencil() const
    {
        return format == RHI::EPixelFormat::D32_FLOAT ||
               format == RHI::EPixelFormat::D24_UNORM_S8_UINT ||
               format == RHI::EPixelFormat::D32_FLOAT_S8_UINT ||
               format == RHI::EPixelFormat::D16_UNORM;
    }
    
    // Check if this is a render target
    bool isRenderTarget() const
    {
        return RHI::hasResourceUsage(usage, RHI::EResourceUsage::RenderTarget);
    }
};

/**
 * RDG Texture resource
 * Reference: UE5 FRDGTexture
 */
class FRDGTexture : public FRDGResource
{
public:
    FRDGTexture(const FString& inName, const FRDGTextureDesc& inDesc)
        : FRDGResource(inName)
        , m_desc(inDesc)
    {
        // Initialize subresource states
        uint32 subresourceCount = inDesc.mipLevels * inDesc.arraySize;
        m_subresourceStates.SetNum(subresourceCount);
    }
    
    virtual ~FRDGTexture() = default;
    
    // Get texture descriptor
    const FRDGTextureDesc& getDesc() const { return m_desc; }
    
    // Get underlying RHI texture
    RHI::IRHITexture* getRHITexture() const
    {
        return static_cast<RHI::IRHITexture*>(getRHI());
    }
    
    // Get subresource state
    FRDGSubresourceState& getSubresourceState(uint32 mipLevel, uint32 arraySlice)
    {
        uint32 index = mipLevel + arraySlice * m_desc.mipLevels;
        return m_subresourceStates[index];
    }
    
    const FRDGSubresourceState& getSubresourceState(uint32 mipLevel, uint32 arraySlice) const
    {
        uint32 index = mipLevel + arraySlice * m_desc.mipLevels;
        return m_subresourceStates[index];
    }
    
    // Get all subresource states (for whole resource transitions)
    TArray<FRDGSubresourceState>& getSubresourceStates() { return m_subresourceStates; }
    const TArray<FRDGSubresourceState>& getSubresourceStates() const { return m_subresourceStates; }
    
private:
    FRDGTextureDesc m_desc;
    TArray<FRDGSubresourceState> m_subresourceStates;
    
    friend class FRDGBuilder;
};

/**
 * RDG Buffer descriptor
 * Reference: UE5 FRDGBufferDesc
 */
struct FRDGBufferDesc
{
    uint32 size = 0;
    uint32 stride = 0;
    
    RHI::EResourceUsage usage = RHI::EResourceUsage::None;
    ERDGBufferFlags flags = ERDGBufferFlags::None;
    
    FString debugName;
    
    FRDGBufferDesc() = default;
    
    // Helper: Create vertex buffer descriptor
    static FRDGBufferDesc createVertexBuffer(
        uint32 inSize,
        uint32 inStride,
        ERDGBufferFlags inFlags = ERDGBufferFlags::None)
    {
        FRDGBufferDesc desc;
        desc.size = inSize;
        desc.stride = inStride;
        desc.usage = RHI::EResourceUsage::VertexBuffer;
        desc.flags = inFlags;
        return desc;
    }
    
    // Helper: Create index buffer descriptor
    static FRDGBufferDesc createIndexBuffer(
        uint32 inSize,
        bool b32Bit = true,
        ERDGBufferFlags inFlags = ERDGBufferFlags::None)
    {
        FRDGBufferDesc desc;
        desc.size = inSize;
        desc.stride = b32Bit ? 4 : 2;
        desc.usage = RHI::EResourceUsage::IndexBuffer;
        desc.flags = inFlags;
        return desc;
    }
    
    // Helper: Create uniform buffer descriptor
    static FRDGBufferDesc createUniformBuffer(
        uint32 inSize,
        ERDGBufferFlags inFlags = ERDGBufferFlags::None)
    {
        FRDGBufferDesc desc;
        desc.size = inSize;
        desc.stride = inSize;
        desc.usage = RHI::EResourceUsage::UniformBuffer;
        desc.flags = inFlags;
        return desc;
    }
};

/**
 * RDG Buffer resource
 * Reference: UE5 FRDGBuffer
 */
class FRDGBuffer : public FRDGResource
{
public:
    FRDGBuffer(const FString& inName, const FRDGBufferDesc& inDesc)
        : FRDGResource(inName)
        , m_desc(inDesc)
    {}
    
    virtual ~FRDGBuffer() = default;
    
    // Get buffer descriptor
    const FRDGBufferDesc& getDesc() const { return m_desc; }
    
    // Get underlying RHI buffer
    RHI::IRHIBuffer* getRHIBuffer() const
    {
        return static_cast<RHI::IRHIBuffer*>(getRHI());
    }
    
    // Get buffer state
    FRDGSubresourceState& getState() { return m_state; }
    const FRDGSubresourceState& getState() const { return m_state; }
    
private:
    FRDGBufferDesc m_desc;
    FRDGSubresourceState m_state;
    
    friend class FRDGBuilder;
};

}} // namespace MonsterRender::RDG

// Hash function for FRDGPassHandle to use with TMap
namespace MonsterEngine {
    inline uint32 GetTypeHash(const MonsterRender::RDG::FRDGPassHandle& handle)
    {
        return GetTypeHash(handle.index);
    }
}
