// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CubeSceneApplication.cpp
 * @brief Implementation of demo application for rotating lit cube
 */

#include "CubeSceneApplication.h"
#include "Engine/Scene.h"
#include "Engine/Actor.h"
#include "Engine/Actors/CubeActor.h"
#include "Engine/Components/CubeMeshComponent.h"
#include "Engine/Components/LightComponent.h"
#include "Engine/Proxies/CubeSceneProxy.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/CameraTypes.h"
#include "Engine/Camera/CameraSceneViewHelper.h"
#include "Engine/SceneView.h"
#include "Engine/SceneRenderer.h"
#include "Engine/LightSceneInfo.h"
#include "Engine/LightSceneProxy.h"
#include "Engine/Material/Material.h"
#include "Engine/Material/MaterialTypes.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/SceneView.h"
#include "Renderer/Scene.h"
#include "Editor/ImGui/ImGuiContext.h"
#include "Editor/ImGui/ImGuiRenderer.h"
#include "Editor/ImGui/ImGuiInputHandler.h"
#include "Core/Logging/LogMacros.h"

// ImGui includes
#include "imgui.h"
#include "Core/Color.h"
#include "Math/MonsterMath.h"
#include "Containers/SparseArray.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanDescriptorSetLayoutCache.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Platform/OpenGL/OpenGLDefinitions.h"
#include "RDG/RDG.h"

namespace MonsterRender
{

using namespace MonsterEngine;

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogCubeSceneApp, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

// Helper function to create config with preferred backend
static ApplicationConfig CreateCubeSceneConfig()
{
    ApplicationConfig config;
    config.name = "CubeScene Application";
    config.preferredBackend = RHI::ERHIBackend::Vulkan;  // Use Vulkan backend
    config.windowProperties.width = 1028;//256/2;
    config.windowProperties.height = 1028;//256/2;
    config.enableValidation = true;
    return config;
}

CubeSceneApplication::CubeSceneApplication()
    : Application(CreateCubeSceneConfig())
    , m_device(nullptr)
    , m_scene(nullptr)
    , m_cameraManager(nullptr)
    , m_cubeActor(nullptr)
    , m_directionalLight(nullptr)
    , m_pointLight(nullptr)
    , m_viewFamily(nullptr)
    , m_sceneView(nullptr)
    , m_sceneRenderer(nullptr)
    , m_bUseSceneRenderer(true)  // Enable FSceneRenderer for UE5-style rendering
    , m_windowWidth(1280)
    , m_windowHeight(720)
    , m_totalTime(0.0f)
    , m_cameraOrbitAngle(0.0f)
    , m_bOrbitCamera(false)
    , m_bImGuiInitialized(false)
    , m_deltaTime(0.0f)
    , m_bShowSceneInfo(true)
    , m_bShowCameraControl(true)
    , m_bShowLightingControl(true)
    , m_bShowDemoWindow(false)
    , m_cubeRotationSpeed(1.0f)
    , m_lightIntensity(3.0f)
    , m_viewportTextureID(0)
    , m_viewportWidth(800)
    , m_viewportHeight(600)
    , m_bShowViewport(true)
    , m_bViewportNeedsResize(false)
    , m_pendingViewportWidth(800)
    , m_pendingViewportHeight(600)
    , m_bViewportTextureReady(false)
{
    m_lightColor[0] = 1.0f;
    m_lightColor[1] = 0.95f;
    m_lightColor[2] = 0.9f;
}

CubeSceneApplication::~CubeSceneApplication()
{
    shutdown();
}

// ============================================================================
// Application Interface
// ============================================================================

void CubeSceneApplication::onInitialize()
{
    MR_LOG(LogCubeSceneApp, Log, "Initializing CubeSceneApplication...");
    
    // Get actual window size from the window system
    if (getWindow())
    {
        m_windowWidth = getWindow()->getWidth();
        m_windowHeight = getWindow()->getHeight();
        MR_LOG(LogCubeSceneApp, Log, "Window size: %ux%u", m_windowWidth, m_windowHeight);
    }
    
    // Get RHI device from engine
    m_device = getEngine() ? getEngine()->getRHIDevice() : nullptr;
    if (!m_device)
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to get RHI device");
        return;
    }
    
    // Initialize scene
    if (!initializeScene())
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to initialize scene");
        return;
    }
    
    // Initialize camera
    if (!initializeCamera())
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to initialize camera");
        return;
    }
    
    // Initialize renderer
    if (!initializeRenderer())
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to initialize renderer");
        return;
    }
    
    // Initialize FSceneRenderer for UE5-style rendering
    if (m_bUseSceneRenderer)
    {
        if (!initializeSceneRenderer())
        {
            MR_LOG(LogCubeSceneApp, Warning, "Failed to initialize FSceneRenderer, falling back to legacy rendering");
            m_bUseSceneRenderer = false;
        }
    }
    
    // Initialize ImGui (DISABLED for cube rendering focus)
    // if (!initializeImGui())
    // {
    //     MR_LOG(LogCubeSceneApp, Warning, "Failed to initialize ImGui, UI will be disabled");
    // }
    
    // Initialize viewport render target
    if (!initializeViewportRenderTarget())
    {
        MR_LOG(LogCubeSceneApp, Warning, "Failed to initialize viewport render target");
    }
    
    // Initialize shadow map
    if (m_bShadowsEnabled)
    {
        if (!initializeShadowMap())
        {
            MR_LOG(LogCubeSceneApp, Warning, "Failed to initialize shadow map, shadows will be disabled");
            m_bShadowsEnabled = false;
        }
    }
    
    MR_LOG(LogCubeSceneApp, Log, "CubeSceneApplication initialized successfully");
}

void CubeSceneApplication::onUpdate(float32 deltaTime)
{
    m_totalTime += deltaTime;
    m_deltaTime = deltaTime;
    
    // Update cube rotation speed from UI
    if (m_cubeActor)
    {
        m_cubeActor->SetRotationSpeed(m_cubeRotationSpeed);
    }
    
    // Update light properties from UI
    if (m_directionalLight)
    {
        m_directionalLight->SetIntensity(m_lightIntensity);
        m_directionalLight->SetLightColor(FLinearColor(m_lightColor[0], m_lightColor[1], m_lightColor[2]));
    }
    
    // Update camera
    updateCamera(deltaTime);
    
    // Update camera manager
    if (m_cameraManager)
    {
        m_cameraManager->UpdateCamera(deltaTime);
    }
    
    // Update cube actor (rotation animation)
    if (m_cubeActor)
    {
        m_cubeActor->Tick(deltaTime);
    }
    
    // Update scene
    if (m_scene)
    {
        // Scene update logic would go here
    }
}

