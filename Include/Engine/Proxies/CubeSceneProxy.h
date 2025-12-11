// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CubeSceneProxy.h
 * @brief Scene proxy for rendering cubes with lighting
 * 
 * FCubeSceneProxy is the rendering thread's representation of UCubeMeshComponent.
 * It manages GPU resources and handles drawing with lighting support.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/StaticMeshRender.cpp
 */

#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Components/CubeMeshComponent.h"
#include "Core/Templates/SharedPointer.h"
#include "RHI/RHI.h"

namespace MonsterEngine
{

// Forward declarations
class FLightSceneInfo;
class FRenderPassContext;

/**
 * Uniform buffer structure for cube rendering with lighting
 * Aligned to 16 bytes for GPU compatibility
 */
struct alignas(16) FCubeLitUniformBuffer
{
    float Model[16];           // Model matrix (4x4)
    float View[16];            // View matrix (4x4)
    float Projection[16];      // Projection matrix (4x4)
    float NormalMatrix[16];    // Normal transformation matrix (4x4)
    float CameraPosition[4];   // Camera world position (xyz) + padding
    float TextureBlend[4];     // Texture blend factor (x) + padding
};

/**
 * Light data structure for shader
 */
struct alignas(16) FCubeLightData
{
    float Position[4];         // Light position (xyz) or direction for directional + type (w)
    float Color[4];            // Light color (rgb) + intensity (a)
    float Params[4];           // Radius, spot angle, etc.
};

/**
 * Light uniform buffer for cube rendering
 */
struct alignas(16) FCubeLightUniformBuffer
{
    FCubeLightData Lights[8];  // Up to 8 lights
    float AmbientColor[4];     // Ambient light color (rgb) + padding
    int32 NumLights;           // Number of active lights
    float Padding[3];          // Padding to 16-byte alignment
};

/**
 * Scene proxy for cube mesh rendering
 * 
 * This proxy:
 * - Creates and manages GPU resources (vertex buffer, uniform buffers, pipeline)
 * - Handles drawing with lighting calculations
 * - Supports multiple lights per object
 * - Thread-safe for rendering
 */
class FCubeSceneProxy : public FPrimitiveSceneProxy
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /**
     * Constructor
     * @param InComponent - The cube mesh component this proxy represents
     */
    explicit FCubeSceneProxy(const UCubeMeshComponent* InComponent);

    /** Destructor */
    virtual ~FCubeSceneProxy();

    // ========================================================================
    // FPrimitiveSceneProxy Interface
    // ========================================================================

    /**
     * Get the type hash for this proxy
     * @return Type hash
     */
    virtual SIZE_T GetTypeHash() const override;

    /**
     * Check if this proxy should be rendered
     * @return True if should render
     */
    bool IsVisible() const { return bVisible; }

    // ========================================================================
    // Rendering
    // ========================================================================

    /**
     * Initialize GPU resources
     * @param Device - RHI device to use
     * @return True if successful
     */
    bool InitializeResources(MonsterRender::RHI::IRHIDevice* Device);

    /**
     * Check if resources are initialized
     * @return True if initialized
     */
    bool AreResourcesInitialized() const { return bResourcesInitialized; }

    /**
     * Draw the cube
     * @param CmdList - Command list to record draw commands
     * @param ViewMatrix - View matrix
     * @param ProjectionMatrix - Projection matrix
     * @param CameraPosition - Camera world position
     */
    void Draw(
        MonsterRender::RHI::IRHICommandList* CmdList,
        const FMatrix& ViewMatrix,
        const FMatrix& ProjectionMatrix,
        const FVector& CameraPosition);

    /**
     * Draw the cube with lighting
     * @param CmdList - Command list to record draw commands
     * @param ViewMatrix - View matrix
     * @param ProjectionMatrix - Projection matrix
     * @param CameraPosition - Camera world position
     * @param AffectingLights - Lights affecting this primitive
     */
    void DrawWithLighting(
        MonsterRender::RHI::IRHICommandList* CmdList,
        const FMatrix& ViewMatrix,
        const FMatrix& ProjectionMatrix,
        const FVector& CameraPosition,
        const TArray<FLightSceneInfo*>& AffectingLights);

