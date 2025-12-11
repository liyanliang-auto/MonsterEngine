// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CubeSceneRendererTest.h
 * @brief Integration test for cube rendering using engine render pipeline
 * 
 * This test demonstrates how to use the engine's rendering architecture:
 * - FScene for scene management
 * - FSceneRenderer for rendering orchestration
 * - FMeshDrawCommand for draw call management
 * - FRenderQueue for draw call organization
 * 
 * Reference: UE5 rendering pipeline integration
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Core/Templates/SharedPointer.h"
#include "RHI/RHI.h"

// Forward declarations
namespace MonsterRender
{
namespace RHI
{
    class IRHIDevice;
    class IRHICommandList;
    class IRHIBuffer;
    class IRHITexture;
    class IRHISampler;
    class IRHIVertexShader;
    class IRHIPixelShader;
    class IRHIPipelineState;
}
}

namespace MonsterEngine
{

// Forward declarations from Renderer namespace
namespace Renderer
{
    class FScene;
    class FSceneRenderer;
    class FViewInfo;
    class FPrimitiveSceneProxy;
    class FPrimitiveSceneInfo;
    class FRenderQueue;
    struct FSceneViewFamily;
    struct FMeshBatch;
}

/**
 * @class FCubeSceneRendererTest
 * @brief Integration test class for cube rendering with engine pipeline
 * 
 * This class demonstrates the proper integration of:
 * 1. Scene management (FScene)
 * 2. Primitive registration (FPrimitiveSceneProxy, FPrimitiveSceneInfo)
 * 3. View setup (FViewInfo, FSceneViewFamily)
 * 4. Visibility culling (frustum, distance, occlusion)
 * 5. Draw command generation (FMeshDrawCommand)
 * 6. Render queue execution (FRenderQueue)
 */
class FCubeSceneRendererTest
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    FCubeSceneRendererTest();
    ~FCubeSceneRendererTest();

    // ========================================================================
    // Test Lifecycle
    // ========================================================================

    /**
     * Initialize the test with RHI device
     * @param Device RHI device to use
     * @return True if initialization successful
     */
    bool Initialize(MonsterRender::RHI::IRHIDevice* Device);

    /**
     * Shutdown and cleanup resources
     */
    void Shutdown();

    /**
     * Update the test (called each frame)
     * @param DeltaTime Time since last frame
     */
    void Update(float DeltaTime);

    /**
     * Render the scene using engine pipeline
     * @param CmdList Command list to record commands
     */
    void Render(MonsterRender::RHI::IRHICommandList* CmdList);

    /**
     * Set window dimensions for view setup
     * @param Width Window width
     * @param Height Window height
     */
    void SetWindowDimensions(uint32 Width, uint32 Height);

    /**
     * Run the integration test
     * @return True if test passed
     */
    bool RunTest();

    // ========================================================================
    // Test Configuration
    // ========================================================================

    /**
     * Set number of cubes to render
     * @param Count Number of cubes
     */
    void SetCubeCount(int32 Count) { CubeCount = Count; }

    /**
     * Enable/disable frustum culling test
     * @param bEnable Enable flag
     */
    void SetFrustumCullingEnabled(bool bEnable) { bTestFrustumCulling = bEnable; }

    /**
     * Enable/disable distance culling test
     * @param bEnable Enable flag
     */
    void SetDistanceCullingEnabled(bool bEnable) { bTestDistanceCulling = bEnable; }

    /**
     * Enable/disable render queue test
     * @param bEnable Enable flag
     */
    void SetRenderQueueEnabled(bool bEnable) { bTestRenderQueue = bEnable; }

private:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    /**
     * Create the scene and add primitives
     * @return True if successful
     */
    bool CreateScene();

    /**
     * Create cube primitives and add to scene
     * @return True if successful
     */
    bool CreateCubePrimitives();

    /**
     * Setup view for rendering
     */
    void SetupView();

    /**
     * Perform visibility culling
     */
    void ComputeVisibility();

    /**
     * Generate mesh draw commands
     */
    void GenerateMeshDrawCommands();

    /**
     * Execute render queue (internal implementation)
     * @param CmdList Command list
     */
    void ExecuteRenderQueueInternal(MonsterRender::RHI::IRHICommandList* CmdList);

    /**
     * Create shared GPU resources (vertex buffer, shaders, pipeline)
     * @return True if successful
     */
    bool CreateSharedGPUResources();

    /**
     * Create vertex buffer for cube geometry
     * @return True if successful
     */
    bool CreateVertexBuffer();

    /**
     * Create uniform buffer for MVP matrices
     * @return True if successful
     */
    bool CreateUniformBuffer();

