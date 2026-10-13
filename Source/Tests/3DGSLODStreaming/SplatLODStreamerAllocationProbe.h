#pragma once
// Executable-only allocation/lifetime observer. Production code and allocators are unchanged.
#include <array>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <new>
#include <thread>
namespace SplatHeapProbe {
struct Allocation{void* pointer=nullptr;size_t bytes=0,payloadBytes=0;unsigned page=~0u;bool armed=false;};
struct State {
    std::mutex mutex;std::array<Allocation,64> allocations{};
    std::atomic<bool> active{false},armed{false},decodeEntered{false},releaseDecode{false},deleteEntered{false},releaseDelete{false},deleting{false},earlyAdmission{false},uncharged{false},timeout{false};
    std::atomic<uint64_t> live{0},peak{0},observedCharge{0},observedLive{0};
    std::atomic<unsigned> oldEncodedCopies{0},replacementAllocations{0};
    size_t encodedBytes=0,decodedBytes=0;MonsterRender::Splat::FSplatPageStreamer* streamer=nullptr;
    void Reset(){std::lock_guard<std::mutex> lock(mutex);for(auto& allocation:allocations)allocation={};active=false;armed=false;decodeEntered=false;releaseDecode=false;deleteEntered=false;releaseDelete=false;deleting=false;earlyAdmission=false;uncharged=false;timeout=false;live=0;peak=0;observedCharge=0;observedLive=0;oldEncodedCopies=0;replacementAllocations=0;}
};
inline State state;
inline thread_local unsigned workerPage=~0u;
inline thread_local bool workerArmed=false;
inline size_t PayloadBytes(size_t bytes){
    // MSVC's large std::vector allocation adds a manual alignment/header region.
    constexpr size_t padding=sizeof(void*)+31;
    for(auto expected:{state.encodedBytes,state.decodedBytes})if(bytes==expected||bytes==expected+padding)return expected;
    return 0;
}
inline void Wait(const std::atomic<bool>& release){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!release.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));if(!release.load())state.timeout=true;}
inline void* Allocate(size_t bytes){
    void* pointer=std::malloc(bytes?bytes:1);if(!pointer)throw std::bad_alloc();auto& s=state;
    const auto payload=PayloadBytes(bytes);
    if(s.active&&workerPage<2&&payload){
        {std::lock_guard<std::mutex> lock(s.mutex);bool stored=false;for(auto& allocation:s.allocations)if(!allocation.pointer){allocation={pointer,bytes,payload,workerPage,workerArmed};stored=true;break;}if(!stored)s.timeout=true;const auto live=s.live.fetch_add(bytes)+bytes;s.peak=std::max(s.peak.load(),live);}
        if(workerArmed&&workerPage==0&&payload==s.encodedBytes)++s.oldEncodedCopies;
        if(workerArmed&&workerPage==1){++s.replacementAllocations;if(s.deleting){s.earlyAdmission=true;auto actual=s.live.load();auto charged=s.streamer->GetMemoryStats().totalReservedBytes;s.observedLive=actual;s.observedCharge=charged;if(actual>charged)s.uncharged=true;}}
        if(workerArmed&&workerPage==0&&payload==s.decodedBytes&&!s.decodeEntered.exchange(true))Wait(s.releaseDecode);
    }
    return pointer;
}
inline void Free(void* pointer) noexcept {
    if(!pointer)return;auto& s=state;Allocation entry;bool found=false;
    {std::lock_guard<std::mutex> lock(s.mutex);for(auto& allocation:s.allocations)if(allocation.pointer==pointer){entry=allocation;found=true;break;}}
    if(found&&entry.armed&&entry.page==0&&entry.payloadBytes==s.encodedBytes&&!s.deleteEntered.exchange(true)){s.deleting=true;Wait(s.releaseDelete);}
    // Free physically before retiring the observer's byte count.
    if(found){std::lock_guard<std::mutex> lock(s.mutex);std::free(pointer);for(auto& allocation:s.allocations)if(allocation.pointer==pointer){allocation={};break;}s.live.fetch_sub(entry.bytes);}else std::free(pointer);
    if(found&&entry.armed&&entry.page==0&&entry.payloadBytes==s.encodedBytes)s.deleting=false;
}
struct Cleanup {~Cleanup(){state.releaseDecode=true;state.releaseDelete=true;state.active=false;}};
}
void* operator new(std::size_t bytes){return SplatHeapProbe::Allocate(bytes);}
void* operator new[](std::size_t bytes){return SplatHeapProbe::Allocate(bytes);}
void operator delete(void* pointer) noexcept {SplatHeapProbe::Free(pointer);}
void operator delete[](void* pointer) noexcept {SplatHeapProbe::Free(pointer);}
void operator delete(void* pointer,std::size_t) noexcept {SplatHeapProbe::Free(pointer);}
void operator delete[](void* pointer,std::size_t) noexcept {SplatHeapProbe::Free(pointer);}
