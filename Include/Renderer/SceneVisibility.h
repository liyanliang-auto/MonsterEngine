// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SceneVisibility.h
 * @brief Scene visibility determination system
 * 
 * This file defines the visibility culling system including:
 * - Frustum culling
 * - Occlusion culling (HZB and Hardware queries)
 * - Distance culling
 * 
 * Reference: UE5 SceneVisibility.cpp, SceneOcclusion.h
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/BitArray.h"
#include "Math/Vector.h"
#include "Math/Plane.h"
#include "Math/Box.h"
#include "Math/Sphere.h"
#include "Renderer/SceneTypes.h"

// Forward declarations for RHI types
namespace MonsterRender { namespace RHI {
    class IRHICommandList;
    class IRHITexture;
    class IRHIBuffer;
    class IRHIDevice;
}}

namespace MonsterEngine
{

// Use RHI types from MonsterRender namespace
using MonsterRender::RHI::IRHICommandList;
using MonsterRender::RHI::IRHITexture;
using MonsterRender::RHI::IRHIBuffer;
using MonsterRender::RHI::IRHIDevice;

// Renderer namespace for low-level rendering classes
namespace Renderer
{

// Forward declarations
class FScene;
class FViewInfo;
class FPrimitiveSceneInfo;

// ============================================================================
// FPrimitiveCullingFlags - Culling Configuration Flags
// ============================================================================

/**
 * @struct FPrimitiveCullingFlags
 * @brief Configuration flags for primitive culling
 * 
 * Controls which culling tests are performed and how.
 * Reference: UE5 FPrimitiveCullingFlags
 */
struct FPrimitiveCullingFlags
{
    /** Whether to perform visibility culling at all */
    uint32 bShouldVisibilityCull : 1;
    
    /** Whether to use custom culling queries */
    uint32 bUseCustomCulling : 1;
    
    /** Whether to also use sphere test before box test */
    uint32 bAlsoUseSphereTest : 1;
    
    /** Whether to use SIMD-optimized 8-plane intersection */
    uint32 bUseFastIntersect : 1;
    
    /** Whether to use visibility octree for acceleration */
    uint32 bUseVisibilityOctree : 1;
    
    /** Whether Nanite meshes are always visible */
    uint32 bNaniteAlwaysVisible : 1;
    
    /** Whether there are hidden primitives to check */
    uint32 bHasHiddenPrimitives : 1;
    
    /** Whether there are show-only primitives */
    uint32 bHasShowOnlyPrimitives : 1;
    
    /** Default constructor */
    FPrimitiveCullingFlags()
        : bShouldVisibilityCull(true)
        , bUseCustomCulling(false)
        , bAlsoUseSphereTest(false)
        , bUseFastIntersect(true)
        , bUseVisibilityOctree(false)
        , bNaniteAlwaysVisible(false)
        , bHasHiddenPrimitives(false)
        , bHasShowOnlyPrimitives(false)
    {
    }
};

// ============================================================================
// FFrustumCuller - Frustum Culling System
// ============================================================================

/**
 * @class FFrustumCuller
 * @brief Performs frustum culling for scene primitives
 * 
 * Tests primitive bounds against the view frustum to determine visibility.
 * Supports SIMD-optimized intersection tests and parallel processing.
 * Reference: UE5 PrimitiveCull, IntersectBox8Plane
 */
class FFrustumCuller
{
public:
    /** Constructor */
    FFrustumCuller();
    
    /** Destructor */
    ~FFrustumCuller();
    
    /**
     * Perform frustum culling for a view
     * @param Scene The scene containing primitives
     * @param View The view to cull against
     * @param Flags Culling configuration flags
     * @return Number of primitives culled
     */
    int32 CullPrimitives(const FScene* Scene, FViewInfo& View, const FPrimitiveCullingFlags& Flags);
    
    /**
     * Test if a primitive is visible in the frustum
     * @param View The view
     * @param Bounds The primitive bounds
     * @param Flags Culling flags
     * @return True if the primitive is visible
     */
    bool IsPrimitiveVisible(const FViewInfo& View, const FPrimitiveBounds& Bounds, 
                           const FPrimitiveCullingFlags& Flags) const;
    
    /**
     * Test box-frustum intersection using 8 permuted planes (SIMD optimized)
     * @param Origin Box center
     * @param Extent Box half-extents
     * @param PermutedPlanes Array of 8 permuted planes
     * @return True if the box intersects the frustum
     */
    static bool IntersectBox8Plane(const Math::FVector& Origin, const Math::FVector& Extent,
                                   const Math::FPlane* PermutedPlanes);
    
