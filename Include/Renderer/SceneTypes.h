// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SceneTypes.h
 * @brief Core scene rendering types and definitions
 * 
 * This file defines fundamental types used throughout the scene rendering system.
 * Reference: UE5 SceneTypes.h, SceneRendering.h
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/BitArray.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Box.h"
#include "Math/Sphere.h"
#include "Math/Plane.h"

namespace MonsterEngine
{

// Forward declarations
class FScene;
class FSceneRenderer;
class FViewInfo;
class FPrimitiveSceneInfo;
class FPrimitiveSceneProxy;
class FLightSceneInfo;
class FLightSceneProxy;

namespace RHI
{
    class IRHIBuffer;
    class IRHIPipelineState;
}

// ============================================================================
// Type Aliases
// ============================================================================

/** Bit array type for scene visibility */
using FSceneBitArray = TBitArray<>;

/** View mask for multi-view rendering */
using FPrimitiveViewMasks = TArray<uint8>;

// ============================================================================
// EMeshPass - Mesh Rendering Pass Types
// ============================================================================

/**
 * @enum EMeshPass
 * @brief Enumeration of all mesh rendering passes
 * 
 * Each pass represents a specific stage in the rendering pipeline.
 * Reference: UE5 EMeshPass::Type
 */
namespace EMeshPass
{
    enum Type : uint8
    {
        /** Depth-only prepass for early-Z optimization */
        DepthPass = 0,
        
        /** Base pass - GBuffer fill for deferred rendering */
        BasePass,
        
        /** Sky rendering pass */
        SkyPass,
        
        /** Single layer water rendering */
        SingleLayerWaterPass,
        
        /** Cascaded shadow map depth pass */
        CSMShadowDepth,
        
        /** Distortion pass for refractive materials */
        Distortion,
        
        /** Velocity buffer pass for motion blur */
        Velocity,
        
        /** Translucent velocity pass */
        TranslucentVelocity,
        
        /** Standard translucency pass */
        TranslucencyStandard,
        
        /** After DOF translucency */
        TranslucencyAfterDOF,
        
        /** After motion blur translucency */
        TranslucencyAfterMotionBlur,
        
        /** All translucency (combined) */
        TranslucencyAll,
        
        /** Light function pass */
        LightmapDensity,
        
        /** Debug view mode pass */
        DebugViewMode,
        
        /** Custom depth pass */
        CustomDepth,
        
        /** Mobile base pass */
        MobileBasePassCSM,
        
        /** Virtual texture feedback */
        VirtualTextureFeedback,
        
        /** Editor selection pass */
        EditorSelection,
        
        /** Hit proxy rendering */
        HitProxy,
        
        /** Hit proxy opaque only */
        HitProxyOpaqueOnly,
        
        /** Total number of mesh passes */
        Num,
        
        /** Maximum number of passes (for static arrays) */
        Max = Num
    };
    
    /**
     * Get the name of a mesh pass
     * @param PassType The pass type
     * @return String name of the pass
     */
    inline const char* GetMeshPassName(Type PassType)
    {
        switch (PassType)
        {
            case DepthPass: return "DepthPass";
            case BasePass: return "BasePass";
            case SkyPass: return "SkyPass";
            case SingleLayerWaterPass: return "SingleLayerWaterPass";
            case CSMShadowDepth: return "CSMShadowDepth";
            case Distortion: return "Distortion";
            case Velocity: return "Velocity";
            case TranslucentVelocity: return "TranslucentVelocity";
            case TranslucencyStandard: return "TranslucencyStandard";
            case TranslucencyAfterDOF: return "TranslucencyAfterDOF";
            case TranslucencyAfterMotionBlur: return "TranslucencyAfterMotionBlur";
            case TranslucencyAll: return "TranslucencyAll";
            case LightmapDensity: return "LightmapDensity";
            case DebugViewMode: return "DebugViewMode";
            case CustomDepth: return "CustomDepth";
            case MobileBasePassCSM: return "MobileBasePassCSM";
            case VirtualTextureFeedback: return "VirtualTextureFeedback";
            case EditorSelection: return "EditorSelection";
            case HitProxy: return "HitProxy";
            case HitProxyOpaqueOnly: return "HitProxyOpaqueOnly";
            default: return "Unknown";
        }
    }
}

// ============================================================================
// FBoxSphereBounds - Combined Box and Sphere Bounds
// ============================================================================

/**
 * @struct FBoxSphereBounds
 * @brief Combined axis-aligned box and bounding sphere
 * 
 * Stores both box extent and sphere radius for efficient culling.
 * Reference: UE5 FBoxSphereBounds
 */
struct FBoxSphereBounds
{
    /** Center of the bounds (both box and sphere) */
    Math::FVector Origin;
    
