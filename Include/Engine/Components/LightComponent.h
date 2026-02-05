// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file LightComponent.h
 * @brief Base class for light components
 * 
 * ULightComponent is the base class for all light components.
 * It provides common light properties and scene registration.
 * Following UE5's light component architecture.
 */

#include "SceneComponent.h"
#include "Engine/SceneTypes.h"
#include "Core/Color.h"

namespace MonsterEngine
{

// Forward declarations
class FLightSceneProxy;
class FLightSceneInfo;
class FScene;

/**
 * Base class for light components
 * 
 * ULightComponent provides:
 * - Light color and intensity
 * - Shadow settings
 * - Light function support
 * - Scene registration
 */
class ULightComponent : public USceneComponent
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /** Default constructor */
    ULightComponent();

    /** Constructor with owner */
    explicit ULightComponent(AActor* InOwner);

    /** Virtual destructor */
    virtual ~ULightComponent();

    // ========================================================================
    // Component Lifecycle
    // ========================================================================

    virtual void OnRegister() override;
    virtual void OnUnregister() override;

    // ========================================================================
    // Light Type
    // ========================================================================

    /** Get the light type */
    virtual ELightType GetLightType() const { return ELightType::Point; }

    // ========================================================================
    // Scene Proxy
    // ========================================================================

    /**
     * Creates the light scene proxy for this component
     * Override in derived classes to create specific proxy types
     * @return The new light scene proxy
     */
    virtual FLightSceneProxy* CreateSceneProxy();

    /** Get the current light scene proxy */
    FLightSceneProxy* GetLightSceneProxy() const { return LightSceneProxy; }

    /** Get the light scene info */
    FLightSceneInfo* GetLightSceneInfo() const { return LightSceneInfo; }

    // ========================================================================
    // Scene Registration
    // ========================================================================

    /** Creates and registers the light with the scene */
    void CreateRenderState();

    /** Destroys and unregisters the light from the scene */
    void DestroyRenderState();

    /** Sends a transform update to the render thread */
    void SendRenderTransform();

    /** Sends a color/brightness update to the render thread */
    void SendRenderLightUpdate();

    /** Marks the render state as dirty */
    void MarkRenderStateDirty();

    // ========================================================================
    // Light Color
    // ========================================================================

    /** Get the light color */
    const FLinearColor& GetLightColor() const { return LightColor; }

    /** Set the light color */
    void SetLightColor(const FLinearColor& NewColor);

    /** Set the light color from an FColor */
    void SetLightColor(const FColor& NewColor, bool bSRGB = true);

    // ========================================================================
    // Intensity
    // ========================================================================

    /** Get the light intensity */
    float GetIntensity() const { return Intensity; }

    /** Set the light intensity */
    void SetIntensity(float NewIntensity);

    /** Get the indirect lighting intensity */
    float GetIndirectLightingIntensity() const { return IndirectLightingIntensity; }

    /** Set the indirect lighting intensity */
    void SetIndirectLightingIntensity(float NewIntensity);

    /** Get the volumetric scattering intensity */
    float GetVolumetricScatteringIntensity() const { return VolumetricScatteringIntensity; }

    /** Set the volumetric scattering intensity */
    void SetVolumetricScatteringIntensity(float NewIntensity);

    // ========================================================================
    // Temperature
    // ========================================================================

    /** Get the color temperature in Kelvin */
    float GetTemperature() const { return Temperature; }

    /** Set the color temperature in Kelvin */
    void SetTemperature(float NewTemperature);

    /** Check if temperature is used */
    bool UseTemperature() const { return bUseTemperature; }

    /** Set whether to use temperature */
    void SetUseTemperature(bool bNewUseTemperature);

    // ========================================================================
    // Shadow Settings
    // ========================================================================

    /** Check if the light casts shadows */
    bool CastsShadow() const { return bCastShadow; }

    /** Set whether the light casts shadows */
    void SetCastShadow(bool bNewCastShadow);

    /** Check if the light casts static shadows */
    bool CastsStaticShadow() const { return bCastStaticShadow; }

    /** Check if the light casts dynamic shadows */
    bool CastsDynamicShadow() const { return bCastDynamicShadow; }

    /** Get the shadow bias */
    float GetShadowBias() const { return ShadowBias; }

    /** Set the shadow bias */
    void SetShadowBias(float NewShadowBias);

    /** Get the shadow slope bias */
    float GetShadowSlopeBias() const { return ShadowSlopeBias; }

    /** Set the shadow slope bias */
    void SetShadowSlopeBias(float NewShadowSlopeBias);

    /** Get the shadow resolution scale */
    float GetShadowResolutionScale() const { return ShadowResolutionScale; }

    /** Set the shadow resolution scale */
    void SetShadowResolutionScale(float NewScale);

    // ========================================================================
    // Visibility
    // ========================================================================

    /** Check if the light affects the world */
    bool AffectsWorld() const { return bAffectsWorld; }

    /** Set whether the light affects the world */
    void SetAffectsWorld(bool bNewAffectsWorld);

    /** Get the lighting channels */
    uint8 GetLightingChannelMask() const { return LightingChannelMask; }

    /** Set the lighting channels */
    void SetLightingChannels(bool bChannel0, bool bChannel1, bool bChannel2);

