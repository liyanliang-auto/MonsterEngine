#include "Core/Application.h"
#include "Core/Log.h"

// Test Suite Forward Declarations
namespace MonsterRender {
    namespace MemorySystemTest {
        void runAllTests();  // Forward declaration
    }
    namespace FMemorySystemTest {
        void runFMemoryTests();  // Forward declaration
    }
    namespace TextureStreamingSystemTest {
        void runTextureStreamingTests();  // Forward declaration
    }
}

// Entry point following UE5's application architecture
int main(int argc, char** argv) {
    using namespace MonsterRender;
    
    MR_LOG_INFO("Starting MonsterRender Engine");
    
    // Set log level for detailed output
    Logger::getInstance().setMinLevel(ELogLevel::Debug);
    
    // Check for test mode flag
    bool runMemoryTests = true;
    bool runTextureTests = true;
    bool runAllTests = true;
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--test-memory") == 0 || strcmp(argv[i], "-tm") == 0) {
            runMemoryTests = true;
        }
        else if (strcmp(argv[i], "--test-texture") == 0 || strcmp(argv[i], "-tt") == 0) {
            runTextureTests = true;
        }
        else if (strcmp(argv[i], "--test-all") == 0 || strcmp(argv[i], "-ta") == 0) {
            runAllTests = true;
        }
    }
    
    // Default behavior: run all tests
    if (!runMemoryTests && !runTextureTests && !runAllTests) {
        runAllTests = true;
    }
    
    // Run tests if requested
    if (runMemoryTests || runTextureTests || runAllTests) {
        MR_LOG_INFO("\n");
        MR_LOG_INFO("==========================================");
        MR_LOG_INFO("  MonsterEngine Test Suite");
        MR_LOG_INFO("==========================================");
        MR_LOG_INFO("\n");
        
        // Run memory system tests
        if (runMemoryTests || runAllTests) {
            MR_LOG_INFO("======================================");
            MR_LOG_INFO("  Memory System Tests");
            MR_LOG_INFO("======================================");
            MR_LOG_INFO("");
            
            MemorySystemTest::runAllTests();
            
            MR_LOG_INFO("\n");
            MR_LOG_INFO("======================================");
            MR_LOG_INFO("  FMemory System Tests");
            MR_LOG_INFO("======================================");
            MR_LOG_INFO("");
            
            FMemorySystemTest::runFMemoryTests();
        }
        
        // Run texture streaming tests
        if (runTextureTests || runAllTests) {
            MR_LOG_INFO("\n\n");
            MR_LOG_INFO("======================================");
            MR_LOG_INFO("  Texture Streaming System Tests");
            MR_LOG_INFO("======================================");
            MR_LOG_INFO("");
            
            TextureStreamingSystemTest::runTextureStreamingTests();
        }
        
        MR_LOG_INFO("\n");
        MR_LOG_INFO("==========================================");
        MR_LOG_INFO("  All Tests Completed");
        MR_LOG_INFO("==========================================");
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