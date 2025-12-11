// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CameraModifier.h
 * @brief Base class for camera modifiers
 * 
 * FCameraModifier is the base class for objects that can modify the final
 * camera properties after being computed by the camera manager.
 * Examples include camera shakes, animations, and other effects.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/Camera/CameraModifier.h
 */

#include "Core/CoreTypes.h"
#include "Engine/Camera/CameraTypes.h"

namespace MonsterEngine
{

// Forward declarations
class FCameraManager;
class AActor;

// ============================================================================
// FCameraModifier
// ============================================================================

/**
 * Camera modifier base class
 * 
 * Camera modifiers can adjust the final camera properties after being
 * computed by the camera manager. They are processed in priority order
 * and can be used for effects like:
 * - Camera shakes
 * - Camera animations
 * - View bobbing
 * - Recoil effects
 * 
 * Modifiers support alpha blending for smooth enable/disable transitions.
 * 
 * Reference UE5: UCameraModifier
 */
class FCameraModifier
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================
    
    /** Default constructor */
    FCameraModifier();
    
    /** Virtual destructor */
    virtual ~FCameraModifier();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Called when the modifier is added to a camera manager
     * @param Camera The camera manager this modifier is added to
     */
    virtual void AddedToCamera(FCameraManager* Camera);
    
    // ========================================================================
    // Modifier Application
    // ========================================================================
    
    /**
     * Modify the camera view
     * This is the main method that applies the modifier's effect.
     * 
     * @param DeltaTime Time since last update in seconds
     * @param InOutPOV The camera POV to modify (in/out)
     * @return True to stop processing further modifiers, false to continue
     */
    virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV);
    
    /**
     * Process view rotation updates
     * Called to give modifiers a chance to adjust view rotation.
     * 
     * @param ViewTarget Current view target actor
     * @param DeltaTime Time since last update
     * @param OutViewRotation View rotation to modify (in/out)
     * @param OutDeltaRot Rotation delta to modify (in/out)
     * @return True to stop processing further modifiers
     */
    virtual bool ProcessViewRotation(AActor* ViewTarget, float DeltaTime, 
                                     FRotator& OutViewRotation, FRotator& OutDeltaRot);
    
    // ========================================================================
    // Enable / Disable
    // ========================================================================
    
    /**
     * Check if the modifier is disabled
     * @return True if disabled
     */
    virtual bool IsDisabled() const { return bDisabled; }
    
    /**
     * Disable the modifier
     * @param bImmediate If true, disable immediately without blend out
     */
    virtual void DisableModifier(bool bImmediate = false);
    
    /**
     * Enable the modifier
     */
    virtual void EnableModifier();
    
    /**
     * Toggle the modifier enabled/disabled state
     */
    virtual void ToggleModifier();
    
    // ========================================================================
    // Alpha Blending
    // ========================================================================
    
    /**
     * Update the alpha blend value
     * @param DeltaTime Time since last update
     */
    virtual void UpdateAlpha(float DeltaTime);
    
    /**
     * Get the current alpha value
     * @return Current alpha (0-1)
     */
    float GetAlpha() const { return Alpha; }
    
    /**
     * Get the alpha blend in time
     * @return Blend in time in seconds
     */
    float GetAlphaInTime() const { return AlphaInTime; }
    
    /**
     * Set the alpha blend in time
     * @param InTime Blend in time in seconds
     */
    void SetAlphaInTime(float InTime) { AlphaInTime = InTime; }
    
    /**
     * Get the alpha blend out time
     * @return Blend out time in seconds
     */
    float GetAlphaOutTime() const { return AlphaOutTime; }
    
    /**
     * Set the alpha blend out time
     * @param OutTime Blend out time in seconds
     */
    void SetAlphaOutTime(float OutTime) { AlphaOutTime = OutTime; }
    
    // ========================================================================
    // Priority
    // ========================================================================
    
    /**
     * Get the modifier priority
     * Lower values = higher priority (processed first)
     * @return Priority value (0-255)
     */
    uint8 GetPriority() const { return Priority; }
    
    /**
     * Set the modifier priority
     * @param InPriority New priority value
     */
    void SetPriority(uint8 InPriority) { Priority = InPriority; }
    
    // ========================================================================
    // Exclusivity
    // ========================================================================
    
    /**
     * Check if this modifier is exclusive
     * Exclusive modifiers prevent other modifiers of the same priority from running
     * @return True if exclusive
     */
    bool IsExclusive() const { return bExclusive; }
    
    /**
     * Set whether this modifier is exclusive
     * @param bInExclusive Whether to be exclusive
     */
    void SetExclusive(bool bInExclusive) { bExclusive = bInExclusive; }
    
    // ========================================================================
    // View Target
    // ========================================================================
    
    /**
     * Get the current view target actor
     * @return The view target actor, or nullptr
     */
    virtual AActor* GetViewTarget() const;
    
    // ========================================================================
    // Debug
    // ========================================================================
    
    /**
     * Check if debug mode is enabled
     * @return True if debug mode is on
     */
    bool IsDebugEnabled() const { return bDebug; }
    
    /**
     * Set debug mode
     * @param bInDebug Whether to enable debug mode
     */
    void SetDebugEnabled(bool bInDebug) { bDebug = bInDebug; }

