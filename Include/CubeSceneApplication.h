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

namespace MonsterEngine
{

// Forward declarations
class FScene;
class FCameraManager;
class ACubeActor;
class UDirectionalLightComponent;
class UPointLightComponent;
class FSceneViewFamily;
class FForwardShadingRenderer;

} // namespace MonsterEngine

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

protected:
    /** RHI device */
    RHI::IRHIDevice* m_device;

    /** Scene containing all actors */
    MonsterEngine::FScene* m_scene;

    /** Camera manager */
    MonsterEngine::FCameraManager* m_cameraManager;

    /** Cube actor */
    MonsterEngine::ACubeActor* m_cubeActor;

    /** Directional light component */
    MonsterEngine::UDirectionalLightComponent* m_directionalLight;

    /** Point light component */
    MonsterEngine::UPointLightComponent* m_pointLight;

    /** Scene view family */
    MonsterEngine::FSceneViewFamily* m_viewFamily;

    /** Forward renderer */
    MonsterEngine::FForwardShadingRenderer* m_renderer;

    /** Window dimensions */
    uint32 m_windowWidth;
    uint32 m_windowHeight;

    /** Total elapsed time */
    float m_totalTime;

    /** Camera orbit angle */
    float m_cameraOrbitAngle;

    /** Whether to orbit camera */
    bool m_bOrbitCamera;
};

} // namespace MonsterRender
