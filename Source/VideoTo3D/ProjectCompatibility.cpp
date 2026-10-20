// Copyright Monster Engine. All Rights Reserved.
#include "VideoTo3D/ProjectPreflight.h"
#include "VideoTo3D/ProjectInput.h"
#include "PreflightWindowsPath.h"

#ifndef _WIN32
#error ProjectCompatibility is a Windows-only product service.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstring>

namespace MonsterEngine::VideoTo3D {
namespace {

[[noreturn]] void Fail(PreflightCode code, const char* message, DWORD error = 0) {
    throw PreflightFailure({code, message, error});
}

void FileFailure(DWORD error) {
    if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD)
        Fail(PreflightCode::AccessDenied, "Metadata read access was denied; choose an accessible project or correct its permissions.", error);
    if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION || error == ERROR_DELETE_PENDING)
        Fail(PreflightCode::UnstableObservation, "Cannot pin metadata while conflicting access is active; retry when the project is idle.", error);
    Fail(PreflightCode::IoFailure, "Cannot open or inspect project metadata; check the native error and retry.", error);
}

bool Missing(DWORD error) { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }

class MetadataFile final {
public:
    MetadataFile() = default;
    ~MetadataFile() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    MetadataFile(const MetadataFile&) = delete;
    MetadataFile& operator=(const MetadataFile&) = delete;

    bool Open(const std::filesystem::path& path) {
        handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (Missing(error)) return false;
            FileFailure(error);
        }
        initial_ = Information();
        if (GetFileType(handle_) != FILE_TYPE_DISK ||
            (initial_.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            initial_.nNumberOfLinks != 1)
            Fail(PreflightCode::UnsafePath, "Metadata must be a regular disk file without reparse points or hardlink aliases.");
        // An existing writable mapping may outlive its creating handle. Metadata
        // comparisons below catch observable changes, but cannot certify arbitrary
        // adversarial mappings or filesystem/hardware behavior.
        return true;
    }

    std::string Read(std::size_t limit) {
        const auto size = (std::uint64_t(initial_.nFileSizeHigh) << 32) | initial_.nFileSizeLow;
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(size, limit));
        LARGE_INTEGER start{};
        if (!SetFilePointerEx(handle_, start, nullptr, FILE_BEGIN)) FileFailure(GetLastError());
        std::string bytes(count, '\0');
        DWORD read = 0;
        if (count && !ReadFile(handle_, bytes.data(), static_cast<DWORD>(count), &read, nullptr))
            FileFailure(GetLastError());
        if (read != count)
            Fail(PreflightCode::UnstableObservation, "Metadata changed or became unreadable during bounded reading; retry when idle.");
        return bytes;
    }

    std::string ReadAll(std::size_t maximum) {
        if (initial_.nFileSizeHigh || initial_.nFileSizeLow > maximum)
            Fail(PreflightCode::CorruptData, "Supported metadata exceeds its documented encoded-size bound; preserve the file and inspect its origin.");
        return Read(maximum);
    }

    void VerifyUnchanged(const std::filesystem::path& path) const {
        const auto after = Information();
        if (!Same(initial_, after))
            Fail(PreflightCode::UnstableObservation, "Inspected metadata identity, size, attributes or write time changed; retry when idle.");
        // With delete sharing denied, the opened entry cannot be replaced during
        // this lifetime. Check the named entry as well rather than assuming it.
        MetadataFile named;
        if (!named.Open(path) || !Same(initial_, named.initial_))
            Fail(PreflightCode::UnstableObservation, "Metadata path no longer identifies the inspected file; retry when idle.");
    }

private:
    BY_HANDLE_FILE_INFORMATION Information() const {
        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(handle_, &information)) FileFailure(GetLastError());
        return information;
    }
    static bool Same(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) {
        return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
            a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow &&
            a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
            a.dwFileAttributes == b.dwFileAttributes && a.nNumberOfLinks == b.nNumberOfLinks &&
            CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0 &&
            CompareFileTime(&a.ftCreationTime, &b.ftCreationTime) == 0;
    }
    HANDLE handle_{INVALID_HANDLE_VALUE};
    BY_HANDLE_FILE_INFORMATION initial_{};
};

std::uint32_t U32(const std::string& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4)
        Fail(PreflightCode::CorruptData, "Metadata header is truncated; preserve the original file.");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    return value;
}

void CheckHeader(const std::string& bytes, const char* magic, CompatibilityFileResult& result) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), magic, 8) != 0)
        Fail(PreflightCode::CorruptData, "Metadata signature or schema framing is invalid/truncated; preserve the original file.");
    result.schemaVersion = U32(bytes, 8);
    if (*result.schemaVersion != 1)
        Fail(PreflightCode::UnsupportedSchema, "Unsupported metadata schema; use a compatible application. No migration was attempted.");
}