    /**
     * Test box-frustum intersection (standard method)
     * @param Origin Box center
     * @param Extent Box half-extents
     * @param Planes Frustum planes
     * @param NumPlanes Number of planes
     * @return True if the box intersects the frustum
     */
    static bool IntersectBoxPlanes(const Math::FVector& Origin, const Math::FVector& Extent,
                                   const Math::FPlane* Planes, int32 NumPlanes);
    
    /**
     * Test sphere-frustum intersection
     * @param Center Sphere center
     * @param Radius Sphere radius
     * @param Planes Frustum planes
     * @param NumPlanes Number of planes
     * @return True if the sphere intersects the frustum
     */
    static bool IntersectSpherePlanes(const Math::FVector& Center, float Radius,
                                      const Math::FPlane* Planes, int32 NumPlanes);
    
private:
    /**
     * Perform culling for a range of primitives (used for parallel processing)
     * @param Scene The scene
     * @param View The view
     * @param Flags Culling flags
     * @param StartIndex Start primitive index
     * @param EndIndex End primitive index (exclusive)
     * @param OutNumCulled Output: number of primitives culled
     */
    void CullPrimitiveRange(const FScene* Scene, FViewInfo& View, 
                           const FPrimitiveCullingFlags& Flags,
                           int32 StartIndex, int32 EndIndex, int32& OutNumCulled);
    
    /** Number of primitives to process per parallel task */
    static constexpr int32 PrimitivesPerTask = 128 * 32; // 128 words * 32 bits
};

// ============================================================================
// FDistanceCuller - Distance-Based Culling
// ============================================================================

/**
 * @class FDistanceCuller
 * @brief Performs distance-based culling for scene primitives
 * 
 * Culls primitives based on their distance from the view origin,
 * respecting min/max draw distances and LOD settings.
 * Reference: UE5 IsDistanceCulled
 */
class FDistanceCuller
{
public:
    /** Constructor */
    FDistanceCuller();
    
    /** Destructor */
    ~FDistanceCuller();
    
    /**
     * Perform distance culling for a view
     * @param Scene The scene containing primitives
     * @param View The view to cull against
     * @return Number of primitives culled
     */
    int32 CullPrimitives(const FScene* Scene, FViewInfo& View);
    
    /**
     * Check if a primitive should be distance culled
     * @param DistanceSquared Squared distance from view to primitive
     * @param MinDrawDistance Minimum draw distance
     * @param MaxDrawDistance Maximum draw distance
     * @param MaxDrawDistanceScale Scale factor for max draw distance
     * @param bOutMayBeFading Output: whether the primitive may be fading
     * @param bOutFadingIn Output: whether the primitive is fading in
     * @return True if the primitive should be culled
     */
    static bool IsDistanceCulled(float DistanceSquared, float MinDrawDistance, float MaxDrawDistance,
                                float MaxDrawDistanceScale, bool& bOutMayBeFading, bool& bOutFadingIn);
    
    /**
     * Get the view distance scale from scalability settings
     */
    static float GetViewDistanceScale();
    
    /**
     * Set the view distance scale
     */
    static void SetViewDistanceScale(float Scale);
    
private:
    /** Global view distance scale factor */
    static float ViewDistanceScale;
    
    /** Fade radius for distance culling transitions */
    static float FadeRadius;
    
    /** Whether LOD fading is disabled */
    static bool bDisableLODFade;
};

// ============================================================================
// FOcclusionQueryPool - Occlusion Query Object Pool
// ============================================================================

/**
 * @class FOcclusionQueryPool
 * @brief Pool of reusable occlusion query objects
 * 
 * Manages a pool of GPU occlusion queries for efficient reuse.
 * Reference: UE5 FOcclusionQueryPool
 */
class FOcclusionQueryPool
{
public:
    /** Constructor */
    FOcclusionQueryPool();
    
    /** Destructor */
    ~FOcclusionQueryPool();
    
    /**
     * Initialize the pool
     * @param Device The RHI device
     * @param InitialSize Initial number of queries to allocate
     */
    void Initialize(IRHIDevice* Device, int32 InitialSize = 256);
    
    /**
     * Shutdown and release all queries
     */
    void Shutdown();
    
    /**
     * Allocate a query from the pool
     * @return Index of the allocated query
     */
    int32 AllocateQuery();
    
    /**
     * Release a query back to the pool
     * @param QueryIndex Index of the query to release
     */
    void ReleaseQuery(int32 QueryIndex);
    
    /**
     * Get the number of allocated queries
     */
    int32 GetNumAllocatedQueries() const { return NumAllocatedQueries; }
    
    /**
     * Get the total pool size
     */
    int32 GetPoolSize() const { return PoolSize; }
    
private:
    /** RHI device */
    IRHIDevice* Device;
    
    /** Pool of query objects (platform-specific handles) */
    TArray<void*> QueryPool;
    
