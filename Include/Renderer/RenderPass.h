// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file RenderPass.h
 * @brief Render pass abstraction system inspired by UE5's rendering architecture
 * 
 * This file defines the base render pass interface and common pass types
 * for the forward rendering pipeline.
 * 
 * Reference UE5 files:
 * - Engine/Source/Runtime/Renderer/Private/MobileShadingRenderer.cpp
 * - Engine/Source/Runtime/Renderer/Public/MeshPassProcessor.h
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "Math/Vector4.h"
#include "RHI/RHIResources.h"

// Forward declare RHI types
namespace MonsterRender { namespace RHI {
    class IRHICommandList;
    class IRHIDevice;
    class IRHITexture;
}}

namespace MonsterEngine
{

// Forward declarations
class FScene;
class FSceneView;
struct FViewInfo;  // FViewInfo is a struct that inherits from FSceneView
class FPrimitiveSceneInfo;
class FLightSceneInfo;

// ============================================================================
// Render Pass Types
// ============================================================================

/**
 * Enumeration of render pass types in the forward pipeline
 * Following UE5's EMeshPass pattern
 */
enum class ERenderPassType : uint8
{
    /** Depth-only prepass for early-z optimization */
    DepthPrepass = 0,
    
    /** Opaque geometry pass with forward lighting */
    Opaque,
    
    /** Skybox/environment pass */
    Skybox,
    
    /** Transparent geometry pass (back-to-front sorted) */
    Transparent,
    
    /** Post-processing pass */
    PostProcess,
    
    /** Shadow depth pass */
    ShadowDepth,
    
    /** Debug visualization pass */
    Debug,
    
    /** Custom pass for extensions */
    Custom,
    
    /** Number of pass types */
    Num
};

/**
 * Get the name of a render pass type
 * @param PassType - The pass type
 * @return String name of the pass
 */
inline const TCHAR* GetRenderPassName(ERenderPassType PassType)
{
    switch (PassType)
    {
    case ERenderPassType::DepthPrepass:  return TEXT("DepthPrepass");
    case ERenderPassType::Opaque:        return TEXT("Opaque");
    case ERenderPassType::Skybox:        return TEXT("Skybox");
    case ERenderPassType::Transparent:   return TEXT("Transparent");
    case ERenderPassType::PostProcess:   return TEXT("PostProcess");
    case ERenderPassType::ShadowDepth:   return TEXT("ShadowDepth");
    case ERenderPassType::Debug:         return TEXT("Debug");
    case ERenderPassType::Custom:        return TEXT("Custom");
    default:                             return TEXT("Unknown");
    }
}

// ============================================================================
// Render Pass State
// ============================================================================

/**
 * Depth comparison function enumeration
 * Used for depth and stencil testing
 */
enum class ECompareFunc : uint8
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

/**
 * Blend factor enumeration
 */
enum class EBlendFactor : uint8
{
    Zero,
    One,
    SrcColor,
    InvSrcColor,
    SrcAlpha,
    InvSrcAlpha,
    DstAlpha,
    InvDstAlpha,
    DstColor,
    InvDstColor,
    SrcAlphaSaturate
};

/**
 * Blend operation enumeration
 */
enum class EBlendOp : uint8
{
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};

/**
 * Fill mode enumeration
 */
enum class EFillMode : uint8
{
    Solid,
    Wireframe
};

/**
 * Cull mode enumeration
 */
enum class ECullMode : uint8
{
    None,
    Front,
    Back
};

/**
 * Depth-stencil state configuration
 */
struct FDepthStencilState
{
    /** Enable depth testing */
    bool bDepthTestEnable = true;
    
    /** Enable depth writing */
    bool bDepthWriteEnable = true;
    
    /** Depth comparison function */
    ECompareFunc DepthCompareFunc = ECompareFunc::Less;
    
    /** Enable stencil testing */
    bool bStencilTestEnable = false;
    
    /** Stencil reference value */
    uint8 StencilRef = 0;
    
    /** Stencil read mask */
    uint8 StencilReadMask = 0xFF;
    
