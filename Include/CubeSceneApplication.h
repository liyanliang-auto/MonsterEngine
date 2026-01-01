// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CubeSceneApplication.h
 * @brief Demo application showing rotating cube with scene, camera, and lighting systems
 * 
 * This application demonstrates the integration of:
 * - Scene system (FScene)
 * - Camera system (FCameraManager)
 * - Actor-Component pattern (ACubeActor, UCubeMeshComponent)
 * - Forward rendering pipeline (FForwardShadingRenderer)
 * - Lighting system (UDirectionalLightComponent, UPointLightComponent)
 */

#include "Core/Application.h"
#include "Core/Templates/SharedPointer.h"
#include "RHI/RHI.h"
#include "Math/MonsterMath.h"
#include "Containers/Array.h"

namespace MonsterEngine
{

// Forward declarations
class FLightSceneInfo;
class FScene;
class FCameraManager;
class FFPSCameraController;
class ACubeActor;
class AFloorActor;
class UDirectionalLightComponent;
class UPointLightComponent;
class FSceneViewFamily;
class FSceneView;
class FMaterial;
class FMaterialInstance;

} // namespace MonsterEngine

// Forward declarations for Renderer namespace (in MonsterEngine)
namespace MonsterEngine { namespace Renderer {
    class FSceneRenderer;
    class FForwardShadingSceneRenderer;
    class FScene;
    struct FSceneViewFamily;
    struct FViewInfo;
}}

// Forward declarations for Editor namespace (ImGui)
namespace MonsterEngine { namespace Editor {
    class FImGuiContext;
    class FImGuiRenderer;
    class FImGuiInputHandler;
}}

namespace MonsterRender
{

/**
 * Demo application for rotating lit cube
 * 
 * Demonstrates:
 * - Creating a scene with actors and components
 * - Using the camera manager for view control
 * - Adding lights to the scene
 * - Rendering with the forward pipeline
 */
class CubeSceneApplication : public Application
{
public:
    CubeSceneApplication();
    virtual ~CubeSceneApplication();

    // Application interface
    virtual void onInitialize() override;
    virtual void onUpdate(float32 deltaTime) override;
    virtual void onRender() override;
    virtual void onShutdown() override;
    virtual void onWindowResize(uint32 width, uint32 height) override;

    // Input events for ImGui
    virtual void onKeyPressed(EKey key) override;
    virtual void onKeyReleased(EKey key) override;
    virtual void onMouseButtonPressed(EKey button, const MousePosition& position) override;
    virtual void onMouseButtonReleased(EKey button, const MousePosition& position) override;
    virtual void onMouseMoved(const MousePosition& position) override;
    virtual void onMouseScrolled(float64 xOffset, float64 yOffset) override;

    /**
     * Set the window dimensions
     * @param Width - Window width
     * @param Height - Window height
     */
    void setWindowDimensions(uint32 Width, uint32 Height);

protected:
    /**
     * Initialize the scene with actors and lights
     * @return True if successful
     */
    bool initializeScene();

    /**
     * Initialize the camera manager
     * @return True if successful
     */
    bool initializeCamera();

    /**
     * Initialize the renderer
     * @return True if successful
     */
    bool initializeRenderer();

    /**
     * Update camera based on input
     * @param DeltaTime - Time since last frame
     */
    void updateCamera(float DeltaTime);

    /**
     * Render the cube with lighting (legacy path)
     * @param cmdList - Command list to record commands
     * @param viewMatrix - View matrix
     * @param projectionMatrix - Projection matrix
     * @param cameraPosition - Camera position for lighting
     * @param lights - Array of affecting lights
     */
    void renderCube(
        RHI::IRHICommandList* cmdList,
        const MonsterEngine::Math::FMatrix& viewMatrix,
        const MonsterEngine::Math::FMatrix& projectionMatrix,
        const MonsterEngine::Math::FVector& cameraPosition,
        const MonsterEngine::TArray<MonsterEngine::FLightSceneInfo*>& lights);

    /**
     * Render using FSceneRenderer (UE5-style rendering pipeline)
     * @param cmdList - Command list to record commands
     * @param viewMatrix - View matrix
     * @param projectionMatrix - Projection matrix
     * @param cameraPosition - Camera position
     */
    void renderWithSceneRenderer(
        RHI::IRHICommandList* cmdList,
        const MonsterEngine::Math::FMatrix& viewMatrix,
        const MonsterEngine::Math::FMatrix& projectionMatrix,
        const MonsterEngine::Math::FVector& cameraPosition);

    /**
     * Initialize the scene renderer
     * @return True if successful
     */
    bool initializeSceneRenderer();

    /**
     * Initialize ImGui for UI rendering
     * @return True if successful
     */
    bool initializeImGui();