protected:
    // ========================================================================
    // Protected Methods
    // ========================================================================
    
    /**
     * Get the target alpha value for blending
     * @return Target alpha (0 if disabled, 1 if enabled)
     */
    virtual float GetTargetAlpha();
    
    /**
     * Native implementation of camera modification
     * Override this in derived classes to implement custom effects.
     * 
     * @param DeltaTime Time since last update
     * @param ViewLocation Current view location
     * @param ViewRotation Current view rotation
     * @param FOV Current field of view
     * @param NewViewLocation Output modified location
     * @param NewViewRotation Output modified rotation
     * @param NewFOV Output modified FOV
     */
    virtual void ModifyCamera(float DeltaTime, 
                              FVector ViewLocation, FRotator ViewRotation, float FOV,
                              FVector& NewViewLocation, FRotator& NewViewRotation, float& NewFOV);

protected:
    // ========================================================================
    // Camera Reference
    // ========================================================================
    
    /** The camera manager that owns this modifier */
    FCameraManager* CameraOwner;
    
    // ========================================================================
    // Alpha Blending
    // ========================================================================
    
    /** Current blend alpha (0-1) */
    float Alpha;
    
    /** Time to blend in (seconds) */
    float AlphaInTime;
    
    /** Time to blend out (seconds) */
    float AlphaOutTime;
    
    // ========================================================================
    // Priority
    // ========================================================================
    
    /** Priority value (0 = highest, 255 = lowest) */
    uint8 Priority;
    
    // ========================================================================
    // Flags
    // ========================================================================
    
    /** If true, enable debug visualization */
    uint8 bDebug : 1;
    
    /** If true, no other modifiers of same priority allowed */
    uint8 bExclusive : 1;
    
    /** If true, modifier is disabled */
    uint8 bDisabled : 1;
    
    /** If true, modifier will disable itself when blend out completes */
    uint8 bPendingDisable : 1;
};

// ============================================================================
// Camera Shake Play Space
// ============================================================================

/**
 * Camera shake play space enumeration
 */
enum class ECameraShakePlaySpace : uint8
{
    /** Shake is applied in camera local space */
    CameraLocal,
    
    /** Shake is applied in world space */
    World,
    
    /** Shake is applied in user-defined space */
    UserDefined
};

// ============================================================================
// FCameraModifier_CameraShake
// ============================================================================

/**
 * Camera shake modifier
 * 
 * Applies camera shake effects to the view.
 * This is a specialized modifier for handling camera shakes.
 * 
 * Reference UE5: UCameraModifier_CameraShake
 */
class FCameraModifier_CameraShake : public FCameraModifier
{
public:
    FCameraModifier_CameraShake();
    virtual ~FCameraModifier_CameraShake();
    
    /**
     * Add a camera shake instance
     * @param ShakeScale Scale of the shake (1.0 = full)
     * @param PlaySpace Play space for the shake
     * @param UserPlaySpaceRot User-defined play space rotation
     */
    virtual void AddCameraShake(float ShakeScale, 
                                ECameraShakePlaySpace PlaySpace = ECameraShakePlaySpace::CameraLocal,
                                const FRotator& UserPlaySpaceRot = FRotator::ZeroRotator);
    
    /**
     * Stop all camera shakes
     * @param bImmediate If true, stop immediately without blend out
     */
    virtual void StopAllCameraShakes(bool bImmediate = false);
    
    // FCameraModifier interface
    virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

protected:
    // TODO: Add shake instance tracking when camera shake system is implemented
};

} // namespace MonsterEngine
