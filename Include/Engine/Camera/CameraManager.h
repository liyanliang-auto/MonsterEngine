// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CameraManager.h
 * @brief Camera manager for controlling player camera
 * 
 * FCameraManager is responsible for managing the camera for a player.
 * It defines the final view properties used by the rendering system.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/Camera/PlayerCameraManager.h
 */

#include "Core/CoreTypes.h"
#include "Engine/Camera/CameraTypes.h"
#include "Containers/Array.h"
#include "Containers/String.h"

namespace MonsterEngine
{

// Forward declarations
class AActor;
class APlayerController;
class FCameraModifier;
class UCameraComponent;

// ============================================================================
// FCameraManager
// ============================================================================

/**
 * Camera manager class
 * 
 * Manages the camera for a player, including:
 * - View target management and blending
 * - Camera modifiers (shakes, animations)
 * - Post-process effect blending
 * - Camera lens effects
 * 
 * The camera manager maintains a "view target" which is the primary actor
 * the camera is associated with. It computes the final camera POV by
 * querying the view target and applying modifiers.
 * 
 * Reference UE5: APlayerCameraManager
 */
class FCameraManager
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================
    
    /** Default constructor */
    FCameraManager();
    
    /** Constructor with owning controller */
    explicit FCameraManager(APlayerController* InOwner);
    
    /** Virtual destructor */
    virtual ~FCameraManager();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initialize the camera manager
     * @param InOwner The owning player controller
     */
    virtual void Initialize(APlayerController* InOwner);
    
    /**
     * Shutdown the camera manager
     */
    virtual void Shutdown();
    
    // ========================================================================
    // Update
    // ========================================================================
    
    /**
     * Update the camera for this frame
     * @param DeltaTime Time since last update in seconds
     */
    virtual void UpdateCamera(float DeltaTime);
    
    // ========================================================================
    // View Target
    // ========================================================================
    
    /**
     * Set the view target
     * @param NewViewTarget New view target actor
     * @param TransitionParams Optional transition parameters
     */
    virtual void SetViewTarget(AActor* NewViewTarget, 
                               FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams());
    
    /**
     * Get the current view target
     * @return The current view target actor
     */
    AActor* GetViewTarget() const;
    
    /**
     * Get the pending view target (during transitions)
     * @return The pending view target actor, or nullptr
     */
    AActor* GetPendingViewTarget() const;
    
    // ========================================================================
    // Camera View
    // ========================================================================
    
    /**
     * Get the camera location
     * @return Camera location in world space
     */
    FVector GetCameraLocation() const;
    
    /**
     * Get the camera rotation
     * @return Camera rotation
     */
    FRotator GetCameraRotation() const;
    
    /**
     * Get the camera view point
     * @param OutLocation Output camera location
     * @param OutRotation Output camera rotation
     */
    void GetCameraViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
    
    /**
     * Get the current camera FOV
     * @return Field of view in degrees
     */
    float GetFOVAngle() const;
    
    /**
     * Get the cached camera view
     * @return The cached camera POV
     */
    const FMinimalViewInfo& GetCameraCacheView() const;
    
    /**
     * Get the last frame's cached camera view
     * @return The last frame's camera POV
     */
    const FMinimalViewInfo& GetLastFrameCameraCacheView() const;
    
    // ========================================================================
    // Camera Modifiers
    // ========================================================================
    
    /**
     * Add a camera modifier
     * @param Modifier The modifier to add
     * @return True if added successfully
     */
    virtual bool AddCameraModifier(FCameraModifier* Modifier);
    
    /**
     * Remove a camera modifier
     * @param Modifier The modifier to remove
     * @return True if removed successfully
     */
    virtual bool RemoveCameraModifier(FCameraModifier* Modifier);
    
    /**
     * Find a camera modifier by type
     * @return The modifier, or nullptr if not found
     */
    template<typename T>
    T* FindCameraModifier() const
    {
        for (FCameraModifier* Modifier : ModifierList)
        {
            if (T* TypedModifier = dynamic_cast<T*>(Modifier))
            {
                return TypedModifier;
            }
        }
        return nullptr;
    }
    
    /**
     * Clear all camera modifiers
     */
    virtual void ClearAllCameraModifiers();
    
    // ========================================================================
    // View Rotation
    // ========================================================================
    
