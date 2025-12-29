// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SceneView.cpp
 * @brief Implementation of FSceneView and FSceneViewFamily
 */

#include "Engine/SceneView.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Use global log category (defined in LogCategories.cpp)
using MonsterRender::LogSceneView;

// ============================================================================
// FSceneView Implementation
// ============================================================================

FSceneView::FSceneView(const FSceneViewInitOptions& InitOptions)
    : Family(InitOptions.ViewFamily)
    , UnscaledViewRect(InitOptions.GetViewRect())
    , ViewRect(InitOptions.GetConstrainedViewRect())
    , ViewLocation(InitOptions.ViewLocation)
    , ViewRotation(InitOptions.ViewRotation)
    , BackgroundColor(InitOptions.BackgroundColor)
    , OverlayColor(InitOptions.OverlayColor)
    , ColorScale(InitOptions.ColorScale)
    , StereoPass(InitOptions.StereoPass)
    , StereoViewIndex(InitOptions.StereoViewIndex)
    , PlayerIndex(InitOptions.PlayerIndex)
    , FOV(InitOptions.FOV)
    , NearClipPlane(InitOptions.NearClipPlane)
    , FarClipPlane(InitOptions.FarClipPlane)
    , LODDistanceFactor(InitOptions.LODDistanceFactor)
    , WorldToMetersScale(InitOptions.WorldToMetersScale)
    , HiddenPrimitives(InitOptions.HiddenPrimitives)
    , CursorPos(InitOptions.CursorPos)
    , bInCameraCut(InitOptions.bInCameraCut)
    , bIsSceneCapture(InitOptions.bIsSceneCapture)
    , bIsReflectionCapture(InitOptions.bIsReflectionCapture)
    , bIsPlanarReflection(InitOptions.bIsPlanarReflection)
    , bUseFieldOfViewForLOD(InitOptions.bUseFieldOfViewForLOD)
{
    // Initialize view matrices
    ViewMatrices.Init(InitOptions);
    
    // Setup view frustum
    SetupViewFrustum();
    
    MR_LOG(LogSceneView, Verbose, "SceneView created at location (%.2f, %.2f, %.2f)", 
           ViewLocation.X, ViewLocation.Y, ViewLocation.Z);
}

void FSceneView::SetupViewFrustum()
{
    // Initialize frustum from view-projection matrix
    // Use base class Init directly since FViewFrustum::Init takes two matrices
    static_cast<FConvexVolume&>(ViewFrustum).Init(ViewMatrices.GetViewProjectionMatrix(), true);
}

FVector4 FSceneView::WorldToScreen(const FVector& WorldPoint) const
{
    return ViewMatrices.GetViewProjectionMatrix().TransformFVector4(FVector4(WorldPoint, 1.0f));
}

FVector FSceneView::ScreenToWorld(const FVector4& ScreenPoint) const
{
    FVector4 WorldPoint = ViewMatrices.GetInvViewProjectionMatrix().TransformFVector4(ScreenPoint);
    
    if (FMath::Abs(WorldPoint.W) > MR_SMALL_NUMBER)
    {
        return FVector(WorldPoint.X / WorldPoint.W, WorldPoint.Y / WorldPoint.W, WorldPoint.Z / WorldPoint.W);
    }
    
    return FVector::ZeroVector;
}

bool FSceneView::ScreenToPixel(const FVector4& ScreenPoint, FVector2D& OutPixelLocation) const
{
    if (ScreenPoint.W > 0.0f)
    {
        float InvW = 1.0f / ScreenPoint.W;
        float NormalizedX = ScreenPoint.X * InvW;
        float NormalizedY = ScreenPoint.Y * InvW;
        
        // Convert from [-1,1] to [0,1]
        float U = (NormalizedX + 1.0f) * 0.5f;
        float V = (1.0f - NormalizedY) * 0.5f; // Y is flipped
        
        // Convert to pixel coordinates
        OutPixelLocation.X = ViewRect.Min.X + U * ViewRect.Width();
        OutPixelLocation.Y = ViewRect.Min.Y + V * ViewRect.Height();
        
        return true;
    }
    
    return false;
}

