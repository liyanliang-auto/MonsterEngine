// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ForwardRenderPasses.cpp
 * @brief Implementation of forward rendering passes
 * 
 * This file implements the core render passes for the forward rendering pipeline:
 * - Depth Prepass
 * - Opaque Pass
 * - Skybox Pass
 * - Transparent Pass
 * - Shadow Depth Pass
 * - Forward Renderer
 */

#include "Core/CoreTypes.h"
#include "Core/Logging/LogMacros.h"
#include "Containers/Array.h"

// Define log category for forward renderer (must be at global scope, before any namespace)
DEFINE_LOG_CATEGORY_STATIC(LogForwardRenderer, Log, All);

#include "Renderer/ForwardRenderPasses.h"
#include "Renderer/Scene.h"
#include "Renderer/MeshElementCollector.h"
#include "Renderer/MeshBatch.h"
#include "Engine/SceneView.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/PrimitiveSceneInfo.h"
#include "Engine/LightSceneInfo.h"
#include "RHI/IRHICommandList.h"

namespace MonsterEngine {
namespace Renderer {

// Type alias to avoid template parsing issues in function signatures
using FLightSceneInfoArray = TArray<FLightSceneInfo*>;

// ============================================================================
// FDepthPrepass Implementation
// ============================================================================

FDepthPrepass::FDepthPrepass()
    : FRenderPassBase(FRenderPassConfig())
{
    Config.PassType = ERenderPassType::DepthPrepass;
    Config.PassName = FString("DepthPrepass");
    Config.Priority = 0; // First pass (lowest priority number = earliest)
    
    // Depth prepass configuration: write depth only, no color output
    Config.DepthStencilState = FDepthStencilState::DepthReadWrite();
    Config.BlendState.bBlendEnable = false;
    Config.BlendState.ColorWriteMask = 0; // No color writes
    Config.RasterizerState = FRasterizerState::Default();
    
    // Clear depth buffer at start of prepass
    Config.bClearDepth = true;
    Config.ClearDepth = 1.0f;
    Config.bClearColor = false;
}

bool FDepthPrepass::ShouldExecute(const FRenderPassContext& Context) const
{
    if (!FRenderPassBase::ShouldExecute(Context))
    {
        return false;
    }
    
    // Only execute if we have opaque primitives to render
    return Context.VisibleOpaquePrimitives != nullptr && 
           Context.VisibleOpaquePrimitives->Num() > 0;
}

void FDepthPrepass::Execute(FRenderPassContext& Context)
{
    if (!Context.VisibleOpaquePrimitives)
    {
        return;
    }
    
    const int32 NumPrimitives = Context.VisibleOpaquePrimitives->Num();
    MR_LOG(LogForwardPasses, Verbose, "Executing DepthPrepass with %d primitives", NumPrimitives);
    
    // Clear depth buffer
    ClearTargets(Context);
    
    // Set viewport
    SetViewport(Context);
    
    // Render depth for all opaque primitives
    for (FPrimitiveSceneInfo* Primitive : *Context.VisibleOpaquePrimitives)
    {
        if (Primitive)
        {
            RenderPrimitiveDepth(Context, Primitive);
        }
    }
}

void FDepthPrepass::RenderPrimitiveDepth(FRenderPassContext& Context, FPrimitiveSceneInfo* Primitive)
{
    if (!Primitive)
    {
        return;
    }
    
    // TODO: Implement actual depth rendering through RHI
    // This will bind depth-only shaders and render the primitive's geometry
    //
    // Implementation steps:
    // 1. Get primitive's mesh data from proxy
    // 2. Bind depth-only vertex shader (transforms vertices, outputs position only)
    // 3. If masked material and bRenderMasked:
    //    - Bind pixel shader for alpha test
    //    - Bind material textures for alpha sampling
    // 4. Set vertex/index buffers
    // 5. Set per-object uniform buffer (world matrix, etc.)
    // 6. Draw indexed primitives
    //
    // Pseudocode:
    // FPrimitiveSceneProxy* Proxy = Primitive->GetProxy();
    // if (Proxy)
    // {
    //     const FMeshBatch& Mesh = Proxy->GetMeshBatch();
    //     
    //     // Bind depth shader
    //     Context.RHICmdList->SetShader(DepthOnlyShader);
    //     
    //     // Set transforms
    //     FMatrix WorldMatrix = Proxy->GetLocalToWorld();
    //     FMatrix ViewProjection = Context.View->GetViewProjectionMatrix();
    //     Context.RHICmdList->SetUniform("WorldViewProjection", WorldMatrix * ViewProjection);
    //     
    //     // Draw
    //     Context.RHICmdList->SetVertexBuffer(Mesh.VertexBuffer);
    //     Context.RHICmdList->SetIndexBuffer(Mesh.IndexBuffer);
    //     Context.RHICmdList->DrawIndexed(Mesh.NumIndices, 0, 0);
    // }
}

// ============================================================================
// FOpaquePass Implementation
// ============================================================================

FOpaquePass::FOpaquePass()
    : FRenderPassBase(FRenderPassConfig())
{
    Config.PassType = ERenderPassType::Opaque;
    Config.PassName = FString("OpaquePass");
    Config.Priority = 100; // After depth prepass
    
    // Opaque pass configuration: depth test with equal (since prepass wrote depth)
    Config.DepthStencilState = FDepthStencilState::DepthReadWrite();
    Config.DepthStencilState.DepthCompareFunc = ECompareFunc::LessEqual;
    Config.BlendState = FBlendState::Opaque();
    Config.RasterizerState = FRasterizerState::Default();
    
    // Clear color buffer (depth already cleared in prepass)
    Config.bClearColor = true;
    Config.ClearColor = FVector4f(0.1f, 0.1f, 0.15f, 1.0f); // Dark blue-gray
    Config.bClearDepth = false; // Depth already cleared in prepass
    
    // Reserve space for temporary light array
    TempAffectingLights.Reserve(MaxLightsPerObject);
}

bool FOpaquePass::ShouldExecute(const FRenderPassContext& Context) const
{
    if (!FRenderPassBase::ShouldExecute(Context))
    {
        return false;
    }
    
    return Context.VisibleOpaquePrimitives != nullptr &&
           Context.VisibleOpaquePrimitives->Num() > 0;
}

void FOpaquePass::Execute(FRenderPassContext& Context)
{
    if (!Context.VisibleOpaquePrimitives)
    {
        return;
    }
    
    const int32 NumPrimitives = Context.VisibleOpaquePrimitives->Num();
    const int32 NumLights = Context.VisibleLights ? Context.VisibleLights->Num() : 0;
    
    MR_LOG(LogForwardRenderer, Verbose, "Executing OpaquePass with %d primitives, %d lights",
           NumPrimitives, NumLights);
    
    // Store RHI device for material uniform buffer creation
    if (!RHIDevice && Context.RHIDevice)
    {
        RHIDevice = Context.RHIDevice;
    }
    
    // Create material uniform buffer if not already created
    if (!MaterialUniformBuffer && RHIDevice)
    {
        MonsterRender::RHI::BufferDesc BufferDesc;
        BufferDesc.size = sizeof(FMaterialUniformBuffer);
        BufferDesc.usage = MonsterRender::RHI::EResourceUsage::UniformBuffer;
        BufferDesc.memoryUsage = MonsterRender::RHI::EMemoryUsage::Upload;
        BufferDesc.debugName = "MaterialUniformBuffer";
        
        MaterialUniformBuffer = RHIDevice->createBuffer(BufferDesc);
        
        if (MaterialUniformBuffer)
        {
            MR_LOG(LogForwardRenderer, Log, "Created material uniform buffer (size: %llu bytes)", 
                   static_cast<uint64>(sizeof(FMaterialUniformBuffer)));
        }
        else
        {
            MR_LOG(LogForwardRenderer, Error, "Failed to create material uniform buffer");
        }
    }
    
    // Clear color buffer
    ClearTargets(Context);
    
    // Set viewport
    SetViewport(Context);
    
    // Create mesh element collector for gathering dynamic mesh elements (UE5 style)
    FMeshElementCollector MeshCollector;
    
    // Setup collector for the current view
    TArray<const FSceneView*> Views;
    Views.Add(Context.View);
    MeshCollector.SetupViews(Views);
    
    // Gather mesh elements from all visible opaque primitives
    for (FPrimitiveSceneInfo* Primitive : *Context.VisibleOpaquePrimitives)
    {
        if (!Primitive)
        {
            continue;
        }
        
        FPrimitiveSceneProxy* Proxy = Primitive->GetProxy();
        if (!Proxy || !Proxy->IsVisible())
        {
            continue;
        }
        
        // Set current primitive for the collector
        MeshCollector.SetPrimitive(Proxy);
        
        // Gather lights affecting this primitive
        TempAffectingLights.Reset();
        if (bLightingEnabled && Context.VisibleLights)
        {
            GatherAffectingLights(Context, Primitive, TempAffectingLights);
        }
        
        // Call GetDynamicMeshElements to collect mesh batches (UE5 pattern)
        // VisibilityMap: bit 0 = view 0 is visible
        uint32 VisibilityMap = 1; // Single view, always visible
        FSceneViewFamily ViewFamily{
            nullptr,  // Scene
            nullptr,  // RenderTarget
            0,        // FrameNumber
            0.0f,     // RealTimeSeconds
            0.0f,     // WorldTimeSeconds
            0.0f,     // DeltaWorldTimeSeconds
            2.2f,     // GammaCorrection
            false,    // bWireframe
            true,     // bDeferredShading
            true,     // bRenderShadows
            true,     // bRenderFog
            true,     // bRenderPostProcessing
            true,     // bRenderMotionBlur
            true,     // bRenderBloom
            true      // bRenderAmbientOcclusion
        };
        
        Proxy->GetDynamicMeshElements(Views, ViewFamily, VisibilityMap, MeshCollector);
    }
    
    // Render all collected mesh batches
    const int32 ViewIndex = 0;
    const TArray<FMeshBatchAndRelevance>* MeshBatches = MeshCollector.GetMeshBatches(ViewIndex);
    
    if (MeshBatches)
    {
        MR_LOG(LogForwardRenderer, Verbose, "Rendering %d mesh batches", MeshBatches->Num());
        
        for (const FMeshBatchAndRelevance& MeshBatchAndRelevance : *MeshBatches)
        {
            const FMeshBatch* MeshBatch = MeshBatchAndRelevance.Mesh;
            const FPrimitiveSceneProxy* Proxy = MeshBatchAndRelevance.PrimitiveSceneProxy;
            
            if (!MeshBatch || !Proxy)
            {
                continue;
            }
            
            // Render this mesh batch
            RenderMeshBatch(Context, MeshBatch, Proxy);
        }
    }
}

void FOpaquePass::RenderMeshBatch(
    FRenderPassContext& Context,
    const FMeshBatch* MeshBatch,
    const FPrimitiveSceneProxy* Proxy)
{
    if (!MeshBatch || !Proxy || !Context.RHICmdList || !Context.View)
    {
        return;
    }
    
    // Validate mesh batch has draw calls
    if (!MeshBatch->HasAnyDrawCalls())
    {
        MR_LOG(LogForwardRenderer, VeryVerbose, "Skipping mesh batch with no draw calls");
        return;
    }
    
    // Get view matrices for uniform buffer updates
    const FMatrix& ViewMatrix = Context.View->ViewMatrices.GetViewMatrix();
    const FMatrix& ProjectionMatrix = Context.View->ViewMatrices.GetProjectionMatrix();
    const FVector& CameraPosition = Context.View->ViewMatrices.GetViewOrigin();
    
    // Check if material render proxy is available and extract material parameters
    FLinearColor BaseColor(1.0f, 1.0f, 1.0f, 1.0f);  // Default white
    float Metallic = 0.0f;
    float Roughness = 0.5f;
    float Specular = 0.5f;
    
    if (MeshBatch->MaterialRenderProxy)
    {
        // Extract material parameters from the render proxy
        FMaterialParameterInfo BaseColorInfo(FName("BaseColor"));
        FMaterialParameterInfo MetallicInfo(FName("Metallic"));
        FMaterialParameterInfo RoughnessInfo(FName("Roughness"));
        FMaterialParameterInfo SpecularInfo(FName("Specular"));
        
        // Try to get base color (vector parameter)
        if (!MeshBatch->MaterialRenderProxy->GetVectorValue(BaseColorInfo, BaseColor))
        {
            MR_LOG(LogForwardRenderer, VeryVerbose, "Using default base color");
        }
        
        // Try to get metallic (scalar parameter)
        if (!MeshBatch->MaterialRenderProxy->GetScalarValue(MetallicInfo, Metallic))
        {
            MR_LOG(LogForwardRenderer, VeryVerbose, "Using default metallic value");
        }
        
        // Try to get roughness (scalar parameter)
        if (!MeshBatch->MaterialRenderProxy->GetScalarValue(RoughnessInfo, Roughness))
        {
            MR_LOG(LogForwardRenderer, VeryVerbose, "Using default roughness value");
        }
        
        // Try to get specular (scalar parameter)
        if (!MeshBatch->MaterialRenderProxy->GetScalarValue(SpecularInfo, Specular))
        {
            MR_LOG(LogForwardRenderer, VeryVerbose, "Using default specular value");
        }
        
        MR_LOG(LogForwardRenderer, Verbose, 
               "Material parameters: BaseColor=(%.2f,%.2f,%.2f,%.2f), Metallic=%.2f, Roughness=%.2f, Specular=%.2f",
               BaseColor.R, BaseColor.G, BaseColor.B, BaseColor.A,
               Metallic, Roughness, Specular);
    }
    else
    {
        MR_LOG(LogForwardRenderer, Verbose, "No material render proxy, using default material parameters");
    }
    
    // Update material uniform buffer with extracted parameters
    if (MaterialUniformBuffer)
    {
        // Fill material uniform buffer structure
        FMaterialUniformBuffer MaterialData;
        MaterialData.BaseColor[0] = BaseColor.R;
        MaterialData.BaseColor[1] = BaseColor.G;
        MaterialData.BaseColor[2] = BaseColor.B;
        MaterialData.BaseColor[3] = BaseColor.A;
        MaterialData.Metallic = Metallic;
        MaterialData.Roughness = Roughness;
        MaterialData.Specular = Specular;
        MaterialData.Padding = 0.0f;
        
        // Map buffer and update data
        void* MappedData = MaterialUniformBuffer->map();
        if (MappedData)
        {
            FMemory::Memcpy(MappedData, &MaterialData, sizeof(FMaterialUniformBuffer));
            MaterialUniformBuffer->unmap();
            
            MR_LOG(LogForwardRenderer, VeryVerbose, "Updated material uniform buffer to GPU");
        }
        else
        {
            MR_LOG(LogForwardRenderer, Warning, "Failed to map material uniform buffer");
        }
    }
    
    // Iterate through all elements in the mesh batch
    for (const FMeshBatchElement& Element : MeshBatch->Elements)
    {
        // Validate element has primitives to draw
        if (Element.NumPrimitives == 0)
        {
            continue;
        }
        
        // Validate required resources
        if (!Element.VertexBuffer || !Element.PipelineState)
        {
            MR_LOG(LogForwardRenderer, Warning, "Mesh batch element missing required resources");
            continue;
        }
        
        // Set pipeline state
        Context.RHICmdList->setPipelineState(Element.PipelineState);
        
        // Bind vertex buffer
        TArray<TSharedPtr<MonsterRender::RHI::IRHIBuffer>> VertexBuffers;
        VertexBuffers.Add(Element.VertexBuffer);
        Context.RHICmdList->setVertexBuffers(0, VertexBuffers);
        
        // Bind material uniform buffer to shader
        // Material data is bound to binding 4 (matching shader layout)
        // Binding 0: TransformUBO
        // Binding 1: texture1
        // Binding 2: texture2
        // Binding 3: LightingUBO
        // Binding 4: MaterialUBO
        if (MaterialUniformBuffer)
        {
            Context.RHICmdList->setConstantBuffer(4, MaterialUniformBuffer);
            MR_LOG(LogForwardRenderer, VeryVerbose, "Bound material uniform buffer to binding 4");
        }
        
        // TODO: Update uniform buffers with view/projection matrices and lighting data
        // This requires the proxy to expose methods for updating its uniform buffers
        // For now, we rely on the proxy having already set up its uniform buffers
        // In a full implementation, we would:
        // 1. Get or create per-frame uniform buffer
        // 2. Update with current view/projection matrices
        // 3. Update with lighting data from TempAffectingLights
        // 4. Bind uniform buffer to shader (Set 0 for per-frame data)
        
        // Draw the mesh element
        if (Element.IndexBuffer)
        {
            // Indexed drawing
            Context.RHICmdList->setIndexBuffer(Element.IndexBuffer);
            
            const uint32 IndexCount = Element.NumPrimitives * 3; // Assuming triangles
            Context.RHICmdList->drawIndexed(
                IndexCount,
                Element.FirstIndex,
                Element.BaseVertexIndex
            );
            
            MR_LOG(LogForwardRenderer, VeryVerbose, 
                   "Drew indexed mesh: %d primitives, %d indices",
                   Element.NumPrimitives, IndexCount);
        }
        else
        {
            // Non-indexed drawing
            const uint32 VertexCount = Element.NumPrimitives * 3; // Assuming triangles
            Context.RHICmdList->draw(VertexCount, Element.BaseVertexIndex);
            
            MR_LOG(LogForwardRenderer, VeryVerbose,
                   "Drew non-indexed mesh: %d primitives, %d vertices",
                   Element.NumPrimitives, VertexCount);
        }
    }
}

void FOpaquePass::GatherAffectingLights(
    FRenderPassContext& Context,
    FPrimitiveSceneInfo* Primitive,
    FLightSceneInfoArray& OutLights)
{
    if (!Context.VisibleLights || !Primitive)
    {
        return;
    }
    
    // Get primitive bounds for light culling
    FPrimitiveSceneProxy* Proxy = Primitive->GetProxy();
    if (!Proxy)
    {
        return;
    }
    const FBoxSphereBounds& PrimitiveBounds = Proxy->GetBounds();
    
    // Check each visible light
    for (FLightSceneInfo* Light : *Context.VisibleLights)
    {
        if (!Light)
        {
            continue;
        }
        
        // Check if light affects this primitive's bounds
        bool bAffects = Light->AffectsBounds(PrimitiveBounds);
        
        if (bAffects)
        {
            OutLights.Add(Light);
            
            // Limit to max lights per object for performance
            if (OutLights.Num() >= MaxLightsPerObject)
            {
                break;
            }
        }
    }
}

void FOpaquePass::SetupLightBuffer(
    FRenderPassContext& Context,
    const FLightSceneInfoArray& Lights)
{
    // TODO: Create and bind light uniform buffer using FLightUniformBufferManager
    // This will pack light data into a GPU-friendly format
    //
    // Pseudocode:
    // FForwardLightData LightData;
    // LightData.NumDirectionalLights = 0;
    // LightData.NumLocalLights = 0;
    // 
    // for (FLightSceneInfo* Light : Lights)
    // {
    //     FLightSceneProxy* Proxy = Light->GetProxy();
    //     if (Proxy->IsDirectionalLight())
    //     {
    //         // Add directional light data
    //         FDirectionalLightData& DirLight = LightData.DirectionalLights[LightData.NumDirectionalLights++];
    //         DirLight.Direction = Proxy->GetDirection();
    //         DirLight.Color = Proxy->GetColor() * Proxy->GetIntensity();
    //     }
    //     else
    //     {
    //         // Add local light data (point/spot)
    //         FLocalLightData& LocalLight = LightData.LocalLights[LightData.NumLocalLights++];
    //         LocalLight.Position = Proxy->GetPosition();
    //         LocalLight.InvRadius = 1.0f / Proxy->GetRadius();
    //         LocalLight.Color = Proxy->GetColor() * Proxy->GetIntensity();
    //         // ... etc
    //     }
    // }
    // 
    // Context.RHICmdList->SetUniformBuffer("ForwardLightData", &LightData, sizeof(LightData));
}

// ============================================================================
// FSkyboxPass Implementation
// ============================================================================

FSkyboxPass::FSkyboxPass()
    : FRenderPassBase(FRenderPassConfig())
{
    Config.PassType = ERenderPassType::Skybox;
    Config.PassName = FString("SkyboxPass");
    Config.Priority = 200; // After opaque, before transparent
    
    // Skybox configuration: depth test (LessEqual to render at far plane), no depth write
    Config.DepthStencilState = FDepthStencilState::DepthReadOnly();
    Config.DepthStencilState.DepthCompareFunc = ECompareFunc::LessEqual;
    Config.BlendState = FBlendState::Opaque();
    Config.RasterizerState = FRasterizerState::Default();
    Config.RasterizerState.CullMode = ECullMode::Front; // Render inside of cube/sphere
    
    // No clearing needed - we render over existing content
    Config.bClearColor = false;
    Config.bClearDepth = false;
}

bool FSkyboxPass::ShouldExecute(const FRenderPassContext& Context) const
{
    if (!FRenderPassBase::ShouldExecute(Context))
    {
        return false;
    }
    
    // Execute if we have a skybox texture or atmospheric scattering enabled
    return SkyboxTexture != nullptr || bAtmosphericScattering;
}

void FSkyboxPass::Execute(FRenderPassContext& Context)
{
    MR_LOG(LogForwardRenderer, Verbose, "Executing SkyboxPass (Texture: %s, Atmosphere: %s)",
           SkyboxTexture ? "Yes" : "No",
           bAtmosphericScattering ? "Yes" : "No");
    
    // Set viewport
    SetViewport(Context);
    
    if (SkyboxTexture)
    {
        RenderSkybox(Context);
    }
    else if (bAtmosphericScattering)
    {
        RenderProceduralSky(Context);
    }
}

void FSkyboxPass::RenderSkybox(FRenderPassContext& Context)
{
    // TODO: Implement skybox rendering
    // Implementation steps:
    // 1. Bind skybox shader
    // 2. Create view matrix with rotation only (remove translation for infinite distance effect)
    // 3. Set projection matrix
    // 4. Bind cubemap texture
    // 5. Set tint and intensity uniforms
    // 6. Draw unit cube or sphere
    //
    // Pseudocode:
    // Context.RHICmdList->SetShader(SkyboxShader);
    // 
    // // View matrix without translation (skybox at infinity)
    // FMatrix ViewRotation = Context.View->GetViewMatrix();
    // ViewRotation.SetOrigin(FVector::ZeroVector);
    // 
    // Context.RHICmdList->SetUniform("ViewRotation", ViewRotation);
    // Context.RHICmdList->SetUniform("Projection", Context.View->GetProjectionMatrix());
    // Context.RHICmdList->SetUniform("SkyboxTint", SkyboxTint);
    // Context.RHICmdList->SetUniform("SkyboxIntensity", SkyboxIntensity);
    // 
    // Context.RHICmdList->SetTexture("SkyboxCubemap", SkyboxTexture);
    // 
    // // Draw unit cube
    // Context.RHICmdList->SetVertexBuffer(UnitCubeVertexBuffer);
    // Context.RHICmdList->SetIndexBuffer(UnitCubeIndexBuffer);
    // Context.RHICmdList->DrawIndexed(36, 0, 0);
}

void FSkyboxPass::RenderProceduralSky(FRenderPassContext& Context)
{
    // TODO: Implement procedural sky rendering
    // This could be a simple gradient sky or full atmospheric scattering
    //
    // Simple gradient sky pseudocode:
    // Context.RHICmdList->SetShader(ProceduralSkyShader);
    // 
    // Context.RHICmdList->SetUniform("SkyColorTop", FVector3f(0.5f, 0.7f, 1.0f));
    // Context.RHICmdList->SetUniform("SkyColorHorizon", FVector3f(0.8f, 0.9f, 1.0f));
    // Context.RHICmdList->SetUniform("SkyColorBottom", FVector3f(0.3f, 0.4f, 0.5f));
    // Context.RHICmdList->SetUniform("SunDirection", GetSunDirection());
    // Context.RHICmdList->SetUniform("SunColor", FVector3f(1.0f, 0.95f, 0.8f));
    // 
    // // Draw fullscreen quad or sphere
    // DrawFullscreenQuad(Context);
}

// ============================================================================
// FTransparentPass Implementation
// ============================================================================

FTransparentPass::FTransparentPass()
    : FRenderPassBase(FRenderPassConfig())
{
    Config.PassType = ERenderPassType::Transparent;
    Config.PassName = FString("TransparentPass");
    Config.Priority = 300; // After skybox
    
    // Transparent configuration: depth test (no write), alpha blending
    Config.DepthStencilState = FDepthStencilState::DepthReadOnly();
    Config.BlendState = FBlendState::AlphaBlend();
    Config.RasterizerState = FRasterizerState::Default();
    
    // No clearing - render over existing content
    Config.bClearColor = false;
    Config.bClearDepth = false;
    
    // Reserve space for sorted primitives
    SortedPrimitives.Reserve(256);
}

bool FTransparentPass::ShouldExecute(const FRenderPassContext& Context) const
{
    if (!FRenderPassBase::ShouldExecute(Context))
    {
        return false;
    }
    
    return Context.VisibleTransparentPrimitives != nullptr &&
           Context.VisibleTransparentPrimitives->Num() > 0;
}

void FTransparentPass::Setup(FRenderPassContext& Context)
{
    FRenderPassBase::Setup(Context);
    
    // Sort primitives if needed
    if (SortMode != ESortMode::None && Context.VisibleTransparentPrimitives)
    {
        SortPrimitives(Context, *Context.VisibleTransparentPrimitives);
    }
}

void FTransparentPass::Execute(FRenderPassContext& Context)
{
    if (!Context.VisibleTransparentPrimitives)
    {
        return;
    }
    
    const int32 NumPrimitives = Context.VisibleTransparentPrimitives->Num();
    MR_LOG(LogForwardRenderer, Verbose, "Executing TransparentPass with %d primitives (SortMode: %d)",
           NumPrimitives, static_cast<int32>(SortMode));
    
    // Set viewport
    SetViewport(Context);
    
    // Render primitives
    if (SortMode != ESortMode::None)
    {
        // Render from sorted list
        for (const auto& SortedPrimitive : SortedPrimitives)
        {
            RenderTransparentPrimitive(Context, SortedPrimitive.Value);
        }
    }
    else
    {
        // Render in submission order
        for (FPrimitiveSceneInfo* Primitive : *Context.VisibleTransparentPrimitives)
        {
            if (Primitive)
            {
                RenderTransparentPrimitive(Context, Primitive);
            }
        }
    }
}

void FTransparentPass::SortPrimitives(
    FRenderPassContext& Context,
    TArray<FPrimitiveSceneInfo*>& Primitives)
{
    SortedPrimitives.Reset();
    
    // Calculate sort keys and add to sorted array
    for (FPrimitiveSceneInfo* Primitive : Primitives)
    {
        if (Primitive)
        {
            float SortKey = CalculateSortKey(Context, Primitive);
            SortedPrimitives.Add(TPair<float, FPrimitiveSceneInfo*>(SortKey, Primitive));
        }
    }
    
    // Sort based on mode
    if (SortMode == ESortMode::BackToFront)
    {
        // Sort by descending distance (far to near)
        SortedPrimitives.Sort([](const TPair<float, FPrimitiveSceneInfo*>& A, 
                                  const TPair<float, FPrimitiveSceneInfo*>& B)
        {
            return A.Key > B.Key;
        });
    }
    else if (SortMode == ESortMode::FrontToBack)
    {
        // Sort by ascending distance (near to far)
        SortedPrimitives.Sort([](const TPair<float, FPrimitiveSceneInfo*>& A, 
                                  const TPair<float, FPrimitiveSceneInfo*>& B)
        {
            return A.Key < B.Key;
        });
    }
}

void FTransparentPass::RenderTransparentPrimitive(
    FRenderPassContext& Context,
    FPrimitiveSceneInfo* Primitive)
{
    if (!Primitive)
    {
        return;
    }
    
    // TODO: Implement transparent primitive rendering
    // Similar to opaque rendering but with alpha blending enabled
    // May also need to handle double-sided rendering for some materials
    //
    // Pseudocode:
    // FPrimitiveSceneProxy* Proxy = Primitive->GetProxy();
    // if (Proxy)
    // {
    //     // Bind transparent shader (may be same as opaque with different blend state)
    //     Context.RHICmdList->SetShader(TransparentLitShader);
    //     
    //     // Set uniforms (same as opaque)
    //     // ...
    //     
    //     // Set opacity/alpha uniform
    //     Context.RHICmdList->SetUniform("Opacity", Proxy->GetMaterial().Opacity);
    //     
    //     // Draw
    //     // ...
    // }
}

float FTransparentPass::CalculateSortKey(
    FRenderPassContext& Context,
    FPrimitiveSceneInfo* Primitive)
{
    if (!Context.View || !Primitive)
    {
        return 0.0f;
    }
    
    // Calculate squared distance from camera to primitive center
    // Using squared distance avoids expensive sqrt and is sufficient for sorting
    FPrimitiveSceneProxy* Proxy = Primitive->GetProxy();
    if (!Proxy)
    {
        return 0.0f;
    }
    const FVector CameraPosition = Context.View->ViewMatrices.GetViewOrigin();
    const FVector PrimitiveCenter = Proxy->GetBounds().Origin;
    
    return static_cast<float>((PrimitiveCenter - CameraPosition).SizeSquared());
}

// ============================================================================
// FShadowDepthPass Implementation
// ============================================================================

FShadowDepthPass::FShadowDepthPass()
    : FRenderPassBase(FRenderPassConfig())
{
    Config.PassType = ERenderPassType::ShadowDepth;
    Config.PassName = FString("ShadowDepthPass");
    Config.Priority = -100; // Before all other passes (negative priority)
    
    // Shadow depth configuration: depth write only, with bias
    Config.DepthStencilState = FDepthStencilState::DepthReadWrite();
    Config.BlendState.bBlendEnable = false;
    Config.BlendState.ColorWriteMask = 0; // No color output
    Config.RasterizerState = FRasterizerState::ShadowDepth();
    
    // Clear depth for shadow map
    Config.bClearDepth = true;
    Config.ClearDepth = 1.0f;
    Config.bClearColor = false;
}

bool FShadowDepthPass::ShouldExecute(const FRenderPassContext& Context) const
{
    if (!FRenderPassBase::ShouldExecute(Context))
    {
        return false;
    }
    
    // Need a light and shadow casters
    return CurrentLight != nullptr && 
           Context.VisibleOpaquePrimitives != nullptr &&
           Context.VisibleOpaquePrimitives->Num() > 0;
}

void FShadowDepthPass::Execute(FRenderPassContext& Context)
{
    if (!CurrentLight || !Context.VisibleOpaquePrimitives)
    {
        return;
    }
    
    MR_LOG(LogForwardRenderer, Verbose, "Executing ShadowDepthPass for light, resolution: %d",
           ShadowConfig.Resolution);
    
    // Setup shadow map render target
    SetupShadowMapTarget(Context);
    
    // Clear depth
    ClearTargets(Context);
    
    // Calculate light view-projection matrix
    FMatrix LightViewProjection;
    
    if (ShadowConfig.Type == EShadowMapType::Cascaded)
    {
        // For CSM, calculate cascade-specific matrix
        LightViewProjection = CalculateCascadeViewProjection(Context, CurrentCascadeIndex);
    }
    else
    {
        LightViewProjection = CalculateLightViewProjection(Context);
    }
    
    // Store in generated shadow data
    GeneratedShadowData.Light = CurrentLight;
    GeneratedShadowData.LightViewProjection = LightViewProjection;
    GeneratedShadowData.Config = ShadowConfig;
    GeneratedShadowData.bValid = true;
    
    // Render shadow casters
    RenderShadowCasters(Context, LightViewProjection);
}

void FShadowDepthPass::SetupShadowMapTarget(FRenderPassContext& Context)
{
    if (!Context.RHICmdList)
    {
        MR_LOG(LogForwardRenderer, Error, "Invalid RHI command list in SetupShadowMapTarget");
        return;
    }
    
    // Create shadow map texture if not already created
    if (!GeneratedShadowData.ShadowMapTexture)
    {
        MonsterRender::RHI::IRHIDevice* Device = Context.RHIDevice;
        if (!Device)
        {
            MR_LOG(LogForwardRenderer, Error, "Failed to get RHI device for shadow map creation");
            return;
        }
        
        // Create shadow map depth texture descriptor
        MonsterRender::RHI::TextureDesc ShadowMapDesc;
        ShadowMapDesc.width = ShadowConfig.Resolution;
        ShadowMapDesc.height = ShadowConfig.Resolution;
        ShadowMapDesc.depth = 1;
        ShadowMapDesc.mipLevels = 1;
        ShadowMapDesc.arraySize = (ShadowConfig.Type == EShadowMapType::CubeMap) ? 6 : 1;
        ShadowMapDesc.format = MonsterRender::RHI::EPixelFormat::D32_FLOAT;
        ShadowMapDesc.usage = MonsterRender::RHI::EResourceUsage::DepthStencil | 
                              MonsterRender::RHI::EResourceUsage::ShaderResource;
        ShadowMapDesc.debugName = "ShadowMap";
        
        // Create the texture (compatible with both Vulkan and OpenGL)
        GeneratedShadowData.ShadowMapTexture = Device->createTexture(ShadowMapDesc);
        
        if (!GeneratedShadowData.ShadowMapTexture)
        {
            MR_LOG(LogForwardRenderer, Error, "Failed to create shadow map texture (resolution: %d)", 
                   ShadowConfig.Resolution);
            return;
        }
        
        MR_LOG(LogForwardRenderer, Log, "Created shadow map texture (resolution: %d, type: %d)", 
               ShadowConfig.Resolution, static_cast<int32>(ShadowConfig.Type));
    }
    
    // Set shadow map as render target (depth only, no color targets)
    TArray<TSharedPtr<MonsterRender::RHI::IRHITexture>> EmptyColorTargets;
    Context.RHICmdList->setRenderTargets(
        TSpan<TSharedPtr<MonsterRender::RHI::IRHITexture>>(EmptyColorTargets),
        GeneratedShadowData.ShadowMapTexture
    );
    
    // Set viewport for shadow map rendering
    MonsterRender::RHI::Viewport ShadowViewport;
    ShadowViewport.x = 0.0f;
    ShadowViewport.y = 0.0f;
    ShadowViewport.width = static_cast<float>(ShadowConfig.Resolution);
    ShadowViewport.height = static_cast<float>(ShadowConfig.Resolution);
    ShadowViewport.minDepth = 0.0f;
    ShadowViewport.maxDepth = 1.0f;
    Context.RHICmdList->setViewport(ShadowViewport);
    
    // Set scissor rect
    MonsterRender::RHI::ScissorRect ShadowScissor;
    ShadowScissor.left = 0;
    ShadowScissor.top = 0;
    ShadowScissor.right = ShadowConfig.Resolution;
    ShadowScissor.bottom = ShadowConfig.Resolution;
    Context.RHICmdList->setScissorRect(ShadowScissor);
}

FMatrix FShadowDepthPass::CalculateLightViewProjection(FRenderPassContext& Context)
{
    if (!CurrentLight)
    {
        MR_LOG(LogForwardRenderer, Warning, "No current light set for shadow depth pass");
        return FMatrix::Identity;
    }
    
    FLightSceneProxy* Proxy = CurrentLight->GetProxy();
    if (!Proxy)
    {
        MR_LOG(LogForwardRenderer, Warning, "Light has no proxy");
        return FMatrix::Identity;
    }
    
    // Get light type and direction
    ELightType LightType = Proxy->GetLightType();
    
    if (LightType == ELightType::Directional)
    {
        // Directional light shadow mapping
        // Reference: UE5 FDirectionalLightSceneProxy::GetWholeSceneShadowProjection
        
        // Get light direction (normalized)
        FVector LightDirection = Proxy->GetDirection();
        if (LightDirection.IsNearlyZero())
        {
            LightDirection = FVector(0.0f, -1.0f, 0.0f); // Default to down
        }
        LightDirection = LightDirection.GetSafeNormal();
        
        // Calculate scene bounds radius from shadow config
        // For now, use a fixed scene bounds. In UE5, this would come from scene bounds or view frustum
        float SceneBoundsRadius = ShadowConfig.SceneBoundsRadius > 0.0f ? 
                                  ShadowConfig.SceneBoundsRadius : 30.0f;
        
        // Calculate light position (far enough to encompass entire scene)
        float LightDistance = SceneBoundsRadius * 2.0f;
        FVector LightPosition = -LightDirection * LightDistance;
        
        // Calculate up vector (avoid parallel to light direction)
        FVector UpVector = FVector(0.0f, 1.0f, 0.0f);
        if (FMath::Abs(FVector::DotProduct(LightDirection, UpVector)) > 0.99f)
        {
            UpVector = FVector(1.0f, 0.0f, 0.0f);
        }
        
        // Create light view matrix (look at scene center from light position)
        FVector TargetPosition = FVector::ZeroVector; // Scene center
        FMatrix LightViewMatrix = FMatrix::MakeLookAt(LightPosition, TargetPosition, UpVector);
        
        // Create orthographic projection for directional light
        // Expand bounds slightly to avoid edge clipping
        float OrthoSize = SceneBoundsRadius * 1.5f;
        float NearPlane = 0.1f;
        float FarPlane = LightDistance * 2.0f;
        
        FMatrix LightProjectionMatrix = FMatrix::MakeOrtho(
            OrthoSize * 2.0f, 
            OrthoSize * 2.0f, 
            NearPlane, 
            FarPlane
        );
        
        // Combine view and projection
        FMatrix LightViewProjection = LightViewMatrix * LightProjectionMatrix;
        
        MR_LOG(LogForwardRenderer, Verbose, 
               "Calculated directional light VP: dir=(%.2f,%.2f,%.2f), bounds=%.1f",
               LightDirection.X, LightDirection.Y, LightDirection.Z, SceneBoundsRadius);
        
        return LightViewProjection;
    }
    else if (LightType == ELightType::Spot)
    {
        // Spot light shadow mapping
        // TODO: Implement spot light perspective projection
        // Reference: UE5 FSpotLightSceneProxy::GetShadowProjection
        
        MR_LOG(LogForwardRenderer, Warning, "Spot light shadows not yet implemented");
        return FMatrix::Identity;
    }
    else if (LightType == ELightType::Point)
    {
        // Point light shadow mapping (cubemap)
        // TODO: Implement point light cubemap projection
        // Reference: UE5 FPointLightSceneProxy::GetShadowProjection
        
        MR_LOG(LogForwardRenderer, Warning, "Point light shadows not yet implemented");
        return FMatrix::Identity;
    }
    
    MR_LOG(LogForwardRenderer, Warning, "Unsupported light type for shadows: %d", 
           static_cast<int32>(LightType));
    return FMatrix::Identity;
}

FMatrix FShadowDepthPass::CalculateCascadeViewProjection(FRenderPassContext& Context, int32 CascadeIndex)
{
    // TODO: Calculate cascade-specific view-projection for CSM
    // This involves:
    // 1. Calculate cascade frustum bounds based on split distances
    // 2. Fit orthographic projection to cascade frustum
    // 3. Stabilize projection to reduce shadow swimming
    
    return FMatrix::Identity;
}

void FShadowDepthPass::RenderShadowCasters(FRenderPassContext& Context, const FMatrix& LightViewProjection)
{
    if (!Context.VisibleOpaquePrimitives || !Context.RHICmdList)
    {
        return;
    }
    
    int32 NumShadowCasters = 0;
    
    // Render all shadow-casting primitives from light's perspective
    // Reference: UE5 FShadowDepthPassMeshProcessor
    for (FPrimitiveSceneInfo* Primitive : *Context.VisibleOpaquePrimitives)
    {
        if (!Primitive)
        {
            continue;
        }
        
        FPrimitiveSceneProxy* Proxy = Primitive->GetProxy();
        if (!Proxy)
        {
            continue;
        }
        
        // Check if primitive casts shadows
        if (!Proxy->CastsShadow())
        {
            continue;
        }
        
        // Check if proxy is visible
        if (!Proxy->IsVisible())
        {
            continue;
        }
        
        // Calculate light position for depth-only rendering
        FVector LightDirection = CurrentLight ? CurrentLight->GetProxy()->GetDirection() : FVector(0.0f, -1.0f, 0.0f);
        FVector LightPosition = -LightDirection.GetSafeNormal() * 10.0f;
        
        // Cast to specific proxy types and call their depth-only draw methods
        // In UE5, this would use FMeshDrawCommand with depth-only shaders
        
        // Try FCubeSceneProxy
        MonsterEngine::FCubeSceneProxy* CubeProxy = dynamic_cast<MonsterEngine::FCubeSceneProxy*>(Proxy);
        if (CubeProxy)
        {
            // Update model matrix if needed
            CubeProxy->UpdateModelMatrix(Proxy->GetLocalToWorld());
            
            // Draw cube for shadow map using light's view-projection
            // The Draw method will use depth-only pipeline internally
            CubeProxy->DrawDepthOnly(
                Context.RHICmdList,
                LightViewProjection
            );
            
            NumShadowCasters++;
            MR_LOG(LogForwardRenderer, VeryVerbose, "Rendered cube shadow caster");
            continue;
        }
        
        // Try FFloorSceneProxy
        MonsterEngine::FFloorSceneProxy* FloorProxy = dynamic_cast<MonsterEngine::FFloorSceneProxy*>(Proxy);
        if (FloorProxy)
        {
            // TODO: Implement DrawDepthOnly for FFloorSceneProxy
            // For now, skip floor shadow casting
            MR_LOG(LogForwardRenderer, VeryVerbose, "Floor shadow casting not yet implemented");
            continue;
        }
        
        // Generic fallback for other proxy types
        MR_LOG(LogForwardRenderer, VeryVerbose, "Unsupported primitive proxy type for shadow casting");
    }
    
    MR_LOG(LogForwardRenderer, Verbose, "Rendered %d shadow casters", NumShadowCasters);
    
    // Transition shadow map from depth attachment to shader resource
    // This is critical for Vulkan - the image layout must be correct for shader reads
    if (GeneratedShadowData.ShadowMapTexture)
    {
        Context.RHICmdList->transitionResource(
            GeneratedShadowData.ShadowMapTexture,
            MonsterRender::RHI::EResourceUsage::DepthStencil,
            MonsterRender::RHI::EResourceUsage::ShaderResource
        );
        
        MR_LOG(LogForwardRenderer, Verbose, "Transitioned shadow map to shader resource");
    }
}

} // namespace Renderer
} // namespace MonsterEngine
