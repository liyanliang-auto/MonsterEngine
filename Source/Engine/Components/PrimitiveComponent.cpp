// Copyright Monster Engine. All Rights Reserved.

/**
 * @file PrimitiveComponent.cpp
 * @brief Implementation of UPrimitiveComponent
 */

#include "Engine/Components/PrimitiveComponent.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/PrimitiveSceneInfo.h"
#include "Engine/Scene.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

DEFINE_LOG_CATEGORY_STATIC(LogPrimitiveComponent, Log, All);

// Static member initialization
uint32 UPrimitiveComponent::NextPrimitiveComponentId = 1;

// ============================================================================
// Construction / Destruction
// ============================================================================

UPrimitiveComponent::UPrimitiveComponent()
    : USceneComponent()
    , SceneProxy(nullptr)
    , PrimitiveSceneInfo(nullptr)
    , PrimitiveComponentId()
    , MinDrawDistance(0.0f)
    , LDMaxDrawDistance(0.0f)
    , CachedMaxDrawDistance(0.0f)
    , BoundsScale(1.0f)
    , LightingChannelMask(0x01)
    , CustomDepthStencilValue(0)
    , bCastShadow(true)
    , bCastDynamicShadow(true)
    , bCastStaticShadow(true)
    , bReceiveShadow(true)
    , bRenderInMainPass(true)
    , bRenderInDepthPass(true)
    , bRenderCustomDepth(false)
    , bAffectDynamicIndirectLighting(true)
    , bRenderStateDirty(false)
    , bRenderTransformDirty(false)
    , bRenderDynamicDataDirty(false)
    , bRegisteredWithScene(false)
{
    GeneratePrimitiveComponentId();
}

UPrimitiveComponent::UPrimitiveComponent(AActor* InOwner)
    : USceneComponent(InOwner)
    , SceneProxy(nullptr)
    , PrimitiveSceneInfo(nullptr)
    , PrimitiveComponentId()
    , MinDrawDistance(0.0f)
    , LDMaxDrawDistance(0.0f)
    , CachedMaxDrawDistance(0.0f)
    , BoundsScale(1.0f)
    , LightingChannelMask(0x01)
    , CustomDepthStencilValue(0)
    , bCastShadow(true)
    , bCastDynamicShadow(true)
    , bCastStaticShadow(true)
    , bReceiveShadow(true)
    , bRenderInMainPass(true)
    , bRenderInDepthPass(true)
    , bRenderCustomDepth(false)
    , bAffectDynamicIndirectLighting(true)
    , bRenderStateDirty(false)
    , bRenderTransformDirty(false)
    , bRenderDynamicDataDirty(false)
    , bRegisteredWithScene(false)
{
    GeneratePrimitiveComponentId();
}

UPrimitiveComponent::~UPrimitiveComponent()
{
    // Ensure render state is destroyed
    if (bRegisteredWithScene)
    {
        DestroyRenderState();
    }
}

// ============================================================================
// Component Lifecycle
// ============================================================================

void UPrimitiveComponent::OnRegister()
{
    USceneComponent::OnRegister();
    
    // Create render state
    CreateRenderState();
}

void UPrimitiveComponent::OnUnregister()
{
    // Destroy render state
    DestroyRenderState();
    
    USceneComponent::OnUnregister();
}

// ============================================================================
// Scene Proxy
// ============================================================================

FPrimitiveSceneProxy* UPrimitiveComponent::CreateSceneProxy()
{
    // Base implementation returns nullptr
    // Derived classes should override to create appropriate proxy
    return nullptr;
}

// ============================================================================
// Scene Registration
// ============================================================================

void UPrimitiveComponent::CreateRenderState()
{
    if (bRegisteredWithScene)
    {
        MR_LOG(LogPrimitiveComponent, Warning, "CreateRenderState called but already registered");
        return;
    }

    OnCreateRenderState();
    bRegisteredWithScene = true;
    
    MR_LOG(LogPrimitiveComponent, Verbose, "Render state created for primitive component");
}

void UPrimitiveComponent::DestroyRenderState()
{
    if (!bRegisteredWithScene)
    {
        return;
    }

    OnDestroyRenderState();
    bRegisteredWithScene = false;
    
    // Clear references
    SceneProxy = nullptr;
    PrimitiveSceneInfo = nullptr;
    
    MR_LOG(LogPrimitiveComponent, Verbose, "Render state destroyed for primitive component");
}

void UPrimitiveComponent::SendRenderTransform()
{
    if (!bRegisteredWithScene || !PrimitiveSceneInfo)
    {
        return;
    }

    // Update transform in scene
    // This would typically enqueue a render command
    if (PrimitiveSceneInfo->GetScene())
    {
        PrimitiveSceneInfo->GetScene()->UpdatePrimitiveTransform(this);
    }
    
    bRenderTransformDirty = false;
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
    bRenderStateDirty = true;
}

void UPrimitiveComponent::MarkRenderTransformDirty()
{
    bRenderTransformDirty = true;
}

void UPrimitiveComponent::MarkRenderDynamicDataDirty()
{
    bRenderDynamicDataDirty = true;
}

// ============================================================================
// Visibility
// ============================================================================

void UPrimitiveComponent::SetMinDrawDistance(float NewMinDrawDistance)
{
    if (MinDrawDistance != NewMinDrawDistance)
    {
        MinDrawDistance = NewMinDrawDistance;
        MarkRenderStateDirty();
    }
}

void UPrimitiveComponent::SetLDMaxDrawDistance(float NewMaxDrawDistance)
{
    if (LDMaxDrawDistance != NewMaxDrawDistance)
    {
        LDMaxDrawDistance = NewMaxDrawDistance;
        CachedMaxDrawDistance = NewMaxDrawDistance;
        MarkRenderStateDirty();
    }
}