    /**
     * Load textures from disk
     * @return True if successful
     */
    bool LoadTextures();

    /**
     * Create shaders (vertex and pixel)
     * @return True if successful
     */
    bool CreateShaders();

    /**
     * Create pipeline state
     * @return True if successful
     */
    bool CreatePipelineState();

    /**
     * Update uniform buffer with current MVP matrices
     */
    void UpdateUniformBuffer();

    /**
     * Build model matrix for a cube
     * @param CubeIndex Index of the cube
     * @param OutMatrix Output model matrix
     */
    void BuildModelMatrix(int32 CubeIndex, float* OutMatrix);

    /**
     * Build view matrix from camera position
     * @param OutMatrix Output view matrix
     */
    void BuildViewMatrix(Math::FMatrix& OutMatrix);

    /**
     * Build projection matrix
     * @param OutMatrix Output projection matrix
     */
    void BuildProjectionMatrix(Math::FMatrix& OutMatrix);

    /**
     * Log test statistics
     */
    void LogStatistics();

private:
    // ========================================================================
    // Core Components
    // ========================================================================

    /** RHI device */
    MonsterRender::RHI::IRHIDevice* Device;

    /** Scene manager */
    Renderer::FScene* Scene;

    /** Scene renderer */
    Renderer::FSceneRenderer* SceneRenderer;

    /** Render queue */
    Renderer::FRenderQueue* RenderQueue;

    /** View family */
    Renderer::FSceneViewFamily* ViewFamily;

    // ========================================================================
    // GPU Resources (Shared)
    // ========================================================================

    /** Vertex buffer for cube geometry */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> VertexBuffer;

    /** Uniform buffer for MVP matrices */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> UniformBuffer;

    /** Texture 1 (container) */
    TSharedPtr<MonsterRender::RHI::IRHITexture> Texture1;

    /** Texture 2 (awesomeface) */
    TSharedPtr<MonsterRender::RHI::IRHITexture> Texture2;

    /** Texture sampler */
    TSharedPtr<MonsterRender::RHI::IRHISampler> Sampler;

    /** Vertex shader */
    TSharedPtr<MonsterRender::RHI::IRHIVertexShader> VertexShader;

    /** Pixel shader */
    TSharedPtr<MonsterRender::RHI::IRHIPixelShader> PixelShader;

    /** Pipeline state */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> PipelineState;

    /** RHI backend type */
    MonsterRender::RHI::ERHIBackend RHIBackend;

    // ========================================================================
    // Cube Data
    // ========================================================================

    /** Cube primitive proxies */
    TArray<Renderer::FPrimitiveSceneProxy*> CubeProxies;

    /** Cube primitive scene infos */
    TArray<Renderer::FPrimitiveSceneInfo*> CubeSceneInfos;

    /** Cube positions */
    TArray<Math::FVector> CubePositions;

    /** Cube rotations (in radians) */
    TArray<float> CubeRotations;

    // ========================================================================
    // Camera State
    // ========================================================================

    /** Camera position */
    Math::FVector CameraPosition;

    /** Camera forward direction */
    Math::FVector CameraForward;

    /** Camera up direction */
    Math::FVector CameraUp;

    /** Camera right direction */
    Math::FVector CameraRight;

    /** Field of view (degrees) */
    float FieldOfView;

    /** Near clip plane */
    float NearClipPlane;

    /** Far clip plane */
    float FarClipPlane;

    // ========================================================================
    // Window State
    // ========================================================================

    /** Window width */
    uint32 WindowWidth;

    /** Window height */
    uint32 WindowHeight;

    // ========================================================================
    // Animation State
    // ========================================================================

    /** Total elapsed time */
    float TotalTime;

    /** Current rotation angle */
    float RotationAngle;

    // ========================================================================
    // Test Configuration
    // ========================================================================

    /** Number of cubes to render */
    int32 CubeCount;

    /** Test frustum culling */
    uint8 bTestFrustumCulling : 1;

    /** Test distance culling */
    uint8 bTestDistanceCulling : 1;

    /** Test render queue */
    uint8 bTestRenderQueue : 1;

    /** Initialized flag */
    uint8 bInitialized : 1;

    // ========================================================================
    // Statistics
    // ========================================================================

    /** Number of visible primitives after culling */
    int32 NumVisiblePrimitives;

    /** Number of draw calls submitted */
    int32 NumDrawCalls;

    /** Number of triangles rendered */
    int32 NumTriangles;

    /** Time spent in visibility computation (ms) */
    float VisibilityTimeMs;

    /** Time spent in draw command generation (ms) */
    float DrawCommandTimeMs;
};

} // namespace MonsterEngine
