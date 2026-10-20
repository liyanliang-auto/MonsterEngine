// Copyright Monster Engine. All Rights Reserved.
#include "PreflightWindowsPath.h"
#include "VideoTo3D/ProjectPreflight.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <string>
#include <vector>

namespace MonsterEngine::VideoTo3D::PreflightDetail {
namespace {

[[noreturn]] void Fail(PreflightCode code, const char* message, DWORD error = 0) {
    throw PreflightFailure({code, message, error});
}

wchar_t UpperAscii(wchar_t value) {
    return value >= L'a' && value <= L'z' ? value - L'a' + L'A' : value;
}

void ValidateComponent(const std::wstring& value) {
    if (value.empty() || value.size() > 255 || value == L"." || value == L".." ||
        value.back() == L'.' || value.back() == L' ')
        Fail(PreflightCode::UnsupportedPath, "Use nonempty path components of at most 255 UTF-16 units without dot or space suffixes.");
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned>(value[i]);
        if (c < 32 || value[i] == L':' || value[i] == L'/' || value[i] == L'"' ||
            value[i] == L'<' || value[i] == L'>' || value[i] == L'|' || value[i] == L'?' || value[i] == L'*')
            Fail(PreflightCode::UnsupportedPath, "Path contains a control character, alternate stream, or unsupported separator/character.");
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i >= value.size() || value[i] < 0xdc00 || value[i] > 0xdfff)
                Fail(PreflightCode::UnsupportedPath, "Path must contain valid UTF-16.");
        } else if (c >= 0xdc00 && c <= 0xdfff) {
            Fail(PreflightCode::UnsupportedPath, "Path must contain valid UTF-16.");
        }
    }
    auto base = value.substr(0, value.find(L'.'));
    // Win32 device aliases also accept spaces before an extension.
    while (!base.empty() && base.back() == L' ') base.pop_back();
    for (auto& c : base) c = UpperAscii(c);
    const bool numbered = base.size() == 4 && (base.substr(0, 3) == L"COM" || base.substr(0, 3) == L"LPT") &&
        ((base[3] >= L'1' && base[3] <= L'9') || base[3] == 0x00b9 || base[3] == 0x00b2 || base[3] == 0x00b3);
    if (numbered || base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" ||
        base == L"CLOCK$" || base == L"CONIN$" || base == L"CONOUT$")
        Fail(PreflightCode::UnsupportedPath, "DOS device-name components are unsupported; select an ordinary directory.");
}

void OpenFailure(DWORD error) {
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_DRIVE)
        Fail(PreflightCode::MissingDestination, "Destination or ancestor does not exist; select an existing directory.", error);
    if (error == ERROR_DIRECTORY)
        Fail(PreflightCode::NotDirectory, "A destination path component is not a directory.", error);
    if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD)
        Fail(PreflightCode::AccessDenied, "Cannot inspect destination ancestors; select an accessible directory.", error);
    if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION)
        Fail(PreflightCode::UnstableObservation, "Cannot pin destination identity while another handle permits conflicting access; retry when idle.", error);
    Fail(PreflightCode::IoFailure, "Cannot open a destination ancestor for identity inspection.", error);
}

void ValidateHandle(HANDLE handle, const std::wstring& expected) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info))
        Fail(PreflightCode::IoFailure, "Cannot inspect pinned directory attributes.", GetLastError());
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        Fail(PreflightCode::UnsafePath, "Reparse points, junctions and symbolic links are unsupported in destination ancestry.");
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        Fail(PreflightCode::NotDirectory, "Destination or ancestor is a regular file; select an existing directory.");
    const DWORD count = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count) Fail(PreflightCode::IoFailure, "Cannot resolve pinned directory identity.", GetLastError());
    if (count > 32768) Fail(PreflightCode::UnsafePath, "Resolved directory identity exceeds supported bounds.");
    std::wstring finalPath(count, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(handle, finalPath.data(), count, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!written || written >= count)
        Fail(PreflightCode::UnstableObservation, "Directory identity resolution changed or failed; retry when idle.", GetLastError());
    finalPath.resize(written);
    // Windows may omit the terminal separator on an opened drive root.
    if (expected.size() == 7 && finalPath.size() == 6) finalPath += L'\\';
    if (CompareStringOrdinal(expected.c_str(), static_cast<int>(expected.size()), finalPath.c_str(),
                             static_cast<int>(finalPath.size()), TRUE) != CSTR_EQUAL)
        Fail(PreflightCode::UnsafePath, "Resolved path differs from the requested directory; short-name, SUBST and redirected aliases are unsupported.");
}

} // namespace

