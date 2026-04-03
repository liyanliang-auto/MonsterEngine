// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "RHI/RHI.h"

namespace MonsterEngine {
namespace Renderer {

// Forward declarations
class FRenderPassManager;

/**
 * Render pass types - defines different rendering stages
 * Reference: UE5 EMeshPass
 */
enum class ERenderPassType : uint8 {
    BasePass,           // Opaque geometry rendering
    PBRPass,            // PBR material rendering
    ShadowDepthPass,    // Shadow map generation
    TranslucencyPass,   // Transparent objects
    PostProcessPass,    // Post-processing effects
    MAX
};

/**
 * Render pass descriptor
 * Describes properties of a render pass
 */
struct FRenderPassDesc {
    ERenderPassType Type;
    FString Name;
    bool bParallelExecute;      // Can this pass be executed in parallel?
    uint32 Priority;            // Execution priority (lower = earlier)
    
    FRenderPassDesc()
        : Type(ERenderPassType::BasePass)
        , Name("Unknown")
        , bParallelExecute(false)
        , Priority(0)
    {}
    
    FRenderPassDesc(ERenderPassType InType, const FString& InName, bool bInParallel, uint32 InPriority)
        : Type(InType)
        , Name(InName)
        , bParallelExecute(bInParallel)
        , Priority(InPriority)
    {}
};

/**
 * Render pass execution context
 * Contains all data needed to execute a render pass
 */
struct FRenderPassContext {
    RHI::IRHICommandList* CmdList;
    class FScene* Scene;
    struct FSceneView* View;
    
    FRenderPassContext()
        : CmdList(nullptr)
        , Scene(nullptr)
        , View(nullptr)
    {}
};

/**
 * Base class for render pass execution
 * Reference: UE5 FMeshPassProcessor
 */
class FRenderPassExecutor {
public:
    virtual ~FRenderPassExecutor() = default;
    
    /**
     * Execute the render pass
     * @param Context Render pass context
     */
    virtual void Execute(const FRenderPassContext& Context) = 0;
    
    /**
     * Get the pass descriptor
     */
    virtual const FRenderPassDesc& GetDesc() const = 0;
};

/**
 * Render pass manager
 * Manages registration and execution of render passes
 * Reference: UE5 FSceneRenderer pass management
 */
class FRenderPassManager {
public:
    FRenderPassManager();
    ~FRenderPassManager();
    
    /**
     * Register a render pass
     * @param Desc Pass descriptor
     * @param Executor Pass executor
     */
    void RegisterPass(const FRenderPassDesc& Desc, TSharedPtr<FRenderPassExecutor> Executor);
    
    /**
     * Execute a specific pass
     * @param Type Pass type to execute
     * @param Context Render context
     */
    void ExecutePass(ERenderPassType Type, const FRenderPassContext& Context);
    
    /**
     * Execute multiple passes in parallel
     * @param Passes Array of pass types to execute
     * @param Context Render context
     */
    void ExecutePassesParallel(const TArray<ERenderPassType>& Passes, const FRenderPassContext& Context);
    
    /**
     * Get pass descriptor by type
     */
    const FRenderPassDesc* GetPassDesc(ERenderPassType Type) const;
    
    /**
     * Check if a pass is registered
     */
    bool IsPassRegistered(ERenderPassType Type) const;
    
private:
    struct FPassEntry {
        FRenderPassDesc Desc;
        TSharedPtr<FRenderPassExecutor> Executor;
    };
    
    TArray<FPassEntry> m_registeredPasses;
};

} // namespace Renderer
} // namespace MonsterEngine
