// Copyright Monster Engine. All Rights Reserved.



/**

 * @file CubeSceneApplication.cpp

 * @brief Implementation of demo application for rotating lit cube

 */



#include "CubeSceneApplication.h"

#include "Engine/Scene.h"

#include "Engine/Actor.h"

#include "Engine/Actors/CubeActor.h"

#include "Engine/Actors/FloorActor.h"

#include "Engine/Components/CubeMeshComponent.h"

#include "Engine/Components/FloorMeshComponent.h"

#include "Engine/Components/LightComponent.h"

#include "Engine/Proxies/CubeSceneProxy.h"

#include "Engine/Proxies/FloorSceneProxy.h"

#include "Engine/Camera/CameraManager.h"

#include "Engine/Camera/CameraTypes.h"

#include "Engine/Camera/CameraSceneViewHelper.h"

#include "Engine/Camera/FPSCameraController.h"

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



// stb_image for texture loading

#define STB_IMAGE_IMPLEMENTATION

#include "../3rd-party/stb/stb_image.h"



namespace MonsterRender

{



using namespace MonsterEngine;



// Use global log category (defined in LogCategories.cpp)

using MonsterRender::LogCubeSceneApp;



// ============================================================================

// Construction / Destruction

// ============================================================================



// Helper function to create config with preferred backend

static ApplicationConfig CreateCubeSceneConfig()

{

    ApplicationConfig config;

    config.name = "Monster Engine";

    config.preferredBackend = RHI::ERHIBackend::Vulkan;  // Use Vulkan backend

    config.windowProperties.width = 1280;

    config.windowProperties.height = 1024;

    config.enableValidation = true;

    return config;

}



CubeSceneApplication::CubeSceneApplication()

    : Application(CreateCubeSceneConfig())

    , m_device(nullptr)

    , m_bUseSceneRenderer(true)  // Enable FSceneRenderer for UE5-style rendering

    , m_windowWidth(1280)

    , m_windowHeight(720)

    , m_totalTime(0.0f)

    , m_cameraOrbitAngle(0.0f)

    , m_bOrbitCamera(false)

    , m_bFPSCameraEnabled(true)  // Enable FPS camera by default

    , m_bMouseLookActive(false)

    , m_lastMouseX(0.0f)

    , m_lastMouseY(0.0f)

    , m_bFirstMouseInput(true)

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

    

    // Update window title to show render backend

    if (getWindow())

    {

        RHI::ERHIBackend backend = m_device->getRHIBackend();

        const char* backendName = (backend == RHI::ERHIBackend::Vulkan) ? "Vulkan" : "OpenGL";

        String windowTitle = String("MonsterEngine - CubeScene [") + backendName + "]";

        getWindow()->setTitle(windowTitle);

        MR_LOG(LogCubeSceneApp, Log, "Window title set to: %s", windowTitle.c_str());

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

    

    // Adjust window size to match swapchain extent

    // Reference: UE5 ensures window and swapchain sizes are synchronized

    if (getWindow() && m_device)

    {

        auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);

        if (vulkanDevice)

        {

            auto swapchainExtent = vulkanDevice->getSwapchainExtent();

            if (swapchainExtent.width != m_windowWidth || swapchainExtent.height != m_windowHeight)

            {

                MR_LOG(LogCubeSceneApp, Log, "Adjusting window size from %ux%u to match swapchain %ux%u",

                       m_windowWidth, m_windowHeight, swapchainExtent.width, swapchainExtent.height);

                getWindow()->setSize(swapchainExtent.width, swapchainExtent.height);

                m_windowWidth = swapchainExtent.width;

                m_windowHeight = swapchainExtent.height;

            }

        }

    }

    

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

            m_bShadowsEnabled = true;

        }

    }

    

    // Load wood texture for floor

    if (!loadWoodTexture())

    {

        MR_LOG(LogCubeSceneApp, Error, "Failed to load wood texture");

        return;

    }

    

    // Initialize PBR helmet rendering

    if (m_bHelmetPBREnabled)

    {

        MR_LOG(LogCubeSceneApp, Log, "PBR helmet enabled, initializing...");

        if (!initializeHelmetPBR())

        {

            MR_LOG(LogCubeSceneApp, Warning, "Failed to initialize PBR helmet");

            m_bHelmetPBREnabled = false;

        }

    }

    

    MR_LOG(LogCubeSceneApp, Log, "CubeSceneApplication initialized successfully");

}



void CubeSceneApplication::onUpdate(float32 deltaTime)

