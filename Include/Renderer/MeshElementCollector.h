// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file MeshElementCollector.h
 * @brief Mesh element collector for gathering dynamic mesh elements
 * 
 * Simplified version of UE5's FMeshElementCollector for MonsterEngine.
 * Reference: Engine/Source/Runtime/Engine/Public/SceneManagement.h
 */

#include "Core/CoreMinimal.h"
#include "Renderer/MeshBatch.h"
#include "Containers/Array.h"

namespace MonsterEngine
{

// Forward declarations
class FSceneView;
class FPrimitiveSceneProxy;

/**
 * Base class for temporary resources allocated from the collector
 * Reference: UE5 FOneFrameResource
 */
class FOneFrameResource
{
public:
    virtual ~FOneFrameResource() {}
};

/**
 * Collects mesh batches from primitives for rendering
 * Provides temporary resource allocation that lives for one frame
 * Reference: UE5 FMeshElementCollector
 */
class FMeshElementCollector
{
public:
    /**
     * Constructor
     * @param InFeatureLevel - Feature level for rendering
     */
    explicit FMeshElementCollector();
    
    /** Destructor */
    ~FMeshElementCollector();
    
    /**
     * Allocate a mesh batch that can be safely referenced by the collector
     * The mesh batch lifetime is guaranteed until the collector is destroyed
     * @return Reference to the allocated mesh batch
     */
    FMeshBatch& AllocateMesh();
    
    /**
     * Add a mesh batch to the collector for a specific view
     * @param ViewIndex - Index of the view
     * @param MeshBatch - Mesh batch to add
     */
    void AddMesh(int32 ViewIndex, FMeshBatch& MeshBatch);
    
    /**
     * Allocate a temporary one-frame resource
     * The resource will be automatically deleted when the collector is destroyed
     * @return Reference to the allocated resource
     */
    template<typename T, typename... ArgsType>
    T& AllocateOneFrameResource(ArgsType&&... Args)
    {
        T* Resource = new T(Forward<ArgsType>(Args)...);
        OneFrameResources.Add(Resource);
        return *Resource;
    }
    
    /**
     * Get the number of mesh batches collected for a view
     * @param ViewIndex - Index of the view
     * @return Number of mesh batches
     */
    uint32 GetMeshBatchCount(uint32 ViewIndex) const;
    
    /**
     * Get all mesh batches for a specific view
     * @param ViewIndex - Index of the view
     * @return Array of mesh batches with relevance
     */
    const TArray<FMeshBatchAndRelevance>* GetMeshBatches(int32 ViewIndex) const;
    
    /**
     * Setup the collector for a set of views
     * @param InViews - Array of views to collect for
     */
    void SetupViews(const TArray<const FSceneView*>& InViews);
    
    /**
     * Clear all collected data
     */
    void ClearViewMeshArrays();
    
    /**
     * Set the current primitive being processed
     * @param InPrimitiveSceneProxy - The primitive proxy
     */
    void SetPrimitive(const FPrimitiveSceneProxy* InPrimitiveSceneProxy);
    
    /**
     * Get the current primitive being processed
     * @return Current primitive proxy
     */
    const FPrimitiveSceneProxy* GetPrimitiveSceneProxy() const { return PrimitiveSceneProxy; }

private:
    /**
     * Storage for mesh batches (never reallocates)
     * Using TArray with reserved space to avoid reallocation
     */
    TArray<FMeshBatch> MeshBatchStorage;
    
    /**
     * Mesh batches organized by view
     * Each view has its own array of mesh batches with relevance
     */
    TArray<TArray<FMeshBatchAndRelevance>> MeshBatchesPerView;
    
    /**
     * Views being collected for
     */
    TArray<const FSceneView*> Views;
    
    /**
     * Current primitive being processed
     */
    const FPrimitiveSceneProxy* PrimitiveSceneProxy;
    
    /**
     * One-frame resources that will be deleted when collector is destroyed
     */
    TArray<FOneFrameResource*> OneFrameResources;
    
    /**
     * Current mesh ID in primitive per view
     */
    TArray<uint16> MeshIdInPrimitivePerView;
    
    // Non-copyable
    FMeshElementCollector(const FMeshElementCollector&) = delete;
    FMeshElementCollector& operator=(const FMeshElementCollector&) = delete;
};

} // namespace MonsterEngine
