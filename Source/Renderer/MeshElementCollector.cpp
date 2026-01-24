// Copyright Monster Engine. All Rights Reserved.

#include "Renderer/MeshElementCollector.h"
#include "Renderer/SceneView.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Core/Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogMeshCollector, Log, All);

namespace MonsterEngine
{

FMeshElementCollector::FMeshElementCollector()
    : PrimitiveSceneProxy(nullptr)
{
    // Reserve space for mesh batch storage to avoid frequent reallocations
    MeshBatchStorage.Reserve(256);
    
    // Reserve space for one-frame resources
    OneFrameResources.Reserve(64);
}

FMeshElementCollector::~FMeshElementCollector()
{
    // Clean up all one-frame resources
    for (FOneFrameResource* Resource : OneFrameResources)
    {
        delete Resource;
    }
    OneFrameResources.Empty();
}

FMeshBatch& FMeshElementCollector::AllocateMesh()
{
    // Add a new mesh batch to storage
    const int32 Index = MeshBatchStorage.Add(FMeshBatch());
    return MeshBatchStorage[Index];
}

void FMeshElementCollector::AddMesh(int32 ViewIndex, FMeshBatch& MeshBatch)
{
    if (ViewIndex < 0 || ViewIndex >= MeshBatchesPerView.Num())
    {
        MR_LOG(LogMeshCollector, Error, "Invalid view index %d (max: %d)", 
               ViewIndex, MeshBatchesPerView.Num());
        return;
    }
    
    // Validate mesh batch
    if (!MeshBatch.HasAnyDrawCalls())
    {
        MR_LOG(LogMeshCollector, Warning, "Attempting to add mesh batch with no draw calls");
        return;
    }
    
    // Create mesh batch and relevance entry
    FMeshBatchAndRelevance MeshAndRelevance(&MeshBatch, PrimitiveSceneProxy);
    
    // Set mesh ID in primitive
    if (ViewIndex < MeshIdInPrimitivePerView.Num())
    {
        MeshBatch.MeshIdInPrimitive = MeshIdInPrimitivePerView[ViewIndex]++;
    }
    
    // Add to the view's mesh batch array
    MeshBatchesPerView[ViewIndex].Add(MeshAndRelevance);
    
    MR_LOG(LogMeshCollector, VeryVerbose, 
           "Added mesh batch to view %d (total: %d)", 
           ViewIndex, MeshBatchesPerView[ViewIndex].Num());
}

uint32 FMeshElementCollector::GetMeshBatchCount(uint32 ViewIndex) const
{
    if (ViewIndex >= static_cast<uint32>(MeshBatchesPerView.Num()))
    {
        return 0;
    }
    
    return static_cast<uint32>(MeshBatchesPerView[ViewIndex].Num());
}

const TArray<FMeshBatchAndRelevance>* FMeshElementCollector::GetMeshBatches(int32 ViewIndex) const
{
    if (ViewIndex < 0 || ViewIndex >= MeshBatchesPerView.Num())
    {
        return nullptr;
    }
    
    return &MeshBatchesPerView[ViewIndex];
}

void FMeshElementCollector::SetupViews(const TArray<const FSceneView*>& InViews)
{
    Views = InViews;
    
    // Setup mesh batch arrays for each view
    const int32 NumViews = InViews.Num();
    MeshBatchesPerView.SetNum(NumViews);
    MeshIdInPrimitivePerView.SetNum(NumViews);
    
    // Reserve space for each view
    for (int32 ViewIndex = 0; ViewIndex < NumViews; ++ViewIndex)
    {
        MeshBatchesPerView[ViewIndex].Reserve(128);
        MeshIdInPrimitivePerView[ViewIndex] = 0;
    }
    
    MR_LOG(LogMeshCollector, Verbose, "Setup collector for %d views", NumViews);
}

void FMeshElementCollector::ClearViewMeshArrays()
{
    // Clear all mesh batches per view
    for (TArray<FMeshBatchAndRelevance>& ViewMeshes : MeshBatchesPerView)
    {
        ViewMeshes.Empty();
    }
    
    // Clear mesh batch storage
    MeshBatchStorage.Empty();
    
    // Reset mesh IDs
    for (uint16& MeshId : MeshIdInPrimitivePerView)
    {
        MeshId = 0;
    }
    
    PrimitiveSceneProxy = nullptr;
    
    MR_LOG(LogMeshCollector, VeryVerbose, "Cleared view mesh arrays");
}

void FMeshElementCollector::SetPrimitive(const FPrimitiveSceneProxy* InPrimitiveSceneProxy)
{
    PrimitiveSceneProxy = InPrimitiveSceneProxy;
    
    // Reset mesh ID counters for new primitive
    for (uint16& MeshId : MeshIdInPrimitivePerView)
    {
        MeshId = 0;
    }
}

} // namespace MonsterEngine
