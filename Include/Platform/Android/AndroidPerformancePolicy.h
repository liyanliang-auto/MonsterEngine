// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace MonsterRender::Splat {
/** Only this selected owner may originate frame tokens. No policy timer is created. */
enum class EAndroidFrameScheduler : uint8_t { Choreographer, Swappy };
/** Ordered owner actions; safe retirement must precede releasing budget leases. */
enum class EAndroidPressureAction : uint8_t { None, TrimCache, RetireSafePages, ReleaseScene };
/** Frozen public identities/inputs captured before an optional M3 experiment. */
struct FAndroidExperimentInputs {
    std::string device, driver, abi; ///< Observed device/driver/ABI, never inferred support.
    std::string sceneHash, cameraHash, configHash, shaderHash; ///< Exact experiment identities.
    uint32_t width = 0, height = 0; ///< Fixed internal render dimensions.
    uint64_t aggregateBudgetBytes = 0; ///< Admitted shared Java/native/GPU backing cap.
    double qualityPixels = 3; ///< Fixed baseline quality threshold.
    int32_t initialThermalStatus = -1; ///< Android thermal status; -1 means unavailable.
    std::optional<double> initialTemperatureCelsius; ///< Actual observed temperature if available.
};
/** Explicit gates; measurementAccepted is supplied only after external evidence review. */
struct FAndroidPerformanceConfig {
    bool thermalQualityOptIn = false; ///< Baseline leaves quality fixed.
    bool swappyOptIn = false; ///< Default is Choreographer only.
    bool swappyCompiled = false, swappyCapable = false; ///< Both actual integration facts required.
    bool measurementAccepted = false; ///< No default claim of pacing/quality improvement.
    double maximumQualityPixels = 8; ///< Optional degradation limit, never changes capacity.
};
/** Copied action proposal; rendering owner records completion separately. */
struct FAndroidPerformanceDecision {
    EAndroidFrameScheduler scheduler = EAndroidFrameScheduler::Choreographer; ///< Sole selected scheduler.
    EAndroidPressureAction pressure = EAndroidPressureAction::None; ///< Ordered safe action requested.
    bool stopPrefetch = false; ///< Stop new speculative reads before cache/page retirement.
    bool manualRecoveryRequired = false; ///< No automatic reopening after critical pressure.
    bool qualityChanged = false; ///< Thermal opt-in proposes a different quality threshold.
    double qualityPixels = 3; ///< Proposed effective threshold; caller applies a new config generation.
    std::string reason; ///< Stable action/gating reason, no performance prediction.
};
/** Owner-thread policy. It neither allocates renderer resources nor schedules timers.
 * Baseline FP32/radix32/fusion-off capacities remain the session's responsibility. */
class FAndroidPerformancePolicy {
public:
    /** Freeze a copy of inputs. Optional M3 modes require complete identities/evidence.
     * Swappy gate failure leaves the current policy unchanged and returns false. */
    bool Configure(const FAndroidPerformanceConfig&, const FAndroidExperimentInputs&, std::string& error);
    /** Immutable copied experiment identity; later observations cannot rewrite it. */
    const FAndroidExperimentInputs& Experiment() const noexcept;
    /** Reject duplicate/stale tokens and every unselected scheduler; no second loop. */
    bool AcceptFrame(EAndroidFrameScheduler source, uint64_t monotonicNanoseconds) noexcept;
    /** Android trim constants; UI_HIDDEN alone is not a low-memory observation. */
    FAndroidPerformanceDecision OnMemoryPressure(int32_t level);
    /** Android thermal status -1..6. Optional quality degrades monotonically per incident. */
    FAndroidPerformanceDecision OnThermalStatus(int32_t status);
    /** Acknowledged user retry clears sticky pressure/quality and timestamp history.
     * Caller must independently re-admit all actual new/old overlapping backing. */
    FAndroidPerformanceDecision RequestManualRecovery();
    /** Current requested actions; does not mean resource retirement was completed. */
    FAndroidPerformanceDecision Snapshot() const;
private:
    FAndroidPerformanceConfig config; ///< Frozen effective policy gates.
    FAndroidExperimentInputs experiment; ///< Frozen pre-experiment inputs.
    FAndroidPerformanceDecision decision; ///< Sticky safe degradation request.
    uint64_t lastFrame = 0; ///< Last accepted timestamp for the selected scheduler.
};
} // namespace MonsterRender::Splat
