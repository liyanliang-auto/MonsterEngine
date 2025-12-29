// Copyright Monster Engine. All Rights Reserved.

/**
 * @file PrimitiveSceneProxy.cpp
 * @brief Implementation of FPrimitiveSceneProxy
 */

#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/PrimitiveSceneInfo.h"
#include "Engine/Scene.h"
#include "Engine/Components/PrimitiveComponent.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Use global log category (defined in LogCategories.cpp)
using MonsterRender::LogPrimitiveSceneProxy;

// ============================================================================
// Construction / Destruction
// ============================================================================

FPrimitiveSceneProxy::FPrimitiveSceneProxy(const UPrimitiveComponent* InComponent, const char* InResourceName)
    : PrimitiveComponent(InComponent)
    , PrimitiveSceneInfo(nullptr)
    , Scene(nullptr)
    , LocalToWorld(FMatrix::Identity)
    , Bounds()
    , LocalBounds(ForceInit)
    , PrimitiveComponentId()
    , Mobility(EComponentMobility::Static)
    , MinDrawDistance(0.0f)
    , MaxDrawDistance(0.0f)
    , ResourceName(InResourceName ? InResourceName : "Unknown")
    , OwnerName("Unknown")
    , bVisible(true)
    , bCastShadow(true)
    , bCastDynamicShadow(true)
    , bCastStaticShadow(true)
    , bReceiveShadow(true)
    , bRenderInMainPass(true)
    , bRenderInDepthPass(true)
    , bRenderCustomDepth(false)
    , bHasDynamicTransform(false)
    , bAffectDynamicIndirectLighting(true)
    , bHiddenInGame(false)
    , bSelectable(true)
{
    if (InComponent)
    {
        // Copy properties from component
        LocalToWorld = InComponent->GetComponentToWorld();
        Bounds = InComponent->GetBounds();
        Mobility = InComponent->GetMobility();
        MinDrawDistance = InComponent->GetMinDrawDistance();
        MaxDrawDistance = InComponent->GetLDMaxDrawDistance();
        PrimitiveComponentId = InComponent->GetPrimitiveComponentId();
        
        bVisible = InComponent->IsVisible();
        bCastShadow = InComponent->CastsShadow();
        bCastDynamicShadow = InComponent->CastsDynamicShadow();
        bCastStaticShadow = InComponent->CastsStaticShadow();
        bReceiveShadow = InComponent->ReceivesShadow();
        bRenderInMainPass = InComponent->ShouldRenderInMainPass();
        bRenderInDepthPass = InComponent->ShouldRenderInDepthPass();
        bRenderCustomDepth = InComponent->UsesCustomDepth();
        bAffectDynamicIndirectLighting = InComponent->AffectsDynamicIndirectLighting();
        bHiddenInGame = InComponent->IsHiddenInGame();
        
        bHasDynamicTransform = (Mobility == EComponentMobility::Movable);
    }
}

FPrimitiveSceneProxy::~FPrimitiveSceneProxy()
{
    MR_LOG(LogPrimitiveSceneProxy, Verbose, "PrimitiveSceneProxy destroyed: %s", ResourceName);
}

// ============================================================================
// Transform
// ============================================================================

void FPrimitiveSceneProxy::SetLocalToWorld(const FMatrix& InLocalToWorld)
{
    LocalToWorld = InLocalToWorld;
    
    // Update bounds based on new transform
    if (LocalBounds.IsValidBox())
    {
        // Transform local bounds to world space
        // This is a simplified version - a full implementation would
        // properly transform the bounding box
        FVector Center = LocalBounds.GetCenter();
        FVector Extent = LocalBounds.GetExtent();
        
        // Transform center
        FVector WorldCenter = LocalToWorld.TransformPosition(Center).GetXYZ();
        
        // Scale extent (simplified - doesn't handle rotation properly)
        FVector Scale = LocalToWorld.GetScaleVector();
        FVector WorldExtent = Extent * Scale;
        
        Bounds = FBoxSphereBounds(WorldCenter, WorldExtent, WorldExtent.Size());
    }
}

// ============================================================================
// View Relevance
// ============================================================================

FPrimitiveViewRelevance FPrimitiveSceneProxy::GetViewRelevance(const class FSceneView* View) const
{
    FPrimitiveViewRelevance Result;
    
    Result.bDrawRelevance = bVisible && !bHiddenInGame;
    Result.bShadowRelevance = bCastShadow;
    Result.bDynamicRelevance = (Mobility == EComponentMobility::Movable);
    Result.bStaticRelevance = (Mobility == EComponentMobility::Static);
    Result.bRenderInMainPass = bRenderInMainPass;
    Result.bRenderCustomDepth = bRenderCustomDepth;
    Result.bHasTranslucency = false; // Would be set by derived classes
    Result.bHasVelocity = bHasDynamicTransform;
    
    return Result;
}

} // namespace MonsterEngine
