// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace MonsterRender::Splat {
/** Charges describe actual retained backing, not overlapping logical counters. */
enum class ESplatBudgetComponent : uint8_t {
    CPUCache, DecodedReady, SourceGPU, StagingGPU, TemporaryGPU,
    Swapchain, Metadata, Builder,
    Java, ///< Generic managed retained backing, excluding other charged components.
    Native, ///< Generic native retained backing, excluding other charged components.
    Count
};
/** Consistent global RAM accounting; GPU charges count on unified-memory devices. */
struct FSplatBudgetSnapshot {
    uint64_t limitBytes = 0; ///< Hard aggregate limit; zero admits no bytes.
    uint64_t reservedBytes = 0; ///< Sum of component reservations.
    uint64_t peakBytes = 0; ///< High-water aggregate charge.
    std::array<uint64_t, static_cast<std::size_t>(ESplatBudgetComponent::Count)> components{}; ///< Disjoint charges.
    bool exclusiveBuild = false; ///< Heavy viewer allocations are prohibited.
    uint64_t admissionSequence=0, admissionRequestedBytes=0, admissionAvailableBytes=0; ///< Last operation at decision time.
    ESplatBudgetComponent admissionComponent=ESplatBudgetComponent::Metadata; ///< Last attempted reservation tag.
    bool admissionGranted=false; ///< Meaningful only when admissionSequence is positive.
    std::string admissionReason; ///< Stable allowlisted decision code; never private detail.
};
/** Immutable facts for one admission operation, captured under the account lock.
 * reservedBytes is the pre-operation charge, including other live readers. */
struct FSplatBudgetAdmissionReceipt {
    uint64_t accountSequence=0; ///< Account decision sequence, including refusals.
    uint64_t requestedBytes=0; ///< Exact bytes requested by this operation.
    uint64_t availableBytes=0; ///< Headroom immediately before this decision.
    uint64_t limitBytes=0; ///< Aggregate policy at this decision.
    uint64_t reservedBytes=0; ///< Aggregate live charge before this operation.
    ESplatBudgetComponent component=ESplatBudgetComponent::Metadata; ///< Attempted charge tag.
    bool granted=false; ///< Whether this exact operation obtained a lease.
    std::string reason; ///< Stable account decision code, never private detail.
};
/** Thread-safe aggregate admission. Reserve peak overlap before allocating, then
 * release only after CPU ownership/GPU completion has actually retired backing.
 * Copies share one accounting lifetime; reservations survive the coordinator. */
class FSplatBudgetCoordinator {
    struct FState;
public:
    /** Move-only lease. Destruction returns its charge exactly once. */
    class FReservation {
    public:
        FReservation() noexcept = default;
        ~FReservation();
        FReservation(FReservation&&) noexcept;
        FReservation& operator=(FReservation&&) noexcept;
        FReservation(const FReservation&) = delete;
        FReservation& operator=(const FReservation&) = delete;
        /** True for an admitted lease, including an admitted zero-byte lease. */
        explicit operator bool() const noexcept;
        /** Actual charged bytes; moved-from/released leases report zero. */
        uint64_t Bytes() const noexcept;
        /** Attribution retained by a live lease. */
        ESplatBudgetComponent Component() const noexcept;
        /** Idempotent explicit retirement after the underlying resource is gone. */
        void Reset() noexcept;
    private:
        friend class FSplatBudgetCoordinator;
        FReservation(std::shared_ptr<FState>, ESplatBudgetComponent, uint64_t) noexcept;
        std::shared_ptr<FState> State; ///< Shared account lifetime.
        uint64_t ReservedBytes = 0; ///< Live charge.
        ESplatBudgetComponent Tag = ESplatBudgetComponent::Metadata; ///< Charge category.
    };
    /** limitBytes must include every charged CPU/GPU/metadata/build component. */
    explicit FSplatBudgetCoordinator(uint64_t limitBytes);
    /** Overflow-safe admission; nullopt leaves accounting unchanged and sets error.
     * Exclusive build admits only Builder/Metadata; zero bytes do not bypass tags. */
    std::optional<FReservation> TryReserve(ESplatBudgetComponent component,
        uint64_t bytes, std::string& error);
    /** Same admission, with operation-local facts; never consult last-admission
     * Snapshot fields to attribute a potentially concurrent decision. */
    std::optional<FReservation> TryReserveWithReceipt(ESplatBudgetComponent component,
        uint64_t bytes, std::string& error, FSplatBudgetAdmissionReceipt& receipt);
    /** Release only this coordinator's lease. Wrong-account/empty returns false. */
    bool Release(FReservation& reservation) noexcept;
    /** Obtain a consistent immutable value snapshot without exposing account locks. */
    FSplatBudgetSnapshot Snapshot() const;
    /** Caller must FIRST await renderer-safe scene release. Admission fails while
     * any CPUCache/DecodedReady/SourceGPU/StagingGPU/TemporaryGPU/Native lease remains.
     * Existing Swapchain/Metadata/Java leases may remain for lightweight task UI.
     * Builder allocation requires exclusive mode. Non-reentrant. */
    bool RequestExclusiveBuild(std::string& error);
    /** End exclusive mode only after build workers and their leases are retired.
     * A live Builder lease keeps exclusive mode active; Snapshot reports that state. */
    void ReleaseExclusiveBuild();
private:
    std::shared_ptr<FState> State; ///< Thread-safe accounting and reservation lifetime.
};
} // namespace MonsterRender::Splat
