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
#include "Core/Logging/LogMacros.h"
#include "Core/Color.h"
#include "Math/MonsterMath.h"
#include "Containers/SparseArray.h"

namespace MonsterRender
{

using namespace MonsterEngine;

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogCubeSceneApp, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

CubeSceneApplication::CubeSceneApplication()
    : Application()
    , m_device(nullptr)
    , m_scene(nullptr)
    , m_cameraManager(nullptr)
    , m_cubeActor(nullptr)
    , m_directionalLight(nullptr)
    , m_pointLight(nullptr)
    , m_viewFamily(nullptr)
    , m_renderer(nullptr)
    , m_windowWidth(1280)
    , m_windowHeight(720)
    , m_totalTime(0.0f)
    , m_cameraOrbitAngle(0.0f)
    , m_bOrbitCamera(false)
{
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
    
    MR_LOG(LogCubeSceneApp, Log, "CubeSceneApplication initialized successfully");
}

void CubeSceneApplication::onUpdate(float32 deltaTime)
{
    m_totalTime += deltaTime;
    
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
    
    // Get command list
    RHI::IRHICommandList* cmdList = m_device->getImmediateCommandList();
    if (!cmdList)
    {
        return;
    }
    
    // Get camera view info
    const FMinimalViewInfo& cameraView = m_cameraManager->GetCameraCacheView();
    
    // Calculate view and projection matrices
    FMatrix viewMatrix = cameraView.CalculateViewRotationMatrix();
    viewMatrix = FMatrix::MakeTranslation(-cameraView.Location) * viewMatrix;
    
    FMatrix projectionMatrix = cameraView.CalculateProjectionMatrix();
    
    // Flip Y for Vulkan
    if (m_device->getRHIBackend() == RHI::ERHIBackend::Vulkan)
    {
        projectionMatrix.M[1][1] *= -1.0f;
    }
    
    // Get camera position
    FVector cameraPosition = cameraView.Location;
    
    // Gather lights from scene
    TArray<FLightSceneInfo*> lights;
    if (m_scene)
    {
        // Get all lights from scene (TSparseArray)
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
    
    // Render cube using scene proxy
    if (m_cubeActor)
    {
        UCubeMeshComponent* meshComp = m_cubeActor->GetCubeMeshComponent();
        if (meshComp)
        {
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
    }
}

void CubeSceneApplication::onShutdown()
{
    MR_LOG(LogCubeSceneApp, Log, "Shutting down CubeSceneApplication...");
    
    // Clean up renderer
    if (m_renderer)
    {
        delete m_renderer;
        m_renderer = nullptr;
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
    m_cubeActor->SetRotationSpeed(1.0f);
    m_cubeActor->SetRotationAxis(FVector(0.5f, 1.0f, 0.0f));
    m_cubeActor->SetScene(m_scene);
    m_cubeActor->BeginPlay();
    
    // Add cube to scene
    UCubeMeshComponent* meshComp = m_cubeActor->GetCubeMeshComponent();
    if (meshComp)
    {
        // Add primitive to scene
        m_scene->AddPrimitive(meshComp);
    }
    
    // Create directional light (sun)
    m_directionalLight = new UDirectionalLightComponent();
    m_directionalLight->SetLightColor(FLinearColor(1.0f, 0.95f, 0.9f));
    m_directionalLight->SetIntensity(1.0f);
    m_directionalLight->SetWorldRotation(FRotator(-45.0f, 30.0f, 0.0f));
    m_scene->AddLight(m_directionalLight);
    
    // Create point light (accent)
    m_pointLight = new UPointLightComponent();
    m_pointLight->SetLightColor(FLinearColor(0.3f, 0.5f, 1.0f));
    m_pointLight->SetIntensity(2.0f);
    m_pointLight->SetWorldLocation(FVector(2.0f, 1.0f, -1.0f));
    m_pointLight->SetAttenuationRadius(10.0f);
    m_scene->AddLight(m_pointLight);
    
    MR_LOG(LogCubeSceneApp, Log, "Scene initialized with cube and lights");
    return true;
}

bool CubeSceneApplication::initializeCamera()
{
    MR_LOG(LogCubeSceneApp, Log, "Initializing camera...");
    
    // Create camera manager
    m_cameraManager = new FCameraManager();
    m_cameraManager->Initialize(nullptr);
    
    // Set initial camera view
    FMinimalViewInfo viewInfo;
    viewInfo.Location = FVector(0.0, 0.0, -3.0);  // Camera at z=-3 looking at origin
    viewInfo.Rotation = FRotator::ZeroRotator;
    viewInfo.FOV = 45.0f;
    viewInfo.AspectRatio = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
    viewInfo.ProjectionMode = ECameraProjectionMode::Perspective;
    viewInfo.OrthoNearClipPlane = 0.1f;
    viewInfo.OrthoFarClipPlane = 100.0f;
    viewInfo.PerspectiveNearClipPlane = 0.1f;
    
    m_cameraManager->SetCameraCachePOV(viewInfo);
    
    MR_LOG(LogCubeSceneApp, Log, "Camera initialized at (0, 0, -3) with FOV=45");
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

} // namespace MonsterRender
