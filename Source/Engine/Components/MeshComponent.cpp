// Copyright Monster Engine. All Rights Reserved.

/**
 * @file MeshComponent.cpp
 * @brief Implementation of UMeshComponent and UStaticMeshComponent
 */

#include "Engine/Components/MeshComponent.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

DEFINE_LOG_CATEGORY_STATIC(LogMeshComponent, Log, All);

// ============================================================================
// UMeshComponent Implementation
// ============================================================================

UMeshComponent::UMeshComponent()
    : UPrimitiveComponent()
    , OverriddenLightmapRes(0)
    , bUseVertexColor(false)
    , bOverrideLightmapRes(false)
{
}

UMeshComponent::UMeshComponent(AActor* InOwner)
    : UPrimitiveComponent(InOwner)
    , OverriddenLightmapRes(0)
    , bUseVertexColor(false)
    , bOverrideLightmapRes(false)
{
}

UMeshComponent::~UMeshComponent()
{
    Materials.Empty();
    OverrideMaterials.Empty();
}

// ============================================================================
// Materials
// ============================================================================

UMaterialInterface* UMeshComponent::GetMaterial(int32 MaterialIndex) const
{
    // First check override materials
    if (MaterialIndex >= 0 && MaterialIndex < OverrideMaterials.Num())
    {
        if (OverrideMaterials[MaterialIndex])
        {
            return OverrideMaterials[MaterialIndex];
        }
    }

    // Then check base materials
    if (MaterialIndex >= 0 && MaterialIndex < Materials.Num())
    {
        return Materials[MaterialIndex];
    }

    return nullptr;
}

void UMeshComponent::SetMaterial(int32 MaterialIndex, UMaterialInterface* Material)
{
    if (MaterialIndex < 0)
    {
        return;
    }

    // Expand array if needed
    if (MaterialIndex >= Materials.Num())
    {
        Materials.SetNum(MaterialIndex + 1);
    }

    if (Materials[MaterialIndex] != Material)
    {
        Materials[MaterialIndex] = Material;
        MarkRenderStateDirty();
    }
}

void UMeshComponent::SetMaterials(const TArray<UMaterialInterface*>& NewMaterials)
{
    Materials = NewMaterials;
    MarkRenderStateDirty();
}

// ============================================================================
// Material Overrides
// ============================================================================

UMaterialInterface* UMeshComponent::GetOverrideMaterial(int32 MaterialIndex) const
{
    if (MaterialIndex >= 0 && MaterialIndex < OverrideMaterials.Num())
    {
        return OverrideMaterials[MaterialIndex];
    }
    return nullptr;
}

void UMeshComponent::SetOverrideMaterial(int32 MaterialIndex, UMaterialInterface* Material)
{
    if (MaterialIndex < 0)
    {
        return;
    }

    // Expand array if needed
    if (MaterialIndex >= OverrideMaterials.Num())
    {
        OverrideMaterials.SetNum(MaterialIndex + 1);
    }

    if (OverrideMaterials[MaterialIndex] != Material)
    {
        OverrideMaterials[MaterialIndex] = Material;
        MarkRenderStateDirty();
    }
}

void UMeshComponent::ClearOverrideMaterials()
{
    if (OverrideMaterials.Num() > 0)
    {
        OverrideMaterials.Empty();
        MarkRenderStateDirty();
    }
}

// ============================================================================
// Rendering Settings
// ============================================================================

void UMeshComponent::SetUseVertexColor(bool bNewUseVertexColor)
{
    if (bUseVertexColor != bNewUseVertexColor)
    {
        bUseVertexColor = bNewUseVertexColor;
        MarkRenderStateDirty();
    }
}

void UMeshComponent::SetOverriddenLightmapRes(int32 NewRes)
{
    if (OverriddenLightmapRes != NewRes)
    {
        OverriddenLightmapRes = NewRes;
        bOverrideLightmapRes = (NewRes > 0);
        MarkRenderStateDirty();
    }
}

// ============================================================================
// UStaticMeshComponent Implementation
// ============================================================================

UStaticMeshComponent::UStaticMeshComponent()
    : UMeshComponent()
    , ForcedLodModel(-1)
    , MinLOD(0)
    , StreamingDistanceMultiplier(1.0f)
    , bUseDefaultCollision(true)
    , bGenerateOverlapEvents(false)
{
}

UStaticMeshComponent::UStaticMeshComponent(AActor* InOwner)
    : UMeshComponent(InOwner)
    , ForcedLodModel(-1)
    , MinLOD(0)
    , StreamingDistanceMultiplier(1.0f)
    , bUseDefaultCollision(true)
    , bGenerateOverlapEvents(false)
{
}

UStaticMeshComponent::~UStaticMeshComponent()
{
}

FPrimitiveSceneProxy* UStaticMeshComponent::CreateSceneProxy()
{
    // In a full implementation, this would create a FStaticMeshSceneProxy
    // For now, return nullptr as we don't have the static mesh asset system
    return nullptr;
}

void UStaticMeshComponent::SetForcedLodModel(int32 NewForcedLod)
{
    if (ForcedLodModel != NewForcedLod)
    {
        ForcedLodModel = NewForcedLod;
        MarkRenderStateDirty();
    }
}

void UStaticMeshComponent::SetMinLOD(int32 NewMinLOD)
{
    if (MinLOD != NewMinLOD)
    {
        MinLOD = NewMinLOD;
        MarkRenderStateDirty();
    }
}

void UStaticMeshComponent::SetStreamingDistanceMultiplier(float NewMultiplier)
{
    if (StreamingDistanceMultiplier != NewMultiplier)
    {
        StreamingDistanceMultiplier = NewMultiplier;
        // This affects texture streaming, not render state
    }
}

} // namespace MonsterEngine
