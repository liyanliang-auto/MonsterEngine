#pragma once

/**
 * OutputDeviceRedirector.h
 * 
 * Central log redirector that distributes log messages to multiple output devices.
 * This is the core of the logging system (equivalent to UE5's GLog).
 * 
 * Features:
 * - Multi-threaded buffering for non-main thread logs
 * - Multiple output device support
 * - Panic mode for crash handling
 */

#include "Core/CoreTypes.h"
#include "Core/Logging/OutputDevice.h"
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>

namespace MonsterRender {

/**
 * Buffered log line for multi-threaded logging
 */
struct FBufferedLine {
    String Data;
    String Category;
    double Time;
    ELogVerbosity::Type Verbosity;

    FBufferedLine(const char* InData, const char* InCategory, ELogVerbosity::Type InVerbosity, double InTime = -1.0)
        : Data(InData)
        , Category(InCategory)
        , Time(InTime)
        , Verbosity(InVerbosity)
    {
    }
};

/**
 * Central log redirector - distributes logs to all registered output devices.
 * Singleton accessed via Get().
 */
class FOutputDeviceRedirector : public FOutputDevice {
public:
    /** Get the global log redirector instance (GLog equivalent) */
    static FOutputDeviceRedirector* Get();

    FOutputDeviceRedirector();
    ~FOutputDeviceRedirector();

    // ========================================================================
    // Output Device Management
    // ========================================================================

    /**
     * Add an output device to receive log messages
     * @param OutputDevice - Device to add
     */
    void AddOutputDevice(FOutputDevice* OutputDevice);

    /**
     * Remove an output device
     * @param OutputDevice - Device to remove
     */
    void RemoveOutputDevice(FOutputDevice* OutputDevice);

    /**
     * Check if a device is registered
     * @param OutputDevice - Device to check
     * @return true if the device is currently registered
     */
    bool IsRedirectingTo(FOutputDevice* OutputDevice) const;

    /**
     * Get the number of registered output devices
     */
    size_t GetNumOutputDevices() const;

    // ========================================================================
    // FOutputDevice Interface
    // ========================================================================

    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) override;
    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) override;
    void Flush() override;
    void TearDown() override;

    bool CanBeUsedOnAnyThread() const override { return true; }
    bool CanBeUsedOnMultipleThreads() const override { return true; }

    // ========================================================================
    // Multi-Threading Support
    // ========================================================================

    /**
     * Set the current thread as the primary (main) logging thread.
     * Only the primary thread can write directly to buffered output devices.
     */
    void SetCurrentThreadAsPrimaryThread();

    /**
     * Flush buffered logs from secondary threads.
     * Should be called periodically from the primary thread.
     */
    void FlushThreadedLogs();

    /**
     * Check if the current thread is the primary thread
     */
    bool IsInPrimaryThread() const;

    // ========================================================================
    // Panic Mode (Crash Handling)
    // ========================================================================

    /**
     * Enter panic mode - only use panic-safe output devices from now on.
     * Flushes all buffered logs immediately.
     */
    void Panic();

    /**
     * Check if we're in panic mode
     */
    bool IsInPanicMode() const { return m_bInPanicMode.load(std::memory_order_acquire); }

    // ========================================================================
    // Backlog Support
    // ========================================================================

    /**
     * Enable/disable backlog (stores all logs for later retrieval)
     */
    void EnableBacklog(bool bEnable);

    /**
     * Serialize the backlog to an output device
     */
    void SerializeBacklog(FOutputDevice* OutputDevice);

private:
    /** Serialize to all output devices (internal) */
    void SerializeToAllDevices(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time);

    /** Buffer a log line for later flushing */
    void BufferLine(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time);

    // Output devices
    TArray<FOutputDevice*> m_outputDevices;
    TArray<bool> m_canBeUsedOnPanic;  // Cached panic-safe flags
    mutable std::mutex m_devicesMutex;

    // Threading
    std::thread::id m_primaryThreadId;
    std::atomic<bool> m_bHasPrimaryThread;

    // Buffered logs from secondary threads
    TArray<FBufferedLine> m_bufferedLines;
    std::mutex m_bufferMutex;

    // Panic mode
    std::atomic<bool> m_bInPanicMode;
    std::thread::id m_panicThreadId;

    // Backlog
    bool m_bBacklogEnabled;
    TArray<FBufferedLine> m_backlog;
    std::mutex m_backlogMutex;
};

/** Global log redirector - equivalent to UE5's GLog */
#define GLog (MonsterRender::FOutputDeviceRedirector::Get())

} // namespace MonsterRender
