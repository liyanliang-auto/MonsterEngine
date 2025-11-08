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
    namespace VirtualTextureSystemTest {
        void RunAllTests();  // Forward declaration
        void RunRealWorldScenarioTests();  // Forward declaration
    }
    namespace VulkanMemoryManagerTest {
        void RunBasicTests();  // Forward declaration
        void RunScenarioTests();  // Forward declaration
        void RunAllTests();  // Forward declaration
    }
    namespace VulkanResourceManagerTest {
        void RunBasicTests();  // Forward declaration
        void RunScenarioTests();  // Forward declaration
        void RunAllTests();  // Forward declaration
    }
}

// Entry point following UE5's application architecture
int main(int argc, char** argv) {
    using namespace MonsterRender;
    
    MR_LOG(Core, Display, TEXT("Starting MonsterRender Engine"));
    
    // 设置日志级别以获得详细输出
    // Set log level for detailed output - Verbose级别会显示更多调试信息
    LogCategoryCore.SetVerbosity(ELogVerbosity::Verbose);
    
    // Check for test mode flag
    bool runMemoryTests = false;
    bool runTextureTests = false;
    bool runVirtualTextureTests = false;
    bool runVulkanMemoryTests = false;
    bool runVulkanResourceTests = false;
    bool runAllTests = false;
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--test-memory") == 0 || strcmp(argv[i], "-tm") == 0) {
            runMemoryTests = true;
        }
        else if (strcmp(argv[i], "--test-texture") == 0 || strcmp(argv[i], "-tt") == 0) {
            runTextureTests = true;
        }
        else if (strcmp(argv[i], "--test-virtual-texture") == 0 || strcmp(argv[i], "-tvt") == 0) {
            runVirtualTextureTests = true;
        }
        else if (strcmp(argv[i], "--test-vulkan-memory") == 0 || strcmp(argv[i], "-tvm") == 0) {
            runVulkanMemoryTests = true;
        }
        else if (strcmp(argv[i], "--test-vulkan-resource") == 0 || strcmp(argv[i], "-tvr") == 0) {
            runVulkanResourceTests = true;
        }
        else if (strcmp(argv[i], "--test-all") == 0 || strcmp(argv[i], "-ta") == 0) {
            runAllTests = true;
        }
    }
    
    // Default behavior: run all tests
    if (!runMemoryTests && !runTextureTests && !runVirtualTextureTests && 
        !runVulkanMemoryTests && !runVulkanResourceTests && !runAllTests) {
        runAllTests = false;
    }
    
    // Run tests if requested
    if (runMemoryTests || runTextureTests || runVirtualTextureTests || 
        runVulkanMemoryTests || runVulkanResourceTests || runAllTests) {
        MR_LOG(Core, Display, TEXT(""));
        MR_LOG(Core, Display, TEXT("=========================================="));
        MR_LOG(Core, Display, TEXT("  MonsterEngine Test Suite"));
        MR_LOG(Core, Display, TEXT("=========================================="));
        MR_LOG(Core, Display, TEXT(""));
        
        // Run memory system tests
        if (runMemoryTests || runAllTests) {
            MR_LOG(Memory, Display, TEXT("======================================"));
            MR_LOG(Memory, Display, TEXT("  Memory System Tests"));
            MR_LOG(Memory, Display, TEXT("======================================"));
            MR_LOG(Memory, Display, TEXT(""));
            
            MemorySystemTest::runAllTests();
            
            MR_LOG(Memory, Display, TEXT(""));
            MR_LOG(Memory, Display, TEXT("======================================"));
            MR_LOG(Memory, Display, TEXT("  FMemory System Tests"));
            MR_LOG(Memory, Display, TEXT("======================================"));
            MR_LOG(Memory, Display, TEXT(""));
            
            FMemorySystemTest::runFMemoryTests();
        }
        
        // Run texture streaming tests
        if (runTextureTests || runAllTests) {
            MR_LOG(Texture, Display, TEXT(""));
            MR_LOG(Texture, Display, TEXT(""));
            MR_LOG(Texture, Display, TEXT("======================================"));
            MR_LOG(Texture, Display, TEXT("  Texture Streaming System Tests"));
            MR_LOG(Texture, Display, TEXT("======================================"));
            MR_LOG(Texture, Display, TEXT(""));
            
            TextureStreamingSystemTest::runTextureStreamingTests();
        }
        
        // Run virtual texture system tests
        if (runVirtualTextureTests || runAllTests) {
            MR_LOG(Texture, Display, TEXT(""));
            MR_LOG(Texture, Display, TEXT(""));
            MR_LOG(Texture, Display, TEXT("=========================================="));
            MR_LOG(Texture, Display, TEXT("  Virtual Texture System Tests"));
            MR_LOG(Texture, Display, TEXT("=========================================="));
            MR_LOG(Texture, Display, TEXT(""));
            
            // Run basic tests
            VirtualTextureSystemTest::RunAllTests();
            
            // Run real-world scenario tests
            MR_LOG(Texture, Display, TEXT(""));
            VirtualTextureSystemTest::RunRealWorldScenarioTests();
        }
        
        // Run Vulkan memory manager tests
        if (runVulkanMemoryTests || runAllTests) {
            MR_LOG(Vulkan, Display, TEXT(""));
            MR_LOG(Vulkan, Display, TEXT(""));
            MR_LOG(Vulkan, Display, TEXT("=========================================="));
            MR_LOG(Vulkan, Display, TEXT("  Vulkan Memory Manager Tests"));
            MR_LOG(Vulkan, Display, TEXT("=========================================="));
            MR_LOG(Vulkan, Display, TEXT(""));
            
            // Run all Vulkan memory tests
            VulkanMemoryManagerTest::RunAllTests();
        }
        
        // Run Vulkan resource manager tests
        if (runVulkanResourceTests || runAllTests) {
            MR_LOG(Vulkan, Display, TEXT(""));
            MR_LOG(Vulkan, Display, TEXT(""));
            MR_LOG(Vulkan, Display, TEXT("=========================================="));
            MR_LOG(Vulkan, Display, TEXT("  Vulkan Resource Manager Tests"));
            MR_LOG(Vulkan, Display, TEXT("=========================================="));
            MR_LOG(Vulkan, Display, TEXT(""));
            
            // Run all Vulkan resource management tests
            VulkanResourceManagerTest::RunAllTests();
        }
        
        MR_LOG(Core, Display, TEXT(""));
        MR_LOG(Core, Display, TEXT("=========================================="));
        MR_LOG(Core, Display, TEXT("  All Tests Completed"));
        MR_LOG(Core, Display, TEXT("=========================================="));
        MR_LOG(Core, Display, TEXT(""));
        
        // Exit after tests
        return 0;
    }
    
    // Create application instance
    auto app = createApplication();
    if (!app) {
        MR_LOG(Core, Error, TEXT("Failed to create application"));
        return -1;
    }
    
    // Run the application
    int32 exitCode = app->run();
    
    // Cleanup is handled by RAII
    app.reset();
    
    MR_LOG(Core, Display, TEXT("MonsterRender Engine shutting down with exit code: %d"), exitCode);
    return exitCode;
}