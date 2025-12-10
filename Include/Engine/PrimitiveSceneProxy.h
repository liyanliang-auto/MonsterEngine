// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file PrimitiveSceneProxy.h
 * @brief Primitive scene proxy for rendering
 * 
 * FPrimitiveSceneProxy is the rendering thread's representation of a UPrimitiveComponent.
 * It encapsulates all the data needed to render the primitive and provides methods
 * for drawing. Following UE5's scene proxy architecture.
 */

#include "SceneTypes.h"
#include "Math/Matrix.h"
#include "Math/Box.h"

namespace MonsterEngine
{

// Forward declarations
class UPrimitiveComponent;
class FPrimitiveSceneInfo;
class FScene;

/**
 * Rendering thread representation of a primitive component
 * 
 * FPrimitiveSceneProxy is created by UPrimitiveComponent::CreateSceneProxy()
 * and is owned by the rendering thread. It contains all the data needed to
 * render the primitive without accessing the game thread's UPrimitiveComponent.
 */
class FPrimitiveSceneProxy
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /**
     * Constructor
     * @param InComponent The primitive component this proxy represents
     * @param InResourceName Optional debug name for the resource
     */
    FPrimitiveSceneProxy(const UPrimitiveComponent* InComponent, const char* InResourceName = nullptr);

    /** Virtual destructor */
    virtual ~FPrimitiveSceneProxy();

    // ========================================================================
    // Proxy Information
    // ========================================================================

    /** Get the primitive component this proxy represents */
    const UPrimitiveComponent* GetPrimitiveComponent() const { return PrimitiveComponent; }

    /** Get the primitive scene info */
    FPrimitiveSceneInfo* GetPrimitiveSceneInfo() const { return PrimitiveSceneInfo; }

    /** Set the primitive scene info (called when added to scene) */
    void SetPrimitiveSceneInfo(FPrimitiveSceneInfo* InPrimitiveSceneInfo) { PrimitiveSceneInfo = InPrimitiveSceneInfo; }

    /** Get the scene this proxy belongs to */
    FScene* GetScene() const { return Scene; }

    /** Set the scene (called when added to scene) */
    void SetScene(FScene* InScene) { Scene = InScene; }

    // ========================================================================
    // Transform
    // ========================================================================

    /** Get the local to world transform matrix */
    const FMatrix& GetLocalToWorld() const { return LocalToWorld; }

    /** Set the local to world transform matrix */
    void SetLocalToWorld(const FMatrix& InLocalToWorld);

    /** Get the world position */
    FVector GetActorPosition() const { return LocalToWorld.GetOrigin(); }

    /** Check if the proxy has a dynamic transform that changes frequently */
    bool HasDynamicTransform() const { return bHasDynamicTransform; }

    // ========================================================================
    // Bounds
    // ========================================================================

    /** Get the world-space bounds */
    const FBoxSphereBounds& GetBounds() const { return Bounds; }

    /** Get the local-space bounds */
    const FBox& GetLocalBounds() const { return LocalBounds; }

    /** Update the bounds */
    void SetBounds(const FBoxSphereBounds& InBounds) { Bounds = InBounds; }

    /** Update the local bounds */
    void SetLocalBounds(const FBox& InLocalBounds) { LocalBounds = InLocalBounds; }

    // ========================================================================
    // Visibility and Rendering Flags
    // ========================================================================

    /** Check if the primitive is visible */
    bool IsVisible() const { return bVisible; }

    /** Set visibility */
    void SetVisible(bool bInVisible) { bVisible = bInVisible; }

    /** Check if the primitive casts shadows */
    bool CastsShadow() const { return bCastShadow; }

    /** Check if the primitive casts dynamic shadows */
    bool CastsDynamicShadow() const { return bCastDynamicShadow; }

    /** Check if the primitive casts static shadows */
    bool CastsStaticShadow() const { return bCastStaticShadow; }

    /** Check if the primitive receives shadows */
    bool ReceivesShadow() const { return bReceiveShadow; }

    /** Check if the primitive should be rendered in the main pass */
    bool ShouldRenderInMainPass() const { return bRenderInMainPass; }

    /** Check if the primitive should be rendered in the depth pass */
    bool ShouldRenderInDepthPass() const { return bRenderInDepthPass; }

    /** Check if the primitive uses custom depth */
    bool UsesCustomDepth() const { return bRenderCustomDepth; }

    // ========================================================================
    // Draw Distance
    // ========================================================================

    /** Get the minimum draw distance */
    float GetMinDrawDistance() const { return MinDrawDistance; }

