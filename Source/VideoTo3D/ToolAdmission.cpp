// Copyright Monster Engine. All Rights Reserved.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "VideoTo3D/ToolAdmission.h"

#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <vector>

namespace MonsterEngine::VideoTo3D {
namespace {

struct FileHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

struct HashAlgorithm {
    BCRYPT_ALG_HANDLE value{nullptr};
    ~HashAlgorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
};

struct HashHandle {
    BCRYPT_HASH_HANDLE value{nullptr};
    ~HashHandle() { if (value) BCryptDestroyHash(value); }
};

bool IsHex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
        (value >= 'A' && value <= 'F');
}

char LowerHex(char value) {
    return value >= 'A' && value <= 'F' ? static_cast<char>(value - 'A' + 'a') : value;
}

std::string Win32Failure(const char* operation, DWORD code) {
    return std::string(operation) + " failed (Win32 " + std::to_string(code) + ")";
}

std::string HashFailure(const char* operation, NTSTATUS code) {
    return std::string(operation) + " failed (NTSTATUS " +
        std::to_string(static_cast<unsigned long>(code)) + ")";
}

} // namespace

ToolIdentity InspectToolIdentity(ToolKind kind, const std::filesystem::path& executable,
    const std::string& expectedSha256) {
    ToolIdentity result{kind, executable, expectedSha256, {}, IdentityStatus::Error, {}};
    if (ToolKindName(kind) == "UNKNOWN") {
        result.error = "Unknown tool role";
        return result;
    }
    if (executable.empty() || !executable.is_absolute() ||
        executable.native().find(L'\0') != std::wstring::npos) {
        result.error = "An explicit absolute executable path without NUL is required";
        return result;
    }
    if (!expectedSha256.empty()) {
        if (expectedSha256.size() != 64) {
            result.error = "Expected SHA-256 must contain exactly 64 hexadecimal characters";
            return result;
        }
        for (char value : expectedSha256) {
            if (!IsHex(value)) {
                result.error = "Expected SHA-256 must contain exactly 64 hexadecimal characters";
                return result;
            }
        }
    }

    // Deny concurrent writes and replacement while observing the executable.
    FileHandle file;
    file.value = CreateFileW(executable.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
            result.status = IdentityStatus::Missing;
        result.error = Win32Failure("Open executable", code);
        return result;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.value, &info)) {
        result.error = Win32Failure("Inspect executable", GetLastError());
        return result;
    }
    if (GetFileType(file.value) != FILE_TYPE_DISK || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        result.error = "Executable must be a regular disk file";
        return result;
    }

    HashAlgorithm algorithm;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        result.error = HashFailure("Open SHA-256 provider", status);
        return result;
    }
    DWORD objectBytes = 0;
    DWORD returnedBytes = 0;
    status = BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &returnedBytes, 0);
    if (status < 0 || returnedBytes != sizeof(objectBytes) || objectBytes == 0) {
        result.error = status < 0 ? HashFailure("Query SHA-256 object size", status) :
            "SHA-256 provider returned an invalid object size";
        return result;
    }
    std::vector<UCHAR> object(objectBytes);
    HashHandle hash;
    status = BCryptCreateHash(algorithm.value, &hash.value, object.data(), objectBytes, nullptr, 0, 0);
    if (status < 0) {
        result.error = HashFailure("Create SHA-256 hash", status);
        return result;
    }
    std::array<UCHAR, 65536> buffer{};
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
            result.error = Win32Failure("Read executable", GetLastError());
            return result;
        }
        if (bytesRead == 0) break;
        status = BCryptHashData(hash.value, buffer.data(), bytesRead, 0);
        if (status < 0) {
            result.error = HashFailure("Hash executable bytes", status);
            return result;
        }
    }
    std::array<UCHAR, 32> digest{};
    status = BCryptFinishHash(hash.value, digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (status < 0) {
        result.error = HashFailure("Finish SHA-256 hash", status);
        return result;
    }
    constexpr char hex[] = "0123456789abcdef";
    result.actualSha256.reserve(digest.size() * 2);
    for (UCHAR value : digest) {
        result.actualSha256.push_back(hex[value >> 4]);
        result.actualSha256.push_back(hex[value & 15]);
    }
    if (expectedSha256.empty()) {
        result.status = IdentityStatus::Unpinned;
        result.error = "No expected SHA-256 pin was provided";
        return result;
    }
    for (std::size_t i = 0; i < expectedSha256.size(); ++i) {
        if (LowerHex(expectedSha256[i]) != result.actualSha256[i]) {
            result.status = IdentityStatus::Mismatch;
            result.error = "Executable SHA-256 does not match its expected pin";
            return result;
        }
    }
    result.status = IdentityStatus::Match;
    return result;
}

std::string ToolKindName(ToolKind kind) {
    switch (kind) {
    case ToolKind::FFprobe: return "FFprobe";
    case ToolKind::FFmpeg: return "FFmpeg";
    case ToolKind::Colmap: return "COLMAP";
    case ToolKind::Brush: return "Brush";
    }
    return "UNKNOWN";
}

std::string IdentityStatusName(IdentityStatus status) {
    switch (status) {
    case IdentityStatus::Missing: return "Missing";
    case IdentityStatus::Unpinned: return "Unpinned";
    case IdentityStatus::Match: return "Match";
    case IdentityStatus::Mismatch: return "Mismatch";
    case IdentityStatus::Error: return "Error";
    }
    return "UNKNOWN";
}

std::string EscapeJson(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(value.size());
    for (unsigned char character : value) {
        switch (character) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (character < 0x20) {
                escaped += "\\u00";
                escaped.push_back(hex[character >> 4]);
                escaped.push_back(hex[character & 15]);
            } else escaped.push_back(static_cast<char>(character));
        }
    }
    return escaped;
}

} // namespace MonsterEngine::VideoTo3D
