// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file LightPrimitiveInteraction.h
 * @brief Light-primitive interaction management
 * 
 * FLightPrimitiveInteraction represents the interaction between a light and a primitive.
 * It manages shadow casting, light mapping, and other lighting-related state.
 * Following UE5's light-primitive interaction architecture.
 */

#include "SceneTypes.h"

namespace MonsterEngine
{

// Forward declarations
class FLightSceneInfo;
class FPrimitiveSceneInfo;

/**
 * Represents the interaction between a light and a primitive
 * 
 * This class manages the relationship between lights and primitives,
 * including shadow casting state, light map information, and linked
 * list pointers for efficient iteration.
 */
class FLightPrimitiveInteraction
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /**
     * Constructor
     * @param InLightSceneInfo The light in this interaction
     * @param InPrimitiveSceneInfo The primitive in this interaction
     * @param bInIsDynamic Whether this is a dynamic interaction
     */
    FLightPrimitiveInteraction(
        FLightSceneInfo* InLightSceneInfo,
        FPrimitiveSceneInfo* InPrimitiveSceneInfo,
        bool bInIsDynamic);

    /** Destructor */
    ~FLightPrimitiveInteraction();

    // ========================================================================
    // Factory Methods
    // ========================================================================

    /**
     * Creates a new light-primitive interaction
     * @param LightSceneInfo The light
     * @param PrimitiveSceneInfo The primitive
     * @return The new interaction, or nullptr if the light doesn't affect the primitive
     */
    static FLightPrimitiveInteraction* Create(
        FLightSceneInfo* LightSceneInfo,
        FPrimitiveSceneInfo* PrimitiveSceneInfo);

    /**
     * Destroys a light-primitive interaction
     * @param Interaction The interaction to destroy
     */
    static void Destroy(FLightPrimitiveInteraction* Interaction);

    // ========================================================================
    // Accessors
    // ========================================================================

    /** Get the light in this interaction */
    FLightSceneInfo* GetLight() const { return LightSceneInfo; }

    /** Get the primitive in this interaction */
    FPrimitiveSceneInfo* GetPrimitive() const { return PrimitiveSceneInfo; }

    // ========================================================================
    // Linked List Navigation - Light's Primitive List
    // ========================================================================

    /** Get the next interaction in the light's primitive list */
    FLightPrimitiveInteraction* GetNextPrimitive() const { return NextPrimitive; }

    /** Get the previous interaction in the light's primitive list */
    FLightPrimitiveInteraction* GetPrevPrimitive() const { return PrevPrimitiveLink ? *PrevPrimitiveLink : nullptr; }

    /** Set the next interaction in the light's primitive list */
    void SetNextPrimitive(FLightPrimitiveInteraction* Next) { NextPrimitive = Next; }

    /** Get the address of the previous link pointer */
    FLightPrimitiveInteraction** GetPrevPrimitiveLink() const { return PrevPrimitiveLink; }

    /** Set the previous link pointer */
    void SetPrevPrimitiveLink(FLightPrimitiveInteraction** PrevLink) { PrevPrimitiveLink = PrevLink; }

    // ========================================================================
    // Linked List Navigation - Primitive's Light List
    // ========================================================================

    /** Get the next interaction in the primitive's light list */
    FLightPrimitiveInteraction* GetNextLight() const { return NextLight; }

    /** Get the previous interaction in the primitive's light list */
    FLightPrimitiveInteraction* GetPrevLight() const { return PrevLightLink ? *PrevLightLink : nullptr; }

    /** Set the next interaction in the primitive's light list */
    void SetNextLight(FLightPrimitiveInteraction* Next) { NextLight = Next; }

    /** Get the address of the previous link pointer */
    FLightPrimitiveInteraction** GetPrevLightLink() const { return PrevLightLink; }

    /** Set the previous link pointer */
    void SetPrevLightLink(FLightPrimitiveInteraction** PrevLink) { PrevLightLink = PrevLink; }

    // ========================================================================
    // Shadow State
    // ========================================================================

    /** Check if this interaction casts shadows */
    bool HasShadow() const { return bCastShadow; }

