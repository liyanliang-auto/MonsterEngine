// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file PBRRenderer.h
 * @brief PBR Renderer for physically-based rendering
 * 
 * FPBRRenderer manages the complete PBR rendering pipeline:
 * - Per-frame uniform buffer management
 * - Per-object rendering with materials
 * - Descriptor set binding
 * 
 * Reference:
 * - Filament: Renderer, View
 * - UE5: FSceneRenderer, FMeshPassProcessor
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Renderer/PBR/PBRUniformBuffers.h"
#include "Renderer/PBR/PBRDescriptorSetLayouts.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"

// Forward declarations
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
    class IRHICommandList;
    class IRHIBuffer;
    class IRHIDescriptorSet;
    class IRHIPipelineState;
}}

namespace MonsterEngine
{

// Forward declarations
class FTexture2D;

namespace Renderer
{

// Forward declarations
class FPBRMaterial;
class FScene;
class FSceneView;

// ============================================================================
// FPBRRenderContext
// ============================================================================

/**
 * @struct FPBRRenderContext
 * @brief Context for a single PBR render pass
 * 
 * Contains all state needed for rendering a frame with PBR.
 */
struct FPBRRenderContext
{
    /** View uniform data */
    FViewUniformBuffer ViewData;
    
    /** Light uniform data */
    FLightUniformBuffer LightData;
    
    /** Current frame index */
    uint32 FrameIndex = 0;
    
    /** Viewport dimensions */
    uint32 ViewportWidth = 1920;
    uint32 ViewportHeight = 1080;
    
    /** Time in seconds */
    float Time = 0.0f;
    float DeltaTime = 0.016f;
};

// ============================================================================
// FPBRRenderer
// ============================================================================

/**
 * @class FPBRRenderer
 * @brief Main PBR rendering class
 * 
 * Manages PBR rendering pipeline including:
 * - Uniform buffer management
 * - Descriptor set binding
 * - Draw call submission
 */
class FPBRRenderer
{
public:
    FPBRRenderer();
    ~FPBRRenderer();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initialize the PBR renderer
     * @param device RHI device for resource creation
     * @return true if initialization succeeded
     */
    bool initialize(MonsterRender::RHI::IRHIDevice* device);
    
    /**
     * Shutdown and release resources
     */
    void shutdown();
    
    /**
     * Check if renderer is initialized
     */
    bool isInitialized() const { return m_initialized; }
    
    // ========================================================================
    // Frame Management
    // ========================================================================
    
    /**
     * Begin a new frame
     * @param frameIndex Current frame index (for multi-buffering)
     */
    void beginFrame(uint32 frameIndex);
    
    /**
     * End the current frame
     */
    void endFrame();
    
    // ========================================================================
    // View Setup
    // ========================================================================
    
    /**
     * Set view matrices from camera
     * @param viewMatrix View matrix (world to camera)
     * @param projectionMatrix Projection matrix
     * @param cameraPosition Camera world position
     */
    void setViewMatrices(const Math::FMatrix& viewMatrix,
                         const Math::FMatrix& projectionMatrix,
                         const Math::FVector& cameraPosition);
    
    /**
     * Set viewport dimensions
     */
    void setViewport(uint32 width, uint32 height);
    
    /**
     * Set time parameters
     */
    void setTime(float time, float deltaTime);
    
    // ========================================================================
    // Lighting Setup
    // ========================================================================
    
    /**
     * Set directional light
     * @param direction Light direction (pointing away from light source)
     * @param color Light color (linear)
     * @param intensity Light intensity multiplier
     */
    void setDirectionalLight(const Math::FVector& direction,
                             const Math::FVector& color,
                             float intensity = 1.0f);
    
    /**
     * Set ambient light
     * @param color Ambient color (linear)
     * @param intensity Ambient intensity multiplier
     */
    void setAmbientLight(const Math::FVector& color, float intensity = 1.0f);
    
    /**
     * Set exposure for tone mapping
     */
    void setExposure(float exposure);
    
    // ========================================================================
    // Rendering
    // ========================================================================
    
    /**
     * Update per-frame uniform buffers
     * Call this after setting view and lighting, before drawing
     */
    void updatePerFrameBuffers();
    
    /**
     * Bind per-frame descriptor set
     * @param cmdList Command list to record binding
     */
    void bindPerFrameDescriptorSet(MonsterRender::RHI::IRHICommandList* cmdList);
    
    /**
     * Draw an object with PBR material
     * @param cmdList Command list to record draw
     * @param material PBR material to use
     * @param modelMatrix Object's model matrix
     * @param vertexBuffer Vertex buffer
     * @param indexBuffer Index buffer (optional)
     * @param vertexCount Number of vertices
     * @param indexCount Number of indices (0 for non-indexed)
     */
    void drawObject(MonsterRender::RHI::IRHICommandList* cmdList,
                    FPBRMaterial* material,
                    const Math::FMatrix& modelMatrix,
                    MonsterRender::RHI::IRHIBuffer* vertexBuffer,
                    MonsterRender::RHI::IRHIBuffer* indexBuffer,
                    uint32 vertexCount,
                    uint32 indexCount = 0);
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    /** Get descriptor set manager */
    FPBRDescriptorSetManager* getDescriptorSetManager() { return &m_descriptorSetManager; }
    
    /** Get current render context */
    const FPBRRenderContext& getRenderContext() const { return m_context; }
    
    /** Get per-frame descriptor set */
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSet> getPerFrameDescriptorSet() const 
    { return m_perFrameDescriptorSet; }
    
    /** Get per-object descriptor set */
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSet> getPerObjectDescriptorSet() const 
    { return m_perObjectDescriptorSet; }
    
private:
    // Device reference
    MonsterRender::RHI::IRHIDevice* m_device = nullptr;
    
    // Descriptor set manager
    FPBRDescriptorSetManager m_descriptorSetManager;
    
    // Render context
    FPBRRenderContext m_context;
    
    // Per-frame uniform buffers
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> m_viewUniformBuffer;
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> m_lightUniformBuffer;
    
    // Per-object uniform buffer
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> m_objectUniformBuffer;
    FObjectUniformBuffer m_objectData;
    
    // Descriptor sets
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSet> m_perFrameDescriptorSet;
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSet> m_perObjectDescriptorSet;
    
    // State
    bool m_initialized = false;
    bool m_perFrameDirty = true;
    
    /**
     * Create per-frame uniform buffers
     */
    bool _createPerFrameBuffers();
    
    /**
     * Create per-object uniform buffer
     */
    bool _createPerObjectBuffer();
    
    /**
     * Create descriptor sets
     */
    bool _createDescriptorSets();
    
    /**
     * Update per-object uniform buffer
     */
    void _updateObjectBuffer(const Math::FMatrix& modelMatrix);
};

} // namespace Renderer
} // namespace MonsterEngine
