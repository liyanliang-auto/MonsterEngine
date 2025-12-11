// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file MeshDrawCommand.h
 * @brief Mesh draw command system
 * 
 * This file defines the mesh draw command system for efficient draw call management.
 * Includes FMeshDrawCommand, FMeshDrawCommandPassSetupTaskContext, and FParallelMeshDrawCommandPass.
 * Reference: UE5 MeshDrawCommands.h
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/StaticArray.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Renderer/SceneTypes.h"
#include "RHI/RHIDefinitions.h"

namespace MonsterEngine
{

// Forward declarations for RHI
namespace RHI
{
    class IRHIDevice;
    class IRHICommandList;
    class IRHIBuffer;
    class IRHIPipelineState;
    class IRHIVertexShader;
    class IRHIPixelShader;
}

// Renderer namespace for low-level rendering classes
namespace Renderer
{

// Forward declarations
class FScene;
class FViewInfo;
class FPrimitiveSceneInfo;
class FMeshPassProcessor;

// ============================================================================
// FMeshDrawShaderBindings - Shader Parameter Bindings
// ============================================================================

/**
 * @struct FMeshDrawShaderBindings
 * @brief Stores shader parameter bindings for a mesh draw command
 * 
 * Contains all uniform buffer and resource bindings needed for rendering.
 * Reference: UE5 FMeshDrawShaderBindings
 */
struct FMeshDrawShaderBindings
{
    /** Uniform buffer bindings */
    TArray<RHI::IRHIBuffer*> UniformBuffers;
    
    /** Shader resource view bindings */
    TArray<void*> ShaderResourceViews;
    
    /** Sampler bindings */
    TArray<void*> Samplers;
    
    /** Default constructor */
    FMeshDrawShaderBindings() = default;
    
    /**
     * Set a uniform buffer binding
     * @param Slot Binding slot
     * @param Buffer The uniform buffer
     */
    void SetUniformBuffer(int32 Slot, RHI::IRHIBuffer* Buffer)
    {
        if (Slot >= UniformBuffers.Num())
        {
            UniformBuffers.SetNum(Slot + 1);
        }
        UniformBuffers[Slot] = Buffer;
    }
    
    /**
     * Clear all bindings
     */
    void Clear()
    {
        UniformBuffers.Empty();
        ShaderResourceViews.Empty();
        Samplers.Empty();
    }
    
    /**
     * Check if bindings are valid
     */
    bool IsValid() const
    {
        return UniformBuffers.Num() > 0 || ShaderResourceViews.Num() > 0;
    }
};

// ============================================================================
// FMeshDrawCommand - Pre-built Draw Command
// ============================================================================

/**
 * @struct FMeshDrawCommand
 * @brief A pre-built, cached draw command
 * 
 * Contains all state needed to execute a draw call, allowing for
 * efficient sorting, merging, and instancing of draw calls.
 * Reference: UE5 FMeshDrawCommand
 */
struct FMeshDrawCommand
{
    // ========================================================================
    // Pipeline State
    // ========================================================================
    
    /** Cached pipeline state object */
    RHI::IRHIPipelineState* CachedPipelineState;
    
    // ========================================================================
    // Vertex/Index Buffers
    // ========================================================================
    
    /** Vertex buffer */
    RHI::IRHIBuffer* VertexBuffer;
    
    /** Index buffer (nullptr for non-indexed draws) */
    RHI::IRHIBuffer* IndexBuffer;
    
    /** Vertex buffer offset in bytes */
    uint32 VertexBufferOffset;
    
    /** Index buffer offset in bytes */
    uint32 IndexBufferOffset;
    
    // ========================================================================
    // Draw Parameters
    // ========================================================================
    
    /** First index for indexed draws */
    uint32 FirstIndex;
    
    /** Number of primitives to draw */
    uint32 NumPrimitives;
    
    /** Number of instances */
    uint32 NumInstances;
    
    /** Base vertex index */
    int32 BaseVertexIndex;
    
    /** Number of vertices (for non-indexed draws) */
    uint32 NumVertices;
    
    // ========================================================================
    // Shader Bindings
    // ========================================================================
    
    /** Vertex shader bindings */
    FMeshDrawShaderBindings VertexShaderBindings;
    
    /** Pixel shader bindings */
    FMeshDrawShaderBindings PixelShaderBindings;
    
    // ========================================================================
    // Sorting and Identification
    // ========================================================================
    
    /** Sort key for draw call ordering */
    uint64 SortKey;
    
    /** Primitive scene info for this draw command */
    FPrimitiveSceneInfo* PrimitiveSceneInfo;
    
    /** Mesh ID for instancing */
    uint32 MeshId;
    
    /** LOD index */
    int8 LODIndex;
    
