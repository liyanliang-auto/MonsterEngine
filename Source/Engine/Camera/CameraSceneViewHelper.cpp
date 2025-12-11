// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CameraSceneViewHelper.cpp
 * @brief Implementation of camera to scene view helper
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/Camera/CameraTypes.cpp
 *                Engine/Source/Runtime/Engine/Private/SceneView.cpp
 */

#include "Engine/Camera/CameraSceneViewHelper.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/SceneView.h"
#include "Core/Logging/LogMacros.h"
#include "Math/MonsterMath.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogCameraSceneView, Log, All);

void FCameraSceneViewHelper::InitViewOptionsFromMinimalViewInfo(
    FSceneViewInitOptions& OutViewInitOptions,
    const FMinimalViewInfo& ViewInfo,
    const FSceneViewFamily* ViewFamily,
    const FIntRect& ViewRect,
    int32 PlayerIndex)
{
    // Set view family
    OutViewInitOptions.ViewFamily = ViewFamily;
    OutViewInitOptions.PlayerIndex = PlayerIndex;
    
    // Set view location and rotation
    OutViewInitOptions.ViewLocation = ViewInfo.Location;
    OutViewInitOptions.ViewRotation = ViewInfo.Rotation;
    OutViewInitOptions.ViewOrigin = ViewInfo.Location;
    
    // Set FOV
    OutViewInitOptions.FOV = ViewInfo.FOV;
    OutViewInitOptions.DesiredFOV = ViewInfo.DesiredFOV;
    
    // Set clip planes
    OutViewInitOptions.NearClipPlane = ViewInfo.GetFinalPerspectiveNearClipPlane();
    OutViewInitOptions.FarClipPlane = ViewInfo.OrthoFarClipPlane;
    
    // Set view rectangle
    if (ViewInfo.bConstrainAspectRatio)
    {
        // Calculate constrained view rect with letterboxing
        FIntRect ConstrainedRect = CalculateConstrainedViewRect(ViewInfo, ViewRect);
        OutViewInitOptions.SetViewRectangle(ViewRect);
        OutViewInitOptions.SetConstrainedViewRectangle(ConstrainedRect);
    }
    else
    {
        OutViewInitOptions.SetViewRectangle(ViewRect);
        OutViewInitOptions.SetConstrainedViewRectangle(ViewRect);
    }
    
    // Calculate view rotation matrix
    OutViewInitOptions.ViewRotationMatrix = CalculateViewRotationMatrix(ViewInfo);
    
    // Calculate projection matrix
    OutViewInitOptions.ProjectionMatrix = CalculateProjectionMatrix(ViewInfo, 
        OutViewInitOptions.GetConstrainedViewRect());
    
    // Set LOD settings
    OutViewInitOptions.bUseFieldOfViewForLOD = ViewInfo.bUseFieldOfViewForLOD;
    OutViewInitOptions.LODDistanceFactor = 1.0f;
    
    // Default colors
    OutViewInitOptions.BackgroundColor = FLinearColor::Transparent;
    OutViewInitOptions.OverlayColor = FLinearColor::Transparent;
    OutViewInitOptions.ColorScale = FLinearColor::White;
    
    MR_LOG(LogCameraSceneView, Verbose, 
        "Initialized view options: Location=(%.2f, %.2f, %.2f), FOV=%.2f",
        ViewInfo.Location.X, ViewInfo.Location.Y, ViewInfo.Location.Z, ViewInfo.FOV);
}

FMatrix FCameraSceneViewHelper::CalculateProjectionMatrix(
    const FMinimalViewInfo& ViewInfo,
    const FIntRect& ViewRect)
{
    FMatrix ProjectionMatrix;
    
    // Calculate aspect ratio from view rect
    float AspectRatio = ViewInfo.AspectRatio;
    if (!ViewInfo.bConstrainAspectRatio && ViewRect.Width() > 0 && ViewRect.Height() > 0)
    {
        AspectRatio = static_cast<float>(ViewRect.Width()) / static_cast<float>(ViewRect.Height());
    }
    
    if (ViewInfo.ProjectionMode == ECameraProjectionMode::Orthographic)
    {
        // Orthographic projection
        const float Width = ViewInfo.OrthoWidth;
        const float Height = ViewInfo.OrthoWidth / AspectRatio;
        const float NearPlane = ViewInfo.OrthoNearClipPlane;
        const float FarPlane = ViewInfo.OrthoFarClipPlane;
        
        ProjectionMatrix = FMatrix::MakeOrtho(Width, Height, NearPlane, FarPlane);
    }
    else
    {
        // Perspective projection
        const float NearPlane = ViewInfo.GetFinalPerspectiveNearClipPlane();
        const float HalfFOVRadians = FMath::DegreesToRadians(ViewInfo.FOV * 0.5f);
        
        ProjectionMatrix = FMatrix::MakePerspective(
            HalfFOVRadians,
            AspectRatio,
            NearPlane,
            100000.0f  // Far plane
        );
    }
    
    // Apply off-center projection offset if needed
    if (!FMath::IsNearlyZero(ViewInfo.OffCenterProjectionOffsetX) || 
        !FMath::IsNearlyZero(ViewInfo.OffCenterProjectionOffsetY))
    {
        ProjectionMatrix.M[2][0] += ViewInfo.OffCenterProjectionOffsetX;
        ProjectionMatrix.M[2][1] += ViewInfo.OffCenterProjectionOffsetY;
    }
    
    return ProjectionMatrix;
}

