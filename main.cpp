#include "Core/CoreMinimal.h"
#include "Engine.h"

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
    rhiCreateInfo.applicationName = "MonsterRender Test Application";
    rhiCreateInfo.windowWidth = 1920;
    rhiCreateInfo.windowHeight = 1080;
    // Note: windowHandle is nullptr for now - we'll add proper window creation later
    
    // Initialize engine
    if (!engine.initialize(rhiCreateInfo)) {
        MR_LOG_ERROR("Failed to initialize engine");
        return -1;
    }
    
    // Run engine (for now just initializes and shuts down)
    engine.run();
    
    MR_LOG_INFO("MonsterRender Engine Application completed successfully");
    return 0;
}