    /** Free list of available query indices */
    TArray<int32> FreeList;
    
    /** Number of currently allocated queries */
    int32 NumAllocatedQueries;
    
    /** Total pool size */
    int32 PoolSize;
};

// ============================================================================
// FPrimitiveOcclusionHistory - Per-Primitive Occlusion History
// ============================================================================

/**
 * @struct FPrimitiveOcclusionHistory
 * @brief Tracks occlusion query history for a primitive
 * 
 * Stores the results of previous occlusion queries to make
 * visibility decisions without waiting for GPU results.
 * Reference: UE5 FPrimitiveOcclusionHistory
 */
struct FPrimitiveOcclusionHistory
{
    /** Last frame the primitive was visible */
    uint32 LastVisibleFrame;
    
    /** Last frame an occlusion query was submitted */
    uint32 LastQuerySubmitFrame;
    
    /** Number of consecutive frames the primitive was occluded */
    uint32 ConsecutiveOccludedFrames;
    
    /** Whether the primitive was visible in the last query */
    uint8 bWasOccluded : 1;
    
    /** Whether a query is currently pending */
    uint8 bQueryPending : 1;
    
    /** Whether to use grouped queries */
    uint8 bGroupedQuery : 1;
    
    /** Default constructor */
    FPrimitiveOcclusionHistory()
        : LastVisibleFrame(0)
        , LastQuerySubmitFrame(0)
        , ConsecutiveOccludedFrames(0)
        , bWasOccluded(false)
        , bQueryPending(false)
        , bGroupedQuery(false)
    {
    }
    
    /**
     * Update history with new query result
     * @param CurrentFrame Current frame number
     * @param bIsOccluded Whether the primitive is occluded
     */
    void UpdateHistory(uint32 CurrentFrame, bool bIsOccluded)
    {
        if (bIsOccluded)
        {
            ConsecutiveOccludedFrames++;
        }
        else
        {
            ConsecutiveOccludedFrames = 0;
            LastVisibleFrame = CurrentFrame;
        }
        bWasOccluded = bIsOccluded;
        bQueryPending = false;
    }
};

// ============================================================================
// FOcclusionCuller - Occlusion Culling System
// ============================================================================

/**
 * @class FOcclusionCuller
 * @brief Performs occlusion culling using GPU queries or HZB
 * 
 * Supports two occlusion culling methods:
 * 1. Hardware Occlusion Queries - GPU-based visibility tests
 * 2. Hierarchical Z-Buffer (HZB) - Software-based depth testing
 * 
 * Reference: UE5 FOcclusionQueryBatcher, FHZBOcclusionTester
 */
class FOcclusionCuller
{
public:
    /** Occlusion culling method */
    enum class EOcclusionMethod : uint8
    {
        /** No occlusion culling */
        None,
        
        /** Hardware occlusion queries */
        HardwareQueries,
        
        /** Hierarchical Z-Buffer */
        HZB,
        
        /** Both methods combined */
        Combined
    };
    
    /** Constructor */
    FOcclusionCuller();
    
    /** Destructor */
    ~FOcclusionCuller();
    
    /**
     * Initialize the occlusion culler
     * @param Device The RHI device
     * @param Method The occlusion method to use
     */
    void Initialize(IRHIDevice* Device, EOcclusionMethod Method = EOcclusionMethod::HardwareQueries);
    
    /**
     * Shutdown and cleanup
     */
    void Shutdown();
    
    /**
     * Begin occlusion culling for a frame
     * @param RHICmdList The command list
     * @param View The view
     */
    void BeginOcclusionCulling(IRHICommandList& RHICmdList, FViewInfo& View);
    
    /**
     * Perform occlusion culling
     * @param Scene The scene
     * @param View The view
     * @param RHICmdList The command list
     * @return Number of primitives culled
     */
    int32 CullPrimitives(const FScene* Scene, FViewInfo& View, IRHICommandList& RHICmdList);
    
    /**
     * End occlusion culling for a frame
     * @param RHICmdList The command list
     */
    void EndOcclusionCulling(IRHICommandList& RHICmdList);
    
    /**
     * Submit an occlusion query for a primitive
     * @param RHICmdList The command list
     * @param PrimitiveIndex Index of the primitive
     * @param Bounds Primitive bounds
     */
    void SubmitOcclusionQuery(IRHICommandList& RHICmdList, int32 PrimitiveIndex, 
                             const FBoxSphereBounds& Bounds);
    
    /**
     * Read back occlusion query results
     * @param RHICmdList The command list
     */
    void ReadbackOcclusionResults(IRHICommandList& RHICmdList);
    