    /** Stencil write mask */
    uint8 StencilWriteMask = 0xFF;
    
    /** Static presets */
    static FDepthStencilState DepthReadWrite()
    {
        FDepthStencilState State;
        State.bDepthTestEnable = true;
        State.bDepthWriteEnable = true;
        State.DepthCompareFunc = ECompareFunc::Less;
        return State;
    }
    
    static FDepthStencilState DepthReadOnly()
    {
        FDepthStencilState State;
        State.bDepthTestEnable = true;
        State.bDepthWriteEnable = false;
        State.DepthCompareFunc = ECompareFunc::LessEqual;
        return State;
    }
    
    static FDepthStencilState DepthDisabled()
    {
        FDepthStencilState State;
        State.bDepthTestEnable = false;
        State.bDepthWriteEnable = false;
        return State;
    }
};

/**
 * Blend state configuration
 */
struct FBlendState
{
    /** Enable blending */
    bool bBlendEnable = false;
    
    EBlendFactor SrcColorBlend = EBlendFactor::One;
    EBlendFactor DstColorBlend = EBlendFactor::Zero;
    EBlendOp ColorBlendOp = EBlendOp::Add;
    
    EBlendFactor SrcAlphaBlend = EBlendFactor::One;
    EBlendFactor DstAlphaBlend = EBlendFactor::Zero;
    EBlendOp AlphaBlendOp = EBlendOp::Add;
    
    /** Color write mask (RGBA bits) */
    uint8 ColorWriteMask = 0x0F;
    
    /** Static presets */
    static FBlendState Opaque()
    {
        FBlendState State;
        State.bBlendEnable = false;
        return State;
    }
    
    static FBlendState AlphaBlend()
    {
        FBlendState State;
        State.bBlendEnable = true;
        State.SrcColorBlend = EBlendFactor::SrcAlpha;
        State.DstColorBlend = EBlendFactor::InvSrcAlpha;
        State.SrcAlphaBlend = EBlendFactor::One;
        State.DstAlphaBlend = EBlendFactor::InvSrcAlpha;
        return State;
    }
    
    static FBlendState Additive()
    {
        FBlendState State;
        State.bBlendEnable = true;
        State.SrcColorBlend = EBlendFactor::One;
        State.DstColorBlend = EBlendFactor::One;
        State.SrcAlphaBlend = EBlendFactor::One;
        State.DstAlphaBlend = EBlendFactor::One;
        return State;
    }
    
    static FBlendState Premultiplied()
    {
        FBlendState State;
        State.bBlendEnable = true;
        State.SrcColorBlend = EBlendFactor::One;
        State.DstColorBlend = EBlendFactor::InvSrcAlpha;
        State.SrcAlphaBlend = EBlendFactor::One;
        State.DstAlphaBlend = EBlendFactor::InvSrcAlpha;
        return State;
    }
};

/**
 * Rasterizer state configuration
 */
struct FRasterizerState
{
    /** Fill mode */
    EFillMode FillMode = EFillMode::Solid;
    
    /** Cull mode */
    ECullMode CullMode = ECullMode::Back;
    
    /** Front face winding */
    bool bFrontCounterClockwise = true;
    
    /** Depth bias */
    float DepthBias = 0.0f;
    float SlopeScaledDepthBias = 0.0f;
    float DepthBiasClamp = 0.0f;
    
    /** Enable depth clipping */
    bool bDepthClipEnable = true;
    
    /** Enable scissor test */
    bool bScissorEnable = false;
    
    /** Enable multisample antialiasing */
    bool bMultisampleEnable = false;
    
    /** Enable antialiased line drawing */
    bool bAntialiasedLineEnable = false;
    
    /** Static presets */
    static FRasterizerState Default()
    {
        return FRasterizerState();
    }
    
    static FRasterizerState NoCull()
    {
        FRasterizerState State;
        State.CullMode = ECullMode::None;
        return State;
    }
    
    static FRasterizerState Wireframe()
    {
        FRasterizerState State;
        State.FillMode = EFillMode::Wireframe;
        return State;
    }
    
