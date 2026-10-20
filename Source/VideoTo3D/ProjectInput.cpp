// Copyright Monster Engine. All Rights Reserved.
#include "VideoTo3D/ProjectInput.h"

#ifndef _WIN32
#error ProjectInput is a Windows-only service.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <limits>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#if defined(MONSTER_VIDEO_TO_3D_INPUT_TEST_HOOK)
extern "C" void MonsterVideoTo3DInputTestCheckpoint(unsigned checkpoint, const wchar_t* sourcePath);
#endif

namespace MonsterEngine::VideoTo3D {
namespace {
constexpr wchar_t kManifestName[] = L"input.mstudio";
constexpr wchar_t kMediaName[] = L"media";
constexpr char kMagic[] = "MSTINPUT";
constexpr std::size_t kBufferSize = 65536;

[[noreturn]] void Fail(InputErrorCode code, const char* message) { throw InputError(code, message); }
[[noreturn]] void WinFail(const char* message) {
    throw InputError(InputErrorCode::IoFailure, std::string(message) + " (Win32 " + std::to_string(GetLastError()) + ")");
}
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
class Hash final {
public:
    Hash() {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            Fail(InputErrorCode::IoFailure, "Cannot open SHA-256 provider");
        DWORD size = 0, returned = 0;
        if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &returned, 0) < 0 ||
            returned != sizeof(size) || size == 0) Fail(InputErrorCode::IoFailure, "Cannot initialize SHA-256 provider");
        object_.resize(size);
        if (BCryptCreateHash(algorithm_, &hash_, object_.data(), size, nullptr, 0, 0) < 0)
            Fail(InputErrorCode::IoFailure, "Cannot create SHA-256 hash");
    }
    ~Hash() { if (hash_) BCryptDestroyHash(hash_); if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0); }
    void Add(const void* data, DWORD size) {
        if (BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<void*>(data)), size, 0) < 0)
            Fail(InputErrorCode::IoFailure, "Cannot hash media bytes");
    }
    std::string Finish() {
        std::array<UCHAR, 32> digest{};
        if (BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
            Fail(InputErrorCode::IoFailure, "Cannot finish media hash");
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (auto byte : digest) { result.push_back(hex[byte >> 4]); result.push_back(hex[byte & 15]); }
        return result;
    }
private:
    BCRYPT_ALG_HANDLE algorithm_{nullptr};
    BCRYPT_HASH_HANDLE hash_{nullptr};
    std::vector<UCHAR> object_;
};