void CubeSceneApplication::onRender()
{
    if (!m_device || !m_scene || !m_cameraManager)
    {
        return;
    }
    
    // Handle pending viewport resize - defer to next frame to avoid using uninitialized textures
    // The resize will happen at the start of the next frame before rendering
    if (m_bViewportNeedsResize)
    {
        // Wait for GPU to finish using old textures before destroying them
        if (m_device->getRHIBackend() == RHI::ERHIBackend::Vulkan)
        {
            auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);
            vulkanDevice->waitForIdle();
        }
        
        resizeViewportRenderTarget(m_pendingViewportWidth, m_pendingViewportHeight);
        m_bViewportNeedsResize = false;
    }
    
    RHI::ERHIBackend backend = m_device->getRHIBackend();
    
    // Get camera view info
    const FMinimalViewInfo& cameraView = m_cameraManager->GetCameraCacheView();
    
    // Calculate view and projection matrices using LookAt
    // Camera looks at origin from its position
    FVector cameraPos = cameraView.Location;
    FVector targetPos = FVector::ZeroVector;  // Look at origin
    FVector upVector = FVector(0.0, 1.0, 0.0);  // Y-up
    
    // DEBUG: Log camera position before MakeLookAt
    MR_LOG(LogCubeSceneApp, Log, "MakeLookAt input: cameraPos=(%.2f, %.2f, %.2f), target=(%.2f, %.2f, %.2f)",
           cameraPos.X, cameraPos.Y, cameraPos.Z, targetPos.X, targetPos.Y, targetPos.Z);
    
    FMatrix viewMatrix = FMatrix::MakeLookAt(cameraPos, targetPos, upVector);
    
    // DEBUG: Log view matrix result
    MR_LOG(LogCubeSceneApp, Log, "ViewMatrix row3 (translation): %.2f, %.2f, %.2f, %.2f",
           viewMatrix.M[3][0], viewMatrix.M[3][1], viewMatrix.M[3][2], viewMatrix.M[3][3]);
    
    FMatrix projectionMatrix = cameraView.CalculateProjectionMatrix();
    
    // Flip Y for Vulkan
    if (backend == RHI::ERHIBackend::Vulkan)
    {
        projectionMatrix.M[1][1] *= -1.0f;
    }
    
    // Get camera position
    FVector cameraPosition = cameraView.Location;
    
    // Gather lights from scene
    TArray<FLightSceneInfo*> lights;
    if (m_scene)
    {
        const TSparseArray<FLightSceneInfoCompact>& sceneLights = m_scene->GetLights();
        for (auto It = sceneLights.CreateConstIterator(); It; ++It)
        {
            const FLightSceneInfoCompact& lightCompact = *It;
            if (lightCompact.LightSceneInfo)
            {
                lights.Add(lightCompact.LightSceneInfo);
            }
        }
    }
    
    if (backend == RHI::ERHIBackend::OpenGL)
    {
        // OpenGL rendering path
        using namespace MonsterEngine::OpenGL;
        
        // Context should already be current from GLFW initialization
        // No need to explicitly make it current here
        
        // Get command list
        RHI::IRHICommandList* cmdList = m_device->getImmediateCommandList();
        if (!cmdList)
        {
            return;
        }
        
        // ================================================================
        // Shadow Depth Pass (if shadows enabled)
        // ================================================================
        FMatrix lightViewProjection = FMatrix::Identity;
        
        if (m_bShadowsEnabled && m_shadowMapTexture && lights.Num() > 0)
        {
            // Get directional light direction from first light
            FVector lightDirection = FVector(0.5, -1.0, 0.3).GetSafeNormal();
            
            // Check if first light is directional and get its direction
            if (lights[0] && lights[0]->Proxy)
            {
                FLightSceneProxy* lightProxy = lights[0]->Proxy;
                if (lightProxy->IsDirectionalLight())
                {
                    lightDirection = lightProxy->GetDirection();
                }
            }
            
            MR_LOG(LogCubeSceneApp, Verbose, "OpenGL: Rendering shadow pass, light dir: (%.2f, %.2f, %.2f)",
                   lightDirection.X, lightDirection.Y, lightDirection.Z);
            
            // Render shadow depth pass
            renderShadowDepthPass(cmdList, lightDirection, lightViewProjection);
        }
        
        // ================================================================
        // Main Render Pass
        // ================================================================
        // Bind default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        // Set viewport
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        
        // Clear screen
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Enable depth testing
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);
        
        // Render cube with or without shadows
        if (m_bShadowsEnabled && m_shadowMapTexture)
        {
            MR_LOG(LogCubeSceneApp, Verbose, "OpenGL: Rendering cube with shadows");
            renderCubeWithShadows(cmdList, viewMatrix, projectionMatrix, cameraPosition, lights, lightViewProjection);
        }
        else
        {
            MR_LOG(LogCubeSceneApp, Verbose, "OpenGL: Rendering cube without shadows");
            renderCube(cmdList, viewMatrix, projectionMatrix, cameraPosition, lights);
        }
        
        // Swap buffers
        getWindow()->swapBuffers();
    }
    else
    {
        // Vulkan rendering path - Direct to swapchain (no ImGui RTT)
        auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);
        auto* context = vulkanDevice->getCommandListContext();
        if (!context) return;
        
        RHI::IRHICommandList* cmdList = m_device->getImmediateCommandList();
        if (!cmdList) return;
        
        // Prepare for new frame
        context->prepareForNewFrame();
        
        // Begin command recording
        cmdList->begin();
        
        // ================================================================
        // Choose rendering path: RDG or traditional
        // ================================================================
        if (m_bUseRDG && m_bShadowsEnabled)
        {
            MR_LOG(LogCubeSceneApp, Log, "Using RDG rendering path");
            renderWithRDG(cmdList, viewMatrix, projectionMatrix, cameraPosition);
        }
        else
        {
            MR_LOG(LogCubeSceneApp, Log, "Using traditional rendering path");
            
            // ================================================================
            // Shadow Depth Pass (if shadows enabled)
            // ================================================================
            FMatrix lightViewProjection = FMatrix::Identity;
            
            if (m_bShadowsEnabled && m_shadowMapTexture && lights.Num() > 0)
            {
                // Get directional light direction from first light
                FVector lightDirection = FVector(0.5, -1.0, 0.3).GetSafeNormal();
                
                // Check if first light is directional and get its direction
                if (lights[0] && lights[0]->Proxy)
                {
                    FLightSceneProxy* lightProxy = lights[0]->Proxy;
                    if (lightProxy->IsDirectionalLight())
                    {
                        lightDirection = lightProxy->GetDirection();
                    }
                }
                
                MR_LOG(LogCubeSceneApp, Verbose, "Rendering shadow pass, light dir: (%.2f, %.2f, %.2f)",
                       lightDirection.X, lightDirection.Y, lightDirection.Z);
                
                // Render shadow depth pass
                renderShadowDepthPass(cmdList, lightDirection, lightViewProjection);
            }
            
            // ================================================================
            // Main Render Pass - Render scene to swapchain
            // ================================================================
            // Set render targets to swapchain (empty array = use swapchain)
            TArray<TSharedPtr<RHI::IRHITexture>> renderTargets;
            cmdList->setRenderTargets(TSpan<TSharedPtr<RHI::IRHITexture>>(renderTargets), nullptr);
            
            // Set viewport to window size
            RHI::Viewport viewport;
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(m_windowWidth);
            viewport.height = static_cast<float>(m_windowHeight);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            cmdList->setViewport(viewport);
            
            // Set scissor rect
            RHI::ScissorRect scissor;
            scissor.left = 0;
            scissor.top = 0;
            scissor.right = m_windowWidth;
            scissor.bottom = m_windowHeight;
            cmdList->setScissorRect(scissor);
            
            // Render cube with or without shadows
            if (m_bShadowsEnabled && m_shadowMapTexture)
            {
                MR_LOG(LogCubeSceneApp, Verbose, "Rendering cube with shadows");
                renderCubeWithShadows(cmdList, viewMatrix, projectionMatrix, cameraPosition, lights, lightViewProjection);
            }
            else
            {
                MR_LOG(LogCubeSceneApp, Verbose, "Rendering cube without shadows");
                renderCube(cmdList, viewMatrix, projectionMatrix, cameraPosition, lights);
            }
        }
        
        // End render pass
        cmdList->endRenderPass();
        
        // End command recording
        cmdList->end();
        
        // Present frame
        m_device->present();
    }
}

