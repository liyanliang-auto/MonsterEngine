// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SceneView.h
 * @brief Scene view and view info classes
 * 
 * This file defines FSceneView and FViewInfo classes for managing
 * view-specific rendering data.
 * Reference: UE5 SceneView.h, SceneRendering.h
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/BitArray.h"
#include "Containers/StaticArray.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Plane.h"
#include "Renderer/SceneTypes.h"
#include "RHI/RHIDefinitions.h"

using MonsterRender::RHI::Viewport;

// Forward declarations for RHI types (in MonsterRender::RHI namespace)
namespace MonsterRender { namespace RHI {
    class IRHITexture;
    class IRHIBuffer;
}}

namespace MonsterEngine
{

// Renderer namespace for low-level rendering classes
namespace Renderer
{

// Bring RHI types into scope
using MonsterRender::RHI::IRHITexture;
using MonsterRender::RHI::IRHIBuffer;

// Forward declarations
class FScene;
class FSceneRenderer;
class FSceneViewState;
class FPrimitiveSceneInfo;

// ============================================================================
// FViewMatrices - View Transformation Matrices
// ============================================================================

/**
 * @struct FViewMatrices
 * @brief Contains all view-related transformation matrices
 * 
 * Stores view, projection, and combined matrices for rendering.
 * Reference: UE5 FViewMatrices
 */
struct FViewMatrices
{
    /** View matrix (world to view space) */
    Math::FMatrix ViewMatrix;
    
    /** Projection matrix (view to clip space) */
    Math::FMatrix ProjectionMatrix;
    
    /** Combined view-projection matrix */
    Math::FMatrix ViewProjectionMatrix;
    
    /** Inverse view matrix */
    Math::FMatrix InvViewMatrix;
    
    /** Inverse projection matrix */
    Math::FMatrix InvProjectionMatrix;
    
    /** Inverse view-projection matrix */
    Math::FMatrix InvViewProjectionMatrix;
    
    /** Pre-view translation (for large world coordinates) */
    Math::FVector PreViewTranslation;
    
    /** View origin in world space */
    Math::FVector ViewOrigin;
    
    /** View forward direction */
    Math::FVector ViewForward;
    
    /** View right direction */
    Math::FVector ViewRight;
    
    /** View up direction */
    Math::FVector ViewUp;
    
    /** Near clip plane distance */
    float NearClipPlane;
    
    /** Far clip plane distance */
    float FarClipPlane;
    
    /** Field of view in degrees */
    float FOV;
    
    /** Aspect ratio (width / height) */
    float AspectRatio;
    
    /** Default constructor */
    FViewMatrices()
        : ViewMatrix(Math::FMatrix::Identity)
        , ProjectionMatrix(Math::FMatrix::Identity)
        , ViewProjectionMatrix(Math::FMatrix::Identity)
        , InvViewMatrix(Math::FMatrix::Identity)
        , InvProjectionMatrix(Math::FMatrix::Identity)
        , InvViewProjectionMatrix(Math::FMatrix::Identity)
        , PreViewTranslation(Math::FVector::ZeroVector)
        , ViewOrigin(Math::FVector::ZeroVector)
        , ViewForward(Math::FVector::ForwardVector)
        , ViewRight(Math::FVector::RightVector)
        , ViewUp(Math::FVector::UpVector)
        , NearClipPlane(1.0f)
        , FarClipPlane(10000.0f)
        , FOV(90.0f)
        , AspectRatio(16.0f / 9.0f)
    {
    }
    
    /**
     * Get the view origin
     */
    const Math::FVector& GetViewOrigin() const { return ViewOrigin; }
    
    /**
     * Get the view forward direction
     */
    const Math::FVector& GetViewForward() const { return ViewForward; }
    
    /**
     * Update derived matrices after view/projection changes
     */
    void UpdateDerivedMatrices()
    {
        ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;
        InvViewMatrix = ViewMatrix.Inverse();
        InvProjectionMatrix = ProjectionMatrix.Inverse();
        InvViewProjectionMatrix = ViewProjectionMatrix.Inverse();
    }
    
