// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SceneView.cpp
 * @brief Scene view implementation
 * 
 * Implements FSceneView, FViewInfo, and related view classes.
 * Reference: UE5 SceneView.cpp
 */

#include "Renderer/SceneView.h"
#include "Renderer/Scene.h"
#include "Core/Logging/LogMacros.h"
#include "Math/MathUtility.h"
#include "Math/MathFunctions.h"
#include "Math/Vector2D.h"

// Define SMALL_NUMBER if not defined
#ifndef SMALL_NUMBER
#define SMALL_NUMBER (1.e-8f)
#endif

namespace MonsterEngine
{
namespace Renderer
{

// ============================================================================
// FSceneView Implementation
// ============================================================================

void FSceneView::InitViewFrustum()
{
    // Build frustum planes from view-projection matrix
    const Math::FMatrix& VP = ViewMatrices.ViewProjectionMatrix;
    
    TArray<Math::FPlane> Planes;
    Planes.Reserve(6);
    
    // Extract frustum planes from view-projection matrix
    // Left plane
    Planes.Add(Math::FPlane(
        VP.M[0][3] + VP.M[0][0],
        VP.M[1][3] + VP.M[1][0],
        VP.M[2][3] + VP.M[2][0],
        VP.M[3][3] + VP.M[3][0]
    ));
    
    // Right plane
    Planes.Add(Math::FPlane(
        VP.M[0][3] - VP.M[0][0],
        VP.M[1][3] - VP.M[1][0],
        VP.M[2][3] - VP.M[2][0],
        VP.M[3][3] - VP.M[3][0]
    ));
    
    // Bottom plane
    Planes.Add(Math::FPlane(
        VP.M[0][3] + VP.M[0][1],
        VP.M[1][3] + VP.M[1][1],
        VP.M[2][3] + VP.M[2][1],
        VP.M[3][3] + VP.M[3][1]
    ));
    
    // Top plane
    Planes.Add(Math::FPlane(
        VP.M[0][3] - VP.M[0][1],
        VP.M[1][3] - VP.M[1][1],
        VP.M[2][3] - VP.M[2][1],
        VP.M[3][3] - VP.M[3][1]
    ));
    
    // Near plane
    Planes.Add(Math::FPlane(
        VP.M[0][2],
        VP.M[1][2],
        VP.M[2][2],
        VP.M[3][2]
    ));
    
    // Far plane
    Planes.Add(Math::FPlane(
        VP.M[0][3] - VP.M[0][2],
        VP.M[1][3] - VP.M[1][2],
        VP.M[2][3] - VP.M[2][2],
        VP.M[3][3] - VP.M[3][2]
    ));
    
    // Normalize all planes
    for (Math::FPlane& Plane : Planes)
    {
        Plane.Normalize();
    }
    
    // Initialize the convex volume
    ViewFrustum.Init(Planes);
}

bool FSceneView::ProjectWorldToScreen(const Math::FVector& WorldPosition, 
                                      Math::FVector2D& OutScreenPosition) const
{
    // Transform to clip space
    Math::FVector4 ClipSpace = ViewMatrices.ViewProjectionMatrix.TransformPosition(WorldPosition);
    
    // Check if behind camera
    if (ClipSpace.W <= 0.0f)
    {
        return false;
    }
    
    // Perspective divide
    float InvW = 1.0f / ClipSpace.W;
    float NdcX = ClipSpace.X * InvW;
    float NdcY = ClipSpace.Y * InvW;
    
    // Convert from NDC [-1, 1] to screen space [0, 1]
    OutScreenPosition.X = (NdcX + 1.0f) * 0.5f * ViewRect.width + ViewRect.x;
    OutScreenPosition.Y = (1.0f - NdcY) * 0.5f * ViewRect.height + ViewRect.y;
    
    return true;
}

bool FSceneView::DeprojectScreenToWorld(const Math::FVector2D& ScreenPosition,
                                        Math::FVector& OutWorldOrigin,
                                        Math::FVector& OutWorldDirection) const
{
    // Convert screen position to NDC
    float NdcX = ((ScreenPosition.X - ViewRect.x) / ViewRect.width) * 2.0f - 1.0f;
    float NdcY = 1.0f - ((ScreenPosition.Y - ViewRect.y) / ViewRect.height) * 2.0f;
    
    // Create near and far points in clip space
    Math::FVector4 NearPoint(NdcX, NdcY, 0.0f, 1.0f);
    Math::FVector4 FarPoint(NdcX, NdcY, 1.0f, 1.0f);
    
    // Transform to world space
    Math::FVector4 WorldNear = ViewMatrices.InvViewProjectionMatrix.TransformFVector4(NearPoint);
    Math::FVector4 WorldFar = ViewMatrices.InvViewProjectionMatrix.TransformFVector4(FarPoint);
    
    // Perspective divide
    if (Math::FMath::Abs(WorldNear.W) < SMALL_NUMBER || Math::FMath::Abs(WorldFar.W) < SMALL_NUMBER)
    {
        return false;
    }
    
    WorldNear /= WorldNear.W;
    WorldFar /= WorldFar.W;
    
    // Calculate ray origin and direction
    OutWorldOrigin = Math::FVector(WorldNear.X, WorldNear.Y, WorldNear.Z);
    Math::FVector FarWorld(WorldFar.X, WorldFar.Y, WorldFar.Z);
    OutWorldDirection = (FarWorld - OutWorldOrigin).GetSafeNormal();
    
    return true;
}

// ============================================================================
// FViewInfo Implementation
// ============================================================================

bool FViewInfo::IsDistanceCulled(float DistanceSquared, float MinDrawDistance, 
                                 float MaxDrawDistance) const
{
    // Check minimum draw distance
    if (MinDrawDistance > 0.0f)
    {
        float MinDistSq = MinDrawDistance * MinDrawDistance;
        if (DistanceSquared < MinDistSq)
        {
            return true;
        }
    }
    
    // Check maximum draw distance
    if (MaxDrawDistance < FLT_MAX)
    {
        float MaxDistSq = MaxDrawDistance * MaxDrawDistance;
        if (DistanceSquared > MaxDistSq)
        {
            return true;
        }
    }
    
    return false;
}

// Note: FConvexVolume methods are now defined inline in Renderer/SceneTypes.h

// ============================================================================
// FBoxSphereBounds Implementation
// ============================================================================

FBoxSphereBounds FBoxSphereBounds::TransformBy(const Math::FMatrix& M) const
{
    // Transform the origin
    Math::FVector4 TransformedOrigin4 = M.TransformPosition(Origin);
    Math::FVector TransformedOrigin(TransformedOrigin4.X, TransformedOrigin4.Y, TransformedOrigin4.Z);
    
    // Transform the extent (need to handle rotation properly)
    // For a proper implementation, we'd need to transform all 8 corners
    // and compute the new AABB. For now, use a conservative estimate.
    
    // Get the absolute values of the rotation matrix columns
    Math::FVector AbsX(Math::FMath::Abs(M.M[0][0]), Math::FMath::Abs(M.M[0][1]), Math::FMath::Abs(M.M[0][2]));
    Math::FVector AbsY(Math::FMath::Abs(M.M[1][0]), Math::FMath::Abs(M.M[1][1]), Math::FMath::Abs(M.M[1][2]));
    Math::FVector AbsZ(Math::FMath::Abs(M.M[2][0]), Math::FMath::Abs(M.M[2][1]), Math::FMath::Abs(M.M[2][2]));
    
    // Compute the new extent
    Math::FVector TransformedExtent;
    TransformedExtent.X = AbsX.X * BoxExtent.X + AbsY.X * BoxExtent.Y + AbsZ.X * BoxExtent.Z;
    TransformedExtent.Y = AbsX.Y * BoxExtent.X + AbsY.Y * BoxExtent.Y + AbsZ.Y * BoxExtent.Z;
    TransformedExtent.Z = AbsX.Z * BoxExtent.X + AbsY.Z * BoxExtent.Y + AbsZ.Z * BoxExtent.Z;
    
    // Compute new sphere radius (conservative estimate)
    float TransformedRadius = TransformedExtent.Size();
    
    return FBoxSphereBounds(TransformedOrigin, TransformedExtent, TransformedRadius);
}

} // namespace Renderer
} // namespace MonsterEngine
