// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CameraTypes.cpp
 * @brief Implementation of camera type structures
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/Camera/CameraTypes.cpp
 */

#include "Engine/Camera/CameraTypes.h"
#include "Core/Logging/LogMacros.h"
#include "Math/MonsterMath.h"

namespace MonsterEngine
{

// Use global log category (defined in LogCategories.cpp)
using MonsterRender::LogCameraTypes;

// Global near clipping plane distance
static float GNearClippingPlane = 10.0f;

// ============================================================================
// FMinimalViewInfo Implementation
// ============================================================================

bool FMinimalViewInfo::Equals(const FMinimalViewInfo& Other) const
{
    return Location.Equals(Other.Location) &&
           Rotation.Equals(Other.Rotation) &&
           FMath::IsNearlyEqual(FOV, Other.FOV) &&
           FMath::IsNearlyEqual(OrthoWidth, Other.OrthoWidth) &&
           FMath::IsNearlyEqual(AspectRatio, Other.AspectRatio) &&
           ProjectionMode == Other.ProjectionMode &&
           bConstrainAspectRatio == Other.bConstrainAspectRatio;
}

void FMinimalViewInfo::BlendViewInfo(FMinimalViewInfo& OtherInfo, float OtherWeight)
{
    // Blend location (manual lerp for FVector)
    double Alpha = static_cast<double>(OtherWeight);
    Location.X = Location.X + Alpha * (OtherInfo.Location.X - Location.X);
    Location.Y = Location.Y + Alpha * (OtherInfo.Location.Y - Location.Y);
    Location.Z = Location.Z + Alpha * (OtherInfo.Location.Z - Location.Z);
    
    // Blend rotation (use quaternion slerp for smooth interpolation)
    FQuat ThisQuat = Rotation.Quaternion();
    FQuat OtherQuat = OtherInfo.Rotation.Quaternion();
    FQuat BlendedQuat = FQuat::Slerp(ThisQuat, OtherQuat, OtherWeight);
    Rotation = BlendedQuat.Rotator();
    
    // Blend FOV
    FOV = FMath::Lerp(FOV, OtherInfo.FOV, OtherWeight);
    DesiredFOV = FMath::Lerp(DesiredFOV, OtherInfo.DesiredFOV, OtherWeight);
    
    // Blend orthographic settings
    OrthoWidth = FMath::Lerp(OrthoWidth, OtherInfo.OrthoWidth, OtherWeight);
    OrthoNearClipPlane = FMath::Lerp(OrthoNearClipPlane, OtherInfo.OrthoNearClipPlane, OtherWeight);
    OrthoFarClipPlane = FMath::Lerp(OrthoFarClipPlane, OtherInfo.OrthoFarClipPlane, OtherWeight);
    
    // Blend aspect ratio
    AspectRatio = FMath::Lerp(AspectRatio, OtherInfo.AspectRatio, OtherWeight);
    
    // Blend post process weight
    PostProcessBlendWeight = FMath::Lerp(PostProcessBlendWeight, OtherInfo.PostProcessBlendWeight, OtherWeight);
    
    // Blend off-center projection
    OffCenterProjectionOffsetX = FMath::Lerp(OffCenterProjectionOffsetX, OtherInfo.OffCenterProjectionOffsetX, OtherWeight);
    OffCenterProjectionOffsetY = FMath::Lerp(OffCenterProjectionOffsetY, OtherInfo.OffCenterProjectionOffsetY, OtherWeight);
    
    // Boolean flags - use the other value if weight > 0.5
    if (OtherWeight > 0.5f)
    {
        bConstrainAspectRatio = OtherInfo.bConstrainAspectRatio;
        bUseFieldOfViewForLOD = OtherInfo.bUseFieldOfViewForLOD;
        ProjectionMode = OtherInfo.ProjectionMode;
    }
}

void FMinimalViewInfo::ApplyBlendWeight(float Weight)
{
    // Scale location by weight
    Location = Location * static_cast<double>(Weight);
    
    // Scale rotation by weight (interpolate from identity)
    FQuat ThisQuat = Rotation.Quaternion();
    FQuat IdentityQuat = FQuat::Identity;
    FQuat BlendedQuat = FQuat::Slerp(IdentityQuat, ThisQuat, Weight);
    Rotation = BlendedQuat.Rotator();
    
    // Scale FOV contribution (relative to 90 degrees as base)
    FOV = 90.0f + (FOV - 90.0f) * Weight;
    DesiredFOV = 90.0f + (DesiredFOV - 90.0f) * Weight;
    
    // Scale other values
    OrthoWidth = OrthoWidth * Weight;
    PostProcessBlendWeight = PostProcessBlendWeight * Weight;
}

void FMinimalViewInfo::AddWeightedViewInfo(const FMinimalViewInfo& OtherView, float Weight)
{
    // Add weighted location
    Location = Location + OtherView.Location * static_cast<double>(Weight);
    
    // Add weighted rotation (accumulate quaternions)
    FQuat ThisQuat = Rotation.Quaternion();
    FQuat OtherQuat = OtherView.Rotation.Quaternion();
    FQuat WeightedOtherQuat = FQuat::Slerp(FQuat::Identity, OtherQuat, Weight);
    FQuat ResultQuat = ThisQuat * WeightedOtherQuat;
    ResultQuat.Normalize();
    Rotation = ResultQuat.Rotator();
    
    // Add weighted FOV
    FOV = FOV + (OtherView.FOV - 90.0f) * Weight;
    DesiredFOV = DesiredFOV + (OtherView.DesiredFOV - 90.0f) * Weight;
    
    // Add weighted orthographic width
    OrthoWidth = OrthoWidth + OtherView.OrthoWidth * Weight;
    
    // Add weighted post process
    PostProcessBlendWeight = PostProcessBlendWeight + OtherView.PostProcessBlendWeight * Weight;
}

FMatrix FMinimalViewInfo::CalculateProjectionMatrix() const
{
    FMatrix ProjectionMatrix;
    
    if (ProjectionMode == ECameraProjectionMode::Orthographic)
    {
        // Orthographic projection matrix
        const float Width = OrthoWidth;
        const float Height = OrthoWidth / AspectRatio;
        
        const float NearPlane = OrthoNearClipPlane;
        const float FarPlane = OrthoFarClipPlane;
        
        // Create orthographic projection matrix
        ProjectionMatrix = FMatrix::MakeOrtho(Width, Height, NearPlane, FarPlane);
    }
    else
    {
        // Perspective projection matrix
        const float NearPlane = GetFinalPerspectiveNearClipPlane();
        const float FarPlane = 100000.0f;
        const float FOVRadians = FMath::DegreesToRadians(FOV);
        
        MR_LOG(LogCameraTypes, Log, "CalculateProjectionMatrix: FOV=%.1f deg (%.3f rad), AspectRatio=%.3f, Near=%.3f, Far=%.1f",
               FOV, FOVRadians, AspectRatio, NearPlane, FarPlane);
        
        // Create perspective projection matrix
        // Note: MakePerspective expects full FOV in radians, not half FOV
        // Use Vulkan-style matrix (depth range [0, 1])
        // Y-flip will be handled separately in CubeSceneApplication
        ProjectionMatrix = FMatrix::MakePerspective(
            FOVRadians,
            AspectRatio,
            NearPlane,
            FarPlane
        );
        
        // DEBUG: Log full projection matrix
        MR_LOG(LogCameraTypes, Log, "ProjectionMatrix:");
        MR_LOG(LogCameraTypes, Log, "  [0]: %.4f, %.4f, %.4f, %.4f", 
               ProjectionMatrix.M[0][0], ProjectionMatrix.M[0][1], ProjectionMatrix.M[0][2], ProjectionMatrix.M[0][3]);
        MR_LOG(LogCameraTypes, Log, "  [1]: %.4f, %.4f, %.4f, %.4f",
               ProjectionMatrix.M[1][0], ProjectionMatrix.M[1][1], ProjectionMatrix.M[1][2], ProjectionMatrix.M[1][3]);
        MR_LOG(LogCameraTypes, Log, "  [2]: %.4f, %.4f, %.4f, %.4f",
               ProjectionMatrix.M[2][0], ProjectionMatrix.M[2][1], ProjectionMatrix.M[2][2], ProjectionMatrix.M[2][3]);
        MR_LOG(LogCameraTypes, Log, "  [3]: %.4f, %.4f, %.4f, %.4f",
               ProjectionMatrix.M[3][0], ProjectionMatrix.M[3][1], ProjectionMatrix.M[3][2], ProjectionMatrix.M[3][3]);
    }
    
    // Apply off-center projection offset if needed
    if (!FMath::IsNearlyZero(OffCenterProjectionOffsetX) || !FMath::IsNearlyZero(OffCenterProjectionOffsetY))
    {
        ProjectionMatrix.M[2][0] += OffCenterProjectionOffsetX;
        ProjectionMatrix.M[2][1] += OffCenterProjectionOffsetY;
    }
    
    return ProjectionMatrix;
}

float FMinimalViewInfo::GetFinalPerspectiveNearClipPlane() const
{
    return PerspectiveNearClipPlane > 0.0f ? PerspectiveNearClipPlane : GNearClippingPlane;
}

FMatrix FMinimalViewInfo::CalculateViewRotationMatrix() const
{
    // Create rotation matrix from the camera rotation
    // The view matrix transforms from world space to view space
    // We need the inverse of the camera rotation
    return FMatrix::MakeFromRotator(Rotation);
}

// ============================================================================
// FViewTargetTransitionParams Implementation
// ============================================================================

float FViewTargetTransitionParams::GetBlendAlpha(float TimePct) const
{
    switch (BlendFunction)
    {
        case EViewTargetBlendFunction::Linear:
            return FMath::Lerp(0.0f, 1.0f, TimePct);
            
        case EViewTargetBlendFunction::Cubic:
            // Cubic interpolation for smooth ease in/out
            return FMath::CubicInterp(0.0f, 0.0f, 1.0f, 0.0f, TimePct);
            
        case EViewTargetBlendFunction::EaseIn:
            // Ease in using power function
            return FMath::Lerp(0.0f, 1.0f, FMath::Pow(TimePct, BlendExp));
            
        case EViewTargetBlendFunction::EaseOut:
            // Ease out using inverse power function
            {
                float Exp = FMath::IsNearlyZero(BlendExp) ? 1.0f : (1.0f / BlendExp);
                return FMath::Lerp(0.0f, 1.0f, FMath::Pow(TimePct, Exp));
            }
            
        case EViewTargetBlendFunction::EaseInOut:
            // Smooth ease in and out
            return FMath::InterpEaseInOut(0.0f, 1.0f, TimePct, BlendExp);
            
        case EViewTargetBlendFunction::PreBlended:
            // Already blended, return 1
            return 1.0f;
            
        default:
            return 1.0f;
    }
}

// ============================================================================
// FViewTarget Implementation
// ============================================================================

void FViewTarget::SetNewTarget(AActor* NewTarget)
{
    Target = NewTarget;
    
    // Reset POV to defaults when changing target
    POV = FMinimalViewInfo();
}

bool FViewTarget::Equal(const FViewTarget& Other) const
{
    return Target == Other.Target;
}

void FViewTarget::CheckViewTarget(APlayerController* OwningController)
{
    // Validate the view target
    // If target is null or pending kill, reset to controller
    if (Target == nullptr)
    {
        // TODO: Reset to owning controller's pawn or controller itself
        // For now, just log a warning
        MR_LOG(LogCameraTypes, Warning, "View target is null");
    }
}

} // namespace MonsterEngine
