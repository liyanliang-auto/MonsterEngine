// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file PrimitiveComponent.h
 * @brief Base class for components that can be rendered
 * 
 * UPrimitiveComponent is the base class for all components that contain or
 * generate geometry that can be rendered or used for collision.
 * Following UE5's primitive component architecture.
 */

#include "SceneComponent.h"
#include "Engine/SceneTypes.h"

namespace MonsterEngine
{

// Forward declarations
class FPrimitiveSceneProxy;
class FPrimitiveSceneInfo;
class FScene;

/**
 * Base class for components that can be rendered
 * 
 * UPrimitiveComponent provides:
 * - Scene proxy creation for rendering
 * - Visibility and culling settings
 * - Shadow casting settings
 * - Collision settings (basic)
 * - Material support
 */
class UPrimitiveComponent : public USceneComponent
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /** Default constructor */
    UPrimitiveComponent();

    /** Constructor with owner */
    explicit UPrimitiveComponent(AActor* InOwner);

    /** Virtual destructor */
    virtual ~UPrimitiveComponent();

    // ========================================================================
    // Component Lifecycle
    // ========================================================================

    virtual void OnRegister() override;
    virtual void OnUnregister() override;

    // ========================================================================
    // Scene Proxy
    // ========================================================================

    /**
     * Creates the scene proxy for this component
     * Override in derived classes to create specific proxy types
     * @return The new scene proxy, or nullptr if none should be created
     */
    virtual FPrimitiveSceneProxy* CreateSceneProxy();

    /**
     * Get the current scene proxy
     * Note: This should only be accessed on the render thread
     */
    FPrimitiveSceneProxy* GetSceneProxy() const { return SceneProxy; }

    /** Get the primitive scene info */
    FPrimitiveSceneInfo* GetPrimitiveSceneInfo() const { return PrimitiveSceneInfo; }

    // ========================================================================
    // Scene Registration
    // ========================================================================

    /**
     * Creates and registers the scene proxy with the scene
     * Called when the component is registered
     */
    void CreateRenderState();

    /**
     * Destroys and unregisters the scene proxy from the scene
     * Called when the component is unregistered
     */
    void DestroyRenderState();

    /**
     * Sends an update to the render state
     * Called when properties change that affect rendering
     */
    void SendRenderTransform();

    /**
     * Marks the render state as dirty
     * Will cause the scene proxy to be recreated
     */
    void MarkRenderStateDirty();

    /**
     * Marks the render transform as dirty
     * Will cause the transform to be updated on the render thread
     */
    void MarkRenderTransformDirty();

    /**
     * Marks the render dynamic data as dirty
     * Will cause dynamic data to be updated on the render thread
     */
    void MarkRenderDynamicDataDirty();

    // ========================================================================
    // Visibility
    // ========================================================================

    /** Get the minimum draw distance */
    float GetMinDrawDistance() const { return MinDrawDistance; }

    /** Set the minimum draw distance */
    void SetMinDrawDistance(float NewMinDrawDistance);

    /** Get the maximum draw distance */
    float GetLDMaxDrawDistance() const { return LDMaxDrawDistance; }

    /** Set the maximum draw distance */
    void SetLDMaxDrawDistance(float NewMaxDrawDistance);

    /** Get the cached max draw distance */
    float GetCachedMaxDrawDistance() const { return CachedMaxDrawDistance; }

    // ========================================================================
    // Shadow Settings
    // ========================================================================

    /** Check if the component casts shadows */
    bool CastsShadow() const { return bCastShadow; }

    /** Set whether the component casts shadows */
    void SetCastShadow(bool bNewCastShadow);

    /** Check if the component casts dynamic shadows */
    bool CastsDynamicShadow() const { return bCastDynamicShadow; }

    /** Set whether the component casts dynamic shadows */
    void SetCastDynamicShadow(bool bNewCastDynamicShadow);

    /** Check if the component casts static shadows */
    bool CastsStaticShadow() const { return bCastStaticShadow; }

    /** Set whether the component casts static shadows */
    void SetCastStaticShadow(bool bNewCastStaticShadow);

    /** Check if the component receives shadows */
    bool ReceivesShadow() const { return bReceiveShadow; }

    /** Set whether the component receives shadows */
    void SetReceiveShadow(bool bNewReceiveShadow);

    // ========================================================================
    // Rendering Flags
    // ========================================================================

    /** Check if the component should render in the main pass */
    bool ShouldRenderInMainPass() const { return bRenderInMainPass; }

    /** Set whether the component should render in the main pass */
    void SetRenderInMainPass(bool bNewRenderInMainPass);

