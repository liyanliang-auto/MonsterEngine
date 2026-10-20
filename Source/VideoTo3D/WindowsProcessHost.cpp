// Copyright Monster Engine. All Rights Reserved.
#include "VideoTo3D/WindowsProcessHost.h"

#ifndef _WIN32
#error WindowsProcessHost is a Windows-only product service.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <thread>

namespace MonsterEngine::VideoTo3D {
namespace {
class Handle {
public:
    HANDLE value{nullptr};
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { Reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void Reset() noexcept {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = nullptr;
    }
};

struct Attributes {
    std::vector<unsigned char> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list{nullptr};
    ~Attributes() { if (list) DeleteProcThreadAttributeList(list); }
};

void Fail(ProcessResult& result, DWORD code, const char* context) {
    if (result.win32Error == ERROR_SUCCESS) {
        result.win32Error = code ? code : ERROR_GEN_FAILURE;
        result.error = context;
    }
}

bool HasNull(const std::wstring& text) { return text.find(L'\0') != std::wstring::npos; }

// CommandLineToArgvW / MSVC CRT quoting, including empty args and trailing slashes.
std::wstring Quote(const std::wstring& argument) {
    std::wstring result(1, L'"');
    std::size_t slashes = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(slashes * (ch == L'"' ? 2 : 1), L'\\');
        if (ch == L'"') result.push_back(L'\\');
        result.push_back(ch);
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

struct EnvironmentLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                    b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
    }
};

bool MakeEnvironment(const ProcessRequest& request, std::vector<wchar_t>& block,
                     ProcessResult& result) {
    LPWCH inherited = GetEnvironmentStringsW();
    if (!inherited) { Fail(result, GetLastError(), "GetEnvironmentStringsW failed"); return false; }
    std::map<std::wstring, std::wstring, EnvironmentLess> environment;
    try {
        for (const wchar_t* item = inherited; *item; ) {
            std::wstring entry(item);
            const auto equal = entry.find(L'=', 1); // Preserve hidden drive directory entries.
            if (equal != std::wstring::npos) environment[entry.substr(0, equal)] = entry.substr(equal + 1);
            item += entry.size() + 1;
        }
    } catch (...) { FreeEnvironmentStringsW(inherited); throw; }
    if (!FreeEnvironmentStringsW(inherited)) {
        Fail(result, GetLastError(), "FreeEnvironmentStringsW failed"); return false;
    }
    for (const auto& [name, value] : request.environmentOverrides) environment[name] = value;
    for (const auto& [name, value] : environment) {
        block.insert(block.end(), name.begin(), name.end());
        block.push_back(L'=');
        block.insert(block.end(), value.begin(), value.end());
        block.push_back(L'\0');
    }
    if (block.empty()) block.push_back(L'\0');
    block.push_back(L'\0');
    return true;
}

bool Pipe(Handle& read, Handle& write, ProcessResult& result) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!CreatePipe(&read.value, &write.value, &security, 0)) {
        Fail(result, GetLastError(), "CreatePipe failed"); return false;
    }
    if (!SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0)) {
        Fail(result, GetLastError(), "SetHandleInformation failed"); return false;
    }
    return true;
}

