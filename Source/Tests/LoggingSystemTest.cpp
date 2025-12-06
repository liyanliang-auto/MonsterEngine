/**
 * LoggingSystemTest.cpp
 * 
 * Test suite for the MonsterEngine logging system.
 * Demonstrates usage of the new UE5-style logging architecture.
 * 
 * Features tested:
 * - Log categories (DECLARE_LOG_CATEGORY_EXTERN / DEFINE_LOG_CATEGORY)
 * - Verbosity levels (Fatal, Error, Warning, Display, Log, Verbose, VeryVerbose)
 * - Compile-time and runtime filtering
 * - Multiple output devices (Console, File, Debug)
 * - Multi-threaded logging
 * - Conditional logging (MR_CLOG)
 */

#include "Core/Logging/Logging.h"
#include "Core/CoreTypes.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

namespace MonsterRender {

// ============================================================================
// Custom Log Categories for Testing
// ============================================================================

// Declare test-specific log categories
DECLARE_LOG_CATEGORY_EXTERN(LogTestBasic, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogTestVerbose, Verbose, All)
DECLARE_LOG_CATEGORY_EXTERN(LogTestWarning, Warning, All)

// Define the log categories
DEFINE_LOG_CATEGORY(LogTestBasic)
DEFINE_LOG_CATEGORY(LogTestVerbose)
DEFINE_LOG_CATEGORY(LogTestWarning)

// ============================================================================
// Test Functions
// ============================================================================

/**
 * Test 1: Basic logging with different verbosity levels
 */
void TestBasicLogging()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 1: Basic Logging with Verbosity Levels" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Using the default LogTemp category
    MR_LOG(LogTemp, Log, "This is a Log level message");
    MR_LOG(LogTemp, Warning, "This is a Warning level message");
    MR_LOG(LogTemp, Error, "This is an Error level message");
    MR_LOG(LogTemp, Verbose, "This is a Verbose level message (may be filtered in Release)");
    MR_LOG(LogTemp, VeryVerbose, "This is a VeryVerbose level message (usually filtered)");

    std::cout << "\n[Test 1 Complete]\n" << std::endl;
}

/**
 * Test 2: Logging with custom categories
 */
void TestCustomCategories()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 2: Custom Log Categories" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Using custom categories
    MR_LOG(LogTestBasic, Log, "Message from LogTestBasic category");
    MR_LOG(LogTestVerbose, Verbose, "Verbose message from LogTestVerbose category");
    MR_LOG(LogTestWarning, Warning, "Warning from LogTestWarning category");

    // Using built-in engine categories
    MR_LOG(LogCore, Log, "Core system message");
    MR_LOG(LogRenderer, Log, "Renderer system message");
    MR_LOG(LogRHI, Log, "RHI system message");
    MR_LOG(LogVulkan, Log, "Vulkan backend message");

    std::cout << "\n[Test 2 Complete]\n" << std::endl;
}

/**
 * Test 3: Printf-style formatting
 */
void TestFormatting()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 3: Printf-Style Formatting" << std::endl;
    std::cout << "========================================\n" << std::endl;

    int frameCount = 1234;
    float fps = 60.5f;
    const char* deviceName = "NVIDIA GeForce RTX 4090";

    MR_LOG(LogTemp, Log, "Frame: %d, FPS: %.2f", frameCount, fps);
    MR_LOG(LogTemp, Log, "GPU Device: %s", deviceName);
    MR_LOG(LogTemp, Log, "Memory Usage: %zu bytes (%.2f MB)", 
           (size_t)1073741824, 1073741824.0 / (1024.0 * 1024.0));

    // Hexadecimal formatting
    void* ptr = (void*)0xDEADBEEF;
    MR_LOG(LogTemp, Log, "Pointer address: %p", ptr);

    std::cout << "\n[Test 3 Complete]\n" << std::endl;
}

/**
 * Test 4: Conditional logging with MR_CLOG
 */
void TestConditionalLogging()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 4: Conditional Logging (MR_CLOG)" << std::endl;
    std::cout << "========================================\n" << std::endl;

    bool bDebugMode = true;
    bool bReleaseMode = false;
    int errorCode = 404;

    // Only logs if condition is true
    MR_CLOG(bDebugMode, LogTemp, Log, "Debug mode is enabled");
    MR_CLOG(bReleaseMode, LogTemp, Log, "This should NOT appear (Release mode is false)");
    MR_CLOG(errorCode != 0, LogTemp, Warning, "Error code is non-zero: %d", errorCode);
    MR_CLOG(errorCode == 200, LogTemp, Log, "This should NOT appear (errorCode != 200)");

    std::cout << "\n[Test 4 Complete]\n" << std::endl;
}

/**
 * Test 5: Runtime verbosity control
 */
