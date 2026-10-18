// Copyright Monster Engine. All Rights Reserved.
#include "Platform/Android/AndroidPerformancePolicy.h"
#include <algorithm>
#include <cmath>

namespace MonsterRender::Splat {
namespace {
bool Hash(const std::string& value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
bool Text(const std::string& value) { return !value.empty() && value.size() <= 512; }
bool Complete(const FAndroidExperimentInputs& input) {
    return Text(input.device) && Text(input.driver) && Text(input.abi) && Hash(input.sceneHash) &&
        Hash(input.cameraHash) && Hash(input.configHash) && Hash(input.shaderHash) &&
        input.width && input.height && input.aggregateBudgetBytes;
}
}
bool FAndroidPerformancePolicy::Configure(const FAndroidPerformanceConfig& requested,
    const FAndroidExperimentInputs& inputs, std::string& error) {
    if (!std::isfinite(inputs.qualityPixels) || inputs.qualityPixels <= 0 ||
        !std::isfinite(requested.maximumQualityPixels) || requested.maximumQualityPixels < inputs.qualityPixels ||
        inputs.initialThermalStatus < -1 || inputs.initialThermalStatus > 6 ||
        (inputs.initialTemperatureCelsius && !std::isfinite(*inputs.initialTemperatureCelsius))) {
        error = "invalid_performance_policy"; return false;
    }
    const bool optional = requested.thermalQualityOptIn || requested.swappyOptIn;
    if (optional && (!requested.measurementAccepted || !Complete(inputs))) {
        error = "m3_experiment_identity_or_measurement_missing"; return false;
    }
    if (requested.swappyOptIn && (!requested.swappyCompiled || !requested.swappyCapable)) {
        error = "swappy_integration_or_capability_missing"; return false;
    }
    config = requested; experiment = inputs; decision = {};
    decision.scheduler = requested.swappyOptIn ? EAndroidFrameScheduler::Swappy : EAndroidFrameScheduler::Choreographer;
    decision.qualityPixels = inputs.qualityPixels; lastFrame = 0;
    error.clear(); return true;
}
const FAndroidExperimentInputs& FAndroidPerformancePolicy::Experiment() const noexcept { return experiment; }
bool FAndroidPerformancePolicy::AcceptFrame(EAndroidFrameScheduler source, uint64_t timestamp) noexcept {
    if (source != decision.scheduler || !timestamp || timestamp <= lastFrame || decision.manualRecoveryRequired) return false;
    lastFrame = timestamp; return true;
}
FAndroidPerformanceDecision FAndroidPerformancePolicy::OnMemoryPressure(int32_t level) {
    EAndroidPressureAction action = EAndroidPressureAction::None;
    if (level >= 80 || level == 15) action = EAndroidPressureAction::ReleaseScene;
    else if (level >= 40) action = EAndroidPressureAction::RetireSafePages;
    else if (level == 5 || level == 10) action = EAndroidPressureAction::TrimCache;
    if (action != EAndroidPressureAction::None) {
        decision.pressure = std::max(decision.pressure, action); decision.stopPrefetch = true;
        decision.reason = "memory_pressure";
        if (decision.pressure == EAndroidPressureAction::ReleaseScene) decision.manualRecoveryRequired = true;
    }
    return decision;
}
FAndroidPerformanceDecision FAndroidPerformancePolicy::OnThermalStatus(int32_t status) {
    if (status < 0 || status > 6) return decision; // Unavailable is not an observed cold device.
    if (status >= 4) {
        decision.pressure = EAndroidPressureAction::ReleaseScene; decision.stopPrefetch = true;
        decision.manualRecoveryRequired = true; decision.reason = "critical_thermal_pressure";
    } else if (status >= 2 && config.thermalQualityOptIn) {
        const double target = std::min(config.maximumQualityPixels, experiment.qualityPixels * (status >= 3 ? 2.0 : 1.5));
        decision.qualityPixels = std::max(decision.qualityPixels, target);
        decision.qualityChanged = decision.qualityPixels != experiment.qualityPixels;
        decision.stopPrefetch = true; decision.reason = "measured_thermal_quality_policy";
    }
    return decision;
}
FAndroidPerformanceDecision FAndroidPerformancePolicy::RequestManualRecovery() {
    const auto scheduler = decision.scheduler; decision = {};
    decision.scheduler = scheduler; decision.qualityPixels = experiment.qualityPixels;
    decision.reason = "manual_recovery_requires_readmission"; lastFrame = 0; return decision;
}
FAndroidPerformanceDecision FAndroidPerformancePolicy::Snapshot() const { return decision; }
} // namespace MonsterRender::Splat
