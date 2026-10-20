// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace MonsterEngine::VideoTo3D {

/** Fixed tool roles; identity admission does not imply runtime capability. */
enum class ToolKind {
    FFprobe, ///< Media metadata probe.
    FFmpeg, ///< Media decoder and frame extractor.
    Colmap, ///< Camera reconstruction tool.
    Brush ///< Upstream Gaussian trainer.
};

/** Result of inspecting actual executable bytes against an explicit pin. */
enum class IdentityStatus {
    Missing, ///< Explicit path does not exist.
    Unpinned, ///< File inspected, but no expected digest was provided.
    Match, ///< Actual SHA-256 matches the valid expected digest.
    Mismatch, ///< Actual SHA-256 differs from the valid expected digest.
    Error ///< Invalid input, inaccessible file, or hashing failure.
};

/** File identity observation only; version, hardware and training remain unverified. */
struct ToolIdentity {
    ToolKind kind; ///< Requested tool role.
    std::filesystem::path executable; ///< Explicit absolute path supplied by the caller.
    std::string expectedSha256; ///< Caller pin; empty means no admission pin.
    std::string actualSha256; ///< Lowercase actual digest, empty on inspection failure.
    IdentityStatus status{IdentityStatus::Error}; ///< Only Match admits a version probe.
    std::string error; ///< Diagnostic for rejection; empty for a match.
};

/** Stream actual bytes through Windows SHA-256; never search PATH or execute a tool.
 * A pin must contain exactly 64 hexadecimal characters (case insensitive).
 * An empty pin produces Unpinned after successful inspection, never Match.
 * The file is protected against writes/deletion during inspection only; callers
 * must revalidate identity when deciding whether to reuse evidence or launch.
 * The returned observation does not hold a lifetime handle across process launch
 * and therefore cannot guarantee the launched image has this identity.
 */
ToolIdentity InspectToolIdentity(ToolKind kind, const std::filesystem::path& executable,
    const std::string& expectedSha256);

/** Stable report name for a tool role; unknown values are reported as UNKNOWN. */
std::string ToolKindName(ToolKind kind);

/** Stable report name for identity status; unknown values are reported as UNKNOWN. */
std::string IdentityStatusName(IdentityStatus status);

/** Escape a UTF-8 JSON string's contents, excluding the surrounding quotes. */
std::string EscapeJson(std::string_view value);

} // namespace MonsterEngine::VideoTo3D
