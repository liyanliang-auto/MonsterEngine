#include "Core/Logging/Logging.h"

namespace MonsterRender {

// ============================================================================
// Global Output Devices
// ============================================================================

static TUniquePtr<FOutputDeviceConsole> GConsoleOutputDevice;
static TUniquePtr<FOutputDeviceDebug> GDebugOutputDevice;
static TUniquePtr<FOutputDeviceFile> GFileOutputDevice;

// ============================================================================
// Logging System Initialization
// ============================================================================

void InitializeLogging(
    const char* LogFilename,
    bool bEnableConsole,
    bool bEnableDebugOutput,
    bool bEnableFileOutput)
{
    // Set the current thread as the primary logging thread
    GLog->SetCurrentThreadAsPrimaryThread();

    // Create and register console output device
    if (bEnableConsole) {
        GConsoleOutputDevice = MakeUnique<FOutputDeviceConsole>();
        GLog->AddOutputDevice(GConsoleOutputDevice.get());
    }

    // Create and register debug output device
    if (bEnableDebugOutput) {
        GDebugOutputDevice = MakeUnique<FOutputDeviceDebug>();
        GLog->AddOutputDevice(GDebugOutputDevice.get());
    }

    // Create and register file output device
    if (bEnableFileOutput) {
        GFileOutputDevice = MakeUnique<FOutputDeviceFile>(LogFilename, false, false);
        GLog->AddOutputDevice(GFileOutputDevice.get());
    }

    MR_LOG(LogInit, Log, "Logging system initialized");
    
    if (GFileOutputDevice && GFileOutputDevice->IsOpened()) {
        MR_LOG(LogInit, Log, "Log file: %s", GFileOutputDevice->GetFilename());
    }
}

void ShutdownLogging()
{
    MR_LOG(LogExit, Log, "Shutting down logging system");

    // Flush all pending logs
    GLog->Flush();

    // Remove and destroy output devices
    if (GFileOutputDevice) {
        GLog->RemoveOutputDevice(GFileOutputDevice.get());
        GFileOutputDevice.reset();
    }

    if (GDebugOutputDevice) {
        GLog->RemoveOutputDevice(GDebugOutputDevice.get());
        GDebugOutputDevice.reset();
    }

    if (GConsoleOutputDevice) {
        GLog->RemoveOutputDevice(GConsoleOutputDevice.get());
        GConsoleOutputDevice.reset();
    }

    // Final teardown
    GLog->TearDown();
}

void SetGlobalLogVerbosity(ELogVerbosity::Type Verbosity)
{
    // Set verbosity for all built-in log categories
    LogTemp.SetVerbosity(Verbosity);
    LogCore.SetVerbosity(Verbosity);
    LogInit.SetVerbosity(Verbosity);
    LogExit.SetVerbosity(Verbosity);
    LogMemory.SetVerbosity(Verbosity);
    LogRenderer.SetVerbosity(Verbosity);
    LogRHI.SetVerbosity(Verbosity);
    LogVulkan.SetVerbosity(Verbosity);
    LogShaders.SetVerbosity(Verbosity);
    LogTextures.SetVerbosity(Verbosity);
    LogPlatform.SetVerbosity(Verbosity);
    LogWindow.SetVerbosity(Verbosity);
    LogInput.SetVerbosity(Verbosity);
}

} // namespace MonsterRender