void TestRuntimeVerbosityControl()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 5: Runtime Verbosity Control" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Get current verbosity
    std::cout << "Current LogTestBasic verbosity: " 
              << ToString(LogTestBasic.GetVerbosity()) << std::endl;

    // Log at different levels
    MR_LOG(LogTestBasic, Log, "Log level message (should appear)");
    MR_LOG(LogTestBasic, Verbose, "Verbose level message (should appear)");

    // Change verbosity to Warning only
    std::cout << "\nChanging verbosity to Warning..." << std::endl;
    LogTestBasic.SetVerbosity(ELogVerbosity::Warning);

    std::cout << "New LogTestBasic verbosity: " 
              << ToString(LogTestBasic.GetVerbosity()) << std::endl;

    MR_LOG(LogTestBasic, Log, "Log level message (should be filtered now)");
    MR_LOG(LogTestBasic, Warning, "Warning level message (should appear)");
    MR_LOG(LogTestBasic, Error, "Error level message (should appear)");

    // Reset to default
    std::cout << "\nResetting to default verbosity..." << std::endl;
    LogTestBasic.ResetToDefault();

    std::cout << "Reset LogTestBasic verbosity: " 
              << ToString(LogTestBasic.GetVerbosity()) << std::endl;

    std::cout << "\n[Test 5 Complete]\n" << std::endl;
}

/**
 * Test 6: Multi-threaded logging
 */
void TestMultiThreadedLogging()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 6: Multi-Threaded Logging" << std::endl;
    std::cout << "========================================\n" << std::endl;

    std::atomic<int> completedThreads{0};
    const int numThreads = 4;
    const int logsPerThread = 5;

    auto threadFunc = [&completedThreads](int threadId) {
        for (int i = 0; i < 5; ++i) {
            MR_LOG(LogTemp, Log, "Thread %d: Message %d", threadId, i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        completedThreads++;
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(threadFunc, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    // Flush any buffered logs from other threads
    FlushLogs();

    std::cout << "\nAll " << numThreads << " threads completed, each logged " 
              << logsPerThread << " messages." << std::endl;
    std::cout << "\n[Test 6 Complete]\n" << std::endl;
}

/**
 * Test 7: Backward compatibility macros
 */
void TestBackwardCompatibility()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 7: Backward Compatibility Macros" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // These macros use LogTemp category internally
    MR_LOG_TRACE("Trace level message");
    MR_LOG_DEBUG("Debug level message");
    MR_LOG_INFO("Info level message");
    MR_LOG_WARNING("Warning level message");
    MR_LOG_ERROR("Error level message");
    // MR_LOG_FATAL("Fatal message"); // Don't call this - it will abort!

    std::cout << "\n[Test 7 Complete]\n" << std::endl;
}

/**
 * Test 8: Static log category (file-local)
 */
namespace {
    // This category is only visible in this file
    DEFINE_LOG_CATEGORY_STATIC(LogLocalTest, Log, All)
}

void TestStaticCategory()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 8: Static (File-Local) Log Category" << std::endl;
    std::cout << "========================================\n" << std::endl;

    MR_LOG(LogLocalTest, Log, "Message from file-local LogLocalTest category");
    MR_LOG(LogLocalTest, Warning, "Warning from file-local category");

    std::cout << "\n[Test 8 Complete]\n" << std::endl;
}

/**
 * Test 9: Demonstrate GLog direct usage
 */
void TestGLogDirect()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 9: Direct GLog Usage" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Direct access to the output device redirector
    GLog->Serialize("Direct GLog message with Log verbosity", ELogVerbosity::Log, "DirectTest");
    GLog->Serialize("Direct GLog warning message", ELogVerbosity::Warning, "DirectTest");

    // Flush to ensure all messages are written
    GLog->Flush();

    std::cout << "\n[Test 9 Complete]\n" << std::endl;
}

} // namespace MonsterRender

// ============================================================================
// Main Test Entry Point
// ============================================================================

/**
 * Run all logging system tests.
 * Call this function from main() or a test framework.
 */
void RunLoggingSystemTests()
{
    using namespace MonsterRender;

    // Initialize the logging system with all output devices
    // This registers Console, Debug, and File output devices with GLog
    InitializeLogging(
        "LoggingTest.log",  // Log file name
        true,               // Enable console output
        true,               // Enable debug output (OutputDebugString)
        true                // Enable file output
    );

    std::cout << "\n" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "       MonsterEngine Logging System Test Suite                  " << std::endl;
    std::cout << "                                                                " << std::endl;
    std::cout << "  Testing UE5-style logging architecture:                       " << std::endl;
    std::cout << "  - Log categories with compile-time/runtime filtering          " << std::endl;
    std::cout << "  - Multiple verbosity levels                                   " << std::endl;
    std::cout << "  - Printf-style formatting                                     " << std::endl;
    std::cout << "  - Multi-threaded logging support                              " << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "\n" << std::endl;

    // Run all tests
    TestBasicLogging();
    TestCustomCategories();
    TestFormatting();
    TestConditionalLogging();
    TestRuntimeVerbosityControl();
    TestMultiThreadedLogging();
    TestBackwardCompatibility();
    TestStaticCategory();
    TestGLogDirect();

    // Final flush
    FlushLogs();

    std::cout << "\n" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "              All Logging Tests Completed!                      " << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "\n" << std::endl;

    // Shutdown the logging system
    ShutdownLogging();
}

// Declare the test function for external use
extern "C" {
    void RunLoggingTests() {
        RunLoggingSystemTests();
    }
}