    /**
     * Update the model matrix (called when transform changes)
     * @param NewLocalToWorld - New local to world transform
     */
    void UpdateModelMatrix(const FMatrix& NewLocalToWorld);

    // ========================================================================
    // Resource Access
    // ========================================================================

    /** Get vertex buffer */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> GetVertexBuffer() const { return VertexBuffer; }

    /** Get pipeline state */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> GetPipelineState() const { return PipelineState; }

    /** Get first texture */
    TSharedPtr<MonsterRender::RHI::IRHITexture> GetTexture1() const { return Texture1; }

    /** Get second texture */
    TSharedPtr<MonsterRender::RHI::IRHITexture> GetTexture2() const { return Texture2; }

    // ========================================================================
    // Settings
    // ========================================================================

    /** Set visibility */
    void SetVisible(bool bInVisible) { bVisible = bInVisible; }

    /** Set texture blend factor */
    void SetTextureBlendFactor(float Factor) { TextureBlendFactor = Factor; }

    /** Get texture blend factor */
    float GetTextureBlendFactor() const { return TextureBlendFactor; }

protected:
    /**
     * Create vertex buffer with cube geometry
     * @return True if successful
     */
    bool CreateVertexBuffer();

    /**
     * Create uniform buffers
     * @return True if successful
     */
    bool CreateUniformBuffers();

    /**
     * Create shaders
     * @return True if successful
     */
    bool CreateShaders();

    /**
     * Create pipeline state
     * @return True if successful
     */
    bool CreatePipelineState();

    /**
     * Load or create textures
     * @return True if successful
     */
    bool LoadTextures();

    /**
     * Update transform uniform buffer
     * @param ViewMatrix - View matrix
     * @param ProjectionMatrix - Projection matrix
     * @param CameraPosition - Camera world position
     */
    void UpdateTransformBuffer(
        const FMatrix& ViewMatrix,
        const FMatrix& ProjectionMatrix,
        const FVector& CameraPosition);

    /**
     * Update light uniform buffer
     * @param Lights - Lights to setup
     */
    void UpdateLightBuffer(const TArray<FLightSceneInfo*>& Lights);

    /**
     * Convert FMatrix to float array (column-major for GPU)
     * @param Matrix - Source matrix
     * @param OutArray - Destination array (16 floats)
     */
    static void MatrixToFloatArray(const FMatrix& Matrix, float* OutArray);

protected:
    /** RHI device */
    MonsterRender::RHI::IRHIDevice* Device;

    /** RHI backend type */
    MonsterRender::RHI::ERHIBackend RHIBackend;

    /** Vertex buffer */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> VertexBuffer;

    /** Transform uniform buffer */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> TransformUniformBuffer;

    /** Light uniform buffer */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> LightUniformBuffer;

    /** First texture */
    TSharedPtr<MonsterRender::RHI::IRHITexture> Texture1;

    /** Second texture */
    TSharedPtr<MonsterRender::RHI::IRHITexture> Texture2;

    /** Texture sampler */
    TSharedPtr<MonsterRender::RHI::IRHISampler> Sampler;

    /** Vertex shader */
    TSharedPtr<MonsterRender::RHI::IRHIVertexShader> VertexShader;

    /** Pixel shader */
    TSharedPtr<MonsterRender::RHI::IRHIPixelShader> PixelShader;

    /** Pipeline state */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> PipelineState;

    /** Texture blend factor */
    float TextureBlendFactor;

    /** Cube half-extent */
    float CubeSize;

    /** Whether resources are initialized */
    uint8 bResourcesInitialized : 1;

    /** Whether the proxy is visible */
    uint8 bVisible : 1;
};

} // namespace MonsterEngine