    static FRasterizerState ShadowDepth()
    {
        FRasterizerState State;
        State.DepthBias = 1.0f;
        State.SlopeScaledDepthBias = 1.0f;
        return State;
    }
};

// ============================================================================
// Render Pass Configuration
// ============================================================================

/**
 * Configuration for a render pass
 */
struct FRenderPassConfig
{
    /** Pass type */
    ERenderPassType PassType = ERenderPassType::Opaque;
    
    /** Pass name for debugging */
    FString PassName;
    
    /** Depth-stencil state */
    FDepthStencilState DepthStencilState;
    
    /** Blend state */
    FBlendState BlendState;
    
    /** Rasterizer state */
    FRasterizerState RasterizerState;
    
    /** Clear color (if clearing) */
    FVector4f ClearColor = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
    
    /** Clear depth value */
    float ClearDepth = 1.0f;
    
    /** Clear stencil value */
    uint8 ClearStencil = 0;
    
    /** Whether to clear color target */
    bool bClearColor = false;
    
    /** Whether to clear depth target */
    bool bClearDepth = false;
    
    /** Whether to clear stencil target */
    bool bClearStencil = false;
    
    /** Whether this pass is enabled */
    bool bEnabled = true;
    
    /** Priority for pass ordering (lower = earlier) */
    int32 Priority = 0;
};

// ============================================================================
// Render Pass Context
// ============================================================================

/**
 * Context passed to render passes during execution
 * Contains all information needed to render a pass
 */
struct FRenderPassContext
{
    /** The scene being rendered */
    FScene* Scene = nullptr;
    
    /** The view being rendered */
    FViewInfo* View = nullptr;
    
    /** View index in the view family */
    int32 ViewIndex = 0;
    
    /** RHI command list for GPU commands */
    MonsterRender::RHI::IRHICommandList* RHICmdList = nullptr;
    
    /** RHI device */
    MonsterRender::RHI::IRHIDevice* RHIDevice = nullptr;
    
    /** Frame number */
    uint32 FrameNumber = 0;
    
    /** Delta time since last frame */
    float DeltaTime = 0.0f;
    
    /** Total elapsed time */
    float TotalTime = 0.0f;
    
    /** Visible opaque primitives for this view */
    TArray<FPrimitiveSceneInfo*>* VisibleOpaquePrimitives = nullptr;
    
    /** Visible transparent primitives for this view (sorted back-to-front) */
    TArray<FPrimitiveSceneInfo*>* VisibleTransparentPrimitives = nullptr;
    
    /** Visible lights affecting this view */
    TArray<FLightSceneInfo*>* VisibleLights = nullptr;
    
    /** Viewport dimensions */
    int32 ViewportX = 0;
    int32 ViewportY = 0;
    int32 ViewportWidth = 0;
    int32 ViewportHeight = 0;
    
    /** Color render target */
    TSharedPtr<MonsterRender::RHI::IRHITexture> ColorTarget;
    
    /** Depth/stencil render target */
    TSharedPtr<MonsterRender::RHI::IRHITexture> DepthTarget;
    
    /** Shadow map render target (for shadow passes) */
    TSharedPtr<MonsterRender::RHI::IRHITexture> ShadowMapTarget;
};

// ============================================================================
// Render Pass Interface
// ============================================================================

/**
 * Abstract base class for render passes
 * Following UE5's render pass pattern
 */
class IRenderPass
{
public:
    /** Virtual destructor */
    virtual ~IRenderPass() = default;
    
    /**
     * Get the pass type
     * @return The render pass type
     */
    virtual ERenderPassType GetPassType() const = 0;
    
    /**
     * Get the pass name for debugging
     * @return The pass name
     */
    virtual const TCHAR* GetPassName() const = 0;
    
    /**
     * Get the pass configuration
     * @return Reference to the pass configuration
     */
    virtual const FRenderPassConfig& GetConfig() const = 0;
    
    /**
     * Get mutable pass configuration
     * @return Reference to the pass configuration
     */
    virtual FRenderPassConfig& GetMutableConfig() = 0;
    
