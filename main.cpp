#include "Core/Application.h"
#include "Core/Log.h"

// Memory System Test Suite
namespace MonsterRender {
    namespace MemorySystemTest {
        void runAllTests();  // Forward declaration
    }
    namespace FMemorySystemTest {
        void runFMemoryTests();  // Forward declaration
    }
}

// Entry point following UE5's application architecture
int main(int argc, char** argv) {
    using namespace MonsterRender;
    
    MR_LOG_INFO("Starting MonsterRender Engine");
    
    // Set log level for detailed output
    Logger::getInstance().setMinLevel(ELogLevel::Debug);
    
    // Check for test mode flag
    bool runTests = true;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--test-memory") == 0 || strcmp(argv[i], "-tm") == 0) {
            runTests = true;
            break;
        }
    }
    
    // Run memory system tests if requested
    if (runTests) {
        MR_LOG_INFO("\n");
        MR_LOG_INFO("======================================");
        MR_LOG_INFO("  Memory System Test Mode");
        MR_LOG_INFO("======================================");
        MR_LOG_INFO("\n");
        
        // Run original memory system tests
        MemorySystemTest::runAllTests();
        
        MR_LOG_INFO("\n\n");
        MR_LOG_INFO("======================================");
        MR_LOG_INFO("  FMemory System Tests");
        MR_LOG_INFO("======================================");
        MR_LOG_INFO("\n");
        
        // Run FMemory system tests
        FMemorySystemTest::runFMemoryTests();
        
        MR_LOG_INFO("\n");
        MR_LOG_INFO("======================================");
        MR_LOG_INFO("  All Tests Completed");
        MR_LOG_INFO("======================================");
        MR_LOG_INFO("\n");
        
        // Exit after tests
        return 0;
    }
    
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