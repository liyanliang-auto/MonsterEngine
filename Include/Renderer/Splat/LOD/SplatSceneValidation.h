// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatBuildTypes.h"

namespace MonsterRender::Splat {
/** Explicit CPU validation admission, independent of GPU residency. */
struct FSplatSceneValidationLimits {
    uint64_t maxManifestBytes = 32ull << 20; ///< Encoded manifest cap before parsing.
    uint64_t maxPageBytes = 16ull << 20; ///< One encoded range cap.
    uint64_t maxDecodedPageBytes = 16ull << 20; ///< One decoded Gaussian vector cap.
    uint64_t maxWorkingBytes = 256ull << 20; ///< Manifest/index plus one page peak.
    uint64_t maxTotalPageBytes = 32ull << 30; ///< Total referenced encoded ranges.
    uint64_t maxPLYBytes = 32ull << 20; ///< Small PLY publication cap.
    uint64_t maxPLYRecords = 100000; ///< Small PLY publication row cap.
    uint32_t maxNodes = 100000, maxPages = 100000; ///< Resident index limits.
};
/** Validation evidence; no scene becomes visible merely because its header parsed. */
struct FSplatSceneValidationResult {
    std::string contentSHA256; ///< Complete manifest or source file hash.
    uint64_t sourceRecords = 0; ///< Declared, parser-verified source count for PLY.
    uint64_t pageCount = 0; ///< All pages successfully decoded.
    uint64_t encodedPageBytes = 0; ///< Exact validated referenced ranges.
    uint32_t shDegree = 0; ///< Source scene SH layout.
};
/** CPU-only native publication gate shared by JNI and package import. */
class FSplatSceneValidation {
public:
    /** Directory implies scene.mgs; .mgs names a manifest; .ply validates bounded rows.
     * Files must be stable private copies. Reject links and escaping page components.
     * Output changes only on complete success; control is polled between bounded work.
     */
    static bool Validate(const std::string& path, const FSplatSceneValidationLimits&,
        FSplatSceneValidationResult&, std::string& error, const FSplatBuildControl* control = nullptr);
};
} // namespace MonsterRender::Splat
