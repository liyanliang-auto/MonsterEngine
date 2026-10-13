// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace MonsterRender::Splat {
/** Invalid node/page identifier. */
inline constexpr uint32_t SplatInvalidId = std::numeric_limits<uint32_t>::max();
/** Versioned error semantics; manifest v2 adds coverage, v3 center bounds; pages remain v1. */
inline constexpr const char* SplatRMSLegacyErrorBuilderVersion = "MonsterEngine-SplatLOD-2-RMS";
inline constexpr const char* SplatRMSErrorBuilderVersion = "MonsterEngine-SplatLOD-3-RMSFootprint";
inline constexpr const char* SplatCenterErrorBuilderVersion = "MonsterEngine-SplatLOD-4-RMSCenterDepth";
inline constexpr const char* SplatFusionBuilderVersion = "MonsterEngine-SplatLOD-5-OriginalFusionGroups";
inline constexpr const char* SplatLeafFusionBuilderVersion = "MonsterEngine-SplatLOD-6-CompleteOriginalFusionGroups";
/** Disjoint contiguous ORIGINAL active members after source mapping, at most four. */
struct FSplatFusionGroup {
    uint32_t firstCount = 0x80000000u, stableId = SplatInvalidId;
    FSplatFusionGroup() = default;
    FSplatFusionGroup(uint32_t first, uint32_t count, uint32_t id = SplatInvalidId)
        : firstCount(first < (1u << 28) && count >= 1 && count <= 4 ? first | ((count - 1) << 28) : 0x80000000u), stableId(id) {}
    uint32_t First() const { return firstCount & 0x0fffffffu; }
    uint32_t Count() const { return firstCount & 0x80000000u ? 0 : ((firstCount >> 28) & 3u) + 1; }
};
/** v4 members index immediate leaf children; v5 also permits a leaf's own page rows. */
struct FSplatOriginalFusionGroup { uint32_t count = 0; uint32_t rows[4] = {}; };
/** Activated Gaussian, coefficient-major RGB SH, quaternion (w,x,y,z). */
struct FSplatGaussian {
    float position[4] = {0,0,0,1}; ///< World position, homogeneous w=1.
    float scale[4] = {1,1,1,0}; ///< Positive standard deviations, unused w=0.
    float rotation[4] = {1,0,0,0}; ///< Unit quaternion in renderer convention.
    float opacity = 1; ///< Activated opacity in [0,1].
    float sh[48] = {}; ///< Coefficient-major RGB, scene-global active stride.
    uint64_t originalId = 0; ///< Stable source row identity; proxy IDs may be synthetic.
};
/** Conservative finite-support bounds in source world coordinates. */
struct FSplatBounds {
    float min[3] = {}; ///< Lower corner.
    float max[3] = {}; ///< Upper corner.
};
/** One independently decodable on-disk page. */
struct FSplatPageInfo {
    uint32_t id = 0; ///< Dense page ID equal to index in manifest.
    std::string relativePath; ///< Safe relative path beneath scene directory.
    uint64_t offset = 0; ///< Range start in file.
    uint64_t byteSize = 0; ///< Encoded range size including page header.
    uint64_t decodedByteSize = 0; ///< In-memory FSplatGaussian storage size.
    uint32_t gaussianCount = 0; ///< Page record count.
    std::string sha256; ///< Lowercase SHA256 of encoded page bytes.
    bool pinned = false; ///< Mandatory coarse fallback residency.
};
/** Node representation replaces its entire descendant region. */
struct FSplatLODNode {
    FSplatBounds bounds; ///< Conservative Gaussian support bounds.
    std::vector<uint32_t> children; ///< Nonempty octants; at most eight child IDs.
    std::vector<uint32_t> pageIds; ///< All pages required for this representation.
    uint32_t gaussianCount = 0; ///< Sum of representation page counts.
    float geometricError = 0; ///< World Gaussian-transport RMS bound for the RMS builder; legacy finite-support displacement otherwise.
    float appearanceError = 0; ///< World-footprint-weighted SH/opacity RMS for the RMS builder; legacy dimensionless estimate otherwise.
    double coverageAreaUpper = 0; ///< Manifest v2: sum alpha*max(sx*sy,sx*sz,sy*sz), world squared; bounds projected Gaussian area for every viewing direction.
    FSplatBounds centerBounds; ///< Manifest v3: contains centers of this representation and every descendant original/proxy; support bounds still control visibility.
    std::vector<FSplatOriginalFusionGroup> originalFusionGroups; ///< v4 leaf-parent or v5 leaf-self ORIGINAL partition; no camera data.
    uint32_t level = 0; ///< Root depth is zero.
};
/** Versioned scene index; leaves preserve normalized source records. */
struct FSplatSceneManifest {
    uint32_t version = 1; ///< Protocol version.
    uint32_t shDegree = 0; ///< Global degree, zero through three.
    std::string sourceSHA256; ///< SHA256 of original source file.
    std::string builderVersion = "MonsterEngine-SplatLOD-1"; ///< Tool identity.
    std::string builderHash; ///< Tool revision/build identity SHA256.
    std::string configHash; ///< Canonical build configuration SHA256.
    std::string buildConfig; ///< Canonical configuration retained for reproducibility.
    std::string endian = "little"; ///< On-disk endian convention.
    std::string coordinates = "source-world"; ///< No implicit coordinate conversion.
    std::string quaternion = "wxyz"; ///< Rotation layout.
    std::string shLayout = "coefficient-major-rgb"; ///< Activated SH layout.
    float supportSigma = 3; ///< Support multiplier, at least the renderer's three sigma.
    uint32_t rootNode = SplatInvalidId; ///< Empty scenes have no root.
    std::vector<FSplatLODNode> nodes; ///< Dense node array.
    std::vector<FSplatPageInfo> pages; ///< Dense page array.
    uint64_t sourceCount = 0; ///< Original input count.
    uint64_t leafCount = 0; ///< Original records represented by leaves.
    uint64_t proxyCount = 0; ///< Nonleaf proxy record count.
    uint64_t totalDataBytes = 0; ///< Sum of encoded page sizes.
};
/** Offline construction controls. */
struct FSplatLODBuildConfig {
    uint32_t maxLeafGaussians = 8192; ///< Leaf subdivision threshold.
    uint32_t maxDepth = 16; ///< Hard recursion bound.
    uint32_t maxPageGaussians = 8192; ///< Independently loaded page size limit.
    float supportSigma = 3; ///< Support bound multiplier; values below three are rejected.
    float proxyReduction = 0.25f; ///< Target parent/child representation ratio.
    float geometricErrorScale = 1; ///< Error safety factor.
    float appearanceWeight = 1; ///< Appearance term in clustering.
    float maxAppearanceError = 0.1f; ///< Appearance merge tolerance.
    bool projectedFusionGroups = false; ///< Optional v4 membership; v3 stays the compatibility default.
    std::string sourceSHA256; ///< Source-file identity supplied by CLI.
    std::string builderHash; ///< Build identity supplied by CLI.
};
/** Camera input independent of engine math and Vulkan types. */
struct FSplatLODView {
    float view[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; ///< Column-major view.
    float projection[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; ///< Column-major projection.
    float cameraPosition[3] = {}; ///< World camera position.
    uint32_t width = 1920, height = 1080; ///< Render dimensions.
    float nearPlane = 0.1f, farPlane = 1000; ///< Positive depth limits.
    float gaussianScale = 1; ///< Renderer standard-deviation scale, 0.01 through 10.
    bool distanceCulling = true; ///< Apply near/far rejection only when enabled.
};
/** Runtime cut and capacity policy. */
struct FSplatLODSettings {
    float qualityPixels = 1; ///< Maximum projected representation error.
    float hysteresis = 0.2f; ///< Fractional refine/coarsen dead band.
    uint64_t activeBudget = 4000000; ///< Active Gaussian cap.
    uint64_t sortBudget = 64000000; ///< Gaussian-tile entry cap.
    uint64_t sourceBudget = 8000000; ///< Source pool Gaussian cap.
    bool freeze = false; ///< Preserve prior cut while still checking residency.
};
/** One ready page's source-pool mapping. */
struct FSplatResidentPage {
    uint32_t pageId = SplatInvalidId; ///< Manifest page ID.
    uint32_t sourceOffset = 0; ///< First Gaussian slot.
    uint32_t gaussianCount = 0; ///< Allocated Gaussian slots.
    uint64_t generation = 0; ///< Slot generation protects stale mappings.
    bool ready = false; ///< Upload completion has been observed.
};
/** Immutable per-frame residency snapshot. */
struct FSplatResidencySnapshot {
    std::vector<FSplatResidentPage> pages; ///< Entries indexed by page ID.
    std::vector<bool> nodeReady; ///< Optional cached complete representation readiness.
    uint64_t sceneGeneration = 0; ///< Owning scene generation.
    uint64_t residencyVersion = 0; ///< Changes on readiness/mapping transitions.
    bool IsPageReady(uint32_t id) const { return id < pages.size() && pages[id].ready; }
    bool IsNodeReady(const FSplatSceneManifest& m, uint32_t id) const {
        if (id >= m.nodes.size()) return false;
        for (auto p : m.nodes[id].pageIds) if (!IsPageReady(p)) return false;
        return !m.nodes[id].pageIds.empty() || m.nodes[id].gaussianCount == 0;
    }
};
/** Complete resident cut and desired asynchronous refinement. */
struct FSplatLODSelection {
    std::vector<uint32_t> selectedNodeIds; ///< Actual mutually exclusive resident cut.
    std::vector<uint32_t> desiredNodeIds; ///< Requested ideal cut.
    std::vector<uint32_t> requestPageIds; ///< Missing visible representation pages.
    std::vector<uint32_t> prefetchPageIds; ///< Lower-priority predicted pages.
    uint64_t activeCount = 0; ///< Actual active record count.
    uint64_t selectedLeafNodes = 0, selectedProxyNodes = 0; ///< Actual cut composition, independent of record count.
    uint64_t proxyGaussianCount = 0; ///< Active records originating from nonleaf representations.
    double maxSelectedProxyErrorPixels = 0; ///< Projected combined error; -1 denotes a forced near-plane fallback.
    uint64_t selectionVersion = 0; ///< Changes when actual cut changes.
    bool degraded = false; ///< Quality reduced due to budget/readiness.
    std::string degradationReason; ///< Diagnostic reason.
};
/** Compact per-frame input indirection into persistent source pool. */
struct FSplatActiveSet {
    std::vector<uint32_t> sourceIndices; ///< sourceIndices[compactActiveIdx].
    uint64_t selectionVersion = 0; ///< Cut identity.
    uint64_t residencyVersion = 0; ///< Source mapping identity.
    std::vector<FSplatFusionGroup> fusionGroups; ///< Optional exact original partition; bypass for A/B/full-refined output.
    uint64_t sceneGeneration = 0; ///< Stable membership/attribute generation, independent of reused pool slots.
};
} // namespace MonsterRender::Splat