    /** Half-extents of the bounding box */
    Math::FVector BoxExtent;
    
    /** Radius of the bounding sphere */
    float SphereRadius;
    
    /** Default constructor */
    FBoxSphereBounds()
        : Origin(Math::FVector::ZeroVector)
        , BoxExtent(Math::FVector::ZeroVector)
        , SphereRadius(0.0f)
    {
    }
    
    /**
     * Constructor from origin, extent, and radius
     */
    FBoxSphereBounds(const Math::FVector& InOrigin, const Math::FVector& InBoxExtent, float InSphereRadius)
        : Origin(InOrigin)
        , BoxExtent(InBoxExtent)
        , SphereRadius(InSphereRadius)
    {
    }
    
    /**
     * Constructor from a box
     */
    explicit FBoxSphereBounds(const Math::FBox& Box)
    {
        Origin = (Box.Min + Box.Max) * 0.5f;
        BoxExtent = (Box.Max - Box.Min) * 0.5f;
        SphereRadius = BoxExtent.Size();
    }
    
    /**
     * Constructor from a sphere
     */
    explicit FBoxSphereBounds(const Math::FSphere& Sphere)
    {
        Origin = Sphere.Center;
        BoxExtent = Math::FVector(Sphere.W, Sphere.W, Sphere.W);
        SphereRadius = Sphere.W;
    }
    
    /**
     * Get the bounding box
     */
    Math::FBox GetBox() const
    {
        return Math::FBox(Origin - BoxExtent, Origin + BoxExtent);
    }
    
    /**
     * Get the bounding sphere
     */
    Math::FSphere GetSphere() const
    {
        return Math::FSphere(Origin, SphereRadius);
    }
    
    /**
     * Transform bounds by a matrix
     */
    FBoxSphereBounds TransformBy(const Math::FMatrix& M) const;
    
    /**
     * Combine with another bounds
     */
    FBoxSphereBounds operator+(const FBoxSphereBounds& Other) const
    {
        Math::FBox CombinedBox = GetBox() + Other.GetBox();
        return FBoxSphereBounds(CombinedBox);
    }
    
    /**
     * Check if bounds are valid
     */
    bool IsValid() const
    {
        return SphereRadius > 0.0f;
    }
};

// ============================================================================
// FPrimitiveBounds - Primitive Culling Bounds
// ============================================================================

/**
 * @struct FPrimitiveBounds
 * @brief Bounds information for primitive culling
 * 
 * Contains all bounds data needed for visibility culling.
 * Reference: UE5 FPrimitiveBounds
 */
struct FPrimitiveBounds
{
    /** Combined box and sphere bounds */
    FBoxSphereBounds BoxSphereBounds;
    
    /** Minimum draw distance (near cull) */
    float MinDrawDistance;
    
    /** Maximum cull distance (far cull) */
    float MaxCullDistance;
    
    /** Default constructor */
    FPrimitiveBounds()
        : MinDrawDistance(0.0f)
        , MaxCullDistance(FLT_MAX)
    {
    }
};

// ============================================================================
// FPrimitiveViewRelevance - View Relevance Flags
// ============================================================================

/**
 * @struct FPrimitiveViewRelevance
 * @brief Flags indicating how a primitive is relevant to a view
 * 
 * Determines which render passes a primitive should be included in.
 * Reference: UE5 FPrimitiveViewRelevance
 */
struct FPrimitiveViewRelevance
{
    /** Has opaque or masked relevance (BasePass) */
    uint32 bOpaqueRelevance : 1;
    
    /** Has masked relevance */
    uint32 bMaskedRelevance : 1;
    
    /** Has translucent relevance */
    uint32 bTranslucentRelevance : 1;
    
