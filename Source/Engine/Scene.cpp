// Copyright Monster Engine. All Rights Reserved.

/**
 * @file Scene.cpp
 * @brief Implementation of FScene and FSceneInterface
 */

#include "Engine/Scene.h"
#include "Engine/PrimitiveSceneInfo.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/LightSceneInfo.h"
#include "Engine/LightSceneProxy.h"
#include "Engine/Components/PrimitiveComponent.h"
#include "Engine/Components/LightComponent.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Define log category for scene system
DEFINE_LOG_CATEGORY_STATIC(LogScene, Log, All);

// ============================================================================
// FSceneInterface Implementation
// ============================================================================

FSceneInterface::FSceneInterface()
{
}

// ============================================================================
// FLightSceneInfoCompact Implementation
// ============================================================================

void FLightSceneInfoCompact::Init(FLightSceneInfo* InLightSceneInfo)
{
    LightSceneInfo = InLightSceneInfo;
    
    if (LightSceneInfo && LightSceneInfo->GetProxy())
    {
        FLightSceneProxy* Proxy = LightSceneInfo->GetProxy();
        LightType = Proxy->GetLightType();
        Color = FVector(Proxy->GetColor().R, Proxy->GetColor().G, Proxy->GetColor().B);
        Position = Proxy->GetPosition();
        Direction = Proxy->GetDirection();
        Radius = Proxy->GetRadius();
        bCastShadow = Proxy->CastsShadow();
        bCastStaticShadow = Proxy->CastsStaticShadow();
        bCastDynamicShadow = Proxy->CastsDynamicShadow();
    }
}

// ============================================================================
// FScene Implementation
// ============================================================================

FScene::FScene(UWorld* InWorld, bool bInRequiresHitProxies, bool bInIsEditorScene)
    : World(InWorld)
    , SimpleDirectionalLight(nullptr)
    , SkyLight(nullptr)
    , bRequiresHitProxies(bInRequiresHitProxies)
    , bIsEditorScene(bInIsEditorScene)
    , FrameNumber(0)
    , NumUncachedStaticLightingInteractions(0)
    , NextPrimitiveComponentId(1)
{
    MR_LOG(LogScene, Log, "FScene created (World=%p, RequiresHitProxies=%d, IsEditorScene=%d)",
           World, bRequiresHitProxies, bIsEditorScene);
}

FScene::~FScene()
{
    MR_LOG(LogScene, Log, "FScene destroyed");
    
    // Clean up primitives
    for (FPrimitiveSceneInfo* Primitive : Primitives)
    {
        if (Primitive)
        {
            delete Primitive;
        }
    }
    Primitives.Empty();
    
    // Clean up lights
    for (auto& LightCompact : Lights)
    {
        if (LightCompact.LightSceneInfo)
        {
            delete LightCompact.LightSceneInfo;
        }
    }
    Lights.Empty();
}

// ============================================================================
// Primitive Management
// ============================================================================

void FScene::AddPrimitive(UPrimitiveComponent* Primitive)
{
    if (!Primitive)
    {
        MR_LOG(LogScene, Warning, "AddPrimitive called with null primitive");
        return;
    }

    MR_LOG(LogScene, Verbose, "Adding primitive to scene");

    // Create scene proxy
    FPrimitiveSceneProxy* SceneProxy = Primitive->CreateSceneProxy();
    if (!SceneProxy)
    {
        MR_LOG(LogScene, Verbose, "Primitive did not create a scene proxy");
        return;
    }

    // Create scene info
    FPrimitiveSceneInfo* SceneInfo = new FPrimitiveSceneInfo(Primitive, this);
    SceneInfo->SetProxy(SceneProxy);
    SceneProxy->SetPrimitiveSceneInfo(SceneInfo);
    SceneProxy->SetScene(this);

    // Assign component ID
    FPrimitiveComponentId ComponentId(NextPrimitiveComponentId++);
    SceneProxy->SetPrimitiveComponentId(ComponentId);

    // Add to scene on render thread
    AddPrimitiveSceneInfo_RenderThread(SceneInfo);
}

