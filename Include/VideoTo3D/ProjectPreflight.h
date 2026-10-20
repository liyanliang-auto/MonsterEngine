// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace MonsterEngine::VideoTo3D {

/** Stable outcome of a bounded, observational preflight operation. */
enum class PreflightCode {
    Ok, InvalidArgument, InvalidBudget, ArithmeticOverflow, UnsupportedPath,
    MissingDestination, NotDirectory, UnsafePath, AccessDenied, CapacityQueryFailed,
    InsufficientSpace, ProbeCreateFailed, ProbeWriteFailed, ProbeFlushFailed,
    ProbeCloseFailed, ProbeCleanupFailed, MissingProject, NoInput, UnsupportedSchema,
    CorruptData, IdentityMismatch, IoFailure, UnstableObservation
};

/** Return a stable ASCII status name without allocating. */
inline const char* PreflightCodeName(PreflightCode code) noexcept {
    switch (code) {
#define MONSTER_PREFLIGHT_NAME(value) case PreflightCode::value: return #value
    MONSTER_PREFLIGHT_NAME(Ok); MONSTER_PREFLIGHT_NAME(InvalidArgument);
    MONSTER_PREFLIGHT_NAME(InvalidBudget); MONSTER_PREFLIGHT_NAME(ArithmeticOverflow);
    MONSTER_PREFLIGHT_NAME(UnsupportedPath); MONSTER_PREFLIGHT_NAME(MissingDestination);
    MONSTER_PREFLIGHT_NAME(NotDirectory); MONSTER_PREFLIGHT_NAME(UnsafePath);
    MONSTER_PREFLIGHT_NAME(AccessDenied); MONSTER_PREFLIGHT_NAME(CapacityQueryFailed);
    MONSTER_PREFLIGHT_NAME(InsufficientSpace); MONSTER_PREFLIGHT_NAME(ProbeCreateFailed);
    MONSTER_PREFLIGHT_NAME(ProbeWriteFailed); MONSTER_PREFLIGHT_NAME(ProbeFlushFailed);
    MONSTER_PREFLIGHT_NAME(ProbeCloseFailed); MONSTER_PREFLIGHT_NAME(ProbeCleanupFailed);
    MONSTER_PREFLIGHT_NAME(MissingProject); MONSTER_PREFLIGHT_NAME(NoInput);
    MONSTER_PREFLIGHT_NAME(UnsupportedSchema); MONSTER_PREFLIGHT_NAME(CorruptData);
    MONSTER_PREFLIGHT_NAME(IdentityMismatch); MONSTER_PREFLIGHT_NAME(IoFailure);
    MONSTER_PREFLIGHT_NAME(UnstableObservation);
#undef MONSTER_PREFLIGHT_NAME
    }
    return "Unknown";
}

/** Actionable UTF-8 diagnostic and an optional Windows error value. */
struct PreflightDiagnostic {
    PreflightCode code{PreflightCode::Ok}; ///< Stable machine-readable outcome.
    std::string message; ///< UTF-8 explanation, without assuming system ANSI encoding.
    std::uint32_t nativeError{0}; ///< GetLastError value, or zero if not applicable.
};

/** Internal fallible-helper exception preserved by public result-returning services. */
class PreflightFailure : public std::runtime_error {
public:
    /** Preserve the typed diagnostic alongside its exception text. */
    explicit PreflightFailure(PreflightDiagnostic diagnostic)
        : std::runtime_error(diagnostic.message), diagnostic_(std::move(diagnostic)) {}
    /** Inspect the original diagnostic without allocation. */
    const PreflightDiagnostic& Diagnostic() const noexcept { return diagnostic_; }
private:
    PreflightDiagnostic diagnostic_;
};

/** Disjoint additional peak bytes, excluding bytes already resident on the volume.
 * Every field is required, even when zero; reserve is additional headroom.
 */