    /**
     * Check if this pass should be executed
     * @param Context - The render context
     * @return True if the pass should execute
     */
    virtual bool ShouldExecute(const FRenderPassContext& Context) const = 0;
    
    /**
     * Setup the pass before execution
     * Called once per frame before Execute
     * @param Context - The render context
     */
    virtual void Setup(FRenderPassContext& Context) = 0;
    
    /**
     * Execute the render pass
     * @param Context - The render context
     */
    virtual void Execute(FRenderPassContext& Context) = 0;
    
    /**
     * Cleanup after pass execution
     * @param Context - The render context
     */
    virtual void Cleanup(FRenderPassContext& Context) = 0;
};

// ============================================================================
// Base Render Pass Implementation
// ============================================================================

/**
 * Base implementation of IRenderPass with common functionality
 */
class FRenderPassBase : public IRenderPass
{
public:
    /**
     * Constructor
     * @param InConfig - Pass configuration
     */
    explicit FRenderPassBase(const FRenderPassConfig& InConfig)
        : Config(InConfig)
    {
    }
    
    /** Destructor */
    virtual ~FRenderPassBase() = default;
    
    // IRenderPass interface
    virtual ERenderPassType GetPassType() const override { return Config.PassType; }
    virtual const TCHAR* GetPassName() const override { return *Config.PassName; }
    virtual const FRenderPassConfig& GetConfig() const override { return Config; }
    virtual FRenderPassConfig& GetMutableConfig() override { return Config; }
    
    virtual bool ShouldExecute(const FRenderPassContext& Context) const override
    {
        return Config.bEnabled && Context.Scene != nullptr && Context.View != nullptr;
    }
    
    virtual void Setup(FRenderPassContext& Context) override
    {
        // Base implementation: Apply render states
        ApplyRenderStates(Context);
    }
    
    virtual void Cleanup(FRenderPassContext& Context) override
    {
        // Base implementation: Nothing to clean up
    }
    
protected:
    /**
     * Apply render states from configuration
     * @param Context - The render context
     */
    virtual void ApplyRenderStates(FRenderPassContext& Context);
    
    /**
     * Set viewport from context
     * @param Context - The render context
     */
    virtual void SetViewport(FRenderPassContext& Context);
    
    /**
     * Clear render targets if configured
     * @param Context - The render context
     */
    virtual void ClearTargets(FRenderPassContext& Context);
    
protected:
    /** Pass configuration */
    FRenderPassConfig Config;
};

// ============================================================================
// Render Pass Manager
// ============================================================================

/**
 * Manages render passes and their execution order
 */
class FRenderPassManager
{
public:
    /** Constructor */
    FRenderPassManager();
    
    /** Destructor */
    ~FRenderPassManager();
    
    /**
     * Register a render pass
     * @param Pass - The pass to register (takes ownership)
     */
    void RegisterPass(IRenderPass* Pass);
    
    /**
     * Unregister a render pass
     * @param PassType - Type of pass to unregister
     */
    void UnregisterPass(ERenderPassType PassType);
    
    /**
     * Get a registered pass by type
     * @param PassType - The pass type
     * @return Pointer to the pass, or nullptr if not found
     */
    IRenderPass* GetPass(ERenderPassType PassType) const;
    
    /**
     * Execute all registered passes in order
     * @param Context - The render context
     */
    void ExecuteAllPasses(FRenderPassContext& Context);
    
    /**
     * Execute a specific pass
     * @param PassType - The pass type to execute
     * @param Context - The render context
     */
    void ExecutePass(ERenderPassType PassType, FRenderPassContext& Context);
    
    /**
     * Sort passes by priority
     */
    void SortPasses();
    
    /**
     * Clear all registered passes
     */
    void ClearPasses();
    
    /**
     * Get all registered passes
     * @return Array of registered passes
     */
    const TArray<IRenderPass*>& GetPasses() const { return Passes; }
    
private:
    /** Registered render passes */
    TArray<IRenderPass*> Passes;
    
    /** Whether passes need sorting */
    bool bNeedsSorting = false;
};

} // namespace MonsterEngine
