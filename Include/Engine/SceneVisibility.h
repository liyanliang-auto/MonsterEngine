// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SceneVisibility.h
 * @brief Scene visibility and culling system, inspired by UE5's visibility system
 * 
 * This file defines the visibility determination system including:
 * - Frustum culling
 * - Distance culling
 * - Occlusion culling
 * - Visibility state management
 */

#include "Core/CoreMinimal.h"
#include "Math/MonsterMath.h"
#include "Engine/SceneTypes.h"
#include "Engine/ConvexVolume.h"
#include "Engine/SceneView.h"
#include "Containers/Array.h"
#include "Containers/BitArray.h"
#include "Containers/Map.h"

namespace MonsterEngine
{

// Forward declarations
class FScene;
class FSceneView;
class FPrimitiveSceneInfo;
class FPrimitiveSceneProxy;

// ============================================================================
// Visibility Constants
// ============================================================================

/** Minimum screen radius for lights to be visible */
constexpr float GMinScreenRadiusForLights = 0.03f;

/** Minimum screen radius for depth prepass */
constexpr float GMinScreenRadiusForDepthPrepass = 0.03f;

/** Minimum screen radius for CSM depth */
constexpr float GMinScreenRadiusForCSMDepth = 0.01f;

// ============================================================================
// Primitive Visibility State
// ============================================================================

/**
 * Visibility state for a primitive across frames
 * Used for temporal coherence in occlusion culling
 */
struct FPrimitiveVisibilityState
{
    /** Frame number when last visibility test was performed */
    uint32 LastVisibilityFrame = 0;

    /** Frame number when primitive was last visible */
    uint32 LastVisibleFrame = 0;

    /** Number of consecutive frames the primitive has been occluded */
    uint32 OccludedFrameCount = 0;

    /** Whether the primitive was visible in the last test */
    bool bWasVisible = true;

    /** Whether an occlusion query is pending */
    bool bOcclusionQueryPending = false;

    /** Resets the visibility state */
    void Reset()
    {
        LastVisibilityFrame = 0;
        LastVisibleFrame = 0;
        OccludedFrameCount = 0;
        bWasVisible = true;
        bOcclusionQueryPending = false;
    }
};

/**
 * Fading state for distance-based LOD transitions
 */
struct FPrimitiveFadingState
{
    /** Whether this fading state is valid */
    bool bValid = false;

    /** Whether the primitive is currently visible */
    bool bIsVisible = false;

    /** Frame number when fading state was last updated */
    uint32 FrameNumber = 0;

    /** End time for the fade transition */
    float EndTime = 0.0f;

    /** Scale and bias for fade time calculation */
    FVector2D FadeTimeScaleBias = FVector2D::ZeroVector;

    /** Resets the fading state */
    void Reset()
    {
        bValid = false;
        bIsVisible = false;
        FrameNumber = 0;
        EndTime = 0.0f;
        FadeTimeScaleBias = FVector2D::ZeroVector;
    }
};

// ============================================================================
// Culling Flags
// ============================================================================

/**
 * Flags controlling primitive culling behavior
 */
struct FPrimitiveCullingFlags
{
    /** Whether to perform visibility culling */
    bool bShouldVisibilityCull = true;

    /** Whether to use custom culling callback */
    bool bUseCustomCulling = false;

    /** Whether to also use sphere test before box test */
    bool bAlsoUseSphereTest = false;

    /** Whether to use fast 8-plane intersection test */
    bool bUseFastIntersect = true;

    /** Whether to use the visibility octree */
    bool bUseVisibilityOctree = false;

    /** Whether there are hidden primitives to check */
    bool bHasHiddenPrimitives = false;

    /** Whether there are show-only primitives */
    bool bHasShowOnlyPrimitives = false;
};

// ============================================================================
// Visibility Results
// ============================================================================

/**
 * Results of visibility determination for a view
 */
struct FViewVisibilityResult
{
    /** Bit array indicating which primitives are visible */
    TBitArray<> PrimitiveVisibilityMap;

    /** Bit array indicating which primitives are potentially fading */
    TBitArray<> PotentiallyFadingPrimitiveMap;

    /** Number of visible primitives */
    int32 NumVisiblePrimitives = 0;

