#pragma once

#include <cstdint>

namespace MonsterRender {

/** Pins both process allocator systems until every renderer/build owner retires. */
class FProcessRuntimeLease {
public:
    /** Acquires initialized allocators; throws on allocation failure. */
    static FProcessRuntimeLease Acquire();
    /** Number of outstanding process owners (diagnostic only). */
    static uint64_t ActiveCount() noexcept;
    FProcessRuntimeLease() noexcept = default;
    ~FProcessRuntimeLease();
    FProcessRuntimeLease(FProcessRuntimeLease&& Other) noexcept;
    FProcessRuntimeLease& operator=(FProcessRuntimeLease&& Other) noexcept;
    FProcessRuntimeLease(const FProcessRuntimeLease&) = delete;
    FProcessRuntimeLease& operator=(const FProcessRuntimeLease&) = delete;
    /** True only after a successful acquisition. */
    bool IsValid() const noexcept { return bAcquired; }
    /** Release after all dependent allocations and worker threads are destroyed. */
    void Reset() noexcept;
private:
    bool bAcquired = false;
};

} // namespace MonsterRender
