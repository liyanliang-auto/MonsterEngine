// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file FloorSceneProxy.h
 * @brief Scene proxy for rendering floor planes with lighting and shadows
 * 
 * FFloorSceneProxy is the rendering thread's representation of UFloorMeshComponent.
 * It manages GPU resources and handles drawing floor geometry with texture and shadows.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/StaticMeshRender.cpp
 */

#include "Engine/PrimitiveSceneProxy.h"
#include "Core/Templates/SharedPointer.h"
#include "RHI/RHI.h"

namespace MonsterEngine
{

// Forward declarations
class UFloorMeshComponent;
class FLightSceneInfo;

/**
 * Vertex structure for floor rendering
 * Contains position, normal, and texture coordinates
 */
struct FFloorVertex
{
    float Position[3];   // Position (x, y, z)
    float Normal[3];     // Normal (nx, ny, nz)
    float TexCoord[2];   // Texture coordinates (u, v)
};

/**
 * Uniform buffer structure for floor rendering
 * Aligned to 16 bytes for GPU compatibility
 */
struct alignas(16) FFloorUniformBuffer
{
    float Model[16];           // Model matrix (4x4)
    float View[16];            // View matrix (4x4)
    float Projection[16];      // Projection matrix (4x4)
    float NormalMatrix[16];    // Normal transformation matrix (4x4)
    float CameraPosition[4];   // Camera world position (xyz) + padding
};

/**
 * Light data structure for floor shader
 */
struct alignas(16) FFloorLightData
{
    float Position[4];         // Light position (xyz) or direction for directional + type (w)
    float Color[4];            // Light color (rgb) + intensity (a)
    float Params[4];           // Radius, spot angle, etc.
};

/**
 * Light uniform buffer for floor rendering
 */
struct alignas(16) FFloorLightUniformBuffer
{
    FFloorLightData Lights[8]; // Up to 8 lights
    float AmbientColor[4];     // Ambient light color (rgb) + padding
    int32 NumLights;           // Number of active lights
    float Padding[3];          // Padding to 16-byte alignment
};

/**
 * Shadow uniform buffer for floor rendering
 */
struct alignas(16) FFloorShadowUniformBuffer
{
    float LightViewProjection[16];  // Light space VP matrix
    float ShadowParams[4];          // x = bias, y = slope bias, z = normal bias, w = shadow distance
    float ShadowMapSize[4];         // xy = size, zw = 1/size
};

/**
 * Scene proxy for floor mesh rendering
 * 
 * This proxy:
 * - Creates and manages GPU resources (vertex buffer, uniform buffers, pipeline)
 * - Handles drawing with lighting and shadow calculations
 * - Supports texture tiling for large floors
 * - Thread-safe for rendering
 */
class FFloorSceneProxy : public FPrimitiveSceneProxy
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /**
     * Constructor
     * @param InComponent - The floor mesh component this proxy represents
     */
    explicit FFloorSceneProxy(const UFloorMeshComponent* InComponent);

    /** Destructor */
    virtual ~FFloorSceneProxy();

    // ========================================================================
    // FPrimitiveSceneProxy Interface
    // ========================================================================

    /**
     * Get the type hash for this proxy
     * @return Type hash
     */
    virtual SIZE_T GetTypeHash() const override;

