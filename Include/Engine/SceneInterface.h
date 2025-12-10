// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SceneInterface.h
 * @brief Abstract interface for scene management
 * 
 * FSceneInterface defines the public interface for scene operations.
 * This allows the engine to interact with the scene without knowing
 * the concrete implementation details. Following UE5's scene architecture.
 */

#include "SceneTypes.h"
#include "Core/CoreMinimal.h"

namespace MonsterEngine
{

// Forward declarations
class UWorld;
class UPrimitiveComponent;
class ULightComponent;
class UDecalComponent;
class UReflectionCaptureComponent;
class USkyLightComponent;
class FPrimitiveSceneInfo;
class FLightSceneProxy;
class FSkyLightSceneProxy;

/**
 * Abstract interface for scene management
 * 
 * This interface defines all operations that can be performed on a scene.
 * The concrete implementation (FScene) is private to the renderer module.
 * Use GetRendererModule().AllocateScene() to create a scene instance.
 */
class FSceneInterface
{
public:
    /**
     * Constructor
     * @param InFeatureLevel The rendering feature level for this scene
     */
    FSceneInterface();

    /** Virtual destructor */
    virtual ~FSceneInterface() {}

    // ========================================================================
    // Primitive Management
    // ========================================================================

    /**
     * Adds a new primitive component to the scene
     * @param Primitive The primitive component to add
     */
    virtual void AddPrimitive(UPrimitiveComponent* Primitive) = 0;

    /**
     * Removes a primitive component from the scene
     * @param Primitive The primitive component to remove
     */
    virtual void RemovePrimitive(UPrimitiveComponent* Primitive) = 0;

    /**
     * Called when a primitive is being unregistered and will not be immediately re-registered
     * @param Primitive The primitive component being released
     */
    virtual void ReleasePrimitive(UPrimitiveComponent* Primitive) = 0;

    /**
     * Updates the transform of a primitive which has already been added to the scene
     * @param Primitive The primitive component to update
     */
    virtual void UpdatePrimitiveTransform(UPrimitiveComponent* Primitive) = 0;

    /**
     * Updates primitive attachment state
     * @param Primitive The primitive component to update
     */
    virtual void UpdatePrimitiveAttachment(UPrimitiveComponent* Primitive) = 0;

    /**
     * Finds the primitive with the associated index
     * @param PrimitiveIndex Index of the primitive to find
     * @return Pointer to the primitive scene info, or nullptr if not found
     */
    virtual FPrimitiveSceneInfo* GetPrimitiveSceneInfo(int32 PrimitiveIndex) = 0;

    // ========================================================================
    // Light Management
    // ========================================================================

    /**
     * Adds a new light component to the scene
     * @param Light The light component to add
     */
    virtual void AddLight(ULightComponent* Light) = 0;

    /**
     * Removes a light component from the scene
     * @param Light The light component to remove
     */
    virtual void RemoveLight(ULightComponent* Light) = 0;

    /**
     * Adds a new light component to the scene which is currently invisible
     * but needed for editor previewing
     * @param Light The light component to add
     */
    virtual void AddInvisibleLight(ULightComponent* Light) = 0;

    /**
     * Updates the transform of a light which has already been added to the scene
     * @param Light The light component to update
     */
    virtual void UpdateLightTransform(ULightComponent* Light) = 0;

    /**
     * Updates the color and brightness of a light which has already been added to the scene
     * @param Light The light component to update
     */
    virtual void UpdateLightColorAndBrightness(ULightComponent* Light) = 0;

    /**
     * Sets the sky light for the scene
     * @param Light The sky light proxy to set
     */
    virtual void SetSkyLight(FSkyLightSceneProxy* Light) = 0;

    /**
     * Disables the sky light for the scene
     * @param Light The sky light proxy to disable
     */
    virtual void DisableSkyLight(FSkyLightSceneProxy* Light) = 0;

    // ========================================================================
    // Decal Management
    // ========================================================================

