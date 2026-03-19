// Copyright Monster Engine. All Rights Reserved.

/**
 * Main Test Entry Point
 * 
 * This file provides a unified entry point for all MonsterEngine tests.
 * It runs comprehensive tests for:
 * - Command List Pool
 * - Parallel Translator
 * - Vulkan Parallel Rendering
 * 
 * Usage:
 *   MonsterEngine.exe --run-tests
 */

#include "Core/Log.h"
#include "Core/CoreMinimal.h"
#include "Tests/TestCommandListPool.h"
#include "Tests/TestParallelTranslator.h"
#include "Tests/TestVulkanParallelRendering.h"
#include <iostream>

using namespace MonsterEngine::Tests;

/**
 * Print test suite header
 */
void PrintTestHeader() {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("  MonsterEngine Test Suite");
    MR_LOG_INFO("  Parallel Rendering System Tests");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("");
}

/**
 * Print test suite footer with results
 */
void PrintTestFooter(bool allPassed) {
    MR_LOG_INFO("");
    MR_LOG_INFO("========================================");
    if (allPassed) {
        MR_LOG_INFO("  ALL TESTS PASSED!");
        MR_LOG_INFO("  Parallel rendering system validated");
    } else {
        MR_LOG_ERROR("  SOME TESTS FAILED!");
        MR_LOG_ERROR("  Please check the logs above");
    }
    MR_LOG_INFO("========================================");
}

/**
 * Run all tests
 * 
 * @return True if all tests passed, false otherwise
 */
bool RunAllTests() {
    bool allPassed = true;
    
    try {
        // Test 1: Command List Pool
        MR_LOG_INFO(">>> Running Command List Pool Tests...");
        MR_LOG_INFO("");
        RunCommandListPoolTests();
        MR_LOG_INFO(">>> Command List Pool Tests: PASSED");
        MR_LOG_INFO("");
        
    } catch (const std::exception& e) {
        MR_LOG_ERROR(">>> Command List Pool Tests: FAILED");
        MR_LOG_ERROR(std::string(">>> Exception: ") + e.what());
        allPassed = false;
    }
    
    try {
        // Test 2: Parallel Translator
        MR_LOG_INFO(">>> Running Parallel Translator Tests...");
        MR_LOG_INFO("");
        RunParallelTranslatorTests();
        MR_LOG_INFO(">>> Parallel Translator Tests: PASSED");
        MR_LOG_INFO("");
        
    } catch (const std::exception& e) {
        MR_LOG_ERROR(">>> Parallel Translator Tests: FAILED");
        MR_LOG_ERROR(std::string(">>> Exception: ") + e.what());
        allPassed = false;
    }
    
    try {
        // Test 3: Vulkan Parallel Rendering
        MR_LOG_INFO(">>> Running Vulkan Parallel Rendering Tests...");
        MR_LOG_INFO("");
        RunVulkanParallelRenderingTests();
        MR_LOG_INFO(">>> Vulkan Parallel Rendering Tests: PASSED");
        MR_LOG_INFO("");
        
    } catch (const std::exception& e) {
        MR_LOG_ERROR(">>> Vulkan Parallel Rendering Tests: FAILED");
        MR_LOG_ERROR(std::string(">>> Exception: ") + e.what());
        allPassed = false;
    }
    
    return allPassed;
}

/**
 * Main entry point for all parallel rendering tests
 * 
 * This function can be called from the main application
 * to run the complete test suite.
 * 
 * @return 0 if all tests passed, 1 otherwise
 */
int RunAllParallelRenderingTests() {
    // Print header
    PrintTestHeader();
    
    // Run all tests
    bool allPassed = RunAllTests();
    
    // Print footer
    PrintTestFooter(allPassed);
    
    // Return exit code
    return allPassed ? 0 : 1;
}