// ============================================================================
// Shadow Settings
// ============================================================================

void UPrimitiveComponent::SetCastShadow(bool bNewCastShadow)
{
    if (bCastShadow != bNewCastShadow)
    {
        bCastShadow = bNewCastShadow;
        MarkRenderStateDirty();
    }
}

void UPrimitiveComponent::SetCastDynamicShadow(bool bNewCastDynamicShadow)
{
    if (bCastDynamicShadow != bNewCastDynamicShadow)
    {
        bCastDynamicShadow = bNewCastDynamicShadow;
        MarkRenderStateDirty();
    }
}

void UPrimitiveComponent::SetCastStaticShadow(bool bNewCastStaticShadow)
{
    if (bCastStaticShadow != bNewCastStaticShadow)
    {
        bCastStaticShadow = bNewCastStaticShadow;
        MarkRenderStateDirty();
    }
}

void UPrimitiveComponent::SetReceiveShadow(bool bNewReceiveShadow)
{
    if (bReceiveShadow != bNewReceiveShadow)
    {
        bReceiveShadow = bNewReceiveShadow;
        MarkRenderStateDirty();
    }
}

// ============================================================================
// Rendering Flags
// ============================================================================

void UPrimitiveComponent::SetRenderInMainPass(bool bNewRenderInMainPass)
{
    if (bRenderInMainPass != bNewRenderInMainPass)
    {
        bRenderInMainPass = bNewRenderInMainPass;
        MarkRenderStateDirty();
    }
}

void UPrimitiveComponent::SetRenderInDepthPass(bool bNewRenderInDepthPass)
{
    if (bRenderInDepthPass != bNewRenderInDepthPass)
    {
        bRenderInDepthPass = bNewRenderInDepthPass;
        MarkRenderStateDirty();
    }
}

void UPrimitiveComponent::SetRenderCustomDepth(bool bNewRenderCustomDepth)
{
    if (bRenderCustomDepth != bNewRenderCustomDepth)
    {
        bRenderCustomDepth = bNewRenderCustomDepth;
        MarkRenderStateDirty();
    }
}

void UPrimitiveComponent::SetCustomDepthStencilValue(int32 NewValue)
{
    if (CustomDepthStencilValue != NewValue)
    {
        CustomDepthStencilValue = NewValue;
        MarkRenderStateDirty();
    }
}

// ============================================================================
// Lighting
// ============================================================================

void UPrimitiveComponent::SetAffectDynamicIndirectLighting(bool bNewAffect)
{
    if (bAffectDynamicIndirectLighting != bNewAffect)
    {
        bAffectDynamicIndirectLighting = bNewAffect;
        MarkRenderStateDirty();
    }
}

void UPrimitiveComponent::SetLightingChannels(bool bChannel0, bool bChannel1, bool bChannel2)
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

FBoxSphereBounds UPrimitiveComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    // Default implementation - derived classes should override
    FBoxSphereBounds Result = USceneComponent::CalcBounds(LocalToWorld);
    
    // Apply bounds scale
    if (BoundsScale != 1.0f)
    {
        Result.BoxExtent = Result.BoxExtent * BoundsScale;
        Result.SphereRadius *= BoundsScale;
    }
    
    return Result;
}

void UPrimitiveComponent::SetBoundsScale(float NewBoundsScale)
{
    if (BoundsScale != NewBoundsScale)
    {
        BoundsScale = NewBoundsScale;
        UpdateBounds();
        MarkRenderStateDirty();
    }
}

// ============================================================================
// Primitive Flags
// ============================================================================

EPrimitiveFlags UPrimitiveComponent::GetPrimitiveFlags() const
{
    EPrimitiveFlags Flags = EPrimitiveFlags::None;
    
    if (bCastShadow) Flags |= EPrimitiveFlags::CastShadow;
    if (bReceiveShadow) Flags |= EPrimitiveFlags::ReceiveShadow;
    if (bVisible) Flags |= EPrimitiveFlags::Visible;
    if (bHiddenInGame) Flags |= EPrimitiveFlags::HiddenInGame;
    if (bAffectDynamicIndirectLighting) Flags |= EPrimitiveFlags::AffectDynamicIndirectLighting;
    if (bCastDynamicShadow) Flags |= EPrimitiveFlags::CastDynamicShadow;
    if (bCastStaticShadow) Flags |= EPrimitiveFlags::CastStaticShadow;
    if (bRenderCustomDepth) Flags |= EPrimitiveFlags::RenderCustomDepth;
    if (bRenderInMainPass) Flags |= EPrimitiveFlags::RenderInMainPass;
    if (bRenderInDepthPass) Flags |= EPrimitiveFlags::RenderInDepthPass;
    
    return Flags;
}

// ============================================================================
// Protected Methods
// ============================================================================

void UPrimitiveComponent::OnCreateRenderState()
{
    // Create scene proxy
    SceneProxy = CreateSceneProxy();
    
    // Scene registration would happen here
    // In a full implementation, this would add the primitive to the scene
}

void UPrimitiveComponent::OnDestroyRenderState()
{
    // Scene unregistration would happen here
    // In a full implementation, this would remove the primitive from the scene
    
    // Note: SceneProxy is owned by the scene and will be deleted there
}

void UPrimitiveComponent::OnUpdateRenderState()
{
    // Called when render state needs to be updated
    // This would typically recreate the scene proxy
}

void UPrimitiveComponent::GeneratePrimitiveComponentId()
{
    PrimitiveComponentId = FPrimitiveComponentId(NextPrimitiveComponentId++);
}

} // namespace MonsterEngine
