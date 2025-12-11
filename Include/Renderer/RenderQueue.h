// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file RenderQueue.h
 * @brief Render queue system for managing and executing draw calls
 * 
 * This file defines the render queue system that collects, sorts, and
 * executes draw calls efficiently.
 * Reference: UE5 rendering pipeline concepts
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/StaticArray.h"
#include "Renderer/SceneTypes.h"
#include "Renderer/MeshDrawCommand.h"

namespace MonsterEngine
{

// Forward declarations
class FScene;
class FViewInfo;

namespace RHI
{
    class IRHIDevice;
    class IRHICommandList;
}

// ============================================================================
// ERenderQueuePriority - Render Queue Priority
// ============================================================================

/**
 * @enum ERenderQueuePriority
 * @brief Priority levels for render queue items
 */
enum class ERenderQueuePriority : uint8
{
    /** Background elements (skybox, etc.) */
    Background = 0,
    
    /** Opaque geometry */
    Opaque = 100,
    
    /** Alpha-tested geometry */
    AlphaTest = 150,
    
    /** Transparent geometry (sorted back-to-front) */
    Transparent = 200,
    
    /** Overlay elements (UI, debug) */
    Overlay = 250
};

// ============================================================================
// FRenderQueueItem - Single Render Queue Item
// ============================================================================

/**
 * @struct FRenderQueueItem
 * @brief A single item in the render queue
 * 
 * Contains all information needed to execute a draw call.
 */
struct FRenderQueueItem
{
    /** The mesh draw command */
    FMeshDrawCommand DrawCommand;
    
    /** Sort key for ordering */
    uint64 SortKey;
    
    /** Priority level */
    ERenderQueuePriority Priority;
    
    /** Distance from camera (for sorting transparent objects) */
    float DistanceFromCamera;
    
    /** Primitive scene info */
    FPrimitiveSceneInfo* PrimitiveSceneInfo;
    
    /** Default constructor */
    FRenderQueueItem()
        : SortKey(0)
        , Priority(ERenderQueuePriority::Opaque)
        , DistanceFromCamera(0.0f)
        , PrimitiveSceneInfo(nullptr)
    {
    }
    
    /**
     * Calculate sort key based on priority and other factors
     */
    void CalculateSortKey()
    {
        // Sort key format:
        // Bits 56-63: Priority (8 bits)
        // Bits 32-55: Pipeline state hash (24 bits)
        // Bits 0-31:  Distance or material ID (32 bits)
        
        uint64 PriorityBits = static_cast<uint64>(Priority) << 56;
        uint64 PipelineBits = (reinterpret_cast<uint64>(DrawCommand.CachedPipelineState) & 0xFFFFFF) << 32;
        
        if (Priority == ERenderQueuePriority::Transparent)
        {
            // For transparent objects, sort by distance (back-to-front)
            // Invert distance so larger distances come first
            uint32 DistanceBits = static_cast<uint32>((1.0f - DistanceFromCamera / 100000.0f) * UINT32_MAX);
            SortKey = PriorityBits | PipelineBits | DistanceBits;
        }
        else
        {
            // For opaque objects, sort by pipeline state to minimize state changes
            SortKey = PriorityBits | PipelineBits | DrawCommand.MeshId;
        }
    }
    
    /**
     * Compare for sorting
     */
    bool operator<(const FRenderQueueItem& Other) const
    {
        return SortKey < Other.SortKey;
    }
};

// ============================================================================
// FRenderQueueBucket - Bucket of Render Queue Items
// ============================================================================

/**
 * @struct FRenderQueueBucket
 * @brief A bucket of render queue items with the same priority
 */
struct FRenderQueueBucket
{
    /** Items in this bucket */
    TArray<FRenderQueueItem> Items;
    
    /** Priority of this bucket */
    ERenderQueuePriority Priority;
    
    /** Whether items are sorted */
    bool bSorted;
    
