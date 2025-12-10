// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SceneTypes.h
 * @brief Scene system type definitions and forward declarations
 * 
 * This file contains fundamental types, enums, and forward declarations
 * used throughout the scene management system. Following UE5's scene architecture.
 */

#include "Core/CoreTypes.h"
#include "Math/MathFwd.h"
#include "Math/Vector.h"
#include "Math/Box.h"
#include "Math/Sphere.h"
#include "Math/Matrix.h"
#include "Containers/Array.h"

namespace MonsterEngine
{

// ============================================================================
// Forward Declarations
// ============================================================================

class FScene;
class FSceneInterface;
class FPrimitiveSceneInfo;
class FPrimitiveSceneProxy;
class FLightSceneInfo;
class FLightSceneProxy;
class FLightPrimitiveInteraction;
class USceneComponent;
class UPrimitiveComponent;
class ULightComponent;
class UMeshComponent;

// ============================================================================
// Scene Component ID
// ============================================================================

/**
 * Unique identifier for primitive components in the scene
 * Used for fast lookup and comparison without pointer dereferencing
 */
struct FPrimitiveComponentId
{
    /** Unique ID value */
    uint32 PrimIDValue;

    /** Default constructor - invalid ID */
    FPrimitiveComponentId() : PrimIDValue(0) {}

    /** Constructor with explicit ID */
    explicit FPrimitiveComponentId(uint32 InValue) : PrimIDValue(InValue) {}

    /** Check if this ID is valid */
    FORCEINLINE bool IsValid() const { return PrimIDValue != 0; }

    /** Equality comparison */
    FORCEINLINE bool operator==(const FPrimitiveComponentId& Other) const
    {
        return PrimIDValue == Other.PrimIDValue;
    }

    /** Inequality comparison */
    FORCEINLINE bool operator!=(const FPrimitiveComponentId& Other) const
    {
        return PrimIDValue != Other.PrimIDValue;
    }

    /** Less than comparison for sorting */
    FORCEINLINE bool operator<(const FPrimitiveComponentId& Other) const
    {
        return PrimIDValue < Other.PrimIDValue;
    }

    /** Hash function for use in containers */
    friend uint32 GetTypeHash(const FPrimitiveComponentId& Id)
    {
        return Id.PrimIDValue;
    }
};

// ============================================================================
// Mobility Type
// ============================================================================

/**
 * Describes how a component can move during gameplay
 * Affects lighting, physics, and rendering optimizations
 */
enum class EComponentMobility : uint8
{
    /** Component cannot move - allows static lighting and other optimizations */
    Static,
    
    /** Component can move but lighting is baked - good for objects that rarely move */
    Stationary,
    
    /** Component can move freely - fully dynamic lighting */
    Movable
};

// ============================================================================
// Light Type
// ============================================================================

/**
 * Types of lights supported by the scene
 */
enum class ELightType : uint8
{
    /** Directional light - simulates distant light source like the sun */
    Directional,
    
    /** Point light - emits light in all directions from a single point */
    Point,
    
    /** Spot light - emits light in a cone shape */
    Spot,
    
    /** Rect light - emits light from a rectangular area */
    Rect,
    
    /** Sky light - captures and applies ambient lighting from the sky */
    Sky,
    
    /** Maximum value for iteration */
    MAX
};

// ============================================================================
// Primitive Flags
// ============================================================================

/**
 * Flags describing primitive rendering and behavior properties
 */
enum class EPrimitiveFlags : uint32
{
    None                    = 0,
    
    /** Primitive casts shadows */
    CastShadow              = 1 << 0,
    
    /** Primitive receives shadows */
    ReceiveShadow           = 1 << 1,
    
    /** Primitive is visible */
    Visible                 = 1 << 2,
    
    /** Primitive is hidden in game */
    HiddenInGame            = 1 << 3,
    
    /** Primitive affects dynamic indirect lighting */
    AffectDynamicIndirectLighting = 1 << 4,
    
    /** Primitive casts dynamic shadow */
    CastDynamicShadow       = 1 << 5,
    
    /** Primitive casts static shadow */
    CastStaticShadow        = 1 << 6,
    
    /** Primitive uses custom depth */
    RenderCustomDepth       = 1 << 7,
    
    /** Primitive is selectable in editor */
    Selectable              = 1 << 8,
    
    /** Primitive has per-instance custom data */
    HasPerInstanceCustomData = 1 << 9,
    
    /** Primitive should be rendered in main pass */
    RenderInMainPass        = 1 << 10,
    
    /** Primitive should be rendered in depth pass */
    RenderInDepthPass       = 1 << 11,
    
