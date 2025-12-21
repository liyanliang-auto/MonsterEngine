#pragma once

#include "Core/CoreMinimal.h"
#include "RDG/RDGDefinitions.h"
#include "RDG/RDGResource.h"
#include "RDG/RDGPass.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"

/**
 * RDG Builder - Main interface for constructing render graphs
 * Reference: UE5 RenderGraphBuilder.h
 */

namespace MonsterRender {
namespace RDG {

// Import commonly used types into RDG namespace
using MonsterEngine::FString;
using MonsterEngine::TArray;
using MonsterEngine::TMap;
using MonsterEngine::TFunction;

/**
 * Resource transition record for barrier insertion
 */
struct FRDGTransition
{
    FRDGResourceRef resource = nullptr;
    ERHIAccess stateBefore = ERHIAccess::Unknown;
    ERHIAccess stateAfter = ERHIAccess::Unknown;
    
    // Resource type flag
    bool bIsTexture = true;
    
    // Subresource information (for textures)
    uint32 mipLevel = 0xFFFFFFFF;
    uint32 arraySlice = 0xFFFFFFFF;
    
    FRDGTransition() = default;
    
    FRDGTransition(FRDGResourceRef inResource, 
                   ERHIAccess inBefore, 
                   ERHIAccess inAfter,
                   bool inIsTexture = true)
        : resource(inResource)
        , stateBefore(inBefore)
        , stateAfter(inAfter)
        , bIsTexture(inIsTexture)
    {}
    
    bool isWholeResource() const
    {
        return mipLevel == 0xFFFFFFFF && arraySlice == 0xFFFFFFFF;
    }
};

/**
 * FRDGBuilder - Render Dependency Graph Builder
 * 
 * Main class for constructing and executing render graphs.
 * Usage pattern:
 * 1. Create resources with CreateTexture/CreateBuffer
 * 2. Add passes with AddPass, declaring resource dependencies
 * 3. Call Execute to compile and run the graph
 * 
 * Reference: UE5 FRDGBuilder
 */
class FRDGBuilder
{
public:
    /**
     * Constructor
     * @param rhiDevice RHI device for resource allocation
     * @param debugName Debug name for this graph
     */
    explicit FRDGBuilder(RHI::IRHIDevice* rhiDevice, const FString& debugName = "RDG");
    
    ~FRDGBuilder();
    
    // Non-copyable
    FRDGBuilder(const FRDGBuilder&) = delete;
    FRDGBuilder& operator=(const FRDGBuilder&) = delete;
    
    /**
     * Create a new texture resource
     * @param name Debug name for the texture
     * @param desc Texture descriptor
     * @return Handle to the created texture
     */
    FRDGTextureRef createTexture(const FString& name, const FRDGTextureDesc& desc);
    
    /**
     * Create a new buffer resource
     * @param name Debug name for the buffer
     * @param desc Buffer descriptor
     * @return Handle to the created buffer
     */
    FRDGBufferRef createBuffer(const FString& name, const FRDGBufferDesc& desc);
    
    /**
     * Register an external texture (not owned by RDG)
     * @param name Debug name
     * @param texture External RHI texture
     * @param initialState Initial access state
     * @return Handle to the registered texture
     */
    FRDGTextureRef registerExternalTexture(
        const FString& name,
        RHI::IRHITexture* texture,
        ERHIAccess initialState = ERHIAccess::Unknown);
    
    /**
     * Register an external buffer (not owned by RDG)
     * @param name Debug name
     * @param buffer External RHI buffer
     * @param initialState Initial access state
     * @return Handle to the registered buffer
     */
    FRDGBufferRef registerExternalBuffer(
        const FString& name,
        RHI::IRHIBuffer* buffer,
        ERHIAccess initialState = ERHIAccess::Unknown);
    
    /**
     * Add a render pass to the graph
     * 
     * @param name Pass name for debugging
     * @param flags Pass execution flags
     * @param setupFunc Lambda to declare resource dependencies
     * @param executeFunc Lambda to execute the pass
     * 
     * Example:
     * builder.AddPass("ShadowDepth", ERDGPassFlags::Raster,
     *     [&](FRDGPassBuilder& passBuilder) {
     *         passBuilder.WriteDepth(shadowMap);
     *     },
     *     [=](RHI::IRHICommandList& cmdList) {
     *         // Render shadow depth
     *     });
     */
    template<typename SetupLambdaType, typename ExecuteLambdaType>
    FRDGPassRef addPass(
        const FString& name,
        ERDGPassFlags flags,
        SetupLambdaType&& setupFunc,
        ExecuteLambdaType&& executeFunc)
    {
        // Create pass
        auto* pass = new TRDGLambdaPass<ExecuteLambdaType>(
            name, 
            flags, 
            std::move(executeFunc));
        
        // Assign handle
        pass->m_handle = FRDGPassHandle(static_cast<uint16>(m_passes.Num()));
        
        // Setup resource dependencies
        FRDGPassBuilder passBuilder;
        setupFunc(passBuilder);
        
        // Record dependencies
        pass->m_textureAccesses = passBuilder.getTextureAccesses();
        pass->m_bufferAccesses = passBuilder.getBufferAccesses();
        
        // Add to pass list
        m_passes.Add(pass);
        
        return pass;
    }
    
    /**
     * Compile and execute the render graph
     * 
     * This will:
     * 1. Build dependency graph
     * 2. Perform topological sort
     * 3. Analyze resource lifetimes
     * 4. Insert resource transitions
     * 5. Execute passes in order
     * 
     * @param rhiCmdList Command list for recording GPU commands
     */
    void execute(RHI::IRHICommandList& rhiCmdList);
    
    /**
     * Get RHI device
     */
    RHI::IRHIDevice* getRHIDevice() const { return m_rhiDevice; }
    
private:
    // Compilation phases (to be implemented in Phase 2-4)
    void _buildDependencyGraph();
    void _topologicalSort();
    void _analyzeResourceLifetimes();
    void _insertTransitions();
    void _allocateResources(RHI::IRHICommandList& rhiCmdList);
    void _executePass(RHI::IRHICommandList& rhiCmdList, FRDGPass* pass);
    void _executeTransitions(RHI::IRHICommandList& rhiCmdList, 
                            const TArray<FRDGTransition>& transitions);
    void _releaseResources();
    
    // Debug validation
    void _validateGraph();
    
private:
    RHI::IRHIDevice* m_rhiDevice;
    FString m_debugName;
    
    // Resources
    TArray<FRDGTexture*> m_textures;
    TArray<FRDGBuffer*> m_buffers;
    
    // Passes
    TArray<FRDGPass*> m_passes;
    TArray<FRDGPass*> m_sortedPasses;  // After topological sort
    
    // Transitions (computed during compilation)
    TMap<FRDGPassHandle, TArray<FRDGTransition>> m_passTransitions;
    
    // Execution state
    bool m_bCompiled = false;
    bool m_bExecuted = false;
    
#if RDG_ENABLE_DEBUG
    bool m_bAllowRHIAccess = false;
#endif
};

}} // namespace MonsterRender::RDG