    /** Constructor */
    explicit FRenderQueueBucket(ERenderQueuePriority InPriority = ERenderQueuePriority::Opaque)
        : Priority(InPriority)
        , bSorted(false)
    {
    }
    
    /**
     * Add an item to the bucket
     */
    void AddItem(const FRenderQueueItem& Item)
    {
        Items.Add(Item);
        bSorted = false;
    }
    
    /**
     * Sort items in the bucket
     */
    void Sort()
    {
        if (!bSorted && Items.Num() > 1)
        {
            Items.Sort([](const FRenderQueueItem& A, const FRenderQueueItem& B)
            {
                return A.SortKey < B.SortKey;
            });
            bSorted = true;
        }
    }
    
    /**
     * Clear all items
     */
    void Clear()
    {
        Items.Empty();
        bSorted = false;
    }
    
    /**
     * Get number of items
     */
    int32 Num() const { return Items.Num(); }
    
    /**
     * Check if bucket is empty
     */
    bool IsEmpty() const { return Items.Num() == 0; }
};

// ============================================================================
// FRenderQueue - Main Render Queue
// ============================================================================

/**
 * @class FRenderQueue
 * @brief Main render queue for collecting and executing draw calls
 * 
 * Manages draw calls organized by pass type and priority.
 * Provides sorting, batching, and efficient execution.
 */
class FRenderQueue
{
public:
    /** Constructor */
    FRenderQueue();
    
    /** Destructor */
    ~FRenderQueue();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initialize the render queue
     * @param Device The RHI device
     */
    void Initialize(RHI::IRHIDevice* Device);
    
    /**
     * Shutdown and cleanup
     */
    void Shutdown();
    
    // ========================================================================
    // Queue Management
    // ========================================================================
    
    /**
     * Begin a new frame
     */
    void BeginFrame();
    
    /**
     * End the current frame
     */
    void EndFrame();
    
    /**
     * Clear all queued items
     */
    void Clear();
    
    // ========================================================================
    // Adding Items
    // ========================================================================
    
    /**
     * Add a mesh batch to the queue
     * @param PassType The render pass type
     * @param MeshBatch The mesh batch to add
     * @param Priority The render priority
     * @param DistanceFromCamera Distance from camera (for sorting)
     * @param PrimitiveSceneInfo The primitive scene info
     */
    void AddMeshBatch(
        EMeshPass::Type PassType,
        const FMeshBatch& MeshBatch,
        ERenderQueuePriority Priority = ERenderQueuePriority::Opaque,
        float DistanceFromCamera = 0.0f,
        FPrimitiveSceneInfo* PrimitiveSceneInfo = nullptr);
    
    /**
     * Add a mesh draw command to the queue
     * @param PassType The render pass type
     * @param DrawCommand The draw command to add
     * @param Priority The render priority
     * @param DistanceFromCamera Distance from camera (for sorting)
     */
    void AddMeshDrawCommand(
        EMeshPass::Type PassType,
        const FMeshDrawCommand& DrawCommand,
        ERenderQueuePriority Priority = ERenderQueuePriority::Opaque,
        float DistanceFromCamera = 0.0f);
    
    /**
     * Add a render queue item directly
     * @param PassType The render pass type
     * @param Item The item to add
     */
    void AddItem(EMeshPass::Type PassType, const FRenderQueueItem& Item);
    
    // ========================================================================
    // Sorting and Optimization
    // ========================================================================
    
    /**
     * Sort all queued items
     */
    void Sort();
    
    /**
     * Sort items for a specific pass
     * @param PassType The pass type to sort
     */
    void SortPass(EMeshPass::Type PassType);
    
    /**
     * Merge compatible draw commands for instancing
     */
    void MergeDrawCommands();
    
    /**
     * Optimize the queue for rendering
     * Performs sorting and merging
     */
    void Optimize();
    
    // ========================================================================
    // Execution
    // ========================================================================
    