    // ========================================================================
    // Bounds
    // ========================================================================

    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

    /** Get the light's influence radius */
    virtual float GetLightInfluenceRadius() const { return 0.0f; }

protected:
    // ========================================================================
    // Protected Methods
    // ========================================================================

    /** Called when render state needs to be created */
    virtual void OnCreateRenderState();

    /** Called when render state needs to be destroyed */
    virtual void OnDestroyRenderState();

    /** Called when the light properties change */
    virtual void OnLightPropertyChanged();

    /** Called when transform is updated - updates proxy direction */
    virtual void OnTransformUpdated() override;

protected:
    // ========================================================================
    // Scene Data
    // ========================================================================

    /** The light scene proxy */
    FLightSceneProxy* LightSceneProxy;

    /** The light scene info */
    FLightSceneInfo* LightSceneInfo;

    // ========================================================================
    // Light Properties
    // ========================================================================

    /** Light color */
    FLinearColor LightColor;

    /** Light intensity */
    float Intensity;

    /** Indirect lighting intensity multiplier */
    float IndirectLightingIntensity;

    /** Volumetric scattering intensity */
    float VolumetricScatteringIntensity;

    /** Color temperature in Kelvin */
    float Temperature;

    // ========================================================================
    // Shadow Properties
    // ========================================================================

    /** Shadow bias */
    float ShadowBias;

    /** Shadow slope bias */
    float ShadowSlopeBias;

    /** Shadow resolution scale */
    float ShadowResolutionScale;

    // ========================================================================
    // Lighting Channels
    // ========================================================================

    /** Lighting channel mask */
    uint8 LightingChannelMask;

    // ========================================================================
    // Flags
    // ========================================================================

    /** Whether the light casts shadows */
    uint8 bCastShadow : 1;

    /** Whether the light casts static shadows */
    uint8 bCastStaticShadow : 1;

    /** Whether the light casts dynamic shadows */
    uint8 bCastDynamicShadow : 1;

    /** Whether the light affects the world */
    uint8 bAffectsWorld : 1;

    /** Whether to use color temperature */
    uint8 bUseTemperature : 1;

    /** Whether the light affects translucent surfaces */
    uint8 bAffectTranslucentLighting : 1;

    /** Whether the render state is dirty */
    uint8 bRenderStateDirty : 1;

    /** Whether the component is registered with the scene */
    uint8 bRegisteredWithScene : 1;
};

/**
 * Directional light component
 * Simulates a distant light source like the sun
 */
class UDirectionalLightComponent : public ULightComponent
{
public:
    UDirectionalLightComponent();
    explicit UDirectionalLightComponent(AActor* InOwner);

    virtual ELightType GetLightType() const override { return ELightType::Directional; }
    virtual FLightSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
};

/**
 * Point light component
 * Emits light in all directions from a single point
 */
class UPointLightComponent : public ULightComponent
{
public:
    UPointLightComponent();
    explicit UPointLightComponent(AActor* InOwner);

    virtual ELightType GetLightType() const override { return ELightType::Point; }
    virtual FLightSceneProxy* CreateSceneProxy() override;
    virtual float GetLightInfluenceRadius() const override { return AttenuationRadius; }

    /** Get the attenuation radius */
    float GetAttenuationRadius() const { return AttenuationRadius; }

    /** Set the attenuation radius */
    void SetAttenuationRadius(float NewRadius);

    /** Get the source radius */
    float GetSourceRadius() const { return SourceRadius; }

    /** Set the source radius */
    void SetSourceRadius(float NewRadius);

    /** Get the soft source radius */
    float GetSoftSourceRadius() const { return SoftSourceRadius; }

    /** Set the soft source radius */
    void SetSoftSourceRadius(float NewRadius);

    /** Get the source length */
    float GetSourceLength() const { return SourceLength; }

    /** Set the source length */
    void SetSourceLength(float NewLength);

protected:
    /** Attenuation radius */
    float AttenuationRadius;

    /** Source radius for area light simulation */
    float SourceRadius;

    /** Soft source radius */
    float SoftSourceRadius;

    /** Source length for capsule lights */
    float SourceLength;

    /** Whether to use inverse squared falloff */
    uint8 bUseInverseSquaredFalloff : 1;
};

/**
 * Spot light component
 * Emits light in a cone shape
 */
class USpotLightComponent : public UPointLightComponent
{
public:
    USpotLightComponent();
    explicit USpotLightComponent(AActor* InOwner);

    virtual ELightType GetLightType() const override { return ELightType::Spot; }
    virtual FLightSceneProxy* CreateSceneProxy() override;

    /** Get the inner cone angle in degrees */
    float GetInnerConeAngle() const { return InnerConeAngle; }

    /** Set the inner cone angle in degrees */
    void SetInnerConeAngle(float NewAngle);

    /** Get the outer cone angle in degrees */
    float GetOuterConeAngle() const { return OuterConeAngle; }

    /** Set the outer cone angle in degrees */
    void SetOuterConeAngle(float NewAngle);

protected:
    /** Inner cone angle in degrees */
    float InnerConeAngle;

    /** Outer cone angle in degrees */
    float OuterConeAngle;
};

} // namespace MonsterEngine
