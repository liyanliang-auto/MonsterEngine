// Copyright Monster Engine. All Rights Reserved.

/**
 * @file LightPrimitiveInteraction.cpp
 * @brief Implementation of FLightPrimitiveInteraction
 */

#include "Engine/LightPrimitiveInteraction.h"
#include "Engine/LightSceneInfo.h"
#include "Engine/LightSceneProxy.h"
#include "Engine/PrimitiveSceneInfo.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

DEFINE_LOG_CATEGORY_STATIC(LogLightPrimitiveInteraction, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

FLightPrimitiveInteraction::FLightPrimitiveInteraction(
    FLightSceneInfo* InLightSceneInfo,
    FPrimitiveSceneInfo* InPrimitiveSceneInfo,
    bool bInIsDynamic)
    : LightSceneInfo(InLightSceneInfo)
    , PrimitiveSceneInfo(InPrimitiveSceneInfo)
    , NextPrimitive(nullptr)
    , PrevPrimitiveLink(nullptr)
    , NextLight(nullptr)
    , PrevLightLink(nullptr)
    , bCastShadow(false)
    , bHasStaticShadowing(false)
    , bHasDynamicShadowing(false)
    , bHasLightMap(false)
    , bIsDynamic(bInIsDynamic)
    , bIsPrimitiveOftenMoving(false)
    , bUncached(true)
    , bSelfShadowOnly(false)
{
    // Determine shadow properties
    if (LightSceneInfo && LightSceneInfo->GetProxy() && 
        PrimitiveSceneInfo && PrimitiveSceneInfo->GetProxy())
    {
        FLightSceneProxy* LightProxy = LightSceneInfo->GetProxy();
        FPrimitiveSceneProxy* PrimitiveProxy = PrimitiveSceneInfo->GetProxy();

        bCastShadow = LightProxy->CastsShadow() && PrimitiveProxy->CastsShadow();
        bHasStaticShadowing = LightProxy->CastsStaticShadow() && PrimitiveProxy->CastsStaticShadow();
        bHasDynamicShadowing = LightProxy->CastsDynamicShadow() && PrimitiveProxy->CastsDynamicShadow();
        
        bIsPrimitiveOftenMoving = PrimitiveProxy->IsMovable();
    }
}

FLightPrimitiveInteraction::~FLightPrimitiveInteraction()
{
    // Ensure we're removed from both lists
    if (PrevPrimitiveLink)
    {
        RemoveFromLightPrimitiveList();
    }
    if (PrevLightLink)
    {
        RemoveFromPrimitiveLightList();
    }
}

// ============================================================================
// Factory Methods
// ============================================================================

FLightPrimitiveInteraction* FLightPrimitiveInteraction::Create(
    FLightSceneInfo* LightSceneInfo,
    FPrimitiveSceneInfo* PrimitiveSceneInfo)
{
    if (!LightSceneInfo || !PrimitiveSceneInfo)
    {
        return nullptr;
    }

    // Check if the light affects the primitive
    FLightSceneProxy* LightProxy = LightSceneInfo->GetProxy();
    FPrimitiveSceneProxy* PrimitiveProxy = PrimitiveSceneInfo->GetProxy();

    if (!LightProxy || !PrimitiveProxy)
    {
        return nullptr;
    }

    // Check bounds intersection
    if (!LightProxy->AffectsBounds(PrimitiveProxy->GetBounds()))
    {
        return nullptr;
    }

    // Check lighting channels
    uint8 LightChannels = LightProxy->GetLightingChannelMask();
    uint8 PrimitiveChannels = PrimitiveProxy->GetPrimitiveComponentId().PrimIDValue > 0 ? 0x01 : 0x01; // Simplified
    
    if ((LightChannels & PrimitiveChannels) == 0)
    {
        return nullptr;
    }

    // Determine if this is a dynamic interaction
    bool bIsDynamic = PrimitiveProxy->IsMovable() || 
                      LightProxy->GetLightType() != ELightType::Directional;

    // Create the interaction
    FLightPrimitiveInteraction* Interaction = new FLightPrimitiveInteraction(
        LightSceneInfo, PrimitiveSceneInfo, bIsDynamic);

    MR_LOG(LogLightPrimitiveInteraction, Verbose, 
           "Created light-primitive interaction (Dynamic=%d, CastShadow=%d)",
           bIsDynamic, Interaction->HasShadow());

    return Interaction;
}

void FLightPrimitiveInteraction::Destroy(FLightPrimitiveInteraction* Interaction)
{
    if (Interaction)
    {
        delete Interaction;
    }
}

// ============================================================================
// Linked List Management - Light's Primitive List
// ============================================================================

void FLightPrimitiveInteraction::AddToLightPrimitiveList(FLightPrimitiveInteraction*& ListHead)
{
    // Insert at head of list
    NextPrimitive = ListHead;
    PrevPrimitiveLink = &ListHead;
    
    if (ListHead)
    {
        ListHead->PrevPrimitiveLink = &NextPrimitive;
    }
    
    ListHead = this;
}

void FLightPrimitiveInteraction::RemoveFromLightPrimitiveList()
{
    if (PrevPrimitiveLink)
    {
        // Update previous element's next pointer
        *PrevPrimitiveLink = NextPrimitive;
        
        // Update next element's prev pointer
        if (NextPrimitive)
        {
            NextPrimitive->PrevPrimitiveLink = PrevPrimitiveLink;
        }
        
        // Clear our pointers
        NextPrimitive = nullptr;
        PrevPrimitiveLink = nullptr;
    }
}

// ============================================================================
// Linked List Management - Primitive's Light List
// ============================================================================

void FLightPrimitiveInteraction::AddToPrimitiveLightList(FLightPrimitiveInteraction*& ListHead)
{
    // Insert at head of list
    NextLight = ListHead;
    PrevLightLink = &ListHead;
    
    if (ListHead)
    {
        ListHead->PrevLightLink = &NextLight;
    }
    
    ListHead = this;
}

void FLightPrimitiveInteraction::RemoveFromPrimitiveLightList()
{
    if (PrevLightLink)
    {
        // Update previous element's next pointer
        *PrevLightLink = NextLight;
        
        // Update next element's prev pointer
        if (NextLight)
        {
            NextLight->PrevLightLink = PrevLightLink;
        }
        
        // Clear our pointers
        NextLight = nullptr;
        PrevLightLink = nullptr;
    }
}

} // namespace MonsterEngine
