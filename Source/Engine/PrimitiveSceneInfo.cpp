// Copyright Monster Engine. All Rights Reserved.

/**
 * @file PrimitiveSceneInfo.cpp
 * @brief Implementation of FPrimitiveSceneInfo
 */

#include "Engine/PrimitiveSceneInfo.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/LightSceneInfo.h"
#include "Engine/LightPrimitiveInteraction.h"
#include "Engine/Components/PrimitiveComponent.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

DEFINE_LOG_CATEGORY_STATIC(LogPrimitiveSceneInfo, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

FPrimitiveSceneInfo::FPrimitiveSceneInfo(UPrimitiveComponent* InComponent, FScene* InScene)
    : Component(InComponent)
    , Proxy(nullptr)
    , Scene(InScene)
    , PrimitiveComponentId()
    , PackedIndex(INDEX_NONE)
    , OctreeId(0)
    , LightList(nullptr)
    , AttachmentParent(nullptr)
    , AttachmentRoot(nullptr)
    , AttachmentGroupIndex(INDEX_NONE)
    , LastRenderTime(0)
    , LastVisibilityChangeTime(0)
    , bNeedsVisibilityCheck(true)
    , bWasRecentlyVisible(false)
    , bHasStaticMeshElements(false)
    , bIsRegistered(false)
    , bNeedsStaticMeshUpdate(false)
    , bIsAttached(false)
{
    if (InComponent)
    {
        PrimitiveComponentId = InComponent->GetPrimitiveComponentId();
    }
}

FPrimitiveSceneInfo::~FPrimitiveSceneInfo()
{
    // Clean up light interactions
    while (LightList)
    {
        FLightPrimitiveInteraction* Interaction = LightList;
        LightList = Interaction->GetNextLight();
        FLightPrimitiveInteraction::Destroy(Interaction);
    }
}

// ============================================================================
// Scene Registration
// ============================================================================

void FPrimitiveSceneInfo::AddToScene()
{
    if (bIsRegistered)
    {
        MR_LOG(LogPrimitiveSceneInfo, Warning, "Primitive already registered with scene");
        return;
    }

    bIsRegistered = true;

    // Create light interactions with all lights in the scene
    if (Scene && Proxy)
    {
        // Iterate through all lights and create interactions
        for (auto& LightCompact : Scene->Lights)
        {
            if (LightCompact.LightSceneInfo)
            {
                // Check if light affects this primitive
                if (LightCompact.LightSceneInfo->AffectsBounds(Proxy->GetBounds()))
                {
                    FLightPrimitiveInteraction* Interaction = 
                        FLightPrimitiveInteraction::Create(LightCompact.LightSceneInfo, this);
                    
                    if (Interaction)
                    {
                        // Add to both light and primitive lists
                        Interaction->AddToPrimitiveLightList(LightList);
                    }
                }
            }
        }
    }

    MR_LOG(LogPrimitiveSceneInfo, Verbose, "Primitive added to scene at index %d", PackedIndex);
}

void FPrimitiveSceneInfo::RemoveFromScene()
{
    if (!bIsRegistered)
    {
        return;
    }

    bIsRegistered = false;

    // Remove all light interactions
    while (LightList)
    {
        FLightPrimitiveInteraction* Interaction = LightList;
        LightList = Interaction->GetNextLight();
        
        // Remove from light's list
        Interaction->RemoveFromLightPrimitiveList();
        
        FLightPrimitiveInteraction::Destroy(Interaction);
    }

    MR_LOG(LogPrimitiveSceneInfo, Verbose, "Primitive removed from scene");
}

// ============================================================================
// Light Interactions
// ============================================================================

void FPrimitiveSceneInfo::AddLightInteraction(FLightPrimitiveInteraction* Interaction)
{
    if (Interaction)
    {
        Interaction->AddToPrimitiveLightList(LightList);
    }
}

void FPrimitiveSceneInfo::RemoveLightInteraction(FLightPrimitiveInteraction* Interaction)
{
    if (Interaction)
    {
        Interaction->RemoveFromPrimitiveLightList();
    }
}

void FPrimitiveSceneInfo::GetRelevantLights(TArray<FLightSceneInfo*>& OutLights) const
{
    OutLights.Empty();

    FLightPrimitiveInteraction* Interaction = LightList;
    while (Interaction)
    {
        OutLights.Add(Interaction->GetLight());
        Interaction = Interaction->GetNextLight();
    }
}

// ============================================================================
// Transform Update
// ============================================================================

void FPrimitiveSceneInfo::UpdateTransform(const FMatrix& NewLocalToWorld, const FBoxSphereBounds& NewBounds)
{
    if (Proxy)
    {
        Proxy->SetLocalToWorld(NewLocalToWorld);
        Proxy->SetBounds(NewBounds);
    }

    // Mark for visibility check
    bNeedsVisibilityCheck = true;

    // Update light interactions - some lights may no longer affect this primitive
    // and new lights may now affect it
    if (Scene && bIsRegistered)
    {
        // For now, just mark interactions as needing update
        // A full implementation would rebuild interactions if bounds changed significantly
    }
}

void FPrimitiveSceneInfo::MarkStaticMeshElementsDirty()
{
    bNeedsStaticMeshUpdate = true;
}

} // namespace MonsterEngine
