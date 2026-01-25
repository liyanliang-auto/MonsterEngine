#pragma once

/**
 * Logging.h
 * 
 * Main include file for the MonsterEngine logging system.
 * Include this file to get access to all logging functionality.
 * 
 * Architecture (following UE5 design):
 * 
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                      MR_LOG Macro                           │
 *   │  MR_LOG(CategoryName, Verbosity, Format, ...)               │
 *   └─────────────────────────────────────────────────────────────┘
 *                                 │
 *                                 ▼
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                   FLogCategory                               │
 *   │  - Compile-time verbosity filtering (if constexpr)          │
 *   │  - Runtime verbosity filtering (IsSuppressed)               │
 *   └─────────────────────────────────────────────────────────────┘
 *                                 │
 *                                 ▼
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │            FOutputDeviceRedirector (GLog)                    │
 *   │  - Central log hub (singleton)                               │
 *   │  - Multi-threaded buffering                                  │
 *   │  - Distributes to all output devices                         │
 *   └─────────────────────────────────────────────────────────────┘
 *                                 │
 *                 ┌───────────────┼───────────────┐
 *                 ▼               ▼               ▼
 *   ┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐
 *   │ FOutputDeviceFile│ │FOutputDeviceConsole│ │FOutputDeviceDebug│
 *   │   (Async file)   │ │   (Color stdout)   │ │ (Debugger output)│
 *   └──────────────────┘ └──────────────────┘ └──────────────────┘
 * 
 * Usage:
 * 
 *   // In header file:
 *   DECLARE_LOG_CATEGORY_EXTERN(LogMyModule, Log, All)
 * 
 *   // In source file:
 *   DEFINE_LOG_CATEGORY(LogMyModule)
 * 
 *   // Logging:
 *   MR_LOG(LogMyModule, Warning, "Failed to load: %s", filename);
 *   MR_LOG(LogMyModule, Log, "Initialization complete");
 * 
 *   // Simple logging (uses LogTemp category):
 *   MR_LOG_INFO("Hello World");
 *   MR_LOG_WARNING("Something might be wrong");
 *   MR_LOG_ERROR("Something went wrong!");
 */

#include "Core/Logging/LogVerbosity.h"
#include "Core/Logging/LogCategory.h"
#include "Core/Logging/LogMacros.h"
#include "Core/Logging/OutputDevice.h"
#include "Core/Logging/OutputDeviceRedirector.h"
#include "Core/Logging/OutputDeviceFile.h"
#include "Core/Logging/OutputDeviceConsole.h"
#include "Core/Logging/OutputDeviceDebug.h"

namespace MonsterRender {

// ============================================================================
// Built-in Log Categories (declared here, defined in LogCategories.cpp)
// ============================================================================

// Core engine categories
DECLARE_LOG_CATEGORY_EXTERN(LogCore, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogInit, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogExit, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogMemory, Log, All)

// Rendering categories
DECLARE_LOG_CATEGORY_EXTERN(LogRenderer, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogRHI, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogVulkan, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogShaders, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogTextures, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogTextureStreaming, Log, All)

// Platform categories
DECLARE_LOG_CATEGORY_EXTERN(LogPlatform, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogWindow, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogInput, Log, All)

// Scene and rendering system categories
DECLARE_LOG_CATEGORY_EXTERN(LogScene, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogSceneRenderer, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogSceneView, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogPrimitiveSceneProxy, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogCubeSceneProxy, Log, All)

// Application categories
DECLARE_LOG_CATEGORY_EXTERN(LogCubeSceneApp, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogCubeActor, Log, All)

// Camera categories
DECLARE_LOG_CATEGORY_EXTERN(LogCameraManager, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(LogCameraTypes, Log, All)

// RDG (Render Dependency Graph) categories
DECLARE_LOG_CATEGORY_EXTERN(LogRDG, Log, All)

// ============================================================================
// Logging System Initialization
// ============================================================================

/**
 * Initialize the logging system with default output devices.
 * Call this early in application startup.
 * 
 * @param LogFilename - Path to the log file (nullptr for auto-generated name)
 * @param bEnableConsole - Enable console output
 * @param bEnableDebugOutput - Enable debugger output (OutputDebugString on Windows)
 * @param bEnableFileOutput - Enable file output
 */
void InitializeLogging(
    const char* LogFilename = nullptr,
    bool bEnableConsole = true,
    bool bEnableDebugOutput = true,
    bool bEnableFileOutput = true);

/**
 * Shutdown the logging system.
 * Flushes all pending logs and cleans up resources.
 */
void ShutdownLogging();

/**
 * Flush all pending log messages.
 * Call this periodically from the main thread to flush buffered logs.
 */
inline void FlushLogs() {
    GLog->FlushThreadedLogs();
    GLog->Flush();
}

/**
 * Set verbosity level for all built-in log categories.
 * This affects all globally declared categories but not static local categories.
 * 
 * @param Verbosity - The verbosity level to set (e.g., ELogVerbosity::Error)
 */
void SetGlobalLogVerbosity(ELogVerbosity::Type Verbosity);

} // namespace MonsterRender