    /** Whether this uses 32-bit indices */
    uint8 bUse32BitIndices : 1;
    
    /** Whether this is a wireframe draw */
    uint8 bWireframe : 1;
    
    /** Whether this draw command is valid */
    uint8 bIsValid : 1;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    /** Default constructor */
    FMeshDrawCommand()
        : CachedPipelineState(nullptr)
        , VertexBuffer(nullptr)
        , IndexBuffer(nullptr)
        , VertexBufferOffset(0)
        , IndexBufferOffset(0)
        , FirstIndex(0)
        , NumPrimitives(0)
        , NumInstances(1)
        , BaseVertexIndex(0)
        , NumVertices(0)
        , SortKey(0)
        , PrimitiveSceneInfo(nullptr)
        , MeshId(0)
        , LODIndex(0)
        , bUse32BitIndices(true)
        , bWireframe(false)
        , bIsValid(false)
    {
    }
    
    // ========================================================================
    // Methods
    // ========================================================================
    
    /**
     * Check if this is an indexed draw
     */
    bool IsIndexed() const { return IndexBuffer != nullptr; }
    
    /**
     * Check if this draw command is valid
     */
    bool IsValid() const { return bIsValid && CachedPipelineState != nullptr; }
    
    /**
     * Submit this draw command to a command list
     * @param RHICmdList The command list
     */
    void SubmitDraw(RHI::IRHICommandList& RHICmdList) const;
    
    /**
     * Calculate the sort key for this draw command
     * Based on pipeline state, material, and depth for optimal batching
     */
    void CalculateSortKey();
    
    /**
     * Check if this draw command can be merged with another
     * @param Other The other draw command
     * @return True if the commands can be merged
     */
    bool CanMergeWith(const FMeshDrawCommand& Other) const
    {
        return CachedPipelineState == Other.CachedPipelineState &&
               VertexBuffer == Other.VertexBuffer &&
               IndexBuffer == Other.IndexBuffer &&
               bUse32BitIndices == Other.bUse32BitIndices;
    }
    
    /**
     * Compare for sorting
     */
    bool operator<(const FMeshDrawCommand& Other) const
    {
        return SortKey < Other.SortKey;
    }
};

// ============================================================================
// FVisibleMeshDrawCommand - Visible Mesh Draw Command
// ============================================================================

/**
 * @struct FVisibleMeshDrawCommand
 * @brief A mesh draw command that has been determined to be visible
 * 
 * Wraps a mesh draw command with additional visibility and instancing data.
 * Reference: UE5 FVisibleMeshDrawCommand
 */
struct FVisibleMeshDrawCommand
{
    /** Pointer to the mesh draw command */
    const FMeshDrawCommand* MeshDrawCommand;
    
    /** Draw primitive ID (for GPU scene) */
    uint32 DrawPrimitiveId;
    
    /** Instance factor for instanced draws */
    uint32 InstanceFactor;
    
    /** Sort key for ordering */
    uint64 SortKey;
    
    /** State bucket ID for grouping */
    int32 StateBucketId;
    
    /** Default constructor */
    FVisibleMeshDrawCommand()
        : MeshDrawCommand(nullptr)
        , DrawPrimitiveId(0)
        , InstanceFactor(1)
        , SortKey(0)
        , StateBucketId(INDEX_NONE)
    {
    }
    
    /**
     * Constructor with mesh draw command
     */
    explicit FVisibleMeshDrawCommand(const FMeshDrawCommand* InCommand)
        : MeshDrawCommand(InCommand)
        , DrawPrimitiveId(0)
        , InstanceFactor(1)
        , SortKey(InCommand ? InCommand->SortKey : 0)
        , StateBucketId(INDEX_NONE)
    {
    }
    
    /**
     * Compare for sorting
     */
    bool operator<(const FVisibleMeshDrawCommand& Other) const
    {
        return SortKey < Other.SortKey;
    }
};

// ============================================================================
// FMeshDrawCommandPassSetupTaskContext - Pass Setup Task Context
// ============================================================================

/**
 * @struct FMeshDrawCommandPassSetupTaskContext
 * @brief Context for parallel mesh draw command pass setup
 * 
 * Contains all data needed to set up mesh draw commands for a pass.
 * Reference: UE5 FMeshDrawCommandPassSetupTaskContext
 */
struct FMeshDrawCommandPassSetupTaskContext
{
    /** Scene being rendered */
    FScene* Scene;
    
    /** View being rendered */
    const FViewInfo* View;
    
    /** Pass type */
    EMeshPass::Type PassType;
    
    /** Mesh pass processor */
    FMeshPassProcessor* MeshPassProcessor;
    