    /**
     * Set view matrix from position and orientation
     */
    void SetViewMatrix(const Math::FVector& Position, const Math::FVector& Forward, 
                       const Math::FVector& Right, const Math::FVector& Up)
    {
        ViewOrigin = Position;
        ViewForward = Forward;
        ViewRight = Right;
        ViewUp = Up;
        
        // Build view matrix (world to view) - manual construction
        // View matrix transforms from world space to view space
        ViewMatrix = Math::FMatrix::Identity;
        ViewMatrix.M[0][0] = Right.X;   ViewMatrix.M[0][1] = Up.X;   ViewMatrix.M[0][2] = Forward.X;
        ViewMatrix.M[1][0] = Right.Y;   ViewMatrix.M[1][1] = Up.Y;   ViewMatrix.M[1][2] = Forward.Y;
        ViewMatrix.M[2][0] = Right.Z;   ViewMatrix.M[2][1] = Up.Z;   ViewMatrix.M[2][2] = Forward.Z;
        ViewMatrix.M[3][0] = -(Right | Position);
        ViewMatrix.M[3][1] = -(Up | Position);
        ViewMatrix.M[3][2] = -(Forward | Position);
        UpdateDerivedMatrices();
    }
    
    /**
     * Set perspective projection matrix
     */
    void SetPerspectiveProjection(float InFOV, float InAspectRatio, float InNear, float InFar)
    {
        FOV = InFOV;
        AspectRatio = InAspectRatio;
        NearClipPlane = InNear;
        FarClipPlane = InFar;
        
        // Build perspective projection matrix manually
        float FOVRad = FOV * 3.14159265359f / 180.0f;
        float TanHalfFOV = std::tan(FOVRad * 0.5f);
        
        ProjectionMatrix = Math::FMatrix(EForceInit::ForceInit);
        ProjectionMatrix.M[0][0] = 1.0f / (AspectRatio * TanHalfFOV);
        ProjectionMatrix.M[1][1] = 1.0f / TanHalfFOV;
        ProjectionMatrix.M[2][2] = FarClipPlane / (FarClipPlane - NearClipPlane);
        ProjectionMatrix.M[2][3] = 1.0f;
        ProjectionMatrix.M[3][2] = -(FarClipPlane * NearClipPlane) / (FarClipPlane - NearClipPlane);
        UpdateDerivedMatrices();
    }
};

// ============================================================================
// FSceneViewFamily - View Family Information
// ============================================================================

/**
 * @struct FSceneViewFamily
 * @brief A family of views to be rendered together
 * 
 * Contains shared settings for a group of views (e.g., stereo rendering).
 * Reference: UE5 FSceneViewFamily
 */
struct FSceneViewFamily
{
    /** Scene being rendered */
    FScene* Scene;
    
    /** Render target texture */
    IRHITexture* RenderTarget;
    
    /** Frame number */
    uint32 FrameNumber;
    
    /** Real time in seconds */
    float RealTimeSeconds;
    
    /** World time in seconds */
    float WorldTimeSeconds;
    
    /** Delta time since last frame */
    float DeltaWorldTimeSeconds;
    
    /** Gamma correction value */
    float GammaCorrection;
    
    /** Whether to render in wireframe mode */
    uint32 bWireframe : 1;
    
    /** Whether to use deferred shading */
    uint32 bDeferredShading : 1;
    
    /** Whether to render shadows */
    uint32 bRenderShadows : 1;
    
    /** Whether to render fog */
    uint32 bRenderFog : 1;
    
    /** Whether to render post-processing */
    uint32 bRenderPostProcessing : 1;
    
    /** Whether to render motion blur */
    uint32 bRenderMotionBlur : 1;
    
    /** Whether to render bloom */
    uint32 bRenderBloom : 1;
    
    /** Whether to render ambient occlusion */
    uint32 bRenderAmbientOcclusion : 1;
    
    /** Default constructor */
    FSceneViewFamily()
        : Scene(nullptr)
        , RenderTarget(nullptr)
        , FrameNumber(0)
        , RealTimeSeconds(0.0f)
        , WorldTimeSeconds(0.0f)
        , DeltaWorldTimeSeconds(0.0f)
        , GammaCorrection(2.2f)
        , bWireframe(false)
        , bDeferredShading(true)
        , bRenderShadows(true)
        , bRenderFog(true)
        , bRenderPostProcessing(true)
        , bRenderMotionBlur(true)
        , bRenderBloom(true)
        , bRenderAmbientOcclusion(true)
    {
    }
};

// ============================================================================
// FSceneView - Base Scene View
// ============================================================================

/**
 * @class FSceneView
 * @brief Base class for scene view information
 * 
 * Contains basic view parameters like matrices, viewport, and visibility settings.
 * Reference: UE5 FSceneView
 */
class FSceneView
{
public:
    /** View family this view belongs to */
    const FSceneViewFamily* Family;
    