struct SpaceBudget {
    std::optional<std::uint64_t> mediaCopies; ///< Additional project-owned media copies.
    std::optional<std::uint64_t> preparedImages; ///< Additional prepared images.
    std::optional<std::uint64_t> reconstructionDatabase; ///< Additional reconstruction data.
    std::optional<std::uint64_t> checkpoints; ///< Additional training checkpoints.
    std::optional<std::uint64_t> temporaryExports; ///< Additional temporary export data.
    std::optional<std::uint64_t> lod; ///< Additional LOD data.
    std::optional<std::uint64_t> newAssets; ///< Additional new/replacement assets.
    std::optional<std::uint64_t> retainedOverlap; ///< Incremental overlap absent from other fields.
    std::optional<std::uint64_t> reserve; ///< Additional caller-selected free-space headroom.
};

/** Point-in-time capacity sample; it is never a reservation. */
struct SpaceSample {
    std::uint64_t availableToCaller{0}; ///< Bytes available under the caller's quota.
    std::uint64_t volumeFree{0}; ///< Windows-reported free bytes on the volume.
    std::uint64_t volumeTotal{0}; ///< Windows-reported total capacity.
};

/** Checked planning totals and any actual capacity observation. */
struct SpacePreflightResult {
    PreflightDiagnostic diagnostic; ///< Outcome and actionable failure detail.
    std::uint64_t incrementalBytes{0}; ///< Checked sum of the eight incremental categories.
    std::uint64_t reserveBytes{0}; ///< Explicit additional headroom.
    std::uint64_t requiredBytes{0}; ///< Checked incremental bytes plus reserve.
    std::optional<SpaceSample> sample; ///< Observed or supplied sample, if available.
};

/** Compare checked totals with caller capacity; equality succeeds, no I/O occurs. */
SpacePreflightResult EvaluateSpaceBudget(const SpaceBudget&, const SpaceSample&);
/** Query one existing guarded directory; never create it or reserve capacity. */
SpacePreflightResult QuerySpacePreflight(const std::filesystem::path& directory, const SpaceBudget&);

/** Real bounded probe lifecycle; a failed cleanup reports only the owned entry. */
struct WritePreflightResult {
    PreflightDiagnostic diagnostic; ///< Outcome, preserving native failure detail.
    std::filesystem::path residualPath; ///< Owned remaining probe, empty if absent.
    std::uint64_t bytesWritten{0}; ///< Actual bytes successfully written.
    bool created{false}; ///< Exclusive creation succeeded.
    bool flushed{false}; ///< Explicit data flush succeeded.
    bool closed{false}; ///< Explicit handle close succeeded.
    bool removed{false}; ///< Owned probe removal succeeded.
};

/** Probe create/write/flush/close/remove without creating the destination directory. */
WritePreflightResult ProbeDirectoryWrite(const std::filesystem::path& directory);

/** Inspection depth of one bounded project or input metadata file. */
struct CompatibilityFileResult {
    PreflightDiagnostic diagnostic; ///< File-specific outcome; NoInput is informational only.
    std::optional<std::uint32_t> schemaVersion; ///< Version only when framing establishes it.
    std::string projectId; ///< Identity only when successfully decoded.
    bool fullyValidatedStructure{false}; ///< Existing supported-format validator succeeded.
};

/** Observational metadata inspection, never migration, queue recovery or media hashing. */
struct CompatibilityPreflightResult {
    PreflightDiagnostic diagnostic; ///< Aggregate outcome; optional absent input can succeed.
    CompatibilityFileResult project; ///< Project snapshot inspection depth.
    CompatibilityFileResult input; ///< Optional input manifest inspection depth.
    bool inputPresent{false}; ///< An input manifest was observed.
    bool mediaIntegrityVerified{false}; ///< False for this metadata-only inspection.
    bool stableObservation{false}; ///< Guarded inspected identities remained unchanged.
};

/** Inspect supported metadata via bounded decoders without mutating any project bytes. */
CompatibilityPreflightResult InspectProjectCompatibility(const std::filesystem::path& directory);

} // namespace MonsterEngine::VideoTo3D