std::string Utf8(const std::wstring& wide) {
    if (wide.empty()) Fail(InputErrorCode::UnsafePath, "Empty media filename");
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (!size) Fail(InputErrorCode::UnsafePath, "Invalid Unicode media filename");
    std::string text(size, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), text.data(), size, nullptr, nullptr) != size)
        WinFail("Cannot encode media filename");
    return text;
}
std::wstring Wide(const std::string& text) {
    if (text.empty() || text.size() > kMaxInputRelativePathBytes)
        Fail(InputErrorCode::CorruptManifest, "Invalid owned path length");
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size) Fail(InputErrorCode::CorruptManifest, "Invalid UTF-8 owned path");
    std::wstring wide(size, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size) != size)
        Fail(InputErrorCode::CorruptManifest, "Invalid UTF-8 owned path");
    return wide;
}
bool IsUuid(std::string_view text) {
    if (text.size() != 36) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (text[i] != '-') return false; }
        else if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) return false;
    }
    return true;
}
bool IsHexHash(std::string_view text) {
    if (text.size() != 64) return false;
    for (char c : text) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
void CheckName(const std::wstring& name, InputErrorCode code) {
    if (name.empty() || name == L"." || name == L".." || name.back() == L' ' || name.back() == L'.')
        Fail(code, "Ambiguous media filename");
    for (wchar_t c : name) if (c < 32 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos)
        Fail(code, "Unsafe media filename");
    std::wstring upper = name;
    for (auto& c : upper) c = static_cast<wchar_t>(towupper(c));
    const auto dot = upper.find(L'.');
    const auto stem = upper.substr(0, dot);
    if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
        (stem.size() == 4 && ((stem.substr(0, 3) == L"COM") || (stem.substr(0, 3) == L"LPT")) &&
         stem[3] >= L'1' && stem[3] <= L'9')) Fail(code, "Reserved Windows media filename");
}
bool IsPhoto(const std::filesystem::path& path) {
    auto ext = path.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    return ext == L".jpg" || ext == L".jpeg" || ext == L".png" || ext == L".tif" || ext == L".tiff";
}
bool IsVideo(const std::filesystem::path& path) {
    auto ext = path.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    return ext == L".mp4" || ext == L".mov" || ext == L".mkv" || ext == L".avi";
}
void CheckNoReparse(const std::filesystem::path& path, bool missingLeaf = false) {
    const auto absolute = std::filesystem::absolute(path);
    auto current = absolute.root_path();
    const auto relative = absolute.relative_path();
    for (auto part = relative.begin(); part != relative.end(); ++part) {
        if (*part == L"." || *part == L"..") Fail(InputErrorCode::UnsafePath, "Path contains an ambiguous component");
        current /= *part;
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (missingLeaf && std::next(part) == relative.end() &&
                (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) return;
            SetLastError(error); WinFail("Cannot inspect path component");
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) Fail(InputErrorCode::UnsafePath, "Path traverses a reparse point");
    }
}
struct Identity { DWORD volume, high, low; };
Identity InspectHandle(HANDLE handle, bool directory, InputErrorCode code) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) WinFail("Cannot inspect open media handle");
    if (GetFileType(handle) != FILE_TYPE_DISK || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory)
        Fail(code, "Media is not a regular unredirected file or directory");
    return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
}
bool Same(Identity a, Identity b) { return a.volume == b.volume && a.high == b.high && a.low == b.low; }
Handle OpenFile(const std::filesystem::path& path, InputErrorCode code, bool write = false) {
    CheckNoReparse(path, true);
    Handle handle(CreateFileW(path.c_str(), write ? GENERIC_WRITE : GENERIC_READ, write ? 0 : FILE_SHARE_READ,
                              nullptr, write ? CREATE_NEW : OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!handle.Valid()) {
        if (!write && (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND))
            Fail(code, "Media file is missing");
        WinFail(write ? "Cannot create owned media file" : "Cannot open media file");
    }
    InspectHandle(handle.Get(), false, code);
    return handle;
}
std::pair<std::uint64_t, std::string> HashFile(HANDLE file, HANDLE copy = INVALID_HANDLE_VALUE) {
    Hash hash;
    std::array<char, kBufferSize> buffer{};
    std::uint64_t total = 0;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) WinFail("Cannot read media file");
        if (read == 0) break;
        if (total > kMaxInputFileBytes - read) Fail(InputErrorCode::LimitExceeded, "Media file exceeds 16 GiB");
        total += read;
        hash.Add(buffer.data(), read);
        if (copy != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            if (!WriteFile(copy, buffer.data(), read, &written, nullptr) || written != read)
                WinFail("Cannot write complete owned media file");
        }
    }
    return {total, hash.Finish()};
}
std::pair<std::uint64_t, std::string> HashPath(const std::filesystem::path& path, InputErrorCode code) {
    auto file = OpenFile(path, code);
    return HashFile(file.Get());
}
void CheckAlias(const std::filesystem::path& source, const std::filesystem::path& project) {
    const auto sourceAbsolute = std::filesystem::absolute(source).lexically_normal();
    const auto projectAbsolute = std::filesystem::absolute(project).lexically_normal();
    auto sourceText = sourceAbsolute.wstring(), projectText = projectAbsolute.wstring();
    for (auto& c : sourceText) c = static_cast<wchar_t>(towlower(c));
    for (auto& c : projectText) c = static_cast<wchar_t>(towlower(c));
    if (sourceText == projectText ||
        (sourceText.size() > projectText.size() && sourceText.compare(0, projectText.size(), projectText) == 0 &&
         (sourceText[projectText.size()] == L'\\' || sourceText[projectText.size()] == L'/')) ||
        (projectText.size() > sourceText.size() && projectText.compare(0, sourceText.size(), sourceText) == 0 &&
         (projectText[sourceText.size()] == L'\\' || projectText[sourceText.size()] == L'/')))
        Fail(InputErrorCode::UnsafePath, "Source and project directories overlap");
}
void CheckHardlinkAlias(Identity source, const std::filesystem::path& project) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(project)) {
        if (entry.path().filename() == L".monsterstudio.lock") continue;
        CheckNoReparse(entry.path());
        const auto attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) WinFail("Cannot inspect project-owned entry");
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) Fail(InputErrorCode::UnsafePath, "Project tree is redirected");
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        auto file = OpenFile(entry.path(), InputErrorCode::UnsafePath);
        if (Same(source, InspectHandle(file.Get(), false, InputErrorCode::UnsafePath)))
            Fail(InputErrorCode::UnsafePath, "Source is a hardlink to a project-owned file");
    }
}
struct Writer {
    std::string bytes;
    void U32(std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<char>(value >> (8 * i))); }
    void U64(std::uint64_t value) { for (unsigned i = 0; i < 8; ++i) bytes.push_back(static_cast<char>(value >> (8 * i))); }
    void Text(const std::string& value) { U32(static_cast<std::uint32_t>(value.size())); bytes += value; }
};
struct Reader {
    const std::string& bytes;
    std::size_t offset{0};
    std::string Raw(std::size_t n) {
        if (n > bytes.size() - offset) Fail(InputErrorCode::CorruptManifest, "Input manifest is truncated");
        auto value = bytes.substr(offset, n); offset += n; return value;
    }
    std::uint32_t U32() { auto value = Raw(4); std::uint32_t n = 0; for (unsigned i = 0; i < 4; ++i) n |= std::uint32_t(static_cast<unsigned char>(value[i])) << (8 * i); return n; }
    std::uint64_t U64() { auto value = Raw(8); std::uint64_t n = 0; for (unsigned i = 0; i < 8; ++i) n |= std::uint64_t(static_cast<unsigned char>(value[i])) << (8 * i); return n; }
    std::string Text(std::size_t limit) { auto n = U32(); if (n > limit) Fail(InputErrorCode::CorruptManifest, "Input manifest text exceeds bound"); return Raw(n); }
};
void Validate(const InputManifest& manifest, const std::string& projectId) {
    if (manifest.schemaVersion != kInputSchemaVersion || manifest.projectId != projectId ||
        !IsUuid(manifest.projectId) || !IsUuid(manifest.inputId) || manifest.revision == 0 ||
        (manifest.kind != InputKind::Video && manifest.kind != InputKind::Photos))
        Fail(InputErrorCode::CorruptManifest, "Input manifest schema or identity is invalid");
    if (manifest.files.empty() || manifest.files.size() > kMaxInputFiles ||
        (manifest.kind == InputKind::Video && manifest.files.size() != 1))
        Fail(InputErrorCode::CorruptManifest, "Input manifest file count is invalid");
    std::uint64_t total = 0;
    std::set<std::wstring> names;
    for (const auto& file : manifest.files) {
        if (file.byteCount > kMaxInputFileBytes || total > kMaxInputTotalBytes - file.byteCount || !IsHexHash(file.sha256))
            Fail(InputErrorCode::CorruptManifest, "Input manifest media identity is invalid");
        total += file.byteCount;
        const auto wide = Wide(file.relativePath);
        const auto path = std::filesystem::path(wide);
        if (path.is_absolute() || path.has_root_path()) Fail(InputErrorCode::CorruptManifest, "Owned media path is absolute");
        auto it = path.begin();
        if (it == path.end() || *it++ != kMediaName || it == path.end() || *it++ != std::filesystem::path(manifest.inputId) ||
            it == path.end()) Fail(InputErrorCode::CorruptManifest, "Owned media path is outside generation");
        const auto name = *it++;
        if (it != path.end()) Fail(InputErrorCode::CorruptManifest, "Nested owned media path is invalid");
        CheckName(name.wstring(), InputErrorCode::CorruptManifest);
        if ((manifest.kind == InputKind::Video && !IsVideo(name)) || (manifest.kind == InputKind::Photos && !IsPhoto(name)))
            Fail(InputErrorCode::CorruptManifest, "Owned media extension does not match source kind");
        auto folded = name.wstring(); for (auto& c : folded) c = static_cast<wchar_t>(towlower(c));
        if (!names.insert(folded).second) Fail(InputErrorCode::CorruptManifest, "Duplicate owned media filename");
    }
}
std::string Encode(const InputManifest& manifest) {
    Validate(manifest, manifest.projectId);
    Writer writer;
    writer.bytes.append(kMagic, 8);
    writer.U32(manifest.schemaVersion); writer.Text(manifest.projectId); writer.Text(manifest.inputId);
    writer.U64(manifest.revision); writer.U32(static_cast<std::uint32_t>(manifest.kind));
    writer.U32(static_cast<std::uint32_t>(manifest.files.size()));
    for (const auto& file : manifest.files) { writer.Text(file.relativePath); writer.U64(file.byteCount); writer.Text(file.sha256); }
    if (writer.bytes.size() > kMaxInputManifestBytes) Fail(InputErrorCode::LimitExceeded, "Input manifest exceeds 1 MiB");
    return writer.bytes;
}
InputManifest Decode(const std::string& bytes, const std::string& projectId) {
    if (bytes.size() > kMaxInputManifestBytes) Fail(InputErrorCode::CorruptManifest, "Input manifest exceeds 1 MiB");
    Reader reader{bytes};
    if (reader.Raw(8) != std::string(kMagic, 8)) Fail(InputErrorCode::CorruptManifest, "Invalid input manifest signature");
    InputManifest manifest;
    manifest.schemaVersion = reader.U32(); manifest.projectId = reader.Text(36); manifest.inputId = reader.Text(36);
    manifest.revision = reader.U64();
    const auto kind = reader.U32();
    if (kind > 1) Fail(InputErrorCode::CorruptManifest, "Unknown input kind");
    manifest.kind = static_cast<InputKind>(kind);
    const auto count = reader.U32();
    if (count == 0 || count > kMaxInputFiles) Fail(InputErrorCode::CorruptManifest, "Invalid input file count");
    manifest.files.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        manifest.files.push_back({reader.Text(kMaxInputRelativePathBytes), reader.U64(), reader.Text(64)});
    if (reader.offset != bytes.size()) Fail(InputErrorCode::CorruptManifest, "Input manifest has trailing bytes");
    Validate(manifest, projectId);
    return manifest;
}
void WriteAll(HANDLE handle, const std::string& bytes) {
    DWORD written = 0;
    if (!WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size())
        WinFail("Cannot write complete input manifest");
    if (!FlushFileBuffers(handle)) WinFail("Cannot flush input manifest");
}
std::string ReadAll(HANDLE handle) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size)) WinFail("Cannot measure input manifest");
    if (size.QuadPart < 0 || size.QuadPart > static_cast<LONGLONG>(kMaxInputManifestBytes))
        Fail(InputErrorCode::CorruptManifest, "Input manifest exceeds 1 MiB");
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty() && (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()))
        WinFail("Cannot read complete input manifest");
    return bytes;
}
} // namespace