    /** Default flags for new primitives */
    Default = CastShadow | ReceiveShadow | Visible | RenderInMainPass | RenderInDepthPass
};

FORCEINLINE EPrimitiveFlags operator|(EPrimitiveFlags A, EPrimitiveFlags B)
{
    return static_cast<EPrimitiveFlags>(static_cast<uint32>(A) | static_cast<uint32>(B));
}

FORCEINLINE EPrimitiveFlags operator&(EPrimitiveFlags A, EPrimitiveFlags B)
{
    return static_cast<EPrimitiveFlags>(static_cast<uint32>(A) & static_cast<uint32>(B));
}

FORCEINLINE EPrimitiveFlags& operator|=(EPrimitiveFlags& A, EPrimitiveFlags B)
{
    A = A | B;
    return A;
}

FORCEINLINE EPrimitiveFlags& operator&=(EPrimitiveFlags& A, EPrimitiveFlags B)
{
    A = A & B;
    return A;
}

FORCEINLINE bool EnumHasAnyFlags(EPrimitiveFlags Flags, EPrimitiveFlags Contains)
{
    return (static_cast<uint32>(Flags) & static_cast<uint32>(Contains)) != 0;
}

// ============================================================================
// Bounding Volumes
// ============================================================================

/**
 * Combined box and sphere bounds for efficient culling
 * Similar to UE5's FBoxSphereBounds
 */
struct FBoxSphereBounds
{
    /** Center of the bounding box and sphere */
    FVector Origin;
    
    /** Half-extent of the bounding box */
    FVector BoxExtent;
    
    /** Radius of the bounding sphere */
    double SphereRadius;

    /** Default constructor - creates invalid bounds */
    FBoxSphereBounds()
        : Origin(FVector::ZeroVector)
        , BoxExtent(FVector::ZeroVector)
        , SphereRadius(0.0)
    {
    }

    /** Constructor from origin, extent, and radius */
    FBoxSphereBounds(const FVector& InOrigin, const FVector& InBoxExtent, double InSphereRadius)
        : Origin(InOrigin)
        , BoxExtent(InBoxExtent)
        , SphereRadius(InSphereRadius)
    {
    }

    /** Constructor from a box */
    explicit FBoxSphereBounds(const FBox& Box)
    {
        Box.GetCenterAndExtents(Origin, BoxExtent);
        SphereRadius = BoxExtent.Size();
    }

    /** Constructor from a sphere */
    explicit FBoxSphereBounds(const FSphere& Sphere)
        : Origin(Sphere.Center)
        , BoxExtent(FVector(Sphere.W))
        , SphereRadius(Sphere.W)
    {
    }

    /** Get the bounding box */
    FORCEINLINE FBox GetBox() const
    {
        return FBox(Origin - BoxExtent, Origin + BoxExtent);
    }

    /** Get the bounding sphere */
    FORCEINLINE FSphere GetSphere() const
    {
        return FSphere(Origin, SphereRadius);
    }

    /** Expand bounds to include a point */
    FBoxSphereBounds& operator+=(const FVector& Point)
    {
        FBox Box = GetBox();
        Box += Point;
        *this = FBoxSphereBounds(Box);
        return *this;
    }

    /** Expand bounds to include another bounds */
    FBoxSphereBounds operator+(const FBoxSphereBounds& Other) const
    {
        FBox Box = GetBox() + Other.GetBox();
        return FBoxSphereBounds(Box);
    }

    /** Transform bounds by a matrix */
    FBoxSphereBounds TransformBy(const FMatrix& M) const;

    /** Check if bounds intersect with a box */
    FORCEINLINE bool Intersect(const FBox& Box) const
    {
        return GetBox().Intersect(Box);
    }

    /** Check if bounds intersect with a sphere */
    bool IntersectSphere(const FVector& SphereCenter, double SphereRadiusSquared) const
    {
        double DistSquared = GetBox().ComputeSquaredDistanceToPoint(SphereCenter);
        return DistSquared <= SphereRadiusSquared;
    }

    /** Get the squared distance from a point to the bounds */
    FORCEINLINE double ComputeSquaredDistanceFromBoxToPoint(const FVector& Point) const
    {
        return GetBox().ComputeSquaredDistanceToPoint(Point);
    }
};

// ============================================================================
// Primitive Bounds
// ============================================================================

/**
 * Bounds information for a primitive in the scene
 */
struct FPrimitiveBounds
{
    /** World-space bounds */
    FBoxSphereBounds BoxSphereBounds;
    
    /** Minimum draw distance */
    float MinDrawDistance;
    
    /** Maximum draw distance (0 = infinite) */
    float MaxDrawDistance;

