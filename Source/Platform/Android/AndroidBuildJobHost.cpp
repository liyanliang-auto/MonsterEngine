// Copyright Monster Engine. All Rights Reserved.
#include "Platform/Android/AndroidBuildJobHost.h"
#include "Core/RuntimeLease.h"
#include "Renderer/Splat/LOD/SplatStreamingLODBuilder.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
// Same constant-space SHA256 primitive used by the shared CPU builder. Keeping
// the fallback private avoids adding a public cryptography API in this job.
struct HashState {
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint8_t block[64]{}; size_t used=0; uint64_t total=0;
    static uint32_t R(uint32_t x,int n){return (x>>n)|(x<<(32-n));}
    void Transform(){
        static constexpr uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t(block[4*i])<<24)|(uint32_t(block[4*i+1])<<16)|(uint32_t(block[4*i+2])<<8)|block[4*i+3];
        for(int i=16;i<64;++i)w[i]=w[i-16]+(R(w[i-15],7)^R(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(R(w[i-2],17)^R(w[i-2],19)^(w[i-2]>>10));
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
        for(int i=0;i<64;++i){uint32_t t=z+(R(e,6)^R(e,11)^R(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t u=(R(a,2)^R(a,13)^R(a,22))+((a&b)^(a&c)^(b&c));z=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
    }
    void Update(const uint8_t* p,size_t n){total+=n;while(n){size_t s=std::min(n,64-used);std::memcpy(block+used,p,s);used+=s;p+=s;n-=s;if(used==64){Transform();used=0;}}}
    std::string Finish(){uint64_t bits=total*8;uint8_t one=0x80;Update(&one,1);uint8_t zero=0;while(used!=56)Update(&zero,1);uint8_t tail[8];for(int i=0;i<8;++i)tail[i]=uint8_t(bits>>(56-8*i));Update(tail,8);std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto x:h)s<<std::setw(8)<<x;return s.str();}
};
void Require(bool value, const std::string& error) { if (!value) throw std::runtime_error(error); }
bool HashValid(const std::string& value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
void Poll(const FSplatBuildControl& control) {
    const auto signal = control.Poll();
    Require(signal != ESplatBuildSignal::Cancel, "cancelled");
    Require(signal != ESplatBuildSignal::Pause, "paused");
}
std::string HashBinary(const std::string& path, const FSplatBuildControl& control) {
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    Require(bool(file), "Cannot open actual builder binary; APK libraries require an identity resolver");
    HashState hash;
    std::array<uint8_t,65536> bytes;
    uint64_t total = 0;
    while (file) {
        Poll(control);
        file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        const auto count = file.gcount();
        Require(count >= 0 && total <= (512ull << 20) - uint64_t(count), "Builder binary identity limit exceeded");
        total += uint64_t(count);
        hash.Update(bytes.data(), size_t(count));
    }
    Require(file.eof() && total > 0, "Builder binary identity read failed");
    Poll(control);
    return hash.Finish();
}
struct Job {
    std::mutex mutex;
    bool invalidated = false;
    FAndroidBuildJobRequest request;
    FSplatBudgetCoordinator budget;
    std::shared_ptr<FSplatBuildControl> control = std::make_shared<FSplatBuildControl>();
    FAndroidBuildJobSnapshot snapshot;
    Job(FAndroidBuildJobRequest value, FSplatBudgetCoordinator account)
        : request(std::move(value)), budget(std::move(account)) {}
};
std::mutex registryMutex;
std::map<uint64_t,std::shared_ptr<Job>> registry;
uint64_t nextId = 1;
std::atomic<bool> workerOwned{false};
// IDs are never reused in this process; exhaustion fails instead of wrapping.
constexpr uint64_t BuildGeneration = 0xb11d0001ull;
std::shared_ptr<Job> Find(uint64_t token) {
    if ((token >> 32) != BuildGeneration || uint32_t(token) == 0) return {};
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto found = registry.find(token);
    return found == registry.end() ? nullptr : found->second;
}
struct WorkerGuard {
    ~WorkerGuard() { workerOwned = false; }
};
struct ExclusiveGuard {
    FSplatBudgetCoordinator* budget;
    ~ExclusiveGuard() { budget->ReleaseExclusiveBuild(); }
};
} // namespace

uint64_t FAndroidBuildJobHost::Create(FAndroidBuildJobRequest request,
    FSplatBudgetCoordinator budget, std::string& error) {
    try {
        for (const auto* path : {&request.stableSource,&request.jobDirectory,&request.outputDirectory}) {
            Require(!path->empty() && path->size() <= 4096 && path->find('\0') == std::string::npos,
                "Invalid private build path");
        }
        Require(request.nativeBinaryPath.size() <= 4096 &&
            request.nativeBinaryPath.find('\0') == std::string::npos, "Invalid builder binary path");
        Require(request.resolveBinaryIdentity || !request.nativeBinaryPath.empty(), "Actual binary identity resolver is required");
        Require(FSplatStreamingLODBuilder::ValidateConfig(request.config,error,false),error);
        auto job = std::make_shared<Job>(std::move(request),std::move(budget));
        std::lock_guard<std::mutex> lock(registryMutex);
        Require(registry.size() < 32 && nextId <= UINT32_MAX, "Build handle registry exhausted");
        const uint64_t handle = (BuildGeneration << 32) | nextId++;
        registry.emplace(handle,std::move(job));
        error.clear();
        return handle;
    } catch (const std::exception& failure) { error = failure.what(); return 0; }
}

FAndroidBuildJobResult FAndroidBuildJobHost::Run(uint64_t handle) {
    FAndroidBuildJobResult result;
    const auto job = Find(handle);
    if (!job) { result.code = "invalid_handle"; return result; }
    bool expected = false;
    if (!workerOwned.compare_exchange_strong(expected,true)) { result.code = "busy"; return result; }
    WorkerGuard workerGuard;
    std::shared_ptr<FSplatBuildControl> control;
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        if (job->invalidated) { result.code = "invalid_handle"; return result; }
        if (job->snapshot.running) { result.code = "busy"; return result; }
        control = job->control;
        job->snapshot.running = true;
        job->snapshot.retired = false;
    }
    bool completed = false;
    std::string error;
    bool resourceFailure = false;
    try {
        // Acquire runtime only on the worker. Builder objects and all scratch
        // destruct before reservation/exclusivity/runtime retirement.
        auto runtime = FProcessRuntimeLease::Acquire();
        Poll(*control);
        if (!job->budget.RequestExclusiveBuild(error)) {
            resourceFailure = true;
            throw std::runtime_error(error);
        }
        ExclusiveGuard exclusive{&job->budget};
        constexpr uint64_t HostWorkspace = 128ull << 10;
        Require(job->request.config.workingMemoryBytes <= UINT64_MAX - HostWorkspace, "Build reservation overflow");
        auto reservation = job->budget.TryReserve(ESplatBudgetComponent::Builder,
            job->request.config.workingMemoryBytes + HostWorkspace,error);
        if (!reservation) { resourceFailure = true; throw std::runtime_error(error); }
        auto config = job->request.config;
        std::string actualHash;
        if (job->request.resolveBinaryIdentity)
            Require(job->request.resolveBinaryIdentity(*control,actualHash,error),error);
        else actualHash = HashBinary(job->request.nativeBinaryPath,*control);
        Poll(*control);
        Require(HashValid(actualHash), "Invalid measured builder binary SHA256");
        Require(config.builderHash.empty() || config.builderHash == actualHash,
            "Builder binary changed; retained journal requires explicit compatible restart");
        config.builderHash = actualHash;
        {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->snapshot.builderHash = actualHash;
        }
        FSplatStreamingLODBuilder builder;
        completed = builder.Run(job->request.stableSource,job->request.jobDirectory,
            job->request.outputDirectory,config,*control,[job](const FSplatBuildProgress& progress) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->snapshot.progress = progress;
                job->snapshot.progress.status.resize(std::min<size_t>(4096,job->snapshot.progress.status.size()));
                job->snapshot.progress.error.resize(std::min<size_t>(4096,job->snapshot.progress.error.size()));
            },error);
    } catch (const std::exception& failure) { error = failure.what(); }
    catch (...) { error = "Unexpected native build failure"; }
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        auto& snapshot = job->snapshot;
        snapshot.running = false;
        snapshot.retired = true;
        const auto signal = control->Poll();
        if (completed) {
            snapshot.progress.stage = ESplatBuildStage::Completed;
            snapshot.progress.error.clear();
            result.code = "completed";
        } else {
            snapshot.progress.stage = signal == ESplatBuildSignal::Cancel ? ESplatBuildStage::Cancelled :
                signal == ESplatBuildSignal::Pause ? ESplatBuildStage::Paused :
                resourceFailure ? ESplatBuildStage::WaitingForResources : snapshot.progress.stage;
            if (snapshot.progress.stage != ESplatBuildStage::Paused &&
                snapshot.progress.stage != ESplatBuildStage::Cancelled &&
                snapshot.progress.stage != ESplatBuildStage::WaitingForResources) snapshot.progress.stage = ESplatBuildStage::Failed;
            result.code = snapshot.progress.stage == ESplatBuildStage::Paused ? "paused" :
                snapshot.progress.stage == ESplatBuildStage::Cancelled ? "cancelled" :
                snapshot.progress.stage == ESplatBuildStage::WaitingForResources ? "waiting_for_resources" : "failed";
            snapshot.progress.error = error.substr(0,4096);
            snapshot.progress.status = result.code;
        }
        result.ok = completed;
        result.message = error.substr(0,4096);
        result.snapshot = snapshot;
    }
    return result;
}

