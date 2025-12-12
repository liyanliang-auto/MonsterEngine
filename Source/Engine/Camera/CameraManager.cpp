// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CameraManager.cpp
 * @brief Implementation of camera manager
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/PlayerCameraManager.cpp
 */

#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/CameraModifier.h"
#include "Engine/Camera/CameraComponent.h"
#include "Core/Logging/LogMacros.h"
#include "Math/MonsterMath.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogCameraManager, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

FCameraManager::FCameraManager()
    : PCOwner(nullptr)
    , ViewTarget()
    , PendingViewTarget()
    , BlendTimeToGo(0.0f)
    , BlendParams()
    , CameraCache()
    , LastFrameCameraCache()
    , ModifierList()
    , DefaultFOV(90.0f)
    , DefaultOrthoWidth(512.0f)
    , DefaultAspectRatio(1.777778f)
    , LockedFOV(-1.0f)
    , CameraStyle()
    , ViewPitchMin(-89.9f)
    , ViewPitchMax(89.9f)
    , ViewYawMin(0.0f)
    , ViewYawMax(359.999f)
    , ViewRollMin(-89.9f)
    , ViewRollMax(89.9f)
    , bIsOrthographic(false)
    , bDefaultConstrainAspectRatio(false)
    , bAlwaysApplyModifiers(false)
    , bIsInitialized(false)
{
}

FCameraManager::FCameraManager(APlayerController* InOwner)
    : FCameraManager()
{
    Initialize(InOwner);
}

FCameraManager::~FCameraManager()
{
    Shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

void FCameraManager::Initialize(APlayerController* InOwner)
{
    if (bIsInitialized)
    {
        MR_LOG(LogCameraManager, Warning, "Camera manager already initialized");
        return;
    }
    
    PCOwner = InOwner;
    bIsInitialized = true;
    
    MR_LOG(LogCameraManager, Log, "Camera manager initialized");
}

void FCameraManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Clear all modifiers
    ClearAllCameraModifiers();
    
    // Clear view targets
    ViewTarget.Target = nullptr;
    PendingViewTarget.Target = nullptr;
    
    PCOwner = nullptr;
    bIsInitialized = false;
    
    MR_LOG(LogCameraManager, Log, "Camera manager shutdown");
}

// ============================================================================
// Update
// ============================================================================

void FCameraManager::UpdateCamera(float DeltaTime)
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Save last frame's camera cache
    LastFrameCameraCache = CameraCache;
    
    // Update view target blend
    if (PendingViewTarget.Target != nullptr && BlendTimeToGo > 0.0f)
    {
        BlendTimeToGo -= DeltaTime;
        
        if (BlendTimeToGo <= 0.0f)
        {
            // Blend complete, switch to pending target
            ViewTarget = PendingViewTarget;
            PendingViewTarget.Target = nullptr;
            BlendTimeToGo = 0.0f;
        }
    }
    
    // Update the current view target
    UpdateViewTarget(ViewTarget, DeltaTime);
    
    // If blending, also update pending view target
    if (PendingViewTarget.Target != nullptr)
    {
        UpdateViewTarget(PendingViewTarget, DeltaTime);
        
        // Blend between view targets
        float BlendPct = 1.0f;
        if (BlendParams.BlendTime > 0.0f)
        {
            BlendPct = 1.0f - (BlendTimeToGo / BlendParams.BlendTime);
            BlendPct = FMath::Clamp(BlendPct, 0.0f, 1.0f);
        }
        
        float BlendAlpha = BlendParams.GetBlendAlpha(BlendPct);
        BlendViewTargets(ViewTarget, PendingViewTarget, BlendAlpha);
    }
    
    // Apply camera modifiers
    ApplyCameraModifiers(DeltaTime, ViewTarget.POV);
    
    // Update camera cache
    CameraCache.POV = ViewTarget.POV;
    CameraCache.TimeStamp = 0.0f;  // TODO: Use actual world time
}

// ============================================================================
// View Target
// ============================================================================