void CubeSceneApplication::renderCube(
    RHI::IRHICommandList* cmdList,
    const FMatrix& viewMatrix,
    const FMatrix& projectionMatrix,
    const FVector& cameraPosition,
    const TArray<FLightSceneInfo*>& lights)
{
    if (!m_cubeActor) return;
    
    UCubeMeshComponent* meshComp = m_cubeActor->GetCubeMeshComponent();
    if (!meshComp) return;
    
    // Ensure component transform is up to date before getting it
    meshComp->UpdateComponentToWorld();
    
    FPrimitiveSceneProxy* baseProxy = meshComp->GetSceneProxy();
    FCubeSceneProxy* cubeProxy = static_cast<FCubeSceneProxy*>(baseProxy);
    
    if (cubeProxy)
    {
        // Initialize resources if needed
        if (!cubeProxy->AreResourcesInitialized())
        {
            cubeProxy->InitializeResources(m_device);
        }
        
        // Update model matrix from actor transform
        cubeProxy->UpdateModelMatrix(m_cubeActor->GetActorTransform().ToMatrixWithScale());
        
        // Draw with lighting
        cubeProxy->DrawWithLighting(cmdList, viewMatrix, projectionMatrix, cameraPosition, lights);
    }
}

// ============================================================================
// FSceneRenderer Rendering Path
// ============================================================================

void CubeSceneApplication::renderWithSceneRenderer(
    RHI::IRHICommandList* cmdList,
    const FMatrix& viewMatrix,
    const FMatrix& projectionMatrix,
    const FVector& cameraPosition)
{
    if (!m_sceneRenderer || !m_scene || !cmdList)
    {
        MR_LOG(LogCubeSceneApp, Warning, "renderWithSceneRenderer: Missing renderer, scene, or command list");
        // Fall back to legacy path
        TArray<FLightSceneInfo*> lights;
        if (m_scene)
        {
            const TSparseArray<FLightSceneInfoCompact>& sceneLights = m_scene->GetLights();
            for (auto It = sceneLights.CreateConstIterator(); It; ++It)
            {
                if (It->LightSceneInfo)
                {
                    lights.Add(It->LightSceneInfo);
                }
            }
        }
        renderCube(cmdList, viewMatrix, projectionMatrix, cameraPosition, lights);
        return;
    }
    
    MR_LOG(LogCubeSceneApp, Verbose, "Rendering with FSceneRenderer...");
    
    // Update renderer view family frame number
    if (m_rendererViewFamily)
    {
        m_rendererViewFamily->FrameNumber++;
        m_rendererViewFamily->DeltaWorldTimeSeconds = 0.016f;  // ~60fps
    }
    
    // Ensure cube resources are initialized and update transform
    if (m_cubeActor)
    {
        UCubeMeshComponent* meshComp = m_cubeActor->GetCubeMeshComponent();
        if (meshComp)
        {
            // Ensure component transform is up to date
            meshComp->UpdateComponentToWorld();
            
            FPrimitiveSceneProxy* baseProxy = meshComp->GetSceneProxy();
            FCubeSceneProxy* cubeProxy = static_cast<FCubeSceneProxy*>(baseProxy);
            if (cubeProxy)
            {
                if (!cubeProxy->AreResourcesInitialized())
                {
                    cubeProxy->InitializeResources(m_device);
                }
                cubeProxy->UpdateModelMatrix(m_cubeActor->GetActorTransform().ToMatrixWithScale());
            }
        }
    }
    
    // Render using FSceneRenderer (UE5-style pipeline)
    // The renderer handles visibility, culling, and draw command generation
    m_sceneRenderer->RenderThreadBegin(*cmdList);
    m_sceneRenderer->Render(*cmdList);
    m_sceneRenderer->RenderThreadEnd(*cmdList);
    
    // Also render the cube directly for now (until FSceneRenderer fully handles mesh drawing)
    // This ensures we see the rotating cube while FSceneRenderer infrastructure is being built
    TArray<FLightSceneInfo*> lights;
    if (m_scene)
    {
        const TSparseArray<FLightSceneInfoCompact>& sceneLights = m_scene->GetLights();
        for (auto It = sceneLights.CreateConstIterator(); It; ++It)
        {
            if (It->LightSceneInfo)
            {
                lights.Add(It->LightSceneInfo);
            }
        }
    }
    
    if (m_cubeActor)
    {
        UCubeMeshComponent* meshComp = m_cubeActor->GetCubeMeshComponent();
        if (meshComp)
        {
            FCubeSceneProxy* cubeProxy = static_cast<FCubeSceneProxy*>(meshComp->GetSceneProxy());
            if (cubeProxy)
            {
                cubeProxy->DrawWithLighting(cmdList, viewMatrix, projectionMatrix, cameraPosition, lights);
            }
        }
    }
    
    MR_LOG(LogCubeSceneApp, Verbose, "FSceneRenderer render complete");
}

bool CubeSceneApplication::initializeSceneRenderer()
{
    MR_LOG(LogCubeSceneApp, Log, "Initializing FSceneRenderer...");
    
    if (!m_scene)
    {
        MR_LOG(LogCubeSceneApp, Error, "Cannot initialize scene renderer: no scene");
        return false;
    }
    
    // Create Renderer::FSceneViewFamily for FSceneRenderer
    if (!m_rendererViewFamily)
    {
        m_rendererViewFamily = new MonsterEngine::Renderer::FSceneViewFamily();
        m_rendererViewFamily->Scene = nullptr;  // Renderer::FScene is different from Engine::FScene
        m_rendererViewFamily->RenderTarget = nullptr;
        m_rendererViewFamily->FrameNumber = 0;
        m_rendererViewFamily->bDeferredShading = false;  // Use forward shading
        m_rendererViewFamily->bRenderFog = false;
        m_rendererViewFamily->bRenderPostProcessing = false;
        m_rendererViewFamily->bRenderShadows = true;
    }
    
    // Create FForwardShadingSceneRenderer
    m_sceneRenderer = new MonsterEngine::Renderer::FForwardShadingSceneRenderer(m_rendererViewFamily);
    if (!m_sceneRenderer)
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to create FSceneRenderer");
        return false;
    }
    
    MR_LOG(LogCubeSceneApp, Log, "FSceneRenderer initialized successfully (Forward Shading)");
    return true;
}

void CubeSceneApplication::onShutdown()
{
    MR_LOG(LogCubeSceneApp, Log, "Shutting down CubeSceneApplication...");
    
    // Unregister viewport texture from ImGui before shutdown
    if (m_imguiRenderer && m_viewportTextureID != 0)
    {
        m_imguiRenderer->UnregisterTexture(static_cast<ImTextureID>(m_viewportTextureID));
        m_viewportTextureID = 0;
    }
    
    // Release viewport render targets
    m_viewportColorTarget.Reset();
    m_viewportDepthTarget.Reset();
    
    // Shutdown ImGui (DISABLED)
    // shutdownImGui();
    
    // Clean up scene renderer
    if (m_sceneRenderer)
    {
        delete m_sceneRenderer;
        m_sceneRenderer = nullptr;
    }
    
    // Clean up renderer view family
    if (m_rendererViewFamily)
    {
        delete m_rendererViewFamily;
        m_rendererViewFamily = nullptr;
    }
    
    // Clean up scene view
    if (m_sceneView)
    {
        delete m_sceneView;
        m_sceneView = nullptr;
    }
    
    // Clean up view family
    if (m_viewFamily)
    {
        delete m_viewFamily;
        m_viewFamily = nullptr;
    }
    
    // Clean up camera manager
    if (m_cameraManager)
    {
        delete m_cameraManager;
        m_cameraManager = nullptr;
    }
    
    // Clean up actors (scene will handle this)
    // Note: Don't delete actors directly if scene owns them
    
    // Clean up scene
    if (m_scene)
    {
        delete m_scene;
        m_scene = nullptr;
    }
    
    m_cubeActor = nullptr;
    m_directionalLight = nullptr;
    m_pointLight = nullptr;
    
    MR_LOG(LogCubeSceneApp, Log, "CubeSceneApplication shutdown complete");
}

