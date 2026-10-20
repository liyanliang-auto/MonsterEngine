// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include "VideoTo3D/ProjectRepository.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace MonsterEngine::VideoTo3D {

/** Supported input-manifest schema version, independent of project.mstudio v1. */
inline constexpr std::uint32_t kInputSchemaVersion = 1;
/** Maximum regular media files in one imported input. */
inline constexpr std::size_t kMaxInputFiles = 4096;
/** Maximum bytes in one imported regular media file. */
inline constexpr std::uint64_t kMaxInputFileBytes = 16ULL * 1024 * 1024 * 1024;
/** Maximum aggregate bytes in one imported input. */
inline constexpr std::uint64_t kMaxInputTotalBytes = 64ULL * 1024 * 1024 * 1024;
/** Maximum UTF-8 bytes in one validated project-relative media path. */
inline constexpr std::size_t kMaxInputRelativePathBytes = 4096;
/** Maximum encoded bytes in the complete independent input manifest. */
inline constexpr std::size_t kMaxInputManifestBytes = 1024 * 1024;

/** Exactly one media source kind is selected for each import operation. */
enum class InputKind {
    Video, ///< One regular video file.
    Photos ///< One flat directory of supported regular photo files.
};

/** Identity of one verified file under the project-owned media generation. */
struct InputFile {
    std::string relativePath; ///< Validated UTF-8 project-relative path; never an external source path.
    std::uint64_t byteCount{0}; ///< Verified file length in bytes.
    std::string sha256; ///< Lowercase 64-hex SHA-256 of the complete file.
};

/** Independently versioned current input bound to one immutable project ID. */
struct InputManifest {
    std::uint32_t schemaVersion{kInputSchemaVersion}; ///< Only the supported input schema is accepted.
    std::string projectId; ///< Immutable canonical lowercase project UUID.
    std::string inputId; ///< Canonical lowercase UUID identifying this import generation.
    std::uint64_t revision{0}; ///< Positive, monotone input-manifest revision.
    InputKind kind{InputKind::Video}; ///< Video or Photos, never both.
    std::vector<InputFile> files; ///< Nonempty set of owned and verified media identities.
};

/** Stable category for an import or inspection failure. */
enum class InputErrorCode {
    NoInput, ///< No current input manifest has been published.
    InvalidSource, ///< Missing, unsupported, mixed or non-regular source.
    UnsafePath, ///< Link, reparse, alias, escape or ambiguous path.
    SourceChanged, ///< Source identity or content changed during copying.
    CorruptManifest, ///< Current manifest or its referenced owned bytes failed validation.
    LimitExceeded, ///< A documented count, byte or path bound was exceeded.
    OwnershipConflict, ///< Project ownership or live lease could not be established.
    IoFailure ///< Filesystem, cryptographic or durable publication failure.
};

/** Typed input failure with an actionable UTF-8 diagnostic. */
class InputError : public std::runtime_error {
public:
    /** Preserve a stable error category alongside the diagnostic text. */
    InputError(InputErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    /** Return the failure category without allocating. */
    InputErrorCode Code() const noexcept { return code_; }

private:
    InputErrorCode code_;
};

/** Owned media import and current-input inspection under an open project lease. */
class ProjectInput final {
public:
    /** Decode bounded metadata only, without opening, hashing or changing any files. */
    static InputManifest DecodeManifestBytes(const std::string& bytes, const std::string& projectId);
    /** Copy one source into project storage and publish its verified manifest. */
    static InputManifest Import(ProjectRepository& repository, InputKind kind,
                                const std::filesystem::path& source);
    /** Read and verify the current manifest and every owned file; NoInput if absent. */
    static InputManifest Inspect(ProjectRepository& repository);
};

} // namespace MonsterEngine::VideoTo3D
