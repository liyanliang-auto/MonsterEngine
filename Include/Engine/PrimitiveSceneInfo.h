// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file PrimitiveSceneInfo.h
 * @brief Primitive scene information for the renderer
 * 
 * FPrimitiveSceneInfo is the renderer's internal state for a single UPrimitiveComponent.
 * It contains all the information needed to render the primitive and manage its
 * interactions with lights and other scene elements. Following UE5's architecture.
 */

#include "SceneTypes.h"
#include "Containers/Array.h"

namespace MonsterEngine
{

// Forward declarations
class FScene;
class FPrimitiveSceneProxy;
class FLightSceneInfo;
class FLightPrimitiveInteraction;
class UPrimitiveComponent;

/**
 * Renderer's internal state for a single UPrimitiveComponent
 * 
 * FPrimitiveSceneInfo acts as the link between the game thread's UPrimitiveComponent
 * and the rendering thread's FPrimitiveSceneProxy. It manages the primitive's
 * registration with the scene and its interactions with lights.
 */
class FPrimitiveSceneInfo
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /**
     * Constructor
     * @param InComponent The primitive component this info represents
     * @param InScene The scene this primitive belongs to
     */
    FPrimitiveSceneInfo(UPrimitiveComponent* InComponent, FScene* InScene);

    /** Destructor */
    ~FPrimitiveSceneInfo();

    // ========================================================================
    // Scene Registration
    // ========================================================================

    /**
     * Adds the primitive to the scene
     * Called on the render thread when the primitive is registered
     */
    void AddToScene();

    /**
     * Removes the primitive from the scene
     * Called on the render thread when the primitive is unregistered
     */
    void RemoveFromScene();

    // ========================================================================
    // Accessors
    // ========================================================================

    /** Get the primitive component */
    UPrimitiveComponent* GetComponent() const { return Component; }

    /** Get the scene proxy */
    FPrimitiveSceneProxy* GetProxy() const { return Proxy; }

    /** Set the scene proxy */
    void SetProxy(FPrimitiveSceneProxy* InProxy) { Proxy = InProxy; }

    /** Get the scene */
    FScene* GetScene() const { return Scene; }

    /** Get the primitive component ID */
    FPrimitiveComponentId GetPrimitiveComponentId() const { return PrimitiveComponentId; }

    // ========================================================================
    // Packed Index
    // ========================================================================

    /**
     * Get the packed index in the scene's primitive arrays
     * This is the index into Primitives, PrimitiveTransforms, etc.
     */
    int32 GetPackedIndex() const { return PackedIndex; }

    /**
     * Set the packed index
     * Called when the primitive is added to or removed from the scene
     */
    void SetPackedIndex(int32 InPackedIndex) { PackedIndex = InPackedIndex; }

    // ========================================================================
    // Octree
    // ========================================================================

    /**
     * Get the octree ID for this primitive
     * Used for spatial queries and culling
     */
    uint32 GetOctreeId() const { return OctreeId; }

    /**
     * Set the octree ID
     * Called when the primitive is added to the octree
     */
    void SetOctreeId(uint32 InOctreeId) { OctreeId = InOctreeId; }

    // ========================================================================
    // Light Interactions
    // ========================================================================

    /**
     * Get the head of the light interaction list
     * This is a linked list of all lights affecting this primitive
     */
    FLightPrimitiveInteraction* GetLightList() const { return LightList; }

    /**
     * Set the head of the light interaction list
     */
    void SetLightList(FLightPrimitiveInteraction* InLightList) { LightList = InLightList; }

    /**
     * Add a light interaction to this primitive
     * @param Interaction The interaction to add
     */
    void AddLightInteraction(FLightPrimitiveInteraction* Interaction);

    /**
     * Remove a light interaction from this primitive
     * @param Interaction The interaction to remove
     */
    void RemoveLightInteraction(FLightPrimitiveInteraction* Interaction);

    /**
     * Get all lights affecting this primitive
     * @param OutLights Output array of light scene infos
     */
    void GetRelevantLights(TArray<FLightSceneInfo*>& OutLights) const;

    // ========================================================================
    // Visibility
    // ========================================================================