void FCameraManager::SetViewTarget(AActor* NewViewTarget, FViewTargetTransitionParams TransitionParams)
{
    // Validate new target
    if (NewViewTarget == nullptr)
    {
        // TODO: Default to owning controller's pawn
        MR_LOG(LogCameraManager, Warning, "SetViewTarget called with null target");
        return;
    }
    
    // Check if already targeting this actor
    if (NewViewTarget == ViewTarget.Target)
    {
        // Already targeting this actor
        if (PendingViewTarget.Target != nullptr)
        {
            // Cancel pending transition
            PendingViewTarget.Target = nullptr;
            BlendTimeToGo = 0.0f;
        }
        return;
    }
    
    // Check if already transitioning to this target
    if (NewViewTarget == PendingViewTarget.Target)
    {
        // Already transitioning to this target
        return;
    }
    
    if (TransitionParams.BlendTime > 0.0f)
    {
        // Start blend transition
        BlendTimeToGo = TransitionParams.BlendTime;
        BlendParams = TransitionParams;
        
        // Store current POV for blending
        if (PendingViewTarget.Target == nullptr)
        {
            PendingViewTarget.POV = ViewTarget.POV;
        }
        
        // Assign new pending target
        AssignViewTarget(NewViewTarget, PendingViewTarget, TransitionParams);
    }
    else
    {
        // Instant switch
        AssignViewTarget(NewViewTarget, ViewTarget, TransitionParams);
        PendingViewTarget.Target = nullptr;
        BlendTimeToGo = 0.0f;
    }
    
    MR_LOG(LogCameraManager, Verbose, "View target set");
}

AActor* FCameraManager::GetViewTarget() const
{
    // Return pending target if blending
    if (PendingViewTarget.Target != nullptr)
    {
        return PendingViewTarget.Target;
    }
    return ViewTarget.Target;
}

AActor* FCameraManager::GetPendingViewTarget() const
{
    return PendingViewTarget.Target;
}

// ============================================================================
// Camera View
// ============================================================================

FVector FCameraManager::GetCameraLocation() const
{
    return CameraCache.POV.Location;
}

FRotator FCameraManager::GetCameraRotation() const
{
    return CameraCache.POV.Rotation;
}

void FCameraManager::GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
    OutLocation = CameraCache.POV.Location;
    OutRotation = CameraCache.POV.Rotation;
}

float FCameraManager::GetFOVAngle() const
{
    if (LockedFOV > 0.0f)
    {
        return LockedFOV;
    }
    return CameraCache.POV.FOV;
}

const FMinimalViewInfo& FCameraManager::GetCameraCacheView() const
{
    return CameraCache.POV;
}

const FMinimalViewInfo& FCameraManager::GetLastFrameCameraCacheView() const
{
    return LastFrameCameraCache.POV;
}

// ============================================================================
// Camera Modifiers
// ============================================================================

bool FCameraManager::AddCameraModifier(FCameraModifier* Modifier)
{
    if (Modifier == nullptr)
    {
        return false;
    }
    
    // Check if already added
    for (FCameraModifier* Existing : ModifierList)
    {
        if (Existing == Modifier)
        {
            return false;
        }
    }
    
    // Find insertion point based on priority
    int32 InsertIndex = ModifierList.Num();
    for (int32 i = 0; i < ModifierList.Num(); ++i)
    {
        if (ModifierList[i]->GetPriority() > Modifier->GetPriority())
        {
            InsertIndex = i;
            break;
        }
    }
    
    // Insert at correct position
    ModifierList.Insert(Modifier, InsertIndex);
    
    // Notify modifier
    Modifier->AddedToCamera(this);
    
    MR_LOG(LogCameraManager, Verbose, "Camera modifier added at index %d", InsertIndex);
    
    return true;
}

bool FCameraManager::RemoveCameraModifier(FCameraModifier* Modifier)
{
    if (Modifier == nullptr)
    {
        return false;
    }
    
    int32 Index = ModifierList.Find(Modifier);
    if (Index != INDEX_NONE)
    {
        ModifierList.RemoveAt(Index);
        MR_LOG(LogCameraManager, Verbose, "Camera modifier removed");
        return true;
    }
    
    return false;
}

void FCameraManager::ClearAllCameraModifiers()
{
    ModifierList.Empty();
    MR_LOG(LogCameraManager, Verbose, "All camera modifiers cleared");
}

// ============================================================================
// View Rotation
// ============================================================================

void FCameraManager::ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation, FRotator& OutDeltaRot)
{
    // Apply view rotation limits
    LimitViewPitch(OutViewRotation, ViewPitchMin, ViewPitchMax);
    LimitViewYaw(OutViewRotation, ViewYawMin, ViewYawMax);
    LimitViewRoll(OutViewRotation, ViewRollMin, ViewRollMax);
    
    // Let modifiers process view rotation
    for (FCameraModifier* Modifier : ModifierList)
    {
        if (Modifier && !Modifier->IsDisabled())
        {
            if (Modifier->ProcessViewRotation(GetViewTarget(), DeltaTime, OutViewRotation, OutDeltaRot))
            {
                // Modifier wants to stop processing
                break;
            }
        }
    }
}