    /** View matrices */
    FViewMatrices ViewMatrices;
    
    /** Previous frame view matrices (for motion blur) */
    FViewMatrices PrevViewMatrices;
    
    /** View rectangle in pixels */
    Viewport ViewRect;
    
    /** Unscaled view rectangle */
    Viewport UnscaledViewRect;
    
    /** View frustum for culling */
    FConvexVolume ViewFrustum;
    
    /** View index (for multi-view rendering) */
    int32 ViewIndex;
    
    /** Desired field of view */
    float DesiredFOV;
    
    /** LOD distance factor */
    float LODDistanceFactor;
    
    /** Whether this is the primary view */
    uint32 bIsPrimaryView : 1;
    
    /** Whether to render scene primitives */
    uint32 bRenderScenePrimitives : 1;
    
    /** Whether to use LOD */
    uint32 bUseLOD : 1;
    
    /** Whether distance-based fade transitions are disabled */
    uint32 bDisableDistanceBasedFadeTransitions : 1;
    
public:
    /** Default constructor */
    FSceneView()
        : Family(nullptr)
        , ViewIndex(0)
        , DesiredFOV(90.0f)
        , LODDistanceFactor(1.0f)
        , bIsPrimaryView(true)
        , bRenderScenePrimitives(true)
        , bUseLOD(true)
        , bDisableDistanceBasedFadeTransitions(false)
    {
    }
    
    /** Virtual destructor */
    virtual ~FSceneView() = default;
    
    /**
     * Initialize the view frustum from view matrices
     */
    void InitViewFrustum();
    
    /**
     * Get the view origin
     */
    const Math::FVector& GetViewOrigin() const { return ViewMatrices.ViewOrigin; }
    
    /**
     * Get the view direction
     */
    const Math::FVector& GetViewDirection() const { return ViewMatrices.ViewForward; }
    
    /**
     * Project a world position to screen space
     */
    bool ProjectWorldToScreen(const Math::FVector& WorldPosition, Math::FVector2D& OutScreenPosition) const;
    
    /**
     * Deproject a screen position to world space
     */
    bool DeprojectScreenToWorld(const Math::FVector2D& ScreenPosition, 
                                Math::FVector& OutWorldOrigin, Math::FVector& OutWorldDirection) const;
};

// ============================================================================
// FSceneViewState - Persistent View State
// ============================================================================

/**
 * @class FSceneViewState
 * @brief Persistent state associated with a view across frames
 * 
 * Stores temporal data like occlusion history, LOD states, and TAA history.
 * Reference: UE5 FSceneViewState
 */
class FSceneViewState
{
public:
    /** Unique ID for this view state */
    uint32 UniqueID;
    
    /** Previous frame number */
    uint32 PrevFrameNumber;
    
    /** Occlusion frame counter */
    uint32 OcclusionFrameCounter;
    
    /** Whether occlusion is disabled */
    bool bOcclusionDisabled;
    
    /** Temporal AA sample index */
    int32 TemporalAASampleIndex;
    
    /** Frame index for temporal effects */
    uint32 FrameIndex;
    
    // TODO: Add occlusion query pools, HZB data, TAA history textures
    
public:
    /** Constructor */
    FSceneViewState()
        : UniqueID(0)
        , PrevFrameNumber(0)
        , OcclusionFrameCounter(0)
        , bOcclusionDisabled(false)
        , TemporalAASampleIndex(0)
        , FrameIndex(0)
    {
        static uint32 NextUniqueID = 1;
        UniqueID = NextUniqueID++;
    }
    
    /** Destructor */
    ~FSceneViewState() = default;
    
    /**
     * Get the unique ID
     */
    uint32 GetUniqueID() const { return UniqueID; }
    
