// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CameraTypes.h
 * @brief Camera type definitions and structures
 * 
 * This file defines core camera types used throughout the engine:
 * - ECameraProjectionMode: Perspective/Orthographic projection
 * - FMinimalViewInfo: Camera view information structure
 * - FViewTarget: View target structure
 * - FViewTargetTransitionParams: View target transition parameters
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/Camera/CameraTypes.h
 */

#include "Core/CoreTypes.h"
#include "Math/MonsterMath.h"

namespace MonsterEngine
{

// Forward declarations
class AActor;
class APlayerController;

// ============================================================================
// Camera Projection Mode
// ============================================================================

/**
 * Camera projection mode enumeration
 */
namespace ECameraProjectionMode
{
    enum Type : uint8
    {
        /** Perspective projection (3D with depth) */
        Perspective = 0,
        
        /** Orthographic projection (2D, no perspective) */
        Orthographic = 1
    };
}

// ============================================================================
// FMinimalViewInfo
// ============================================================================

/**
 * Minimal view information structure
 * 
 * Contains all the essential camera parameters needed for rendering.
 * This is the primary data structure passed between camera components
 * and the rendering system.
 * 
 * Reference UE5: FMinimalViewInfo in CameraTypes.h
 */
struct FMinimalViewInfo
{
    // ========================================================================
    // Transform
    // ========================================================================
    
    /** Camera location in world space */
    FVector Location;
    
    /** Camera rotation in world space */
    FRotator Rotation;
    
    // ========================================================================
    // Projection Settings
    // ========================================================================
    
    /** Horizontal field of view in degrees (perspective mode) */
    float FOV;
    
    /** Desired FOV before any adjustments */
    float DesiredFOV;
    
    /** Orthographic view width in world units (orthographic mode) */
    float OrthoWidth;
    
    /** Near clip plane distance for orthographic projection */
    float OrthoNearClipPlane;
    
    /** Far clip plane distance for orthographic projection */
    float OrthoFarClipPlane;
    
    /** Near clip plane distance for perspective projection (-1 uses global default) */
    float PerspectiveNearClipPlane;
    
    /** Aspect ratio (Width / Height) */
    float AspectRatio;
    
    /** If true, add black bars to maintain aspect ratio */
    uint32 bConstrainAspectRatio : 1;
    
    /** If true, use FOV when computing mesh LOD */
    uint32 bUseFieldOfViewForLOD : 1;
    
    /** Projection mode (perspective or orthographic) */
    ECameraProjectionMode::Type ProjectionMode;
    
    // ========================================================================
    // Post Process
    // ========================================================================
    
    /** Blend weight for post process settings (0-1) */
    float PostProcessBlendWeight;
    
    // TODO: Add FPostProcessSettings when implemented
    // FPostProcessSettings PostProcessSettings;
    
    // ========================================================================
    // Off-Center Projection
    // ========================================================================
    
    /** Off-axis projection offset as proportion of screen dimensions */
    float OffCenterProjectionOffsetX;
    float OffCenterProjectionOffsetY;
    
    // ========================================================================
    // Previous Frame Data
    // ========================================================================
    
    /** Optional previous frame transform for motion blur */
    bool bHasPreviousViewTransform;
    FTransform PreviousViewTransform;
    
    // ========================================================================
    // Constructor
    // ========================================================================
    
    FMinimalViewInfo()
        : Location(FVector::ZeroVector)
        , Rotation(FRotator::ZeroRotator)
        , FOV(90.0f)
        , DesiredFOV(90.0f)
        , OrthoWidth(512.0f)
        , OrthoNearClipPlane(0.0f)
        , OrthoFarClipPlane(100000.0f)
        , PerspectiveNearClipPlane(-1.0f)
        , AspectRatio(1.33333333f)
        , bConstrainAspectRatio(false)
        , bUseFieldOfViewForLOD(true)
        , ProjectionMode(ECameraProjectionMode::Perspective)
        , PostProcessBlendWeight(0.0f)
        , OffCenterProjectionOffsetX(0.0f)
        , OffCenterProjectionOffsetY(0.0f)
        , bHasPreviousViewTransform(false)
        , PreviousViewTransform()
    {
    }
    
    // ========================================================================
    // Methods
    // ========================================================================
    