void CubeSceneApplication::setWindowDimensions(uint32 Width, uint32 Height)
{
    m_windowWidth = Width;
    m_windowHeight = Height;
    
    // Update camera aspect ratio
    if (m_cameraManager)
    {
        FMinimalViewInfo viewInfo = m_cameraManager->GetCameraCacheView();
        viewInfo.AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
        m_cameraManager->SetCameraCachePOV(viewInfo);
    }
}

// ============================================================================
// Initialization Helpers
// ============================================================================

bool CubeSceneApplication::initializeScene()
{
    MR_LOG(LogCubeSceneApp, Log, "Initializing scene...");
    
    // Create scene
    m_scene = new FScene(nullptr, false);
    if (!m_scene)
    {
        return false;
    }
    
    // Create cube actor
    m_cubeActor = new ACubeActor();
    m_cubeActor->SetRotationSpeed(0.0f);
    m_cubeActor->SetRotationEnabled(true);  // Disable per-frame rotation for debugging
    m_cubeActor->SetRotationAxis(FVector(0.5f, 1.0f, 0.0f));
    m_cubeActor->SetScene(m_scene);
    m_cubeActor->BeginPlay();
    
    // Add cube to scene
    UCubeMeshComponent* meshComp = m_cubeActor->GetCubeMeshComponent();
    if (meshComp)
    {
        // Add primitive to scene
        m_scene->AddPrimitive(meshComp);
        
        // Pre-initialize GPU resources for the cube proxy
        // This must be done before any render pass begins
        FPrimitiveSceneProxy* baseProxy = meshComp->GetSceneProxy();
        FCubeSceneProxy* cubeProxy = static_cast<FCubeSceneProxy*>(baseProxy);
        if (cubeProxy && m_device)
        {
            cubeProxy->InitializeResources(m_device);
        }
    }
    
    // Create directional light (sun) - main light source
    m_directionalLight = new UDirectionalLightComponent();
    m_directionalLight->SetLightColor(FLinearColor(1.0f, 1.0f, 1.0f));  // Pure white
    m_directionalLight->SetIntensity(3.0f);  // Increased intensity for brighter scene
    m_directionalLight->SetWorldRotation(FRotator(-45.0f, 30.0f, 0.0f));
    m_scene->AddLight(m_directionalLight);
    
    // Point light disabled for now
    // m_pointLight = new UPointLightComponent();
    // m_pointLight->SetLightColor(FLinearColor(0.3f, 0.5f, 1.0f));
    // m_pointLight->SetIntensity(2.0f);
    // m_pointLight->SetWorldLocation(FVector(2.0f, 1.0f, -1.0f));
    // m_pointLight->SetAttenuationRadius(10.0f);
    // m_scene->AddLight(m_pointLight);
    m_pointLight = nullptr;
    
    // Create and configure cube material
    m_cubeMaterial = MakeShared<FMaterial>(FName("CubeMaterial"));
    if (m_cubeMaterial)
    {
        // Set material properties
        FMaterialProperties matProps;
        matProps.BlendMode = EMaterialBlendMode::Opaque;
        matProps.ShadingModel = EMaterialShadingModel::DefaultLit;
        matProps.bTwoSided = false;
        m_cubeMaterial->SetMaterialProperties(matProps);
        
        // Set default material parameters
        m_cubeMaterial->SetDefaultVectorParameter(FName("BaseColor"), FLinearColor(0.8f, 0.6f, 0.4f, 1.0f));
        m_cubeMaterial->SetDefaultScalarParameter(FName("Metallic"), 0.0f);
        m_cubeMaterial->SetDefaultScalarParameter(FName("Roughness"), 0.5f);
        m_cubeMaterial->SetDefaultScalarParameter(FName("Specular"), 0.5f);
        
        MR_LOG(LogCubeSceneApp, Log, "Cube material created with default parameters");
    }
    
    MR_LOG(LogCubeSceneApp, Log, "Scene initialized with cube, lights, and material");
    return true;
}

bool CubeSceneApplication::initializeCamera()
{
    MR_LOG(LogCubeSceneApp, Log, "Initializing camera...");
    
    // Create camera manager
    m_cameraManager = new FCameraManager();
    m_cameraManager->Initialize(nullptr);
    
    // Set initial camera view
    // Camera at z=5 looking at origin (positive Z, looking towards negative Z)
    FMinimalViewInfo viewInfo;
    viewInfo.Location = FVector(0.0, 0.0, 5.0);  // Camera at z=5 (moved back for better view)
    viewInfo.Rotation = FRotator(0.0, 180.0, 0.0);  // Rotate 180 degrees to look at origin
    viewInfo.FOV = 45.0f;
    viewInfo.AspectRatio = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
    viewInfo.ProjectionMode = ECameraProjectionMode::Perspective;
    viewInfo.OrthoNearClipPlane = 0.1f;
    viewInfo.OrthoFarClipPlane = 100.0f;
    viewInfo.PerspectiveNearClipPlane = 0.1f;
    
    // Set both cache and view target to ensure UpdateCamera doesn't overwrite
    m_cameraManager->SetCameraCachePOV(viewInfo);
    m_cameraManager->SetViewTargetPOV(viewInfo);
    
    // Verify camera was set correctly
    const FMinimalViewInfo& verifyView = m_cameraManager->GetCameraCacheView();
    MR_LOG(LogCubeSceneApp, Log, "Camera initialized: Location=(%.2f, %.2f, %.2f), FOV=%.1f",
           verifyView.Location.X, verifyView.Location.Y, verifyView.Location.Z, verifyView.FOV);
    return true;
}

bool CubeSceneApplication::initializeRenderer()
{
    MR_LOG(LogCubeSceneApp, Log, "Initializing renderer...");
    
    // Note: FSceneViewFamily and FForwardShadingRenderer would be created here if fully implemented
    // For now, we render directly through the scene proxy
    m_viewFamily = nullptr;
    
    MR_LOG(LogCubeSceneApp, Log, "Renderer initialized");
    return true;
}

void CubeSceneApplication::updateCamera(float DeltaTime)
{
    if (!m_cameraManager)
    {
        return;
    }
    
    // Optional: Orbit camera around the cube
    if (m_bOrbitCamera)
    {
        m_cameraOrbitAngle += DeltaTime * 0.5f;  // Slow orbit
        
        float radius = 3.0f;
        float x = radius * FMath::Sin(m_cameraOrbitAngle);
        float z = -radius * FMath::Cos(m_cameraOrbitAngle);
        
        FMinimalViewInfo viewInfo = m_cameraManager->GetCameraCacheView();
        viewInfo.Location = FVector(x, 0.0, z);
        
        // Look at origin
        FVector lookDir = -viewInfo.Location.GetSafeNormal();
        viewInfo.Rotation = FRotator(0.0f, 0.0f, 0.0f);  // Simplified - just face forward
        
        m_cameraManager->SetCameraCachePOV(viewInfo);
    }
}

// ============================================================================
// ImGui Integration
// ============================================================================

bool CubeSceneApplication::initializeImGui()
{
    MR_LOG(LogCubeSceneApp, Log, "Initializing ImGui...");
    
    // Create ImGui context
    m_imguiContext = MakeUnique<Editor::FImGuiContext>();
    if (!m_imguiContext->Initialize())
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to initialize ImGui context");
        return false;
    }
    
    // Create ImGui renderer
    if (!m_device)
    {
        MR_LOG(LogCubeSceneApp, Error, "No RHI device available for ImGui renderer");
        return false;
    }
    
    m_imguiRenderer = MakeUnique<Editor::FImGuiRenderer>();
    if (!m_imguiRenderer->Initialize(m_device))
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to initialize ImGui renderer");
        return false;
    }
    
    // Create input handler
    m_imguiInputHandler = MakeUnique<Editor::FImGuiInputHandler>(m_imguiContext.Get());
    
    // Set initial window size
    if (getWindow())
    {
        m_imguiRenderer->OnWindowResize(getWindow()->getWidth(), getWindow()->getHeight());
    }
    
    m_bImGuiInitialized = true;
    MR_LOG(LogCubeSceneApp, Log, "ImGui initialized successfully");
    
    return true;
}

