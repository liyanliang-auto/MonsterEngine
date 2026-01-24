// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file MeshBatch.h
 * @brief Mesh batch structures for rendering
 * 
 * Simplified version of UE5's FMeshBatch system for MonsterEngine.
 * Reference: Engine/Source/Runtime/Engine/Public/MeshBatch.h
 */

#include "Core/CoreMinimal.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "RHI/IRHIResource.h"

// Forward declarations
namespace MonsterRender { namespace RHI {
    class IRHIBuffer;
    class IRHIPipelineState;
}}

namespace MonsterEngine
{

// Forward declarations
class FPrimitiveSceneProxy;
class FMaterialRenderProxy;

/**
 * Primitive topology types
 */
enum EPrimitiveType : uint8
{
    PT_TriangleList,
    PT_TriangleStrip,
    PT_LineList,
    PT_PointList,
    PT_Num
};

/**
 * Depth priority group for rendering order
 */
enum ESceneDepthPriorityGroup : uint8
{
    SDPG_World,         // World geometry (default)
    SDPG_Foreground,    // Foreground elements (UI, etc.)
    SDPG_Num
};

/**
 * A single mesh batch element representing one draw call
 * Reference: UE5 FMeshBatchElement
 */
struct FMeshBatchElement
{
    /** Vertex buffer for this element */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> VertexBuffer;
    
    /** Index buffer for this element (optional, nullptr for non-indexed draws) */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> IndexBuffer;
    
    /** Pipeline state for this draw call */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> PipelineState;
    
    /** Number of primitives to draw */
    uint32 NumPrimitives = 0;
    
    /** First index in the index buffer */
    uint32 FirstIndex = 0;
    
    /** Base vertex index (added to all indices) */
    int32 BaseVertexIndex = 0;
    
    /** Minimum vertex index used */
    uint32 MinVertexIndex = 0;
    
    /** Maximum vertex index used */
    uint32 MaxVertexIndex = 0;
    
    /** Number of instances to draw */
    uint32 NumInstances = 1;
    
    /** User data pointer for custom data */
    void* UserData = nullptr;
    
    /** Default constructor */
    FMeshBatchElement()
        : NumPrimitives(0)
        , FirstIndex(0)
        , BaseVertexIndex(0)
        , MinVertexIndex(0)
        , MaxVertexIndex(0)
        , NumInstances(1)
        , UserData(nullptr)
    {
    }
    
    /** Get number of primitives based on topology type */
    FORCEINLINE uint32 GetNumPrimitives() const
    {
        return NumPrimitives;
    }
};

/**
 * A batch of mesh elements with the same material and rendering state
 * Reference: UE5 FMeshBatch
 */
struct FMeshBatch
{
    /** Array of mesh batch elements (usually just one) */
    TArray<FMeshBatchElement> Elements;
    
    /** Material render proxy (required) */
    const FMaterialRenderProxy* MaterialRenderProxy = nullptr;
    
    /** Primitive type (triangles, lines, etc.) */
    EPrimitiveType Type = PT_TriangleList;
    
    /** Depth priority group */
    ESceneDepthPriorityGroup DepthPriorityGroup = SDPG_World;
    
    /** LOD index of this mesh */
    int8 LODIndex = -1;
    
    /** Mesh ID within the primitive */
    uint16 MeshIdInPrimitive = 0;
    
    /** Rendering flags */
    uint32 bCastShadow : 1;              // Can cast shadows
    uint32 bUseForMaterial : 1;          // Can be used in material passes
    uint32 bUseForDepthPass : 1;         // Can be used in depth-only passes
    uint32 bReverseCulling : 1;          // Reverse culling direction
    uint32 bDisableBackfaceCulling : 1;  // Disable backface culling
    uint32 bWireframe : 1;               // Render as wireframe
    
    /** Default constructor */
    FMeshBatch()
        : MaterialRenderProxy(nullptr)
        , Type(PT_TriangleList)
        , DepthPriorityGroup(SDPG_World)
        , LODIndex(-1)
        , MeshIdInPrimitive(0)
        , bCastShadow(1)
        , bUseForMaterial(1)
        , bUseForDepthPass(1)
        , bReverseCulling(0)
        , bDisableBackfaceCulling(0)
        , bWireframe(0)
    {
        // Reserve space for at least one element
        Elements.Reserve(1);
    }
    
    /** Get total number of primitives in all elements */
    FORCEINLINE int32 GetNumPrimitives() const
    {
        int32 Count = 0;
        for (const FMeshBatchElement& Element : Elements)
        {
            Count += Element.GetNumPrimitives();
        }
        return Count;
    }
    
    /** Check if this batch has any draw calls */
    FORCEINLINE bool HasAnyDrawCalls() const
    {
        for (const FMeshBatchElement& Element : Elements)
        {
            if (Element.GetNumPrimitives() > 0)
            {
                return true;
            }
        }
        return false;
    }
};

/**
 * A mesh batch with cached relevance information
 * Reference: UE5 FMeshBatchAndRelevance
 */
struct FMeshBatchAndRelevance
{
    /** Pointer to the mesh batch */
    const FMeshBatch* Mesh = nullptr;
    
    /** The primitive that created this mesh */
    const FPrimitiveSceneProxy* PrimitiveSceneProxy = nullptr;
    
    /** Cached material flags for fast filtering */
    uint32 bHasOpaqueMaterial : 1;
    uint32 bHasMaskedMaterial : 1;
    uint32 bRenderInMainPass : 1;
    
    /** Constructor */
    FMeshBatchAndRelevance()
        : Mesh(nullptr)
        , PrimitiveSceneProxy(nullptr)
        , bHasOpaqueMaterial(1)
        , bHasMaskedMaterial(0)
        , bRenderInMainPass(1)
    {
    }
    
    /** Constructor with mesh and proxy */
    FMeshBatchAndRelevance(const FMeshBatch* InMesh, const FPrimitiveSceneProxy* InProxy)
        : Mesh(InMesh)
        , PrimitiveSceneProxy(InProxy)
        , bHasOpaqueMaterial(1)
        , bHasMaskedMaterial(0)
        , bRenderInMainPass(1)
    {
    }
    
    /** Accessors */
    bool GetHasOpaqueMaterial() const { return bHasOpaqueMaterial; }
    bool GetHasMaskedMaterial() const { return bHasMaskedMaterial; }
    bool GetRenderInMainPass() const { return bRenderInMainPass; }
};

} // namespace MonsterEngine
