#pragma once

#include "Core/CoreMinimal.h"
#include "RDG/RDGFwd.h"

/**
 * Core definitions for Render Dependency Graph (RDG) system
 * Reference: UE5 RenderGraphDefinitions.h and RHIAccess.h
 */

namespace MonsterRender {
namespace RDG {

/**
 * Resource access state enumeration (similar to UE5's ERHIAccess)
 * Represents the state a resource is in during GPU execution
 * 
 * These states are used for automatic resource barrier insertion
 * Reference: UE5 RHI/Public/RHIAccess.h
 */
enum class ERHIAccess : uint32
{
    // Unknown state - requires full cache flush
    Unknown = 0,
    
    // Read-only states (can be combined)
    CPURead                 = 1 << 0,   // CPU read access
    Present                 = 1 << 1,   // Swapchain presentation
    IndirectArgs            = 1 << 2,   // Indirect draw/dispatch arguments
    VertexOrIndexBuffer     = 1 << 3,   // Vertex or index buffer binding
    SRVCompute              = 1 << 4,   // Shader resource view (compute)
    SRVGraphics             = 1 << 5,   // Shader resource view (graphics)
    CopySrc                 = 1 << 6,   // Copy source
    ResolveSrc              = 1 << 7,   // MSAA resolve source
    DSVRead                 = 1 << 8,   // Depth-stencil read (depth test)
    
    // Read-write states
    UAVCompute              = 1 << 9,   // Unordered access view (compute)
    UAVGraphics             = 1 << 10,  // Unordered access view (graphics)
    RTV                     = 1 << 11,  // Render target view
    CopyDest                = 1 << 12,  // Copy destination
    ResolveDst              = 1 << 13,  // MSAA resolve destination
    DSVWrite                = 1 << 14,  // Depth-stencil write
    
    // Special states
    Discard                 = 1 << 15,  // Transient resource discard
    
    // Aliases for common usage
    None = Unknown,
    Last = Discard,
    Mask = (Last << 1) - 1,
    
    // Useful masks
    SRVMask = SRVCompute | SRVGraphics,
    UAVMask = UAVCompute | UAVGraphics,
    
    // Read-only exclusive mask (cannot combine with write states)
    ReadOnlyExclusiveMask = CPURead | Present | IndirectArgs | VertexOrIndexBuffer | 
                            SRVGraphics | SRVCompute | CopySrc | ResolveSrc,
    
    // Read-only mask (may combine with some write states)
    ReadOnlyMask = ReadOnlyExclusiveMask | DSVRead,
    
    // Readable mask (includes UAV which is read-write)
    ReadableMask = ReadOnlyMask | UAVMask,
    
    // Write-only exclusive mask (cannot combine with read states)
    WriteOnlyExclusiveMask = RTV | CopyDest | ResolveDst,
    
    // Write-only mask (may combine with some read states)
    WriteOnlyMask = WriteOnlyExclusiveMask | DSVWrite,
    
