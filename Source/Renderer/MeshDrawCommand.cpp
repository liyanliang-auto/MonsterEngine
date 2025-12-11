// Copyright Monster Engine. All Rights Reserved.

/**
 * @file MeshDrawCommand.cpp
 * @brief Mesh draw command system implementation
 * 
 * Implements FMeshDrawCommand, FParallelMeshDrawCommandPass, and mesh pass processors.
 * Reference: UE5 MeshDrawCommands.cpp
 */

#include "Renderer/MeshDrawCommand.h"
#include "Renderer/Scene.h"
#include "Renderer/SceneView.h"
#include "Core/Logging/Logging.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDevice.h"

using namespace MonsterRender;

namespace MonsterEngine
{

// ============================================================================
// FMeshDrawCommand Implementation
// ============================================================================

void FMeshDrawCommand::SubmitDraw(RHI::IRHICommandList& RHICmdList) const
{
    if (!IsValid())
    {
        return;
    }
    
    // Set pipeline state
    if (CachedPipelineState)
    {
        // RHICmdList.SetPipelineState(CachedPipelineState);
    }
    
    // Bind vertex buffer
    if (VertexBuffer)
    {
        // RHICmdList.SetVertexBuffer(0, VertexBuffer, VertexBufferOffset);
    }
    
    // Bind shader resources
    // Apply vertex shader bindings
    for (int32 i = 0; i < VertexShaderBindings.UniformBuffers.Num(); ++i)
    {
        if (VertexShaderBindings.UniformBuffers[i])
        {
            // RHICmdList.SetUniformBuffer(EShaderStage::Vertex, i, VertexShaderBindings.UniformBuffers[i]);
        }
    }
    
    // Apply pixel shader bindings
    for (int32 i = 0; i < PixelShaderBindings.UniformBuffers.Num(); ++i)
    {
        if (PixelShaderBindings.UniformBuffers[i])
        {
            // RHICmdList.SetUniformBuffer(EShaderStage::Pixel, i, PixelShaderBindings.UniformBuffers[i]);
        }
    }
    
    // Issue draw call
    if (IsIndexed())
    {
        // Bind index buffer
        // RHICmdList.SetIndexBuffer(IndexBuffer, bUse32BitIndices, IndexBufferOffset);
        
        // Draw indexed
        // RHICmdList.DrawIndexedInstanced(NumPrimitives * 3, NumInstances, FirstIndex, BaseVertexIndex, 0);
    }
    else
    {
        // Draw non-indexed
        // RHICmdList.DrawInstanced(NumVertices, NumInstances, 0, 0);
    }
}

void FMeshDrawCommand::CalculateSortKey()
{
    // Sort key format (64 bits):
    // Bits 48-63: Pipeline state hash (16 bits)
    // Bits 32-47: Material ID (16 bits)
    // Bits 16-31: Mesh ID (16 bits)
    // Bits 0-15:  LOD and flags (16 bits)
    
    uint64 PipelineHash = reinterpret_cast<uint64>(CachedPipelineState) & 0xFFFF;
    uint64 MaterialBits = (MeshId >> 16) & 0xFFFF;
    uint64 MeshBits = MeshId & 0xFFFF;
    uint64 LODBits = static_cast<uint64>(LODIndex & 0xFF);
    uint64 FlagBits = (bWireframe ? 0x100 : 0);
    
    SortKey = (PipelineHash << 48) | (MaterialBits << 32) | (MeshBits << 16) | LODBits | FlagBits;
}

// ============================================================================
// FParallelMeshDrawCommandPass Implementation
// ============================================================================

FParallelMeshDrawCommandPass::FParallelMeshDrawCommandPass()
    : bSetupTaskComplete(false)
    , MaxNumDraws(0)
{
}

FParallelMeshDrawCommandPass::~FParallelMeshDrawCommandPass()
{
}

void FParallelMeshDrawCommandPass::DispatchPassSetup(
    FScene* Scene,
    const FViewInfo& View,
    EMeshPass::Type PassType,
    FMeshPassProcessor* MeshPassProcessor,
    const TArray<FMeshBatchAndRelevance>& DynamicMeshElements,
    bool bUseGPUScene,
    int32 InMaxNumDraws)
{
    // Setup task context
    TaskContext.Scene = Scene;
    TaskContext.View = &View;
    TaskContext.PassType = PassType;
    TaskContext.MeshPassProcessor = MeshPassProcessor;
    TaskContext.DynamicMeshElements = &DynamicMeshElements;
    TaskContext.bUseGPUScene = bUseGPUScene;
    TaskContext.MaxNumDraws = InMaxNumDraws;
    
    MaxNumDraws = InMaxNumDraws;
    bSetupTaskComplete = false;
    
    // Execute the setup task (could be parallelized)
    ExecutePassSetupTask();
}

void FParallelMeshDrawCommandPass::WaitForSetupTask()
{
    // In a parallel implementation, this would wait for the task to complete
    // For now, the task is executed synchronously
}

void FParallelMeshDrawCommandPass::ExecutePassSetupTask()
{
    TaskContext.Reset();
    
    // Generate mesh draw commands from dynamic mesh elements
    GenerateDynamicMeshDrawCommands();
    
    bSetupTaskComplete = true;
    
    MR_LOG(LogRenderer, Verbose, "Pass setup complete: %d draw commands",
           TaskContext.VisibleMeshDrawCommands.Num());
}

void FParallelMeshDrawCommandPass::GenerateDynamicMeshDrawCommands()
{
    if (!TaskContext.DynamicMeshElements || !TaskContext.MeshPassProcessor)
    {
        return;
    }
    
    TArray<FMeshDrawCommand> GeneratedCommands;
    
    for (const FMeshBatchAndRelevance& MeshBatchAndRelevance : *TaskContext.DynamicMeshElements)
    {
        const FMeshBatch& MeshBatch = MeshBatchAndRelevance.MeshBatch;
        
        if (!MeshBatch.IsValid())
        {
            continue;
        }
        
        // Check if this mesh batch is relevant for this pass
        bool bRelevant = false;
        switch (TaskContext.PassType)
        {
            case EMeshPass::DepthPass:
                bRelevant = MeshBatchAndRelevance.ViewRelevance.bDrawInDepthPass;
                break;
            case EMeshPass::BasePass:
                bRelevant = MeshBatchAndRelevance.ViewRelevance.bDrawInBasePass;
                break;
            case EMeshPass::TranslucencyStandard:
            case EMeshPass::TranslucencyAll:
                bRelevant = MeshBatchAndRelevance.ViewRelevance.HasTranslucency();
                break;
            default:
                bRelevant = true;
                break;
        }
        
        if (!bRelevant)
        {
            continue;
        }
        
        // Generate mesh draw command through the processor
        GeneratedCommands.Empty();
        TaskContext.MeshPassProcessor->AddMeshBatch(
            MeshBatch,
            ~0ull, // All elements
            MeshBatchAndRelevance.PrimitiveSceneInfo,
            GeneratedCommands);
        
        // Add generated commands to visible list
        for (const FMeshDrawCommand& Command : GeneratedCommands)
        {
            if (Command.IsValid())
            {
                int32 CmdIndex = TaskContext.VisibleMeshDrawCommands.Add(FVisibleMeshDrawCommand());
                FVisibleMeshDrawCommand& VisibleCommand = TaskContext.VisibleMeshDrawCommands[CmdIndex];
                VisibleCommand.MeshDrawCommand = &Command;
                VisibleCommand.SortKey = Command.SortKey;
                TaskContext.NumDynamicMeshCommandsGenerated++;
            }
        }
    }
}

void FParallelMeshDrawCommandPass::BuildRenderingCommands(RHI::IRHICommandList& RHICmdList)
{
    // Sort and merge commands
    SortVisibleMeshDrawCommands();
    MergeMeshDrawCommands();
}

void FParallelMeshDrawCommandPass::SortVisibleMeshDrawCommands()
{
    if (TaskContext.VisibleMeshDrawCommands.Num() <= 1)
    {
        return;
    }
    
    // Sort by sort key
    TaskContext.VisibleMeshDrawCommands.Sort([](const FVisibleMeshDrawCommand& A, const FVisibleMeshDrawCommand& B)
    {
        return A.SortKey < B.SortKey;
    });
    
    MR_LOG(LogRenderer, Verbose, "Sorted %d mesh draw commands",
           TaskContext.VisibleMeshDrawCommands.Num());
}

void FParallelMeshDrawCommandPass::MergeMeshDrawCommands()
{
    if (!TaskContext.bDynamicInstancing || TaskContext.VisibleMeshDrawCommands.Num() <= 1)
    {
        return;
    }
    
    // Merge compatible draw commands for instancing
    // Commands are already sorted, so compatible commands should be adjacent
    
    int32 NumMerged = 0;
    
    for (int32 i = 0; i < TaskContext.VisibleMeshDrawCommands.Num() - 1; ++i)
    {
        FVisibleMeshDrawCommand& Current = TaskContext.VisibleMeshDrawCommands[i];
        FVisibleMeshDrawCommand& Next = TaskContext.VisibleMeshDrawCommands[i + 1];
        
        if (Current.MeshDrawCommand && Next.MeshDrawCommand &&
            Current.MeshDrawCommand->CanMergeWith(*Next.MeshDrawCommand))
        {
            // Merge by increasing instance count
            Current.InstanceFactor += Next.InstanceFactor;
            
            // Mark the merged command as invalid
            Next.MeshDrawCommand = nullptr;
            NumMerged++;
        }
    }
    
    // Remove merged (null) commands by compacting
    if (NumMerged > 0)
    {
        int32 WriteIdx = 0;
        for (int32 ReadIdx = 0; ReadIdx < TaskContext.VisibleMeshDrawCommands.Num(); ++ReadIdx)
        {
            if (TaskContext.VisibleMeshDrawCommands[ReadIdx].MeshDrawCommand != nullptr)
            {
                if (WriteIdx != ReadIdx)
                {
                    TaskContext.VisibleMeshDrawCommands[WriteIdx] = TaskContext.VisibleMeshDrawCommands[ReadIdx];
                }
                ++WriteIdx;
            }
        }
        TaskContext.VisibleMeshDrawCommands.SetNum(WriteIdx);
        
        MR_LOG(LogRenderer, Verbose, "Merged %d draw commands, %d remaining",
                     NumMerged, TaskContext.VisibleMeshDrawCommands.Num());
    }
}

void FParallelMeshDrawCommandPass::DispatchDraw(RHI::IRHICommandList& RHICmdList)
{
    if (!HasAnyDraws())
    {
        return;
    }
    
    MR_LOG(LogRenderer, Verbose, "Dispatching %d draws for pass %s",
                 GetNumDraws(), EMeshPass::GetMeshPassName(TaskContext.PassType));
    
    // Submit all draw commands
    for (const FVisibleMeshDrawCommand& VisibleCommand : TaskContext.VisibleMeshDrawCommands)
    {
        if (VisibleCommand.MeshDrawCommand)
        {
            VisibleCommand.MeshDrawCommand->SubmitDraw(RHICmdList);
        }
    }
}

// ============================================================================
// FDepthPassMeshProcessor Implementation
// ============================================================================

void FDepthPassMeshProcessor::AddMeshBatch(
    const FMeshBatch& MeshBatch,
    uint64 BatchElementMask,
    FPrimitiveSceneInfo* PrimitiveSceneInfo,
    TArray<FMeshDrawCommand>& OutMeshDrawCommands)
{
    if (!MeshBatch.IsValid())
    {
        return;
    }
    
    // Create depth-only draw command
    int32 DepthIndex = OutMeshDrawCommands.Add(FMeshDrawCommand());
    FMeshDrawCommand& DrawCommand = OutMeshDrawCommands[DepthIndex];
    
    // Setup draw command from mesh batch
    DrawCommand.VertexBuffer = MeshBatch.VertexBuffer;
    DrawCommand.IndexBuffer = MeshBatch.IndexBuffer;
    DrawCommand.NumVertices = MeshBatch.NumVertices;
    DrawCommand.NumPrimitives = MeshBatch.NumIndices / 3;
    DrawCommand.FirstIndex = MeshBatch.FirstIndex;
    DrawCommand.BaseVertexIndex = MeshBatch.BaseVertexLocation;
    DrawCommand.NumInstances = MeshBatch.NumInstances;
    DrawCommand.bUse32BitIndices = MeshBatch.bUse32BitIndices;
    DrawCommand.PrimitiveSceneInfo = PrimitiveSceneInfo;
    
    // For depth pass, we use a depth-only pipeline state
    DrawCommand.CachedPipelineState = MeshBatch.PipelineState; // Would be depth-only PSO
    
    DrawCommand.bIsValid = true;
    DrawCommand.CalculateSortKey();
}

// ============================================================================
// FBasePassMeshProcessor Implementation
// ============================================================================

void FBasePassMeshProcessor::AddMeshBatch(
    const FMeshBatch& MeshBatch,
    uint64 BatchElementMask,
    FPrimitiveSceneInfo* PrimitiveSceneInfo,
    TArray<FMeshDrawCommand>& OutMeshDrawCommands)
{
    if (!MeshBatch.IsValid())
    {
        return;
    }
    
    // Create base pass draw command
    int32 NewIndex = OutMeshDrawCommands.Add(FMeshDrawCommand());
    FMeshDrawCommand& DrawCommand = OutMeshDrawCommands[NewIndex];
    
    // Setup draw command from mesh batch
    DrawCommand.VertexBuffer = MeshBatch.VertexBuffer;
    DrawCommand.IndexBuffer = MeshBatch.IndexBuffer;
    DrawCommand.NumVertices = MeshBatch.NumVertices;
    DrawCommand.NumPrimitives = MeshBatch.NumIndices / 3;
    DrawCommand.FirstIndex = MeshBatch.FirstIndex;
    DrawCommand.BaseVertexIndex = MeshBatch.BaseVertexLocation;
    DrawCommand.NumInstances = MeshBatch.NumInstances;
    DrawCommand.bUse32BitIndices = MeshBatch.bUse32BitIndices;
    DrawCommand.PrimitiveSceneInfo = PrimitiveSceneInfo;
    DrawCommand.CachedPipelineState = MeshBatch.PipelineState;
    
    DrawCommand.bIsValid = true;
    DrawCommand.CalculateSortKey();
}

// ============================================================================
// FShadowDepthPassMeshProcessor Implementation
// ============================================================================

void FShadowDepthPassMeshProcessor::AddMeshBatch(
    const FMeshBatch& MeshBatch,
    uint64 BatchElementMask,
    FPrimitiveSceneInfo* PrimitiveSceneInfo,
    TArray<FMeshDrawCommand>& OutMeshDrawCommands)
{
    if (!MeshBatch.IsValid() || !MeshBatch.bCastShadow)
    {
        return;
    }
    
    // Create shadow depth draw command
    int32 NewIndex = OutMeshDrawCommands.Add(FMeshDrawCommand());
    FMeshDrawCommand& DrawCommand = OutMeshDrawCommands[NewIndex];
    
    // Setup draw command from mesh batch
    DrawCommand.VertexBuffer = MeshBatch.VertexBuffer;
    DrawCommand.IndexBuffer = MeshBatch.IndexBuffer;
    DrawCommand.NumVertices = MeshBatch.NumVertices;
    DrawCommand.NumPrimitives = MeshBatch.NumIndices / 3;
    DrawCommand.FirstIndex = MeshBatch.FirstIndex;
    DrawCommand.BaseVertexIndex = MeshBatch.BaseVertexLocation;
    DrawCommand.NumInstances = MeshBatch.NumInstances;
    DrawCommand.bUse32BitIndices = MeshBatch.bUse32BitIndices;
    DrawCommand.PrimitiveSceneInfo = PrimitiveSceneInfo;
    
    // For shadow pass, we use a shadow depth pipeline state
    DrawCommand.CachedPipelineState = MeshBatch.PipelineState; // Would be shadow depth PSO
    
    DrawCommand.bIsValid = true;
    DrawCommand.CalculateSortKey();
}

} // namespace MonsterEngine
