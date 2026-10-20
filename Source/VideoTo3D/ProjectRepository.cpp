// Copyright Monster Engine. All Rights Reserved.
#include "VideoTo3D/ProjectRepository.h"

#ifndef _WIN32
#error ProjectRepository is a Windows-only product service.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MonsterEngine::VideoTo3D {
namespace {
constexpr wchar_t kSnapshotName[] = L"project.mstudio";
constexpr wchar_t kLockName[] = L".monsterstudio.lock";
constexpr char kMagic[] = "MSTPROJ1";

class Handle final {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    ~Handle() { if (Valid()) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.value_) { other.value_ = INVALID_HANDLE_VALUE; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { if (Valid()) CloseHandle(value_); value_ = other.value_; other.value_ = INVALID_HANDLE_VALUE; }
        return *this;
    }
    bool Valid() const noexcept { return value_ != INVALID_HANDLE_VALUE && value_ != nullptr; }
    HANDLE Get() const noexcept { return value_; }
private:
    HANDLE value_;
};
class FindHandle final {
public:
    explicit FindHandle(HANDLE value) noexcept : value_(value) {}
    ~FindHandle() { if (Valid()) FindClose(value_); }
    FindHandle(const FindHandle&) = delete;
    FindHandle& operator=(const FindHandle&) = delete;
    bool Valid() const noexcept { return value_ != INVALID_HANDLE_VALUE; }
    HANDLE Get() const noexcept { return value_; }
private:
    HANDLE value_;
};

[[noreturn]] void Fail(ProjectErrorCode code, const char* message) { throw ProjectError(code, message); }
[[noreturn]] void WinFail(const char* message) {
    const DWORD code = GetLastError();
    throw ProjectError(ProjectErrorCode::IoFailure, std::string(message) + " (Win32 " + std::to_string(code) + ")");
}
bool IsUuid(const std::string& value) {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i != value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (value[i] != '-') return false; }
        else if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    }
    return true;
}
bool ValidUtf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char a = static_cast<unsigned char>(text[i++]);
        if (a == 0) return false;
        if (a < 0x80) continue;
        unsigned need = 0; unsigned cp = 0; unsigned min = 0;
        if (a >= 0xc2 && a <= 0xdf) { need = 1; cp = a & 0x1f; min = 0x80; }
        else if (a >= 0xe0 && a <= 0xef) { need = 2; cp = a & 0x0f; min = 0x800; }
        else if (a >= 0xf0 && a <= 0xf4) { need = 3; cp = a & 7; min = 0x10000; }
        else return false;
        if (text.size() - i < need) return false;
        for (unsigned j = 0; j < need; ++j) {
            const unsigned char b = static_cast<unsigned char>(text[i++]);
            if ((b & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (b & 0x3f);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
void CheckText(const std::string& value, std::size_t maximum, const char* label) {
    if (value.size() > maximum) Fail(ProjectErrorCode::LimitExceeded, label);
    if (value.empty() || !ValidUtf8(value)) Fail(ProjectErrorCode::InvalidInput, label);
}
void CheckReference(const std::string& reference) {
    CheckText(reference, kMaxEvidenceReferenceBytes, "Invalid evidence reference text or size");
    if (reference.find(':') != std::string::npos || reference.front() == '/' || reference.front() == '\\')
        Fail(ProjectErrorCode::UnsafePath, "Evidence reference must be a relative path");
    std::size_t start = 0;
    for (std::size_t i = 0; i <= reference.size(); ++i) {
        if (i != reference.size() && reference[i] != '/' && reference[i] != '\\') continue;
        const std::string component = reference.substr(start, i - start);
        if (component.empty() || component == "." || component == ".." ||
            component.back() == ' ' || component.back() == '.' ||
            component.find_first_of("<>\"|?*") != std::string::npos)
            Fail(ProjectErrorCode::UnsafePath, "Evidence reference contains an unsafe component");
        for (unsigned char c : component) if (c < 0x20) Fail(ProjectErrorCode::UnsafePath, "Evidence reference contains a control character");
        start = i + 1;
    }
}

class Writer final {
public:
    std::string bytes;
    void U32(std::uint32_t v) { for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<char>(v >> (8 * i))); }
    void U64(std::uint64_t v) { for (unsigned i = 0; i < 8; ++i) bytes.push_back(static_cast<char>(v >> (8 * i))); }
    void Text(const std::string& v) { U32(static_cast<std::uint32_t>(v.size())); bytes.append(v); }
};
std::string Encode(const ProjectSnapshot& snapshot) {
    Writer writer;
    writer.bytes.append(kMagic, 8);
    writer.U32(snapshot.schemaVersion);
    writer.U64(snapshot.revision);
    writer.Text(snapshot.projectId);
    writer.Text(snapshot.name);
    writer.U32(static_cast<std::uint32_t>(snapshot.runs.size()));
    for (const auto& run : snapshot.runs) {
        writer.Text(run.runId); writer.Text(run.stageId);
        writer.U64(run.generation); writer.U64(run.lastEventSequence);
        writer.U32(static_cast<std::uint32_t>(run.state));
        writer.Text(run.requestBytes);
        writer.U32(static_cast<std::uint32_t>(run.evidenceReferences.size()));
        for (const auto& reference : run.evidenceReferences) writer.Text(reference);
    }
    if (writer.bytes.size() > kMaxSnapshotBytes) Fail(ProjectErrorCode::LimitExceeded, "Project snapshot exceeds 16 MiB");
    return writer.bytes;
}
class Reader final {
public:
    explicit Reader(const std::string& bytes) : bytes_(bytes) {}
    std::string Raw(std::size_t size) {
        if (size > bytes_.size() - offset_) Fail(ProjectErrorCode::InvalidInput, "Project file is truncated");
        std::string result = bytes_.substr(offset_, size); offset_ += size; return result;
    }
    std::uint32_t U32() { auto value = Raw(4); std::uint32_t result = 0; for (unsigned i = 0; i < 4; ++i) result |= std::uint32_t(static_cast<unsigned char>(value[i])) << (8 * i); return result; }
    std::uint64_t U64() { auto value = Raw(8); std::uint64_t result = 0; for (unsigned i = 0; i < 8; ++i) result |= std::uint64_t(static_cast<unsigned char>(value[i])) << (8 * i); return result; }
    std::string Text(std::size_t maximum) {
        const auto size = U32();
        if (size > maximum) Fail(ProjectErrorCode::LimitExceeded, "Project string exceeds its size limit");
        return Raw(size);
    }
    bool Done() const noexcept { return offset_ == bytes_.size(); }
private:
    const std::string& bytes_;
    std::size_t offset_{0};
};
ProjectSnapshot Decode(const std::string& bytes) {
    if (bytes.size() > kMaxSnapshotBytes) Fail(ProjectErrorCode::LimitExceeded, "Project file exceeds 16 MiB");
    Reader reader(bytes);
    if (reader.Raw(8) != std::string(kMagic, 8)) Fail(ProjectErrorCode::InvalidInput, "Project file has an invalid signature");
    ProjectSnapshot snapshot;
    snapshot.schemaVersion = reader.U32();
    if (snapshot.schemaVersion != 1) Fail(ProjectErrorCode::IncompatibleSchema, "Unsupported project schema version");
    snapshot.revision = reader.U64();
    snapshot.projectId = reader.Text(36);
    snapshot.name = reader.Text(kMaxProjectNameBytes);
    const auto count = reader.U32();
    if (count > kMaxRuns) Fail(ProjectErrorCode::LimitExceeded, "Project has too many runs");
    snapshot.runs.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        PlannedRun run;
        run.runId = reader.Text(36); run.stageId = reader.Text(36);
        run.generation = reader.U64(); run.lastEventSequence = reader.U64();
        const auto state = reader.U32();
        if (state > static_cast<std::uint32_t>(RunState::Failed)) Fail(ProjectErrorCode::InvalidInput, "Project has an unknown run state");
        run.state = static_cast<RunState>(state);
        run.requestBytes = reader.Text(kMaxRequestBytes);
        const auto refs = reader.U32();
        if (refs > kMaxEvidenceReferences) Fail(ProjectErrorCode::LimitExceeded, "Run has too many evidence references");
        run.evidenceReferences.reserve(refs);
        for (std::uint32_t j = 0; j < refs; ++j) run.evidenceReferences.push_back(reader.Text(kMaxEvidenceReferenceBytes));
        snapshot.runs.push_back(std::move(run));
    }
    if (!reader.Done()) Fail(ProjectErrorCode::InvalidInput, "Project file contains trailing bytes");
    ProjectRepository::ValidateSnapshot(snapshot);
    return snapshot;
}

std::filesystem::path PhysicalDirectory(const std::filesystem::path& path) {
    if (path.empty()) Fail(ProjectErrorCode::InvalidInput, "Project directory is empty");
    Handle directory(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!directory.Valid()) WinFail("Cannot open project directory");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(directory.Get(), &info)) WinFail("Cannot inspect project directory");
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) Fail(ProjectErrorCode::UnsafePath, "Project location is not a directory");
    const DWORD length = GetFinalPathNameByHandleW(directory.Get(), nullptr, 0, FILE_NAME_NORMALIZED);
    if (!length) WinFail("Cannot resolve project directory");
    std::wstring result(length + 1, L'\0');
    const DWORD copied = GetFinalPathNameByHandleW(directory.Get(), result.data(), static_cast<DWORD>(result.size()), FILE_NAME_NORMALIZED);
    if (!copied || copied > length) WinFail("Cannot resolve project directory");
    result.resize(copied);
    return std::filesystem::path(result);
}
void CheckManaged(const std::filesystem::path& path, bool mayBeMissing) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (mayBeMissing && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) return;
        SetLastError(error); WinFail("Cannot inspect managed project file");
    }
    if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
        Fail(ProjectErrorCode::UnsafePath, "Managed project file is redirected or is a directory");
}
void RequireEmptyDirectory(const std::filesystem::path& directory) {
    WIN32_FIND_DATAW entry{};
    FindHandle find(FindFirstFileW((directory / L"*").c_str(), &entry));
    if (!find.Valid()) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return;
        WinFail("Cannot inspect project directory contents");
    }
    do {
        const std::wstring item(entry.cFileName);
        if (item != L"." && item != L".." && item != kLockName)
            Fail(ProjectErrorCode::Conflict, "Project directory already contains files; choose an empty location");
    } while (FindNextFileW(find.Get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) WinFail("Cannot finish inspecting project directory");
}
Handle AcquireLock(const std::filesystem::path& directory) {
    const auto path = directory / kLockName;
    CheckManaged(path, true);
    Handle lock(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!lock.Valid()) {
        const DWORD code = GetLastError();
        if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION)
            Fail(ProjectErrorCode::OwnershipConflict, "Project is already open in another process");
        SetLastError(code); WinFail("Cannot acquire project ownership");
    }
    CheckManaged(path, false);
    return lock;
}
std::string ReadSnapshot(const std::filesystem::path& path) {
    CheckManaged(path, false);
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file.Valid()) WinFail("Cannot open project snapshot");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.Get(), &info)) WinFail("Cannot inspect open project snapshot");
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
        Fail(ProjectErrorCode::UnsafePath, "Open project snapshot is redirected or is a directory");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.Get(), &size)) WinFail("Cannot measure project snapshot");
    if (size.QuadPart < 0 || size.QuadPart > static_cast<LONGLONG>(kMaxSnapshotBytes))
        Fail(ProjectErrorCode::LimitExceeded, "Project file exceeds 16 MiB");
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty() && (!ReadFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()))
        WinFail("Cannot read complete project snapshot");
    return bytes;
}
void Publish(const std::filesystem::path& directory, const std::string& projectId,
             const std::string& bytes, bool existing) {
    const auto primary = directory / kSnapshotName;
    CheckManaged(primary, !existing);
    const std::string id = ProjectRepository::NewId();
    const std::wstring temporaryName = L".monsterstudio-" + std::wstring(projectId.begin(), projectId.end()) +
                                       L"-" + std::wstring(id.begin(), id.end()) + L".tmp";
    const auto staging = directory / temporaryName;
    Handle file(CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file.Valid()) WinFail("Cannot create project staging file");
    try {
        DWORD written = 0;
        if (!WriteFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size())
            WinFail("Cannot write complete project staging file");
        if (!FlushFileBuffers(file.Get())) WinFail("Cannot flush project staging file");
        file = Handle();
        CheckManaged(primary, !existing);
        if (existing) {
            if (!MoveFileExW(staging.c_str(), primary.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                WinFail("Cannot atomically replace project snapshot");
        } else {
            if (!MoveFileExW(staging.c_str(), primary.c_str(), MOVEFILE_WRITE_THROUGH))
                WinFail("Cannot publish new project snapshot");
        }
    } catch (...) {
        file = Handle();
        DeleteFileW(staging.c_str());
        throw;
    }
}
void CleanOwnedStages(const std::filesystem::path& directory, const ProjectSnapshot& snapshot) {
    const std::wstring prefix = L".monsterstudio-" + std::wstring(snapshot.projectId.begin(), snapshot.projectId.end()) + L"-";
    WIN32_FIND_DATAW entry{};
    FindHandle find(FindFirstFileW((directory / L".monsterstudio-*.tmp").c_str(), &entry));
    if (!find.Valid()) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) WinFail("Cannot enumerate project staging files");
        return;
    }
    do {
        const std::wstring name(entry.cFileName);
        if (name.size() != prefix.size() + 36 + 4 || name.compare(0, prefix.size(), prefix) != 0 ||
            name.substr(name.size() - 4) != L".tmp" || (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        std::string stageId;
        stageId.reserve(36);
        for (std::size_t i = prefix.size(); i < prefix.size() + 36; ++i) {
            if (name[i] > 0x7f) { stageId.clear(); break; }
            stageId.push_back(static_cast<char>(name[i]));
        }
        if (!IsUuid(stageId)) continue;
        const auto candidate = directory / name;
        try {
            const auto staged = Decode(ReadSnapshot(candidate));
            if (staged.projectId == snapshot.projectId && staged.revision <= snapshot.revision)
                DeleteFileW(candidate.c_str());
        } catch (const ProjectError&) { /* Unknown or damaged remnants are user bytes. */ }
    } while (FindNextFileW(find.Get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) WinFail("Cannot finish enumerating project staging files");
}
} // namespace

struct ProjectRepository::Impl {
    std::filesystem::path directory;
    Handle lock;
    ProjectSnapshot snapshot;
};

ProjectRepository::ProjectRepository(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ProjectRepository::~ProjectRepository() = default;
const ProjectSnapshot& ProjectRepository::Snapshot() const noexcept { return impl_->snapshot; }
const std::filesystem::path& ProjectRepository::Directory() const noexcept { return impl_->directory; }

ProjectSnapshot ProjectRepository::DecodeSnapshotBytes(const std::string& bytes) {
    return Decode(bytes);
}

void ProjectRepository::ValidateSnapshot(const ProjectSnapshot& snapshot) {
    if (snapshot.schemaVersion != 1) Fail(ProjectErrorCode::IncompatibleSchema, "Unsupported project schema version");
    if (!IsUuid(snapshot.projectId)) Fail(ProjectErrorCode::InvalidInput, "Invalid project identifier");
    CheckText(snapshot.name, kMaxProjectNameBytes, "Invalid project name or size");
    if (snapshot.name == "." || snapshot.name == ".." || snapshot.name.back() == ' ' || snapshot.name.back() == '.' ||
        snapshot.name.find_first_of("<>:\"/\\|?*") != std::string::npos)
        Fail(ProjectErrorCode::InvalidInput, "Project name contains Windows path characters");
    for (unsigned char c : snapshot.name) if (c < 0x20) Fail(ProjectErrorCode::InvalidInput, "Project name contains a control character");
    if (snapshot.runs.size() > kMaxRuns) Fail(ProjectErrorCode::LimitExceeded, "Project has too many runs");
    std::unordered_set<std::string> allIds;
    allIds.insert(snapshot.projectId);
    for (const auto& run : snapshot.runs) {
        if (!IsUuid(run.runId) || !IsUuid(run.stageId) || run.generation == 0 ||
            static_cast<std::uint32_t>(run.state) > static_cast<std::uint32_t>(RunState::Failed))
            Fail(ProjectErrorCode::InvalidInput, "Run identity, generation or state is invalid");
        if ((run.state == RunState::Pending && run.lastEventSequence != 0) ||
            (run.state != RunState::Pending && run.state != RunState::Canceled && run.lastEventSequence == 0))
            Fail(ProjectErrorCode::InvalidInput, "Run state and event sequence disagree");
        if (!allIds.insert(run.runId).second || !allIds.insert(run.stageId).second)
            Fail(ProjectErrorCode::Conflict, "Duplicate project, run or stage identifier");
        CheckText(run.requestBytes, kMaxRequestBytes, "Invalid planned request text or size");
        if (run.evidenceReferences.size() > kMaxEvidenceReferences)
            Fail(ProjectErrorCode::LimitExceeded, "Run has too many evidence references");
        for (const auto& reference : run.evidenceReferences) CheckReference(reference);
    }
    (void)Encode(snapshot);
}

std::string ProjectRepository::NewId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) Fail(ProjectErrorCode::IoFailure, "Cannot generate project identifier");
    char text[37]{};
    std::snprintf(text, sizeof(text), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  static_cast<unsigned>(guid.Data1), static_cast<unsigned>(guid.Data2), static_cast<unsigned>(guid.Data3),
                  guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return text;
}

std::unique_ptr<ProjectRepository> ProjectRepository::Create(const std::filesystem::path& directory, const std::string& name) {
    ProjectSnapshot initial;
    initial.projectId = NewId(); initial.name = name;
    ValidateSnapshot(initial);
    if (directory.empty()) Fail(ProjectErrorCode::InvalidInput, "Project directory is empty");
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        WinFail("Cannot create project directory");
    auto impl = std::make_unique<Impl>();
    impl->directory = PhysicalDirectory(directory);
    RequireEmptyDirectory(impl->directory);
    impl->lock = AcquireLock(impl->directory);
    RequireEmptyDirectory(impl->directory);
    Publish(impl->directory, initial.projectId, Encode(initial), false);
    impl->snapshot = std::move(initial);
    return std::unique_ptr<ProjectRepository>(new ProjectRepository(std::move(impl)));
}

std::unique_ptr<ProjectRepository> ProjectRepository::Open(const std::filesystem::path& directory) {
    auto impl = std::make_unique<Impl>();
    impl->directory = PhysicalDirectory(directory);
    impl->lock = AcquireLock(impl->directory);
    impl->snapshot = Decode(ReadSnapshot(impl->directory / kSnapshotName));
    CleanOwnedStages(impl->directory, impl->snapshot);
    return std::unique_ptr<ProjectRepository>(new ProjectRepository(std::move(impl)));
}

void ProjectRepository::Commit(const ProjectSnapshot& next) {
    ValidateSnapshot(next);
    const auto& old = impl_->snapshot;
    if (next.projectId != old.projectId || next.name != old.name || old.revision == UINT64_MAX ||
        next.revision != old.revision + 1)
        Fail(ProjectErrorCode::Conflict, "Project identity or next revision does not match current snapshot");
    if (next.runs.size() < old.runs.size()) Fail(ProjectErrorCode::Conflict, "Historical runs cannot be removed");
    std::unordered_map<std::string, const PlannedRun*> previous;
    for (const auto& run : old.runs) previous.emplace(run.runId, &run);
    for (const auto& run : next.runs) {
        const auto it = previous.find(run.runId);
        if (it == previous.end()) continue;
        const auto& before = *it->second;
        if (run.stageId != before.stageId || run.generation != before.generation ||
            run.requestBytes != before.requestBytes || run.evidenceReferences != before.evidenceReferences ||
            run.lastEventSequence < before.lastEventSequence)
            Fail(ProjectErrorCode::Conflict, "Historical run identity or immutable request changed");
        previous.erase(it);
    }
    if (!previous.empty()) Fail(ProjectErrorCode::Conflict, "Historical run identity was removed");
    ProjectSnapshot durable = next;
    const auto bytes = Encode(durable);
    Publish(impl_->directory, next.projectId, bytes, true);
    using std::swap;
    swap(impl_->snapshot, durable);
}
} // namespace MonsterEngine::VideoTo3D
