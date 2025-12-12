// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CubeSceneRendererTestApp.h
 * @brief Application wrapper for FCubeSceneRendererTest
 * 
 * This application integrates FCubeSceneRendererTest into the Application framework,
 * allowing it to be run as a standalone application with proper RHI device initialization.
 * 
 * Demonstrates the complete rendering pipeline:
 * - Scene management (Renderer::FScene)
 * - Primitive registration (FPrimitiveSceneProxy, FPrimitiveSceneInfo)
 * - View setup (FViewInfo, FSceneViewFamily)
 * - Visibility culling (frustum, distance)
 * - Draw command generation (FMeshDrawCommand)
 * - Render queue execution (FRenderQueue)
 */

#include "Core/Application.h"
#include "Core/Templates/UniquePtr.h"

namespace MonsterEngine
{
    class FCubeSceneRendererTest;
}

namespace MonsterRender
{

/**
 * @class CubeSceneRendererTestApp
 * @brief Application wrapper for FCubeSceneRendererTest
 * 
 * Provides the Application framework integration for the cube scene renderer test,
 * handling window creation, RHI device initialization, and main loop management.
 */
class CubeSceneRendererTestApp : public Application
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    CubeSceneRendererTestApp();
    virtual ~CubeSceneRendererTestApp();

    // ========================================================================
    // Application Interface
    // ========================================================================

    /**
     * Called when the application is initialized
     * Initializes the FCubeSceneRendererTest with the RHI device
     */
    virtual void onInitialize() override;

    /**
     * Called each frame to update the application
     * @param deltaTime Time since last frame in seconds
     */
    virtual void onUpdate(float32 deltaTime) override;

    /**
     * Called each frame to render the application
     * Executes the rendering pipeline through FCubeSceneRendererTest
     */
    virtual void onRender() override;

    /**
     * Called when the application is shutting down
     * Cleans up FCubeSceneRendererTest resources
     */
    virtual void onShutdown() override;

    /**
     * Handle window resize events
     * @param width New window width
     * @param height New window height
     */
    virtual void onWindowResize(uint32 width, uint32 height) override;

protected:
    /** The cube scene renderer test instance */
    MonsterEngine::FCubeSceneRendererTest* m_cubeSceneTest;

    /** Window dimensions */
    uint32 m_windowWidth;
    uint32 m_windowHeight;

    /** Frame timing */
    float32 m_totalTime;
    float32 m_frameTime;
    uint32 m_frameCount;

    /** Statistics display interval */
    float32 m_statsDisplayTimer;
    static constexpr float32 STATS_DISPLAY_INTERVAL = 2.0f;
};

/**
 * Factory function to create CubeSceneRendererTestApp
 * @return Unique pointer to the created application
 */
MonsterEngine::TUniquePtr<Application> CreateCubeSceneRendererTestApp();

} // namespace MonsterRender
