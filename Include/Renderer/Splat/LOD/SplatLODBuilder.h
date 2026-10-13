// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatLODTypes.h"
namespace MonsterRender::Splat {
/** Deterministic offline sparse-octree construction; no graphics device required. */
class FSplatLODBuilder {
public:
    /** Deterministically add one verified Gaussian's anisotropic coverage statistic. */
    static void AccumulateCoverage(FSplatLODNode& node, const FSplatGaussian& gaussian);
    /** Offline partition of verified immediate leaf-child rows, no camera data.
     * Uses existing deterministic grouping; rejects groups larger than four. */
    static bool BuildOriginalFusionGroups(const std::vector<FSplatGaussian>& originals,
        const FSplatBounds& centers, const FSplatLODBuildConfig& config,
        std::vector<FSplatOriginalFusionGroup>& output, std::string& error, uint32_t shDegree = 3);
    /** Builds scene.mgs and independently checksummed page files in a new directory.
     * Publishes the manifest only after successful construction. Existing output is rejected.
     */
    bool Build(const std::vector<FSplatGaussian>& source, uint32_t shDegree,
               const FSplatLODBuildConfig& config, const std::string& outputDirectory,
               FSplatSceneManifest& output, std::string& error);
    /** Canonical configuration JSON used for reproducibility and hashing. */
    static std::string CanonicalConfig(const FSplatLODBuildConfig& config);
};
}