    /**
     * Shutdown ImGui
     */
    void shutdownImGui();

    /**
     * Render ImGui interface
     */
    void renderImGui();

    /**
     * Render the scene info panel
     */
    void renderSceneInfoPanel();

    /**
     * Render the camera control panel
     */
    void renderCameraControlPanel();

    /**
     * Render the lighting control panel
     */
    void renderLightingControlPanel();

    /**
     * Render the 3D viewport panel
     */
    void renderViewportPanel();

    /**
     * Initialize render target texture for viewport
     * @return True if successful
     */
    bool initializeViewportRenderTarget();

    /**
     * Resize viewport render target
     * @param Width New width
     * @param Height New height
     */
    void resizeViewportRenderTarget(uint32 Width, uint32 Height);

    /**
     * Render scene to viewport render target
     * @param cmdList Command list
     * @param viewMatrix View matrix
     * @param projectionMatrix Projection matrix
     * @param cameraPosition Camera position
     */
    void renderSceneToViewport(
        RHI::IRHICommandList* cmdList,
        const MonsterEngine::Math::FMatrix& viewMatrix,
        const MonsterEngine::Math::FMatrix& projectionMatrix,
        const MonsterEngine::Math::FVector& cameraPosition);

    /**
     * Initialize shadow map resources
     * @return True if successful
     */
    bool initializeShadowMap();

    /**
     * Initialize floor geometry and vertex buffer
     * @return True if successful
     */
    bool initializeFloor();

    /**
     * Load wood texture for floor rendering
     * @return True if successful
     */
    bool loadWoodTexture();

    /**
     * Render shadow depth pass
     * @param cmdList Command list
     * @param lightDirection Direction of the directional light
     * @param outLightViewProjection Output light view-projection matrix
     */
    void renderShadowDepthPass(
        RHI::IRHICommandList* cmdList,
        const MonsterEngine::Math::FVector& lightDirection,
        MonsterEngine::Math::FMatrix& outLightViewProjection);

    /**
     * Render cube with shadows
     * @param cmdList Command list
     * @param viewMatrix View matrix
     * @param projectionMatrix Projection matrix
     * @param cameraPosition Camera position
     * @param lights Affecting lights
     * @param lightViewProjection Light space VP matrix
     */
    void renderCubeWithShadows(
        RHI::IRHICommandList* cmdList,
        const MonsterEngine::Math::FMatrix& viewMatrix,
        const MonsterEngine::Math::FMatrix& projectionMatrix,
        const MonsterEngine::Math::FVector& cameraPosition,
        const MonsterEngine::TArray<MonsterEngine::FLightSceneInfo*>& lights,
        const MonsterEngine::Math::FMatrix& lightViewProjection);

    /**
     * Calculate light view-projection matrix for shadow mapping
     * @param lightDirection Direction of the light
     * @param sceneBounds Scene bounding sphere
     * @return Light view-projection matrix
     */
    MonsterEngine::Math::FMatrix calculateLightViewProjection(
        const MonsterEngine::Math::FVector& lightDirection,
        float sceneBoundsRadius);

    /**
     * Render scene using RDG (Render Dependency Graph)
     * This demonstrates the RDG system with Shadow Depth Pass and Main Render Pass
     * @param cmdList Command list
     * @param viewMatrix View matrix
     * @param projectionMatrix Projection matrix
     * @param cameraPosition Camera position
     */
    void renderWithRDG(
        RHI::IRHICommandList* cmdList,
        const MonsterEngine::Math::FMatrix& viewMatrix,
        const MonsterEngine::Math::FMatrix& projectionMatrix,
        const MonsterEngine::Math::FVector& cameraPosition);

protected:
    /** RHI device */
    RHI::IRHIDevice* m_device;

    /** Scene containing all actors */
    MonsterEngine::FScene* m_scene;

    /** Camera manager */
    MonsterEngine::FCameraManager* m_cameraManager;

    /** Cube actor */
    MonsterEngine::ACubeActor* m_cubeActor;

    /** Floor actor */
    MonsterEngine::AFloorActor* m_floorActor;

    /** Directional light component */
    MonsterEngine::UDirectionalLightComponent* m_directionalLight;

    /** Point light component */
    MonsterEngine::UPointLightComponent* m_pointLight;

    /** Scene view family (Engine namespace - for camera) */
    MonsterEngine::FSceneViewFamily* m_viewFamily;

    /** Scene view for rendering (Engine namespace) */
    MonsterEngine::FSceneView* m_sceneView;

    /** Renderer view family (Renderer namespace - for FSceneRenderer) */
    MonsterEngine::Renderer::FSceneViewFamily* m_rendererViewFamily = nullptr;