void FCameraManager::LimitViewPitch(FRotator& ViewRotation, float InViewPitchMin, float InViewPitchMax)
{
    ViewRotation.Pitch = FMath::Clamp(static_cast<float>(ViewRotation.Pitch), InViewPitchMin, InViewPitchMax);
}

void FCameraManager::LimitViewYaw(FRotator& ViewRotation, float InViewYawMin, float InViewYawMax)
{
    // Yaw limiting is more complex due to wrapping
    // For now, just normalize
    float YawValue = static_cast<float>(ViewRotation.Yaw);
    YawValue = FMath::Fmod(YawValue, 360.0f);
    if (YawValue < 0.0f)
    {
        YawValue += 360.0f;
    }
    ViewRotation.Yaw = YawValue;
}

void FCameraManager::LimitViewRoll(FRotator& ViewRotation, float InViewRollMin, float InViewRollMax)
{
    ViewRotation.Roll = FMath::Clamp(static_cast<float>(ViewRotation.Roll), InViewRollMin, InViewRollMax);
}

// ============================================================================
// Protected Methods
// ============================================================================

void FCameraManager::UpdateViewTarget(FViewTarget& OutVT, float DeltaTime)
{
    if (OutVT.Target == nullptr)
    {
        return;
    }
    
    // Reset POV to defaults
    OutVT.POV.FOV = DefaultFOV;
    OutVT.POV.OrthoWidth = DefaultOrthoWidth;
    OutVT.POV.AspectRatio = DefaultAspectRatio;
    OutVT.POV.bConstrainAspectRatio = bDefaultConstrainAspectRatio;
    OutVT.POV.bUseFieldOfViewForLOD = true;
    OutVT.POV.ProjectionMode = bIsOrthographic ? 
        ECameraProjectionMode::Orthographic : ECameraProjectionMode::Perspective;
    OutVT.POV.PostProcessBlendWeight = 1.0f;
    
    // TODO: Query the target actor for camera view
    // For now, use the actor's transform directly
    // In a full implementation, this would:
    // 1. Check if target is a CameraActor and use its CameraComponent
    // 2. Otherwise, call Target->CalcCamera() to find a camera component
    // 3. Fall back to actor's eye viewpoint
    
    // Placeholder: Use target's location and rotation
    // OutVT.POV.Location = OutVT.Target->GetActorLocation();
    // OutVT.POV.Rotation = OutVT.Target->GetActorRotation();
}

void FCameraManager::ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
    // Apply each modifier in priority order
    for (FCameraModifier* Modifier : ModifierList)
    {
        if (Modifier && !Modifier->IsDisabled())
        {
            if (Modifier->ModifyCamera(DeltaTime, InOutPOV))
            {
                // Modifier wants to stop processing
                break;
            }
        }
    }
}

void FCameraManager::BlendViewTargets(const FViewTarget& A, const FViewTarget& B, float Alpha)
{
    // Blend the POVs
    FMinimalViewInfo BlendedPOV = A.POV;
    BlendedPOV.BlendViewInfo(const_cast<FMinimalViewInfo&>(B.POV), Alpha);
    
    // Store blended result in current view target
    ViewTarget.POV = BlendedPOV;
}

void FCameraManager::AssignViewTarget(AActor* NewTarget, FViewTarget& VT,
                                       FViewTargetTransitionParams TransitionParams)
{
    if (NewTarget == nullptr)
    {
        return;
    }
    
    // Store old target for notifications
    AActor* OldTarget = VT.Target;
    
    // Assign new target
    VT.Target = NewTarget;
    
    // Reset POV to defaults
    VT.POV.AspectRatio = DefaultAspectRatio;
    VT.POV.bConstrainAspectRatio = bDefaultConstrainAspectRatio;
    VT.POV.FOV = DefaultFOV;
    
    // TODO: Notify old target that it's no longer the view target
    // TODO: Notify new target that it's now the view target
    
    MR_LOG(LogCameraManager, Verbose, "View target assigned");
}

void FCameraManager::SetCameraCachePOV(const FMinimalViewInfo& InPOV)
{
    CameraCache.POV = InPOV;
}

void FCameraManager::SetViewTargetPOV(const FMinimalViewInfo& InPOV)
{
    ViewTarget.POV = InPOV;
}

void FCameraManager::SetLastFrameCameraCachePOV(const FMinimalViewInfo& InPOV)
{
    LastFrameCameraCache.POV = InPOV;
}

} // namespace MonsterEngine
