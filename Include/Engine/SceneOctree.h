// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SceneOctree.h
 * @brief Scene-specific octree utilities and helper functions
 * 
 * This file provides utility functions for working with scene octrees.
 * The actual octree types (FScenePrimitiveOctree, FSceneLightOctree) are
 * defined in Octree.h as type aliases of TOctree.
 * 
 * This file adds:
 * - Helper functions for frustum culling
 * - Utility functions for light-primitive queries
 * - Scene-specific octree operations
 * 
 * Following UE5's scene octree architecture for efficient spatial queries
 * during visibility culling and light-primitive interaction detection.
 */

#include "Octree.h"
#include "ConvexVolume.h"
#include "SceneTypes.h"
#include "Containers/Array.h"

namespace MonsterEngine
{

// Forward declarations
class FPrimitiveSceneInfo;
class FLightSceneInfo;

/**
 * Helper class for scene octree operations
 * 
 * This class provides static utility functions for common octree operations
 * used in scene rendering, such as frustum culling and light queries.
 */
class FSceneOctreeHelper
{
public:
    /**
     * Find all primitives in the octree that are visible in the given frustum
     * 
     * This function performs hierarchical frustum culling using the octree
     * structure for efficient spatial queries.
     * 
     * @param Octree The primitive octree to query
     * @param Frustum The view frustum to cull against
     * @param OutVisiblePrimitives Output array of visible primitives
     */
    static void FindPrimitivesInFrustum(
        const FScenePrimitiveOctree& Octree,
        const FConvexVolume& Frustum,
        TArray<FPrimitiveSceneInfo*>& OutVisiblePrimitives);

    /**
     * Find all primitives in the octree within a sphere
     * 
     * Useful for finding primitives affected by point lights or
     * other spherical regions of influence.
     * 
     * @param Octree The primitive octree to query
     * @param Center The center of the sphere
     * @param Radius The radius of the sphere
     * @param OutPrimitives Output array of primitives in the sphere
     */
    static void FindPrimitivesInSphere(
        const FScenePrimitiveOctree& Octree,
        const FVector& Center,
        float Radius,
        TArray<FPrimitiveSceneInfo*>& OutPrimitives);

    /**
     * Find all primitives in the octree within a box
     * 
     * @param Octree The primitive octree to query
     * @param Box The axis-aligned bounding box to query
     * @param OutPrimitives Output array of primitives in the box
     */
    static void FindPrimitivesInBox(
        const FScenePrimitiveOctree& Octree,
        const FBox& Box,
        TArray<FPrimitiveSceneInfo*>& OutPrimitives);

    /**
     * Find all lights in the octree that affect a given bounds
     * 
     * @param Octree The light octree to query
     * @param Bounds The bounds to check for light influence
     * @param OutLights Output array of affecting lights
     */
    static void FindLightsAffectingBounds(
        const FSceneLightOctree& Octree,
        const FBoxSphereBounds& Bounds,
        TArray<FLightSceneInfo*>& OutLights);

    /**
     * Find all lights in the octree that affect a given point
     * 
     * @param Octree The light octree to query
     * @param Point The world-space point
     * @param OutLights Output array of affecting lights
     */
    static void FindLightsAffectingPoint(
        const FSceneLightOctree& Octree,
        const FVector& Point,
        TArray<FLightSceneInfo*>& OutLights);
};

/**
 * Add a primitive to the scene octree
 * 
 * Helper function that creates the compact representation and adds it to the octree.
 * 
 * @param Octree The octree to add to
 * @param PrimitiveSceneInfo The primitive to add
 * @param Bounds The world-space bounds of the primitive
 * @return The octree ID assigned to the primitive
 */
inline uint32 AddPrimitiveToOctree(
    FScenePrimitiveOctree& Octree,
    FPrimitiveSceneInfo* PrimitiveSceneInfo,
    const FBoxSphereBounds& Bounds)
{
    // Create compact representation for octree storage
    FPrimitiveSceneInfoCompact Compact(PrimitiveSceneInfo);
    Compact.Bounds = Bounds;
    
    // Add to octree and return the assigned ID
    return Octree.AddElement(Compact);
}

/**
 * Remove a primitive from the scene octree
 * 
 * @param Octree The octree to remove from
 * @param PrimitiveSceneInfo The primitive to remove
 * @param Bounds The current bounds of the primitive
 * @return True if the primitive was found and removed
 */
inline bool RemovePrimitiveFromOctree(
    FScenePrimitiveOctree& Octree,
    FPrimitiveSceneInfo* PrimitiveSceneInfo,
    const FBoxSphereBounds& Bounds)
{
    // Create compact representation to find in octree
    FPrimitiveSceneInfoCompact Compact(PrimitiveSceneInfo);
    Compact.Bounds = Bounds;
    
    // Remove from octree
    return Octree.RemoveElement(Compact);
}

/**
 * Add a light to the scene octree
 * 
 * @param Octree The octree to add to
 * @param LightSceneInfo The light to add
 * @param Bounds The world-space bounds of the light
 * @return The octree ID assigned to the light
 */
inline uint32 AddLightToOctree(
    FSceneLightOctree& Octree,
    FLightSceneInfo* LightSceneInfo,
    const FBoxSphereBounds& Bounds)
{
    // Create compact representation for octree storage
    FLightSceneInfoCompactOctree Compact(LightSceneInfo);
    Compact.Bounds = Bounds;
    
    // Add to octree and return the assigned ID
    return Octree.AddElement(Compact);
}

/**
 * Remove a light from the scene octree
 * 
 * @param Octree The octree to remove from
 * @param LightSceneInfo The light to remove
 * @param Bounds The current bounds of the light
 * @return True if the light was found and removed
 */
inline bool RemoveLightFromOctree(
    FSceneLightOctree& Octree,
    FLightSceneInfo* LightSceneInfo,
    const FBoxSphereBounds& Bounds)
{
    // Create compact representation to find in octree
    FLightSceneInfoCompactOctree Compact(LightSceneInfo);
    Compact.Bounds = Bounds;
    
    // Remove from octree
    return Octree.RemoveElement(Compact);
}

} // namespace MonsterEngine