    /**
     * Process view rotation updates
     * @param DeltaTime Time since last update
     * @param OutViewRotation View rotation to modify (in/out)
     * @param OutDeltaRot Rotation delta to modify (in/out)
     */
    virtual void ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation, FRotator& OutDeltaRot);
    
    /**
     * Limit view pitch
     * @param ViewRotation View rotation to limit
     * @param InViewPitchMin Minimum pitch
     * @param InViewPitchMax Maximum pitch
     */
    virtual void LimitViewPitch(FRotator& ViewRotation, float InViewPitchMin, float InViewPitchMax);
    
    /**
     * Limit view yaw
     * @param ViewRotation View rotation to limit
     * @param InViewYawMin Minimum yaw
     * @param InViewYawMax Maximum yaw
     */
    virtual void LimitViewYaw(FRotator& ViewRotation, float InViewYawMin, float InViewYawMax);
    
    /**
     * Limit view roll
     * @param ViewRotation View rotation to limit
     * @param InViewRollMin Minimum roll
     * @param InViewRollMax Maximum roll
     */
    virtual void LimitViewRoll(FRotator& ViewRotation, float InViewRollMin, float InViewRollMax);
    
    // ========================================================================
    // Camera Settings
    // ========================================================================
    
    /**
     * Get the default FOV
     * @return Default field of view in degrees
     */
    float GetDefaultFOV() const { return DefaultFOV; }
    
    /**
     * Set the default FOV
     * @param InDefaultFOV New default FOV
     */
    void SetDefaultFOV(float InDefaultFOV) { DefaultFOV = InDefaultFOV; }
    
    /**
     * Get the default aspect ratio
     * @return Default aspect ratio
     */
    float GetDefaultAspectRatio() const { return DefaultAspectRatio; }
    
    /**
     * Set the default aspect ratio
     * @param InDefaultAspectRatio New default aspect ratio
     */
    void SetDefaultAspectRatio(float InDefaultAspectRatio) { DefaultAspectRatio = InDefaultAspectRatio; }
    
    /**
     * Check if using orthographic projection
     * @return True if orthographic
     */
    bool IsOrthographic() const { return bIsOrthographic; }
    
    /**
     * Set orthographic mode
     * @param bInIsOrthographic Whether to use orthographic projection
     */
    void SetOrthographic(bool bInIsOrthographic) { bIsOrthographic = bInIsOrthographic; }
    
    // ========================================================================
    // Camera Style
    // ========================================================================
    
    /**
     * Get the camera style name
     * @return Camera style name
     */
    const FString& GetCameraStyle() const { return CameraStyle; }
    
    /**
     * Set the camera style
     * @param InCameraStyle New camera style name
     */
    void SetCameraStyle(const FString& InCameraStyle) { CameraStyle = InCameraStyle; }
    
    // ========================================================================
    // Owner
    // ========================================================================
    
    /**
     * Get the owning player controller
     * @return The owning player controller
     */
    APlayerController* GetOwningPlayerController() const { return PCOwner; }

protected:
    // ========================================================================
    // Protected Methods
    // ========================================================================
    
    /**
     * Update the view target
     * @param OutVT View target to update
     * @param DeltaTime Time since last update
     */
    virtual void UpdateViewTarget(FViewTarget& OutVT, float DeltaTime);
    
    /**
     * Apply camera modifiers to the POV
     * @param DeltaTime Time since last update
     * @param InOutPOV POV to modify
     */
    virtual void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV);
    
    /**
     * Blend view targets during transition
     * @param A First view target
     * @param B Second view target
     * @param Alpha Blend alpha (0 = A, 1 = B)
     */
    virtual void BlendViewTargets(const FViewTarget& A, const FViewTarget& B, float Alpha);
    
    /**
     * Assign a new view target
     * @param NewTarget New target actor
     * @param VT View target to assign to
     * @param TransitionParams Transition parameters
     */
    virtual void AssignViewTarget(AActor* NewTarget, FViewTarget& VT, 
                                  FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams());
    
    /**
     * Set the camera cache POV
     * @param InPOV POV to cache
     */
    void SetCameraCachePOV(const FMinimalViewInfo& InPOV);
    
    /**
     * Set the last frame camera cache POV
     * @param InPOV POV to cache
     */
    void SetLastFrameCameraCachePOV(const FMinimalViewInfo& InPOV);

protected:
    // ========================================================================
    // Owner
    // ========================================================================
    
    /** Owning player controller */
    APlayerController* PCOwner;
    
    // ========================================================================
    // View Targets
    // ========================================================================
    
    /** Current view target */
    FViewTarget ViewTarget;
    
    /** Pending view target (during transitions) */
    FViewTarget PendingViewTarget;
    
    /** Time remaining in view target blend */
    float BlendTimeToGo;
    
    /** Current blend parameters */
    FViewTargetTransitionParams BlendParams;
    
    // ========================================================================
    // Camera Cache
    // ========================================================================
    
    /** Cached camera POV */
    FCameraCacheEntry CameraCache;
    
    /** Last frame's cached camera POV */
    FCameraCacheEntry LastFrameCameraCache;
    
    // ========================================================================
    // Camera Modifiers
    // ========================================================================
    
    /** List of active camera modifiers */
    TArray<FCameraModifier*> ModifierList;
    
    // ========================================================================
    // Camera Settings
    // ========================================================================
    
    /** Default field of view */
    float DefaultFOV;
    
    /** Default orthographic width */
    float DefaultOrthoWidth;
    
    /** Default aspect ratio */
    float DefaultAspectRatio;
    
    /** Locked FOV value (if > 0) */
    float LockedFOV;
    
    /** Camera style name */
    FString CameraStyle;
    
    // ========================================================================
    // View Limits
    // ========================================================================
    
    /** Minimum view pitch */
    float ViewPitchMin;
    
    /** Maximum view pitch */
    float ViewPitchMax;
    
    /** Minimum view yaw */
    float ViewYawMin;
    
    /** Maximum view yaw */
    float ViewYawMax;
    
    /** Minimum view roll */
    float ViewRollMin;
    
    /** Maximum view roll */
    float ViewRollMax;
    
    // ========================================================================
    // Flags
    // ========================================================================
    
    /** If true, use orthographic projection */
    uint8 bIsOrthographic : 1;
    
    /** If true, constrain aspect ratio by default */
    uint8 bDefaultConstrainAspectRatio : 1;
    
    /** If true, always apply modifiers even in debug modes */
    uint8 bAlwaysApplyModifiers : 1;
    
    /** If true, camera manager is initialized */
    uint8 bIsInitialized : 1;
};

} // namespace MonsterEngine
