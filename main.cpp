#include "Core/Application.h"
#include "Core/Logging/Logging.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "CubeSceneApplication.h"
#include "Tests/CubeSceneRendererTest.h"

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

// Logging System Test Forward Declaration
void RunLoggingSystemTests();

// Math Library Test Forward Declaration
void RunMathLibraryTests();

// Container Test Forward Declaration
// Implementation in Source/Tests/ContainerTest.cpp
void RunContainerTests();

// Smart Pointer Test Forward Declaration
// Implementation in Source/Tests/SmartPointerTest.cpp
void RunSmartPointerTests();

// Entry point following UE5's application architecture
int main(int argc, char** argv) {
    using namespace MonsterRender;
    
    // ========================================================================
    // Initialize Logging System FIRST - before any other initialization
    // ========================================================================
    InitializeLogging(
        "MonsterEngine.log",    // Log file name
        true,                   // Enable console output
        true,                   // Enable debug output (OutputDebugString on Windows)
        true                    // Enable file output
    );
    
    MR_LOG(LogInit, Log, "Starting MonsterRender Engine");
    MR_LOG(LogInit, Log, "Command line arguments: %d", argc);
    
    // Check for test mode flag
    bool runMemoryTests = false;
    bool runTextureTests = false;
    bool runVirtualTextureTests = false;
    bool runVulkanMemoryTests = false;
    bool runVulkanResourceTests = false;
    bool runLoggingTests = false;
    bool runMathTests = false;
    bool runContainerTests = false;
    bool runSmartPointerTests = false;
    bool runAllTests = false;
    bool runCubeScene = false;  // Run CubeSceneApplication with lighting
    bool runCubeSceneTest = false;  // Run CubeSceneRendererTest (pipeline integration test)
    
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
        else if (strcmp(argv[i], "--test-logging") == 0 || strcmp(argv[i], "-tl") == 0) {
            runLoggingTests = true;
        }
        else if (strcmp(argv[i], "--test-math") == 0 || strcmp(argv[i], "-tmath") == 0) {
            runMathTests = true;
        }
        else if (strcmp(argv[i], "--test-container") == 0 || strcmp(argv[i], "-tc") == 0) {
            runContainerTests = true;
        }
        else if (strcmp(argv[i], "--test-smartptr") == 0 || strcmp(argv[i], "-tsp") == 0) {
            runSmartPointerTests = true;
        }
        else if (strcmp(argv[i], "--test-all") == 0 || strcmp(argv[i], "-ta") == 0) {
            runAllTests = true;
        }
        else if (strcmp(argv[i], "--cube-scene") == 0 || strcmp(argv[i], "-cs") == 0) {
            runCubeScene = true;
        }
        else if (strcmp(argv[i], "--cube-scene-test") == 0 || strcmp(argv[i], "-cst") == 0) {
            runCubeSceneTest = true;
        }
    }
    
    // Default behavior: don't run tests, run application
    if (!runMemoryTests && !runTextureTests && !runVirtualTextureTests && 
        !runVulkanMemoryTests && !runVulkanResourceTests && !runLoggingTests && 
        !runMathTests && !runContainerTests && !runSmartPointerTests && !runAllTests) {
        runAllTests = false;
    }
    
    // Run logging tests separately (can run standalone)
    if (runLoggingTests) {
        RunLoggingSystemTests();
        //return 0;
    }
    
    // Run math library tests
    if (runMathTests) {
        printf("\n");
        printf("==========================================");
        printf("  Math Library Tests");
        printf("==========================================");
        printf("\n");
        
        RunMathLibraryTests();
        return 0;
    }
    
    // Run container tests
    if (runContainerTests) {
        printf("Starting container tests...\n");
        fflush(stdout);
        printf("\n");
        RunContainerTests();
        printf("Container tests completed.\n");
        fflush(stdout);
        return 0;
    }
    
    // Run smart pointer tests
    if (runSmartPointerTests) {
        RunSmartPointerTests();
        return 0;
    }
    
    // Run tests if requested
    if (runMemoryTests || runTextureTests || runVirtualTextureTests || 
        runVulkanMemoryTests || runVulkanResourceTests || runMathTests || runContainerTests || runAllTests) {
        printf("\n");
        printf("==========================================");
        printf("  MonsterEngine Test Suite");
        printf("==========================================");
        printf("\n");
        
        // Run memory system tests
        if (runMemoryTests || runAllTests) {
            printf("======================================");
            printf("  Memory System Tests");
            printf("======================================");
            printf("");
            
            MemorySystemTest::runAllTests();
            
            printf("\n");
            printf("======================================");
            printf("  FMemory System Tests");
            printf("======================================");
            printf("");
            
            FMemorySystemTest::runFMemoryTests();
        }
        
        // Run texture streaming tests
        if (runTextureTests || runAllTests) {
            printf("\n\n");
            printf("======================================");
            printf("  Texture Streaming System Tests");
            printf("======================================");
            printf("");
            
            TextureStreamingSystemTest::runTextureStreamingTests();
        }
        
        // Run virtual texture system tests
        if (runVirtualTextureTests || runAllTests) {
            printf("\n\n");
            printf("==========================================");
            printf("  Virtual Texture System Tests");
            printf("==========================================");
            printf("");
            
            // Run basic tests
            VirtualTextureSystemTest::RunAllTests();
            
            // Run real-world scenario tests
            printf("\n");
            VirtualTextureSystemTest::RunRealWorldScenarioTests();
        }
        
        // Run Vulkan memory manager tests
        if (runVulkanMemoryTests || runAllTests) {
            printf("\n\n");
            printf("==========================================");
            printf("  Vulkan Memory Manager Tests");
            printf("==========================================");
            printf("");
            
            // Run all Vulkan memory tests
            VulkanMemoryManagerTest::RunAllTests();
        }
        
        // Run Vulkan resource manager tests
        if (runVulkanResourceTests || runAllTests) {
            printf("\n\n");
            printf("==========================================");
            printf("  Vulkan Resource Manager Tests");
            printf("==========================================");
            printf("");
            
            // Run all Vulkan resource management tests
            VulkanResourceManagerTest::RunAllTests();
        }
        
        printf("\n");
        printf("==========================================");
        printf("  All Tests Completed");
        printf("==========================================");
        printf("\n");
        
        // Exit after tests
        return 0;
    }
    
    // Run CubeSceneRendererTest if requested
    if (runCubeSceneTest) {
        MR_LOG(LogInit, Log, "Running CubeSceneRendererTest (pipeline integration test)...");
        
        // Create a simple application to get the RHI device
        TUniquePtr<Application> testApp = createApplication();
        if (!testApp) {
            MR_LOG(LogInit, Error, "Failed to create test application");
            return -1;
        }
        
        // Initialize the application to get the device
        if (!testApp->initialize()) {
            MR_LOG(LogInit, Error, "Failed to initialize test application");
            return -1;
        }
        
        // Get the RHI device from the application
        RHI::IRHIDevice* device = testApp->getDevice();
        if (!device) {
            MR_LOG(LogInit, Error, "Failed to get RHI device");
            return -1;
        }
        
        // Create and run the test
        MonsterEngine::FCubeSceneRendererTest cubeTest;
        cubeTest.SetCubeCount(1);
        cubeTest.SetWindowDimensions(1280, 720);
        
        if (!cubeTest.Initialize(device)) {
            MR_LOG(LogInit, Error, "Failed to initialize CubeSceneRendererTest");
            testApp->shutdown();
            return -1;
        }
        
        // Run the test
        bool testPassed = cubeTest.RunTest();
        MR_LOG(LogInit, Log, "CubeSceneRendererTest %s", testPassed ? "PASSED" : "FAILED");
        
        // Cleanup
        cubeTest.Shutdown();
        testApp->shutdown();
        
        ShutdownLogging();
        return testPassed ? 0 : -1;
    }
    
    // Create application instance
    TUniquePtr<Application> app;
    if (runCubeScene) {
        MR_LOG(LogInit, Log, "Running CubeSceneApplication with lighting...");
        app = MakeUnique<CubeSceneApplication>();
    } else {
        app = createApplication();
    }
    if (!app) {
        printf("Failed to create application");
        return -1;
    }
    
    // Run the application
    int32 exitCode = app->run();
    
    // Shutdown before destroying to ensure proper cleanup order
    app->shutdown();
    
    // Cleanup is handled by RAII
    app.reset();
    
    MR_LOG(LogExit, Log, "MonsterRender Engine shutting down with exit code: %d", exitCode);
    
    // Shutdown logging system LAST - after all other cleanup
    ShutdownLogging();
    
    return exitCode;
}