void CubeSceneApplication::shutdownImGui()
{
    MR_LOG(LogCubeSceneApp, Log, "Shutting down ImGui...");
    
    m_bImGuiInitialized = false;
    
    // Shutdown in reverse order
    m_imguiInputHandler.Reset();
    
    if (m_imguiRenderer)
    {
        m_imguiRenderer->Shutdown();
        m_imguiRenderer.Reset();
    }
    
    if (m_imguiContext)
    {
        m_imguiContext->Shutdown();
        m_imguiContext.Reset();
    }
    
    MR_LOG(LogCubeSceneApp, Log, "ImGui shutdown complete");
}

void CubeSceneApplication::renderImGui()
{
    if (!m_bImGuiInitialized || !m_imguiContext || !m_imguiRenderer)
    {
        return;
    }
    
    // Get window dimensions
    uint32 width = getWindow() ? getWindow()->getWidth() : m_windowWidth;
    uint32 height = getWindow() ? getWindow()->getHeight() : m_windowHeight;
    
    // Begin ImGui frame
    m_imguiContext->BeginFrame(m_deltaTime, width, height);
    
    // Render main menu bar
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Viewport", nullptr, &m_bShowViewport);
            ImGui::Separator();
            ImGui::MenuItem("Scene Info", nullptr, &m_bShowSceneInfo);
            ImGui::MenuItem("Camera Control", nullptr, &m_bShowCameraControl);
            ImGui::MenuItem("Lighting Control", nullptr, &m_bShowLightingControl);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &m_bShowDemoWindow);
            ImGui::EndMenu();
        }
        
        // Show FPS in menu bar
        ImGui::Separator();
        ImGui::Text("FPS: %.1f (%.3f ms)", 1.0f / m_deltaTime, m_deltaTime * 1000.0f);
        
        ImGui::EndMainMenuBar();
    }
    
    // Render panels
    if (m_bShowViewport)
    {
        renderViewportPanel();
    }
    
    if (m_bShowSceneInfo)
    {
        renderSceneInfoPanel();
    }
    
    if (m_bShowCameraControl)
    {
        renderCameraControlPanel();
    }
    
    if (m_bShowLightingControl)
    {
        renderLightingControlPanel();
    }
    
    // Render demo window if enabled
    if (m_bShowDemoWindow)
    {
        ImGui::ShowDemoWindow(&m_bShowDemoWindow);
    }
    
    // End ImGui frame
    m_imguiContext->EndFrame();
    
    // Get draw data and render
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData && m_device)
    {
        RHI::IRHICommandList* cmdList = m_device->getImmediateCommandList();
        if (cmdList)
        {
            m_imguiRenderer->RenderDrawData(cmdList, drawData);
        }
    }
}

void CubeSceneApplication::renderSceneInfoPanel()
{
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scene Info", &m_bShowSceneInfo))
    {
        ImGui::Text("Cube Scene Application");
        ImGui::Separator();
        
        ImGui::Text("Total Time: %.2f s", m_totalTime);
        ImGui::Text("Window: %u x %u", m_windowWidth, m_windowHeight);
        
        ImGui::Separator();
        ImGui::Text("Rendering:");
        ImGui::Text("  Backend: %s", m_device ? (m_device->getRHIBackend() == RHI::ERHIBackend::Vulkan ? "Vulkan" : "OpenGL") : "None");
        ImGui::Text("  FSceneRenderer: %s", m_bUseSceneRenderer ? "Enabled" : "Disabled");
        
        ImGui::Separator();
        ImGui::Text("Scene:");
        ImGui::Text("  Cube Actor: %s", m_cubeActor ? "Active" : "None");
        ImGui::Text("  Lights: %d", m_scene ? m_scene->GetLights().Num() : 0);
    }
    ImGui::End();
}