void FScene::RemovePrimitive(UPrimitiveComponent* Primitive)
{
    if (!Primitive)
    {
        return;
    }

    FPrimitiveSceneInfo* SceneInfo = Primitive->GetPrimitiveSceneInfo();
    if (SceneInfo)
    {
        RemovePrimitiveSceneInfo_RenderThread(SceneInfo);
    }
}

void FScene::ReleasePrimitive(UPrimitiveComponent* Primitive)
{
    RemovePrimitive(Primitive);
}

void FScene::UpdatePrimitiveTransform(UPrimitiveComponent* Primitive)
{
    if (!Primitive)
    {
        return;
    }

    FPrimitiveSceneInfo* SceneInfo = Primitive->GetPrimitiveSceneInfo();
    if (SceneInfo && SceneInfo->GetProxy())
    {
        // Update transform
        FMatrix NewLocalToWorld = Primitive->GetComponentToWorld();
        FBoxSphereBounds NewBounds = Primitive->GetBounds();
        
        SceneInfo->UpdateTransform(NewLocalToWorld, NewBounds);
        
        // Update packed arrays
        int32 PackedIndex = SceneInfo->GetPackedIndex();
        if (PackedIndex >= 0 && PackedIndex < Primitives.Num())
        {
            PrimitiveTransforms[PackedIndex] = NewLocalToWorld;
            PrimitiveBounds[PackedIndex].BoxSphereBounds = NewBounds;
        }
    }
}

void FScene::UpdatePrimitiveAttachment(UPrimitiveComponent* Primitive)
{
    if (!Primitive)
    {
        return;
    }

    // Update attachment group info
    FPrimitiveSceneInfo* SceneInfo = Primitive->GetPrimitiveSceneInfo();
    if (SceneInfo)
    {
        // Handle attachment hierarchy updates
        // This would involve updating the AttachmentGroups map
    }
}

FPrimitiveSceneInfo* FScene::GetPrimitiveSceneInfo(int32 PrimitiveIndex)
{
    if (PrimitiveIndex >= 0 && PrimitiveIndex < Primitives.Num())
    {
        return Primitives[PrimitiveIndex];
    }
    return nullptr;
}

FPrimitiveSceneInfo* FScene::GetPrimitive(int32 Index) const
{
    if (Index >= 0 && Index < Primitives.Num())
    {
        return Primitives[Index];
    }
    return nullptr;
}

void FScene::AddPrimitiveSceneInfo_RenderThread(FPrimitiveSceneInfo* PrimitiveSceneInfo)
{
    if (!PrimitiveSceneInfo)
    {
        return;
    }

    // Add to packed arrays
    int32 PackedIndex = Primitives.Num();
    PrimitiveSceneInfo->SetPackedIndex(PackedIndex);

    Primitives.Add(PrimitiveSceneInfo);
    
    FPrimitiveSceneProxy* Proxy = PrimitiveSceneInfo->GetProxy();
    if (Proxy)
    {
        PrimitiveTransforms.Add(Proxy->GetLocalToWorld());
        PrimitiveSceneProxies.Add(Proxy);
        
        FPrimitiveBounds Bounds;
        Bounds.BoxSphereBounds = Proxy->GetBounds();
        Bounds.MinDrawDistance = Proxy->GetMinDrawDistance();
        Bounds.MaxDrawDistance = Proxy->GetMaxDrawDistance();
        PrimitiveBounds.Add(Bounds);
        
        PrimitiveFlagsCompact.Add(FPrimitiveFlagsCompact(EPrimitiveFlags::Default));
        PrimitiveVisibilityIds.Add(FPrimitiveVisibilityId());
        PrimitiveOcclusionFlags.Add(EOcclusionFlags::CanBeOccluded);
        PrimitiveOcclusionBounds.Add(Proxy->GetBounds());
        PrimitiveComponentIds.Add(Proxy->GetPrimitiveComponentId());
    }

    // Add to scene
    PrimitiveSceneInfo->AddToScene();

    MR_LOG(LogScene, Verbose, "Primitive added at index %d, total primitives: %d",
           PackedIndex, Primitives.Num());
}

