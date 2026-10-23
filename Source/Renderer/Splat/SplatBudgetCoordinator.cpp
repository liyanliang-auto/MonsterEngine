// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/SplatBudgetCoordinator.h"
#include <algorithm>
#include <mutex>
#include <utility>

namespace MonsterRender::Splat {
struct FSplatBudgetCoordinator::FState {
    mutable std::mutex mutex;
    FSplatBudgetSnapshot snapshot;
    std::array<uint64_t, static_cast<size_t>(ESplatBudgetComponent::Count)> leases{};
    explicit FState(uint64_t limit) { snapshot.limitBytes = limit; }
};

FSplatBudgetCoordinator::FReservation::FReservation(std::shared_ptr<FState> state,
    ESplatBudgetComponent component, uint64_t bytes) noexcept
    : State(std::move(state)), ReservedBytes(bytes), Tag(component) {}
FSplatBudgetCoordinator::FReservation::~FReservation() { Reset(); }
FSplatBudgetCoordinator::FReservation::FReservation(FReservation&& other) noexcept
    : State(std::move(other.State)), ReservedBytes(std::exchange(other.ReservedBytes, 0)), Tag(other.Tag) {}
FSplatBudgetCoordinator::FReservation& FSplatBudgetCoordinator::FReservation::operator=(FReservation&& other) noexcept {
    if (this != &other) {
        Reset(); State = std::move(other.State); ReservedBytes = std::exchange(other.ReservedBytes, 0); Tag = other.Tag;
    }
    return *this;
}
FSplatBudgetCoordinator::FReservation::operator bool() const noexcept { return bool(State); }
uint64_t FSplatBudgetCoordinator::FReservation::Bytes() const noexcept { return State ? ReservedBytes : 0; }
ESplatBudgetComponent FSplatBudgetCoordinator::FReservation::Component() const noexcept { return Tag; }
void FSplatBudgetCoordinator::FReservation::Reset() noexcept {
    if (!State) return;
    auto state = std::move(State);
    std::lock_guard lock(state->mutex);
    const auto index = static_cast<size_t>(Tag);
    state->snapshot.reservedBytes -= ReservedBytes;
    state->snapshot.components[index] -= ReservedBytes;
    --state->leases[index];
    ReservedBytes = 0;
}
FSplatBudgetCoordinator::FSplatBudgetCoordinator(uint64_t limitBytes) : State(std::make_shared<FState>(limitBytes)) {}
std::optional<FSplatBudgetCoordinator::FReservation> FSplatBudgetCoordinator::TryReserve(
    ESplatBudgetComponent component, uint64_t bytes, std::string& error) {
    FSplatBudgetAdmissionReceipt receipt;
    return TryReserveWithReceipt(component,bytes,error,receipt);
}
std::optional<FSplatBudgetCoordinator::FReservation> FSplatBudgetCoordinator::TryReserveWithReceipt(
    ESplatBudgetComponent component, uint64_t bytes, std::string& error, FSplatBudgetAdmissionReceipt& receipt) {
    const auto index = static_cast<size_t>(component);
    std::lock_guard lock(State->mutex);
    auto& snapshot = State->snapshot;
    ++snapshot.admissionSequence; snapshot.admissionRequestedBytes=bytes;
    snapshot.admissionAvailableBytes=snapshot.limitBytes-snapshot.reservedBytes;
    snapshot.admissionComponent=component; snapshot.admissionGranted=false;
    receipt={snapshot.admissionSequence,bytes,snapshot.admissionAvailableBytes,
        snapshot.limitBytes,snapshot.reservedBytes,component,false,{}};
    const auto refuse=[&](const char* code)->std::optional<FReservation>{
        receipt.reason=snapshot.admissionReason=error=code;return std::nullopt;
    };
    if (index >= static_cast<size_t>(ESplatBudgetComponent::Count)) return refuse("invalid_budget_component");
    if (snapshot.exclusiveBuild && component != ESplatBudgetComponent::Builder && component != ESplatBudgetComponent::Metadata) {
        return refuse("exclusive_build_active");
    }
    if (component == ESplatBudgetComponent::Builder && !snapshot.exclusiveBuild) {
        return refuse("exclusive_build_required");
    }
    if (bytes > snapshot.limitBytes - snapshot.reservedBytes || State->leases[index] == UINT64_MAX) {
        return refuse("aggregate_budget_exceeded");
    }
    // Complete fallible string assignments before changing live accounting.
    receipt.reason="reservation_admitted";snapshot.admissionReason="reservation_admitted";
    snapshot.reservedBytes += bytes; snapshot.components[index] += bytes;
    ++State->leases[index]; snapshot.peakBytes = std::max(snapshot.peakBytes, snapshot.reservedBytes);
    snapshot.admissionGranted=true;receipt.granted=true;
    error.clear(); return FReservation(State, component, bytes);
}
bool FSplatBudgetCoordinator::Release(FReservation& reservation) noexcept {
    if (!reservation.State || reservation.State != State) return false;
    reservation.Reset(); return true;
}
FSplatBudgetSnapshot FSplatBudgetCoordinator::Snapshot() const {
    std::lock_guard lock(State->mutex); return State->snapshot;
}
bool FSplatBudgetCoordinator::RequestExclusiveBuild(std::string& error) {
    std::lock_guard lock(State->mutex);
    if (State->snapshot.exclusiveBuild) { error = "exclusive_build_already_active"; return false; }
    // Count ownership as well as bytes: even a zero-byte admission cannot bypass
    // resource retirement or make a heavyweight owner appear absent.
    for (const auto component : {ESplatBudgetComponent::CPUCache, ESplatBudgetComponent::DecodedReady,
         ESplatBudgetComponent::SourceGPU, ESplatBudgetComponent::StagingGPU,
         ESplatBudgetComponent::TemporaryGPU, ESplatBudgetComponent::Native, ESplatBudgetComponent::Builder}) {
        if (State->leases[static_cast<size_t>(component)]) { error = "viewer_resources_not_retired"; return false; }
    }
    State->snapshot.exclusiveBuild = true; error.clear(); return true;
}
void FSplatBudgetCoordinator::ReleaseExclusiveBuild() {
    std::lock_guard lock(State->mutex);
    if (!State->leases[static_cast<size_t>(ESplatBudgetComponent::Builder)]) State->snapshot.exclusiveBuild = false;
}
} // namespace MonsterRender::Splat