void CubeSceneApplication::renderCameraControlPanel()
{
    ImGui::SetNextWindowSize(ImVec2(300, 180), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Camera Control", &m_bShowCameraControl))
    {
        ImGui::Checkbox("Orbit Camera", &m_bOrbitCamera);
        
        if (m_cameraManager)
        {
            FMinimalViewInfo viewInfo = m_cameraManager->GetCameraCacheView();
            
            ImGui::Separator();
            ImGui::Text("Camera Position:");
            ImGui::Text("  X: %.2f  Y: %.2f  Z: %.2f", viewInfo.Location.X, viewInfo.Location.Y, viewInfo.Location.Z);
            
            ImGui::Separator();
            float fov = viewInfo.FOV;
            if (ImGui::SliderFloat("FOV", &fov, 30.0f, 120.0f))
            {
                viewInfo.FOV = fov;
                m_cameraManager->SetCameraCachePOV(viewInfo);
            }
        }
    }
    ImGui::End();
}

void CubeSceneApplication::renderLightingControlPanel()
{
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Lighting Control", &m_bShowLightingControl))
    {
        ImGui::Text("Cube Rotation");
        ImGui::SliderFloat("Rotation Speed", &m_cubeRotationSpeed, 0.0f, 5.0f);
        
        ImGui::Separator();
        ImGui::Text("Directional Light");
        ImGui::SliderFloat("Intensity", &m_lightIntensity, 0.0f, 10.0f);
        ImGui::ColorEdit3("Color", m_lightColor);
    }
    ImGui::End();
}

// ============================================================================
// Input Events for ImGui
// ============================================================================

void CubeSceneApplication::onWindowResize(uint32 width, uint32 height)
{
    m_windowWidth = width;
    m_windowHeight = height;
    
    // Update camera aspect ratio
    if (m_cameraManager)
    {
        FMinimalViewInfo viewInfo = m_cameraManager->GetCameraCacheView();
        viewInfo.AspectRatio = static_cast<float>(width) / static_cast<float>(height);
        m_cameraManager->SetCameraCachePOV(viewInfo);
    }
    
    // Update ImGui renderer
    if (m_imguiRenderer)
    {
        m_imguiRenderer->OnWindowResize(width, height);
    }
}

void CubeSceneApplication::onKeyPressed(EKey key)
{
    if (m_imguiInputHandler)
    {
        m_imguiInputHandler->OnKeyEvent(key, EInputAction::Pressed);
    }
    
    // Handle application shortcuts
    if (key == EKey::Escape)
    {
        requestExit();
    }
}

void CubeSceneApplication::onKeyReleased(EKey key)
{
    if (m_imguiInputHandler)
    {
        m_imguiInputHandler->OnKeyEvent(key, EInputAction::Released);
    }
}

void CubeSceneApplication::onMouseButtonPressed(EKey button, const MousePosition& position)
{
    if (m_imguiInputHandler)
    {
        m_imguiInputHandler->OnMouseButton(button, true);
    }
}

void CubeSceneApplication::onMouseButtonReleased(EKey button, const MousePosition& position)
{
    if (m_imguiInputHandler)
    {
        m_imguiInputHandler->OnMouseButton(button, false);
    }
}

void CubeSceneApplication::onMouseMoved(const MousePosition& position)
{
    if (m_imguiInputHandler)
    {
        m_imguiInputHandler->OnMouseMove(static_cast<float>(position.x), static_cast<float>(position.y));
    }
}

void CubeSceneApplication::onMouseScrolled(float64 xOffset, float64 yOffset)
{
    if (m_imguiInputHandler)
    {
        m_imguiInputHandler->OnMouseScroll(static_cast<float>(xOffset), static_cast<float>(yOffset));
    }
}

// ============================================================================
// Viewport Render Target
// ============================================================================

bool CubeSceneApplication::initializeViewportRenderTarget()
{
    MR_LOG(LogCubeSceneApp, Log, "Initializing viewport render target (%ux%u)...", m_viewportWidth, m_viewportHeight);
    
    if (!m_device)
    {
        MR_LOG(LogCubeSceneApp, Error, "Cannot create viewport render target: no device");
        return false;
    }
    
    // Create color render target texture
    // Use B8G8R8A8_SRGB to match swapchain format for pipeline compatibility
    RHI::TextureDesc colorDesc;
    colorDesc.width = m_viewportWidth;
    colorDesc.height = m_viewportHeight;
    colorDesc.depth = 1;
    colorDesc.mipLevels = 1;
    colorDesc.arraySize = 1;
    colorDesc.format = RHI::EPixelFormat::B8G8R8A8_SRGB;  // Match swapchain format
    colorDesc.usage = RHI::EResourceUsage::RenderTarget | RHI::EResourceUsage::ShaderResource;
    colorDesc.debugName = "Viewport Color Target";
    
    m_viewportColorTarget = m_device->createTexture(colorDesc);
    if (!m_viewportColorTarget)
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to create viewport color target");
        return false;
    }
    
    // Create depth target texture
    // Use D32_SFLOAT to match swapchain depth format for pipeline compatibility
    RHI::TextureDesc depthDesc;
    depthDesc.width = m_viewportWidth;
    depthDesc.height = m_viewportHeight;
    depthDesc.depth = 1;
    depthDesc.mipLevels = 1;
    depthDesc.arraySize = 1;
    depthDesc.format = RHI::EPixelFormat::D32_FLOAT;  // Match swapchain depth format
    depthDesc.usage = RHI::EResourceUsage::DepthStencil;
    depthDesc.debugName = "Viewport Depth Target";
    
    m_viewportDepthTarget = m_device->createTexture(depthDesc);
    if (!m_viewportDepthTarget)
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to create viewport depth target");
        return false;
    }
    
    // NOTE: RTT render pass uses initialLayout=UNDEFINED with loadOp=CLEAR
    // This handles any previous layout automatically, no manual initialization needed
    
    // Register color target with ImGui renderer
    if (m_imguiRenderer && m_viewportColorTarget)
    {
        m_viewportTextureID = static_cast<uint64>(m_imguiRenderer->RegisterTexture(m_viewportColorTarget));
        MR_LOG(LogCubeSceneApp, Log, "Viewport texture registered with ImGui (ID: %llu)", m_viewportTextureID);
    }
    
    // Register viewport color target for per-command-buffer layout transitions
    if (m_device->getRHIBackend() == RHI::ERHIBackend::Vulkan && m_viewportColorTarget)
    {
        auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);
        auto* vulkanTexture = static_cast<RHI::Vulkan::VulkanTexture*>(m_viewportColorTarget.Get());
        if (vulkanDevice && vulkanTexture)
        {
            vulkanDevice->registerTextureForLayoutTransition(
                vulkanTexture->getImage(),
                1,  // mip levels
                1   // array layers
            );
        }
    }
    
    MR_LOG(LogCubeSceneApp, Log, "Viewport render target initialized successfully");
    return true;
}

void CubeSceneApplication::resizeViewportRenderTarget(uint32 Width, uint32 Height)
{
    if (Width == 0 || Height == 0)
    {
        return;
    }
    
    if (Width == m_viewportWidth && Height == m_viewportHeight)
    {
        return;
    }
    
    MR_LOG(LogCubeSceneApp, Log, "Resizing viewport render target to %ux%u", Width, Height);
    
    // Unregister old texture from ImGui
    if (m_imguiRenderer && m_viewportTextureID != 0)
    {
        m_imguiRenderer->UnregisterTexture(static_cast<ImTextureID>(m_viewportTextureID));
        m_viewportTextureID = 0;
    }
    
    // Clear descriptor set cache to invalidate references to old textures
    if (m_device && m_device->getRHIBackend() == RHI::ERHIBackend::Vulkan)
    {
        auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);
        auto* cache = vulkanDevice->GetDescriptorSetCache();
        if (cache)
        {
            cache->ClearCache();
        }
    }
    
    // Release old textures
    m_viewportColorTarget.Reset();
    m_viewportDepthTarget.Reset();
    
    // Mark viewport texture as not ready until it's rendered to
    m_bViewportTextureReady = false;
    
    // Update dimensions
    m_viewportWidth = Width;
    m_viewportHeight = Height;
    
    // Recreate render targets
    initializeViewportRenderTarget();
}