void FScene::RemovePrimitiveSceneInfo_RenderThread(FPrimitiveSceneInfo* PrimitiveSceneInfo)
{
    if (!PrimitiveSceneInfo)
    {
        return;
    }

    int32 PackedIndex = PrimitiveSceneInfo->GetPackedIndex();
    if (PackedIndex < 0 || PackedIndex >= Primitives.Num())
    {
        return;
    }

    // Remove from scene
    PrimitiveSceneInfo->RemoveFromScene();

    // Swap with last element and remove
    int32 LastIndex = Primitives.Num() - 1;
    if (PackedIndex != LastIndex)
    {
        // Update the moved primitive's packed index
        FPrimitiveSceneInfo* MovedPrimitive = Primitives[LastIndex];
        MovedPrimitive->SetPackedIndex(PackedIndex);

        // Swap in all arrays
        Primitives[PackedIndex] = Primitives[LastIndex];
        PrimitiveTransforms[PackedIndex] = PrimitiveTransforms[LastIndex];
        PrimitiveSceneProxies[PackedIndex] = PrimitiveSceneProxies[LastIndex];
        PrimitiveBounds[PackedIndex] = PrimitiveBounds[LastIndex];
        PrimitiveFlagsCompact[PackedIndex] = PrimitiveFlagsCompact[LastIndex];
        PrimitiveVisibilityIds[PackedIndex] = PrimitiveVisibilityIds[LastIndex];
        PrimitiveOcclusionFlags[PackedIndex] = PrimitiveOcclusionFlags[LastIndex];
        PrimitiveOcclusionBounds[PackedIndex] = PrimitiveOcclusionBounds[LastIndex];
        PrimitiveComponentIds[PackedIndex] = PrimitiveComponentIds[LastIndex];
    }

    // Remove last element
    Primitives.RemoveAt(LastIndex);
    PrimitiveTransforms.RemoveAt(LastIndex);
    PrimitiveSceneProxies.RemoveAt(LastIndex);
    PrimitiveBounds.RemoveAt(LastIndex);
    PrimitiveFlagsCompact.RemoveAt(LastIndex);
    PrimitiveVisibilityIds.RemoveAt(LastIndex);
    PrimitiveOcclusionFlags.RemoveAt(LastIndex);
    PrimitiveOcclusionBounds.RemoveAt(LastIndex);
    PrimitiveComponentIds.RemoveAt(LastIndex);

    // Delete scene info and proxy
    delete PrimitiveSceneInfo->GetProxy();
    delete PrimitiveSceneInfo;

    MR_LOG(LogScene, Verbose, "Primitive removed, total primitives: %d", Primitives.Num());
}

// ============================================================================
// Light Management
// ============================================================================

void FScene::AddLight(ULightComponent* Light)
{
    if (!Light)
    {
        MR_LOG(LogScene, Warning, "AddLight called with null light");
        return;
    }

    MR_LOG(LogScene, Verbose, "Adding light to scene");

    // Create scene proxy
    FLightSceneProxy* SceneProxy = Light->CreateSceneProxy();
    if (!SceneProxy)
    {
        MR_LOG(LogScene, Verbose, "Light did not create a scene proxy");
        return;
    }

    // Create scene info
    FLightSceneInfo* SceneInfo = new FLightSceneInfo(SceneProxy, true);
    SceneInfo->SetScene(this);
    SceneProxy->SetLightSceneInfo(SceneInfo);

    // Add to scene on render thread
    AddLightSceneInfo_RenderThread(SceneInfo);
}

void FScene::RemoveLight(ULightComponent* Light)
{
    if (!Light)
    {
        return;
    }

    FLightSceneInfo* SceneInfo = Light->GetLightSceneInfo();
    if (SceneInfo)
    {
        RemoveLightSceneInfo_RenderThread(SceneInfo);
    }
}