    FPrimitiveBounds()
        : MinDrawDistance(0.0f)
        , MaxDrawDistance(0.0f)
    {
    }
};

// ============================================================================
// Primitive Flags Compact
// ============================================================================

/**
 * Compact storage for primitive flags used in tight loops
 */
struct FPrimitiveFlagsCompact
{
    /** Packed flags */
    uint32 Flags;

    FPrimitiveFlagsCompact() : Flags(0) {}
    explicit FPrimitiveFlagsCompact(EPrimitiveFlags InFlags) 
        : Flags(static_cast<uint32>(InFlags)) {}

    FORCEINLINE bool IsVisible() const 
    { 
        return (Flags & static_cast<uint32>(EPrimitiveFlags::Visible)) != 0; 
    }
    
    FORCEINLINE bool CastsShadow() const 
    { 
        return (Flags & static_cast<uint32>(EPrimitiveFlags::CastShadow)) != 0; 
    }
    
    FORCEINLINE bool ReceivesShadow() const 
    { 
        return (Flags & static_cast<uint32>(EPrimitiveFlags::ReceiveShadow)) != 0; 
    }
};

// ============================================================================
// Occlusion Flags
// ============================================================================

/**
 * Flags that affect how primitives are occlusion culled
 */
namespace EOcclusionFlags
{
    enum Type : uint8
    {
        /** No flags */
        None = 0x0,
        
        /** Indicates the primitive can be occluded */
        CanBeOccluded = 0x1,
        
        /** Allow the primitive to be batched with others to determine occlusion */
        AllowApproximateOcclusion = 0x4,
        
        /** Indicates the primitive has a valid ID for precomputed visibility */
        HasPrecomputedVisibility = 0x8,
        
        /** Indicates the primitive has subprimitive queries */
        HasSubprimitiveQueries = 0x10,
    };
}

// ============================================================================
// Precomputed Visibility ID
// ============================================================================

/**
 * Precomputed primitive visibility ID
 */
struct FPrimitiveVisibilityId
{
    /** Index into the byte where precomputed occlusion data is stored */
    int32 ByteIndex;
    
    /** Mask of the bit where precomputed occlusion data is stored */
    uint8 BitMask;

    FPrimitiveVisibilityId() : ByteIndex(-1), BitMask(0) {}
};

// ============================================================================
// Attachment Group Info
// ============================================================================

/**
 * Information about a group of attached primitives
 * Used for hierarchical culling and transform updates
 */
struct FAttachmentGroupSceneInfo
{
    /** The parent primitive, which is the root of the attachment tree */
    FPrimitiveSceneInfo* ParentSceneInfo;
    
    /** The primitives in the attachment group */
    TArray<FPrimitiveSceneInfo*> Primitives;

    FAttachmentGroupSceneInfo() : ParentSceneInfo(nullptr) {}
};

// ============================================================================
// Scene View Relevance
// ============================================================================

/**
 * Describes which rendering passes a primitive is relevant to
 */
struct FPrimitiveViewRelevance
{
    /** Whether the primitive is drawn in the main opaque pass */
    uint8 bDrawRelevance : 1;
    
    /** Whether the primitive is drawn in the shadow pass */
    uint8 bShadowRelevance : 1;
    
    /** Whether the primitive is drawn in the dynamic path */
    uint8 bDynamicRelevance : 1;
    
    /** Whether the primitive is drawn in the static path */
    uint8 bStaticRelevance : 1;
    
    /** Whether the primitive renders to the depth buffer */
    uint8 bRenderInMainPass : 1;
    
    /** Whether the primitive uses custom depth */
    uint8 bRenderCustomDepth : 1;
    
    /** Whether the primitive has translucency */
    uint8 bHasTranslucency : 1;
    
    /** Whether the primitive has velocity */
    uint8 bHasVelocity : 1;

    FPrimitiveViewRelevance()
    {
        // Initialize all bits to 0
        *reinterpret_cast<uint8*>(this) = 0;
    }
};

// ============================================================================
// Depth Priority Group
// ============================================================================

/**
 * Scene depth priority groups for rendering order
 */
enum class ESceneDepthPriorityGroup : uint8
{
    /** World scene depth priority group */
    World,
    
    /** Foreground scene depth priority group */
    Foreground
};

// ============================================================================
// Static Constants
// ============================================================================

// INDEX_NONE is already defined in ContainerFwd.h

/** Maximum number of lighting channels */
constexpr int32 NUM_LIGHTING_CHANNELS = 3;

/** Maximum number of atmosphere lights */
constexpr int32 NUM_ATMOSPHERE_LIGHTS = 2;

} // namespace MonsterEngine