    // Writable mask (includes UAV which is read-write)
    WritableMask = WriteOnlyMask | UAVMask
};

// Enable bitwise operations for ERHIAccess
inline constexpr ERHIAccess operator|(ERHIAccess lhs, ERHIAccess rhs)
{
    return static_cast<ERHIAccess>(static_cast<uint32>(lhs) | static_cast<uint32>(rhs));
}

inline constexpr ERHIAccess operator&(ERHIAccess lhs, ERHIAccess rhs)
{
    return static_cast<ERHIAccess>(static_cast<uint32>(lhs) & static_cast<uint32>(rhs));
}

inline constexpr ERHIAccess operator~(ERHIAccess rhs)
{
    return static_cast<ERHIAccess>(~static_cast<uint32>(rhs));
}

inline constexpr ERHIAccess& operator|=(ERHIAccess& lhs, ERHIAccess rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

inline constexpr ERHIAccess& operator&=(ERHIAccess& lhs, ERHIAccess rhs)
{
    lhs = lhs & rhs;
    return lhs;
}

// Helper functions for access state validation
inline constexpr bool enumHasAnyFlags(ERHIAccess flags, ERHIAccess contains)
{
    return (flags & contains) != ERHIAccess::None;
}

inline constexpr bool enumHasAllFlags(ERHIAccess flags, ERHIAccess contains)
{
    return (flags & contains) == contains;
}

inline constexpr bool isReadOnlyExclusiveAccess(ERHIAccess access)
{
    return enumHasAnyFlags(access, ERHIAccess::ReadOnlyExclusiveMask) && 
           !enumHasAnyFlags(access, ~ERHIAccess::ReadOnlyExclusiveMask);
}

inline constexpr bool isReadOnlyAccess(ERHIAccess access)
{
    return enumHasAnyFlags(access, ERHIAccess::ReadOnlyMask) && 
           !enumHasAnyFlags(access, ~ERHIAccess::ReadOnlyMask);
}

inline constexpr bool isWriteOnlyAccess(ERHIAccess access)
{
    return enumHasAnyFlags(access, ERHIAccess::WriteOnlyMask) && 
           !enumHasAnyFlags(access, ~ERHIAccess::WriteOnlyMask);
}

inline constexpr bool isWritableAccess(ERHIAccess access)
{
    return enumHasAnyFlags(access, ERHIAccess::WritableMask);
}

inline constexpr bool isReadableAccess(ERHIAccess access)
{
    return enumHasAnyFlags(access, ERHIAccess::ReadableMask);
}

inline constexpr bool isInvalidAccess(ERHIAccess access)
{
    return (enumHasAnyFlags(access, ERHIAccess::ReadOnlyExclusiveMask) && 
            enumHasAnyFlags(access, ERHIAccess::WritableMask)) ||
           (enumHasAnyFlags(access, ERHIAccess::WriteOnlyExclusiveMask) && 
            enumHasAnyFlags(access, ERHIAccess::ReadableMask));
}

inline constexpr bool isValidAccess(ERHIAccess access)
{
    return !isInvalidAccess(access);
}

/**
 * Pass flags to control pass behavior
 * Reference: UE5 RenderGraphDefinitions.h ERDGPassFlags
 */
enum class ERDGPassFlags : uint16
{
    None = 0,
    
    // Pass uses rasterization on the graphics pipeline
    Raster = 1 << 0,
    
    // Pass uses compute on the graphics pipeline
    Compute = 1 << 1,
    
    // Pass uses compute on the async compute pipeline
    AsyncCompute = 1 << 2,
    
    // Pass uses copy commands
    Copy = 1 << 3,
    
    // Pass (and its producers) will never be culled
    NeverCull = 1 << 4,
    
    // Skip automatic render pass begin/end
    SkipRenderPass = 1 << 5,
    
    // Never merge this pass with other passes
    NeverMerge = 1 << 6,
    
    // Pass will never run in parallel
    NeverParallel = 1 << 7,
    
    // Pass uses copy commands but writes to staging resource
    Readback = Copy | NeverCull
};

// Enable bitwise operations for ERDGPassFlags
inline constexpr ERDGPassFlags operator|(ERDGPassFlags lhs, ERDGPassFlags rhs)
{
    return static_cast<ERDGPassFlags>(static_cast<uint16>(lhs) | static_cast<uint16>(rhs));
}

inline constexpr ERDGPassFlags operator&(ERDGPassFlags lhs, ERDGPassFlags rhs)
{
    return static_cast<ERDGPassFlags>(static_cast<uint16>(lhs) & static_cast<uint16>(rhs));
}

inline constexpr bool enumHasAnyFlags(ERDGPassFlags flags, ERDGPassFlags contains)
{
    return (flags & contains) != ERDGPassFlags::None;
}

/**
 * Texture flags to control texture behavior
 * Reference: UE5 RenderGraphDefinitions.h ERDGTextureFlags
 */
enum class ERDGTextureFlags : uint8
{
    None = 0,
    
    // Texture survives across multiple frames (multi-GPU)
    MultiFrame = 1 << 0,
    
    // Skip automatic resource tracking (user manages transitions)
    SkipTracking = 1 << 1,
    
    // Force immediate first barrier (no split barrier)
    ForceImmediateFirstBarrier = 1 << 2,
    
    // Maintain compression (don't decompress metadata)
    MaintainCompression = 1 << 3
};

// Enable bitwise operations for ERDGTextureFlags
inline constexpr ERDGTextureFlags operator|(ERDGTextureFlags lhs, ERDGTextureFlags rhs)
{
    return static_cast<ERDGTextureFlags>(static_cast<uint8>(lhs) | static_cast<uint8>(rhs));
}

inline constexpr ERDGTextureFlags operator&(ERDGTextureFlags lhs, ERDGTextureFlags rhs)
{
    return static_cast<ERDGTextureFlags>(static_cast<uint8>(lhs) | static_cast<uint8>(rhs));
}

inline constexpr bool enumHasAnyFlags(ERDGTextureFlags flags, ERDGTextureFlags contains)
{
    return (flags & contains) != ERDGTextureFlags::None;
}

/**
 * Buffer flags to control buffer behavior
 * Reference: UE5 RenderGraphDefinitions.h ERDGBufferFlags
 */
enum class ERDGBufferFlags : uint8
{
    None = 0,
    
    // Buffer survives across multiple frames (multi-GPU)
    MultiFrame = 1 << 0,
    
    // Skip automatic resource tracking (user manages transitions)
    SkipTracking = 1 << 1,
    
    // Force immediate first barrier (no split barrier)
    ForceImmediateFirstBarrier = 1 << 2
};

// Enable bitwise operations for ERDGBufferFlags
inline constexpr ERDGBufferFlags operator|(ERDGBufferFlags lhs, ERDGBufferFlags rhs)
{
    return static_cast<ERDGBufferFlags>(static_cast<uint8>(lhs) | static_cast<uint8>(rhs));
}

inline constexpr ERDGBufferFlags operator&(ERDGBufferFlags lhs, ERDGBufferFlags rhs)
{
    return static_cast<ERDGBufferFlags>(static_cast<uint8>(lhs) | static_cast<uint8>(rhs));
}

inline constexpr bool enumHasAnyFlags(ERDGBufferFlags flags, ERDGBufferFlags contains)
{
    return (flags & contains) != ERDGBufferFlags::None;
}

/**
 * Debug configuration
 */
#if !defined(RDG_ENABLE_DEBUG)
    #if defined(_DEBUG) || defined(DEBUG)
        #define RDG_ENABLE_DEBUG 1
    #else
        #define RDG_ENABLE_DEBUG 0
    #endif
#endif

#if RDG_ENABLE_DEBUG
    #define IF_RDG_ENABLE_DEBUG(Op) Op
#else
    #define IF_RDG_ENABLE_DEBUG(Op)
#endif

}} // namespace MonsterRender::RDG
