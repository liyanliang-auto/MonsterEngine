#include "Core/Logging/OutputDeviceRedirector.h"
#include <algorithm>
#include <chrono>

namespace MonsterRender {

// ============================================================================
// Singleton Instance
// ============================================================================

FOutputDeviceRedirector* FOutputDeviceRedirector::Get() {
    static FOutputDeviceRedirector Instance;
    return &Instance;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

FOutputDeviceRedirector::FOutputDeviceRedirector()
    : m_bHasPrimaryThread(false)
    , m_bInPanicMode(false)
    , m_bBacklogEnabled(false)
{
}

FOutputDeviceRedirector::~FOutputDeviceRedirector() {
    TearDown();
}

// ============================================================================
// Output Device Management
// ============================================================================

void FOutputDeviceRedirector::AddOutputDevice(FOutputDevice* OutputDevice) {
    if (!OutputDevice) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_devicesMutex);

    // Check if already added
    auto it = std::find(m_outputDevices.begin(), m_outputDevices.end(), OutputDevice);
    if (it != m_outputDevices.end()) {
        return;
    }

    m_outputDevices.push_back(OutputDevice);
    m_canBeUsedOnPanic.push_back(OutputDevice->CanBeUsedOnPanicThread());
}

void FOutputDeviceRedirector::RemoveOutputDevice(FOutputDevice* OutputDevice) {
    std::lock_guard<std::mutex> lock(m_devicesMutex);

    auto it = std::find(m_outputDevices.begin(), m_outputDevices.end(), OutputDevice);
    if (it != m_outputDevices.end()) {
        size_t index = std::distance(m_outputDevices.begin(), it);
        m_outputDevices.erase(it);
        m_canBeUsedOnPanic.erase(m_canBeUsedOnPanic.begin() + index);
    }
}

bool FOutputDeviceRedirector::IsRedirectingTo(FOutputDevice* OutputDevice) const {
    std::lock_guard<std::mutex> lock(m_devicesMutex);
    return std::find(m_outputDevices.begin(), m_outputDevices.end(), OutputDevice) != m_outputDevices.end();
}

size_t FOutputDeviceRedirector::GetNumOutputDevices() const {
    std::lock_guard<std::mutex> lock(m_devicesMutex);
    return m_outputDevices.size();
}

// ============================================================================
// FOutputDevice Interface
// ============================================================================

void FOutputDeviceRedirector::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) {
    // Get current time
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    double time = std::chrono::duration<double>(duration).count();

    Serialize(Message, Verbosity, Category, time);
}

void FOutputDeviceRedirector::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) {
    // In panic mode, only use panic-safe devices
    if (m_bInPanicMode.load(std::memory_order_acquire)) {
        // Only the panic thread can log
        if (std::this_thread::get_id() == m_panicThreadId) {
            SerializeToAllDevices(Message, Verbosity, Category, Time);
        }
        return;
    }

    // Check if we're on the primary thread
    if (IsInPrimaryThread()) {
        // Direct output to all devices
        SerializeToAllDevices(Message, Verbosity, Category, Time);
    } else {
        // Buffer for later flushing
        BufferLine(Message, Verbosity, Category, Time);
    }

    // Store in backlog if enabled
    if (m_bBacklogEnabled) {
        std::lock_guard<std::mutex> lock(m_backlogMutex);
        m_backlog.emplace_back(Message, Category, Verbosity, Time);
    }
}

void FOutputDeviceRedirector::Flush() {
    // Flush buffered logs first
    FlushThreadedLogs();

    // Then flush all output devices
    std::lock_guard<std::mutex> lock(m_devicesMutex);
    for (auto* device : m_outputDevices) {
        device->Flush();
    }
}

void FOutputDeviceRedirector::TearDown() {
    Flush();

    std::lock_guard<std::mutex> lock(m_devicesMutex);
    for (auto* device : m_outputDevices) {
        device->TearDown();
    }
    m_outputDevices.clear();
    m_canBeUsedOnPanic.clear();
}

// ============================================================================
// Multi-Threading Support
// ============================================================================

void FOutputDeviceRedirector::SetCurrentThreadAsPrimaryThread() {
    m_primaryThreadId = std::this_thread::get_id();
    m_bHasPrimaryThread.store(true, std::memory_order_release);
}

void FOutputDeviceRedirector::FlushThreadedLogs() {
    TArray<FBufferedLine> linesToFlush;

    // Grab all buffered lines
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        linesToFlush = std::move(m_bufferedLines);
        m_bufferedLines.clear();
    }

    // Output them
    for (const auto& line : linesToFlush) {
        SerializeToAllDevices(line.Data.c_str(), line.Verbosity, line.Category.c_str(), line.Time);
    }
}

bool FOutputDeviceRedirector::IsInPrimaryThread() const {
    if (!m_bHasPrimaryThread.load(std::memory_order_acquire)) {
        // No primary thread set - treat all threads as primary
        return true;
    }
    return std::this_thread::get_id() == m_primaryThreadId;
}

// ============================================================================
// Panic Mode
// ============================================================================

void FOutputDeviceRedirector::Panic() {
    // Only one thread can be the panic thread
    bool expected = false;
    if (!m_bInPanicMode.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return; // Another thread already in panic mode
    }

    m_panicThreadId = std::this_thread::get_id();

    // Flush all buffered logs immediately
    FlushThreadedLogs();

    // Flush all panic-safe devices
    std::lock_guard<std::mutex> lock(m_devicesMutex);
    for (size_t i = 0; i < m_outputDevices.size(); ++i) {
        if (m_canBeUsedOnPanic[i]) {
            m_outputDevices[i]->Flush();
        }
    }
}

// ============================================================================
// Backlog Support
// ============================================================================

void FOutputDeviceRedirector::EnableBacklog(bool bEnable) {
    std::lock_guard<std::mutex> lock(m_backlogMutex);
    m_bBacklogEnabled = bEnable;
    if (!bEnable) {
        m_backlog.clear();
    }
}

void FOutputDeviceRedirector::SerializeBacklog(FOutputDevice* OutputDevice) {
    if (!OutputDevice) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_backlogMutex);
    for (const auto& line : m_backlog) {
        OutputDevice->Serialize(line.Data.c_str(), line.Verbosity, line.Category.c_str(), line.Time);
    }
}

// ============================================================================
// Internal Methods
// ============================================================================

void FOutputDeviceRedirector::SerializeToAllDevices(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) {
    std::lock_guard<std::mutex> lock(m_devicesMutex);

    bool bInPanic = m_bInPanicMode.load(std::memory_order_acquire);

    for (size_t i = 0; i < m_outputDevices.size(); ++i) {
        // In panic mode, only use panic-safe devices
        if (bInPanic && !m_canBeUsedOnPanic[i]) {
            continue;
        }

        m_outputDevices[i]->Serialize(Message, Verbosity, Category, Time);
    }
}

void FOutputDeviceRedirector::BufferLine(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_bufferedLines.emplace_back(Message, Category, Verbosity, Time);
}

} // namespace MonsterRender
