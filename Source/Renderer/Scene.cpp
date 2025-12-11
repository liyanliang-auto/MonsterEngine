// Copyright Monster Engine. All Rights Reserved.

/**
 * @file Scene.cpp
 * @brief Scene management implementation
 * 
 * Implements FScene class for managing scene primitives and lights.
 * Reference: UE5 Scene.cpp
 */

#include "Renderer/Scene.h"
#include "Core/Logging/Logging.h"

using namespace MonsterRender;

namespace MonsterEngine
{

// ============================================================================
// FPrimitiveSceneProxy Implementation
// ============================================================================

FPrimitiveViewRelevance FPrimitiveSceneProxy::GetViewRelevance(const FViewInfo* View) const
{
    FPrimitiveViewRelevance Result;
    
    // Default relevance - opaque, draws in main pass
    Result.bOpaqueRelevance = true;
    Result.bDrawInBasePass = true;
    Result.bRenderInMainPass = bRenderInMainPass;
    Result.bRenderInDepthPass = bRenderInDepthPass;
    Result.bShadowRelevance = bCastShadow;
    Result.bHasDynamicMeshElement = true;
    
    return Result;
}

// ============================================================================
// FScene Implementation
// ============================================================================

FScene::FScene()
    : FrameNumber(0)
    , NextComponentId(1)
{
    MR_LOG(LogRenderer, Log, "FScene (Renderer) created");
}

FScene::~FScene()
{
    // Clean up all primitives
    for (FPrimitiveSceneInfo* Primitive : Primitives)
    {
        if (Primitive)
        {
            delete Primitive;
        }
    }
    Primitives.Empty();
    
    // Clean up all lights
    for (FLightSceneInfo* Light : Lights)
    {
        if (Light)
        {
            delete Light;
        }
    }
    Lights.Empty();
    
    MR_LOG(LogRenderer, Log, "FScene (Renderer) destroyed");
}

// ============================================================================
// Primitive Management
// ============================================================================

FPrimitiveSceneInfo* FScene::AddPrimitive(FPrimitiveSceneProxy* Proxy)
{
    if (!Proxy)
    {
        MR_LOG(LogRenderer, Warning, "Attempted to add null primitive proxy to scene");
        return nullptr;
    }
    
    // Create scene info for the primitive
    FPrimitiveSceneInfo* PrimitiveSceneInfo = new FPrimitiveSceneInfo(Proxy, this);
    
    // Assign component ID
    PrimitiveSceneInfo->SetComponentId(NextComponentId++);
    
    // Add to scene arrays
    AddPrimitiveToArrays(PrimitiveSceneInfo);
    
    MR_LOG(LogRenderer, Verbose, "Added primitive to scene, index: %d, total primitives: %d",
                 PrimitiveSceneInfo->GetIndex(), Primitives.Num());
    
    return PrimitiveSceneInfo;
}

void FScene::RemovePrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo)
{
    if (!PrimitiveSceneInfo)
    {
        MR_LOG(LogRenderer, Warning, "Attempted to remove null primitive from scene");
        return;
    }
    
    int32 Index = PrimitiveSceneInfo->GetIndex();
    if (Index == INDEX_NONE || Index >= Primitives.Num())
    {
        MR_LOG(LogRenderer, Warning, "Invalid primitive index for removal: %d", Index);
        return;
    }
    
    // Remove from arrays
    RemovePrimitiveFromArrays(PrimitiveSceneInfo);
    
    // Delete the scene info
    delete PrimitiveSceneInfo;
    
    MR_LOG(LogRenderer, Verbose, "Removed primitive from scene, remaining primitives: %d", Primitives.Num());
}

void FScene::UpdatePrimitiveTransform(FPrimitiveSceneInfo* PrimitiveSceneInfo, 
                                      const Math::FMatrix& NewTransform)
{
    if (!PrimitiveSceneInfo || !PrimitiveSceneInfo->Proxy)
    {
        return;
    }
    
    int32 Index = PrimitiveSceneInfo->GetIndex();
    if (Index == INDEX_NONE || Index >= Primitives.Num())
    {
        return;
    }
    
    // Update the proxy transform
    FPrimitiveSceneProxy* Proxy = PrimitiveSceneInfo->Proxy;
    Proxy->SetLocalToWorld(NewTransform);
    
    // Update bounds in the scene arrays
    PrimitiveBounds[Index].BoxSphereBounds = Proxy->GetBounds();
}