InputManifest ProjectInput::DecodeManifestBytes(const std::string& bytes, const std::string& projectId) {
    return Decode(bytes, projectId);
}

InputManifest ProjectInput::Inspect(ProjectRepository& repository) {
    try {
    const auto& root = repository.Directory();
    const auto& projectId = repository.Snapshot().projectId;
    ProjectRepository::ValidateSnapshot(repository.Snapshot());
    const auto path = root / kManifestName;
    CheckNoReparse(root);
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES && (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND))
        Fail(InputErrorCode::NoInput, "Project has no published input");
    if (attributes == INVALID_FILE_ATTRIBUTES) WinFail("Cannot inspect input manifest");
    auto file = OpenFile(path, InputErrorCode::CorruptManifest);
    auto manifest = Decode(ReadAll(file.Get()), projectId);
    for (const auto& item : manifest.files) {
        const auto owned = root / Wide(item.relativePath);
        try {
            const auto [size, digest] = HashPath(owned, InputErrorCode::CorruptManifest);
            if (size != item.byteCount || digest != item.sha256)
                Fail(InputErrorCode::CorruptManifest, "Owned media bytes differ from input manifest");
        } catch (const InputError& error) {
            if (error.Code() == InputErrorCode::LimitExceeded) Fail(InputErrorCode::CorruptManifest, "Owned media exceeds manifest bound");
            throw;
        }
    }
    return manifest;
    } catch (const ProjectError& error) {
        throw InputError(InputErrorCode::OwnershipConflict, std::string("Invalid open project: ") + error.what());
    } catch (const std::filesystem::filesystem_error& error) {
        throw InputError(InputErrorCode::IoFailure, std::string("Cannot inspect project input: ") + error.what());
    }
}

