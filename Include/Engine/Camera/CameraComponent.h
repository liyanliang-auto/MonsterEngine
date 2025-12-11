// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CameraComponent.h
 * @brief Camera component for actors
 * 
 * UCameraComponent represents a camera viewpoint that can be attached to actors.
 * It provides camera settings like FOV, projection mode, and post-process overrides.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/Camera/CameraComponent.h
 */

#include "Engine/Components/SceneComponent.h"
#include "Engine/Camera/CameraTypes.h"

namespace MonsterEngine
{

// Forward declarations
class AActor;

// ============================================================================
// UCameraComponent
// ============================================================================

/**
 * Camera component class
 * 
 * Represents a camera viewpoint and settings that can be attached to any actor.
 * The default behavior for an actor used as the camera view target is to look
 * for an attached camera component and use its location, rotation, and settings.
 * 
 * Features:
 * - Field of view control
 * - Perspective/Orthographic projection
 * - Post-process settings
 * - Pawn control rotation support
 * - VR/XR head tracking support
 * 
 * Reference UE5: UCameraComponent
 */
class UCameraComponent : public USceneComponent
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================
    
    /** Default constructor */
    UCameraComponent();
    
    /** Constructor with owner */
    explicit UCameraComponent(AActor* InOwner);
    
    /** Virtual destructor */
    virtual ~UCameraComponent();
    
    // ========================================================================
    // Component Lifecycle
    // ========================================================================
    
    /** Called when the component is registered */
    virtual void OnRegister() override;
    
    /** Called when the component is unregistered */
    virtual void OnUnregister() override;
    
    /** Called every frame to update the component */
    virtual void TickComponent(float DeltaTime) override;
    
    // ========================================================================
    // Camera Settings - Field of View
    // ========================================================================
    
    /**
     * Get the field of view in degrees
     * @return Field of view in degrees
     */
    float GetFieldOfView() const { return FieldOfView; }
    
    /**
     * Set the field of view
     * @param InFieldOfView New field of view in degrees
     */
    virtual void SetFieldOfView(float InFieldOfView) { FieldOfView = InFieldOfView; }
    
    // ========================================================================
    // Camera Settings - Orthographic
    // ========================================================================
    
    /**
     * Get the orthographic width
     * @return Orthographic width in world units
     */
    float GetOrthoWidth() const { return OrthoWidth; }
    
    /**
     * Set the orthographic width
     * @param InOrthoWidth New orthographic width
     */
    void SetOrthoWidth(float InOrthoWidth) { OrthoWidth = InOrthoWidth; }
    
    /**
     * Get the orthographic near clip plane
     * @return Near clip plane distance
     */
    float GetOrthoNearClipPlane() const { return OrthoNearClipPlane; }
    
    /**
     * Set the orthographic near clip plane
     * @param InOrthoNearClipPlane New near clip plane distance
     */
    void SetOrthoNearClipPlane(float InOrthoNearClipPlane) { OrthoNearClipPlane = InOrthoNearClipPlane; }
    
    /**
     * Get the orthographic far clip plane
     * @return Far clip plane distance
     */
    float GetOrthoFarClipPlane() const { return OrthoFarClipPlane; }
    
    /**
     * Set the orthographic far clip plane
     * @param InOrthoFarClipPlane New far clip plane distance
     */
    void SetOrthoFarClipPlane(float InOrthoFarClipPlane) { OrthoFarClipPlane = InOrthoFarClipPlane; }
    
    // ========================================================================
    // Camera Settings - Aspect Ratio
    // ========================================================================
    
    /**
     * Get the aspect ratio
     * @return Aspect ratio (width/height)
     */
    float GetAspectRatio() const { return AspectRatio; }
    
    /**
     * Set the aspect ratio
     * @param InAspectRatio New aspect ratio
     */
    void SetAspectRatio(float InAspectRatio) { AspectRatio = InAspectRatio; }
    
    /**
     * Check if aspect ratio is constrained
     * @return True if aspect ratio is constrained
     */
    bool IsAspectRatioConstrained() const { return bConstrainAspectRatio; }
    
    /**
     * Set whether to constrain aspect ratio
     * @param bInConstrainAspectRatio Whether to constrain
     */
    void SetConstraintAspectRatio(bool bInConstrainAspectRatio) { bConstrainAspectRatio = bInConstrainAspectRatio; }
    
    // ========================================================================
    // Camera Settings - Projection Mode
    // ========================================================================
    
    /**
     * Get the projection mode
     * @return Current projection mode
     */
    ECameraProjectionMode::Type GetProjectionMode() const { return ProjectionMode; }
    
    /**
     * Set the projection mode
     * @param InProjectionMode New projection mode
     */
    void SetProjectionMode(ECameraProjectionMode::Type InProjectionMode) { ProjectionMode = InProjectionMode; }
    
    // ========================================================================
    // Camera Settings - Post Process
    // ========================================================================
    
