// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatBuildTypes.h"
#include "Renderer/Splat/SplatBudgetCoordinator.h"
#include <functional>

namespace MonsterRender::Splat {
/** Trusted native/JVM adapter invoked only on the Run worker. Hash the actual
 * loaded library or exact APK .so entry with bounded buffers and control polls.
 * A builderHash string from untrusted settings is never sufficient identity. */
using FAndroidBuildBinaryIdentityResolver = std::function<bool(
    const FSplatBuildControl&, std::string& sha256, std::string& error)>;
/** Private stable paths and typed policy. Create only validates bounded values;
 * filesystem/identity work is deferred to Run. The caller pins source/output
 * ownership until retirement. An APK pseudo-path is not a normal file path. */
struct FAndroidBuildJobRequest {
    std::string stableSource; ///< Immutable private source file.
    std::string jobDirectory; ///< Durable private journal directory.
    std::string outputDirectory; ///< New SceneRepository staging child, same filesystem.
    std::string nativeBinaryPath; ///< Optional actual filesystem .so for fallback hashing.
    FAndroidBuildBinaryIdentityResolver resolveBinaryIdentity; ///< Trusted APK-aware worker callback, if present.
    FSplatBoundedBuildConfig config; ///< Optional builderHash is expected identity, compared against measured identity.
};
/** Copied progress. retired means Run has returned all workspace/runtime leases;
 * it does not mean journal/source files were removed. No native token is persisted. */
struct FAndroidBuildJobSnapshot {
    FSplatBuildProgress progress; ///< Last actual bounded/committed builder observation.
    std::string builderHash; ///< Actual binary identity after worker resolution.
    bool running = false; ///< A worker currently owns Run.
    bool retired = true; ///< No worker workspace or runtime lease remains.
};
/** Actual Run result, serialized into NativeResult by JNI. */
struct FAndroidBuildJobResult {
    bool ok = false; ///< True only after builder validated and atomically published output.
    std::string code; ///< completed/paused/cancelled/waiting_for_resources/failed/invalid_handle/busy.
    std::string message; ///< Bounded private diagnostic, redact on export.
    FAndroidBuildJobSnapshot snapshot; ///< Original progress and retirement state.
};
/** CPU-only process build registry. Handles pack nonzero u32 generation/ID and
 * are checked on every call. One worker globally; no Activity/Surface/Vulkan.
 * JNI passes a copy of its SAME process coordinator, never a separate account. */
class FAndroidBuildJobHost {
public:
    /** No file I/O. Caller must use worker for any resolver/source preparation. */
    static uint64_t Create(FAndroidBuildJobRequest, FSplatBudgetCoordinator sharedBudget, std::string& error);
    /** Blocking worker-only. Acquires exclusive build admission only after the
     * caller retired heavy viewer backing; failure is waiting_for_resources.
     * Committed checkpoints survive pause/cancel; resume with Control then Run. */
    static FAndroidBuildJobResult Run(uint64_t handle);
    /** Concurrent bounded signal only; resume is rejected while Run is retiring. */
    static bool Control(uint64_t handle, const std::string& action, std::string& error);
    /** Thread-safe copied progress. Never performs filesystem or builder work. */
    static bool Snapshot(uint64_t handle, FAndroidBuildJobSnapshot&, std::string& error);
    /** Invalidates registry ownership then signals cancellation; no join or I/O.
     * An already-running Run retains all ownership until safe retirement. */
    static bool Destroy(uint64_t handle);
};
}