void FScene::AddPrimitiveToArrays(FPrimitiveSceneInfo* PrimitiveSceneInfo)
{
    // Get the new index
    int32 NewIndex = Primitives.Num();
    PrimitiveSceneInfo->SetIndex(NewIndex);
    
    // Add to all parallel arrays
    Primitives.Add(PrimitiveSceneInfo);
    
    // Add bounds
    FPrimitiveBounds NewBounds;
    if (PrimitiveSceneInfo->Proxy)
    {
        NewBounds.BoxSphereBounds = PrimitiveSceneInfo->Proxy->GetBounds();
        NewBounds.MinDrawDistance = PrimitiveSceneInfo->Proxy->MinDrawDistance;
        NewBounds.MaxCullDistance = PrimitiveSceneInfo->Proxy->MaxDrawDistance;
    }
    PrimitiveBounds.Add(NewBounds);
    
    // Add occlusion flags
    uint8 OcclusionFlags = EOcclusionFlags::CanBeOccluded;
    PrimitiveOcclusionFlags.Add(OcclusionFlags);
    
    // Add component ID
    PrimitiveComponentIds.Add(PrimitiveSceneInfo->GetComponentId());
}

void FScene::RemovePrimitiveFromArrays(FPrimitiveSceneInfo* PrimitiveSceneInfo)
{
    int32 Index = PrimitiveSceneInfo->GetIndex();
    if (Index == INDEX_NONE || Index >= Primitives.Num())
    {
        return;
    }
    
    // If this is not the last element, swap with the last element
    int32 LastIndex = Primitives.Num() - 1;
    if (Index != LastIndex)
    {
        // Swap with last element
        Primitives[Index] = Primitives[LastIndex];
        PrimitiveBounds[Index] = PrimitiveBounds[LastIndex];
        PrimitiveOcclusionFlags[Index] = PrimitiveOcclusionFlags[LastIndex];
        PrimitiveComponentIds[Index] = PrimitiveComponentIds[LastIndex];
        
        // Update the swapped primitive's index
        if (Primitives[Index])
        {
            Primitives[Index]->SetIndex(Index);
        }
    }
    
    // Remove the last element
    Primitives.RemoveAt(LastIndex);
    PrimitiveBounds.RemoveAt(LastIndex);
    PrimitiveOcclusionFlags.RemoveAt(LastIndex);
    PrimitiveComponentIds.RemoveAt(LastIndex);
    
    // Invalidate the removed primitive's index
    PrimitiveSceneInfo->SetIndex(INDEX_NONE);
}

// ============================================================================
// Light Management
// ============================================================================

FLightSceneInfo* FScene::AddLight(FLightSceneProxy* Proxy)
{
    if (!Proxy)
    {
        MR_LOG(LogRenderer, Warning, "Attempted to add null light proxy to scene");
        return nullptr;
    }
    
    // Create scene info for the light
    FLightSceneInfo* LightSceneInfo = new FLightSceneInfo(Proxy, this);
    
    // Assign ID
    int32 NewId = Lights.Num();
    LightSceneInfo->SetId(NewId);
    
    // Add to lights array
    Lights.Add(LightSceneInfo);
    
    MR_LOG(LogRenderer, Verbose, "Added light to scene, id: %d, total lights: %d",
                 NewId, Lights.Num());
    
    return LightSceneInfo;
}

void FScene::RemoveLight(FLightSceneInfo* LightSceneInfo)
{
    if (!LightSceneInfo)
    {
        MR_LOG(LogRenderer, Warning, "Attempted to remove null light from scene");
        return;
    }
    
    int32 Id = LightSceneInfo->GetId();
    if (Id == INDEX_NONE || Id >= Lights.Num())
    {
        MR_LOG(LogRenderer, Warning, "Invalid light id for removal: %d", Id);
        return;
    }
    
    // Swap with last element if not already last
    int32 LastIndex = Lights.Num() - 1;
    if (Id != LastIndex)
    {
        Lights[Id] = Lights[LastIndex];
        if (Lights[Id])
        {
            Lights[Id]->SetId(Id);
        }
    }
    
    // Remove last element
    Lights.RemoveAt(LastIndex);
    
    // Invalidate the removed light's ID
    LightSceneInfo->SetId(INDEX_NONE);
    
    // Delete the scene info
    delete LightSceneInfo;
    
    MR_LOG(LogRenderer, Verbose, "Removed light from scene, remaining lights: %d", Lights.Num());
}

// ============================================================================
// Frame Management
// ============================================================================

void FScene::BeginFrame()
{
    FrameNumber++;
}

void FScene::EndFrame()
{
    // Any end-of-frame cleanup can go here
}

} // namespace MonsterEngine
