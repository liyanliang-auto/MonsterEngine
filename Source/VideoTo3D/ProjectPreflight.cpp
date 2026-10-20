// Copyright Monster Engine. All Rights Reserved.
#include "VideoTo3D/ProjectPreflight.h"
#include "PreflightWindowsPath.h"

#ifndef _WIN32
#error ProjectPreflight is a Windows-only service.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <limits>

namespace MonsterEngine::VideoTo3D {
namespace {

bool CalculateBudget(const SpaceBudget& budget, SpacePreflightResult& result) {
    const std::array<const std::optional<std::uint64_t>*, 9> fields{
        &budget.mediaCopies, &budget.preparedImages, &budget.reconstructionDatabase,
        &budget.checkpoints, &budget.temporaryExports, &budget.lod, &budget.newAssets,
        &budget.retainedOverlap, &budget.reserve};
    for (const auto* field : fields) {
        if (!field->has_value()) {
            result.diagnostic = {PreflightCode::InvalidBudget,
                "Supply every disjoint incremental byte category and reserve explicitly, including zero."};
            return false;
        }
    }
    result.reserveBytes = *budget.reserve;
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto value = **fields[i];
        if (value > std::numeric_limits<std::uint64_t>::max() - total) {
            result.diagnostic = {PreflightCode::ArithmeticOverflow,
                "Additional byte requirements exceed uint64 capacity; reduce the plan."};
            return false;
        }
        total += value;
        if (i == 7) result.incrementalBytes = total;
    }
    result.requiredBytes = total;
    return true;
}

void CompareCapacity(SpacePreflightResult& result, const SpaceSample& sample) {
    result.sample = sample;
    if (result.requiredBytes > sample.availableToCaller) {
        result.diagnostic = {PreflightCode::InsufficientSpace,
            "Additional planned bytes plus reserve exceed caller-available capacity; select another destination or reduce the plan."};
    } else {
        result.diagnostic = {PreflightCode::Ok,
            "The capacity sample covers additional planned bytes plus reserve; this observation does not reserve space."};
    }
}

bool AccessFailure(DWORD error) noexcept {
    return error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD || error == ERROR_WRITE_PROTECT;
}

// After CREATE_NEW, both handles refer to the same owned object. The second handle
// keeps its exclusive sharing contract alive while the data handle is closed.
// Deletion is requested by handle, never by reopening or deleting a pathname.
void RunOwnedProbe(HANDLE writer, WritePreflightResult& result) {
    HANDLE keeper = INVALID_HANDLE_VALUE;
    PreflightCode code = PreflightCode::Ok;
    const char* message = "Exclusive bounded create, write, flush, close and owned removal succeeded.";
    DWORD error = 0;
    const auto fail = [&](PreflightCode next, const char* text, DWORD native) {
        if (code == PreflightCode::Ok) { code = next; message = text; error = native; }
    };
    if (!DuplicateHandle(GetCurrentProcess(), writer, GetCurrentProcess(), &keeper,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) {
        fail(PreflightCode::IoFailure, "Cannot retain owned probe identity for safe cleanup; retry the probe.", GetLastError());
    } else {
        std::array<unsigned char, 4096> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(i % 251);
        while (result.bytesWritten < bytes.size()) {
            DWORD written = 0;
            const auto remaining = static_cast<DWORD>(bytes.size() - result.bytesWritten);
            if (!WriteFile(writer, bytes.data() + static_cast<std::size_t>(result.bytesWritten), remaining, &written, nullptr)) {
                fail(PreflightCode::ProbeWriteFailed, "Cannot write the bounded probe payload; check storage and access permissions.", GetLastError());
                break;
            }
            if (written == 0 || written > remaining) {
                fail(PreflightCode::ProbeWriteFailed, "Probe write made no valid progress; check storage health and retry.", ERROR_WRITE_FAULT);
                break;
            }
            result.bytesWritten += written;
        }
        if (code == PreflightCode::Ok) {
            if (FlushFileBuffers(writer)) result.flushed = true;
            else fail(PreflightCode::ProbeFlushFailed, "Cannot flush the probe payload; check storage health and retry.", GetLastError());
        }
        if (CloseHandle(writer)) result.closed = true;
        else fail(PreflightCode::ProbeCloseFailed, "Cannot close the probe data handle; the probe lifecycle is incomplete.", GetLastError());
    }

    // If duplication failed, the original handle still owns the exact entry.
    const HANDLE cleanup = keeper == INVALID_HANDLE_VALUE ? writer : keeper;
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    DWORD cleanupError = 0;
    if (!SetFileInformationByHandle(cleanup, FileDispositionInfo, &disposition, sizeof(disposition)))
        cleanupError = GetLastError();
    const bool cleanupClosed = CloseHandle(cleanup) != FALSE;
    if (!cleanupClosed && cleanupError == 0) cleanupError = GetLastError();
    if (keeper == INVALID_HANDLE_VALUE) result.closed = cleanupClosed;
    if (cleanupError == 0 && result.closed) {
        result.removed = true;
        result.residualPath.clear();
    } else {
        // Cleanup takes precedence, retaining any earlier failure in the message.
        const auto original = code;
        code = PreflightCode::ProbeCleanupFailed;
        if (cleanupError != 0) error = cleanupError;
        result.diagnostic = {code, original == PreflightCode::Ok
            ? "Cannot confirm removal of the owned probe; inspect residualPath and remove only this reported entry when safe."
            : std::string("Cannot confirm removal of the owned probe; inspect residualPath. Earlier outcome: ") +
                PreflightCodeName(original) + ". " + message, error};
        return;
    }
    result.diagnostic = {code, message, error};
}

} // namespace

SpacePreflightResult EvaluateSpaceBudget(const SpaceBudget& budget, const SpaceSample& sample) {
    SpacePreflightResult result;
    if (CalculateBudget(budget, result)) CompareCapacity(result, sample);
    return result;
}

SpacePreflightResult QuerySpacePreflight(const std::filesystem::path& directory, const SpaceBudget& budget) {
    SpacePreflightResult result;
    if (!CalculateBudget(budget, result)) return result;
    try {
        const PreflightDetail::DirectoryGuard guard(directory);
        ULARGE_INTEGER caller{}, total{}, free{};
        if (!GetDiskFreeSpaceExW(guard.Path().c_str(), &caller, &total, &free)) {
            const DWORD error = GetLastError();
            result.diagnostic = {AccessFailure(error) ? PreflightCode::AccessDenied : PreflightCode::CapacityQueryFailed,
                "Cannot query destination capacity; check volume availability and caller permissions.", error};
        } else {
            CompareCapacity(result, {caller.QuadPart, free.QuadPart, total.QuadPart});
        }
    } catch (const PreflightFailure& failure) {
        result.diagnostic = failure.Diagnostic();
    } catch (const std::exception&) {
        result.diagnostic = {PreflightCode::IoFailure, "Cannot complete the guarded capacity observation; retry with an accessible local directory."};
    }
    return result;
}

WritePreflightResult ProbeDirectoryWrite(const std::filesystem::path& directory) {
    WritePreflightResult result;
    try {
        const PreflightDetail::DirectoryGuard guard(directory);
        constexpr wchar_t hex[] = L"0123456789abcdef";
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            std::array<UCHAR, 16> random{};
            if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
                result.diagnostic = {PreflightCode::IoFailure, "Cannot generate an exclusive probe name; retry when the system random provider is available."};
                return result;
            }
            std::wstring name = L".monster-preflight-";
            for (const auto byte : random) { name.push_back(hex[byte >> 4]); name.push_back(hex[byte & 15]); }
            name += L".tmp";
            const auto path = PreflightDetail::NormalizePreflightPath(guard.Path() / name);
            // Allocate the report path before ownership begins so cleanup never
            // depends on a later path allocation succeeding.
            result.residualPath = path;
            HANDLE writer = CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (writer == INVALID_HANDLE_VALUE) {
                const DWORD error = GetLastError();
                result.residualPath.clear();
                if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
                result.diagnostic = {AccessFailure(error) ? PreflightCode::AccessDenied : PreflightCode::ProbeCreateFailed,
                    "Cannot exclusively create a removable probe; select a destination permitting create, write and delete.", error};
                return result;
            }
            result.created = true;
            RunOwnedProbe(writer, result);
            return result;
        }
        result.diagnostic = {PreflightCode::ProbeCreateFailed, "Probe names repeatedly collided; no preexisting entry was modified. Retry when the destination is idle.", ERROR_FILE_EXISTS};
    } catch (const PreflightFailure& failure) {
        result.diagnostic = failure.Diagnostic();
    } catch (const std::exception&) {
        result.diagnostic = {PreflightCode::IoFailure, "Cannot complete the bounded write preflight; inspect any reported owned residual path."};
    }
    return result;
}

} // namespace MonsterEngine::VideoTo3D
