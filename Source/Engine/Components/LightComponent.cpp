// Copyright Monster Engine. All Rights Reserved.

/**
 * @file LightComponent.cpp
 * @brief Implementation of ULightComponent and derived classes
 */

#include "Engine/Components/LightComponent.h"
#include "Engine/LightSceneProxy.h"
#include "Engine/LightSceneInfo.h"
#include "Engine/Scene.h"
#include "Core/Logging/LogMacros.h"
#include <cmath>

namespace MonsterEngine
{

DEFINE_LOG_CATEGORY_STATIC(LogLightComponent, Log, All);

// ============================================================================
// ULightComponent Implementation
// ============================================================================

ULightComponent::ULightComponent()
    : USceneComponent()
    , LightSceneProxy(nullptr)
    , LightSceneInfo(nullptr)
    , LightColor(FLinearColor::White)
    , Intensity(1.0f)
    , IndirectLightingIntensity(1.0f)
    , VolumetricScatteringIntensity(1.0f)
    , Temperature(6500.0f)
    , ShadowBias(0.5f)
    , ShadowSlopeBias(0.5f)
    , ShadowResolutionScale(1.0f)
    , LightingChannelMask(0x01)
    , bCastShadow(true)
    , bCastStaticShadow(true)
    , bCastDynamicShadow(true)
    , bAffectsWorld(true)
    , bUseTemperature(false)
    , bAffectTranslucentLighting(true)
    , bRenderStateDirty(false)
    , bRegisteredWithScene(false)
{
}

ULightComponent::ULightComponent(AActor* InOwner)
    : USceneComponent(InOwner)
    , LightSceneProxy(nullptr)
    , LightSceneInfo(nullptr)
    , LightColor(FLinearColor::White)
    , Intensity(1.0f)
    , IndirectLightingIntensity(1.0f)
    , VolumetricScatteringIntensity(1.0f)
    , Temperature(6500.0f)
    , ShadowBias(0.5f)
    , ShadowSlopeBias(0.5f)
    , ShadowResolutionScale(1.0f)
    , LightingChannelMask(0x01)
    , bCastShadow(true)
    , bCastStaticShadow(true)
    , bCastDynamicShadow(true)
    , bAffectsWorld(true)
    , bUseTemperature(false)
    , bAffectTranslucentLighting(true)
    , bRenderStateDirty(false)
    , bRegisteredWithScene(false)
{
}

ULightComponent::~ULightComponent()
{
    if (bRegisteredWithScene)
    {
        DestroyRenderState();
    }
}

// ============================================================================
// Component Lifecycle
// ============================================================================

void ULightComponent::OnRegister()
{
    USceneComponent::OnRegister();
    CreateRenderState();
}

void ULightComponent::OnUnregister()
{
    DestroyRenderState();
    USceneComponent::OnUnregister();
}

// ============================================================================
// Scene Proxy
// ============================================================================

FLightSceneProxy* ULightComponent::CreateSceneProxy()
{
    return new FLightSceneProxy(this);
}

// ============================================================================
// Scene Registration
// ============================================================================

void ULightComponent::CreateRenderState()
{
    if (bRegisteredWithScene)
    {
        return;
    }

    OnCreateRenderState();
    bRegisteredWithScene = true;
    
    MR_LOG(LogLightComponent, Verbose, "Light render state created");
}

void ULightComponent::DestroyRenderState()
{
    if (!bRegisteredWithScene)
    {
        return;
    }

    OnDestroyRenderState();
    bRegisteredWithScene = false;
    
    LightSceneProxy = nullptr;
    LightSceneInfo = nullptr;
    
    MR_LOG(LogLightComponent, Verbose, "Light render state destroyed");
}

void ULightComponent::SendRenderTransform()
{
    if (!bRegisteredWithScene || !LightSceneInfo)
    {
        return;
    }

    // Update transform in scene
    if (LightSceneInfo->GetScene())
    {
        LightSceneInfo->GetScene()->UpdateLightTransform(this);
    }
}

void ULightComponent::SendRenderLightUpdate()
{
    if (!bRegisteredWithScene || !LightSceneInfo)
    {
        return;
    }

    // Update light properties in scene
    if (LightSceneInfo->GetScene())
    {
        LightSceneInfo->GetScene()->UpdateLightColorAndBrightness(this);
    }
}

void ULightComponent::MarkRenderStateDirty()
{
    bRenderStateDirty = true;
}

// ============================================================================
// Light Color
// ============================================================================

void ULightComponent::SetLightColor(const FLinearColor& NewColor)
{
    if (LightColor != NewColor)
    {
        LightColor = NewColor;
        OnLightPropertyChanged();
    }
}

void ULightComponent::SetLightColor(const FColor& NewColor, bool bSRGB)
{
    FLinearColor LinearColor = bSRGB ? 
        FLinearColor::FromSRGBColor(NewColor) : 
        FLinearColor(NewColor);
    SetLightColor(LinearColor);
}

// ============================================================================
// Intensity
// ============================================================================

void ULightComponent::SetIntensity(float NewIntensity)
{
    if (Intensity != NewIntensity)
    {
        Intensity = NewIntensity;
        OnLightPropertyChanged();
    }
}

void ULightComponent::SetIndirectLightingIntensity(float NewIntensity)
{
    if (IndirectLightingIntensity != NewIntensity)
    {
        IndirectLightingIntensity = NewIntensity;
        MarkRenderStateDirty();
    }
}

void ULightComponent::SetVolumetricScatteringIntensity(float NewIntensity)
{
    if (VolumetricScatteringIntensity != NewIntensity)
    {
        VolumetricScatteringIntensity = NewIntensity;
        MarkRenderStateDirty();
    }
}

// ============================================================================
// Temperature
// ============================================================================

void ULightComponent::SetTemperature(float NewTemperature)
{
    if (Temperature != NewTemperature)
    {
        Temperature = NewTemperature;
        if (bUseTemperature)
        {
            OnLightPropertyChanged();
        }
    }
}

void ULightComponent::SetUseTemperature(bool bNewUseTemperature)
{
    if (bUseTemperature != bNewUseTemperature)
    {
        bUseTemperature = bNewUseTemperature;
        OnLightPropertyChanged();
    }
}

// ============================================================================
// Shadow Settings
// ============================================================================

void ULightComponent::SetCastShadow(bool bNewCastShadow)
{
    if (bCastShadow != bNewCastShadow)
    {
        bCastShadow = bNewCastShadow;
        MarkRenderStateDirty();
    }
}

void ULightComponent::SetShadowBias(float NewShadowBias)
{
    if (ShadowBias != NewShadowBias)
    {
        ShadowBias = NewShadowBias;
        MarkRenderStateDirty();
    }
}

void ULightComponent::SetShadowSlopeBias(float NewShadowSlopeBias)
{
    if (ShadowSlopeBias != NewShadowSlopeBias)
    {
        ShadowSlopeBias = NewShadowSlopeBias;
        MarkRenderStateDirty();
    }
}

void ULightComponent::SetShadowResolutionScale(float NewScale)
{
    if (ShadowResolutionScale != NewScale)
    {
        ShadowResolutionScale = NewScale;
        MarkRenderStateDirty();
    }
}

// ============================================================================
// Visibility
// ============================================================================

void ULightComponent::SetAffectsWorld(bool bNewAffectsWorld)
{
    if (bAffectsWorld != bNewAffectsWorld)
    {
        bAffectsWorld = bNewAffectsWorld;
        MarkRenderStateDirty();
    }
}

void ULightComponent::SetLightingChannels(bool bChannel0, bool bChannel1, bool bChannel2)
{
    uint8 NewMask = 0;
    if (bChannel0) NewMask |= 0x01;
    if (bChannel1) NewMask |= 0x02;
    if (bChannel2) NewMask |= 0x04;
    
    if (LightingChannelMask != NewMask)
    {
        LightingChannelMask = NewMask;
        MarkRenderStateDirty();
    }
}

// ============================================================================
// Bounds
// ============================================================================

FBoxSphereBounds ULightComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    float InfluenceRadius = GetLightInfluenceRadius();
    if (InfluenceRadius > 0.0f)
    {
        return FBoxSphereBounds(
            LocalToWorld.GetTranslation(),
            FVector(InfluenceRadius, InfluenceRadius, InfluenceRadius),
            InfluenceRadius);
    }
    return USceneComponent::CalcBounds(LocalToWorld);
}

