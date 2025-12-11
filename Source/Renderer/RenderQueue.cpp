// Copyright Monster Engine. All Rights Reserved.

/**
 * @file RenderQueue.cpp
 * @brief Render queue system implementation
 * 
 * Implements FRenderQueue and FRenderQueueManager for draw call management.
 */

#include "Renderer/RenderQueue.h"
#include "Renderer/Scene.h"
#include "Renderer/SceneView.h"
#include "Core/Logging/Logging.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDevice.h"

using namespace MonsterRender;

namespace MonsterEngine
{

// ============================================================================
// FRenderQueue Implementation
// ============================================================================

FRenderQueue::FRenderQueue()
    : Device(nullptr)
    , FrameNumber(0)
    , NumDrawCalls(0)
    , NumTriangles(0)
    , NumStateChanges(0)
    , LastPipelineState(nullptr)
{
    // Initialize pass sorted flags
    for (int32 i = 0; i < EMeshPass::Num; ++i)
    {
        PassSorted[i] = false;
    }
}

FRenderQueue::~FRenderQueue()
{
    Shutdown();
}

void FRenderQueue::Initialize(RHI::IRHIDevice* InDevice)
{
    Device = InDevice;
    
    // Pre-allocate some space for common passes
    PassItems[EMeshPass::DepthPass].Reserve(1024);
    PassItems[EMeshPass::BasePass].Reserve(2048);
    PassItems[EMeshPass::TranslucencyStandard].Reserve(256);
    
    MR_LOG(LogRenderer, Log, "FRenderQueue initialized");
}

void FRenderQueue::Shutdown()
{
    Clear();
    Device = nullptr;
}

void FRenderQueue::BeginFrame()
{
    FrameNumber++;
    Clear();
    ResetStatistics();
}

void FRenderQueue::EndFrame()
{
    // Any end-of-frame cleanup
}

void FRenderQueue::Clear()
{
    for (int32 i = 0; i < EMeshPass::Num; ++i)
    {
        PassItems[i].Empty();
        PassSorted[i] = false;
    }
    LastPipelineState = nullptr;
}

void FRenderQueue::AddMeshBatch(
    EMeshPass::Type PassType,
    const FMeshBatch& MeshBatch,
    ERenderQueuePriority Priority,
    float DistanceFromCamera,
    FPrimitiveSceneInfo* PrimitiveSceneInfo)
{
    if (!MeshBatch.IsValid())
    {
        return;
    }
    
    FRenderQueueItem Item;
    Item.DrawCommand = ConvertMeshBatchToDrawCommand(MeshBatch);
    Item.Priority = Priority;
    Item.DistanceFromCamera = DistanceFromCamera;
    Item.PrimitiveSceneInfo = PrimitiveSceneInfo;
    Item.CalculateSortKey();
    
    AddItem(PassType, Item);
}

void FRenderQueue::AddMeshDrawCommand(
    EMeshPass::Type PassType,
    const FMeshDrawCommand& DrawCommand,
    ERenderQueuePriority Priority,
    float DistanceFromCamera)
{
    if (!DrawCommand.IsValid())
    {
        return;
    }
    
    FRenderQueueItem Item;
    Item.DrawCommand = DrawCommand;
    Item.Priority = Priority;
    Item.DistanceFromCamera = DistanceFromCamera;
    Item.PrimitiveSceneInfo = DrawCommand.PrimitiveSceneInfo;
    Item.CalculateSortKey();
    
    AddItem(PassType, Item);
}

void FRenderQueue::AddItem(EMeshPass::Type PassType, const FRenderQueueItem& Item)
{
    if (PassType >= EMeshPass::Num)
    {
        MR_LOG(LogRenderer, Warning, "Invalid pass type: %d", static_cast<int>(PassType));
        return;
    }
    
    PassItems[PassType].Add(Item);
    PassSorted[PassType] = false;
}

void FRenderQueue::Sort()
{
    for (int32 i = 0; i < EMeshPass::Num; ++i)
    {
        SortPass(static_cast<EMeshPass::Type>(i));
    }
}

void FRenderQueue::SortPass(EMeshPass::Type PassType)
{
    if (PassType >= EMeshPass::Num || PassSorted[PassType])
    {
        return;
    }
    
    TArray<FRenderQueueItem>& Items = PassItems[PassType];
    
    if (Items.Num() <= 1)
    {
        PassSorted[PassType] = true;
        return;
    }
    
    // Sort by sort key
    Items.Sort([](const FRenderQueueItem& A, const FRenderQueueItem& B)
    {
        return A.SortKey < B.SortKey;
    });
    
    PassSorted[PassType] = true;
    
    MR_LOG(LogRenderer, Verbose, "Sorted %d items for pass %s",
                 static_cast<int32>(Items.Num()), EMeshPass::GetMeshPassName(PassType));
}

void FRenderQueue::MergeDrawCommands()
{
    // Merge compatible draw commands across all passes
    for (int32 PassIndex = 0; PassIndex < EMeshPass::Num; ++PassIndex)
    {
        TArray<FRenderQueueItem>& Items = PassItems[PassIndex];
        
        if (Items.Num() <= 1)
        {
            continue;
        }
        
        // Ensure sorted before merging
        if (!PassSorted[PassIndex])
        {
            SortPass(static_cast<EMeshPass::Type>(PassIndex));
        }
        
        int32 NumMerged = 0;
        
        for (int32 i = 0; i < Items.Num() - 1; ++i)
        {
            FRenderQueueItem& Current = Items[i];
            FRenderQueueItem& Next = Items[i + 1];
            
            if (Current.DrawCommand.IsValid() && Next.DrawCommand.IsValid() &&
                Current.DrawCommand.CanMergeWith(Next.DrawCommand))
            {
                // Merge by increasing instance count
                Current.DrawCommand.NumInstances += Next.DrawCommand.NumInstances;
                
                // Mark merged item as invalid
                Next.DrawCommand.bIsValid = false;
                NumMerged++;
            }
        }
        
        // Remove merged items
        if (NumMerged > 0)
        {
            // Remove invalid items by compacting the array
            int32 WriteIndex = 0;
            for (int32 ReadIndex = 0; ReadIndex < Items.Num(); ++ReadIndex)
            {
                if (Items[ReadIndex].DrawCommand.IsValid())
                {
                    if (WriteIndex != ReadIndex)
                    {
                        Items[WriteIndex] = Items[ReadIndex];
                    }
                    ++WriteIndex;
                }
            }
            Items.SetNum(WriteIndex);
            
            MR_LOG(LogRenderer, Verbose, "Pass %s: merged %d draw commands, %d remaining",
                         EMeshPass::GetMeshPassName(static_cast<EMeshPass::Type>(PassIndex)),
                         NumMerged, Items.Num());
        }
    }
}

void FRenderQueue::Optimize()
{
    Sort();
    MergeDrawCommands();
}

void FRenderQueue::Execute(RHI::IRHICommandList& RHICmdList)
{
    // Execute passes in order
    // Depth pass first
    ExecutePass(RHICmdList, EMeshPass::DepthPass);
    
    // Base pass
    ExecutePass(RHICmdList, EMeshPass::BasePass);
    
    // Shadow passes
    ExecutePass(RHICmdList, EMeshPass::CSMShadowDepth);
    
    // Sky pass
    ExecutePass(RHICmdList, EMeshPass::SkyPass);
    
    // Translucency passes (sorted back-to-front)
    ExecutePass(RHICmdList, EMeshPass::TranslucencyStandard);
    ExecutePass(RHICmdList, EMeshPass::TranslucencyAfterDOF);
    
    // Distortion
    ExecutePass(RHICmdList, EMeshPass::Distortion);
    
    // Velocity
    ExecutePass(RHICmdList, EMeshPass::Velocity);
}

void FRenderQueue::ExecutePass(RHI::IRHICommandList& RHICmdList, EMeshPass::Type PassType)
{
    if (PassType >= EMeshPass::Num)
    {
        return;
    }
    
    const TArray<FRenderQueueItem>& Items = PassItems[PassType];
    
    if (Items.Num() == 0)
    {
        return;
    }
    
    MR_LOG(LogRenderer, Verbose, "Executing pass %s with %d draw calls",
                 EMeshPass::GetMeshPassName(PassType), Items.Num());
    
    for (const FRenderQueueItem& Item : Items)
    {
        SubmitDrawCommand(RHICmdList, Item.DrawCommand);
    }
}

void FRenderQueue::ExecutePassPriority(RHI::IRHICommandList& RHICmdList,
                                       EMeshPass::Type PassType,
                                       ERenderQueuePriority Priority)
{
    if (PassType >= EMeshPass::Num)
    {
        return;
    }
    
    const TArray<FRenderQueueItem>& Items = PassItems[PassType];
    
    for (const FRenderQueueItem& Item : Items)
    {
        if (Item.Priority == Priority)
        {
            SubmitDrawCommand(RHICmdList, Item.DrawCommand);
        }
    }
}

int32 FRenderQueue::GetNumItems(EMeshPass::Type PassType) const
{
    if (PassType >= EMeshPass::Num)
    {
        return 0;
    }
    return PassItems[PassType].Num();
}

int32 FRenderQueue::GetTotalNumItems() const
{
    int32 Total = 0;
    for (int32 i = 0; i < EMeshPass::Num; ++i)
    {
        Total += PassItems[i].Num();
    }
    return Total;
}

bool FRenderQueue::HasItems(EMeshPass::Type PassType) const
{
    if (PassType >= EMeshPass::Num)
    {
        return false;
    }
    return PassItems[PassType].Num() > 0;
}

bool FRenderQueue::IsEmpty() const
{
    for (int32 i = 0; i < EMeshPass::Num; ++i)
    {
        if (PassItems[i].Num() > 0)
        {
            return false;
        }
    }
    return true;
}

const TArray<FRenderQueueItem>& FRenderQueue::GetItems(EMeshPass::Type PassType) const
{
    static TArray<FRenderQueueItem> EmptyArray;
    if (PassType >= EMeshPass::Num)
    {
        return EmptyArray;
    }
    return PassItems[PassType];
}

void FRenderQueue::ResetStatistics()
{
    NumDrawCalls = 0;
    NumTriangles = 0;
    NumStateChanges = 0;
}

FMeshDrawCommand FRenderQueue::ConvertMeshBatchToDrawCommand(const FMeshBatch& MeshBatch)
{
    FMeshDrawCommand DrawCommand;
    
    DrawCommand.VertexBuffer = reinterpret_cast<RHI::IRHIBuffer*>(MeshBatch.VertexBuffer);
    DrawCommand.IndexBuffer = reinterpret_cast<RHI::IRHIBuffer*>(MeshBatch.IndexBuffer);
    DrawCommand.CachedPipelineState = reinterpret_cast<RHI::IRHIPipelineState*>(MeshBatch.PipelineState);
    DrawCommand.NumVertices = MeshBatch.NumVertices;
    DrawCommand.NumPrimitives = MeshBatch.NumIndices > 0 ? MeshBatch.NumIndices / 3 : MeshBatch.NumVertices / 3;
    DrawCommand.FirstIndex = MeshBatch.FirstIndex;
    DrawCommand.BaseVertexIndex = MeshBatch.BaseVertexLocation;
    DrawCommand.NumInstances = MeshBatch.NumInstances;
    DrawCommand.bUse32BitIndices = MeshBatch.bUse32BitIndices;
    DrawCommand.bIsValid = MeshBatch.IsValid();
    
    DrawCommand.CalculateSortKey();
    
    return DrawCommand;
}

void FRenderQueue::SubmitDrawCommand(RHI::IRHICommandList& RHICmdList, const FMeshDrawCommand& DrawCommand)
{
    if (!DrawCommand.IsValid())
    {
        return;
    }
    
    // Track state changes
    if (DrawCommand.CachedPipelineState != LastPipelineState)
    {
        NumStateChanges++;
        LastPipelineState = DrawCommand.CachedPipelineState;
    }
    
    // Submit the draw
    DrawCommand.SubmitDraw(RHICmdList);
    
    // Update statistics
    NumDrawCalls++;
    NumTriangles += DrawCommand.NumPrimitives * DrawCommand.NumInstances;
}

// ============================================================================
// FRenderQueueManager Implementation
// ============================================================================

FRenderQueueManager::FRenderQueueManager()
    : Device(nullptr)
{
}

FRenderQueueManager::~FRenderQueueManager()
{
    Shutdown();
}

void FRenderQueueManager::Initialize(RHI::IRHIDevice* InDevice)
{
    Device = InDevice;
    
    // Create at least one render queue
    RenderQueues.SetNum(1);
    RenderQueues[0].Initialize(Device);
    
    MR_LOG(LogRenderer, Log, "FRenderQueueManager initialized");
}

void FRenderQueueManager::Shutdown()
{
    for (FRenderQueue& Queue : RenderQueues)
    {
        Queue.Shutdown();
    }
    RenderQueues.Empty();
    Device = nullptr;
}

FRenderQueue& FRenderQueueManager::GetRenderQueue(int32 ViewIndex)
{
    // Ensure we have enough queues
    while (ViewIndex >= RenderQueues.Num())
    {
        int32 NewIndex = RenderQueues.Add(FRenderQueue());
        RenderQueues[NewIndex].Initialize(Device);
    }
    
    return RenderQueues[ViewIndex];
}

void FRenderQueueManager::BeginFrame()
{
    for (FRenderQueue& Queue : RenderQueues)
    {
        Queue.BeginFrame();
    }
}

void FRenderQueueManager::EndFrame()
{
    for (FRenderQueue& Queue : RenderQueues)
    {
        Queue.EndFrame();
    }
}

void FRenderQueueManager::ClearAll()
{
    for (FRenderQueue& Queue : RenderQueues)
    {
        Queue.Clear();
    }
}

} // namespace MonsterEngine