void FScene::AddInvisibleLight(ULightComponent* Light)
{
    if (!Light)
    {
        return;
    }

    // Create scene proxy
    FLightSceneProxy* SceneProxy = Light->CreateSceneProxy();
    if (!SceneProxy)
    {
        return;
    }

    // Create scene info (invisible)
    FLightSceneInfo* SceneInfo = new FLightSceneInfo(SceneProxy, false);
    SceneInfo->SetScene(this);
    SceneProxy->SetLightSceneInfo(SceneInfo);

    // Add to invisible lights
    FLightSceneInfoCompact Compact;
    Compact.Init(SceneInfo);
    int32 Index = InvisibleLights.Add(Compact);
    SceneInfo->SetId(Index);
}

void FScene::UpdateLightTransform(ULightComponent* Light)
{
    if (!Light)
    {
        return;
    }

    FLightSceneInfo* SceneInfo = Light->GetLightSceneInfo();
    if (SceneInfo)
    {
        SceneInfo->UpdateTransform();
        
        // Update compact info
        int32 Id = SceneInfo->GetId();
        if (Id >= 0 && Lights.IsValidIndex(Id))
        {
            Lights[Id].Init(SceneInfo);
        }
    }
}

void FScene::UpdateLightColorAndBrightness(ULightComponent* Light)
{
    if (!Light)
    {
        return;
    }

    FLightSceneInfo* SceneInfo = Light->GetLightSceneInfo();
    if (SceneInfo)
    {
        SceneInfo->UpdateColorAndBrightness();
        
        // Update compact info
        int32 Id = SceneInfo->GetId();
        if (Id >= 0 && Lights.IsValidIndex(Id))
        {
            Lights[Id].Init(SceneInfo);
        }
    }
}

void FScene::SetSkyLight(FSkyLightSceneProxy* Light)
{
    if (SkyLight != Light)
    {
        if (Light)
        {
            SkyLightStack.Add(Light);
        }
        SkyLight = Light;
        MR_LOG(LogScene, Verbose, "Sky light set: %p", Light);
    }
}

void FScene::DisableSkyLight(FSkyLightSceneProxy* Light)
{
    if (SkyLight == Light)
    {
        SkyLightStack.Remove(Light);
        SkyLight = SkyLightStack.Num() > 0 ? SkyLightStack.Last() : nullptr;
        MR_LOG(LogScene, Verbose, "Sky light disabled, new sky light: %p", SkyLight);
    }
}

void FScene::AddLightSceneInfo_RenderThread(FLightSceneInfo* LightSceneInfo)
{
    if (!LightSceneInfo)
    {
        return;
    }

    // Add to lights array
    FLightSceneInfoCompact Compact;
    Compact.Init(LightSceneInfo);
    int32 Index = Lights.Add(Compact);
    LightSceneInfo->SetId(Index);

    // Track directional lights
    if (LightSceneInfo->GetLightType() == ELightType::Directional)
    {
        DirectionalLights.Add(LightSceneInfo);
        
        // Set as simple directional light if none exists
        if (!SimpleDirectionalLight)
        {
            SimpleDirectionalLight = LightSceneInfo;
        }
    }

    // Add to scene
    LightSceneInfo->AddToScene();

    MR_LOG(LogScene, Verbose, "Light added at index %d, total lights: %d",
           Index, Lights.Num());
}

void FScene::RemoveLightSceneInfo_RenderThread(FLightSceneInfo* LightSceneInfo)
{
    if (!LightSceneInfo)
    {
        return;
    }

    int32 Id = LightSceneInfo->GetId();
    
    // Remove from scene
    LightSceneInfo->RemoveFromScene();

    // Remove from directional lights
    if (LightSceneInfo->GetLightType() == ELightType::Directional)
    {
        DirectionalLights.Remove(LightSceneInfo);
        
        if (SimpleDirectionalLight == LightSceneInfo)
        {
            SimpleDirectionalLight = DirectionalLights.Num() > 0 ? DirectionalLights[0] : nullptr;
        }
    }

    // Remove from lights array
    if (Id >= 0 && Lights.IsValidIndex(Id))
    {
        Lights.RemoveAt(Id);
    }

    // Delete scene info and proxy
    delete LightSceneInfo->GetProxy();
    delete LightSceneInfo;

    MR_LOG(LogScene, Verbose, "Light removed, total lights: %d", Lights.Num());
}

