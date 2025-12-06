#pragma once

/**
 * OutputDevice.h
 * 
 * Base class for all log output devices following UE5 architecture.
 * Output devices receive formatted log messages and write them to their destination.
 */

#include "Core/CoreTypes.h"
#include "Core/Logging/LogVerbosity.h"
#include <cstdarg>

namespace MonsterRender {

/**
 * Enum for log timestamp display format
 */
namespace ELogTimes {
    enum Type {
        None,           // No timestamp
        UTC,            // UTC time
        SinceStart,     // Seconds since application start
        Local,          // Local time
    };
}

/**
 * Abstract base class for all log output devices.
 * Subclasses implement Serialize() to write logs to their specific destination.
 */
class FOutputDevice {
public:
    FOutputDevice()
        : m_bSuppressEventTag(false)
        , m_bAutoEmitLineTerminator(true)
    {
    }

    virtual ~FOutputDevice() = default;

    // Non-copyable but movable
    FOutputDevice(const FOutputDevice&) = default;
    FOutputDevice& operator=(const FOutputDevice&) = default;
    FOutputDevice(FOutputDevice&&) = default;
    FOutputDevice& operator=(FOutputDevice&&) = default;

    // ========================================================================
    // Core Interface - Must be implemented by subclasses
    // ========================================================================

    /**
     * Serialize a log message to this output device.
     * @param Message - The formatted log message
     * @param Verbosity - Verbosity level of the message
     * @param Category - Name of the log category
     */
    virtual void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) = 0;

    /**
     * Serialize with timestamp
     */
    virtual void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) {
        Serialize(Message, Verbosity, Category);
    }

    /**
     * Flush any buffered output
     */
    virtual void Flush() {}

    /**
     * Clean up resources. Called before destruction.
     */
    virtual void TearDown() {}

    // ========================================================================
    // Thread Safety
    // ========================================================================

    /**
     * @return true if this device can be used from any thread
     */
    virtual bool CanBeUsedOnAnyThread() const { return false; }

    /**
     * @return true if this device can be used from multiple threads simultaneously
     */
    virtual bool CanBeUsedOnMultipleThreads() const { return false; }

    /**
     * @return true if this device can be used during a panic/crash
     */
    virtual bool CanBeUsedOnPanicThread() const { return false; }

    // ========================================================================
    // Configuration
    // ========================================================================

    /** Set whether to suppress the event tag (e.g., "[Log]") */
    void SetSuppressEventTag(bool bSuppress) { m_bSuppressEventTag = bSuppress; }
    bool GetSuppressEventTag() const { return m_bSuppressEventTag; }

    /** Set whether to automatically emit line terminator */
    void SetAutoEmitLineTerminator(bool bAuto) { m_bAutoEmitLineTerminator = bAuto; }
    bool GetAutoEmitLineTerminator() const { return m_bAutoEmitLineTerminator; }

    // ========================================================================
    // Convenience Logging Methods
    // ========================================================================

    /** Log a simple string message */
    void Log(ELogVerbosity::Type Verbosity, const char* Category, const char* Message) {
        Serialize(Message, Verbosity, Category);
    }

    /** Log a formatted message (printf-style) */
    void Logf(ELogVerbosity::Type Verbosity, const char* Category, const char* Format, ...);

protected:
    bool m_bSuppressEventTag;
    bool m_bAutoEmitLineTerminator;
};

} // namespace MonsterRender
