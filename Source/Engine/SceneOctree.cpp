// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SceneOctree.cpp
 * @brief Implementation of scene-specific octree helper functions
 * 
 * This file implements the FSceneOctreeHelper class which provides
 * utility functions for common octree operations used in scene rendering,
 * such as frustum culling and light-primitive interaction detection.
 */

#include "Engine/SceneOctree.h"
#include "Engine/PrimitiveSceneInfo.h"
#include "Engine/LightSceneInfo.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Define log category for scene octree operations
DEFINE_LOG_CATEGORY_STATIC(LogSceneOctree, Log, All);

// ============================================================================
// FSceneOctreeHelper Implementation
// ============================================================================

void FSceneOctreeHelper::FindPrimitivesInFrustum(
    const FScenePrimitiveOctree& Octree,
    const FConvexVolume& Frustum,
    TArray<FPrimitiveSceneInfo*>& OutVisiblePrimitives)
{
    // Access the frustum planes directly (Planes is a public member)
    const TArray<FPlane>& FrustumPlanes = Frustum.Planes;
    
    // Check if the frustum has valid planes
    if (FrustumPlanes.Num() == 0)
    {
        MR_LOG(LogSceneOctree, Warning, "FindPrimitivesInFrustum called with empty frustum");
        return;
    }

    // Use the octree's frustum query to find candidate elements
    // This performs hierarchical culling through the octree nodes
    TArray<FPrimitiveSceneInfoCompact> Elements;
    Octree.FindElementsInFrustum(FrustumPlanes.GetData(), FrustumPlanes.Num(), Elements);

    // Extract primitive scene infos from elements
    // Perform a precise frustum test since the octree query is conservative
    OutVisiblePrimitives.Empty(Elements.Num());
    for (const FPrimitiveSceneInfoCompact& Element : Elements)
    {
        // Additional frustum test against the actual bounds
        // The octree query tests node bounds, so we need to verify the element bounds
        if (Frustum.IntersectBox(Element.Bounds.Origin, Element.Bounds.BoxExtent))
        {
            OutVisiblePrimitives.Add(Element.PrimitiveSceneInfo);
        }
    }

    MR_LOG(LogSceneOctree, Verbose, "FindPrimitivesInFrustum found %d primitives", 
           OutVisiblePrimitives.Num());
}

void FSceneOctreeHelper::FindPrimitivesInSphere(
    const FScenePrimitiveOctree& Octree,
    const FVector& Center,
    float Radius,
    TArray<FPrimitiveSceneInfo*>& OutPrimitives)
{
    // Use the octree's sphere query
    TArray<FPrimitiveSceneInfoCompact> Elements;
    Octree.FindElementsInSphere(Center, Radius, Elements);

    // Extract primitive scene infos from elements
    OutPrimitives.Empty(Elements.Num());
    for (const FPrimitiveSceneInfoCompact& Element : Elements)
    {
        OutPrimitives.Add(Element.PrimitiveSceneInfo);
    }

    MR_LOG(LogSceneOctree, Verbose, "FindPrimitivesInSphere found %d primitives", 
           OutPrimitives.Num());
}

void FSceneOctreeHelper::FindPrimitivesInBox(
    const FScenePrimitiveOctree& Octree,
    const FBox& Box,
    TArray<FPrimitiveSceneInfo*>& OutPrimitives)
{
    // Use the octree's box query
    TArray<FPrimitiveSceneInfoCompact> Elements;
    Octree.FindElementsInBox(Box, Elements);

    // Extract primitive scene infos from elements
    OutPrimitives.Empty(Elements.Num());
    for (const FPrimitiveSceneInfoCompact& Element : Elements)
    {
        OutPrimitives.Add(Element.PrimitiveSceneInfo);
    }

    MR_LOG(LogSceneOctree, Verbose, "FindPrimitivesInBox found %d primitives", 
           OutPrimitives.Num());
}

void FSceneOctreeHelper::FindLightsAffectingBounds(
    const FSceneLightOctree& Octree,
    const FBoxSphereBounds& Bounds,
    TArray<FLightSceneInfo*>& OutLights)
{
    // Create a query box from the bounds
    FBox QueryBox = Bounds.GetBox();

    // Use the octree's box query
    TArray<FLightSceneInfoCompactOctree> Elements;
    Octree.FindElementsInBox(QueryBox, Elements);

    // Extract light scene infos from elements
    // Also check if the light's influence actually reaches the bounds
    OutLights.Empty(Elements.Num());
    for (const FLightSceneInfoCompactOctree& Element : Elements)
    {
        // Check if the light's bounds intersect with the query bounds
        if (Element.Bounds.GetBox().Intersect(QueryBox))
        {
            OutLights.Add(Element.LightSceneInfo);
        }
    }

    MR_LOG(LogSceneOctree, Verbose, "FindLightsAffectingBounds found %d lights", 
           OutLights.Num());
}

void FSceneOctreeHelper::FindLightsAffectingPoint(
    const FSceneLightOctree& Octree,
    const FVector& Point,
    TArray<FLightSceneInfo*>& OutLights)
{
    // Query with a small sphere around the point
    TArray<FLightSceneInfoCompactOctree> Elements;
    Octree.FindElementsInSphere(Point, 0.1f, Elements);

    // Extract light scene infos and verify the point is within the light's influence
    OutLights.Empty(Elements.Num());
    for (const FLightSceneInfoCompactOctree& Element : Elements)
    {
        // Check if the point is within the light's bounds
        if (Element.Bounds.GetBox().IsInside(Point))
        {
            OutLights.Add(Element.LightSceneInfo);
        }
    }

    MR_LOG(LogSceneOctree, Verbose, "FindLightsAffectingPoint found %d lights", 
           OutLights.Num());
}

} // namespace MonsterEngine