    /** Check if the component should render in the depth pass */
    bool ShouldRenderInDepthPass() const { return bRenderInDepthPass; }

    /** Set whether the component should render in the depth pass */
    void SetRenderInDepthPass(bool bNewRenderInDepthPass);

    /** Check if the component uses custom depth */
    bool UsesCustomDepth() const { return bRenderCustomDepth; }

    /** Set whether the component uses custom depth */
    void SetRenderCustomDepth(bool bNewRenderCustomDepth);

    /** Get the custom depth stencil value */
    int32 GetCustomDepthStencilValue() const { return CustomDepthStencilValue; }

    /** Set the custom depth stencil value */
    void SetCustomDepthStencilValue(int32 NewValue);

    // ========================================================================
    // Lighting
    // ========================================================================

    /** Check if the component affects dynamic indirect lighting */
    bool AffectsDynamicIndirectLighting() const { return bAffectDynamicIndirectLighting; }

    /** Set whether the component affects dynamic indirect lighting */
    void SetAffectDynamicIndirectLighting(bool bNewAffect);

    /** Get the lighting channels */
    uint8 GetLightingChannelMask() const { return LightingChannelMask; }

    /** Set the lighting channels */
    void SetLightingChannels(bool bChannel0, bool bChannel1, bool bChannel2);

    // ========================================================================
    // Component ID
    // ========================================================================

    /** Get the primitive component ID */
    FPrimitiveComponentId GetPrimitiveComponentId() const { return PrimitiveComponentId; }

    // ========================================================================
    // Bounds
    // ========================================================================

    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

    /** Get the bounds scale */
    float GetBoundsScale() const { return BoundsScale; }

    /** Set the bounds scale */
    void SetBoundsScale(float NewBoundsScale);

    // ========================================================================
    // Primitive Flags
    // ========================================================================

    /** Get the primitive flags */
    EPrimitiveFlags GetPrimitiveFlags() const;

protected:
    // ========================================================================
    // Protected Methods
    // ========================================================================

    /** Called when render state needs to be created */
    virtual void OnCreateRenderState();

    /** Called when render state needs to be destroyed */
    virtual void OnDestroyRenderState();

    /** Called when the scene proxy needs to be updated */
    virtual void OnUpdateRenderState();

    /** Generate a new primitive component ID */
    void GeneratePrimitiveComponentId();

protected:
    // ========================================================================
    // Scene Data
    // ========================================================================

    /** The scene proxy for rendering */
    FPrimitiveSceneProxy* SceneProxy;

    /** The primitive scene info */
    FPrimitiveSceneInfo* PrimitiveSceneInfo;

    /** Unique ID for this primitive component */
    FPrimitiveComponentId PrimitiveComponentId;

    // ========================================================================
    // Draw Distance
    // ========================================================================

    /** Minimum distance at which the primitive should be rendered */
    float MinDrawDistance;

    /** Max draw distance exposed to level designers */
    float LDMaxDrawDistance;

    /** The distance to cull this primitive at (cached) */
    float CachedMaxDrawDistance;

    // ========================================================================
    // Bounds
    // ========================================================================

    /** Scale to apply to bounds for culling */
    float BoundsScale;

    // ========================================================================
    // Lighting
    // ========================================================================

    /** Lighting channel mask */
    uint8 LightingChannelMask;

    /** Custom depth stencil value */
    int32 CustomDepthStencilValue;

    // ========================================================================
    // Flags
    // ========================================================================

    /** Whether the component casts shadows */
    uint8 bCastShadow : 1;

    /** Whether the component casts dynamic shadows */
    uint8 bCastDynamicShadow : 1;

    /** Whether the component casts static shadows */
    uint8 bCastStaticShadow : 1;

    /** Whether the component receives shadows */
    uint8 bReceiveShadow : 1;

    /** Whether the component should render in the main pass */
    uint8 bRenderInMainPass : 1;

    /** Whether the component should render in the depth pass */
    uint8 bRenderInDepthPass : 1;

    /** Whether the component uses custom depth */
    uint8 bRenderCustomDepth : 1;

    /** Whether the component affects dynamic indirect lighting */
    uint8 bAffectDynamicIndirectLighting : 1;

    /** Whether the render state is dirty */
    uint8 bRenderStateDirty : 1;

    /** Whether the render transform is dirty */
    uint8 bRenderTransformDirty : 1;

    /** Whether the render dynamic data is dirty */
    uint8 bRenderDynamicDataDirty : 1;

    /** Whether the component is registered with the scene */
    uint8 bRegisteredWithScene : 1;

private:
    /** Static counter for generating unique primitive component IDs */
    static uint32 NextPrimitiveComponentId;
};

} // namespace MonsterEngine
