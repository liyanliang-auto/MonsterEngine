// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CameraComponent.cpp
 * @brief Implementation of camera component
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/Camera/CameraComponent.cpp
 */

#include "Engine/Camera/CameraComponent.h"
#include "Core/Logging/LogMacros.h"
#include "Math/MonsterMath.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogCameraComponent, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

UCameraComponent::UCameraComponent()
    : USceneComponent()
    , FieldOfView(90.0f)
    , OrthoWidth(512.0f)
    , OrthoNearClipPlane(0.0f)
    , OrthoFarClipPlane(100000.0f)
    , AspectRatio(1.777778f)  // 16:9
    , PostProcessBlendWeight(1.0f)
    , ProjectionMode(ECameraProjectionMode::Perspective)
    , AdditiveOffset()
    , AdditiveFOVOffset(0.0f)
    , bConstrainAspectRatio(false)
    , bUseFieldOfViewForLOD(true)
    , bUsePawnControlRotation(false)
    , bLockToHmd(false)
    , bUseAdditiveOffset(false)
    , bIsActive(true)
{
}

UCameraComponent::UCameraComponent(AActor* InOwner)
    : USceneComponent(InOwner)
    , FieldOfView(90.0f)
    , OrthoWidth(512.0f)
    , OrthoNearClipPlane(0.0f)
    , OrthoFarClipPlane(100000.0f)
    , AspectRatio(1.777778f)
    , PostProcessBlendWeight(1.0f)
    , ProjectionMode(ECameraProjectionMode::Perspective)
    , AdditiveOffset()
    , AdditiveFOVOffset(0.0f)
    , bConstrainAspectRatio(false)
    , bUseFieldOfViewForLOD(true)
    , bUsePawnControlRotation(false)
    , bLockToHmd(false)
    , bUseAdditiveOffset(false)
    , bIsActive(true)
{
}

UCameraComponent::~UCameraComponent()
{
}

// ============================================================================
// Component Lifecycle
// ============================================================================

void UCameraComponent::OnRegister()
{
    USceneComponent::OnRegister();
    
    MR_LOG(LogCameraComponent, Verbose, "Camera component registered");
}

void UCameraComponent::OnUnregister()
{
    USceneComponent::OnUnregister();
    
    MR_LOG(LogCameraComponent, Verbose, "Camera component unregistered");
}

void UCameraComponent::TickComponent(float DeltaTime)
{
    USceneComponent::TickComponent(DeltaTime);
    
    // Handle XR camera if applicable
    HandleXRCamera();
}

// ============================================================================
// Camera View
// ============================================================================

void UCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
    // Handle XR/VR camera tracking
    if (bLockToHmd && IsXRHeadTrackedCamera())
    {
        HandleXRCamera();
    }
    
    // Get the component's world transform
    FTransform ComponentTransform = GetComponentTransform();
    
    // Apply additive offset if enabled
    if (bUseAdditiveOffset)
    {
        ComponentTransform = AdditiveOffset * ComponentTransform;
    }
    
    // Set location and rotation from transform
    DesiredView.Location = ComponentTransform.GetLocation();
    DesiredView.Rotation = ComponentTransform.GetRotation().Rotator();
    
    // Set FOV with additive offset
    DesiredView.FOV = FieldOfView + AdditiveFOVOffset;
    DesiredView.DesiredFOV = FieldOfView;
    
    // Set projection mode
    DesiredView.ProjectionMode = ProjectionMode;
    
    // Set orthographic settings
    DesiredView.OrthoWidth = OrthoWidth;
    DesiredView.OrthoNearClipPlane = OrthoNearClipPlane;
    DesiredView.OrthoFarClipPlane = OrthoFarClipPlane;
    
    // Set aspect ratio
    DesiredView.AspectRatio = AspectRatio;
    DesiredView.bConstrainAspectRatio = bConstrainAspectRatio;
    
    // Set LOD settings
    DesiredView.bUseFieldOfViewForLOD = bUseFieldOfViewForLOD;
    
    // Set post process settings
    DesiredView.PostProcessBlendWeight = PostProcessBlendWeight;
    
    // TODO: Copy post process settings when implemented
    // DesiredView.PostProcessSettings = PostProcessSettings;
}

// ============================================================================
// Additive Offset
// ============================================================================

void UCameraComponent::AddAdditiveOffset(const FTransform& Transform, float FOVOffset)
{
    if (!bUseAdditiveOffset)
    {
        // First time adding offset, just set it
        AdditiveOffset = Transform;
        AdditiveFOVOffset = FOVOffset;
        bUseAdditiveOffset = true;
    }
    else
    {
        // Accumulate with existing offset
        AdditiveOffset = Transform * AdditiveOffset;
        AdditiveFOVOffset += FOVOffset;
    }
}

void UCameraComponent::ClearAdditiveOffset()
{
    AdditiveOffset = FTransform::Identity;
    AdditiveFOVOffset = 0.0f;
    bUseAdditiveOffset = false;
}

void UCameraComponent::GetAdditiveOffset(FTransform& OutAdditiveOffset, float& OutAdditiveFOVOffset) const
{
    OutAdditiveOffset = AdditiveOffset;
    OutAdditiveFOVOffset = AdditiveFOVOffset;
}

// ============================================================================
// Camera Cut Notification
// ============================================================================

void UCameraComponent::NotifyCameraCut()
{
    // Called when the camera is cut to (no blend)
    // Can be used to reset interpolation or other state
    MR_LOG(LogCameraComponent, Verbose, "Camera cut notification received");
}

// ============================================================================
// XR/VR Support
// ============================================================================

void UCameraComponent::HandleXRCamera()
{
    // TODO: Implement XR/VR camera tracking when XR system is available
    // This would query the HMD position and rotation and apply it to the camera
}

bool UCameraComponent::IsXRHeadTrackedCamera() const
{
    // TODO: Check if XR system is active and this camera should be tracked
    // For now, return false as XR is not implemented
    return false;
}

} // namespace MonsterEngine