InputManifest ProjectInput::Import(ProjectRepository& repository, InputKind kind, const std::filesystem::path& source) {
    try {
    if (kind != InputKind::Video && kind != InputKind::Photos) Fail(InputErrorCode::InvalidSource, "Unknown input kind");
    if (source.empty()) Fail(InputErrorCode::InvalidSource, "Input source is empty");
    ProjectRepository::ValidateSnapshot(repository.Snapshot());
    const auto& root = repository.Directory();
    CheckNoReparse(root);
    const DWORD sourceAttributes = GetFileAttributesW(source.c_str());
    if (sourceAttributes == INVALID_FILE_ATTRIBUTES &&
        (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND))
        Fail(InputErrorCode::InvalidSource, "Input source does not exist");
    if (sourceAttributes == INVALID_FILE_ATTRIBUTES) WinFail("Cannot inspect input source");
    CheckNoReparse(source);
    CheckAlias(source, root);
    InputManifest manifest;
    manifest.projectId = repository.Snapshot().projectId;
    manifest.inputId = ProjectRepository::NewId();
    manifest.kind = kind;
    try { const auto previous = Inspect(repository); manifest.revision = previous.revision + 1;
          if (manifest.revision == 0) Fail(InputErrorCode::LimitExceeded, "Input revision overflow"); }
    catch (const InputError& error) { if (error.Code() != InputErrorCode::NoInput) throw; manifest.revision = 1; }
    std::vector<std::filesystem::path> sources;
    if (kind == InputKind::Video) {
        CheckName(source.filename().wstring(), InputErrorCode::UnsafePath);
        if (!IsVideo(source)) Fail(InputErrorCode::InvalidSource, "Unsupported video extension");
        sources.push_back(source);
    } else {
        Handle directory(CreateFileW(source.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!directory.Valid()) WinFail("Cannot open photo directory");
        InspectHandle(directory.Get(), true, InputErrorCode::InvalidSource);
        for (const auto& entry : std::filesystem::directory_iterator(source)) {
            CheckNoReparse(entry.path());
            CheckName(entry.path().filename().wstring(), InputErrorCode::UnsafePath);
            if (!IsPhoto(entry.path())) Fail(InputErrorCode::InvalidSource, "Photo directory contains unsupported entry");
            sources.push_back(entry.path());
            if (sources.size() > kMaxInputFiles) Fail(InputErrorCode::LimitExceeded, "More than 4096 photos");
        }
        if (sources.empty()) Fail(InputErrorCode::InvalidSource, "Photo directory is empty");
        std::sort(sources.begin(), sources.end(), [](const auto& a, const auto& b) { return a.filename().wstring() < b.filename().wstring(); });
        std::set<std::wstring> folded;
        for (const auto& path : sources) {
            auto name = path.filename().wstring(); for (auto& c : name) c = static_cast<wchar_t>(towlower(c));
            if (!folded.insert(name).second) Fail(InputErrorCode::UnsafePath, "Case-colliding photo filenames");
        }
    }
    const auto media = root / kMediaName;
    const DWORD mediaAttributes = GetFileAttributesW(media.c_str());
    if (mediaAttributes == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) WinFail("Cannot inspect media directory");
        if (!CreateDirectoryW(media.c_str(), nullptr)) WinFail("Cannot create media directory");
    }
    CheckNoReparse(media);
    if (!(GetFileAttributesW(media.c_str()) & FILE_ATTRIBUTE_DIRECTORY)) Fail(InputErrorCode::UnsafePath, "Media root is not a directory");
    const auto generation = media / std::filesystem::path(manifest.inputId);
    if (!CreateDirectoryW(generation.c_str(), nullptr)) WinFail("Cannot create immutable media generation");
    std::uint64_t aggregate = 0;
    for (const auto& path : sources) {
        const auto relative = std::filesystem::path(kMediaName) / std::filesystem::path(manifest.inputId) / path.filename();
        const auto relativeUtf8 = Utf8(relative.wstring());
        if (relativeUtf8.size() > kMaxInputRelativePathBytes) Fail(InputErrorCode::LimitExceeded, "Owned media path exceeds 4096 UTF-8 bytes");
        auto sourceFile = OpenFile(path, InputErrorCode::InvalidSource);
        const auto identity = InspectHandle(sourceFile.Get(), false, InputErrorCode::InvalidSource);
        CheckHardlinkAlias(identity, root);
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(sourceFile.Get(), &length)) WinFail("Cannot measure source media");
        if (length.QuadPart < 0 || static_cast<std::uint64_t>(length.QuadPart) > kMaxInputFileBytes ||
            aggregate > kMaxInputTotalBytes - static_cast<std::uint64_t>(length.QuadPart))
            Fail(InputErrorCode::LimitExceeded, "Input media exceeds byte bounds");
        auto owned = OpenFile(root / relative, InputErrorCode::UnsafePath, true);
        const auto copied = HashFile(sourceFile.Get(), owned.Get());
        if (copied.first != static_cast<std::uint64_t>(length.QuadPart)) Fail(InputErrorCode::SourceChanged, "Source media size changed during copy");
        if (!FlushFileBuffers(owned.Get())) WinFail("Cannot flush owned media");
        owned = Handle();
        sourceFile = Handle();
#if defined(MONSTER_VIDEO_TO_3D_INPUT_TEST_HOOK)
        MonsterVideoTo3DInputTestCheckpoint(1, path.c_str());
#endif
        auto sourceAgain = OpenFile(path, InputErrorCode::SourceChanged);
        if (!Same(identity, InspectHandle(sourceAgain.Get(), false, InputErrorCode::SourceChanged)))
            Fail(InputErrorCode::SourceChanged, "Source media identity changed during copy");
        const auto verified = HashFile(sourceAgain.Get());
        const auto ownedVerified = HashPath(root / relative, InputErrorCode::IoFailure);
        if (verified != copied || ownedVerified != copied)
            Fail(InputErrorCode::SourceChanged, "Source or owned media changed during copy");
        aggregate += copied.first;
        manifest.files.push_back({relativeUtf8, copied.first, copied.second});
    }
    const auto bytes = Encode(manifest);
    const auto stage = root / std::filesystem::path(L".monsterstudio-input-" + std::wstring(manifest.inputId.begin(), manifest.inputId.end()) + L".tmp");
    auto staging = OpenFile(stage, InputErrorCode::UnsafePath, true);
    WriteAll(staging.Get(), bytes);
    staging = Handle();
    const auto target = root / kManifestName;
    CheckNoReparse(target, true);
#if defined(MONSTER_VIDEO_TO_3D_INPUT_TEST_HOOK)
    MonsterVideoTo3DInputTestCheckpoint(2, nullptr);
#endif
    if (!MoveFileExW(stage.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        WinFail("Cannot atomically publish input manifest");
    return manifest;
    } catch (const ProjectError& error) {
        throw InputError(InputErrorCode::OwnershipConflict, std::string("Invalid open project: ") + error.what());
    } catch (const std::filesystem::filesystem_error& error) {
        throw InputError(InputErrorCode::IoFailure, std::string("Cannot import project input: ") + error.what());
    }
}
} // namespace MonsterEngine::VideoTo3D