    /**
     * Called at the start of a new frame
     */
    void OnStartFrame(uint32 CurrentFrameNumber)
    {
        PrevFrameNumber = CurrentFrameNumber;
        FrameIndex++;
    }
};

// ============================================================================
// FViewInfo - Extended View Information for Rendering
// ============================================================================

/**
 * @class FViewInfo
 * @brief Extended view class with renderer-specific data
 * 
 * Contains all data needed by the renderer for a single view,
 * including visibility maps, mesh elements, and render passes.
 * Reference: UE5 FViewInfo
 */
class FViewInfo : public FSceneView
{
public:
    /** Persistent view state */
    FSceneViewState* State;
    
    /** Primitive visibility bitmap */
    FSceneBitArray PrimitiveVisibilityMap;
    
    /** Static mesh visibility bitmap */
    FSceneBitArray StaticMeshVisibilityMap;
    
    /** Potentially fading primitive bitmap */
    FSceneBitArray PotentiallyFadingPrimitiveMap;
    
    /** Ray tracing visibility bitmap */
    FSceneBitArray PrimitiveRayTracingVisibilityMap;
    
    /** Primitive view relevance array */
    TArray<FPrimitiveViewRelevance> PrimitiveViewRelevanceMap;
    
    /** Dynamic mesh elements collected for this view */
    TArray<FMeshBatchAndRelevance> DynamicMeshElements;
    
    /** Number of visible dynamic mesh elements */
    int32 NumVisibleDynamicMeshElements;
    
    /** Visible light information */
    TArray<FVisibleLightInfo> VisibleLightInfos;
    
    /** Number of visible dynamic primitives */
    int32 NumVisibleDynamicPrimitives;
    
    /** Number of visible static mesh elements */
    int32 NumVisibleStaticMeshElements;
    
    /** Whether this view has any translucent primitives */
    uint32 bHasTranslucentPrimitives : 1;
    
    /** Whether this view has any distortion primitives */
    uint32 bHasDistortionPrimitives : 1;
    
    /** Whether this view has any custom depth primitives */
    uint32 bHasCustomDepthPrimitives : 1;
    
    /** Whether visibility has been computed */
    uint32 bVisibilityComputed : 1;
    
    // TODO: Add parallel mesh draw command passes
    // TStaticArray<FParallelMeshDrawCommandPass, EMeshPass::Num> ParallelMeshDrawCommandPasses;
    
public:
    /** Default constructor */
    FViewInfo()
        : FSceneView()
        , State(nullptr)
        , NumVisibleDynamicMeshElements(0)
        , NumVisibleDynamicPrimitives(0)
        , NumVisibleStaticMeshElements(0)
        , bHasTranslucentPrimitives(false)
        , bHasDistortionPrimitives(false)
        , bHasCustomDepthPrimitives(false)
        , bVisibilityComputed(false)
    {
    }
    
    /** Destructor */
    virtual ~FViewInfo() = default;
    
    /**
     * Initialize visibility arrays for the given number of primitives
     * @param NumPrimitives Number of primitives in the scene
     */
    void InitVisibilityArrays(int32 NumPrimitives)
    {
        PrimitiveVisibilityMap.Init(false, NumPrimitives);
        StaticMeshVisibilityMap.Init(false, NumPrimitives);
        PotentiallyFadingPrimitiveMap.Init(false, NumPrimitives);
        PrimitiveRayTracingVisibilityMap.Init(false, NumPrimitives);
        PrimitiveViewRelevanceMap.SetNum(NumPrimitives);
    }
    
    /**
     * Reset visibility data for a new frame
     */
    void ResetVisibility()
    {
        PrimitiveVisibilityMap.Init(false, PrimitiveVisibilityMap.Num());
        StaticMeshVisibilityMap.Init(false, StaticMeshVisibilityMap.Num());
        PotentiallyFadingPrimitiveMap.Init(false, PotentiallyFadingPrimitiveMap.Num());
        PrimitiveRayTracingVisibilityMap.Init(false, PrimitiveRayTracingVisibilityMap.Num());
        
        DynamicMeshElements.Empty();
        NumVisibleDynamicMeshElements = 0;
        NumVisibleDynamicPrimitives = 0;
        NumVisibleStaticMeshElements = 0;
        
        bHasTranslucentPrimitives = false;
        bHasDistortionPrimitives = false;
        bHasCustomDepthPrimitives = false;
        bVisibilityComputed = false;
    }
    