// Each pipe has its own reader. Peek permits bounded retirement even on pipe errors.
void Drain(std::stop_token stop, HANDLE pipe, std::size_t capacity, std::string& capture,
           std::uint64_t& total, bool& truncated, DWORD& error) noexcept {
    std::array<char, 16384> bytes{};
    bool captureFailed = false;
    bool stopping = false;
    DWORD retirementBytes = 0;
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            const DWORD code = GetLastError();
            if (code != ERROR_BROKEN_PIPE) error = code;
            return;
        }
        if (!stopping && stop.stop_requested()) {
            // Refresh after observing stop: the earlier peek may predate the child's final writes.
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
                const DWORD code = GetLastError();
                if (code != ERROR_BROKEN_PIPE) error = code;
                return;
            }
            stopping = true;
            retirementBytes = available;
        }
        if (stopping) {
            available = std::min(available, retirementBytes);
            if (!available) return;
        }
        if (!available) {
            if (stop.stop_requested()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        DWORD count = 0;
        if (!ReadFile(pipe, bytes.data(), std::min<DWORD>(available, static_cast<DWORD>(bytes.size())),
                      &count, nullptr)) {
            const DWORD code = GetLastError();
            if (code != ERROR_BROKEN_PIPE) error = code;
            return;
        }
        if (!count) return;
        if (stopping) retirementBytes -= count;
        total += std::min<std::uint64_t>(count, std::numeric_limits<std::uint64_t>::max() - total);
        const std::size_t retained = captureFailed ? 0 : std::min<std::size_t>(count, capacity - capture.size());
        try { capture.append(bytes.data(), retained); }
        catch (...) { captureFailed = true; error = ERROR_NOT_ENOUGH_MEMORY; }
        if (retained != count || captureFailed) truncated = true;
    }
}

// A successful TerminateJobObject call alone does not prove that retirement finished.
bool RetireJob(HANDLE job, HANDLE completionPort, HANDLE rootProcess, ProcessResult& result) noexcept {
    if (!TerminateJobObject(job, ERROR_PROCESS_ABORTED))
        Fail(result, GetLastError(), "TerminateJobObject failed");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    try {
        std::vector<std::unique_ptr<Handle>> processes;
        std::uint64_t reportedProcesses = 0;
        do {
            // The port was attached before the first assignment. Each NEW_PROCESS packet
            // represents an assignment, even if that process has already exited. Retain
            // handles: job accounting can reach zero before process objects are signaled.
            DWORD message = 0;
            ULONG_PTR key = 0;
            LPOVERLAPPED processId = nullptr;
            while (GetQueuedCompletionStatus(completionPort, &message, &key, &processId, 0)) {
                if (message != JOB_OBJECT_MSG_NEW_PROCESS) continue;
                ++reportedProcesses;
                auto process = std::make_unique<Handle>();
                process->value = OpenProcess(
                    SYNCHRONIZE, FALSE,
                    static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(processId)));
                if (!process->value) {
                    const DWORD error = GetLastError();
                    // No process object with this ID remains. In particular, an ID
                    // cannot be reused while an observer still holds the old object.
                    if (error == ERROR_INVALID_PARAMETER) continue;
                    Fail(result, error, "OpenProcess for job retirement failed"); return false;
                }
                // Even if this ID was recycled, waiting is conservative: never infer
                // completion from job membership, which can disappear during exit.
                processes.push_back(std::move(process));
            }
            if (GetLastError() != WAIT_TIMEOUT) {
                Fail(result, GetLastError(), "Job completion dequeue failed"); return false;
            }
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                                           &accounting, sizeof(accounting), nullptr)) {
                Fail(result, GetLastError(), "QueryInformationJobObject failed"); return false;
            }
            if (accounting.ActiveProcesses == 0 && reportedProcesses == accounting.TotalProcesses) {
                const DWORD rootWait = WaitForSingleObject(rootProcess, 0);
                if (rootWait == WAIT_FAILED) {
                    Fail(result, GetLastError(), "Root process retirement wait failed"); return false;
                }
                bool signaled = rootWait == WAIT_OBJECT_0;
                for (const auto& process : processes) {
                    const DWORD wait = WaitForSingleObject(process->value, 0);
                    if (wait == WAIT_FAILED) {
                        Fail(result, GetLastError(), "Job process retirement wait failed"); return false;
                    }
                    signaled = signaled && wait == WAIT_OBJECT_0;
                }
                if (signaled) return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
    } catch (...) {
        Fail(result, ERROR_NOT_ENOUGH_MEMORY, "Job retirement tracking allocation failed"); return false;
    }
    Fail(result, ERROR_TIMEOUT, "Job retirement was not confirmed within 10 seconds");
    return false;
}
} // namespace

