// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MonsterRender::Splat {
/** Requirements extracted from selected final SPIR-V manifest entries. */
struct FAndroidShaderRequirements {
    bool valid = false; ///< False for absent, malformed or incomplete inventory.
    bool shaderInt64 = false, shaderFloat64 = false, shaderFloat16 = false; ///< Actual capability declarations.
    std::array<uint32_t,3> localSize{1,1,1}; ///< Maximum per-axis workgroup sizes.
    uint64_t maxInvocations = 1; ///< Maximum product for any selected shader.
    uint64_t sharedMemoryBytes = 0; ///< Conservative maximum compiled workgroup storage.
    std::vector<uint32_t> capabilities; ///< Unique final SPIR-V capability IDs.
    std::string shaderIdentity; ///< SHA256 of supplied manifest bytes.
};
/** Bounded opt-in fault controls. They simulate failure, never successful execution. */
enum class EAndroidProbeFault : uint8_t { None, LoaderUnavailable, LimitAPITo10, HideInt64, FailSurface };
/** Reproducible identity supplied by packaging/platform code, unavailable fields empty. */
struct FAndroidProbeIdentity {
    std::string application, build, workspace, abi; ///< Public app/source/build identity.
    std::string apkSHA256, nativeSHA256, configSHA256; ///< Exact artifacts when measured.
    std::optional<bool> emulator, translated; ///< Unknown differs from observed false.
};
/** Synchronous read-only capability query; does not initialize renderer globals. */
struct FAndroidProbeOptions {
    void* nativeWindow = nullptr; ///< Borrowed ANativeWindow, retained by caller through Probe.
    std::string shaderManifestJSON; ///< Verified packaged shader-manifest.json bytes.
    std::vector<std::string> activeShaders; ///< Empty selects the standard non-fusion pipeline.
    FAndroidProbeIdentity identity; ///< Observed packaging and platform identity.
    EAndroidProbeFault fault = EAndroidProbeFault::None; ///< Explicit diagnostic-only simulation.
};
/** Versioned immutable-by-copy report; JSON retains partial results on any failure. */
struct FAndroidCapabilityReport {
    bool diagnosticsOnly = true; ///< No device currently meets all queried admission gates.
    bool renderEligible = false; ///< Static preflight only; never means execution passed.
    std::string json; ///< Schema-1 report; unavailable observations are null or pending.
};
/** Actual owner-selected device identity; UUID is lowercase hex when queried. */
struct FAndroidPhysicalDeviceIdentity {
    uint32_t vendorID=0,deviceID=0,driverVersion=0; ///< Actual VkPhysicalDeviceProperties fields.
    std::string name,deviceUUID; ///< Observed name and optional Vulkan device UUID.
};
/** Evidence from an actual owner-thread session, associated with original identities. */
struct FAndroidExecutionObservation {
    uint32_t physicalDeviceIndex = 0; ///< Index in this report's physical device array.
    std::string stage; ///< graphicsSmoke, splatSmoke or streaming.
    bool passed = false; ///< Explicit observed result; failure requires a reason.
    bool submissionCompleted = false; ///< Actual completion of submissionToken observed.
    bool readbackValidated = false; ///< Actual image/data validation performed.
    uint64_t submissionToken = 0, frameId = 0; ///< Real completion token and original frame.
    uint64_t sceneGeneration = 0, surfaceGeneration = 0; ///< Original owner generations.
    std::vector<std::string> requestedFeatures, enabledFeatures; ///< Actual successful device request.
    int32_t vkResult = 0; ///< Observed raw result, not a synthetic success measurement.
    std::string reason; ///< Bounded execution failure/validation explanation.
};
/** Independent Vulkan probe and explicit later execution evidence integration. */
class FAndroidCapabilities {
public:
    /** Parse actual manifest requirements without making Vulkan calls. */
    static FAndroidShaderRequirements ParseShaderRequirements(const std::string& manifest,
        const std::vector<std::string>& activeShaders, std::string& error);
    /** Creates/destroys only local instance/surface, preserving renderer dispatch. */
    static FAndroidCapabilityReport Probe(const FAndroidProbeOptions& options);
    /** Match the actual owner device to exactly one independent report entry.
     * Missing or ambiguous identity never falls back to enumeration order. */
    static bool MatchPhysicalDevice(const FAndroidCapabilityReport&,
        const FAndroidPhysicalDeviceIdentity&, uint32_t& reportIndex, std::string& error,
        bool requireRenderEligible = false);
    /** Add real owner observations; successful probes require token completion/readback.
     * Input is trusted owner evidence, not arbitrary JNI/user JSON. Does not execute GPU work.
     */
    static bool ObserveExecution(FAndroidCapabilityReport&, const FAndroidExecutionObservation&, std::string& error);
};
} // namespace MonsterRender::Splat