{

    m_totalTime += deltaTime;

    m_deltaTime = deltaTime;

    

    // Update cube rotation speed from UI for all cubes

    for (auto& cubeActor : m_cubeActors)

    {

        if (cubeActor)

        {

            cubeActor->SetRotationSpeed(m_cubeRotationSpeed);

        }

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

    

    // Update all cube actors (rotation animation)

    for (auto& cubeActor : m_cubeActors)

    {

        if (cubeActor)

        {

            cubeActor->Tick(deltaTime);

        }

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

    

    // Calculate view matrix using FPS camera controller if available

    FMatrix viewMatrix;

    FVector cameraPos = cameraView.Location;

    

    if (m_fpsCameraController && m_bFPSCameraEnabled)

    {

        // Use FPS camera's view matrix directly - this uses the camera's orientation

        viewMatrix = m_fpsCameraController->GetViewMatrix();

        cameraPos = m_fpsCameraController->GetPosition();

        

        MR_LOG(LogCubeSceneApp, Log, "Using FPS camera: pos=(%.2f, %.2f, %.2f), yaw=%.2f, pitch=%.2f",

               cameraPos.X, cameraPos.Y, cameraPos.Z,

               m_fpsCameraController->GetYaw(), m_fpsCameraController->GetPitch());

    }

    else

    {

        // Fallback: look at origin using LookAt matrix

        FVector targetPos = FVector::ZeroVector;

        FVector upVector = FVector(0.0, 1.0, 0.0);  // Y-up

        

        MR_LOG(LogCubeSceneApp, Log, "Using LookAt: cameraPos=(%.2f, %.2f, %.2f), target=(%.2f, %.2f, %.2f)",

               cameraPos.X, cameraPos.Y, cameraPos.Z, targetPos.X, targetPos.Y, targetPos.Z);

        

        viewMatrix = FMatrix::MakeLookAt(cameraPos, targetPos, upVector);

    }

    

    FMatrix projectionMatrix = cameraView.CalculateProjectionMatrix();

    // Note: MakePerspective already includes Y-flip for Vulkan (negative M[1][1])

    // Combined with RHI viewport negative height, this produces correct rendering

    // Do NOT apply additional Y-flip here to avoid double-flip

    

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

        

        MR_LOG(LogCubeSceneApp, Log, "Shadow check: enabled=%d, texture=%p, lights=%d",

               m_bShadowsEnabled, m_shadowMapTexture.Get(), lights.Num());

        

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

            

            MR_LOG(LogCubeSceneApp, Log, "Executing shadow depth pass, light dir: (%.2f, %.2f, %.2f)",

                   lightDirection.X, lightDirection.Y, lightDirection.Z);

            

            // Render shadow depth pass

            renderShadowDepthPass(cmdList, lightDirection, lightViewProjection);

        }

        else

        {

            MR_LOG(LogCubeSceneApp, Warning, "Shadow depth pass SKIPPED");

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

        

        // Enable back-face culling (CCW is front face to match Vulkan pipeline)

        glEnable(GL_CULL_FACE);

        glCullFace(GL_BACK);

        glFrontFace(GL_CCW);

        

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

        

        // Render PBR helmet

        if (m_bHelmetPBREnabled && m_bHelmetInitialized)

        {

            renderHelmetWithPBR(cmdList, viewMatrix, projectionMatrix, cameraPosition);

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

        if (m_bUseRDG/* && m_bShadowsEnabled*/)

        {

            MR_LOG(LogCubeSceneApp, Log, "Using RDG rendering path");

            renderWithRDG(cmdList, viewMatrix, projectionMatrix, cameraPosition);

            // NOTE: PBR helmet rendering is integrated into RDG MainRenderPass

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

            

            // Render PBR helmet

            if (m_bHelmetPBREnabled && m_bHelmetInitialized)

            {

                renderHelmetWithPBR(cmdList, viewMatrix, projectionMatrix, cameraPosition);

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

    if (!m_bRenderCube)

        return;

    // Render all cube actors

    for (auto& cubeActor : m_cubeActors)

    {

        if (!cubeActor) continue;

        

        UCubeMeshComponent* meshComp = cubeActor->GetCubeMeshComponent();

        if (!meshComp) continue;

        

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

            cubeProxy->UpdateModelMatrix(cubeActor->GetActorTransform().ToMatrixWithScale());

            

            // Draw with lighting

            cubeProxy->DrawWithLighting(cmdList, viewMatrix, projectionMatrix, cameraPosition, lights);

        }

    }

    

    // Render floor with lighting (no shadows in this path)

    if (m_floorActor)

    {

        UFloorMeshComponent* floorMeshComp = m_floorActor->GetFloorMeshComponent();

        if (floorMeshComp)

        {

            floorMeshComp->UpdateComponentToWorld();

            

            FFloorSceneProxy* floorProxy = floorMeshComp->GetFloorSceneProxy();

            if (floorProxy)

            {

                // Initialize resources if needed

                if (!floorProxy->AreResourcesInitialized())

                {

                    floorProxy->InitializeResources(m_device);

                }

                

                // Update floor model matrix

                floorProxy->UpdateModelMatrix(m_floorActor->GetActorTransform().ToMatrixWithScale());

                

                // Draw floor with lighting

                floorProxy->DrawWithLighting(cmdList, viewMatrix, projectionMatrix, cameraPosition, lights);

            }

        }

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

    

    // Ensure cube resources are initialized and update transform for all cubes

    for (auto& cubeActor : m_cubeActors)

    {

        if (!cubeActor) continue;

        

        UCubeMeshComponent* meshComp = cubeActor->GetCubeMeshComponent();

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

                cubeProxy->UpdateModelMatrix(cubeActor->GetActorTransform().ToMatrixWithScale());

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

    

    // Render all cube actors

    for (auto& cubeActor : m_cubeActors)

    {

        if (!cubeActor) continue;

        

        UCubeMeshComponent* meshComp = cubeActor->GetCubeMeshComponent();

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

        m_rendererViewFamily = MakeUnique<MonsterEngine::Renderer::FSceneViewFamily>();

        m_rendererViewFamily->Scene = nullptr;  // Renderer::FScene is different from Engine::FScene

        m_rendererViewFamily->RenderTarget = nullptr;

        m_rendererViewFamily->FrameNumber = 0;

        m_rendererViewFamily->bDeferredShading = false;  // Use forward shading

        m_rendererViewFamily->bRenderFog = false;

        m_rendererViewFamily->bRenderPostProcessing = false;

        m_rendererViewFamily->bRenderShadows = true;

    }

    

    // Create FForwardShadingSceneRenderer

    m_sceneRenderer = MakeUnique<MonsterEngine::Renderer::FForwardShadingSceneRenderer>(m_rendererViewFamily.Get());

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

    

    MR_LOG(LogCubeSceneApp, Log, "=== CubeSceneApplication::onShutdown() called ===");

    

    // CRITICAL: Clean up in reverse order of initialization to avoid dangling pointers

    

    // Step 1: Shutdown ImGui first (it may reference textures)

    MR_LOG(LogCubeSceneApp, Log, "Step 1: Shutting down ImGui...");

    shutdownImGui();

    MR_LOG(LogCubeSceneApp, Log, "ImGui shutdown complete");

    

    // Step 2: Unregister viewport texture (after ImGui shutdown)

    if (m_viewportTextureID != 0)

    {

        m_viewportTextureID = 0;

    }

    

    // Step 3: Clean up scene renderer (may reference scene and views)

    m_sceneRenderer.Reset();

    

    // Step 4: Clean up renderer view family

    m_rendererViewFamily.Reset();

    

    // Step 5: Clean up scene view

    m_sceneView.Reset();

    

    // Step 6: Clean up view family

    m_viewFamily.Reset();

    

    // Step 7: Clean up FPS camera controller BEFORE camera manager

    // This is critical because controller may reference camera manager

    if (m_fpsCameraController)

    {

        // Detach from camera manager first

        m_fpsCameraController->SetCameraManager(nullptr);

        m_fpsCameraController.Reset();

    }

    

    // Step 8: Clean up camera manager

    m_cameraManager.Reset();

    

    // Step 9: Clean up scene first (this will clean up FPrimitiveSceneInfo which owns proxies)

    MR_LOG(LogCubeSceneApp, Log, "Destroying scene...");

    m_scene.Reset();

    MR_LOG(LogCubeSceneApp, Log, "Scene destroyed");

    

    // Step 10: Clean up actors (they own components, which are now safe to destroy since proxies are gone)

    MR_LOG(LogCubeSceneApp, Log, "Destroying actors...");

    m_floorActor.Reset();

    m_cubeActors.Empty();  // Clear all cube actors

    m_directionalLight.Reset();

    m_pointLight.Reset();

    MR_LOG(LogCubeSceneApp, Log, "Actors destroyed");

    

    // Step 11: Release GPU resources (textures, buffers)

    // These must be released AFTER scene and actors are destroyed

    m_viewportColorTarget.Reset();

    m_viewportDepthTarget.Reset();

    m_shadowMapTexture.Reset();

    m_woodTexture.Reset();

    m_woodSampler.Reset();

    

    // Step 12: Clear material references

    m_cubeMaterial.Reset();

    

    // Device is owned by Engine, don't delete it

    m_device = nullptr;

    

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

    m_scene = MakeUnique<FScene>(nullptr, false);

    if (!m_scene)

    {

        return false;

    }

    

    // Create multiple cube actors for shadow demonstration

    // Reference: LearnOpenGL camera tutorial - multiple cubes at different positions

    // Cube positions: one on floor, two floating at different heights for visible shadows

    const FVector cubePositions[] = {

        FVector(-2.0f, 0.5f, -1.0f),   // Cube 1: on floor (Y=0.5 so bottom touches Y=0)

        FVector(0.0f, 2.0f, 0.0f),     // Cube 2: floating high for shadow

        FVector(2.5f, 1.2f, 1.5f)      // Cube 3: floating medium height

    };

    const int32 numCubes = 3;

    

    for (int32 i = 0; i < numCubes; ++i)

    {

        TUniquePtr<ACubeActor> cubeActor = MakeUnique<ACubeActor>();

        cubeActor->SetRotationSpeed(0.0f);

        cubeActor->SetRotationEnabled(false);  // Rotation disabled

        cubeActor->SetRotationAxis(FVector(0.5f, 1.0f, 0.0f));

        cubeActor->SetActorRotation(FRotator(0.0f, 0.0f, 0.0f));

        cubeActor->SetScene(m_scene.Get());

        cubeActor->BeginPlay();

        

        // Set position AFTER BeginPlay to ensure components are initialized

        cubeActor->SetActorLocation(cubePositions[i]);

        

        // Verify the position was set correctly

        FVector actualPos = cubeActor->GetActorLocation();

        MR_LOG(LogCubeSceneApp, Log, "Cube %d position set to: (%.2f, %.2f, %.2f)", 

               i, actualPos.X, actualPos.Y, actualPos.Z);

        

        // Add cube to scene

        UCubeMeshComponent* meshComp = cubeActor->GetCubeMeshComponent();

        if (meshComp)

        {

            // Add primitive to scene

            m_scene->AddPrimitive(meshComp);

            

            // Pre-initialize GPU resources for the cube proxy

            FPrimitiveSceneProxy* baseProxy = meshComp->GetSceneProxy();

            FCubeSceneProxy* cubeProxy = static_cast<FCubeSceneProxy*>(baseProxy);

            if (cubeProxy && m_device)

            {

                cubeProxy->InitializeResources(m_device);

            }

        }

        

        m_cubeActors.Add(std::move(cubeActor));

    }

    

    MR_LOG(LogCubeSceneApp, Log, "Created %d cube actors for shadow demonstration", numCubes);

    

    // Create floor actor

    m_floorActor = MakeUnique<AFloorActor>();

    m_floorActor->SetFloorSize(25.0f);

    m_floorActor->SetTextureTile(25.0f);

    m_floorActor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));  // Floor at origin

    m_floorActor->SetScene(m_scene.Get());

    m_floorActor->BeginPlay();

    

    // Add floor to scene and initialize its proxy

    UFloorMeshComponent* floorMeshComp = m_floorActor->GetFloorMeshComponent();

    if (floorMeshComp)

    {

        m_scene->AddPrimitive(floorMeshComp);

        

        // Initialize floor proxy GPU resources

        FFloorSceneProxy* floorProxy = floorMeshComp->GetFloorSceneProxy();

        if (floorProxy && m_device)

        {

            floorProxy->InitializeResources(m_device);

            

            // Set floor texture and sampler (will be set after loading)

            if (m_woodTexture)

            {

                floorProxy->SetTexture(m_woodTexture);

            }

            if (m_woodSampler)

            {

                floorProxy->SetSampler(m_woodSampler);

            }

        }

    }

    

    MR_LOG(LogCubeSceneApp, Log, "Floor actor created at position (0, 0, 0)");

    

    // Create directional light (sun) - main light source

    // Light direction: pitch=-45 (from above), yaw=200 (from front-left of helmet)

    // The helmet faces roughly -Z direction after rotation, so light should come from +Z side

    m_directionalLight = MakeUnique<UDirectionalLightComponent>();

    m_directionalLight->SetLightColor(FLinearColor(1.0f, 0.98f, 0.95f));  // Slight warm tint

    m_directionalLight->SetIntensity(4.0f);  // Higher intensity for stronger specular highlights

    m_directionalLight->SetWorldRotation(FRotator(-225.0f, 30.0f, 0.0f));  // Light from front of helmet

    m_scene->AddLight(m_directionalLight.Get());

    

    // Point light disabled for now

    // m_pointLight = new UPointLightComponent();

    // m_pointLight->SetLightColor(FLinearColor(0.3f, 0.5f, 1.0f));

    // m_pointLight->SetIntensity(2.0f);

    // m_pointLight->SetWorldLocation(FVector(2.0f, 1.0f, -1.0f));

    // m_pointLight->SetAttenuationRadius(10.0f);

    // m_scene->AddLight(m_pointLight.Get());

    m_pointLight.Reset();

    

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

    m_cameraManager = MakeUnique<FCameraManager>();

    m_cameraManager->Initialize(nullptr);

    

    // Set initial camera view

    // Camera positioned to see both cube and floor

    // Cube is at (0, 1.5, 0), floor is at y=-0.5

    // Position camera at (5, 3, 5) looking at origin with downward pitch

    FMinimalViewInfo viewInfo;

    viewInfo.Location = FVector(5.0, 3.0, 5.0);  // Camera at (5, 3, 5) for good overview

    viewInfo.Rotation = FRotator(-30.0, -135.0, 0.0);  // Pitch down 30 degrees, look towards origin

    viewInfo.FOV = 60.0f;  // Wider FOV to see more of the scene

    // Use swapchain extent for aspect ratio, not window size

    auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);

    auto swapchainExtent = vulkanDevice->getSwapchainExtent();

    viewInfo.AspectRatio = static_cast<float>(swapchainExtent.width) / static_cast<float>(swapchainExtent.height);

    viewInfo.ProjectionMode = ECameraProjectionMode::Perspective;

    viewInfo.OrthoNearClipPlane = 0.1f;

    viewInfo.OrthoFarClipPlane = 100.0f;

    viewInfo.PerspectiveNearClipPlane = 0.1f;

    

    // Set both cache and view target to ensure UpdateCamera doesn't overwrite

    m_cameraManager->SetCameraCachePOV(viewInfo);

    m_cameraManager->SetViewTargetPOV(viewInfo);

    

    // Create FPS camera controller

    // Initial position: (5, 3, 5), looking towards origin (-X, -Z direction)

    // WorldUp = Y-up, Yaw = -135 (looking towards -X, -Z), Pitch = -30 (looking down)

    m_fpsCameraController = MakeUnique<FFPSCameraController>(

        FVector(5.0, 3.0, 5.0),   // Position: diagonal view

        FVector(0.0, 1.0, 0.0),   // World up (Y-up)

        -135.0f,                   // Yaw (looking towards origin from diagonal)

        -30.0f                     // Pitch (looking down 30 degrees to see floor)

    );

    

    // Configure FPS camera settings

    FFPSCameraSettings& settings = m_fpsCameraController->GetSettings();

    settings.MovementSpeed = 5.0f;

    settings.MouseSensitivity = 0.1f;

    settings.MinFOV = 1.0f;

    settings.MaxFOV = 90.0f;

    settings.MinPitch = -89.0f;

    settings.MaxPitch = 89.0f;

    settings.bConstrainPitch = true;

    settings.SprintMultiplier = 2.0f;

    

    // Set FOV to match camera manager

    m_fpsCameraController->SetFOV(45.0f);

    

    // Initialize with camera manager

    m_fpsCameraController->Initialize(m_cameraManager.Get());

    

    // Verify camera position after FPS controller initialization

    FMinimalViewInfo finalView = m_cameraManager->GetCameraCacheView();

    MR_LOG(LogCubeSceneApp, Log, "Final camera position: (%.2f, %.2f, %.2f), Rotation: (%.1f, %.1f, %.1f)", 

           finalView.Location.X, finalView.Location.Y, finalView.Location.Z,

           finalView.Rotation.Pitch, finalView.Rotation.Yaw, finalView.Rotation.Roll);

    

    MR_LOG(LogCubeSceneApp, Log, "Camera initialized successfully");

    

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

    

    // FPS Camera mode - process keyboard input for movement

    if (m_bFPSCameraEnabled && m_fpsCameraController && !m_bOrbitCamera)

    {

        // Get input manager from window

        IInputManager* inputManager = getWindow() ? getWindow()->getInputManager() : nullptr;

        if (inputManager)

        {

            // Check for sprint (Shift key)

            bool bSprinting = inputManager->isKeyDown(EKey::LeftShift) || 

                              inputManager->isKeyDown(EKey::RightShift);

            

            // Process WASD movement

            if (inputManager->isKeyDown(EKey::W))

            {

                m_fpsCameraController->ProcessKeyboard(ECameraMovement::Forward, DeltaTime, bSprinting);

            }

            if (inputManager->isKeyDown(EKey::S))

            {

                m_fpsCameraController->ProcessKeyboard(ECameraMovement::Backward, DeltaTime, bSprinting);

            }

            if (inputManager->isKeyDown(EKey::A))

            {

                m_fpsCameraController->ProcessKeyboard(ECameraMovement::Left, DeltaTime, bSprinting);

            }

            if (inputManager->isKeyDown(EKey::D))

            {

                m_fpsCameraController->ProcessKeyboard(ECameraMovement::Right, DeltaTime, bSprinting);

            }

            

            // Process vertical movement (Q/E or Space/Ctrl)

            if (inputManager->isKeyDown(EKey::E) || inputManager->isKeyDown(EKey::Space))

            {

                m_fpsCameraController->ProcessKeyboard(ECameraMovement::Up, DeltaTime, bSprinting);

            }

            if (inputManager->isKeyDown(EKey::Q) || inputManager->isKeyDown(EKey::LeftControl))

            {

                m_fpsCameraController->ProcessKeyboard(ECameraMovement::Down, DeltaTime, bSprinting);

            }

        }

        

        // Update FPS camera controller (applies state to camera manager)

        m_fpsCameraController->Update(DeltaTime);

    }

    // Optional: Orbit camera around the cube

    else if (m_bOrbitCamera)

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

    // Early exit if ImGui was never initialized

    if (!m_bImGuiInitialized && !m_imguiContext && !m_imguiRenderer && !m_imguiInputHandler)

    {

        return;

    }

    

    MR_LOG(LogCubeSceneApp, Log, "Shutting down ImGui...");

    

    // Mark as not initialized first to prevent any further rendering

    m_bImGuiInitialized = false;

    

    // Shutdown in reverse order

    // Step 1: Release input handler (doesn't need explicit shutdown)

    m_imguiInputHandler.Reset();

    

    // Step 2: Shutdown renderer (releases GPU resources)

    if (m_imguiRenderer)

    {

        m_imguiRenderer->Shutdown();

        m_imguiRenderer.Reset();

    }

    

    // Step 3: Shutdown context (destroys ImGui context)

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

        ImGui::Text("  Cube Actors: %d", static_cast<int>(m_cubeActors.Num()));

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

    // Reference: UE5 FSlateRHIRenderer::OnWindowResized

    MR_LOG(LogCubeSceneApp, Log, "Window resized to %ux%u", width, height);

    

    m_windowWidth = width;

    m_windowHeight = height;

    

    // Recreate swapchain with new size

    // Reference: UE5 FVulkanDynamicRHI::RHIResizeViewport

    if (m_device)

    {

        auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);

        if (vulkanDevice)

        {

            if (!vulkanDevice->recreateSwapchain(width, height))

            {

                MR_LOG(LogCubeSceneApp, Warning, "Failed to recreate swapchain on window resize");

                return;

            }

            

            // Get actual swapchain extent (may differ from window size)

            auto swapchainExtent = vulkanDevice->getSwapchainExtent();

            

            // Update camera aspect ratio based on actual swapchain size

            if (m_cameraManager)

            {

                FMinimalViewInfo viewInfo = m_cameraManager->GetCameraCacheView();

                viewInfo.AspectRatio = static_cast<float>(swapchainExtent.width) / static_cast<float>(swapchainExtent.height);

                m_cameraManager->SetCameraCachePOV(viewInfo);

                MR_LOG(LogCubeSceneApp, Verbose, "Camera aspect ratio updated to %f", viewInfo.AspectRatio);

            }

        }

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

    

    // Toggle FPS camera mode with F key

    if (key == EKey::F)

    {

        m_bFPSCameraEnabled = !m_bFPSCameraEnabled;

        MR_LOG(LogCubeSceneApp, Log, "FPS Camera %s", m_bFPSCameraEnabled ? "enabled" : "disabled");

    }

    

    // Toggle orbit camera with O key

    if (key == EKey::O)

    {

        m_bOrbitCamera = !m_bOrbitCamera;

        MR_LOG(LogCubeSceneApp, Log, "Orbit Camera %s", m_bOrbitCamera ? "enabled" : "disabled");

    }

    

    // Reset camera with R key

    if (key == EKey::R)

    {

        if (m_fpsCameraController)

        {

            m_fpsCameraController->SetPosition(FVector(0.0, 2.0, 5.0));

            m_fpsCameraController->SetRotation(-90.0f, 0.0f);

            m_fpsCameraController->SetFOV(45.0f);

            m_bFirstMouseInput = true;

            MR_LOG(LogCubeSceneApp, Log, "Camera reset to initial position");

        }

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

    

    // Enable mouse look when right mouse button is pressed

    if (button == EKey::MouseRight)

    {

        m_bMouseLookActive = true;

        m_bFirstMouseInput = true;  // Reset first mouse flag to prevent jump

        m_lastMouseX = static_cast<float>(position.x);

        m_lastMouseY = static_cast<float>(position.y);

    }

}



void CubeSceneApplication::onMouseButtonReleased(EKey button, const MousePosition& position)

{

    if (m_imguiInputHandler)

    {

        m_imguiInputHandler->OnMouseButton(button, false);

    }

    

    // Disable mouse look when right mouse button is released

    if (button == EKey::MouseRight)

    {

        m_bMouseLookActive = false;

    }

}



void CubeSceneApplication::onMouseMoved(const MousePosition& position)

{

    if (m_imguiInputHandler)

    {

        m_imguiInputHandler->OnMouseMove(static_cast<float>(position.x), static_cast<float>(position.y));

    }

    

    // Process mouse movement for FPS camera look

    if (m_bFPSCameraEnabled && m_bMouseLookActive && m_fpsCameraController && !m_bOrbitCamera)

    {

        float xpos = static_cast<float>(position.x);

        float ypos = static_cast<float>(position.y);

        

        if (m_bFirstMouseInput)

        {

            m_lastMouseX = xpos;

            m_lastMouseY = ypos;

            m_bFirstMouseInput = false;

        }

        

        // Calculate mouse offset

        float xoffset = xpos - m_lastMouseX;

        float yoffset = m_lastMouseY - ypos;  // Reversed: y-coordinates go from bottom to top

        

        m_lastMouseX = xpos;

        m_lastMouseY = ypos;

        

        // Process mouse movement for camera rotation

        m_fpsCameraController->ProcessMouseMovement(xoffset, yoffset);

    }

}



void CubeSceneApplication::onMouseScrolled(float64 xOffset, float64 yOffset)

{

    if (m_imguiInputHandler)

    {

        m_imguiInputHandler->OnMouseScroll(static_cast<float>(xOffset), static_cast<float>(yOffset));

    }

    

    // Process mouse scroll for FPS camera zoom

    if (m_bFPSCameraEnabled && m_fpsCameraController && !m_bOrbitCamera)

    {

        m_fpsCameraController->ProcessMouseScroll(static_cast<float>(yOffset));

    }

}



// ============================================================================

// Viewport Render Target

// ============================================================================



bool CubeSceneApplication::initializeViewportRenderTarget()

{

    if (!m_device)

    {

        MR_LOG(LogCubeSceneApp, Error, "Cannot create viewport render target: no device");

        return false;

    }

    

    // Get swapchain extent for viewport depth target

    // Reference: UE5 ensures depth buffer matches swapchain size

    auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);

    auto swapchainExtent = vulkanDevice->getSwapchainExtent();

    

    // Update viewport dimensions to match swapchain

    m_viewportWidth = swapchainExtent.width;

    m_viewportHeight = swapchainExtent.height;

    

    MR_LOG(LogCubeSceneApp, Log, "Initializing viewport render target (%ux%u) to match swapchain...", 

           m_viewportWidth, m_viewportHeight);

    

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

    

    // Render all cube actors to viewport

    for (auto& cubeActor : m_cubeActors)

    {

        if (!cubeActor) continue;

        

        UCubeMeshComponent* meshComp = cubeActor->GetCubeMeshComponent();

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

                cubeProxy->UpdateModelMatrix(cubeActor->GetActorTransform().ToMatrixWithScale());

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





bool CubeSceneApplication::loadWoodTexture()

{

    MR_LOG(LogCubeSceneApp, Log, "Loading wood texture...");

    

    if (!m_device)

    {

        MR_LOG(LogCubeSceneApp, Error, "Cannot load wood texture: no device");

        return false;

    }

    

    // Load texture using stb_image

    int32 width = 0;

    int32 height = 0;

    int32 channels = 0;

    const char* texturePath = "E:\\MonsterEngine\\resources\\textures\\wood.png";

    

    // Flip texture vertically for OpenGL/Vulkan compatibility

    stbi_set_flip_vertically_on_load(true);

    

    unsigned char* imageData = stbi_load(texturePath, &width, &height, &channels, STBI_rgb_alpha);

    if (!imageData)

    {

        MR_LOG(LogCubeSceneApp, Error, "Failed to load wood texture from: %s", texturePath);

        return false;

    }

    

    MR_LOG(LogCubeSceneApp, Log, "Wood texture loaded: %dx%d, %d channels", width, height, channels);

    

    // Create texture descriptor

    RHI::TextureDesc textureDesc;

    textureDesc.width = static_cast<uint32>(width);

    textureDesc.height = static_cast<uint32>(height);

    textureDesc.depth = 1;

    textureDesc.mipLevels = 1;

    textureDesc.arraySize = 1;

    textureDesc.format = RHI::EPixelFormat::R8G8B8A8_UNORM;

    textureDesc.usage = RHI::EResourceUsage::ShaderResource | RHI::EResourceUsage::TransferDst;

    textureDesc.debugName = "WoodTexture";

    

    m_woodTexture = m_device->createTexture(textureDesc);

    if (!m_woodTexture)

    {

        MR_LOG(LogCubeSceneApp, Error, "Failed to create wood texture");

        stbi_image_free(imageData);

        return false;

    }

    

    // Upload texture data using staging buffer

    uint32 imageSize = width * height * 4; // RGBA format

    

    // Create staging buffer

    RHI::BufferDesc stagingDesc;

    stagingDesc.size = imageSize;

    stagingDesc.usage = RHI::EResourceUsage::TransferSrc;

    stagingDesc.cpuAccessible = true;

    stagingDesc.debugName = "WoodTextureStagingBuffer";

    

    TSharedPtr<RHI::IRHIBuffer> stagingBuffer = m_device->createBuffer(stagingDesc);

    if (!stagingBuffer)

    {

        MR_LOG(LogCubeSceneApp, Error, "Failed to create staging buffer for wood texture");

        stbi_image_free(imageData);

        return false;

    }

    

    // Copy image data to staging buffer

    void* mappedData = stagingBuffer->map();

    if (!mappedData)

    {

        MR_LOG(LogCubeSceneApp, Error, "Failed to map staging buffer");

        stbi_image_free(imageData);

        return false;

    }

    

    FMemory::Memcpy(mappedData, imageData, imageSize);

    stagingBuffer->unmap();

    

    // Free stb_image data

    stbi_image_free(imageData);

    

    // Get immediate command list for texture upload

    RHI::IRHICommandList* cmdList = m_device->getImmediateCommandList();

    if (!cmdList)

    {

        MR_LOG(LogCubeSceneApp, Error, "Failed to get immediate command list");

        return false;

    }

    

    // Cast to Vulkan command list for texture upload

    auto* vulkanCmdList = dynamic_cast<MonsterRender::RHI::Vulkan::FVulkanRHICommandListImmediate*>(cmdList);

    if (!vulkanCmdList)

    {

        MR_LOG(LogCubeSceneApp, Error, "Failed to cast to Vulkan command list");

        return false;

    }

    

    // Begin command recording if not already recording

    bool wasRecording = vulkanCmdList->isRecording();

    if (!wasRecording)

    {

        vulkanCmdList->begin();

    }

    

    // Transition texture from UNDEFINED to TRANSFER_DST_OPTIMAL

    vulkanCmdList->transitionTextureLayoutSimple(

        m_woodTexture,

        VK_IMAGE_LAYOUT_UNDEFINED,

        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL

    );

    

    // Copy from staging buffer to texture

    vulkanCmdList->copyBufferToTexture(

        stagingBuffer,

        0,              // buffer offset

        m_woodTexture,

        0,              // mip level

        0,              // array layer

        width,

        height,

        1               // depth

    );

    

    // Transition texture from TRANSFER_DST_OPTIMAL to SHADER_READ_ONLY_OPTIMAL

    vulkanCmdList->transitionTextureLayoutSimple(

        m_woodTexture,

        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,

        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

    );

    

    // End command recording if we started it

    if (!wasRecording)

    {

        vulkanCmdList->end();

    }

    

    // Submit commands and wait for completion

    auto* vulkanDevice = dynamic_cast<MonsterRender::RHI::Vulkan::VulkanDevice*>(m_device);

    if (vulkanDevice && vulkanDevice->getCommandListContext())

    {

        vulkanDevice->getCommandListContext()->submitCommands(nullptr, 0, nullptr, 0);

    }

    

    // Wait for GPU to complete texture upload

    m_device->waitForIdle();

    

    // Refresh command buffer for next use

    if (vulkanDevice && vulkanDevice->getCommandListContext())

    {

        vulkanDevice->getCommandListContext()->refreshCommandBuffer();

    }

    

    // Register texture for layout transition tracking

    auto* vulkanTexture = dynamic_cast<MonsterRender::RHI::Vulkan::VulkanTexture*>(m_woodTexture.Get());

    if (vulkanDevice && vulkanTexture)

    {

        VkImage image = vulkanTexture->getImage();

        vulkanDevice->registerTextureForLayoutTransition(image, 1, 1);

    }

    

    // Create sampler with repeat mode and linear filtering

    RHI::SamplerDesc samplerDesc;

    samplerDesc.filter = RHI::ESamplerFilter::Trilinear;

    samplerDesc.addressU = RHI::ESamplerAddressMode::Wrap;

    samplerDesc.addressV = RHI::ESamplerAddressMode::Wrap;

    samplerDesc.addressW = RHI::ESamplerAddressMode::Wrap;

    samplerDesc.maxAnisotropy = 16;

    samplerDesc.debugName = "WoodSampler";

    

    m_woodSampler = m_device->createSampler(samplerDesc);

    if (!m_woodSampler)

    {

        MR_LOG(LogCubeSceneApp, Error, "Failed to create wood sampler");

        return false;

    }

    

    MR_LOG(LogCubeSceneApp, Log, "Wood texture loaded successfully");

    

    // Set texture to floor proxy if it exists

    if (m_floorActor)

    {

        UFloorMeshComponent* floorMeshComp = m_floorActor->GetFloorMeshComponent();

        if (floorMeshComp)

        {

            FFloorSceneProxy* floorProxy = floorMeshComp->GetFloorSceneProxy();

            if (floorProxy)

            {

                floorProxy->SetTexture(m_woodTexture);

                floorProxy->SetSampler(m_woodSampler);

                MR_LOG(LogCubeSceneApp, Log, "Wood texture set to floor proxy");

            }

        }

    }

    

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

    if (!cmdList || !m_shadowMapTexture || m_cubeActors.Num() == 0)

    {

        return;

    }

    

    MR_LOG(LogCubeSceneApp, Verbose, "Rendering shadow depth pass");

    

    // Calculate light view-projection matrix

    // Must be large enough to cover entire floor (size = 25.0f, so bounds = ��25)

    float sceneBoundsRadius = 30.0f;  // Cover floor and cubes

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

    

    // Render all cube actors to shadow map

    FVector lightPos = -lightDirection.GetSafeNormal() * 10.0f;

    

    for (auto& cubeActor : m_cubeActors)

    {

        if (!cubeActor) continue;

        

        UCubeMeshComponent* meshComp = cubeActor->GetCubeMeshComponent();

        if (meshComp)

        {

            meshComp->UpdateComponentToWorld();

            

            FPrimitiveSceneProxy* baseProxy = meshComp->GetSceneProxy();

            FCubeSceneProxy* cubeProxy = static_cast<FCubeSceneProxy*>(baseProxy);

            

            if (cubeProxy && cubeProxy->AreResourcesInitialized())

            {

                // Update model matrix

                cubeProxy->UpdateModelMatrix(cubeActor->GetActorTransform().ToMatrixWithScale());

                

                // Draw cube for shadow map (using light's matrices)

                cubeProxy->Draw(cmdList, outLightViewProjection, FMatrix::Identity, lightPos);

            }

        }

    }

    

    // Note: Floor does not cast shadows (it only receives shadows)

    // So we don't render floor to shadow map

    

    // End shadow render pass

    cmdList->endRenderPass();

    

    // Transition shadow map from depth attachment to shader resource for sampling

    // This is critical for Vulkan - the image layout must be correct for shader reads

    cmdList->transitionResource(

        m_shadowMapTexture,

        RHI::EResourceUsage::DepthStencil,

        RHI::EResourceUsage::ShaderResource

    );

    

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

    if (!m_bRenderCube) 

        return;



    if (m_cubeActors.Num() == 0 || !cmdList)

    {

        return;

    }

    

    // Create shadow parameters

    FVector4 shadowParams(

        m_shadowDepthBias,

        m_shadowSlopeBias,

        m_shadowNormalBias,

        m_shadowDistance

    );

    

    // Render all cube actors with shadows

    for (auto& cubeActor : m_cubeActors)

    {

        if (!cubeActor) continue;

        

        UCubeMeshComponent* meshComp = cubeActor->GetCubeMeshComponent();

        if (!meshComp) continue;

        

        meshComp->UpdateComponentToWorld();

        

        FPrimitiveSceneProxy* baseProxy = meshComp->GetSceneProxy();

        FCubeSceneProxy* cubeProxy = static_cast<FCubeSceneProxy*>(baseProxy);

        

        if (!cubeProxy) continue;

        

        // Initialize resources if needed

        if (!cubeProxy->AreResourcesInitialized())

        {

            cubeProxy->InitializeResources(m_device);

        }

        

        // Update model matrix

        cubeProxy->UpdateModelMatrix(cubeActor->GetActorTransform().ToMatrixWithScale());

        

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

    

    // Render floor with shadows (floor receives shadows from cubes)

    if (m_floorActor)

    {

        UFloorMeshComponent* floorMeshComp = m_floorActor->GetFloorMeshComponent();

        if (floorMeshComp)

        {

            floorMeshComp->UpdateComponentToWorld();

            

            FFloorSceneProxy* floorProxy = floorMeshComp->GetFloorSceneProxy();

            if (floorProxy)

            {

                // Initialize resources if needed

                if (!floorProxy->AreResourcesInitialized())

                {

                    floorProxy->InitializeResources(m_device);

                }

                

                // Update floor model matrix

                floorProxy->UpdateModelMatrix(m_floorActor->GetActorTransform().ToMatrixWithScale());

                

                // Draw floor with shadows

                floorProxy->DrawWithShadows(

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

        }

    }

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

    

    // Get light direction from light scene info

    FVector lightDirection = FVector(0.5f, -1.0f, 0.3f);

    if (m_directionalLight->GetLightSceneInfo() && m_directionalLight->GetLightSceneInfo()->Proxy)

    {

        lightDirection = m_directionalLight->GetLightSceneInfo()->Proxy->GetDirection();

    }

    lightDirection.Normalize();

    

    // Calculate light view-projection matrix

    // Must be large enough to cover entire floor (size = 25.0f, so bounds = ±25)

    Math::FMatrix lightViewProjection = calculateLightViewProjection(lightDirection, 30.0f);

    

    // Create RDG builder

    FRDGBuilder graphBuilder(m_device, "CubeSceneRenderGraph");

    

    // Register external shadow map texture (only if shadows are enabled and texture exists)

    FRDGTextureRef shadowMapRDG = nullptr;

    if (m_bShadowsEnabled && m_shadowMapTexture)

    {

        shadowMapRDG = graphBuilder.registerExternalTexture(

            "ShadowMap",

            m_shadowMapTexture.Get(),

            ERHIAccess::Unknown

        );

    }

    

    // Add Shadow Depth Pass (conditionally based on m_bShadowsEnabled)

    if (m_bShadowsEnabled && shadowMapRDG)

    {

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

                MR_LOG(LogCubeSceneApp, Log, "Executing Shadow Depth Pass");

                

                // NOTE: Do NOT call setRenderTargets here!

                // RDG automatically calls _setupRenderTargets() before executing this lambda.

                // The shadow map depth target is already set via builder.writeDepth() declaration.

                

                // Set viewport for shadow map

                RHI::Viewport viewport;

                viewport.x = 0;

                viewport.y = 0;

                viewport.width = static_cast<float32>(m_shadowMapResolution);

                viewport.height = static_cast<float32>(m_shadowMapResolution);

                viewport.minDepth = 0.0f;

                viewport.maxDepth = 1.0f;

                rhiCmdList.setViewport(viewport);

                

                // Set scissor rect

                RHI::ScissorRect scissor;

                scissor.left = 0;

                scissor.top = 0;

                scissor.right = static_cast<int32>(m_shadowMapResolution);

                scissor.bottom = static_cast<int32>(m_shadowMapResolution);

                rhiCmdList.setScissorRect(scissor);

                

                // Clear depth buffer

                rhiCmdList.clearDepthStencil(m_shadowMapTexture, 1.0f, 0);

                

                // Render all cube actors to shadow map

                for (auto& cubeActor : m_cubeActors)

                {

                    if (!cubeActor) continue;

                    

                    UCubeMeshComponent* meshComp = cubeActor->GetCubeMeshComponent();

                    if (meshComp)

                    {

                        meshComp->UpdateComponentToWorld();

                        FPrimitiveSceneProxy* baseProxy = meshComp->GetSceneProxy();

                        FCubeSceneProxy* cubeProxy = dynamic_cast<FCubeSceneProxy*>(baseProxy);

                        

                        if (cubeProxy && cubeProxy->AreResourcesInitialized())

                        {

                            cubeProxy->UpdateModelMatrix(cubeActor->GetActorTransform().ToMatrixWithScale());

                            

                            // Draw depth only using depth-only pipeline (no color attachment)

                            cubeProxy->DrawDepthOnly(&rhiCmdList, lightViewProjection);

                        }

                    }

                }

                

                // NOTE: Floor does NOT cast shadows, only receives them

                // So we don't render floor to shadow map

                

                // NOTE: Do NOT call endRenderPass() here!

                // RDG automatically ends the render pass after this lambda returns.

                // Calling endRenderPass() manually would cause render pass state corruption.

                

                // NOTE: Resource transitions are handled by RDG based on pass declarations.

                // The shadow map will be transitioned to shader resource before MainRenderPass.

                

                MR_LOG(LogCubeSceneApp, Log, "Shadow Depth Pass complete");

            }

        );

    }

    

    // Add Main Render Pass

    graphBuilder.addPass(

        "MainRenderPass",

        ERDGPassFlags::Raster,

        [&, shadowMapRDG](FRDGPassBuilder& builder)

        {

            // Declare that we will read from the shadow map (only if available)

            if (shadowMapRDG)

            {

                builder.readTexture(shadowMapRDG, ERHIAccess::SRVGraphics);

            }

        },

        [this, viewMatrix, projectionMatrix, cameraPosition, lightViewProjection](RHI::IRHICommandList& rhiCmdList)

        {

            MR_LOG(LogCubeSceneApp, Log, "Executing Main Render Pass with shadows");

            

            // NOTE: Do NOT call setRenderTargets here!

            // RDG automatically calls _setupRenderTargets() before executing this lambda.

            // Since no explicit render targets are declared, RDG uses swapchain rendering.

            

            // Set viewport to swapchain size (not window size)

            auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);

            auto swapchainExtent = vulkanDevice->getSwapchainExtent();

            RHI::Viewport viewport;

            viewport.x = 0.0f;

            viewport.y = 0.0f;

            viewport.width = static_cast<float>(swapchainExtent.width);

            viewport.height = static_cast<float>(swapchainExtent.height);

            viewport.minDepth = 0.0f;

            viewport.maxDepth = 1.0f;

            rhiCmdList.setViewport(viewport);

            

            // Set scissor rect to match viewport

            RHI::ScissorRect scissor;

            scissor.left = 0;

            scissor.top = 0;

            scissor.right = swapchainExtent.width;

            scissor.bottom = swapchainExtent.height;

            rhiCmdList.setScissorRect(scissor);

            

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

            

            // renderCubeWithShadows already renders both cubes and floor with shadows

            renderCubeWithShadows(

                &rhiCmdList,

                viewMatrix,

                projectionMatrix,

                cameraPosition,

                lights,

                lightViewProjection

            );

            

            // Render PBR helmet in the same render pass

            if (m_bHelmetPBREnabled && m_bHelmetInitialized)

            {

                renderHelmetWithPBR(&rhiCmdList, viewMatrix, projectionMatrix, cameraPosition);

            }

        }

    );

    

    // Execute the render graph

    MR_LOG(LogCubeSceneApp, Log, "Executing RDG with %d passes", 2);

    graphBuilder.execute(*cmdList);

    MR_LOG(LogCubeSceneApp, Log, "RDG execution complete");

}



} // namespace MonsterRender

