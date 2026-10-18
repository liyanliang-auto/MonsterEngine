// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatLODTypes.h"

namespace MonsterRender::Splat {
/** Main-thread CPU selector. The returned cut never includes a node and its descendants. */
class FSplatLODSelector {
public:
    /** Select a complete resident cut and priority-ordered asynchronous requests.
     * Versioned mobile RMS/center/support metadata uses the same projection
     * semantics as the desktop center-error builder. Missing children retain ancestors.
     * Tile costs inferred from node bounds are conservative estimates, not measured
     * Gaussian-tile counts. The pipeline's same-frame GPU overflow guard is authoritative.
     */
    FSplatLODSelection Select(const FSplatSceneManifest& manifest,
        const FSplatLODView& view, const FSplatLODSettings& settings,
        const FSplatResidencySnapshot& residency);
    /** Clear hysteresis/freeze history when changing scenes or replay runs. */
    void Reset();

private:
    const FSplatSceneManifest* m_manifest = nullptr; ///< Identity of history owner.
    uint64_t m_sceneGeneration = 0; ///< Reject history from another scene generation.
    uint64_t m_residencyVersion = 0; ///< Version of prior source mapping.
    uint64_t m_version = 0; ///< Monotonic actual-cut/mapping version.
    std::vector<uint32_t> m_previousSelected; ///< Prior resident cut for hysteresis/freeze.
    std::vector<uint32_t> m_previousDesired; ///< Prior target cut for frozen unloaded scenes.
};
} // namespace MonsterRender::Splat