    /**
     * Check if a primitive is visible
     * @param PrimitiveIndex Index of the primitive
     * @return True if the primitive is visible
     */
    bool IsPrimitiveVisible(int32 PrimitiveIndex) const
    {
        if (PrimitiveIndex >= 0 && PrimitiveIndex < PrimitiveVisibilityMap.Num())
        {
            return PrimitiveVisibilityMap[PrimitiveIndex];
        }
        return false;
    }
    
    /**
     * Set primitive visibility
     * @param PrimitiveIndex Index of the primitive
     * @param bVisible Whether the primitive is visible
     */
    void SetPrimitiveVisibility(int32 PrimitiveIndex, bool bVisible)
    {
        if (PrimitiveIndex >= 0 && PrimitiveIndex < PrimitiveVisibilityMap.Num())
        {
            PrimitiveVisibilityMap.SetBit(PrimitiveIndex, bVisible);
        }
    }
    
    /**
     * Check if distance culling should be performed
     * @param DistanceSquared Squared distance from view to primitive
     * @param MinDrawDistance Minimum draw distance
     * @param MaxDrawDistance Maximum draw distance
     * @return True if the primitive should be culled
     */
    bool IsDistanceCulled(float DistanceSquared, float MinDrawDistance, float MaxDrawDistance) const;
    
    /**
     * Add a dynamic mesh element
     * @param MeshBatch The mesh batch to add
     * @param ViewRelevance View relevance flags
     * @param PrimitiveSceneInfo The primitive scene info
     */
    void AddDynamicMeshElement(const FMeshBatch& MeshBatch, 
                               const FPrimitiveViewRelevance& ViewRelevance,
                               FPrimitiveSceneInfo* PrimitiveSceneInfo)
    {
        int32 NewIndex = DynamicMeshElements.Add(FMeshBatchAndRelevance());
        FMeshBatchAndRelevance& Element = DynamicMeshElements[NewIndex];
        Element.MeshBatch = MeshBatch;
        Element.ViewRelevance = ViewRelevance;
        Element.PrimitiveSceneInfo = PrimitiveSceneInfo;
        NumVisibleDynamicMeshElements++;
        
        if (ViewRelevance.HasTranslucency())
        {
            bHasTranslucentPrimitives = true;
        }
        if (ViewRelevance.bDistortionRelevance)
        {
            bHasDistortionPrimitives = true;
        }
        if (ViewRelevance.bRenderCustomDepth)
        {
            bHasCustomDepthPrimitives = true;
        }
    }
};

// ============================================================================
// FViewCommands - Per-View Mesh Commands
// ============================================================================

/**
 * @struct FViewCommands
 * @brief Container for per-view mesh draw commands
 * 
 * Stores mesh commands organized by pass type.
 * Reference: UE5 FViewCommands
 */
struct FViewCommands
{
    /** Mesh commands per pass */
    TStaticArray<TArray<FMeshBatch>, EMeshPass::Num> MeshCommands;
    
    /** Number of dynamic mesh command build requests per pass */
    TStaticArray<int32, EMeshPass::Num> NumDynamicMeshCommandBuildRequestElements;
    
    /** Default constructor */
    FViewCommands()
    {
        for (int32 i = 0; i < EMeshPass::Num; ++i)
        {
            NumDynamicMeshCommandBuildRequestElements[i] = 0;
        }
    }
    
    /**
     * Reset all commands
     */
    void Reset()
    {
        for (int32 i = 0; i < EMeshPass::Num; ++i)
        {
            MeshCommands[i].Empty();
            NumDynamicMeshCommandBuildRequestElements[i] = 0;
        }
    }
    
    /**
     * Add a mesh command to a specific pass
     */
    void AddMeshCommand(EMeshPass::Type PassType, const FMeshBatch& MeshBatch)
    {
        MeshCommands[PassType].Add(MeshBatch);
    }
    
    /**
     * Get mesh commands for a specific pass
     */
    const TArray<FMeshBatch>& GetMeshCommands(EMeshPass::Type PassType) const
    {
        return MeshCommands[PassType];
    }
    
    /**
     * Get the number of mesh commands for a specific pass
     */
    int32 GetNumMeshCommands(EMeshPass::Type PassType) const
    {
        return MeshCommands[PassType].Num();
    }
};

} // namespace Renderer
} // namespace MonsterEngine