    /**
     * Check if a primitive is occluded based on history
     * @param PrimitiveIndex Index of the primitive
     * @param CurrentFrame Current frame number
     * @return True if the primitive should be considered occluded
     */
    bool IsPrimitiveOccluded(int32 PrimitiveIndex, uint32 CurrentFrame) const;
    
    /**
     * Get the occlusion method
     */
    EOcclusionMethod GetOcclusionMethod() const { return OcclusionMethod; }
    
    /**
     * Set the occlusion method
     */
    void SetOcclusionMethod(EOcclusionMethod Method) { OcclusionMethod = Method; }
    
    /**
     * Check if occlusion culling is enabled
     */
    bool IsEnabled() const { return OcclusionMethod != EOcclusionMethod::None; }
    
    // ========================================================================
    // HZB Methods
    // ========================================================================
    
    /**
     * Build the Hierarchical Z-Buffer
     * @param RHICmdList The command list
     * @param DepthTexture The scene depth texture
     */
    void BuildHZB(IRHICommandList& RHICmdList, IRHITexture* DepthTexture);
    
    /**
     * Test a bounds against the HZB
     * @param Bounds The bounds to test
     * @param ViewProjectionMatrix The view-projection matrix
     * @return True if the bounds are visible
     */
    bool TestHZB(const FBoxSphereBounds& Bounds, const Math::FMatrix& ViewProjectionMatrix) const;
    
private:
    /** RHI device */
    IRHIDevice* Device;
    
    /** Occlusion query pool */
    FOcclusionQueryPool QueryPool;
    
    /** Per-primitive occlusion history */
    TArray<FPrimitiveOcclusionHistory> OcclusionHistory;
    
    /** Pending query indices */
    TArray<int32> PendingQueries;
    
    /** HZB texture */
    IRHITexture* HZBTexture;
    
    /** HZB mip levels */
    int32 HZBMipLevels;
    
    /** Current occlusion method */
    EOcclusionMethod OcclusionMethod;
    
    /** Current frame number */
    uint32 CurrentFrame;
    
    /** Number of frames to wait before considering a primitive occluded */
    static constexpr uint32 OcclusionFrameThreshold = 2;
    
    /** Number of consecutive occluded frames before skipping queries */
    static constexpr uint32 SkipQueryThreshold = 5;
};

// ============================================================================
// FSceneVisibility - Main Visibility System
// ============================================================================

/**
 * @class FSceneVisibility
 * @brief Main visibility determination system
 * 
 * Coordinates all visibility culling methods (frustum, occlusion, distance)
 * to determine which primitives are visible in each view.
 * Reference: UE5 ComputeViewVisibility
 */
class FSceneVisibility
{
public:
    /** Constructor */
    FSceneVisibility();
    
    /** Destructor */
    ~FSceneVisibility();
    
    /**
     * Initialize the visibility system
     * @param Device The RHI device
     */
    void Initialize(IRHIDevice* Device);
    
    /**
     * Shutdown the visibility system
     */
    void Shutdown();
    
    /**
     * Compute visibility for a view
     * @param Scene The scene
     * @param View The view
     * @param RHICmdList The command list
     */
    void ComputeViewVisibility(const FScene* Scene, FViewInfo& View, IRHICommandList& RHICmdList);
    
    /**
     * Get the frustum culler
     */
    FFrustumCuller& GetFrustumCuller() { return FrustumCuller; }
    
    /**
     * Get the distance culler
     */
    FDistanceCuller& GetDistanceCuller() { return DistanceCuller; }
    
    /**
     * Get the occlusion culler
     */
    FOcclusionCuller& GetOcclusionCuller() { return OcclusionCuller; }
    
    /**
     * Set whether frustum culling is enabled
     */
    void SetFrustumCullingEnabled(bool bEnabled) { bFrustumCullingEnabled = bEnabled; }
    
    /**
     * Set whether distance culling is enabled
     */
    void SetDistanceCullingEnabled(bool bEnabled) { bDistanceCullingEnabled = bEnabled; }
    
    /**
     * Set whether occlusion culling is enabled
     */
    void SetOcclusionCullingEnabled(bool bEnabled) { bOcclusionCullingEnabled = bEnabled; }
    
private:
    /** Frustum culler */
    FFrustumCuller FrustumCuller;
    
    /** Distance culler */
    FDistanceCuller DistanceCuller;
    
    /** Occlusion culler */
    FOcclusionCuller OcclusionCuller;
    
    /** Whether frustum culling is enabled */
    bool bFrustumCullingEnabled;
    
    /** Whether distance culling is enabled */
    bool bDistanceCullingEnabled;
    
    /** Whether occlusion culling is enabled */
    bool bOcclusionCullingEnabled;
};

} // namespace Renderer
} // namespace MonsterEngine