bool IsUuid(const std::string& value) {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') return false;
        } else if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    }
    return true;
}

void InspectSnapshot(MetadataFile& file, CompatibilityFileResult& result) {
    try {
        CheckHeader(file.Read(12), "MSTPROJ1", result);
        const auto snapshot = ProjectRepository::DecodeSnapshotBytes(file.ReadAll(kMaxSnapshotBytes));
        result.projectId = snapshot.projectId;
        result.fullyValidatedStructure = true;
        result.diagnostic = {PreflightCode::Ok, "Supported project v1 metadata structure validated; run states and staging files were not changed.", 0};
    } catch (const ProjectError& error) {
        result.diagnostic = {PreflightCode::CorruptData, std::string("Project metadata validation failed: ") + error.what(), 0};
    } catch (const PreflightFailure& error) { result.diagnostic = error.Diagnostic(); }
}

void InspectInput(MetadataFile& file, const std::string& projectId, CompatibilityFileResult& result) {
    try {
        CheckHeader(file.Read(12), "MSTINPUT", result);
        // Only inspect bounded v1 identity framing before calling the existing
        // full decoder, so an unknown schema is never parsed as v1.
        const auto prefix = file.Read(52);
        if (U32(prefix, 12) != 36 || prefix.size() < 52 || !IsUuid(prefix.substr(16, 36)))
            Fail(PreflightCode::CorruptData, "Input metadata contains malformed/truncated project identity framing.");
        result.projectId = prefix.substr(16, 36);
        if (result.projectId != projectId)
            Fail(PreflightCode::IdentityMismatch, "Input belongs to a different project identity; preserve both original metadata files.");
        const auto manifest = ProjectInput::DecodeManifestBytes(file.ReadAll(kMaxInputManifestBytes), projectId);
        result.projectId = manifest.projectId;
        result.fullyValidatedStructure = true;
        result.diagnostic = {PreflightCode::Ok, "Supported input v1 metadata structure validated; referenced media bytes were not read or hashed.", 0};
    } catch (const InputError& error) {
        result.diagnostic = {PreflightCode::CorruptData, std::string("Input metadata validation failed: ") + error.what(), 0};
    } catch (const PreflightFailure& error) { result.diagnostic = error.Diagnostic(); }
}

} // namespace

CompatibilityPreflightResult InspectProjectCompatibility(const std::filesystem::path& directory) {
    CompatibilityPreflightResult result;
    result.project.diagnostic.message = "Project metadata was not inspected.";
    result.input.diagnostic.message = "Input metadata was not inspected.";
    CompatibilityFileResult* active = &result.project;
    try {
        PreflightDetail::DirectoryGuard guard(directory);
        const auto projectPath = guard.Path() / L"project.mstudio";
        const auto inputPath = guard.Path() / L"input.mstudio";
        MetadataFile project;
        if (!project.Open(projectPath))
            Fail(PreflightCode::MissingProject, "No project.mstudio exists in the selected directory; select an existing project.");
        InspectSnapshot(project, result.project);
        if (result.project.diagnostic.code != PreflightCode::Ok) {
            project.VerifyUnchanged(projectPath);
            result.stableObservation = true;
            result.diagnostic = result.project.diagnostic;
            return result;
        }
        active = &result.input;
        MetadataFile input;
        result.inputPresent = input.Open(inputPath);
        if (result.inputPresent) InspectInput(input, result.project.projectId, result.input);
        else result.input.diagnostic = {PreflightCode::NoInput, "No optional input.mstudio is present; no media integrity was checked.", 0};

        project.VerifyUnchanged(projectPath);
        if (result.inputPresent) input.VerifyUnchanged(inputPath);
        else {
            // Child entry creation is not prevented by a directory guard. Recheck
            // absence while the project is still pinned; do not combine an old
            // absence observation with a later project snapshot.
            MetadataFile recheck;
            if (recheck.Open(inputPath))
                Fail(PreflightCode::UnstableObservation, "Optional input appeared during inspection; retry to inspect a consistent metadata view.");
        }
        result.stableObservation = true;
        if (result.input.diagnostic.code != PreflightCode::Ok && result.input.diagnostic.code != PreflightCode::NoInput)
            result.diagnostic = result.input.diagnostic;
        else result.diagnostic = {PreflightCode::Ok, "Compatible bounded metadata observation; media integrity and future application/tool compatibility remain unverified.", 0};
    } catch (const PreflightFailure& error) {
        result.diagnostic = error.Diagnostic();
        active->diagnostic = error.Diagnostic();
        result.stableObservation = false;
    } catch (const std::exception&) {
        result.diagnostic = {PreflightCode::IoFailure, "Unexpected metadata inspection failure; preserve the project and retry when idle.", 0};
        active->diagnostic = result.diagnostic;
        result.stableObservation = false;
    }
    return result;
}

} // namespace MonsterEngine::VideoTo3D