std::filesystem::path NormalizePreflightPath(const std::filesystem::path& path) {
    auto value = path.native();
    if (value.rfind(L"\\\\?\\", 0) == 0) value.erase(0, 4);
    if (value.size() < 3 || !((value[0] >= L'A' && value[0] <= L'Z') || (value[0] >= L'a' && value[0] <= L'z')) ||
        value[1] != L':' || value[2] != L'\\')
        Fail(PreflightCode::UnsupportedPath, "Use an absolute local drive path, optionally with an extended DOS prefix; relative, UNC and device paths are unsupported.");
    if (value.find(L"\\\\", 2) != std::wstring::npos)
        Fail(PreflightCode::UnsupportedPath, "Repeated path separators are unsupported.");
    if (value.size() > 3 && value.back() == L'\\') value.pop_back();
    value[0] = UpperAscii(value[0]);
    if (value.size() + 4 > 16384)
        Fail(PreflightCode::UnsupportedPath, "Destination exceeds the 16384 UTF-16-unit extended-path bound.");
    for (std::size_t begin = 3; begin < value.size();) {
        const auto end = value.find(L'\\', begin);
        ValidateComponent(value.substr(begin, end == std::wstring::npos ? end : end - begin));
        if (end == std::wstring::npos) break;
        begin = end + 1;
        if (begin == value.size())
            Fail(PreflightCode::UnsupportedPath, "Repeated path separators are unsupported.");
    }
    return std::filesystem::path(L"\\\\?\\" + value);
}

struct DirectoryGuard::Impl {
    std::filesystem::path path;
    std::vector<HANDLE> handles;
    ~Impl() {
        for (auto it = handles.rbegin(); it != handles.rend(); ++it) CloseHandle(*it);
    }
};

DirectoryGuard::DirectoryGuard(const std::filesystem::path& path) : impl_(std::make_unique<Impl>()) {
    impl_->path = NormalizePreflightPath(path);
    const auto value = impl_->path.native();
    const auto root = value.substr(0, 7);
    const auto driveType = GetDriveTypeW(root.c_str());
    if (driveType == DRIVE_REMOTE)
        Fail(PreflightCode::UnsupportedPath, "Network and mapped drives are unsupported; select a local drive.");
    if (driveType == DRIVE_NO_ROOT_DIR || driveType == DRIVE_UNKNOWN)
        Fail(PreflightCode::MissingDestination, "Destination drive is unavailable; select an existing local drive.");
    for (std::size_t end = 7;;) {
        const auto prefix = value.substr(0, end);
        HANDLE handle = CreateFileW(prefix.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) OpenFailure(GetLastError());
        // Transfer ownership before any fallible validation or allocation.
        try { impl_->handles.push_back(handle); }
        catch (...) { CloseHandle(handle); throw; }
        ValidateHandle(handle, prefix);
        if (end == value.size()) break;
        end = value.find(L'\\', end + (end == 7 ? 0 : 1));
        if (end == std::wstring::npos) end = value.size();
    }
}

DirectoryGuard::~DirectoryGuard() = default;
const std::filesystem::path& DirectoryGuard::Path() const noexcept { return impl_->path; }
void* DirectoryGuard::NativeHandle() const noexcept { return impl_->handles.back(); }

} // namespace MonsterEngine::VideoTo3D::PreflightDetail