FMatrix FCameraSceneViewHelper::CalculateViewRotationMatrix(const FMinimalViewInfo& ViewInfo)
{
    // Create rotation matrix from the camera rotation
    // This transforms from world space to view space
    return FMatrix::MakeFromRotator(ViewInfo.Rotation);
}

void FCameraSceneViewHelper::ApplyCameraManagerToViewOptions(
    FSceneViewInitOptions& OutViewInitOptions,
    const FCameraManager* CameraManager,
    const FSceneViewFamily* ViewFamily,
    const FIntRect& ViewRect,
    int32 PlayerIndex)
{
    if (CameraManager == nullptr)
    {
        MR_LOG(LogCameraSceneView, Warning, "ApplyCameraManagerToViewOptions called with null CameraManager");
        return;
    }
    
    // Get the current camera view from the camera manager
    const FMinimalViewInfo& CameraView = CameraManager->GetCameraCacheView();
    
    // Initialize view options from the camera view
    InitViewOptionsFromMinimalViewInfo(
        OutViewInitOptions,
        CameraView,
        ViewFamily,
        ViewRect,
        PlayerIndex
    );
    
    MR_LOG(LogCameraSceneView, Verbose, "Applied camera manager view to scene view options");
}

FIntRect FCameraSceneViewHelper::CalculateConstrainedViewRect(
    const FMinimalViewInfo& ViewInfo,
    const FIntRect& UnconstrainedViewRect)
{
    if (!ViewInfo.bConstrainAspectRatio)
    {
        return UnconstrainedViewRect;
    }
    
    const float DesiredAspectRatio = ViewInfo.AspectRatio;
    const float ViewRectWidth = static_cast<float>(UnconstrainedViewRect.Width());
    const float ViewRectHeight = static_cast<float>(UnconstrainedViewRect.Height());
    const float CurrentAspectRatio = ViewRectWidth / ViewRectHeight;
    
    FIntRect ConstrainedRect = UnconstrainedViewRect;
    
    if (CurrentAspectRatio > DesiredAspectRatio)
    {
        // Viewport is wider than desired - add pillarboxing (black bars on sides)
        const float NewWidth = ViewRectHeight * DesiredAspectRatio;
        const float WidthDiff = ViewRectWidth - NewWidth;
        const int32 HalfWidthDiff = static_cast<int32>(WidthDiff * 0.5f);
        
        ConstrainedRect.Min.X = UnconstrainedViewRect.Min.X + HalfWidthDiff;
        ConstrainedRect.Max.X = UnconstrainedViewRect.Max.X - HalfWidthDiff;
    }
    else if (CurrentAspectRatio < DesiredAspectRatio)
    {
        // Viewport is taller than desired - add letterboxing (black bars on top/bottom)
        const float NewHeight = ViewRectWidth / DesiredAspectRatio;
        const float HeightDiff = ViewRectHeight - NewHeight;
        const int32 HalfHeightDiff = static_cast<int32>(HeightDiff * 0.5f);
        
        ConstrainedRect.Min.Y = UnconstrainedViewRect.Min.Y + HalfHeightDiff;
        ConstrainedRect.Max.Y = UnconstrainedViewRect.Max.Y - HalfHeightDiff;
    }
    
    return ConstrainedRect;
}

float FCameraSceneViewHelper::CalculateEffectiveFOV(
    const FMinimalViewInfo& ViewInfo,
    const FIntRect& ViewRect)
{
    if (ViewInfo.ProjectionMode == ECameraProjectionMode::Orthographic)
    {
        // FOV doesn't apply to orthographic projection
        return ViewInfo.FOV;
    }
    
    float EffectiveFOV = ViewInfo.FOV;
    
    // If aspect ratio is constrained, we may need to adjust FOV
    if (ViewInfo.bConstrainAspectRatio)
    {
        const float DesiredAspectRatio = ViewInfo.AspectRatio;
        const float ViewRectWidth = static_cast<float>(ViewRect.Width());
        const float ViewRectHeight = static_cast<float>(ViewRect.Height());
        const float CurrentAspectRatio = ViewRectWidth / ViewRectHeight;
        
        if (!FMath::IsNearlyEqual(CurrentAspectRatio, DesiredAspectRatio))
        {
            // Adjust FOV to maintain the same vertical field of view
            // when the aspect ratio changes
            const float HalfFOVRadians = FMath::DegreesToRadians(ViewInfo.FOV * 0.5f);
            const float TanHalfFOV = FMath::Tan(HalfFOVRadians);
            
            // Calculate new horizontal FOV based on current aspect ratio
            const float NewTanHalfFOV = TanHalfFOV * (CurrentAspectRatio / DesiredAspectRatio);
            EffectiveFOV = FMath::RadiansToDegrees(FMath::Atan(NewTanHalfFOV)) * 2.0f;
        }
    }
    
    return EffectiveFOV;
}

} // namespace MonsterEngine
