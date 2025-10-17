#include "Core/Application.h"
#include "TriangleRenderer.h"
#include "Core/Log.h"

using namespace MonsterRender;

/**
 * Triangle Demo Application - demonstrates basic window and rendering functionality
 * Following UE5's application pattern
 */
class TriangleApplication : public Application {
public:
    TriangleApplication() 
        : Application(createConfig()) {
    }
    
private:
    static ApplicationConfig createConfig() {
        ApplicationConfig config;
        config.name = "MonsterRender Triangle Demo";
        config.version = "1.0.0";
        config.windowProperties.title = config.name;
        config.windowProperties.width = 1280;
        config.windowProperties.height = 720;
        config.windowProperties.resizable = true;
        config.enableValidation = true;
        config.enableDebugMarkers = true;
        return config;
    }
    
    // Application lifecycle
    void onInitialize() override {
        MR_LOG_INFO("Initializing Triangle Demo Application");
        
        // Get RHI device
        auto* device = getEngine()->getRHIDevice();
        if (!device) {
            MR_LOG_ERROR("Failed to get RHI device");
            requestExit();
            return;
        }
        
        // Create triangle renderer
        m_triangleRenderer = MakeUnique<TriangleRenderer>();
        if (!m_triangleRenderer->initialize(device)) {
            MR_LOG_ERROR("Failed to initialize triangle renderer");
            requestExit();
            return;
        }
        
        MR_LOG_INFO("Triangle Demo Application initialized successfully");
    }
    
    void onShutdown() override {
        MR_LOG_INFO("Shutting down Triangle Demo Application");
        m_triangleRenderer.reset();
    }
    
    void onUpdate(float32 deltaTime) override {
        // Update logic here
        // For this simple demo, we don't need much updating
    }
    
    void onRender() override {
        auto* device = getEngine()->getRHIDevice();
        if (!device || !m_triangleRenderer) {
            return;
        }
        
        // Get immediate command list
        auto* cmdList = device->getImmediateCommandList();
        if (!cmdList) {
            return;
        }
        
        // Begin frame rendering
        cmdList->begin();
        
        // Set render targets (begins render pass and clears to black)
        TArray<TSharedPtr<RHI::IRHITexture>> renderTargets; // Empty = use default swapchain
        cmdList->setRenderTargets(TSpan<TSharedPtr<RHI::IRHITexture>>(renderTargets), nullptr);
        
        // Render the triangle
        m_triangleRenderer->render(cmdList);
        
        // End command recording
        cmdList->end();
        
        // Present frame
        device->present();
    }
    
    void onWindowResize(uint32 width, uint32 height) override {
        MR_LOG_INFO("Window resized to " + std::to_string(width) + "x" + std::to_string(height));
        // Handle resize logic here
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
        MR_LOG_INFO("Mouse button pressed at (" + 
                   std::to_string(position.x) + ", " + std::to_string(position.y) + ")");
    }
    
    void onMouseMoved(const MousePosition& position) override {
        // Handle mouse movement here
        // Note: This is called very frequently, so avoid heavy logging
    }
    
private:
    TUniquePtr<TriangleRenderer> m_triangleRenderer;
};

// Application creation function - required by the application framework
TUniquePtr<Application> MonsterRender::createApplication() {
    return MakeUnique<TriangleApplication>();
}