    /** Has distortion relevance */
    uint32 bDistortionRelevance : 1;
    
    /** Has velocity relevance */
    uint32 bVelocityRelevance : 1;
    
    /** Has shadow relevance */
    uint32 bShadowRelevance : 1;
    
    /** Has normal translucency relevance */
    uint32 bNormalTranslucencyRelevance : 1;
    
    /** Has separate translucency relevance */
    uint32 bSeparateTranslucencyRelevance : 1;
    
    /** Draws in depth pass */
    uint32 bDrawInDepthPass : 1;
    
    /** Draws in base pass */
    uint32 bDrawInBasePass : 1;
    
    /** Uses world position offset */
    uint32 bUsesWorldPositionOffset : 1;
    
    /** Uses displacement */
    uint32 bUsesDisplacement : 1;
    
    /** Has dynamic mesh element */
    uint32 bHasDynamicMeshElement : 1;
    
    /** Has static mesh element */
    uint32 bHasStaticMeshElement : 1;
    
    /** Renders in main pass */
    uint32 bRenderInMainPass : 1;
    
    /** Renders in depth pass */
    uint32 bRenderInDepthPass : 1;
    
    /** Renders custom depth */
    uint32 bRenderCustomDepth : 1;
    
    /** Uses global distance field */
    uint32 bUsesGlobalDistanceField : 1;
    
    /** Uses light function atlas */
    uint32 bUsesLightFunctionAtlas : 1;
    
    /** Default constructor - initialize all flags to false */
    FPrimitiveViewRelevance()
    {
        // Zero-initialize all bit fields
        std::memset(this, 0, sizeof(FPrimitiveViewRelevance));
    }
    
    /**
     * Check if primitive has any opaque relevance
     */
    bool HasOpaqueOrMaskedRelevance() const
    {
        return bOpaqueRelevance || bMaskedRelevance;
    }
    
    /**
     * Check if primitive has any translucent relevance
     */
    bool HasTranslucency() const
    {
        return bTranslucentRelevance || bNormalTranslucencyRelevance || bSeparateTranslucencyRelevance;
    }
    
    /**
     * Check if primitive has any relevance at all
     */
    bool HasRelevance() const
    {
        return bOpaqueRelevance || bMaskedRelevance || bTranslucentRelevance || 
               bDistortionRelevance || bShadowRelevance;
    }
};

// ============================================================================
// FConvexVolume - Convex Volume for Frustum Culling
// ============================================================================

/**
 * @struct FConvexVolume
 * @brief Convex volume defined by planes (used for frustum culling)
 * 
 * Stores a set of planes that define a convex volume.
 * Reference: UE5 FConvexVolume
 */
struct FConvexVolume
{
    /** Planes defining the convex volume */
    TArray<Math::FPlane> Planes;
    
    /** Permuted planes for SIMD-optimized intersection tests */
    TArray<Math::FPlane> PermutedPlanes;
    
    /** Default constructor */
    FConvexVolume() = default;
    
    /**
     * Initialize from an array of planes
     */
    void Init(const TArray<Math::FPlane>& InPlanes)
    {
        Planes = InPlanes;
        BuildPermutedPlanes();
    }
    
    /**
     * Build permuted planes for SIMD optimization
     * Arranges planes in SOA format for vectorized intersection tests
     */
    void BuildPermutedPlanes();
    
    /**
     * Test if a point is inside the volume
     */
    bool IntersectPoint(const Math::FVector& Point) const;
    
    /**
     * Test if a sphere intersects the volume
     */
    bool IntersectSphere(const Math::FVector& Center, float Radius) const;
    
    /**
     * Test if a box intersects the volume
     */
    bool IntersectBox(const Math::FVector& Origin, const Math::FVector& Extent) const;
    
    /**
     * Test if bounds intersect the volume
     */
    bool IntersectBounds(const FBoxSphereBounds& Bounds) const
    {
        return IntersectSphere(Bounds.Origin, Bounds.SphereRadius) &&
               IntersectBox(Bounds.Origin, Bounds.BoxExtent);
    }
    
    /**
     * Get number of planes
     */
    int32 GetNumPlanes() const { return Planes.Num(); }
};

// ============================================================================
// EOcclusionFlags - Occlusion Query Flags
// ============================================================================

/**
 * @enum EOcclusionFlags
 * @brief Flags for occlusion query behavior
 */
namespace EOcclusionFlags
{
    enum Type : uint8
    {
        /** No occlusion flags */
        None = 0,
        