// ============================================================================
// Decal Management
// ============================================================================

void FScene::AddDecal(UDecalComponent* Component)
{
    // Decal implementation would go here
    MR_LOG(LogScene, Verbose, "AddDecal called");
}

void FScene::RemoveDecal(UDecalComponent* Component)
{
    // Decal implementation would go here
    MR_LOG(LogScene, Verbose, "RemoveDecal called");
}

void FScene::UpdateDecalTransform(UDecalComponent* Component)
{
    // Decal implementation would go here
}

// ============================================================================
// Scene Queries
// ============================================================================

void FScene::GetRelevantLights(UPrimitiveComponent* Primitive,
                               TArray<const ULightComponent*>* RelevantLights) const
{
    if (!Primitive || !RelevantLights)
    {
        return;
    }

    RelevantLights->Empty();

    // Get the primitive's scene info
    FPrimitiveSceneInfo* SceneInfo = Primitive->GetPrimitiveSceneInfo();
    if (!SceneInfo)
    {
        return;
    }

    // Iterate through light interactions
    TArray<FLightSceneInfo*> LightInfos;
    SceneInfo->GetRelevantLights(LightInfos);

    // Note: Would need to convert FLightSceneInfo back to ULightComponent
    // This requires storing the component reference in the scene info
}

bool FScene::HasAnyLights() const
{
    return Lights.Num() > 0 || SkyLight != nullptr;
}

// ============================================================================
// Fog Management
// ============================================================================

void FScene::AddExponentialHeightFog(class UExponentialHeightFogComponent* FogComponent)
{
    MR_LOG(LogScene, Verbose, "AddExponentialHeightFog called");
}

void FScene::RemoveExponentialHeightFog(class UExponentialHeightFogComponent* FogComponent)
{
    MR_LOG(LogScene, Verbose, "RemoveExponentialHeightFog called");
}

bool FScene::HasAnyExponentialHeightFog() const
{
    return false; // Would check ExponentialFogs array
}

// ============================================================================
// Frame Management
// ============================================================================

void FScene::StartFrame()
{
    // Called at the start of each frame
    // Could be used for per-frame cleanup or setup
}

// ============================================================================
// World Offset
// ============================================================================

void FScene::ApplyWorldOffset(const FVector& InOffset)
{
    MR_LOG(LogScene, Log, "Applying world offset: (%f, %f, %f)",
           InOffset.X, InOffset.Y, InOffset.Z);

    // Update all primitive transforms
    for (int32 i = 0; i < Primitives.Num(); ++i)
    {
        FMatrix& Transform = PrimitiveTransforms[i];
        FVector Origin = Transform.GetOrigin();
        Origin = Origin + InOffset;
        Transform.SetOrigin(Origin);

        // Update bounds
        PrimitiveBounds[i].BoxSphereBounds.Origin = 
            PrimitiveBounds[i].BoxSphereBounds.Origin + InOffset;
        PrimitiveOcclusionBounds[i].Origin = 
            PrimitiveOcclusionBounds[i].Origin + InOffset;
    }

    // Update light positions
    for (auto& LightCompact : Lights)
    {
        if (LightCompact.LightSceneInfo && LightCompact.LightSceneInfo->GetProxy())
        {
            // Would need to update the proxy's position
            LightCompact.Position = LightCompact.Position + InOffset;
        }
    }
}

// ============================================================================
// Scene Release
// ============================================================================

void FScene::Release()
{
    MR_LOG(LogScene, Log, "Releasing scene");
    
    // This would typically be called to clean up the scene
    // In a full implementation, this would involve:
    // 1. Removing all primitives
    // 2. Removing all lights
    // 3. Cleaning up any other scene resources
    
    // For now, the destructor handles cleanup
    delete this;
}

} // namespace MonsterEngine
