#include "Core/Application.h"
#include "TriangleRenderer.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"

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
        
        // Wait for GPU to finish all work before destroying resources
        auto* device = getEngine()->getRHIDevice();
        if (device) {
            device->waitForIdle();
        }
        
        m_triangleRenderer.reset();
    }
    
    void onUpdate(float32 deltaTime) override {
        // Update logic here
        // For this simple demo, we don't need much updating
    }
    
    void onRender() override {
        MR_LOG_INFO("===== onRender() STARTED =====");
        
        auto* device = getEngine()->getRHIDevice();
        MR_LOG_INFO("Device: " + std::string(device ? "VALID" : "NULL"));
        if (!device) return;
        
        MR_LOG_INFO("TriangleRenderer: " + std::string(m_triangleRenderer ? "VALID" : "NULL"));
        if (!m_triangleRenderer) return;
        
        auto* vulkanDevice = static_cast<MonsterRender::RHI::Vulkan::VulkanDevice*>(device);
        auto* context = vulkanDevice->getCommandListContext();
        MR_LOG_INFO("Context: " + std::string(context ? "VALID" : "NULL"));
        if (!context) return;
        
        auto* cmdList = device->getImmediateCommandList();
        MR_LOG_INFO("CmdList: " + std::string(cmdList ? "VALID" : "NULL"));
        if (!cmdList) {
            return;
        }
        
        MR_LOG_INFO("Step 1: prepareForNewFrame");
        context->prepareForNewFrame();
        
        MR_LOG_INFO("Step 2: cmdList->begin()");
        cmdList->begin();
        
        MR_LOG_INFO("Step 3: setRenderTargets");
        TArray<TSharedPtr<RHI::IRHITexture>> renderTargets;
        cmdList->setRenderTargets(TSpan<TSharedPtr<RHI::IRHITexture>>(renderTargets), nullptr);
        
        MR_LOG_INFO("Step 4: render");
        m_triangleRenderer->render(cmdList);
        
        MR_LOG_INFO("Step 5: endRenderPass");
        cmdList->endRenderPass();
        
        MR_LOG_INFO("Step 6: cmdList->end()");
        cmdList->end();
        
        MR_LOG_INFO("Step 7: present");
        device->present();
        
        MR_LOG_INFO("===== onRender() COMPLETED =====");
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
// NOTE: Commented out to use CubeApplication instead
/*
TUniquePtr<Application> MonsterRender::createApplication() {
    return MakeUnique<TriangleApplication>();
}
*/
