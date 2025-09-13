#include "Core/CoreMinimal.h"
#include "Engine.h"
#include "TriangleRenderer.h"

using namespace MonsterRender;

int main() {
    MR_LOG_INFO("Starting MonsterRender Engine Application");
    
    // Set log level for more detailed output  
    Logger::getInstance().setMinLevel(ELogLevel::Debug);
    
    // Create engine
    Engine engine;
    
    // Setup RHI create info
    RHI::RHICreateInfo rhiCreateInfo;
    rhiCreateInfo.preferredBackend = RHI::ERHIBackend::Vulkan;
    rhiCreateInfo.enableValidation = true;
    rhiCreateInfo.enableDebugMarkers = true;
    rhiCreateInfo.applicationName = "MonsterRender Triangle Demo";
    rhiCreateInfo.windowWidth = 1920;
    rhiCreateInfo.windowHeight = 1080;
    // Note: windowHandle is nullptr for now - we'll add proper window creation later
    
    // Initialize engine
    if (!engine.initialize(rhiCreateInfo)) {
        MR_LOG_ERROR("Failed to initialize engine");
        return -1;
    }
    
    // Get RHI device
    auto* device = engine.getRHIDevice();
    if (!device) {
        MR_LOG_ERROR("Failed to get RHI device");
        return -1;
    }
    
    // Create triangle renderer
    TriangleRenderer triangleRenderer;
    if (!triangleRenderer.initialize(device)) {
        MR_LOG_ERROR("Failed to initialize triangle renderer");
        return -1;
    }
    
    MR_LOG_INFO("Triangle renderer initialized successfully");
    
    // Basic rendering loop (just one frame for now)
    {
        MR_LOG_INFO("Rendering triangle...");
        
        // Get immediate command list
        auto* cmdList = device->getImmediateCommandList();
        if (cmdList) {
            cmdList->begin();
            
            // Clear screen (when render targets are implemented)
            // cmdList->clearRenderTarget(...);
            
            // Render triangle
            triangleRenderer.render(cmdList);
            
            cmdList->end();
            
            // Present frame
            device->present();
            
            MR_LOG_INFO("Frame rendered and presented");
        } else {
            MR_LOG_WARNING("No immediate command list available");
        }
    }
    
    // Wait for any GPU work to complete
    device->waitForIdle();
    
    MR_LOG_INFO("MonsterRender Triangle Demo completed successfully");
    return 0;
}