// ============================================================================
// Protected Methods
// ============================================================================

void ULightComponent::OnCreateRenderState()
{
    LightSceneProxy = CreateSceneProxy();
}

void ULightComponent::OnDestroyRenderState()
{
    // Light proxy is owned by the scene
}

void ULightComponent::OnLightPropertyChanged()
{
    if (bRegisteredWithScene)
    {
        SendRenderLightUpdate();
    }
}

void ULightComponent::OnTransformUpdated()
{
    // Call base class implementation
    USceneComponent::OnTransformUpdated();
    
    // Update the proxy's transform when the component's transform changes
    if (LightSceneProxy)
    {
        LightSceneProxy->SetTransform(GetComponentToWorld());
    }
}

// ============================================================================
// UDirectionalLightComponent Implementation
// ============================================================================

UDirectionalLightComponent::UDirectionalLightComponent()
    : ULightComponent()
{
}

UDirectionalLightComponent::UDirectionalLightComponent(AActor* InOwner)
    : ULightComponent(InOwner)
{
}

FLightSceneProxy* UDirectionalLightComponent::CreateSceneProxy()
{
    FLightSceneProxy* Proxy = new FLightSceneProxy(this);
    // Proxy is already configured in constructor
    return Proxy;
}

FBoxSphereBounds UDirectionalLightComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    // Directional lights have infinite bounds
    return FBoxSphereBounds(FVector::ZeroVector, FVector(1e10, 1e10, 1e10), 1e10);
}

