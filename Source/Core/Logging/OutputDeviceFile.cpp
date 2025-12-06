#include "Core/Logging/OutputDeviceFile.h"
#include "Core/Logging/LogVerbosity.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace MonsterRender {

// ============================================================================
// FAsyncWriter Implementation
// ============================================================================

FAsyncWriter::FAsyncWriter(const char* Filename, bool bAppend)
    : m_bStopRequested(false)
    , m_bFlushRequested(false)
{
    // Open the file
    std::ios_base::openmode mode = std::ios::out;
    if (bAppend) {
        mode |= std::ios::app;
    } else {
        mode |= std::ios::trunc;
    }

    m_file.open(Filename, mode);

    if (m_file.is_open()) {
        // Start the writer thread
        m_writerThread = std::thread(&FAsyncWriter::WriterThreadFunc, this);
    }
}

FAsyncWriter::~FAsyncWriter() {
    Stop();
}

void FAsyncWriter::Write(const char* Data, size_t Length) {
    if (!m_file.is_open() || m_bStopRequested.load(std::memory_order_acquire)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_writeQueue.emplace(Data, Length);
    }
    m_queueCV.notify_one();
}

void FAsyncWriter::Flush() {
    if (!m_file.is_open()) {
        return;
    }

    // Request flush and wait for completion
    m_bFlushRequested.store(true, std::memory_order_release);
    m_queueCV.notify_one();

    // Wait for flush to complete
    std::unique_lock<std::mutex> lock(m_queueMutex);
    m_flushCV.wait(lock, [this]() {
        return !m_bFlushRequested.load(std::memory_order_acquire) || 
               m_bStopRequested.load(std::memory_order_acquire);
    });
}

void FAsyncWriter::Stop() {
    if (m_bStopRequested.exchange(true, std::memory_order_acq_rel)) {
        return; // Already stopped
    }

    m_queueCV.notify_one();

    if (m_writerThread.joinable()) {
        m_writerThread.join();
    }

    // Write any remaining data synchronously
    while (!m_writeQueue.empty()) {
        m_file << m_writeQueue.front();
        m_writeQueue.pop();
    }

    m_file.flush();
    m_file.close();
}

void FAsyncWriter::WriterThreadFunc() {
    while (!m_bStopRequested.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(m_queueMutex);

        // Wait for data or stop request
        m_queueCV.wait(lock, [this]() {
            return !m_writeQueue.empty() || 
                   m_bStopRequested.load(std::memory_order_acquire) ||
                   m_bFlushRequested.load(std::memory_order_acquire);
        });

        // Process all queued writes
        while (!m_writeQueue.empty()) {
            String data = std::move(m_writeQueue.front());
            m_writeQueue.pop();
            lock.unlock();

            m_file << data;

            lock.lock();
        }

        // Handle flush request
        if (m_bFlushRequested.load(std::memory_order_acquire)) {
            lock.unlock();
            m_file.flush();
            m_bFlushRequested.store(false, std::memory_order_release);
            m_flushCV.notify_all();
            lock.lock();
        }
    }
}

// ============================================================================
// FOutputDeviceFile Implementation
// ============================================================================

FOutputDeviceFile::FOutputDeviceFile(const char* Filename, bool bAppendIfExists, bool bCreateWriterLazily)
    : m_bAppendIfExists(bAppendIfExists)
    , m_bCreateWriterLazily(bCreateWriterLazily)
    , m_bDead(false)
{
    if (Filename) {
        m_filename = Filename;
    } else {
        // Generate default filename with timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::ostringstream oss;
        oss << "MonsterEngine_";
        
        // Use thread-safe localtime
        std::tm tm_buf;
#if PLATFORM_WINDOWS
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
        oss << ".log";
        
        m_filename = oss.str();
    }

    if (!bCreateWriterLazily) {
        CreateWriter();
    }
}

FOutputDeviceFile::~FOutputDeviceFile() {
    TearDown();
}

void FOutputDeviceFile::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) {
    Serialize(Message, Verbosity, Category, -1.0);
}

void FOutputDeviceFile::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) {
    if (m_bDead) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_writerMutex);

    // Create writer lazily if needed
    if (!m_asyncWriter && !CreateWriter()) {
        return;
    }

    // Format and write the log line
    String formattedLine = FormatLogLine(Message, Verbosity, Category, Time);
    m_asyncWriter->Write(formattedLine);
}

void FOutputDeviceFile::Flush() {
    std::lock_guard<std::mutex> lock(m_writerMutex);
    if (m_asyncWriter) {
        m_asyncWriter->Flush();
    }
}

void FOutputDeviceFile::TearDown() {
    std::lock_guard<std::mutex> lock(m_writerMutex);
    if (m_asyncWriter) {
        m_asyncWriter->Stop();
        m_asyncWriter.reset();
    }
    m_bDead = true;
}

void FOutputDeviceFile::SetFilename(const char* Filename) {
    std::lock_guard<std::mutex> lock(m_writerMutex);

    // Close current file
    if (m_asyncWriter) {
        m_asyncWriter->Stop();
        m_asyncWriter.reset();
    }

    m_filename = Filename ? Filename : "";
    m_bDead = false;

    if (!m_bCreateWriterLazily && !m_filename.empty()) {
        CreateWriter();
    }
}

bool FOutputDeviceFile::IsOpened() const {
    std::lock_guard<std::mutex> lock(m_writerMutex);
    return m_asyncWriter && m_asyncWriter->IsOpen();
}

void FOutputDeviceFile::CreateBackupCopy(const char* Filename) {
    if (!std::filesystem::exists(Filename)) {
        return;
    }

    // Generate backup filename with timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::ostringstream oss;
    
    // Get base name without extension
    std::filesystem::path path(Filename);
    oss << path.stem().string();
    oss << "-backup-";

    std::tm tm_buf;
#if PLATFORM_WINDOWS
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
    oss << path.extension().string();

    std::filesystem::path backupPath = path.parent_path() / oss.str();

    try {
        std::filesystem::rename(Filename, backupPath);
    } catch (...) {
        // Ignore backup failures
    }
}

bool FOutputDeviceFile::CreateWriter() {
    if (m_filename.empty()) {
        return false;
    }

    // Create backup of existing file if not appending
    if (!m_bAppendIfExists) {
        CreateBackupCopy(m_filename.c_str());
    }

    m_asyncWriter = MakeUnique<FAsyncWriter>(m_filename.c_str(), m_bAppendIfExists);

    if (!m_asyncWriter->IsOpen()) {
        m_asyncWriter.reset();
        m_bDead = true;
        return false;
    }

    return true;
}

String FOutputDeviceFile::FormatLogLine(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) {
    std::ostringstream oss;

    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf;
#if PLATFORM_WINDOWS
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif

    oss << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "]";

    // Category and verbosity
    oss << "[" << Category << "]";
    oss << "[" << ToShortString(Verbosity) << "] ";

    // Message
    oss << Message;

    // Line terminator
    if (GetAutoEmitLineTerminator()) {
        oss << "\n";
    }

    return oss.str();
}

} // namespace MonsterRender