    /** Get the maximum draw distance */
    float GetMaxDrawDistance() const { return MaxDrawDistance; }

    /** Set draw distances */
    void SetDrawDistances(float InMinDrawDistance, float InMaxDrawDistance)
    {
        MinDrawDistance = InMinDrawDistance;
        MaxDrawDistance = InMaxDrawDistance;
    }

    // ========================================================================
    // Mobility
    // ========================================================================

    /** Get the mobility of this primitive */
    EComponentMobility GetMobility() const { return Mobility; }

    /** Check if the primitive is static */
    bool IsStatic() const { return Mobility == EComponentMobility::Static; }

    /** Check if the primitive is movable */
    bool IsMovable() const { return Mobility == EComponentMobility::Movable; }

    // ========================================================================
    // Component ID
    // ========================================================================

    /** Get the primitive component ID */
    FPrimitiveComponentId GetPrimitiveComponentId() const { return PrimitiveComponentId; }

    /** Set the primitive component ID */
    void SetPrimitiveComponentId(FPrimitiveComponentId InId) { PrimitiveComponentId = InId; }

    // ========================================================================
    // View Relevance
    // ========================================================================

    /**
     * Determines the relevance of this primitive's elements to the given view
     * @param View The view to check relevance for
     * @return The view relevance flags
     */
    virtual FPrimitiveViewRelevance GetViewRelevance(const class FSceneView* View) const;

    // ========================================================================
    // Drawing
    // ========================================================================

    /**
     * Draws the primitive's static elements
     * Called during static mesh batch generation
     */
    virtual void DrawStaticElements(class FStaticPrimitiveDrawInterface* PDI) {}

    /**
     * Draws the primitive's dynamic elements
     * Called every frame for dynamic primitives
     */
    virtual void GetDynamicMeshElements(
        const TArray<const class FSceneView*>& Views,
        const class FSceneViewFamily& ViewFamily,
        uint32 VisibilityMap,
        class FMeshElementCollector& Collector) const {}

    /**
     * Get the type hash for this proxy type
     * Used for efficient type comparison
     */
    virtual SIZE_T GetTypeHash() const = 0;

    // ========================================================================
    // Debug
    // ========================================================================

    /** Get the resource name for debugging */
    const char* GetResourceName() const { return ResourceName; }

    /** Get the owner name for debugging */
    const char* GetOwnerName() const { return OwnerName; }

protected:
    // ========================================================================
    // Protected Data
    // ========================================================================

    /** The primitive component this proxy represents (game thread only!) */
    const UPrimitiveComponent* PrimitiveComponent;

    /** The primitive scene info (set when added to scene) */
    FPrimitiveSceneInfo* PrimitiveSceneInfo;

    /** The scene this proxy belongs to */
    FScene* Scene;

    /** Local to world transform matrix */
    FMatrix LocalToWorld;

    /** World-space bounds */
    FBoxSphereBounds Bounds;

    /** Local-space bounds */
    FBox LocalBounds;

    /** Primitive component ID */
    FPrimitiveComponentId PrimitiveComponentId;

    /** Mobility of this primitive */
    EComponentMobility Mobility;

    /** Minimum draw distance */
    float MinDrawDistance;

    /** Maximum draw distance (0 = infinite) */
    float MaxDrawDistance;

    /** Resource name for debugging */
    const char* ResourceName;

    /** Owner name for debugging */
    const char* OwnerName;

    // ========================================================================
    // Flags
    // ========================================================================

    /** Whether the primitive is visible */
    uint8 bVisible : 1;

    /** Whether the primitive casts any shadow */
    uint8 bCastShadow : 1;

    /** Whether the primitive casts dynamic shadows */
    uint8 bCastDynamicShadow : 1;

    /** Whether the primitive casts static shadows */
    uint8 bCastStaticShadow : 1;

    /** Whether the primitive receives shadows */
    uint8 bReceiveShadow : 1;

    /** Whether the primitive should be rendered in the main pass */
    uint8 bRenderInMainPass : 1;

    /** Whether the primitive should be rendered in the depth pass */
    uint8 bRenderInDepthPass : 1;

    /** Whether the primitive uses custom depth */
    uint8 bRenderCustomDepth : 1;

    /** Whether the primitive has a dynamic transform */
    uint8 bHasDynamicTransform : 1;

    /** Whether the primitive affects dynamic indirect lighting */
    uint8 bAffectDynamicIndirectLighting : 1;

    /** Whether the primitive is hidden in game */
    uint8 bHiddenInGame : 1;

    /** Whether the primitive is selectable in editor */
    uint8 bSelectable : 1;
};

} // namespace MonsterEngine