    /** Dynamic mesh elements to process */
    const TArray<FMeshBatchAndRelevance>* DynamicMeshElements;
    
    /** Output: visible mesh draw commands */
    TArray<FVisibleMeshDrawCommand> VisibleMeshDrawCommands;
    
    /** Output: number of dynamic mesh draw commands generated */
    int32 NumDynamicMeshCommandsGenerated;
    
    /** Maximum number of draws */
    int32 MaxNumDraws;
    
    /** Whether to use GPU scene */
    bool bUseGPUScene;
    
    /** Whether dynamic instancing is enabled */
    bool bDynamicInstancing;
    
    /** Default constructor */
    FMeshDrawCommandPassSetupTaskContext()
        : Scene(nullptr)
        , View(nullptr)
        , PassType(EMeshPass::BasePass)
        , MeshPassProcessor(nullptr)
        , DynamicMeshElements(nullptr)
        , NumDynamicMeshCommandsGenerated(0)
        , MaxNumDraws(0)
        , bUseGPUScene(false)
        , bDynamicInstancing(true)
    {
    }
    
    /**
     * Reset the context for reuse
     */
    void Reset()
    {
        VisibleMeshDrawCommands.Empty();
        NumDynamicMeshCommandsGenerated = 0;
    }
};

// ============================================================================
// FParallelMeshDrawCommandPass - Parallel Mesh Draw Command Pass
// ============================================================================

/**
 * @class FParallelMeshDrawCommandPass
 * @brief Manages parallel processing of mesh draw commands for a render pass
 * 
 * Handles the setup, sorting, merging, and dispatching of mesh draw commands
 * for a specific render pass. Supports parallel command generation.
 * Reference: UE5 FParallelMeshDrawCommandPass
 */
class FParallelMeshDrawCommandPass
{
public:
    /** Constructor */
    FParallelMeshDrawCommandPass();
    
    /** Destructor */
    ~FParallelMeshDrawCommandPass();
    
    // ========================================================================
    // Pass Setup
    // ========================================================================
    
    /**
     * Dispatch the pass setup task
     * @param Scene The scene
     * @param View The view
     * @param PassType The mesh pass type
     * @param MeshPassProcessor The mesh pass processor
     * @param DynamicMeshElements Dynamic mesh elements to process
     * @param bUseGPUScene Whether to use GPU scene
     * @param MaxNumDraws Maximum number of draws
     */
    void DispatchPassSetup(
        FScene* Scene,
        const FViewInfo& View,
        EMeshPass::Type PassType,
        FMeshPassProcessor* MeshPassProcessor,
        const TArray<FMeshBatchAndRelevance>& DynamicMeshElements,
        bool bUseGPUScene,
        int32 MaxNumDraws);
    
    /**
     * Wait for the setup task to complete
     */
    void WaitForSetupTask();
    
    /**
     * Check if the setup task is complete
     */
    bool IsSetupTaskComplete() const { return bSetupTaskComplete; }
    
    // ========================================================================
    // Command Building
    // ========================================================================
    