    /**
     * Check if this view info equals another
     * @param Other The other view info to compare
     * @return True if equal
     */
    bool Equals(const FMinimalViewInfo& Other) const;
    
    /**
     * Blend this view info with another
     * @param OtherInfo The other view info to blend with
     * @param OtherWeight Weight of the other view info (0-1)
     */
    void BlendViewInfo(FMinimalViewInfo& OtherInfo, float OtherWeight);
    
    /**
     * Apply a blend weight to this view info
     * @param Weight The weight to apply
     */
    void ApplyBlendWeight(float Weight);
    
    /**
     * Add a weighted view info to this one
     * @param OtherView The other view info to add
     * @param Weight The weight of the other view
     */
    void AddWeightedViewInfo(const FMinimalViewInfo& OtherView, float Weight);
    
    /**
     * Calculate the projection matrix for this view info
     * @return The projection matrix
     */
    FMatrix CalculateProjectionMatrix() const;
    
    /**
     * Get the final perspective near clip plane
     * Uses PerspectiveNearClipPlane if positive, otherwise uses global default
     * @return The near clip plane distance
     */
    float GetFinalPerspectiveNearClipPlane() const;
    
    /**
     * Calculate the view rotation matrix from the rotation
     * @return The view rotation matrix
     */
    FMatrix CalculateViewRotationMatrix() const;
};

// ============================================================================
// View Target Blend Function
// ============================================================================

/**
 * Blend function types for view target transitions
 */
enum class EViewTargetBlendFunction : uint8
{
    /** Linear interpolation */
    Linear,
    
    /** Cubic interpolation (smooth) */
    Cubic,
    
    /** Ease in (accelerate) */
    EaseIn,
    
    /** Ease out (decelerate) */
    EaseOut,
    
    /** Ease in and out */
    EaseInOut,
    
    /** Pre-blended (no engine blending) */
    PreBlended,
    
    Max
};

// ============================================================================
// FViewTargetTransitionParams
// ============================================================================

/**
 * Parameters for view target transitions
 * 
 * Defines how to blend between view targets when switching cameras.
 * 
 * Reference UE5: FViewTargetTransitionParams
 */
struct FViewTargetTransitionParams
{
    /** Total duration of blend in seconds (0 = instant) */
    float BlendTime;
    
    /** Blend function to use */
    EViewTargetBlendFunction BlendFunction;
    
    /** Exponent for blend functions that use it */
    float BlendExp;
    
    /** If true, lock outgoing view target to last frame's POV */
    uint32 bLockOutgoing : 1;
    
    FViewTargetTransitionParams()
        : BlendTime(0.0f)
        , BlendFunction(EViewTargetBlendFunction::Cubic)
        , BlendExp(2.0f)
        , bLockOutgoing(false)
    {
    }
    
    /**
     * Get the blend alpha for a given time percentage
     * @param TimePct Time percentage (0-1)
     * @return Blend alpha value
     */
    float GetBlendAlpha(float TimePct) const;
};

// ============================================================================
// FViewTarget
// ============================================================================

/**
 * View target structure
 * 
 * Associates a target actor with computed camera POV.
 * The camera manager uses this to track what the camera is looking at.
 * 
 * Reference UE5: FTViewTarget
 */
struct FViewTarget
{
    /** Target actor used to compute POV */
    AActor* Target;
    
    /** Computed point of view */
    FMinimalViewInfo POV;
    
    FViewTarget()
        : Target(nullptr)
        , POV()
    {
    }
    
    /**
     * Set a new target actor
     * @param NewTarget The new target actor
     */
    void SetNewTarget(AActor* NewTarget);
    
    /**
     * Check if this view target equals another
     * @param Other The other view target to compare
     * @return True if equal
     */
    bool Equal(const FViewTarget& Other) const;
    
    /**
     * Validate the view target
     * @param OwningController The owning player controller
     */
    void CheckViewTarget(APlayerController* OwningController);
};

// ============================================================================
// FCameraCacheEntry
// ============================================================================

/**
 * Cached camera POV entry
 * 
 * Stores a camera POV with timestamp for caching purposes.
 */
struct FCameraCacheEntry
{
    /** World time when this entry was created */
    float TimeStamp;
    
    /** Cached camera POV */
    FMinimalViewInfo POV;
    
    FCameraCacheEntry()
        : TimeStamp(0.0f)
        , POV()
    {
    }
};

} // namespace MonsterEngine
