#include "Core/Application.h"
#include "Core/Log.h"

// Entry point following UE5's application architecture
int main() {
    using namespace MonsterRender;
    
    MR_LOG_INFO("Starting MonsterRender Engine");
    
    // Set log level for detailed output
    Logger::getInstance().setMinLevel(ELogLevel::Debug);
    
    // Create application instance
    auto app = createApplication();
    if (!app) {
        MR_LOG_ERROR("Failed to create application");
        return -1;
    }
    
    // Run the application
    int32 exitCode = app->run();
    
    // Cleanup is handled by RAII
    app.reset();
    
    MR_LOG_INFO("MonsterRender Engine shutting down with exit code: " + std::to_string(exitCode));
    return exitCode;
}