ProcessResult WindowsProcessHost::Run(const ProcessRequest& request,
                                      const std::atomic_bool* cancelRequested) {
    ProcessResult result;
    result.treeRetired = true; // No child exists until CreateProcessW succeeds.
    Handle job, completionPort, process, primaryThread, stdoutRead, stdoutWrite, stderrRead, stderrWrite, stdinNull;
    std::jthread stdoutReader, stderrReader;
    DWORD stdoutError = 0, stderrError = 0;
    bool assigned = false;
    const auto started = std::chrono::steady_clock::now();
    try {
        const std::wstring application = request.executable.wstring();
        const std::wstring directory = request.workingDirectory.wstring();
        bool valid = !application.empty() && request.executable.is_absolute() && !HasNull(application)
            && application.find(L'"') == std::wstring::npos && !HasNull(directory)
            && (directory.empty() || request.workingDirectory.is_absolute()) && request.timeout.count() >= 0;
        for (const auto& arg : request.arguments) valid = valid && !HasNull(arg);
        for (const auto& [name, value] : request.environmentOverrides)
            valid = valid && !name.empty() && !HasNull(name) && !HasNull(value)
                && name.find(L'=') == std::wstring::npos && name.size() <= 32767 && value.size() <= 32767;
        if (!valid) { Fail(result, ERROR_INVALID_PARAMETER, "Invalid explicit path, argument, environment or timeout"); return result; }
        // Cancellation wins over expiry when both are observed. Expiry wins over
        // process completion observed at/after the startup-inclusive wall limit.
        const auto observeStop = [&] {
            if (cancelRequested && cancelRequested->load()) { result.cancelled = true; return true; }
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started) >= request.timeout) {
                result.timedOut = true; return true;
            }
            return false;
        };
        if (observeStop()) return result;
        std::wstring command = Quote(application);
        for (const auto& arg : request.arguments) { command.push_back(L' '); command += Quote(arg); }
        if (command.size() >= 32767) { Fail(result, ERROR_BAD_LENGTH, "Windows command line exceeds 32767 UTF-16 units"); return result; }
        std::vector<wchar_t> environment;
        if (!MakeEnvironment(request, environment, result)) return result;
        job.value = CreateJobObjectW(nullptr, nullptr);
        if (!job.value) { Fail(result, GetLastError(), "CreateJobObjectW failed"); return result; }
        completionPort.value = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
        if (!completionPort.value) { Fail(result, GetLastError(), "CreateIoCompletionPort failed"); return result; }
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT association{};
        association.CompletionPort = completionPort.value;
        if (!SetInformationJobObject(job.value, JobObjectAssociateCompletionPortInformation,
                                     &association, sizeof(association))) {
            Fail(result, GetLastError(), "Job completion port association failed"); return result;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            Fail(result, GetLastError(), "SetInformationJobObject failed"); return result;
        }
        if (!Pipe(stdoutRead, stdoutWrite, result) || !Pipe(stderrRead, stderrWrite, result)) return result;
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        stdinNull.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stdinNull.value == INVALID_HANDLE_VALUE) { Fail(result, GetLastError(), "Open NUL stdin failed"); return result; }
        Attributes attributes;
        SIZE_T attributeBytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
        if (!attributeBytes) { Fail(result, GetLastError(), "Attribute list size query failed"); return result; }
        attributes.storage.resize(attributeBytes);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.storage.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeBytes)) {
            Fail(result, GetLastError(), "InitializeProcThreadAttributeList failed"); return result;
        }
        attributes.list = list;
        HANDLE inherited[] = {stdoutWrite.value, stderrWrite.value, stdinNull.value};
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                        inherited, sizeof(inherited), nullptr, nullptr)) {
            Fail(result, GetLastError(), "UpdateProcThreadAttribute failed"); return result;
        }
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = stdoutWrite.value;
        startup.StartupInfo.hStdError = stderrWrite.value;
        startup.StartupInfo.hStdInput = stdinNull.value;
        startup.lpAttributeList = list;
        PROCESS_INFORMATION child{};
        if (observeStop()) return result;
        if (!CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, TRUE,
                            CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                            environment.data(), directory.empty() ? nullptr : directory.c_str(),
                            &startup.StartupInfo, &child)) {
            Fail(result, GetLastError(), "CreateProcessW failed"); return result;
        }
        process.value = child.hProcess;
        primaryThread.value = child.hThread;
        result.launched = true;
        result.treeRetired = false;
        stdoutWrite.Reset(); stderrWrite.Reset(); stdinNull.Reset();
        if (!AssignProcessToJobObject(job.value, process.value)) {
            Fail(result, GetLastError(), "AssignProcessToJobObject failed; child never resumed");
        } else {
            assigned = true;
            stdoutReader = std::jthread(Drain, stdoutRead.value, request.maxCapturedBytesPerStream,
                std::ref(result.stdoutBytes), std::ref(result.stdoutTotalBytes), std::ref(result.stdoutTruncated), std::ref(stdoutError));
            stderrReader = std::jthread(Drain, stderrRead.value, request.maxCapturedBytesPerStream,
                std::ref(result.stderrBytes), std::ref(result.stderrTotalBytes), std::ref(result.stderrTruncated), std::ref(stderrError));
            if (observeStop()) {
                // Startup consumed the budget, or cancellation arrived. Keep the
                // child suspended and retire it through the common cleanup below.
            } else if (ResumeThread(primaryThread.value) == static_cast<DWORD>(-1)) {
                Fail(result, GetLastError(), "ResumeThread failed");
            } else {
                for (;;) {
                    if (observeStop()) break;
                    const DWORD wait = WaitForSingleObject(process.value, 10);
                    if (wait == WAIT_FAILED) { Fail(result, GetLastError(), "WaitForSingleObject failed"); break; }
                    if (observeStop()) break;
                    if (wait == WAIT_OBJECT_0) break;
                }
            }
        }
    } catch (const std::exception&) {
        Fail(result, ERROR_NOT_ENOUGH_MEMORY, "Process host allocation or reader startup failed");
    } catch (...) {
        Fail(result, ERROR_GEN_FAILURE, "Unexpected process host failure");
    }
    if (result.launched) {
        if (assigned) result.treeRetired = RetireJob(job.value, completionPort.value, process.value, result);
        else {
            // The primary thread is still suspended: it cannot have spawned children.
            if (!TerminateProcess(process.value, ERROR_PROCESS_ABORTED))
                Fail(result, GetLastError(), "TerminateProcess of unassigned suspended child failed");
            result.treeRetired = WaitForSingleObject(process.value, 10000) == WAIT_OBJECT_0;
            if (!result.treeRetired) Fail(result, ERROR_TIMEOUT, "Suspended child retirement was not confirmed");
        }
        result.exited = WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0;
        if (result.exited) {
            DWORD exit = 0;
            if (GetExitCodeProcess(process.value, &exit)) result.exitCode = exit;
            else Fail(result, GetLastError(), "GetExitCodeProcess failed");
        }
    }
    // Job closure is a final kill-on-close safeguard, including failed retirement queries.
    job.Reset();
    stdoutReader.request_stop(); stderrReader.request_stop();
    if (stdoutReader.joinable()) stdoutReader.join();
    if (stderrReader.joinable()) stderrReader.join();
    if (stdoutError) Fail(result, stdoutError, "Stdout drain or capture failed");
    if (stderrError) Fail(result, stderrError, "Stderr drain or capture failed");
    return result;
}
} // namespace MonsterEngine::VideoTo3D
