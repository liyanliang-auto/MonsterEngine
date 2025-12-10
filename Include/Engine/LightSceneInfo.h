// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file LightSceneInfo.h
 * @brief Light scene information for the renderer
 * 
 * FLightSceneInfo is the renderer's internal state for a single ULightComponent.
 * It contains all the information needed to manage the light's interactions with
 * primitives and other scene elements. Following UE5's architecture.
 */

#include "SceneTypes.h"
#include "Containers/Array.h"

namespace MonsterEngine
{

// Forward declarations
class FScene;
class FLightSceneProxy;
class FLightPrimitiveInteraction;
class ULightComponent;
class FPrimitiveSceneInfo;

/**
 * Renderer's internal state for a single ULightComponent
 * 
 * FLightSceneInfo acts as the link between the game thread's ULightComponent
 * and the rendering thread's FLightSceneProxy. It manages the light's
 * registration with the scene and its interactions with primitives.
 */
class FLightSceneInfo
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /**
     * Constructor
     * @param InProxy The light scene proxy
     * @param bInVisible Whether the light is initially visible
     */
    FLightSceneInfo(FLightSceneProxy* InProxy, bool bInVisible = true);

    /** Destructor */
    ~FLightSceneInfo();

    // ========================================================================
    // Scene Registration
    // ========================================================================

    /**
     * Adds the light to the scene
     * Called on the render thread when the light is registered
     */
    void AddToScene();

    /**
     * Removes the light from the scene
     * Called on the render thread when the light is unregistered
     */
    void RemoveFromScene();

    // ========================================================================
    // Accessors
    // ========================================================================

    /** Get the light scene proxy */
    FLightSceneProxy* GetProxy() const { return Proxy; }

    /** Get the scene */
    FScene* GetScene() const { return Scene; }

    /** Set the scene */
    void SetScene(FScene* InScene) { Scene = InScene; }

    /** Get the light type */
    ELightType GetLightType() const;

    /** Get the light ID in the scene */
    int32 GetId() const { return Id; }

    /** Set the light ID */
    void SetId(int32 InId) { Id = InId; }

    // ========================================================================
    // Octree
    // ========================================================================

    /** Get the octree ID for this light */
    uint32 GetOctreeId() const { return OctreeId; }

    /** Set the octree ID */
    void SetOctreeId(uint32 InOctreeId) { OctreeId = InOctreeId; }

    // ========================================================================
    // Primitive Interactions
    // ========================================================================

    /**
     * Get the head of the primitive interaction list
     * This is a linked list of all primitives affected by this light
     */
    FLightPrimitiveInteraction* GetDynamicInteractionOftenMovingPrimitiveList() const 
    { 
        return DynamicInteractionOftenMovingPrimitiveList; 
    }

    /**
     * Get the head of the static primitive interaction list
     */
    FLightPrimitiveInteraction* GetDynamicInteractionStaticPrimitiveList() const 
    { 
        return DynamicInteractionStaticPrimitiveList; 
    }

    /**
     * Add a primitive interaction
     * @param Interaction The interaction to add
     */
    void AddInteraction(FLightPrimitiveInteraction* Interaction);

    /**
     * Remove a primitive interaction
     * @param Interaction The interaction to remove
     */
    void RemoveInteraction(FLightPrimitiveInteraction* Interaction);

    /**
     * Get all primitives affected by this light
     * @param OutPrimitives Output array of primitive scene infos
     */
    void GetAffectedPrimitives(TArray<FPrimitiveSceneInfo*>& OutPrimitives) const;

    /**
     * Check if this light affects a primitive
     * @param PrimitiveSceneInfo The primitive to check
     * @return True if the light affects the primitive
     */
    bool AffectsPrimitive(const FPrimitiveSceneInfo* PrimitiveSceneInfo) const;

    // ========================================================================
    // Visibility
    // ========================================================================

    /** Check if the light is visible */
    bool IsVisible() const { return bVisible; }

    /** Set visibility */
    void SetVisible(bool bInVisible) { bVisible = bInVisible; }

    /** Check if the light is precomputed (static) */
    bool IsPrecomputedLightingValid() const { return bPrecomputedLightingValid; }

    // ========================================================================
    // Shadow
    // ========================================================================

    /** Check if the light casts shadows */
    bool CastsShadow() const;

    /** Check if the light casts static shadows */
    bool CastsStaticShadow() const;

    /** Check if the light casts dynamic shadows */
    bool CastsDynamicShadow() const;

    /** Get the shadow map channel */
    int32 GetShadowMapChannel() const { return ShadowMapChannel; }

    /** Set the shadow map channel */
    void SetShadowMapChannel(int32 InChannel) { ShadowMapChannel = InChannel; }

    // ========================================================================
    // Bounds
    // ========================================================================

    /** Get the light's bounding sphere */
    FBoxSphereBounds GetBoundingSphere() const;

    /** Check if the light affects a bounding box */
    bool AffectsBounds(const FBoxSphereBounds& Bounds) const;

    // ========================================================================
    // Transform Update
    // ========================================================================

    /**
     * Updates the light's transform
     * Called when the light component's transform changes
     */
    void UpdateTransform();

    /**
     * Updates the light's color and brightness
     * Called when the light component's color or intensity changes
     */
    void UpdateColorAndBrightness();

public:
    // ========================================================================
    // Public Data
    // ========================================================================

    /** The light scene proxy */
    FLightSceneProxy* Proxy;

    /** The scene this light belongs to */
    FScene* Scene;

    /** ID in the scene's light array */
    int32 Id;

    /** ID in the scene's light octree */
    uint32 OctreeId;

    /** Shadow map channel for this light */
    int32 ShadowMapChannel;

    /** 
     * Linked list of dynamic interactions with primitives that move often
     * These are updated every frame
     */
    FLightPrimitiveInteraction* DynamicInteractionOftenMovingPrimitiveList;

    /**
     * Linked list of dynamic interactions with static primitives
     * These are only updated when the light or primitive changes
     */
    FLightPrimitiveInteraction* DynamicInteractionStaticPrimitiveList;

    /** Number of dynamic interactions */
    int32 NumDynamicInteractions;

    // ========================================================================
    // Flags
    // ========================================================================

    /** Whether the light is visible */
    uint8 bVisible : 1;

    /** Whether precomputed lighting is valid for this light */
    uint8 bPrecomputedLightingValid : 1;

    /** Whether the light is registered with the scene */
    uint8 bIsRegistered : 1;

    /** Whether the light needs to rebuild its interactions */
    uint8 bNeedsInteractionRebuild : 1;
};

} // namespace MonsterEngine
