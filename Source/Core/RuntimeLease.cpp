#include "Core/RuntimeLease.h"
#include "Core/HAL/FMemoryManager.h"
#include "Core/Memory.h"
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace MonsterRender {
namespace {
std::mutex GLeaseMutex;
std::atomic<uint64_t> GLeaseCount{0};
}

FProcessRuntimeLease FProcessRuntimeLease::Acquire() {
    std::lock_guard<std::mutex> Lock(GLeaseMutex);
    if (!GLeaseCount.load(std::memory_order_acquire)) {
        // Publish the pin before initialization so legacy shutdown cannot race it.
        GLeaseCount.store(1, std::memory_order_release);
        try {
            if (!FMemoryManager::Get().Initialize() || !MemorySystem::get().initialize())
                throw std::runtime_error("Process allocator initialization failed");
        } catch (...) {
            GLeaseCount.store(0, std::memory_order_release);
            // The final-owner guards must observe zero while unwinding a
            // partially initialized pair of allocator systems.
            MemorySystem::get().shutdown();
            FMemoryManager::Get().Shutdown();
            throw;
        }
    } else {
        GLeaseCount.fetch_add(1, std::memory_order_acq_rel);
    }
    FProcessRuntimeLease Lease;
    Lease.bAcquired = true;
    return Lease;
}

uint64_t FProcessRuntimeLease::ActiveCount() noexcept { return GLeaseCount.load(std::memory_order_acquire); }
FProcessRuntimeLease::~FProcessRuntimeLease() { Reset(); }
FProcessRuntimeLease::FProcessRuntimeLease(FProcessRuntimeLease&& Other) noexcept
    : bAcquired(std::exchange(Other.bAcquired, false)) {}
FProcessRuntimeLease& FProcessRuntimeLease::operator=(FProcessRuntimeLease&& Other) noexcept {
    if (this != &Other) { Reset(); bAcquired = std::exchange(Other.bAcquired, false); }
    return *this;
}
void FProcessRuntimeLease::Reset() noexcept {
    if (!bAcquired) return;
    std::lock_guard<std::mutex> Lock(GLeaseMutex);
    bAcquired = false;
    if (GLeaseCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        // Drop the published count before shutdown. Both allocator entry
        // points use ActiveCount() as their final-owner guard.
        MemorySystem::get().shutdown();
        FMemoryManager::Get().Shutdown();
    }
}
} // namespace MonsterRender