    /**
     * Execute all queued draw calls
     * @param RHICmdList The command list
     */
    void Execute(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Execute draw calls for a specific pass
     * @param RHICmdList The command list
     * @param PassType The pass type to execute
     */
    void ExecutePass(RHI::IRHICommandList& RHICmdList, EMeshPass::Type PassType);
    
    /**
     * Execute draw calls for a specific pass and priority
     * @param RHICmdList The command list
     * @param PassType The pass type
     * @param Priority The priority level
     */
    void ExecutePassPriority(RHI::IRHICommandList& RHICmdList, 
                            EMeshPass::Type PassType, 
                            ERenderQueuePriority Priority);
    
    // ========================================================================
    // Queries
    // ========================================================================
    
    /**
     * Get the number of items in a pass
     * @param PassType The pass type
     * @return Number of items
     */
    int32 GetNumItems(EMeshPass::Type PassType) const;
    
    /**
     * Get the total number of items across all passes
     * @return Total number of items
     */
    int32 GetTotalNumItems() const;
    
    /**
     * Check if a pass has any items
     * @param PassType The pass type
     * @return True if the pass has items
     */
    bool HasItems(EMeshPass::Type PassType) const;
    
    /**
     * Check if the queue is empty
     * @return True if the queue is empty
     */
    bool IsEmpty() const;
    
    /**
     * Get items for a specific pass
     * @param PassType The pass type
     * @return Array of items
     */
    const TArray<FRenderQueueItem>& GetItems(EMeshPass::Type PassType) const;
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    /**
     * Get the number of draw calls executed
     */
    int32 GetNumDrawCalls() const { return NumDrawCalls; }
    
    /**
     * Get the number of triangles rendered
     */
    int32 GetNumTriangles() const { return NumTriangles; }
    
    /**
     * Get the number of state changes
     */
    int32 GetNumStateChanges() const { return NumStateChanges; }
    
    /**
     * Reset statistics
     */
    void ResetStatistics();
    
private:
    /**
     * Convert mesh batch to draw command
     */
    FMeshDrawCommand ConvertMeshBatchToDrawCommand(const FMeshBatch& MeshBatch);
    
    /**
     * Submit a draw command to the command list
     */
    void SubmitDrawCommand(RHI::IRHICommandList& RHICmdList, const FMeshDrawCommand& DrawCommand);
    
private:
    /** RHI device */
    RHI::IRHIDevice* Device;
    
    /** Items per pass */
    TStaticArray<TArray<FRenderQueueItem>, EMeshPass::Num> PassItems;
    
    /** Whether each pass is sorted */
    TStaticArray<bool, EMeshPass::Num> PassSorted;
    
    /** Current frame number */
    uint32 FrameNumber;
    
    /** Statistics */
    int32 NumDrawCalls;
    int32 NumTriangles;
    int32 NumStateChanges;
    
    /** Last used pipeline state (for tracking state changes) */
    RHI::IRHIPipelineState* LastPipelineState;
};

// ============================================================================
// FRenderQueueManager - Global Render Queue Manager
// ============================================================================

/**
 * @class FRenderQueueManager
 * @brief Manages multiple render queues for different views/purposes
 */
class FRenderQueueManager
{
public:
    /** Constructor */
    FRenderQueueManager();
    
    /** Destructor */
    ~FRenderQueueManager();
    
    /**
     * Initialize the manager
     * @param Device The RHI device
     */
    void Initialize(RHI::IRHIDevice* Device);
    
    /**
     * Shutdown the manager
     */
    void Shutdown();
    
    /**
     * Get or create a render queue for a view
     * @param ViewIndex The view index
     * @return The render queue
     */
    FRenderQueue& GetRenderQueue(int32 ViewIndex = 0);
    
    /**
     * Begin a new frame
     */
    void BeginFrame();
    
    /**
     * End the current frame
     */
    void EndFrame();
    
    /**
     * Clear all queues
     */
    void ClearAll();
    
private:
    /** RHI device */
    RHI::IRHIDevice* Device;
    
    /** Render queues per view */
    TArray<FRenderQueue> RenderQueues;
};

} // namespace MonsterEngine