    /** Number of primitives culled by frustum */
    int32 NumFrustumCulled = 0;

    /** Number of primitives culled by distance */
    int32 NumDistanceCulled = 0;

    /** Number of primitives culled by occlusion */
    int32 NumOcclusionCulled = 0;

    /** Initializes the result for a given number of primitives */
    void Init(int32 NumPrimitives)
    {
        PrimitiveVisibilityMap.Init(false, NumPrimitives);
        PotentiallyFadingPrimitiveMap.Init(false, NumPrimitives);
        NumVisiblePrimitives = 0;
        NumFrustumCulled = 0;
        NumDistanceCulled = 0;
        NumOcclusionCulled = 0;
    }

    /** Marks a primitive as visible */
    void SetVisible(int32 PrimitiveIndex)
    {
        if (!PrimitiveVisibilityMap[PrimitiveIndex])
        {
            PrimitiveVisibilityMap.SetBit(PrimitiveIndex, true);
            ++NumVisiblePrimitives;
        }
    }

    /** Marks a primitive as not visible */
    void SetNotVisible(int32 PrimitiveIndex)
    {
        if (PrimitiveVisibilityMap[PrimitiveIndex])
        {
            PrimitiveVisibilityMap.SetBit(PrimitiveIndex, false);
            --NumVisiblePrimitives;
        }
    }

    /** Checks if a primitive is visible */
    FORCEINLINE bool IsVisible(int32 PrimitiveIndex) const
    {
        return PrimitiveVisibilityMap[PrimitiveIndex];
    }
};

// ============================================================================
// Visibility Query Interface
// ============================================================================

/**
 * Interface for custom visibility queries
 */
class ICustomVisibilityQuery
{
public:
    virtual ~ICustomVisibilityQuery() = default;

    /**
     * Checks if a primitive is visible
     * @param VisibilityId - Visibility ID of the primitive
     * @param Bounds - Bounds of the primitive
     * @return true if the primitive should be considered visible
     */
    virtual bool IsVisible(int32 VisibilityId, const FBoxSphereBounds& Bounds) = 0;

    /**
     * Prepares the query for a new frame
     * @param View - The view being rendered
     */
    virtual void PrepareForFrame(const FSceneView& View) {}
};

// ============================================================================
// Frustum Culling
// ============================================================================

/**
 * Performs frustum culling on primitives
 */
class FFrustumCuller
{
public:
    /**
     * Constructor
     * @param InViewFrustum - The view frustum to cull against
     */
    explicit FFrustumCuller(const FViewFrustum& InViewFrustum)
        : ViewFrustum(InViewFrustum)
    {
    }

    /**
     * Tests if a bounding box is visible in the frustum
     * @param Origin - Center of the box
     * @param Extent - Half-extents of the box
     * @return true if the box is visible
     */
    FORCEINLINE bool IsVisible(const FVector& Origin, const FVector& Extent) const
    {
        return ViewFrustum.IntersectBox(Origin, Extent);
    }

    /**
     * Tests if a bounding sphere is visible in the frustum
     * @param Origin - Center of the sphere
     * @param Radius - Radius of the sphere
     * @return true if the sphere is visible
     */
    FORCEINLINE bool IsVisible(const FVector& Origin, float Radius) const
    {
        return ViewFrustum.IntersectSphere(Origin, Radius);
    }

    /**
     * Tests if box-sphere bounds are visible in the frustum
     * @param Bounds - The bounds to test
     * @return true if the bounds are visible
     */
    bool IsVisible(const FBoxSphereBounds& Bounds) const
    {
        // First do a quick sphere test
        if (!ViewFrustum.IntersectSphere(Bounds.Origin, Bounds.SphereRadius))
        {
            return false;
        }
        
        // Then do the more accurate box test
        return ViewFrustum.IntersectBox(Bounds.Origin, Bounds.BoxExtent);
    }

