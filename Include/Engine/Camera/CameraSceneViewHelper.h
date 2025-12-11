// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CameraSceneViewHelper.h
 * @brief Helper class for integrating camera system with scene rendering
 * 
 * This file provides utilities to convert camera view information
 * into scene view initialization options for rendering.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/Camera/CameraTypes.cpp
 *                Engine/Source/Runtime/Engine/Private/SceneView.cpp
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Math/MonsterMath.h"
#include "Engine/Camera/CameraTypes.h"

namespace MonsterEngine
{

// Forward declarations
class FSceneViewFamily;
struct FSceneViewInitOptions;
struct FIntRect;
class FCameraManager;

/**
 * Helper class for converting camera data to scene view data
 * 
 * This class provides static methods to bridge the gap between
 * the camera system (FMinimalViewInfo, FCameraManager) and
 * the rendering system (FSceneView, FSceneViewInitOptions).
 */
class FCameraSceneViewHelper
{
public:
    /**
     * Initialize scene view options from minimal view info
     * 
     * Converts camera view information into scene view initialization options
     * that can be used to create an FSceneView for rendering.
     * 
     * @param OutViewInitOptions - Output view initialization options
     * @param ViewInfo - Source camera view information
     * @param ViewFamily - The view family this view belongs to
     * @param ViewRect - The viewport rectangle
     * @param PlayerIndex - Player index for this view (-1 if not a player view)
     */
    static void InitViewOptionsFromMinimalViewInfo(
        FSceneViewInitOptions& OutViewInitOptions,
        const FMinimalViewInfo& ViewInfo,
        const FSceneViewFamily* ViewFamily,
        const FIntRect& ViewRect,
        int32 PlayerIndex = INDEX_NONE
    );
    
    /**
     * Calculate projection matrix from minimal view info
     * 
     * @param ViewInfo - Source camera view information
     * @param ViewRect - The viewport rectangle (for aspect ratio)
     * @return The projection matrix
     */
    static FMatrix CalculateProjectionMatrix(
        const FMinimalViewInfo& ViewInfo,
        const FIntRect& ViewRect
    );
    
    /**
     * Calculate view rotation matrix from minimal view info
     * 
     * @param ViewInfo - Source camera view information
     * @return The view rotation matrix
     */
    static FMatrix CalculateViewRotationMatrix(const FMinimalViewInfo& ViewInfo);
    
    /**
     * Apply camera manager view to scene view options
     * 
     * Gets the current camera view from the camera manager and
     * initializes scene view options for rendering.
     * 
     * @param OutViewInitOptions - Output view initialization options
     * @param CameraManager - The camera manager to get view from
     * @param ViewFamily - The view family this view belongs to
     * @param ViewRect - The viewport rectangle
     * @param PlayerIndex - Player index for this view
     */
    static void ApplyCameraManagerToViewOptions(
        FSceneViewInitOptions& OutViewInitOptions,
        const FCameraManager* CameraManager,
        const FSceneViewFamily* ViewFamily,
        const FIntRect& ViewRect,
        int32 PlayerIndex = 0
    );
    
    /**
     * Calculate constrained view rect based on aspect ratio
     * 
     * If the camera has constrained aspect ratio enabled, this calculates
     * the actual view rectangle with letterboxing/pillarboxing.
     * 
     * @param ViewInfo - Source camera view information
     * @param UnconstrainedViewRect - The unconstrained viewport rectangle
     * @return The constrained view rectangle
     */
    static FIntRect CalculateConstrainedViewRect(
        const FMinimalViewInfo& ViewInfo,
        const FIntRect& UnconstrainedViewRect
    );
    
    /**
     * Calculate effective FOV considering aspect ratio
     * 
     * Adjusts the FOV based on the actual viewport aspect ratio
     * compared to the desired aspect ratio.
     * 
     * @param ViewInfo - Source camera view information
     * @param ViewRect - The viewport rectangle
     * @return The effective FOV in degrees
     */
    static float CalculateEffectiveFOV(
        const FMinimalViewInfo& ViewInfo,
        const FIntRect& ViewRect
    );
    
private:
    /** Private constructor - static class only */
    FCameraSceneViewHelper() = delete;
};

} // namespace MonsterEngine