        /** Can be occluded by other objects */
        CanBeOccluded = 1 << 0,
        
        /** Has precomputed visibility data */
        HasPrecomputedVisibility = 1 << 1,
        
        /** Allow approximate occlusion */
        AllowApproximateOcclusion = 1 << 2,
        
        /** Has sub-primitive occlusion queries */
        HasSubprimitiveQueries = 1 << 3,
    };
}

// ============================================================================
// FVisibleLightInfo - Visible Light Information
// ============================================================================

/**
 * @struct FVisibleLightInfo
 * @brief Information about a visible light in the scene
 */
struct FVisibleLightInfo
{
    /** Index of the light in the scene */
    int32 LightIndex;
    
    /** Light scene info pointer */
    FLightSceneInfo* LightSceneInfo;
    
    /** Whether the light affects the view */
    bool bAffectsView;
    
    /** Default constructor */
    FVisibleLightInfo()
        : LightIndex(INDEX_NONE)
        , LightSceneInfo(nullptr)
        , bAffectsView(false)
    {
    }
};

// ============================================================================
// FMeshBatch - Mesh Batch for Rendering
// ============================================================================

/**
 * @struct FMeshBatch
 * @brief A batch of mesh elements to be rendered together
 * 
 * Contains all information needed to render a group of mesh elements.
 * Reference: UE5 FMeshBatch
 */
struct FMeshBatch
{
    /** Vertex buffer */
    RHI::IRHIBuffer* VertexBuffer;
    
    /** Index buffer (optional) */
    RHI::IRHIBuffer* IndexBuffer;
    
    /** Pipeline state */
    RHI::IRHIPipelineState* PipelineState;
    
    /** Number of vertices */
    uint32 NumVertices;
    
    /** Number of indices (0 if not indexed) */
    uint32 NumIndices;
    
    /** First vertex index */
    uint32 FirstVertex;
    
    /** First index */
    uint32 FirstIndex;
    
    /** Number of instances */
    uint32 NumInstances;
    
    /** Base vertex location for indexed draws */
    int32 BaseVertexLocation;
    
    /** Primitive type */
    uint8 PrimitiveType;
    
    /** Whether this batch uses 32-bit indices */
    uint8 bUse32BitIndices : 1;
    
    /** Whether this batch is selected (editor) */
    uint8 bSelected : 1;
    
    /** Whether this batch is hovered (editor) */
    uint8 bHovered : 1;
    
    /** Whether this batch casts shadows */
    uint8 bCastShadow : 1;
    
    /** Whether this batch receives decals */
    uint8 bReceivesDecals : 1;
    
    /** Default constructor */
    FMeshBatch()
        : VertexBuffer(nullptr)
        , IndexBuffer(nullptr)
        , PipelineState(nullptr)
        , NumVertices(0)
        , NumIndices(0)
        , FirstVertex(0)
        , FirstIndex(0)
        , NumInstances(1)
        , BaseVertexLocation(0)
        , PrimitiveType(0)
        , bUse32BitIndices(true)
        , bSelected(false)
        , bHovered(false)
        , bCastShadow(true)
        , bReceivesDecals(true)
    {
    }
    
    /**
     * Check if this is an indexed draw
     */
    bool IsIndexed() const { return IndexBuffer != nullptr && NumIndices > 0; }
    
    /**
     * Check if this batch is valid
     */
    bool IsValid() const { return VertexBuffer != nullptr && NumVertices > 0; }
};

/**
 * @struct FMeshBatchAndRelevance
 * @brief Mesh batch with its view relevance
 */
struct FMeshBatchAndRelevance
{
    /** The mesh batch */
    FMeshBatch MeshBatch;
    
    /** View relevance flags */
    FPrimitiveViewRelevance ViewRelevance;
    
    /** Primitive scene info */
    FPrimitiveSceneInfo* PrimitiveSceneInfo;
    
    /** Default constructor */
    FMeshBatchAndRelevance()
        : PrimitiveSceneInfo(nullptr)
    {
    }
};

} // namespace MonsterEngine