// ============================================================================
// UPointLightComponent Implementation
// ============================================================================

UPointLightComponent::UPointLightComponent()
    : ULightComponent()
    , AttenuationRadius(1000.0f)
    , SourceRadius(0.0f)
    , SoftSourceRadius(0.0f)
    , SourceLength(0.0f)
    , bUseInverseSquaredFalloff(true)
{
}

UPointLightComponent::UPointLightComponent(AActor* InOwner)
    : ULightComponent(InOwner)
    , AttenuationRadius(1000.0f)
    , SourceRadius(0.0f)
    , SoftSourceRadius(0.0f)
    , SourceLength(0.0f)
    , bUseInverseSquaredFalloff(true)
{
}

FLightSceneProxy* UPointLightComponent::CreateSceneProxy()
{
    FLightSceneProxy* Proxy = new FLightSceneProxy(this);
    return Proxy;
}

void UPointLightComponent::SetAttenuationRadius(float NewRadius)
{
    if (AttenuationRadius != NewRadius)
    {
        AttenuationRadius = NewRadius;
        UpdateBounds();
        MarkRenderStateDirty();
    }
}

void UPointLightComponent::SetSourceRadius(float NewRadius)
{
    if (SourceRadius != NewRadius)
    {
        SourceRadius = NewRadius;
        MarkRenderStateDirty();
    }
}

void UPointLightComponent::SetSoftSourceRadius(float NewRadius)
{
    if (SoftSourceRadius != NewRadius)
    {
        SoftSourceRadius = NewRadius;
        MarkRenderStateDirty();
    }
}

void UPointLightComponent::SetSourceLength(float NewLength)
{
    if (SourceLength != NewLength)
    {
        SourceLength = NewLength;
        MarkRenderStateDirty();
    }
}

// ============================================================================
// USpotLightComponent Implementation
// ============================================================================

USpotLightComponent::USpotLightComponent()
    : UPointLightComponent()
    , InnerConeAngle(0.0f)
    , OuterConeAngle(44.0f)
{
}

USpotLightComponent::USpotLightComponent(AActor* InOwner)
    : UPointLightComponent(InOwner)
    , InnerConeAngle(0.0f)
    , OuterConeAngle(44.0f)
{
}

FLightSceneProxy* USpotLightComponent::CreateSceneProxy()
{
    FLightSceneProxy* Proxy = new FLightSceneProxy(this);
    return Proxy;
}

void USpotLightComponent::SetInnerConeAngle(float NewAngle)
{
    // Clamp to valid range
    NewAngle = std::max(0.0f, std::min(NewAngle, OuterConeAngle));
    
    if (InnerConeAngle != NewAngle)
    {
        InnerConeAngle = NewAngle;
        MarkRenderStateDirty();
    }
}

void USpotLightComponent::SetOuterConeAngle(float NewAngle)
{
    // Clamp to valid range (0-90 degrees)
    NewAngle = std::max(InnerConeAngle, std::min(NewAngle, 90.0f));
    
    if (OuterConeAngle != NewAngle)
    {
        OuterConeAngle = NewAngle;
        MarkRenderStateDirty();
    }
}

} // namespace MonsterEngine
