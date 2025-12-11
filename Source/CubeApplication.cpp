#include "Core/Application.h"
#include "CubeRenderer.h"
#include "Core/Log.h"
#include "RHI/RHI.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/OpenGL/OpenGLDevice.h"
#include "Platform/OpenGL/OpenGLContext.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Platform/OpenGL/OpenGLDefinitions.h"

using namespace MonsterRender;

/**
 * Cube Demo Application - demonstrates textured 3D cube with rotation
 * 
 * Features:
 * - 3D cube rendering with texture mapping
 * - MVP (Model-View-Projection) transformation
 * - Rotation animation
 * - Depth testing
 * - Multiple texture sampling
 * 
 * Reference: LearnOpenGL Coordinate Systems tutorial
 * https://learnopengl-cn.github.io/01%20Getting%20started/08%20Coordinate%20Systems/
 */
class CubeApplication : public Application {
public:
    CubeApplication() 
        : Application(createConfig()) {
    }
    
private:
    static ApplicationConfig createConfig() {
        ApplicationConfig config;
        config.name = "MonsterRender Textured Rotating Cube Demo";
        config.version = "1.0.0";
        config.windowProperties.title = config.name;
        config.windowProperties.width = 800;   // Match LearnOpenGL window size
        config.windowProperties.height = 600;
        config.windowProperties.resizable = true;
        // Re-enable validation for debugging
        config.enableValidation = true;
        config.enableDebugMarkers = true;
        // Select RHI backend: None = auto-select, Vulkan, OpenGL
        // Use None for auto-selection (Vulkan preferred on Windows)
        // Change to RHI::ERHIBackend::OpenGL to test OpenGL backend
        config.preferredBackend = RHI::ERHIBackend::Vulkan;
        return config;
    }
    
    // Application lifecycle
    void onInitialize() override {
        MR_LOG_INFO("=================================================");
        MR_LOG_INFO("Initializing Textured Rotating Cube Application");
        MR_LOG_INFO("=================================================");
        
        // Get RHI device
        auto* device = getEngine()->getRHIDevice();
        if (!device) {
            MR_LOG_ERROR("Failed to get RHI device");
            requestExit();
            return;
        }
        
        // Create cube renderer
        m_cubeRenderer = MakeUnique<CubeRenderer>();
        if (!m_cubeRenderer->initialize(device)) {
            MR_LOG_ERROR("Failed to initialize cube renderer");
            requestExit();
            return;
        }
        
        // Set initial window dimensions
        m_cubeRenderer->setWindowDimensions(
            getEngine()->getWindowWidth(), 
            getEngine()->getWindowHeight()
        );
        
        MR_LOG_INFO("Cube Application initialized successfully");
        MR_LOG_INFO("Press ESC to exit");
        MR_LOG_INFO("=================================================");
    }
    
    void onShutdown() override {
        MR_LOG_INFO("Shutting down Cube Application");
        
        // Wait for GPU to finish all work before destroying resources
        auto* device = getEngine()->getRHIDevice();
        if (device) {
            device->waitForIdle();
        }
        
        m_cubeRenderer.reset();
        
        MR_LOG_INFO("Cube Application shutdown complete");
    }
    
    void onUpdate(float32 deltaTime) override {
        // Update cube animation
        if (m_cubeRenderer) {
            m_cubeRenderer->update(deltaTime);
        }
    }
    
    void onRender() override {
        auto* device = getEngine()->getRHIDevice();
        if (!device) return;
        
        if (!m_cubeRenderer) return;
        
        RHI::ERHIBackend backend = device->getBackendType();
        
        if (backend == RHI::ERHIBackend::OpenGL) {
            // OpenGL rendering path
            using namespace MonsterEngine::OpenGL;
            
            // Debug: count frames
            static int frameCount = 0;
            if (frameCount++ % 60 == 0) {
                MR_LOG_INFO("OpenGL frame: " + std::to_string(frameCount));
            }
            
            // Use GLFW's OpenGL context (already current from window creation)
            // Bind default framebuffer (backbuffer)
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            
            // Set viewport to window size
            int width = getEngine()->getWindowWidth();
            int height = getEngine()->getWindowHeight();
            glViewport(0, 0, width, height);
            
            // Clear the screen with a dark teal color
            glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            
            // Check for OpenGL errors
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                MR_LOG_ERROR("OpenGL error after clear: " + std::to_string(err));
            }
            
            // Enable depth testing
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            
            auto* cmdList = device->getImmediateCommandList();
            if (cmdList) {
                // Render cube using OpenGL command list
                m_cubeRenderer->render(cmdList, m_lastFrameTime);
            }
            
            // Present frame using GLFW swap buffers
            getWindow()->swapBuffers();
        }
        else {
            // Vulkan rendering path
            auto* vulkanDevice = static_cast<MonsterRender::RHI::Vulkan::VulkanDevice*>(device);
            auto* context = vulkanDevice->getCommandListContext();
            if (!context) return;
            
            auto* cmdList = device->getImmediateCommandList();
            if (!cmdList) return;
            
            // Prepare for new frame
            context->prepareForNewFrame();
            
            // Begin command recording
            cmdList->begin();
            
            // Set render targets (swapchain)
            TArray<TSharedPtr<RHI::IRHITexture>> renderTargets;
            cmdList->setRenderTargets(TSpan<TSharedPtr<RHI::IRHITexture>>(renderTargets), nullptr);
            
            // Render cube
            m_cubeRenderer->render(cmdList, m_lastFrameTime);
            
            // End render pass
            cmdList->endRenderPass();
            
            // End command recording
            cmdList->end();
            
            // Present frame
            device->present();
        }
    }
    
    void onWindowResize(uint32 width, uint32 height) override {
        MR_LOG_INFO("Window resized to " + std::to_string(width) + "x" + std::to_string(height));
        
        if (m_cubeRenderer) {
            m_cubeRenderer->setWindowDimensions(width, height);
        }
    }
    
    void onKeyPressed(EKey key) override {
        switch (key) {
            case EKey::Escape:
                MR_LOG_INFO("Escape key pressed - exiting application");
                requestExit();
                break;
                
            case EKey::Space:
                MR_LOG_INFO("Space key pressed");
                break;
                
            default:
                break;
        }
    }
    
    void onKeyReleased(EKey key) override {
        // Handle key releases here
    }
    
    void onMouseButtonPressed(EKey button, const MousePosition& position) override {
        MR_LOG_DEBUG("Mouse button pressed at (" + 
                   std::to_string(position.x) + ", " + std::to_string(position.y) + ")");
    }
    
    void onMouseMoved(const MousePosition& position) override {
        // Handle mouse movement here
        // Note: Called very frequently, avoid heavy logging
    }
    
private:
    TUniquePtr<CubeRenderer> m_cubeRenderer;
    float32 m_lastFrameTime = 0.016f;  // ~60 FPS initial estimate
};

// Application creation function - required by the application framework
// CubeRenderer uses:
//   - binding 0: uniform buffer (MVP matrices)
//   - binding 1: sampler2D texture1 (container.jpg)
//   - binding 2: sampler2D texture2 (awesomeface.png)
// NOTE: To use CubeSceneApplication with lighting, see CubeSceneApplication.cpp
TUniquePtr<Application> MonsterRender::createApplication() {
    return MakeUnique<CubeApplication>();
}