FVector4 FSceneView::PixelToScreen(float X, float Y, float Z) const
{
    // Convert from pixel coordinates to [0,1]
    float U = (X - ViewRect.Min.X) / ViewRect.Width();
    float V = (Y - ViewRect.Min.Y) / ViewRect.Height();
    
    // Convert from [0,1] to [-1,1]
    float NormalizedX = U * 2.0f - 1.0f;
    float NormalizedY = 1.0f - V * 2.0f; // Y is flipped
    
    return FVector4(NormalizedX, NormalizedY, Z, 1.0f);
}

bool FSceneView::WorldToPixel(const FVector& WorldPoint, FVector2D& OutPixelLocation) const
{
    FVector4 ScreenPoint = WorldToScreen(WorldPoint);
    return ScreenToPixel(ScreenPoint, OutPixelLocation);
}

FVector4 FSceneView::PixelToWorld(float X, float Y, float Z) const
{
    FVector4 ScreenPoint = PixelToScreen(X, Y, Z);
    FVector WorldPoint = ScreenToWorld(ScreenPoint);
    return FVector4(WorldPoint, 1.0f);
}

FPlane FSceneView::Project(const FVector& WorldPoint) const
{
    FVector4 ScreenPoint = WorldToScreen(WorldPoint);
    
    if (FMath::Abs(ScreenPoint.W) > MR_SMALL_NUMBER)
    {
        float InvW = 1.0f / ScreenPoint.W;
        return FPlane(ScreenPoint.X * InvW, ScreenPoint.Y * InvW, ScreenPoint.Z * InvW, ScreenPoint.W);
    }
    
    return FPlane(0, 0, 0, 0);
}

FVector FSceneView::Deproject(const FPlane& ScreenPoint) const
{
    FVector4 HomogeneousPoint(ScreenPoint.X * ScreenPoint.W, ScreenPoint.Y * ScreenPoint.W, 
                              ScreenPoint.Z * ScreenPoint.W, ScreenPoint.W);
    return ScreenToWorld(HomogeneousPoint);
}

void FSceneView::DeprojectScreenToWorld(const FVector2D& ScreenPos, FVector& OutWorldOrigin, FVector& OutWorldDirection) const
{
    // Convert screen position to normalized device coordinates
    float U = (ScreenPos.X - ViewRect.Min.X) / ViewRect.Width();
    float V = (ScreenPos.Y - ViewRect.Min.Y) / ViewRect.Height();
    
    float NormalizedX = U * 2.0f - 1.0f;
    float NormalizedY = 1.0f - V * 2.0f;
    
    // Create near and far points in clip space
    FVector4 NearPoint(NormalizedX, NormalizedY, 0.0f, 1.0f);
    FVector4 FarPoint(NormalizedX, NormalizedY, 1.0f, 1.0f);
    
    // Transform to world space
    FVector NearWorld = ScreenToWorld(NearPoint);
    FVector FarWorld = ScreenToWorld(FarPoint);
    
    // Calculate ray
    OutWorldOrigin = NearWorld;
    OutWorldDirection = (FarWorld - NearWorld).GetSafeNormal();
}

// ============================================================================
// FSceneViewFamily Implementation
// ============================================================================

FSceneViewFamily::FSceneViewFamily(const ConstructionValues& CVS)
    : RenderTarget(CVS.RenderTarget)
    , Scene(CVS.Scene)
    , Time(CVS.Time)
    , FrameNumber(0)
    , GammaCorrection(CVS.GammaCorrection)
    , bRealtimeUpdate(CVS.bRealtimeUpdate)
    , bDeferClear(CVS.bDeferClear)
    , bResolveScene(CVS.bResolveScene)
    , bWorldIsPaused(false)
{
    MR_LOG(LogSceneView, Verbose, "SceneViewFamily created");
}

FSceneViewFamily::~FSceneViewFamily()
{
    // Note: Views are not owned by the family in the base class
    MR_LOG(LogSceneView, Verbose, "SceneViewFamily destroyed");
}

// ============================================================================
// FSceneViewFamilyContext Implementation
// ============================================================================

FSceneViewFamilyContext::~FSceneViewFamilyContext()
{
    // Delete all views owned by this context
    for (const FSceneView* View : Views)
    {
        delete View;
    }
    Views.Empty();
}

} // namespace MonsterEngine
