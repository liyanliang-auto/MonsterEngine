// Copyright Monster Engine. All Rights Reserved.
// ParallelRenderingTestMain - Main entry point for parallel rendering tests
//
// This provides a command-line interface to run parallel rendering tests
// with various configurations.
//
// Usage:
//   MonsterEngine.exe --test-parallel-rendering
//   MonsterEngine.exe --test-parallel-rendering --lists=8 --draws=100 --frames=3
//   MonsterEngine.exe --test-parallel-rendering --verbose
//
// Reference: UE5 command line parsing
// Engine/Source/Runtime/Core/Private/Misc/CommandLine.cpp

#include "Core/CoreMinimal.h"
#include "Core/Log.h"
#include "Core/FTaskGraph.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "RHI/RHI.h"
#include "Tests/TestParallelRendering.h"
#include <string>
#include <cstring>

using namespace MonsterEngine;
using namespace MonsterRender::RHI::Vulkan;

/**
 * Parse command line arguments for parallel rendering test
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @param outConfig Output configuration
 * @return True if parsing succeeded
 */
bool ParseCommandLineArgs(int argc, char* argv[], FParallelRenderingTestConfig& outConfig) {
    // Note: outConfig is already initialized with defaults from the struct definition
    // We only override values that are specified on the command line
    
    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        // Check for verbose flag
        if (arg == "--verbose" || arg == "-v") {
            outConfig.verbose = true;
            MR_LOG_INFO("Verbose mode enabled");
            continue;
        }
        
        // Check for no-timing flag
        if (arg == "--no-timing") {
            outConfig.enableTiming = false;
            MR_LOG_INFO("Timing disabled");
            continue;
        }
        
        // Check for no-validation flag
        if (arg == "--no-validation") {
            outConfig.enableValidation = false;
            MR_LOG_INFO("Validation disabled");
            continue;
        }
        
        // Parse key=value arguments
        size_t equalPos = arg.find('=');
        if (equalPos != std::string::npos) {
            std::string key = arg.substr(0, equalPos);
            std::string value = arg.substr(equalPos + 1);
            
            try {
                if (key == "--lists" || key == "-l") {
                    outConfig.numParallelLists = std::stoi(value);
                    MR_LOG_INFO("Number of parallel lists: " + value);
                }
                else if (key == "--draws" || key == "-d") {
                    outConfig.drawCallsPerList = std::stoi(value);
                    MR_LOG_INFO("Draw calls per list: " + value);
                }
                else if (key == "--frames" || key == "-f") {
                    outConfig.numFrames = std::stoi(value);
                    MR_LOG_INFO("Number of frames: " + value);
                }
                else {
                    MR_LOG_WARNING("Unknown argument: " + key);
                }
            }
            catch (const std::exception& e) {
                MR_LOG_ERROR("Failed to parse argument " + key + ": " + e.what());
                return false;
            }
        }
    }
    
    // Validate configuration
    if (outConfig.numParallelLists == 0) {
        MR_LOG_ERROR("Number of parallel lists must be > 0");
        return false;
    }
    
    if (outConfig.drawCallsPerList == 0) {
        MR_LOG_ERROR("Draw calls per list must be > 0");
        return false;
    }
    
    if (outConfig.numFrames == 0) {
        MR_LOG_ERROR("Number of frames must be > 0");
        return false;
    }
    
    return true;
}

/**
 * Print usage information
 */
void PrintUsage() {
    MR_LOG_INFO("Parallel Rendering Test - Usage:");
    MR_LOG_INFO("");
    MR_LOG_INFO("  MonsterEngine.exe --test-parallel-rendering [options]");
    MR_LOG_INFO("");
    MR_LOG_INFO("Options:");
    MR_LOG_INFO("  --lists=N, -l=N     Number of parallel command lists (default: 4)");
    MR_LOG_INFO("  --draws=N, -d=N     Draw calls per command list (default: 50)");
    MR_LOG_INFO("  --frames=N, -f=N    Number of frames to render (default: 1)");
    MR_LOG_INFO("  --verbose, -v       Enable verbose logging");
    MR_LOG_INFO("  --no-timing         Disable performance timing");
    MR_LOG_INFO("  --no-validation     Disable validation checks");
    MR_LOG_INFO("");
    MR_LOG_INFO("Examples:");
    MR_LOG_INFO("  MonsterEngine.exe --test-parallel-rendering");
    MR_LOG_INFO("  MonsterEngine.exe --test-parallel-rendering --lists=8 --draws=100");
    MR_LOG_INFO("  MonsterEngine.exe --test-parallel-rendering --frames=3 --verbose");
}