void CubeSceneApplication::renderSceneToViewport(
    RHI::IRHICommandList* cmdList,
    const FMatrix& viewMatrix,
    const FMatrix& projectionMatrix,
    const FVector& cameraPosition)
{
    if (!cmdList || !m_viewportColorTarget || !m_viewportDepthTarget)
    {
        return;
    }
    
    // NOTE: Do NOT transition textures before render pass!
    // Device render pass has initialLayout=UNDEFINED with loadOp=CLEAR.
    // Pre-transitioning to COLOR_ATTACHMENT_OPTIMAL causes layout mismatch.
    // The render pass will handle the layout transition automatically.
    
    // Set viewport render targets
    TArray<TSharedPtr<RHI::IRHITexture>> renderTargets;
    renderTargets.Add(m_viewportColorTarget);
    cmdList->setRenderTargets(TSpan<TSharedPtr<RHI::IRHITexture>>(renderTargets), m_viewportDepthTarget);
    
    // Clear render targets
    float clearColor[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
    cmdList->clearRenderTarget(m_viewportColorTarget, clearColor);
    cmdList->clearDepthStencil(m_viewportDepthTarget, 1.0f, 0);
    
    // Set viewport and scissor
    RHI::Viewport viewport(static_cast<float>(m_viewportWidth), static_cast<float>(m_viewportHeight));
    cmdList->setViewport(viewport);
    
    RHI::ScissorRect scissor;
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<int32>(m_viewportWidth);
    scissor.bottom = static_cast<int32>(m_viewportHeight);
    cmdList->setScissorRect(scissor);
    
    MR_LOG(LogCubeSceneApp, Log, "Viewport: %.0fx%.0f, Scissor: %dx%d", 
           viewport.width, viewport.height, scissor.right, scissor.bottom);
    
    // Gather lights from scene
    TArray<FLightSceneInfo*> lights;
    if (m_scene)
    {
        const TSparseArray<FLightSceneInfoCompact>& sceneLights = m_scene->GetLights();
        for (auto It = sceneLights.CreateConstIterator(); It; ++It)
        {
            if (It->LightSceneInfo)
            {
                lights.Add(It->LightSceneInfo);
            }
        }
    }
    
    // Render cube to viewport
    if (m_cubeActor)
    {
        UCubeMeshComponent* meshComp = m_cubeActor->GetCubeMeshComponent();
        if (meshComp)
        {
            // Ensure component transform is up to date
            meshComp->UpdateComponentToWorld();
            
            FCubeSceneProxy* cubeProxy = static_cast<FCubeSceneProxy*>(meshComp->GetSceneProxy());
            if (cubeProxy)
            {
                if (!cubeProxy->AreResourcesInitialized())
                {
                    cubeProxy->InitializeResources(m_device);
                }
                cubeProxy->UpdateModelMatrix(m_cubeActor->GetActorTransform().ToMatrixWithScale());
                cubeProxy->DrawWithLighting(cmdList, viewMatrix, projectionMatrix, cameraPosition, lights);
            }
        }
    }
    
    // End render pass for viewport
    // RTT render pass has finalLayout=SHADER_READ_ONLY_OPTIMAL, no manual transition needed
    cmdList->endRenderPass();
    
    // Mark viewport texture as ready for display
    m_bViewportTextureReady = true;
}

void CubeSceneApplication::renderViewportPanel()
{
    ImGui::SetNextWindowSize(ImVec2(820, 640), ImGuiCond_FirstUseEver);
    
    // Use window flags to allow resize
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    
    if (ImGui::Begin("Viewport", &m_bShowViewport, windowFlags))
    {
        // Get available content region size
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        uint32 newWidth = static_cast<uint32>(contentSize.x);
        uint32 newHeight = static_cast<uint32>(contentSize.y);
        
        // Ensure minimum size
        if (newWidth < 64) newWidth = 64;
        if (newHeight < 64) newHeight = 64;
        
        // Note: Viewport resize is temporarily disabled due to descriptor set cache issues
        // TODO: Fix descriptor set cache invalidation when textures are destroyed
        // if (newWidth != m_viewportWidth || newHeight != m_viewportHeight)
        // {
        //     m_bViewportNeedsResize = true;
        //     m_pendingViewportWidth = newWidth;
        //     m_pendingViewportHeight = newHeight;
        // }
        
        // Display the viewport texture only if it has been rendered to
        if (m_viewportTextureID != 0 && m_bViewportTextureReady)
        {
            // Flip UV vertically for Vulkan
            ImVec2 uv0(0.0f, 0.0f);
            ImVec2 uv1(1.0f, 1.0f);
            
            if (m_device && m_device->getRHIBackend() == RHI::ERHIBackend::Vulkan)
            {
                // Vulkan has inverted Y, so flip the texture
                uv0 = ImVec2(0.0f, 1.0f);
                uv1 = ImVec2(1.0f, 0.0f);
            }
            
            ImGui::Image(static_cast<ImTextureID>(m_viewportTextureID), ImVec2(static_cast<float>(m_viewportWidth), static_cast<float>(m_viewportHeight)), uv0, uv1);
        }
        else
        {
            // Show placeholder while viewport texture is being initialized
            ImGui::Text("Initializing viewport...");
        }
        
        // Show viewport info
        ImGui::SetCursorPos(ImVec2(10, 30));
        ImGui::Text("Viewport: %ux%u", m_viewportWidth, m_viewportHeight);
    }
    ImGui::End();
}

// ============================================================================
// Shadow Mapping Implementation
// ============================================================================

bool CubeSceneApplication::initializeShadowMap()
{
    MR_LOG(LogCubeSceneApp, Log, "Initializing shadow map (resolution: %u)", m_shadowMapResolution);
    
    if (!m_device)
    {
        MR_LOG(LogCubeSceneApp, Error, "Cannot initialize shadow map: no device");
        return false;
    }
    
    // Create shadow map depth texture
    RHI::TextureDesc shadowMapDesc;
    shadowMapDesc.width = m_shadowMapResolution;
    shadowMapDesc.height = m_shadowMapResolution;
    shadowMapDesc.depth = 1;
    shadowMapDesc.mipLevels = 1;
    shadowMapDesc.arraySize = 1;
    shadowMapDesc.format = RHI::EPixelFormat::D32_FLOAT;
    shadowMapDesc.usage = RHI::EResourceUsage::DepthStencil | RHI::EResourceUsage::ShaderResource;
    shadowMapDesc.debugName = "ShadowMap";
    
    m_shadowMapTexture = m_device->createTexture(shadowMapDesc);
    if (!m_shadowMapTexture)
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to create shadow map texture");
        return false;
    }
    
    MR_LOG(LogCubeSceneApp, Log, "Shadow map initialized successfully");
    return true;
}

FMatrix CubeSceneApplication::calculateLightViewProjection(
    const FVector& lightDirection,
    float sceneBoundsRadius)
{
    // Normalize light direction
    FVector lightDir = lightDirection.GetSafeNormal();
    if (lightDir.IsNearlyZero())
    {
        lightDir = FVector(0.0, -1.0, 0.0);  // Default to down
    }
    
    // Calculate light position (far enough to encompass scene)
    float lightDistance = sceneBoundsRadius * 2.0f;
    FVector lightPos = -lightDir * lightDistance;
    
    // Calculate up vector (avoid parallel to light direction)
    FVector upVector = FVector(0.0, 1.0, 0.0);
    if (FMath::Abs(FVector::DotProduct(lightDir, upVector)) > 0.99f)
    {
        upVector = FVector(1.0, 0.0, 0.0);
    }
    
    // Create light view matrix
    FVector targetPos = FVector::ZeroVector;  // Look at scene center
    FMatrix lightViewMatrix = FMatrix::MakeLookAt(lightPos, targetPos, upVector);
    
    // Create orthographic projection for directional light
    float orthoSize = sceneBoundsRadius * 1.5f;
    float nearPlane = 0.1f;
    float farPlane = lightDistance * 2.0f;
    
    FMatrix lightProjectionMatrix = FMatrix::MakeOrtho(orthoSize * 2.0, orthoSize * 2.0, nearPlane, farPlane);
    
    // Combine view and projection
    return lightViewMatrix * lightProjectionMatrix;
}

void CubeSceneApplication::renderShadowDepthPass(
    RHI::IRHICommandList* cmdList,
    const FVector& lightDirection,
    FMatrix& outLightViewProjection)
{
    if (!cmdList || !m_shadowMapTexture || !m_cubeActor)
    {
        return;
    }
    
    MR_LOG(LogCubeSceneApp, Verbose, "Rendering shadow depth pass");
    
    // Calculate light view-projection matrix
    float sceneBoundsRadius = 5.0f;  // Approximate scene bounds
    outLightViewProjection = calculateLightViewProjection(lightDirection, sceneBoundsRadius);
    
    // Set shadow map as render target (depth only)
    TArray<TSharedPtr<RHI::IRHITexture>> emptyColorTargets;
    cmdList->setRenderTargets(
        TSpan<TSharedPtr<RHI::IRHITexture>>(emptyColorTargets),
        m_shadowMapTexture
    );
    
    // Set viewport for shadow map
    RHI::Viewport shadowViewport;
    shadowViewport.x = 0.0f;
    shadowViewport.y = 0.0f;
    shadowViewport.width = static_cast<float>(m_shadowMapResolution);
    shadowViewport.height = static_cast<float>(m_shadowMapResolution);
    shadowViewport.minDepth = 0.0f;
    shadowViewport.maxDepth = 1.0f;
    cmdList->setViewport(shadowViewport);
    
    // Set scissor rect
    RHI::ScissorRect shadowScissor;
    shadowScissor.left = 0;
    shadowScissor.top = 0;
    shadowScissor.right = m_shadowMapResolution;
    shadowScissor.bottom = m_shadowMapResolution;
    cmdList->setScissorRect(shadowScissor);
    
    // Clear depth buffer
    cmdList->clearDepthStencil(m_shadowMapTexture, 1.0f, 0);
    
    // Get cube mesh component and render to shadow map
    UCubeMeshComponent* meshComp = m_cubeActor->GetCubeMeshComponent();
    if (meshComp)
    {
        meshComp->UpdateComponentToWorld();
        
        FPrimitiveSceneProxy* baseProxy = meshComp->GetSceneProxy();
        FCubeSceneProxy* cubeProxy = static_cast<FCubeSceneProxy*>(baseProxy);
        
        if (cubeProxy && cubeProxy->AreResourcesInitialized())
        {
            // Update model matrix
            cubeProxy->UpdateModelMatrix(m_cubeActor->GetActorTransform().ToMatrixWithScale());
            
            // For shadow depth pass, we render with light's view-projection
            // Use identity projection since we're rendering depth only
            FVector lightPos = -lightDirection.GetSafeNormal() * 10.0f;
            TArray<FLightSceneInfo*> emptyLights;
            
            // Draw cube for shadow map (using light's matrices)
            cubeProxy->Draw(cmdList, outLightViewProjection, FMatrix::Identity, lightPos);
        }
    }
    
    // End shadow render pass
    cmdList->endRenderPass();
    
    MR_LOG(LogCubeSceneApp, Verbose, "Shadow depth pass complete");
}