    /** Set whether this interaction casts shadows */
    void SetCastShadow(bool bInCastShadow) { bCastShadow = bInCastShadow; }

    /** Check if this interaction uses static shadowing */
    bool HasStaticShadowing() const { return bHasStaticShadowing; }

    /** Check if this interaction uses dynamic shadowing */
    bool HasDynamicShadowing() const { return bHasDynamicShadowing; }

    /** Check if this interaction is uncached (needs to be rebuilt) */
    bool IsUncached() const { return bUncached; }

    /** Mark this interaction as uncached */
    void SetUncached(bool bInUncached) { bUncached = bInUncached; }

    // ========================================================================
    // Light Map State
    // ========================================================================

    /** Check if this interaction uses a light map */
    bool HasLightMap() const { return bHasLightMap; }

    /** Set whether this interaction uses a light map */
    void SetHasLightMap(bool bInHasLightMap) { bHasLightMap = bInHasLightMap; }

    /** Get the light map interaction type */
    // ELightMapInteractionType GetLightMapInteractionType() const { return LightMapInteractionType; }

    // ========================================================================
    // Dynamic State
    // ========================================================================

    /** Check if this is a dynamic interaction */
    bool IsDynamic() const { return bIsDynamic; }

    /** Check if the primitive is often moving */
    bool IsPrimitiveOftenMoving() const { return bIsPrimitiveOftenMoving; }

    /** Set whether the primitive is often moving */
    void SetPrimitiveOftenMoving(bool bInOftenMoving) { bIsPrimitiveOftenMoving = bInOftenMoving; }

    // ========================================================================
    // Linked List Management
    // ========================================================================

    /**
     * Adds this interaction to the light's primitive list
     * @param ListHead Reference to the head of the list
     */
    void AddToLightPrimitiveList(FLightPrimitiveInteraction*& ListHead);

    /**
     * Removes this interaction from the light's primitive list
     */
    void RemoveFromLightPrimitiveList();

    /**
     * Adds this interaction to the primitive's light list
     * @param ListHead Reference to the head of the list
     */
    void AddToPrimitiveLightList(FLightPrimitiveInteraction*& ListHead);

    /**
     * Removes this interaction from the primitive's light list
     */
    void RemoveFromPrimitiveLightList();

private:
    // ========================================================================
    // Private Data
    // ========================================================================

    /** The light in this interaction */
    FLightSceneInfo* LightSceneInfo;

    /** The primitive in this interaction */
    FPrimitiveSceneInfo* PrimitiveSceneInfo;

    // ========================================================================
    // Linked List Pointers - Light's Primitive List
    // ========================================================================

    /** Next interaction in the light's list of primitives */
    FLightPrimitiveInteraction* NextPrimitive;

    /** Pointer to the previous interaction's NextPrimitive pointer (or list head) */
    FLightPrimitiveInteraction** PrevPrimitiveLink;

    // ========================================================================
    // Linked List Pointers - Primitive's Light List
    // ========================================================================

    /** Next interaction in the primitive's list of lights */
    FLightPrimitiveInteraction* NextLight;

    /** Pointer to the previous interaction's NextLight pointer (or list head) */
    FLightPrimitiveInteraction** PrevLightLink;

    // ========================================================================
    // Flags
    // ========================================================================

    /** Whether this interaction casts shadows */
    uint8 bCastShadow : 1;

    /** Whether this interaction has static shadowing */
    uint8 bHasStaticShadowing : 1;

    /** Whether this interaction has dynamic shadowing */
    uint8 bHasDynamicShadowing : 1;

    /** Whether this interaction uses a light map */
    uint8 bHasLightMap : 1;

    /** Whether this is a dynamic interaction */
    uint8 bIsDynamic : 1;

    /** Whether the primitive is often moving */
    uint8 bIsPrimitiveOftenMoving : 1;

    /** Whether this interaction is uncached */
    uint8 bUncached : 1;

    /** Whether this interaction is self-shadowing only */
    uint8 bSelfShadowOnly : 1;
};

} // namespace MonsterEngine