/**
 * Main entry point for parallel rendering test
 * 
 * This function requires a valid Vulkan device to be passed in.
 * It should be called from the main application after the engine is initialized.
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @param device Vulkan device to use for testing
 * @return 0 on success, 1 on failure
 */
int RunParallelRenderingTestMain(int argc, char* argv[], VulkanDevice* device) {
    if (!device) {
        MR_LOG_ERROR("Invalid Vulkan device - cannot run parallel rendering test");
        return 1;
    }
    
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("MonsterEngine - Parallel Rendering Test");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("");
    
    // Parse command line arguments
    FParallelRenderingTestConfig config;
    if (!ParseCommandLineArgs(argc, argv, config)) {
        PrintUsage();
        return 1;
    }
    
    // Initialize task graph if not already initialized
    // Note: We always try to initialize - the task graph will handle re-initialization
    MR_LOG_INFO("Initializing task graph system...");
    FTaskGraph::Initialize();
    MR_LOG_INFO("Task graph initialized with " + 
               std::to_string(FTaskGraph::GetNumWorkerThreads()) + " worker threads");
    
    // Run the parallel rendering integration example
    bool testPassed = false;
    try {
        MR_LOG_INFO("");
        testPassed = RunParallelRenderingIntegrationExample(device);
    }
    catch (const std::exception& e) {
        MR_LOG_ERROR("Exception during test: " + std::string(e.what()));
        testPassed = false;
    }
    
    // Print final result
    MR_LOG_INFO("");
    MR_LOG_INFO("========================================");
    if (testPassed) {
        MR_LOG_INFO("TEST PASSED");
        MR_LOG_INFO("Parallel rendering system validated successfully");
    } else {
        MR_LOG_ERROR("TEST FAILED");
        MR_LOG_ERROR("Please check the logs above for details");
    }
    MR_LOG_INFO("========================================");
    
    return testPassed ? 0 : 1;
}

/**
 * Overloaded version that initializes Vulkan device automatically
 * This is the entry point called from main.cpp
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @return 0 on success, 1 on failure
 */
int RunParallelRenderingTestMain(int argc, char** argv) {
    MR_LOG_INFO("Initializing Vulkan device for parallel rendering test...");
    
    // Create RHI device
    MonsterRender::RHI::RHICreateInfo createInfo;
    createInfo.preferredBackend = MonsterRender::RHI::ERHIBackend::Vulkan;
    createInfo.enableValidation = true;
    createInfo.enableDebugMarkers = true;
    createInfo.applicationName = "Parallel Rendering Integration Test";
    
    auto rhiDevice = MonsterRender::RHI::RHIFactory::createDevice(createInfo);
    if (!rhiDevice) {
        MR_LOG_ERROR("Failed to create RHI device - cannot run parallel rendering test");
        return 1;
    }
    
    // Cast to VulkanDevice
    VulkanDevice* device = static_cast<VulkanDevice*>(rhiDevice.get());
    if (!device) {
        MR_LOG_ERROR("Device is not a VulkanDevice - cannot run parallel rendering test");
        return 1;
    }
    
    // Call the main test function with the device
    int exitCode = RunParallelRenderingTestMain(argc, argv, device);
    
    // Cleanup is handled by unique_ptr destructor
    return exitCode;
}

/**
 * Check if parallel rendering test should be run
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @return True if test should be run
 */
bool ShouldRunParallelRenderingTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--test-parallel-rendering" || 
            arg == "--test-parallel" ||
            arg == "-tpr") {
            return true;
        }
    }
    return false;
}
