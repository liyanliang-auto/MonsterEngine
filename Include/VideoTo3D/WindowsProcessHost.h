// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace MonsterEngine::VideoTo3D {

/** Explicit, shell-free Windows process launch configuration. */
struct ProcessRequest {
    /** Absolute application path; no PATH search is performed. */
    std::filesystem::path executable;
    /** UTF-16 arguments excluding argv[0]. */
    std::vector<std::wstring> arguments;
    /** Optional absolute working directory; empty inherits the caller directory. */
    std::filesystem::path workingDirectory;
    /** Case-insensitive overrides of the inherited Unicode environment. */
    std::map<std::wstring, std::wstring> environmentOverrides;
    /** Wall time limit, including startup; zero requests immediate timeout. */
    std::chrono::milliseconds timeout{30000};
    /** Maximum retained prefix per stream; excess bytes are still drained. */
    std::size_t maxCapturedBytesPerStream{1048576};
    /** Optional bounded worker tick for external event files. Never called on pipe readers.
     * An exception is a host failure and retires the isolated tree. It must not block.
     */
    std::function<void()> observeTick;
};

/** Lifecycle and bounded raw-byte diagnostics; termination never implies saved state. */
struct ProcessResult {
    /** CreateProcessW succeeded, even if assignment or startup subsequently failed. */
    bool launched{false};
    /** The directly launched process was observed to terminate. */
    bool exited{false};
    /** The wall time limit caused termination. */
    bool timedOut{false};
    /** The cancellation flag caused termination. */
    bool cancelled{false};
    /** The job was observed empty (or no process was created). */
    bool treeRetired{false};
    /** Direct process exit code; meaningful only when exited is true. */
    std::uint32_t exitCode{0};
    /** First lifecycle, pipe or capture error, if any. */
    std::uint32_t win32Error{0};
    /** Captured stdout prefix, without encoding assumptions. */
    std::string stdoutBytes;
    /** Captured stderr prefix, without encoding assumptions. */
    std::string stderrBytes;
    /** Total stdout bytes drained, saturating at UINT64_MAX. */
    std::uint64_t stdoutTotalBytes{0};
    /** Total stderr bytes drained, saturating at UINT64_MAX. */
    std::uint64_t stderrTotalBytes{0};
    /** Stdout exceeded retained capture capacity. */
    bool stdoutTruncated{false};
    /** Stderr exceeded retained capture capacity. */
    bool stderrTruncated{false};
    /** Host failure details, independent of child nonzero exit status. */
    std::string error;
};

/** Windows-only blocking host. Call on an orchestration worker, never the UI thread. */
class WindowsProcessHost {
public:
    /** Run in an isolated job and retire all descendants on every completion path. */
    static ProcessResult Run(const ProcessRequest& request,
                             const std::atomic_bool* cancelRequested = nullptr);
};

} // namespace MonsterEngine::VideoTo3D