void CubeSceneApplication::renderCubeWithShadows(
    RHI::IRHICommandList* cmdList,
    const FMatrix& viewMatrix,
    const FMatrix& projectionMatrix,
    const FVector& cameraPosition,
    const TArray<FLightSceneInfo*>& lights,
    const FMatrix& lightViewProjection)
{
    if (!m_cubeActor || !cmdList)
    {
        return;
    }
    
    UCubeMeshComponent* meshComp = m_cubeActor->GetCubeMeshComponent();
    if (!meshComp)
    {
        return;
    }
    
    meshComp->UpdateComponentToWorld();
    
    FPrimitiveSceneProxy* baseProxy = meshComp->GetSceneProxy();
    FCubeSceneProxy* cubeProxy = static_cast<FCubeSceneProxy*>(baseProxy);
    
    if (!cubeProxy)
    {
        return;
    }
    
    // Initialize resources if needed
    if (!cubeProxy->AreResourcesInitialized())
    {
        cubeProxy->InitializeResources(m_device);
    }
    
    // Update model matrix
    cubeProxy->UpdateModelMatrix(m_cubeActor->GetActorTransform().ToMatrixWithScale());
    
    // Create shadow parameters
    FVector4 shadowParams(
        m_shadowDepthBias,
        m_shadowSlopeBias,
        m_shadowNormalBias,
        m_shadowDistance
    );
    
    // Draw with shadows
    cubeProxy->DrawWithShadows(
        cmdList,
        viewMatrix,
        projectionMatrix,
        cameraPosition,
        lights,
        lightViewProjection,
        m_shadowMapTexture,
        shadowParams
    );
}

// ============================================================================
// RDG-based Rendering
// ============================================================================

void CubeSceneApplication::renderWithRDG(
    RHI::IRHICommandList* cmdList,
    const Math::FMatrix& viewMatrix,
    const Math::FMatrix& projectionMatrix,
    const Math::FVector& cameraPosition)
{
    using namespace RDG;
    
    if (!m_bShadowsEnabled || !m_directionalLight)
    {
        // Fallback to non-RDG rendering if shadows are disabled
        TArray<FLightSceneInfo*> lights;
        if (m_directionalLight)
        {
            lights.Add(m_directionalLight->GetLightSceneInfo());
        }
        if (m_pointLight)
        {
            lights.Add(m_pointLight->GetLightSceneInfo());
        }
        renderCube(cmdList, viewMatrix, projectionMatrix, cameraPosition, lights);
        return;
    }
    
    // Get light direction from light scene info
    FVector lightDirection = FVector(0.5f, -1.0f, 0.3f);
    if (m_directionalLight->GetLightSceneInfo() && m_directionalLight->GetLightSceneInfo()->Proxy)
    {
        lightDirection = m_directionalLight->GetLightSceneInfo()->Proxy->GetDirection();
    }
    lightDirection.Normalize();
    
    // Calculate light view-projection matrix
    Math::FMatrix lightViewProjection = calculateLightViewProjection(lightDirection, 10.0f);
    
    // Create RDG builder
    FRDGBuilder graphBuilder(m_device, "CubeSceneRenderGraph");
    
    // Register external shadow map texture
    FRDGTextureRef shadowMapRDG = graphBuilder.registerExternalTexture(
        "ShadowMap",
        m_shadowMapTexture.Get(),
        ERHIAccess::Unknown
    );
    
    // Add Shadow Depth Pass
    graphBuilder.addPass(
        "ShadowDepthPass",
        ERDGPassFlags::Raster,
        [&](FRDGPassBuilder& builder)
        {
            // Declare that we will write to the shadow map depth
            builder.writeDepth(shadowMapRDG, ERHIAccess::DSVWrite);
        },
        [this, lightViewProjection](RHI::IRHICommandList& rhiCmdList)
        {
            // Set shadow map as render target
            TArray<TSharedPtr<RHI::IRHITexture>> emptyColorTargets;
            TSharedPtr<RHI::IRHITexture> depthTarget(m_shadowMapTexture.Get(), [](RHI::IRHITexture*){});
            rhiCmdList.setRenderTargets(
                TSpan<TSharedPtr<RHI::IRHITexture>>(emptyColorTargets.GetData(), 0),
                depthTarget
            );
            
            // Clear depth
            rhiCmdList.clearDepthStencil(depthTarget, true, false, 1.0f, 0);
            
            // Set viewport
            RHI::Viewport viewport;
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = static_cast<float32>(m_shadowMapResolution);
            viewport.height = static_cast<float32>(m_shadowMapResolution);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            rhiCmdList.setViewport(viewport);
            
            // Set scissor
            RHI::ScissorRect scissor;
            scissor.left = 0;
            scissor.top = 0;
            scissor.right = static_cast<int32>(m_shadowMapResolution);
            scissor.bottom = static_cast<int32>(m_shadowMapResolution);
            rhiCmdList.setScissorRect(scissor);
            
            // Render cube to shadow map
            if (m_cubeActor)
            {
                UCubeMeshComponent* meshComp = m_cubeActor->GetCubeMeshComponent();
                if (meshComp)
                {
                    meshComp->UpdateComponentToWorld();
                    FPrimitiveSceneProxy* baseProxy = meshComp->GetSceneProxy();
                    FCubeSceneProxy* cubeProxy = static_cast<FCubeSceneProxy*>(baseProxy);
                    
                    if (cubeProxy && cubeProxy->AreResourcesInitialized())
                    {
                        cubeProxy->UpdateModelMatrix(m_cubeActor->GetActorTransform().ToMatrixWithScale());
                        
                        // Render depth only - use regular draw method for shadow depth pass
                        cubeProxy->Draw(&rhiCmdList, lightViewProjection, lightViewProjection, 
                                      FVector::ZeroVector);
                    }
                }
            }
            
            rhiCmdList.endRenderPass();
        }
    );
    
    // Add Main Render Pass
    graphBuilder.addPass(
        "MainRenderPass",
        ERDGPassFlags::Raster,
        [&](FRDGPassBuilder& builder)
        {
            // Declare that we will read from the shadow map
            builder.readTexture(shadowMapRDG, ERHIAccess::SRVGraphics);
        },
        [this, viewMatrix, projectionMatrix, cameraPosition, lightViewProjection](RHI::IRHICommandList& rhiCmdList)
        {
            // Render cube with shadows
            TArray<FLightSceneInfo*> lights;
            if (m_directionalLight)
            {
                lights.Add(m_directionalLight->GetLightSceneInfo());
            }
            if (m_pointLight)
            {
                lights.Add(m_pointLight->GetLightSceneInfo());
            }
            
            renderCubeWithShadows(
                &rhiCmdList,
                viewMatrix,
                projectionMatrix,
                cameraPosition,
                lights,
                lightViewProjection
            );
        }
    );
    
    // Execute the render graph
    MR_LOG(LogCubeSceneApp, Log, "Executing RDG with %d passes", 2);
    graphBuilder.execute(*cmdList);
    MR_LOG(LogCubeSceneApp, Log, "RDG execution complete");
}

} // namespace MonsterRender