    /**
     * Build rendering commands from visible mesh draw commands
     * @param RHICmdList The command list
     */
    void BuildRenderingCommands(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Sort visible mesh draw commands
     */
    void SortVisibleMeshDrawCommands();
    
    /**
     * Merge compatible mesh draw commands for instancing
     */
    void MergeMeshDrawCommands();
    
    // ========================================================================
    // Draw Dispatch
    // ========================================================================
    
    /**
     * Dispatch draw commands to the command list
     * @param RHICmdList The command list
     */
    void DispatchDraw(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Get the number of draws
     */
    int32 GetNumDraws() const { return TaskContext.VisibleMeshDrawCommands.Num(); }
    
    /**
     * Check if there are any draws
     */
    bool HasAnyDraws() const { return GetNumDraws() > 0; }
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    /**
     * Get the task context
     */
    FMeshDrawCommandPassSetupTaskContext& GetTaskContext() { return TaskContext; }
    const FMeshDrawCommandPassSetupTaskContext& GetTaskContext() const { return TaskContext; }
    
    /**
     * Get visible mesh draw commands
     */
    TArray<FVisibleMeshDrawCommand>& GetVisibleMeshDrawCommands() 
    { 
        return TaskContext.VisibleMeshDrawCommands; 
    }
    
private:
    /**
     * Execute the pass setup task
     */
    void ExecutePassSetupTask();
    
    /**
     * Generate mesh draw commands from dynamic mesh elements
     */
    void GenerateDynamicMeshDrawCommands();
    
private:
    /** Task context */
    FMeshDrawCommandPassSetupTaskContext TaskContext;
    
    /** Whether the setup task is complete */
    bool bSetupTaskComplete;
    
    /** Maximum number of draws for this pass */
    int32 MaxNumDraws;
};

// ============================================================================
// FMeshPassProcessor - Base Mesh Pass Processor
// ============================================================================

/**
 * @class FMeshPassProcessor
 * @brief Base class for mesh pass processors
 * 
 * Processes mesh batches and generates mesh draw commands for a specific pass.
 * Derived classes implement pass-specific logic (depth, base, shadow, etc.).
 * Reference: UE5 FMeshPassProcessor
 */
class FMeshPassProcessor
{
public:
    /** Constructor */
    FMeshPassProcessor(FScene* InScene, const FViewInfo* InView, EMeshPass::Type InPassType)
        : Scene(InScene)
        , View(InView)
        , PassType(InPassType)
    {
    }
    
    /** Virtual destructor */
    virtual ~FMeshPassProcessor() = default;
    
    /**
     * Add a mesh batch to be processed
     * @param MeshBatch The mesh batch
     * @param BatchElementMask Mask of elements to process
     * @param PrimitiveSceneInfo The primitive scene info
     * @param OutMeshDrawCommands Output array for generated commands
     */
    virtual void AddMeshBatch(
        const FMeshBatch& MeshBatch,
        uint64 BatchElementMask,
        FPrimitiveSceneInfo* PrimitiveSceneInfo,
        TArray<FMeshDrawCommand>& OutMeshDrawCommands) = 0;
    
    /**
     * Get the pass type
     */
    EMeshPass::Type GetPassType() const { return PassType; }
    
    /**
     * Get the scene
     */
    FScene* GetScene() const { return Scene; }
    
    /**
     * Get the view
     */
    const FViewInfo* GetView() const { return View; }
    
protected:
    /** Scene being rendered */
    FScene* Scene;
    
    /** View being rendered */
    const FViewInfo* View;
    
    /** Pass type */
    EMeshPass::Type PassType;
};

// ============================================================================
// FDepthPassMeshProcessor - Depth Pass Processor
// ============================================================================

/**
 * @class FDepthPassMeshProcessor
 * @brief Mesh pass processor for depth-only rendering
 * 
 * Generates mesh draw commands for the depth prepass.
 * Reference: UE5 FDepthPassMeshProcessor
 */
class FDepthPassMeshProcessor : public FMeshPassProcessor
{
public:
    /** Constructor */
    FDepthPassMeshProcessor(FScene* InScene, const FViewInfo* InView)
        : FMeshPassProcessor(InScene, InView, EMeshPass::DepthPass)
    {
    }
    
    virtual void AddMeshBatch(
        const FMeshBatch& MeshBatch,
        uint64 BatchElementMask,
        FPrimitiveSceneInfo* PrimitiveSceneInfo,
        TArray<FMeshDrawCommand>& OutMeshDrawCommands) override;
};

// ============================================================================
// FBasePassMeshProcessor - Base Pass Processor
// ============================================================================

/**
 * @class FBasePassMeshProcessor
 * @brief Mesh pass processor for base pass (GBuffer fill)
 * 
 * Generates mesh draw commands for the base pass.
 * Reference: UE5 FBasePassMeshProcessor
 */
class FBasePassMeshProcessor : public FMeshPassProcessor
{
public:
    /** Constructor */
    FBasePassMeshProcessor(FScene* InScene, const FViewInfo* InView)
        : FMeshPassProcessor(InScene, InView, EMeshPass::BasePass)
    {
    }
    
    virtual void AddMeshBatch(
        const FMeshBatch& MeshBatch,
        uint64 BatchElementMask,
        FPrimitiveSceneInfo* PrimitiveSceneInfo,
        TArray<FMeshDrawCommand>& OutMeshDrawCommands) override;
};

// ============================================================================
// FShadowDepthPassMeshProcessor - Shadow Depth Pass Processor
// ============================================================================

/**
 * @class FShadowDepthPassMeshProcessor
 * @brief Mesh pass processor for shadow depth rendering
 * 
 * Generates mesh draw commands for shadow map rendering.
 * Reference: UE5 FShadowDepthPassMeshProcessor
 */
class FShadowDepthPassMeshProcessor : public FMeshPassProcessor
{
public:
    /** Constructor */
    FShadowDepthPassMeshProcessor(FScene* InScene, const FViewInfo* InView)
        : FMeshPassProcessor(InScene, InView, EMeshPass::CSMShadowDepth)
    {
    }
    
    virtual void AddMeshBatch(
        const FMeshBatch& MeshBatch,
        uint64 BatchElementMask,
        FPrimitiveSceneInfo* PrimitiveSceneInfo,
        TArray<FMeshDrawCommand>& OutMeshDrawCommands) override;
};

} // namespace Renderer
} // namespace MonsterEngine
