// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CameraModifier.cpp
 * @brief Implementation of camera modifier base class
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/Camera/CameraModifier.cpp
 */

#include "Engine/Camera/CameraModifier.h"
#include "Engine/Camera/CameraManager.h"
#include "Core/Logging/LogMacros.h"
#include "Math/MonsterMath.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogCameraModifier, Log, All);

// ============================================================================
// FCameraModifier Implementation
// ============================================================================

FCameraModifier::FCameraModifier()
    : CameraOwner(nullptr)
    , Alpha(0.0f)
    , AlphaInTime(0.0f)
    , AlphaOutTime(0.0f)
    , Priority(127)  // Middle priority by default
    , bDebug(false)
    , bExclusive(false)
    , bDisabled(true)  // Start disabled
    , bPendingDisable(false)
{
}

FCameraModifier::~FCameraModifier()
{
}

// ============================================================================
// Initialization
// ============================================================================

void FCameraModifier::AddedToCamera(FCameraManager* Camera)
{
    CameraOwner = Camera;
    
    // Enable the modifier when added
    EnableModifier();
    
    MR_LOG(LogCameraModifier, Verbose, "Camera modifier added to camera manager");
}

// ============================================================================
// Modifier Application
// ============================================================================

bool FCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
    // Update alpha blend
    UpdateAlpha(DeltaTime);
    
    // If fully disabled, skip processing
    if (Alpha <= 0.0f)
    {
        return false;
    }
    
    // Store original values
    FVector OriginalLocation = InOutPOV.Location;
    FRotator OriginalRotation = InOutPOV.Rotation;
    float OriginalFOV = InOutPOV.FOV;
    
    // Call native modification
    FVector NewLocation = OriginalLocation;
    FRotator NewRotation = OriginalRotation;
    float NewFOV = OriginalFOV;
    
    ModifyCamera(DeltaTime, OriginalLocation, OriginalRotation, OriginalFOV,
                 NewLocation, NewRotation, NewFOV);
    
    // Apply alpha blend to the modification
    if (Alpha < 1.0f)
    {
        // Blend location (manual lerp for FVector)
        double AlphaD = static_cast<double>(Alpha);
        InOutPOV.Location.X = OriginalLocation.X + AlphaD * (NewLocation.X - OriginalLocation.X);
        InOutPOV.Location.Y = OriginalLocation.Y + AlphaD * (NewLocation.Y - OriginalLocation.Y);
        InOutPOV.Location.Z = OriginalLocation.Z + AlphaD * (NewLocation.Z - OriginalLocation.Z);
        
        // Blend rotation using quaternion slerp
        FQuat OrigQuat = OriginalRotation.Quaternion();
        FQuat NewQuat = NewRotation.Quaternion();
        FQuat BlendedQuat = FQuat::Slerp(OrigQuat, NewQuat, Alpha);
        InOutPOV.Rotation = BlendedQuat.Rotator();
        
        // Blend FOV
        InOutPOV.FOV = FMath::Lerp(OriginalFOV, NewFOV, Alpha);
    }
    else
    {
        // Full alpha, use new values directly
        InOutPOV.Location = NewLocation;
        InOutPOV.Rotation = NewRotation;
        InOutPOV.FOV = NewFOV;
    }
    
    // Return false to continue processing other modifiers
    // Return true if this modifier should stop the chain (exclusive behavior)
    return bExclusive && Alpha >= 1.0f;
}

bool FCameraModifier::ProcessViewRotation(AActor* ViewTarget, float DeltaTime,
                                          FRotator& OutViewRotation, FRotator& OutDeltaRot)
{
    // Default implementation does nothing
    // Derived classes can override to modify view rotation
    return false;
}

// ============================================================================
// Enable / Disable
// ============================================================================

void FCameraModifier::DisableModifier(bool bImmediate)
{
    if (bImmediate)
    {
        // Disable immediately
        bDisabled = true;
        bPendingDisable = false;
        Alpha = 0.0f;
    }
    else
    {
        // Mark for disable after blend out
        bPendingDisable = true;
    }
}

void FCameraModifier::EnableModifier()
{
    bDisabled = false;
    bPendingDisable = false;
}

void FCameraModifier::ToggleModifier()
{
    if (bDisabled)
    {
        EnableModifier();
    }
    else
    {
        DisableModifier(false);
    }
}

// ============================================================================
// Alpha Blending
// ============================================================================

void FCameraModifier::UpdateAlpha(float DeltaTime)
{
    float TargetAlpha = GetTargetAlpha();
    
    if (Alpha < TargetAlpha)
    {
        // Blending in
        if (AlphaInTime > 0.0f)
        {
            Alpha = FMath::Min(Alpha + DeltaTime / AlphaInTime, TargetAlpha);
        }
        else
        {
            Alpha = TargetAlpha;
        }
    }
    else if (Alpha > TargetAlpha)
    {
        // Blending out
        if (AlphaOutTime > 0.0f)
        {
            Alpha = FMath::Max(Alpha - DeltaTime / AlphaOutTime, TargetAlpha);
        }
        else
        {
            Alpha = TargetAlpha;
        }
        
        // Check if blend out is complete and we should disable
        if (bPendingDisable && Alpha <= 0.0f)
        {
            bDisabled = true;
            bPendingDisable = false;
        }
    }
}

float FCameraModifier::GetTargetAlpha()
{
    // Return 0 if disabled or pending disable, 1 otherwise
    return (bDisabled || bPendingDisable) ? 0.0f : 1.0f;
}

// ============================================================================
// View Target
// ============================================================================

AActor* FCameraModifier::GetViewTarget() const
{
    if (CameraOwner)
    {
        return CameraOwner->GetViewTarget();
    }
    return nullptr;
}

// ============================================================================
// Protected Methods
// ============================================================================

void FCameraModifier::ModifyCamera(float DeltaTime,
                                   FVector ViewLocation, FRotator ViewRotation, float FOV,
                                   FVector& NewViewLocation, FRotator& NewViewRotation, float& NewFOV)
{
    // Default implementation: no modification
    NewViewLocation = ViewLocation;
    NewViewRotation = ViewRotation;
    NewFOV = FOV;
}

// ============================================================================
// FCameraModifier_CameraShake Implementation
// ============================================================================

FCameraModifier_CameraShake::FCameraModifier_CameraShake()
    : FCameraModifier()
{
    // Camera shake modifier has high priority
    Priority = 10;
}

FCameraModifier_CameraShake::~FCameraModifier_CameraShake()
{
}

void FCameraModifier_CameraShake::AddCameraShake(float ShakeScale,
                                                  ECameraShakePlaySpace PlaySpace,
                                                  const FRotator& UserPlaySpaceRot)
{
    // TODO: Implement camera shake instance creation and tracking
    MR_LOG(LogCameraModifier, Verbose, "Adding camera shake with scale: %f", ShakeScale);
}

void FCameraModifier_CameraShake::StopAllCameraShakes(bool bImmediate)
{
    // TODO: Stop all active camera shakes
    MR_LOG(LogCameraModifier, Verbose, "Stopping all camera shakes");
}

bool FCameraModifier_CameraShake::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
    // TODO: Apply active camera shakes to the POV
    // For now, just call base implementation
    return FCameraModifier::ModifyCamera(DeltaTime, InOutPOV);
}

} // namespace MonsterEngine
