// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatBuildTypes.h"

namespace MonsterRender::Splat {
/** Deterministic single-worker external-memory mobile builder. Journal artifacts
 * and cumulative-error weighted sidecars survive process death. No GPU or legacy
 * Build(LoadCPU) dependency. All paths refer to stable application-owned storage. */
class FSplatStreamingLODBuilder {
public:
    /** Strict bounded flat numeric policy JSON. Unknown/duplicate fields fail.
     * Builder/parser/schema identities cannot be overridden by this policy file.
     * Set config.builderHash to the actual binary SHA256 before Run. */
    static bool ParseConfig(const std::string& json, FSplatBoundedBuildConfig& config, std::string& error);
    /** Canonical path-free identity includes every algorithm and resource option. */
    static std::string CanonicalConfig(const FSplatBoundedBuildConfig&);
    /** Validate policy arithmetic before allocating parser/partition/proxy memory.
     * requireBuildHash=false is only for pre-admission UI/config parsing. */
    static bool ValidateConfig(const FSplatBoundedBuildConfig&, std::string& error, bool requireBuildHash = true);
    /** Worker-only. Resume is automatically selected by exact source/config/binary
     * journal identity. A true result means independently validated scene.mgs and
     * pages were atomically published, never only a queued task or parsed header.
     * Pause/cancel leaves committed work intact. Call again with fresh control.
     * Callback receives copies at committed/bounded progress boundaries.
     * jobDirectory and outputDirectory must share a filesystem; output is new or
     * the same journal's recoverable completed publication. Source is immutable. */
    bool Run(const std::string& stableSource, const std::string& jobDirectory,
        const std::string& outputDirectory, const FSplatBoundedBuildConfig&,
        FSplatBuildControl&, FSplatBuildProgressCallback progress, std::string& error);
};
}
