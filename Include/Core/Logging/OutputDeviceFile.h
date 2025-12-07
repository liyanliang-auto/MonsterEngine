#pragma once

/**
 * OutputDeviceFile.h
 * 
 * File output device with asynchronous writing support.
 * Writes log messages to a file using a background thread.
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/UniquePtr.h"
#include "Core/Logging/OutputDevice.h"
#include <fstream>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <queue>

namespace MonsterRender {

// Use MonsterEngine smart pointers
using MonsterEngine::TUniquePtr;
using MonsterEngine::MakeUnique;

/**
 * Asynchronous file writer that writes logs in a background thread.
 * This prevents file I/O from blocking the main thread.
 */
class FAsyncWriter {
public:
    /**
     * Constructor
     * @param Filename - Path to the log file
     * @param bAppend - If true, append to existing file; otherwise overwrite
     */
    FAsyncWriter(const char* Filename, bool bAppend = false);
    ~FAsyncWriter();

    /** Write data to the file (queued for async write) */
    void Write(const char* Data, size_t Length);

    /** Write a string */
    void Write(const String& Data) { Write(Data.c_str(), Data.length()); }

    /** Flush all pending writes and wait for completion */
    void Flush();

    /** Stop the writer thread */
    void Stop();

    /** Check if the file is open */
    bool IsOpen() const { return m_file.is_open(); }

private:
    void WriterThreadFunc();

    std::ofstream m_file;
    std::thread m_writerThread;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;
    std::queue<String> m_writeQueue;
    std::atomic<bool> m_bStopRequested;
    std::atomic<bool> m_bFlushRequested;
    std::condition_variable m_flushCV;
};

/**
 * File output device - writes logs to a file asynchronously.
 */
class FOutputDeviceFile : public FOutputDevice {
public:
    /**
     * Constructor
     * @param Filename - Path to the log file (nullptr for auto-generated name)
     * @param bAppendIfExists - If true, append to existing file
     * @param bCreateWriterLazily - If true, delay file creation until first write
     */
    FOutputDeviceFile(
        const char* Filename = nullptr,
        bool bAppendIfExists = false,
        bool bCreateWriterLazily = true);

    ~FOutputDeviceFile();

    // ========================================================================
    // FOutputDevice Interface
    // ========================================================================

    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) override;
    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) override;
    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, 
                  const char* File, int32 Line) override;
    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category,
                  double Time, const char* File, int32 Line) override;
    void Flush() override;
    void TearDown() override;

    bool CanBeUsedOnAnyThread() const override { return true; }
    bool CanBeUsedOnMultipleThreads() const override { return true; }
    bool CanBeUsedOnPanicThread() const override { return true; }

    // ========================================================================
    // File-Specific Methods
    // ========================================================================

    /** Set the filename (closes current file if open) */
    void SetFilename(const char* Filename);

    /** Get the current filename */
    const char* GetFilename() const { return m_filename.c_str(); }

    /** Check if the file is open */
    bool IsOpened() const;

    /** Create a backup of an existing log file */
    static void CreateBackupCopy(const char* Filename);

private:
    /** Create the async writer if not already created */
    bool CreateWriter();

    /** Format a log line with timestamp, category, file and line */
    String FormatLogLine(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, 
                        double Time, const char* File = nullptr, int32 Line = 0);

    TUniquePtr<FAsyncWriter> m_asyncWriter;
    String m_filename;
    bool m_bAppendIfExists;
    bool m_bCreateWriterLazily;
    bool m_bDead;
    mutable std::mutex m_writerMutex;  // mutable to allow locking in const methods
};

} // namespace MonsterRender