    /**
     * Get the post process blend weight
     * @return Post process blend weight (0-1)
     */
    float GetPostProcessBlendWeight() const { return PostProcessBlendWeight; }
    
    /**
     * Set the post process blend weight
     * @param InPostProcessBlendWeight New blend weight
     */
    void SetPostProcessBlendWeight(float InPostProcessBlendWeight) { PostProcessBlendWeight = InPostProcessBlendWeight; }
    
    // ========================================================================
    // Camera Settings - LOD
    // ========================================================================
    
    /**
     * Check if FOV is used for LOD calculations
     * @return True if FOV is used for LOD
     */
    bool GetUseFieldOfViewForLOD() const { return bUseFieldOfViewForLOD; }
    
    /**
     * Set whether to use FOV for LOD calculations
     * @param bInUseFieldOfViewForLOD Whether to use FOV for LOD
     */
    void SetUseFieldOfViewForLOD(bool bInUseFieldOfViewForLOD) { bUseFieldOfViewForLOD = bInUseFieldOfViewForLOD; }
    
    // ========================================================================
    // Camera Settings - Pawn Control
    // ========================================================================
    
    /**
     * Check if using pawn control rotation
     * @return True if using pawn control rotation
     */
    bool GetUsePawnControlRotation() const { return bUsePawnControlRotation; }
    
    /**
     * Set whether to use pawn control rotation
     * @param bInUsePawnControlRotation Whether to use pawn control rotation
     */
    void SetUsePawnControlRotation(bool bInUsePawnControlRotation) { bUsePawnControlRotation = bInUsePawnControlRotation; }
    
    // ========================================================================
    // Camera View
    // ========================================================================
    
    /**
     * Get the camera view for this component
     * This is the main method called by the camera system to get the view.
     * 
     * @param DeltaTime Time since last update
     * @param DesiredView Output view info to fill
     */
    virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView);
    
    /**
     * Check if this camera component is active
     * @return True if active
     */
    bool IsActive() const { return bIsActive; }
    
    /**
     * Set whether this camera component is active
     * @param bNewActive Whether to activate
     */
    void SetActive(bool bNewActive) { bIsActive = bNewActive; }
    
    // ========================================================================
    // Additive Offset
    // ========================================================================
    
    /**
     * Add an additive offset to the camera
     * @param Transform Additive transform offset
     * @param FOVOffset Additive FOV offset
     */
    void AddAdditiveOffset(const FTransform& Transform, float FOVOffset);
    
    /**
     * Clear any additive offset
     */
    void ClearAdditiveOffset();
    
    /**
     * Get the current additive offset
     * @param OutAdditiveOffset Output additive transform
     * @param OutAdditiveFOVOffset Output additive FOV offset
     */
    void GetAdditiveOffset(FTransform& OutAdditiveOffset, float& OutAdditiveFOVOffset) const;
    
    // ========================================================================
    // Camera Cut Notification
    // ========================================================================
    
    /**
     * Notify that the camera was cut to
     * Called when switching to this camera without blending
     */
    virtual void NotifyCameraCut();

protected:
    // ========================================================================
    // Protected Methods
    // ========================================================================
    
    /**
     * Handle XR/VR camera tracking
     */
    virtual void HandleXRCamera();
    
    /**
     * Check if this is an XR head-tracked camera
     * @return True if XR head-tracked
     */
    bool IsXRHeadTrackedCamera() const;

protected:
    // ========================================================================
    // Camera Settings
    // ========================================================================
    
    /**
     * Horizontal field of view in degrees (perspective mode)
     * Range: 5-170 degrees
     */
    float FieldOfView;
    
    /** Orthographic view width in world units */
    float OrthoWidth;
    
    /** Orthographic near clip plane distance */
    float OrthoNearClipPlane;
    
    /** Orthographic far clip plane distance */
    float OrthoFarClipPlane;
    
    /** Aspect ratio (width/height) */
    float AspectRatio;
    
    /** Post process blend weight (0-1) */
    float PostProcessBlendWeight;
    
    /** Projection mode (perspective or orthographic) */
    ECameraProjectionMode::Type ProjectionMode;
    
    // ========================================================================
    // Additive Offset
    // ========================================================================
    
    /** Additive offset transform */
    FTransform AdditiveOffset;
    
    /** Additive FOV offset */
    float AdditiveFOVOffset;
    
    // ========================================================================
    // Flags
    // ========================================================================
    
    /** If true, constrain aspect ratio with black bars */
    uint8 bConstrainAspectRatio : 1;
    
    /** If true, use FOV for LOD calculations */
    uint8 bUseFieldOfViewForLOD : 1;
    
    /** If true, use pawn control rotation */
    uint8 bUsePawnControlRotation : 1;
    
    /** If true, lock camera to HMD */
    uint8 bLockToHmd : 1;
    
    /** If true, use additive offset */
    uint8 bUseAdditiveOffset : 1;
    
    /** If true, this camera component is active */
    uint8 bIsActive : 1;
};

} // namespace MonsterEngine