    /** Scene renderer (UE5-style) */
    MonsterEngine::Renderer::FSceneRenderer* m_sceneRenderer = nullptr;

    /** Flag to use FSceneRenderer for rendering */
    bool m_bUseSceneRenderer;

    /** Cube material */
    MonsterEngine::TSharedPtr<MonsterEngine::FMaterial> m_cubeMaterial;

    /** Window dimensions */
    uint32 m_windowWidth;
    uint32 m_windowHeight;

    /** Total elapsed time */
    float m_totalTime;

    /** Camera orbit angle */
    float m_cameraOrbitAngle;

    /** Whether to orbit camera */
    bool m_bOrbitCamera;

    // ========================================================================
    // FPS Camera Controller
    // ========================================================================

    /** FPS camera controller for WASD + mouse look */
    MonsterEngine::TUniquePtr<MonsterEngine::FFPSCameraController> m_fpsCameraController;

    /** Whether FPS camera mode is enabled */
    bool m_bFPSCameraEnabled;

    /** Whether mouse look is active (right mouse button held) */
    bool m_bMouseLookActive;

    /** Last mouse position for delta calculation */
    float m_lastMouseX;
    float m_lastMouseY;

    /** First mouse input flag */
    bool m_bFirstMouseInput;

    // ========================================================================
    // ImGui Members
    // ========================================================================

    /** ImGui context */
    MonsterEngine::TUniquePtr<MonsterEngine::Editor::FImGuiContext> m_imguiContext;

    /** ImGui renderer */
    MonsterEngine::TUniquePtr<MonsterEngine::Editor::FImGuiRenderer> m_imguiRenderer;

    /** ImGui input handler */
    MonsterEngine::TUniquePtr<MonsterEngine::Editor::FImGuiInputHandler> m_imguiInputHandler;

    /** Whether ImGui is initialized */
    bool m_bImGuiInitialized;

    /** Delta time for ImGui */
    float m_deltaTime;

    // ========================================================================
    // UI State
    // ========================================================================

    /** Show scene info panel */
    bool m_bShowSceneInfo;

    /** Show camera control panel */
    bool m_bShowCameraControl;

    /** Show lighting control panel */
    bool m_bShowLightingControl;

    /** Show ImGui demo window */
    bool m_bShowDemoWindow;

    /** Cube rotation speed */
    float m_cubeRotationSpeed;

    /** Light intensity */
    float m_lightIntensity;

    /** Light color */
    float m_lightColor[3];

    // ========================================================================
    // Shadow Mapping
    // ========================================================================

    /** Shadow map depth texture */
    MonsterEngine::TSharedPtr<RHI::IRHITexture> m_shadowMapTexture;

    /** Shadow map resolution */
    uint32 m_shadowMapResolution = 1024;

    /** Shadow depth bias */
    float m_shadowDepthBias = 0.005f;

    /** Shadow slope bias */
    float m_shadowSlopeBias = 0.01f;

    /** Shadow normal bias */
    float m_shadowNormalBias = 0.02f;

    /** Shadow distance */
    float m_shadowDistance = 50.0f;

    /** Whether shadows are enabled */
    bool m_bShadowsEnabled = true;

    /** Whether to use RDG for rendering */
    bool m_bUseRDG = true;  // Temporarily disabled for debugging

    // ========================================================================
    // Viewport Render Target
    // ========================================================================

    /** Viewport render target texture (color) */
    MonsterEngine::TSharedPtr<RHI::IRHITexture> m_viewportColorTarget;

    /** Viewport depth target texture */
    MonsterEngine::TSharedPtr<RHI::IRHITexture> m_viewportDepthTarget;

    /** ImGui texture ID for viewport */
    uint64 m_viewportTextureID;

    /** Viewport dimensions */
    uint32 m_viewportWidth;
    uint32 m_viewportHeight;

    /** Show viewport panel */
    bool m_bShowViewport;

    /** Viewport needs resize */
    bool m_bViewportNeedsResize;

    /** New viewport size (pending resize) */
    uint32 m_pendingViewportWidth;
    uint32 m_pendingViewportHeight;

    /** Viewport texture is ready for display (has been rendered to at least once) */
    bool m_bViewportTextureReady;

    // ========================================================================
    // Floor Rendering
    // ========================================================================

    /** Floor vertex buffer */
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_floorVertexBuffer;

    /** Number of floor vertices */
    uint32 m_floorVertexCount = 6;

    /** Wood floor texture */
    MonsterEngine::TSharedPtr<RHI::IRHITexture> m_woodTexture;

    /** Wood texture sampler */
    MonsterEngine::TSharedPtr<RHI::IRHISampler> m_woodSampler;
};

} // namespace MonsterRender
