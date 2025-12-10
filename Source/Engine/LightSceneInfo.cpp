// Copyright Monster Engine. All Rights Reserved.

/**
 * @file LightSceneInfo.cpp
 * @brief Implementation of FLightSceneInfo
 */

#include "Engine/LightSceneInfo.h"
#include "Engine/LightSceneProxy.h"
#include "Engine/LightPrimitiveInteraction.h"
#include "Engine/PrimitiveSceneInfo.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

DEFINE_LOG_CATEGORY_STATIC(LogLightSceneInfo, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

FLightSceneInfo::FLightSceneInfo(FLightSceneProxy* InProxy, bool bInVisible)
    : Proxy(InProxy)
    , Scene(nullptr)
    , Id(INDEX_NONE)
    , OctreeId(0)
    , ShadowMapChannel(INDEX_NONE)
    , DynamicInteractionOftenMovingPrimitiveList(nullptr)
    , DynamicInteractionStaticPrimitiveList(nullptr)
    , NumDynamicInteractions(0)
    , bVisible(bInVisible)
    , bPrecomputedLightingValid(false)
    , bIsRegistered(false)
    , bNeedsInteractionRebuild(false)
{
}

FLightSceneInfo::~FLightSceneInfo()
{
    // Clean up interactions
    while (DynamicInteractionOftenMovingPrimitiveList)
    {
        FLightPrimitiveInteraction* Interaction = DynamicInteractionOftenMovingPrimitiveList;
        DynamicInteractionOftenMovingPrimitiveList = Interaction->GetNextPrimitive();
        FLightPrimitiveInteraction::Destroy(Interaction);
    }

    while (DynamicInteractionStaticPrimitiveList)
    {
        FLightPrimitiveInteraction* Interaction = DynamicInteractionStaticPrimitiveList;
        DynamicInteractionStaticPrimitiveList = Interaction->GetNextPrimitive();
        FLightPrimitiveInteraction::Destroy(Interaction);
    }
}

// ============================================================================
// Scene Registration
// ============================================================================

void FLightSceneInfo::AddToScene()
{
    if (bIsRegistered)
    {
        MR_LOG(LogLightSceneInfo, Warning, "Light already registered with scene");
        return;
    }

    bIsRegistered = true;

    // Create interactions with all primitives in the scene
    if (Scene)
    {
        for (FPrimitiveSceneInfo* PrimitiveInfo : Scene->GetPrimitives())
        {
            if (PrimitiveInfo && AffectsBounds(PrimitiveInfo->GetProxy()->GetBounds()))
            {
                FLightPrimitiveInteraction* Interaction = 
                    FLightPrimitiveInteraction::Create(this, PrimitiveInfo);
                
                if (Interaction)
                {
                    // Determine which list to add to based on primitive mobility
                    if (Interaction->IsPrimitiveOftenMoving())
                    {
                        Interaction->AddToLightPrimitiveList(DynamicInteractionOftenMovingPrimitiveList);
                    }
                    else
                    {
                        Interaction->AddToLightPrimitiveList(DynamicInteractionStaticPrimitiveList);
                    }
                    NumDynamicInteractions++;
                }
            }
        }
    }

    MR_LOG(LogLightSceneInfo, Verbose, "Light added to scene with %d interactions", NumDynamicInteractions);
}

void FLightSceneInfo::RemoveFromScene()
{
    if (!bIsRegistered)
    {
        return;
    }

    bIsRegistered = false;

    // Remove all interactions
    while (DynamicInteractionOftenMovingPrimitiveList)
    {
        FLightPrimitiveInteraction* Interaction = DynamicInteractionOftenMovingPrimitiveList;
        DynamicInteractionOftenMovingPrimitiveList = Interaction->GetNextPrimitive();
        
        // Remove from primitive's list
        Interaction->RemoveFromPrimitiveLightList();
        
        FLightPrimitiveInteraction::Destroy(Interaction);
    }

    while (DynamicInteractionStaticPrimitiveList)
    {
        FLightPrimitiveInteraction* Interaction = DynamicInteractionStaticPrimitiveList;
        DynamicInteractionStaticPrimitiveList = Interaction->GetNextPrimitive();
        
        // Remove from primitive's list
        Interaction->RemoveFromPrimitiveLightList();
        
        FLightPrimitiveInteraction::Destroy(Interaction);
    }

    NumDynamicInteractions = 0;

    MR_LOG(LogLightSceneInfo, Verbose, "Light removed from scene");
}

// ============================================================================
// Accessors
// ============================================================================

ELightType FLightSceneInfo::GetLightType() const
{
    return Proxy ? Proxy->GetLightType() : ELightType::Point;
}

// ============================================================================
// Primitive Interactions
// ============================================================================

void FLightSceneInfo::AddInteraction(FLightPrimitiveInteraction* Interaction)
{
    if (!Interaction)
    {
        return;
    }

    if (Interaction->IsPrimitiveOftenMoving())
    {
        Interaction->AddToLightPrimitiveList(DynamicInteractionOftenMovingPrimitiveList);
    }
    else
    {
        Interaction->AddToLightPrimitiveList(DynamicInteractionStaticPrimitiveList);
    }
    NumDynamicInteractions++;
}

void FLightSceneInfo::RemoveInteraction(FLightPrimitiveInteraction* Interaction)
{
    if (!Interaction)
    {
        return;
    }

    Interaction->RemoveFromLightPrimitiveList();
    NumDynamicInteractions--;
}

void FLightSceneInfo::GetAffectedPrimitives(TArray<FPrimitiveSceneInfo*>& OutPrimitives) const
{
    OutPrimitives.Empty();

    // Add from often moving list
    FLightPrimitiveInteraction* Interaction = DynamicInteractionOftenMovingPrimitiveList;
    while (Interaction)
    {
        OutPrimitives.Add(Interaction->GetPrimitive());
        Interaction = Interaction->GetNextPrimitive();
    }

    // Add from static list
    Interaction = DynamicInteractionStaticPrimitiveList;
    while (Interaction)
    {
        OutPrimitives.Add(Interaction->GetPrimitive());
        Interaction = Interaction->GetNextPrimitive();
    }
}

bool FLightSceneInfo::AffectsPrimitive(const FPrimitiveSceneInfo* PrimitiveSceneInfo) const
{
    if (!PrimitiveSceneInfo)
    {
        return false;
    }

    // Check in both lists
    FLightPrimitiveInteraction* Interaction = DynamicInteractionOftenMovingPrimitiveList;
    while (Interaction)
    {
        if (Interaction->GetPrimitive() == PrimitiveSceneInfo)
        {
            return true;
        }
        Interaction = Interaction->GetNextPrimitive();
    }

    Interaction = DynamicInteractionStaticPrimitiveList;
    while (Interaction)
    {
        if (Interaction->GetPrimitive() == PrimitiveSceneInfo)
        {
            return true;
        }
        Interaction = Interaction->GetNextPrimitive();
    }

    return false;
}

// ============================================================================
// Shadow
// ============================================================================

bool FLightSceneInfo::CastsShadow() const
{
    return Proxy ? Proxy->CastsShadow() : false;
}

bool FLightSceneInfo::CastsStaticShadow() const
{
    return Proxy ? Proxy->CastsStaticShadow() : false;
}

bool FLightSceneInfo::CastsDynamicShadow() const
{
    return Proxy ? Proxy->CastsDynamicShadow() : false;
}

// ============================================================================
// Bounds
// ============================================================================

FBoxSphereBounds FLightSceneInfo::GetBoundingSphere() const
{
    return Proxy ? Proxy->GetBounds() : FBoxSphereBounds();
}

bool FLightSceneInfo::AffectsBounds(const FBoxSphereBounds& Bounds) const
{
    return Proxy ? Proxy->AffectsBounds(Bounds) : false;
}

// ============================================================================
// Transform Update
// ============================================================================

void FLightSceneInfo::UpdateTransform()
{
    // Mark interactions as needing rebuild
    bNeedsInteractionRebuild = true;
    
    MR_LOG(LogLightSceneInfo, Verbose, "Light transform updated");
}

void FLightSceneInfo::UpdateColorAndBrightness()
{
    // Color/brightness changes don't require interaction rebuild
    MR_LOG(LogLightSceneInfo, Verbose, "Light color/brightness updated");
}

} // namespace MonsterEngine
