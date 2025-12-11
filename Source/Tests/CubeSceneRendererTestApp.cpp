// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CubeSceneRendererTestApp.cpp
 * @brief Implementation of Application wrapper for FCubeSceneRendererTest
 */

#include "Tests/CubeSceneRendererTestApp.h"
#include "Tests/CubeSceneRendererTest.h"
#include "Core/Logging/LogMacros.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Platform/OpenGL/OpenGLDefinitions.h"

namespace MonsterRender
{

using namespace MonsterEngine;

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogCubeSceneTestApp, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

CubeSceneRendererTestApp::CubeSceneRendererTestApp()
    : Application()
    , m_cubeSceneTest(nullptr)
    , m_windowWidth(1280)
    , m_windowHeight(720)
    , m_totalTime(0.0f)
    , m_frameTime(0.0f)
    , m_frameCount(0)
    , m_statsDisplayTimer(0.0f)
{
    // Application name is set via ApplicationConfig in base class
}

CubeSceneRendererTestApp::~CubeSceneRendererTestApp()
{
    // Ensure cleanup happens
    if (m_cubeSceneTest)
    {
        m_cubeSceneTest->Shutdown();
        delete m_cubeSceneTest;
        m_cubeSceneTest = nullptr;
    }
}

// ============================================================================
// Application Interface
// ============================================================================

void CubeSceneRendererTestApp::onInitialize()
{
    MR_LOG(LogCubeSceneTestApp, Log, "Initializing CubeSceneRendererTestApp...");
    
    // Get RHI device from engine
    RHI::IRHIDevice* device = getEngine() ? getEngine()->getRHIDevice() : nullptr;
    if (!device)
    {
        MR_LOG(LogCubeSceneTestApp, Error, "Failed to get RHI device from engine");
        return;
    }
    
    // Create and initialize the cube scene renderer test
    m_cubeSceneTest = new FCubeSceneRendererTest();
    if (!m_cubeSceneTest)
    {
        MR_LOG(LogCubeSceneTestApp, Error, "Failed to create FCubeSceneRendererTest");
        return;
    }
    
    // Initialize with RHI device
    if (!m_cubeSceneTest->Initialize(device))
    {
        MR_LOG(LogCubeSceneTestApp, Error, "Failed to initialize FCubeSceneRendererTest");
        delete m_cubeSceneTest;
        m_cubeSceneTest = nullptr;
        return;
    }
    
    // Set initial window dimensions
    m_cubeSceneTest->SetWindowDimensions(m_windowWidth, m_windowHeight);
    
    MR_LOG(LogCubeSceneTestApp, Log, "CubeSceneRendererTestApp initialized successfully");
    MR_LOG(LogCubeSceneTestApp, Log, "  Window: %ux%u", m_windowWidth, m_windowHeight);
    MR_LOG(LogCubeSceneTestApp, Log, "  RHI Backend: %s", 
           RHI::GetRHIBackendName(device->getRHIBackend()));
}

void CubeSceneRendererTestApp::onUpdate(float32 deltaTime)
{
    if (!m_cubeSceneTest)
    {
        return;
    }
    
    // Update timing
    m_totalTime += deltaTime;
    m_frameTime = deltaTime;
    m_frameCount++;
    
    // Update the cube scene test
    m_cubeSceneTest->Update(deltaTime);
    
    // Display statistics periodically
    m_statsDisplayTimer += deltaTime;
    if (m_statsDisplayTimer >= STATS_DISPLAY_INTERVAL)
    {
        m_statsDisplayTimer = 0.0f;
        
        // Calculate FPS
        float fps = (deltaTime > 0.0f) ? (1.0f / deltaTime) : 0.0f;
        
        MR_LOG(LogCubeSceneTestApp, Log, "Stats: FPS=%.1f, FrameTime=%.2fms, TotalTime=%.1fs",
               fps, deltaTime * 1000.0f, m_totalTime);
    }
}

void CubeSceneRendererTestApp::onRender()
{
    if (!m_cubeSceneTest)
    {
        return;
    }
    
    // Get RHI device
    RHI::IRHIDevice* device = getEngine() ? getEngine()->getRHIDevice() : nullptr;
    if (!device)
    {
        return;
    }
    
    RHI::ERHIBackend backend = device->getRHIBackend();
    
    if (backend == RHI::ERHIBackend::OpenGL)
    {
        // OpenGL rendering path
        using namespace MonsterEngine::OpenGL;
        
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
        
        // Get command list and render
        RHI::IRHICommandList* cmdList = device->getImmediateCommandList();
        if (cmdList)
        {
            m_cubeSceneTest->Render(cmdList);
        }
        
        // Swap buffers
        getWindow()->swapBuffers();
    }
    else
    {
        // Vulkan rendering path
        auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(device);
        auto* context = vulkanDevice->getCommandListContext();
        if (!context) return;
        
        RHI::IRHICommandList* cmdList = device->getImmediateCommandList();
        if (!cmdList) return;
        
        // Prepare for new frame
        context->prepareForNewFrame();
        
        // Begin command recording
        cmdList->begin();
        
        // Set render targets (swapchain)
        TArray<TSharedPtr<RHI::IRHITexture>> renderTargets;
        cmdList->setRenderTargets(TSpan<TSharedPtr<RHI::IRHITexture>>(renderTargets), nullptr);
        
        // Render cube scene
        m_cubeSceneTest->Render(cmdList);
        
        // End render pass
        cmdList->endRenderPass();
        
        // End command recording
        cmdList->end();
        
        // Present frame
        device->present();
    }
}

void CubeSceneRendererTestApp::onShutdown()
{
    MR_LOG(LogCubeSceneTestApp, Log, "Shutting down CubeSceneRendererTestApp...");
    
    if (m_cubeSceneTest)
    {
        m_cubeSceneTest->Shutdown();
        delete m_cubeSceneTest;
        m_cubeSceneTest = nullptr;
    }
    
    MR_LOG(LogCubeSceneTestApp, Log, "CubeSceneRendererTestApp shutdown complete");
    MR_LOG(LogCubeSceneTestApp, Log, "  Total frames: %u", m_frameCount);
    MR_LOG(LogCubeSceneTestApp, Log, "  Total time: %.2f seconds", m_totalTime);
}

void CubeSceneRendererTestApp::onWindowResize(uint32 width, uint32 height)
{
    // Call base class
    Application::onWindowResize(width, height);
    
    // Update dimensions
    m_windowWidth = static_cast<uint32>(width);
    m_windowHeight = static_cast<uint32>(height);
    
    // Update cube scene test
    if (m_cubeSceneTest)
    {
        m_cubeSceneTest->SetWindowDimensions(m_windowWidth, m_windowHeight);
    }
    
    MR_LOG(LogCubeSceneTestApp, Log, "Window resized to %ux%u", m_windowWidth, m_windowHeight);
}

// ============================================================================
// Factory Function
// ============================================================================

MonsterEngine::TUniquePtr<Application> CreateCubeSceneRendererTestApp()
{
    return MonsterEngine::MakeUnique<CubeSceneRendererTestApp>();
}

} // namespace MonsterRender