    /**
     * Adds a new decal component to the scene
     * @param Component The decal component to add
     */
    virtual void AddDecal(UDecalComponent* Component) = 0;

    /**
     * Removes a decal component from the scene
     * @param Component The decal component to remove
     */
    virtual void RemoveDecal(UDecalComponent* Component) = 0;

    /**
     * Updates the transform of a decal which has already been added to the scene
     * @param Component The decal component to update
     */
    virtual void UpdateDecalTransform(UDecalComponent* Component) = 0;

    // ========================================================================
    // Reflection Capture Management
    // ========================================================================

    /**
     * Adds a reflection capture to the scene
     * @param Component The reflection capture component to add
     */
    virtual void AddReflectionCapture(UReflectionCaptureComponent* Component) {}

    /**
     * Removes a reflection capture from the scene
     * @param Component The reflection capture component to remove
     */
    virtual void RemoveReflectionCapture(UReflectionCaptureComponent* Component) {}

    /**
     * Updates a reflection capture's transform and re-captures the scene
     * @param Component The reflection capture component to update
     */
    virtual void UpdateReflectionCaptureTransform(UReflectionCaptureComponent* Component) {}

    // ========================================================================
    // Scene Queries
    // ========================================================================

    /**
     * Retrieves the lights interacting with the passed in primitive
     * @param Primitive The primitive to query
     * @param RelevantLights Output array of lights interacting with the primitive
     */
    virtual void GetRelevantLights(UPrimitiveComponent* Primitive, 
                                   TArray<const ULightComponent*>* RelevantLights) const = 0;

    /**
     * Indicates if hit proxies should be processed by this scene
     * @return true if hit proxies should be rendered in this scene
     */
    virtual bool RequiresHitProxies() const = 0;

    /**
     * Get the optional UWorld that is associated with this scene
     * @return UWorld instance used by this scene, or nullptr
     */
    virtual UWorld* GetWorld() const = 0;

    /**
     * Return the scene to be used for rendering
     * @return The render scene, or nullptr if rendering has been disabled
     */
    virtual FScene* GetRenderScene() { return nullptr; }

    /**
     * Called when the world is being cleaned up
     */
    virtual void OnWorldCleanup() {}

    // ========================================================================
    // Scene State
    // ========================================================================

    /**
     * @return True if there are any lights in the scene
     */
    virtual bool HasAnyLights() const = 0;

    /**
     * @return True if this is an editor scene
     */
    virtual bool IsEditorScene() const { return false; }

    /**
     * Updates all static draw lists
     */
    virtual void UpdateStaticDrawLists() {}

    /**
     * Release this scene and remove it from the rendering thread
     */
    virtual void Release() = 0;

    // ========================================================================
    // Fog Management
    // ========================================================================

    /**
     * Adds a new exponential height fog component to the scene
     * @param FogComponent The fog component to add
     */
    virtual void AddExponentialHeightFog(class UExponentialHeightFogComponent* FogComponent) = 0;

    /**
     * Removes an exponential height fog component from the scene
     * @param FogComponent The fog component to remove
     */
    virtual void RemoveExponentialHeightFog(class UExponentialHeightFogComponent* FogComponent) = 0;

    /**
     * @return True if there are any exponential height fog potentially enabled in the scene
     */
    virtual bool HasAnyExponentialHeightFog() const = 0;

    // ========================================================================
    // Frame Management
    // ========================================================================

    /**
     * Called at the start of each frame
     */
    virtual void StartFrame() {}

    /**
     * @return The current frame number
     */
    virtual uint32 GetFrameNumber() const { return 0; }

    /**
     * Increments the frame number
     */
    virtual void IncrementFrameNumber() {}

    // ========================================================================
    // World Offset
    // ========================================================================

    /**
     * Shifts scene data by provided delta
     * Called on world origin changes
     * @param InOffset Delta to shift scene by
     */
    virtual void ApplyWorldOffset(const FVector& InOffset) {}

protected:
    /** This scene's feature level */
    // ERHIFeatureLevel::Type FeatureLevel;
};

} // namespace MonsterEngine