    /**
     * Tests visibility and returns outcode for partial containment
     * @param Origin - Center of the box
     * @param Extent - Half-extents of the box
     * @return Outcode indicating inside/outside/intersecting
     */
    FOutcode GetVisibilityOutcode(const FVector& Origin, const FVector& Extent) const
    {
        return ViewFrustum.GetBoxIntersectionOutcode(Origin, Extent);
    }

private:
    const FViewFrustum& ViewFrustum;
};

// ============================================================================
// Distance Culling
// ============================================================================

/**
 * Performs distance-based culling
 */
class FDistanceCuller
{
public:
    /**
     * Constructor
     * @param InViewOrigin - Origin of the view
     * @param InMaxDrawDistanceScale - Scale factor for max draw distance
     */
    FDistanceCuller(const FVector& InViewOrigin, float InMaxDrawDistanceScale = 1.0f)
        : ViewOrigin(InViewOrigin)
        , MaxDrawDistanceScale(InMaxDrawDistanceScale)
    {
    }

    /**
     * Tests if a primitive should be culled based on distance
     * @param Bounds - Bounds of the primitive
     * @param MinDrawDistance - Minimum draw distance
     * @param MaxDrawDistance - Maximum draw distance
     * @return true if the primitive should be culled
     */
    bool ShouldCull(const FBoxSphereBounds& Bounds, float MinDrawDistance, float MaxDrawDistance) const
    {
        float DistanceSquared = (Bounds.Origin - ViewOrigin).SizeSquared();
        float ScaledMaxDistance = MaxDrawDistance * MaxDrawDistanceScale;

        // Check minimum distance
        if (MinDrawDistance > 0.0f && DistanceSquared < FMath::Square(MinDrawDistance))
        {
            return true;
        }

        // Check maximum distance
        if (MaxDrawDistance > 0.0f && DistanceSquared > FMath::Square(ScaledMaxDistance))
        {
            return true;
        }

        return false;
    }

    /**
     * Gets the squared distance from view origin to a point
     * @param Point - Point to measure distance to
     * @return Squared distance
     */
    FORCEINLINE float GetDistanceSquared(const FVector& Point) const
    {
        return (Point - ViewOrigin).SizeSquared();
    }

private:
    FVector ViewOrigin;
    float MaxDrawDistanceScale;
};

// ============================================================================
// Occlusion Culling
// ============================================================================

/**
 * Occlusion query result
 */
struct FOcclusionQueryResult
{
    /** Index of the primitive */
    int32 PrimitiveIndex = INDEX_NONE;

    /** Number of visible samples (0 = fully occluded) */
    uint32 NumVisibleSamples = 0;

    /** Whether the query is complete */
    bool bQueryComplete = false;

    /** Whether the primitive is visible based on the query */
    bool IsVisible() const { return NumVisibleSamples > 0; }
};

/**
 * Manages occlusion queries for a view
 */
class FOcclusionQueryManager
{
public:
    /** Maximum number of pending queries */
    static constexpr int32 MaxPendingQueries = 4096;

    /** Constructor */
    FOcclusionQueryManager() = default;

    /** Destructor */
    ~FOcclusionQueryManager() = default;

    /**
     * Begins a new frame of occlusion queries
     * @param FrameNumber - Current frame number
     */
    void BeginFrame(uint32 FrameNumber);

    /**
     * Ends the current frame of occlusion queries
     */
    void EndFrame();

    /**
     * Requests an occlusion query for a primitive
     * @param PrimitiveIndex - Index of the primitive
     * @param Bounds - Bounds to query
     * @return true if the query was successfully queued
     */
    bool RequestQuery(int32 PrimitiveIndex, const FBoxSphereBounds& Bounds);

    /**
     * Gets the result of a previous occlusion query
     * @param PrimitiveIndex - Index of the primitive
     * @param OutResult - Result of the query
     * @return true if a result is available
     */
    bool GetQueryResult(int32 PrimitiveIndex, FOcclusionQueryResult& OutResult) const;

    /**
     * Checks if a primitive was visible in the last query
     * @param PrimitiveIndex - Index of the primitive
     * @return true if the primitive was visible or no query exists
     */
    bool WasVisibleLastFrame(int32 PrimitiveIndex) const;

    /**
     * Gets the number of pending queries
     * @return Number of pending queries
     */
    int32 GetNumPendingQueries() const { return PendingQueries.Num(); }

private:
    /** Pending occlusion queries */
    TArray<int32> PendingQueries;

    /** Results from previous frame */
    TMap<int32, FOcclusionQueryResult> QueryResults;