    /** Check if the primitive needs a visibility check */
    bool NeedsVisibilityCheck() const { return bNeedsVisibilityCheck; }

    /** Set whether the primitive needs a visibility check */
    void SetNeedsVisibilityCheck(bool bInNeedsVisibilityCheck) { bNeedsVisibilityCheck = bInNeedsVisibilityCheck; }

    /** Check if the primitive is visible in the last frame */
    bool WasRecentlyVisible() const { return bWasRecentlyVisible; }

    /** Set whether the primitive was recently visible */
    void SetWasRecentlyVisible(bool bInWasRecentlyVisible) { bWasRecentlyVisible = bInWasRecentlyVisible; }

    // ========================================================================
    // Static Mesh Elements
    // ========================================================================

    /** Check if the primitive has static mesh elements */
    bool HasStaticMeshElements() const { return bHasStaticMeshElements; }

    /** Set whether the primitive has static mesh elements */
    void SetHasStaticMeshElements(bool bInHasStaticMeshElements) { bHasStaticMeshElements = bInHasStaticMeshElements; }

    // ========================================================================
    // Attachment
    // ========================================================================

    /** Get the parent primitive for attachment */
    FPrimitiveSceneInfo* GetAttachmentParent() const { return AttachmentParent; }

    /** Set the parent primitive for attachment */
    void SetAttachmentParent(FPrimitiveSceneInfo* InParent) { AttachmentParent = InParent; }

    /** Get the attachment root */
    FPrimitiveSceneInfo* GetAttachmentRoot() const { return AttachmentRoot; }

    /** Set the attachment root */
    void SetAttachmentRoot(FPrimitiveSceneInfo* InRoot) { AttachmentRoot = InRoot; }

    /** Get the attachment group index */
    int32 GetAttachmentGroupIndex() const { return AttachmentGroupIndex; }

    /** Set the attachment group index */
    void SetAttachmentGroupIndex(int32 InIndex) { AttachmentGroupIndex = InIndex; }

    // ========================================================================
    // Transform Update
    // ========================================================================

    /**
     * Updates the primitive's transform
     * @param NewLocalToWorld The new local to world transform
     * @param NewBounds The new world-space bounds
     */
    void UpdateTransform(const FMatrix& NewLocalToWorld, const FBoxSphereBounds& NewBounds);

    /**
     * Marks the primitive as needing a static mesh update
     */
    void MarkStaticMeshElementsDirty();

public:
    // ========================================================================
    // Public Data
    // ========================================================================

    /** The primitive component this info represents */
    UPrimitiveComponent* Component;

    /** The scene proxy for rendering */
    FPrimitiveSceneProxy* Proxy;

    /** The scene this primitive belongs to */
    FScene* Scene;

    /** Unique ID for this primitive component */
    FPrimitiveComponentId PrimitiveComponentId;

    /** Index into the scene's packed primitive arrays */
    int32 PackedIndex;

    /** ID in the scene's primitive octree */
    uint32 OctreeId;

    /** Head of the linked list of light interactions */
    FLightPrimitiveInteraction* LightList;

    /** Parent primitive for attachment hierarchy */
    FPrimitiveSceneInfo* AttachmentParent;

    /** Root of the attachment hierarchy */
    FPrimitiveSceneInfo* AttachmentRoot;

    /** Index in the attachment group */
    int32 AttachmentGroupIndex;

    /** Last frame this primitive was rendered */
    uint32 LastRenderTime;

    /** Last frame this primitive was visible */
    uint32 LastVisibilityChangeTime;

    // ========================================================================
    // Flags
    // ========================================================================

    /** Whether the primitive needs a visibility check */
    uint8 bNeedsVisibilityCheck : 1;

    /** Whether the primitive was recently visible */
    uint8 bWasRecentlyVisible : 1;

    /** Whether the primitive has static mesh elements */
    uint8 bHasStaticMeshElements : 1;

    /** Whether the primitive is registered with the scene */
    uint8 bIsRegistered : 1;

    /** Whether the primitive needs a static mesh update */
    uint8 bNeedsStaticMeshUpdate : 1;

    /** Whether the primitive is attached to another primitive */
    uint8 bIsAttached : 1;
};

} // namespace MonsterEngine