    /**
     * Gather dynamic mesh elements for rendering (UE5 style)
     * @param Views - Array of views to render
     * @param ViewFamily - View family information
     * @param VisibilityMap - Visibility flags for each view
     * @param Collector - Mesh element collector to add meshes to
     */
    virtual void GetDynamicMeshElements(
        const TArray<const class FSceneView*>& Views,
        const class FSceneViewFamily& ViewFamily,
        uint32 VisibilityMap,
        class FMeshElementCollector& Collector) const override;

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
     * Draw the floor
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
     * Draw the floor with lighting
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
     * Draw the floor with lighting and shadows
     * @param CmdList - Command list to record draw commands
     * @param ViewMatrix - View matrix
     * @param ProjectionMatrix - Projection matrix
     * @param CameraPosition - Camera world position
     * @param AffectingLights - Lights affecting this primitive
     * @param LightViewProjection - Light space view-projection matrix
     * @param ShadowMap - Shadow depth texture
     * @param ShadowParams - Shadow parameters (bias, slope bias, normal bias, distance)
     */
    void DrawWithShadows(
        MonsterRender::RHI::IRHICommandList* CmdList,
        const FMatrix& ViewMatrix,
        const FMatrix& ProjectionMatrix,
        const FVector& CameraPosition,
        const TArray<FLightSceneInfo*>& AffectingLights,
        const FMatrix& LightViewProjection,
        TSharedPtr<MonsterRender::RHI::IRHITexture> ShadowMap,
        const FVector4& ShadowParams);

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

    /** Get floor texture */
    TSharedPtr<MonsterRender::RHI::IRHITexture> GetTexture() const { return FloorTexture; }

    /** Get vertex count */
    uint32 GetVertexCount() const { return VertexCount; }

    // ========================================================================
    // Settings
    // ========================================================================

    /** Set visibility */
    void SetVisible(bool bInVisible) { bVisible = bInVisible; }

    /** Set floor texture */
    void SetTexture(TSharedPtr<MonsterRender::RHI::IRHITexture> InTexture);

    /** Set texture sampler */
    void SetSampler(TSharedPtr<MonsterRender::RHI::IRHISampler> InSampler);

    /** Get floor size */
    float GetFloorSize() const { return FloorSize; }

    /** Get texture tile factor */
    float GetTextureTile() const { return TextureTile; }

protected:
    /**
     * Create vertex buffer with floor geometry
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
     * Update shadow uniform buffer
     * @param LightViewProjection - Light space VP matrix
     * @param ShadowParams - Shadow parameters
     * @param ShadowMapSize - Shadow map dimensions
     */
    void UpdateShadowBuffer(
        const FMatrix& LightViewProjection,
        const FVector4& ShadowParams,
        uint32 ShadowMapWidth,
        uint32 ShadowMapHeight);

    /**
     * Create shadow-enabled shaders
     * @return True if successful
     */
    bool CreateShadowShaders();

    /**
     * Create shadow-enabled pipeline state
     * @return True if successful
     */
    bool CreateShadowPipelineState();

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

    /** Number of vertices */
    uint32 VertexCount;

    /** Transform uniform buffer */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> TransformUniformBuffer;

    /** Light uniform buffer */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> LightUniformBuffer;

    /** Floor texture */
    TSharedPtr<MonsterRender::RHI::IRHITexture> FloorTexture;

    /** Texture sampler */
    TSharedPtr<MonsterRender::RHI::IRHISampler> Sampler;

    /** Vertex shader */
    TSharedPtr<MonsterRender::RHI::IRHIVertexShader> VertexShader;

    /** Pixel shader */
    TSharedPtr<MonsterRender::RHI::IRHIPixelShader> PixelShader;

    /** Pipeline state */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> PipelineState;

    /** Shadow uniform buffer */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> ShadowUniformBuffer;

    /** Shadow-enabled vertex shader */
    TSharedPtr<MonsterRender::RHI::IRHIVertexShader> ShadowVertexShader;

    /** Shadow-enabled pixel shader */
    TSharedPtr<MonsterRender::RHI::IRHIPixelShader> ShadowPixelShader;

    /** Shadow-enabled pipeline state */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> ShadowPipelineState;

    /** Shadow sampler for comparison */
    TSharedPtr<MonsterRender::RHI::IRHISampler> ShadowSampler;

    /** Floor half-extent */
    float FloorSize;

    /** Texture tile factor */
    float TextureTile;

    /** Whether resources are initialized */
    uint8 bResourcesInitialized : 1;

    /** Whether the proxy is visible */
    uint8 bVisible : 1;
};

} // namespace MonsterEngine