    /** Current frame number */
    uint32 CurrentFrameNumber = 0;
};

// ============================================================================
// Scene Visibility Manager
// ============================================================================

/**
 * Manages visibility determination for a scene
 */
class FSceneVisibilityManager
{
public:
    /** Constructor */
    FSceneVisibilityManager() = default;

    /** Destructor */
    ~FSceneVisibilityManager() = default;

    /**
     * Computes visibility for a view
     * @param Scene - The scene to compute visibility for
     * @param View - The view to compute visibility for
     * @param OutResult - Results of visibility determination
     */
    void ComputeVisibility(const FScene& Scene, const FSceneView& View, FViewVisibilityResult& OutResult);

    /**
     * Performs frustum culling on all primitives
     * @param Scene - The scene
     * @param View - The view
     * @param OutResult - Results to update
     */
    void FrustumCull(const FScene& Scene, const FSceneView& View, FViewVisibilityResult& OutResult);

    /**
     * Performs distance culling on visible primitives
     * @param Scene - The scene
     * @param View - The view
     * @param OutResult - Results to update
     */
    void DistanceCull(const FScene& Scene, const FSceneView& View, FViewVisibilityResult& OutResult);

    /**
     * Performs occlusion culling on visible primitives
     * @param Scene - The scene
     * @param View - The view
     * @param OutResult - Results to update
     */
    void OcclusionCull(const FScene& Scene, const FSceneView& View, FViewVisibilityResult& OutResult);

    /**
     * Gets the visibility state for a primitive
     * @param PrimitiveIndex - Index of the primitive
     * @return Visibility state
     */
    FPrimitiveVisibilityState& GetVisibilityState(int32 PrimitiveIndex);

    /**
     * Gets the fading state for a primitive
     * @param PrimitiveId - Component ID of the primitive
     * @return Fading state
     */
    FPrimitiveFadingState& GetFadingState(FPrimitiveComponentId PrimitiveId);

    /**
     * Sets the custom visibility query
     * @param InCustomQuery - Custom query interface
     */
    void SetCustomVisibilityQuery(ICustomVisibilityQuery* InCustomQuery)
    {
        CustomVisibilityQuery = InCustomQuery;
    }

    /**
     * Gets the occlusion query manager
     * @return Reference to the occlusion query manager
     */
    FOcclusionQueryManager& GetOcclusionQueryManager() { return OcclusionQueryManager; }

private:
    /** Visibility states for primitives */
    TArray<FPrimitiveVisibilityState> VisibilityStates;

    /** Fading states for primitives (keyed by component ID) */
    TMap<FPrimitiveComponentId, FPrimitiveFadingState> FadingStates;

    /** Occlusion query manager */
    FOcclusionQueryManager OcclusionQueryManager;

    /** Custom visibility query interface */
    ICustomVisibilityQuery* CustomVisibilityQuery = nullptr;

    /** Current frame number */
    uint32 CurrentFrameNumber = 0;
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Computes the screen size of a sphere
 * @param Origin - Center of the sphere in world space
 * @param SphereRadius - Radius of the sphere
 * @param ViewOrigin - View origin
 * @param ProjMatrix - Projection matrix
 * @param ViewRect - View rectangle
 * @return Screen radius in pixels
 */
float ComputeBoundsScreenSize(
    const FVector& Origin,
    float SphereRadius,
    const FVector& ViewOrigin,
    const FMatrix& ProjMatrix,
    const FIntRect& ViewRect);

/**
 * Computes the screen size ratio of a sphere
 * @param Origin - Center of the sphere in world space
 * @param SphereRadius - Radius of the sphere
 * @param ViewOrigin - View origin
 * @param ProjMatrix - Projection matrix
 * @return Screen size ratio (0-1 range, where 1 = full screen)
 */
float ComputeBoundsScreenSizeRatio(
    const FVector& Origin,
    float SphereRadius,
    const FVector& ViewOrigin,
    const FMatrix& ProjMatrix);

/**
 * Tests if a primitive should be drawn based on screen size
 * @param ScreenSize - Screen size of the primitive
 * @param MinScreenSize - Minimum screen size to be visible
 * @return true if the primitive should be drawn
 */
FORCEINLINE bool ShouldDrawPrimitive(float ScreenSize, float MinScreenSize)
{
    return ScreenSize >= MinScreenSize;
}

} // namespace MonsterEngine