bool FAndroidBuildJobHost::Control(uint64_t handle,const std::string& action,std::string& error) {
    const auto job = Find(handle);
    if (!job) { error = "invalid_handle"; return false; }
    std::lock_guard<std::mutex> lock(job->mutex);
    if (job->invalidated) { error = "invalid_handle"; return false; }
    if (action == "pause") job->control->RequestPause();
    else if (action == "cancel") job->control->RequestCancel();
    else if (action == "resume") {
        if (job->snapshot.running) { error = "Wait for current worker retirement before resume"; return false; }
        if (job->snapshot.progress.stage == ESplatBuildStage::Completed) { error = "Job already completed"; return false; }
        job->control = std::make_shared<FSplatBuildControl>();
    } else { error = "Unknown build control"; return false; }
    error.clear();
    return true;
}

bool FAndroidBuildJobHost::Snapshot(uint64_t handle,FAndroidBuildJobSnapshot& snapshot,std::string& error) {
    const auto job = Find(handle);
    if (!job) { error = "invalid_handle"; return false; }
    std::lock_guard<std::mutex> lock(job->mutex);
    if (job->invalidated) { error = "invalid_handle"; return false; }
    snapshot = job->snapshot;
    error.clear();
    return true;
}

bool FAndroidBuildJobHost::Destroy(uint64_t handle) {
    std::shared_ptr<Job> retired;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        auto found = registry.find(handle);
        if (found == registry.end()) return false;
        retired = std::move(found->second);
        registry.erase(found);
    }
    std::lock_guard<std::mutex> lock(retired->mutex);
    retired->invalidated = true;
    retired->control->RequestCancel();
    return true;
}
}

