// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/Streaming/SplatPageStreamer.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace MonsterRender::Splat {
namespace {
using Clock = std::chrono::steady_clock;
constexpr uint64_t TaskCharge = 1024;
constexpr uint64_t CacheCharge = 128;
constexpr size_t MaximumTasks = 65536;
constexpr uint32_t ReadChunk = 256 * 1024;
struct Key {
    uint32_t page;
    uint64_t generation;
    bool operator==(const Key& other) const { return page == other.page && generation == other.generation; }
};
struct KeyHash {
    size_t operator()(const Key& key) const {
        return std::hash<uint64_t>{}(key.generation) ^ (std::hash<uint32_t>{}(key.page) << 1);
    }
};
uint64_t PayloadBytes(const FSplatPageResult& result) {
    return uint64_t(result.bytes.capacity()) + uint64_t(result.decodedGaussians.capacity()) * sizeof(FSplatGaussian);
}
bool UnderDirectory(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto a = root.begin(), b = path.begin();
    for (; a != root.end(); ++a, ++b) {
        if (b == path.end()) return false;
#ifdef _WIN32
        auto x = a->wstring(), y = b->wstring();
        if (_wcsicmp(x.c_str(), y.c_str()) != 0) return false;
#else
        if (*a != *b) return false;
#endif
    }
    return b != path.end();
}
#ifdef _WIN32
struct FileHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~FileHandle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
};
#endif
}

struct FSplatPageStreamer::FImpl {
    enum class State { Pending, Reading, Ready };
    struct Task {
        Key key{};
        State state = State::Pending;
        double priority = 0;
        uint64_t order = 0, completionOrder = 0;
        uint32_t attempt = 0;
        Clock::time_point due{};
        uint64_t reserved = TaskCharge;
        bool obsolete = false;
        FSplatPageResult result;
    };
    struct CacheEntry {
        std::shared_ptr<const std::vector<uint8_t>> bytes;
        uint64_t lastUse = 0;
    };
    struct Lease {
        uint64_t reserved = 0, payload = 0;
        bool terminalFailure = false, released = false;
    };
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::vector<std::thread> workers;
    std::unordered_map<Key, Task, KeyHash> tasks;
    std::unordered_map<Key, Lease, KeyHash> transfers;
    std::unordered_map<uint32_t, CacheEntry> cache;
    FSplatSceneManifest manifest;
    FSplatStreamingConfig config;
    std::filesystem::path directory;
    std::atomic<bool> stopping{true}, hasGeneration{false}, hasCancellation{false};
    std::atomic<uint64_t> generation{0}, cancelledThrough{0};
    bool opened = false, memoryPressure = false;
    uint64_t used = 0, peak = 0, cacheBytes = 0, order = 0;
    FSplatStreamingStats stats;
    std::string lastError;
    std::function<FSplatPageReadFault(uint32_t,uint32_t)> injector;

    bool Cancelled(uint64_t g) const {
        return stopping.load() || (hasGeneration.load() && generation.load() != g) ||
            (hasCancellation.load() && g <= cancelledThrough.load());
    }
    void AddUsed(uint64_t bytes) { used += bytes; peak = (std::max)(peak, used); }
    bool MakeRoom(uint64_t bytes) {
        if (bytes > config.cpuCacheBytes) return false;
        while (used > config.cpuCacheBytes - bytes) {
            auto oldest = cache.end();
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->second.bytes.use_count() != 1) continue;
                if (oldest == cache.end() || it->second.lastUse < oldest->second.lastUse) oldest = it;
            }
            if (oldest == cache.end()) return false;
            const auto n = uint64_t(oldest->second.bytes->capacity());
            cache.erase(oldest);
            used -= n + CacheCharge;
            cacheBytes -= n;
        }
        return true;
    }
    void RemoveCancelledOwned() {
        for (auto it = tasks.begin(); it != tasks.end();) {
            if (Cancelled(it->first.generation) && it->second.state != State::Reading) {
                const auto charge = it->second.reserved;
                it = tasks.erase(it); // Destroy payload before releasing reservation.
                used -= charge;
                ++stats.cancelled;
            } else ++it;
        }
        for (auto it = transfers.begin(); it != transfers.end();) {
            if (it->second.released && Cancelled(it->first.generation)) {
                used -= it->second.reserved;
                it = transfers.erase(it);
            } else ++it;
        }
    }
    bool Read(const FSplatPageInfo& page, uint64_t g, std::vector<uint8_t>& output,
              uint64_t& bytesRead, std::string& error) {
        try {
            // Canonical containment is checked on the worker, including symlinks.
            const auto path = std::filesystem::weakly_canonical(directory / std::filesystem::u8path(page.relativePath));
            if (!UnderDirectory(directory, path)) { error = "Page path escapes scene directory"; return false; }
            output.resize(static_cast<size_t>(page.byteSize));
#ifdef _WIN32
            FileHandle file;
            file.value = CreateFileW(path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (file.value == INVALID_HANDLE_VALUE) {
                error = "Cannot open page, Windows error " + std::to_string(::GetLastError()); return false;
            }
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file.value, &size) || size.QuadPart < 0 ||
                page.offset > uint64_t(size.QuadPart) || page.byteSize > uint64_t(size.QuadPart) - page.offset) {
                error = "Page range exceeds file size"; return false;
            }
            FileHandle event;
            event.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!event.value) { error = "Cannot create read completion event"; return false; }
            for (uint64_t done = 0; done < page.byteSize;) {
                if (Cancelled(g)) { error = "Cancelled"; return false; }
                const DWORD count = DWORD((std::min)(uint64_t(ReadChunk), page.byteSize - done));
                const uint64_t offset = page.offset + done;
                OVERLAPPED operation{};
                operation.Offset = DWORD(offset);
                operation.OffsetHigh = DWORD(offset >> 32);
                operation.hEvent = event.value;
                if (!ResetEvent(event.value)) { error = "Cannot reset read completion event"; return false; }
                DWORD received = 0;
                BOOL immediate = ReadFile(file.value, output.data() + done, count, &received, &operation);
                if (!immediate && ::GetLastError() != ERROR_IO_PENDING) {
                    error = "Page range read failed, Windows error " + std::to_string(::GetLastError()); return false;
                }
                if (!immediate) {
                    for (;;) {
                        const DWORD status = WaitForSingleObject(event.value, 10);
                        if (status == WAIT_OBJECT_0) break;
                        if (status != WAIT_TIMEOUT || Cancelled(g)) {
                            // Local-disk overlapped I/O can be cancelled; observe
                            // cancellation completion before freeing its destination.
                            CancelIoEx(file.value, &operation);
                            GetOverlappedResult(file.value, &operation, &received, TRUE);
                            bytesRead += received;
                            error = status == WAIT_TIMEOUT ? "Cancelled" : "Page I/O wait failed";
                            return false;
                        }
                    }
                }
                if (!GetOverlappedResult(file.value, &operation, &received, FALSE)) {
                    error = "Page I/O completion failed, Windows error " + std::to_string(::GetLastError()); return false;
                }
                bytesRead += received;
                if (received != count) { error = "Truncated page range"; return false; }
                done += received;
            }
#else
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) { error = "Cannot open page"; return false; }
            const auto size = file.tellg();
            if (size < 0 || page.offset > uint64_t(size) || page.byteSize > uint64_t(size) - page.offset ||
                page.offset > uint64_t((std::numeric_limits<std::streamoff>::max)())) {
                error = "Page range exceeds file size"; return false;
            }
            file.seekg(static_cast<std::streamoff>(page.offset));
            for (uint64_t done = 0; done < page.byteSize;) {
                if (Cancelled(g)) { error = "Cancelled"; return false; }
                const auto n = (std::min)(uint64_t(ReadChunk), page.byteSize - done);
                file.read(reinterpret_cast<char*>(output.data() + done), static_cast<std::streamsize>(n));
                const auto received = file.gcount();
                if (received > 0) bytesRead += uint64_t(received);
                if (uint64_t(received) != n || !file) { error = "Truncated page range"; return false; }
                done += n;
            }
#endif
            return true;
        } catch (const std::exception& e) { error = e.what(); return false; }
    }
    bool TakeWork(Key& key, uint32_t& attempt,
                  std::shared_ptr<const std::vector<uint8_t>>& cached,
                  bool& reserveCache,
                  std::function<FSplatPageReadFault(uint32_t,uint32_t)>& fault,
                  std::unique_lock<std::mutex>& lock) {
        while (!stopping.load()) {
            RemoveCancelledOwned();
            auto nextWake = Clock::time_point::max();
            const auto now = Clock::now();
            uint64_t evictable = 0;
            for (const auto& pair : cache) if (pair.second.bytes.use_count() == 1)
                evictable += uint64_t(pair.second.bytes->capacity()) + CacheCharge;
            bool haveCandidate = false;
            auto best = tasks.end();
            // One bounded scan picks the highest-priority page that can fit.
            // A cache hit can be evicted and re-read if retaining it blocks work.
            for (auto it = tasks.begin(); it != tasks.end(); ++it) {
                const auto& task = it->second;
                if (task.state != State::Pending || Cancelled(it->first.generation)) continue;
                if (task.due > now) { nextWake = (std::min)(nextWake, task.due); continue; }
                haveCandidate = true;
                const auto& page = manifest.pages[it->first.page];
                const uint64_t available = config.cpuCacheBytes - used + evictable;
                if (page.byteSize + page.decodedByteSize > available) continue;
                if (best == tasks.end() || task.priority > best->second.priority ||
                    (task.priority == best->second.priority && task.order < best->second.order)) best = it;
            }
            if (best != tasks.end()) {
                const auto candidate = best->first;
                auto& task = best->second;
                const auto& page = manifest.pages[candidate.page];
                auto hit = cache.find(candidate.page);
                const uint64_t payload = page.byteSize + page.decodedByteSize;
                const uint64_t cachePin = hit != cache.end() && hit->second.bytes.use_count() == 1 ?
                    uint64_t(hit->second.bytes->capacity()) + CacheCharge : 0;
                cached = hit != cache.end() && payload <= config.cpuCacheBytes - used + evictable - cachePin ?
                    hit->second.bytes : nullptr; // Pins only when the hit can fit.
                reserveCache = !memoryPressure && !cached && MakeRoom(payload + page.byteSize + CacheCharge);
                const uint64_t extra = payload + (reserveCache ? page.byteSize + CacheCharge : 0);
                if (!reserveCache && !MakeRoom(extra)) {
                    cached.reset();
                    throw std::runtime_error("CPU cache accounting changed inside scheduler lock");
                }
                AddUsed(extra);
                task.reserved += extra;
                task.state = State::Reading;
                key = candidate; attempt = task.attempt; fault = injector;
                if (cached) { ++stats.cacheHits; cache.at(candidate.page).lastUse = ++order; }
                return true;
            }
            // A saturated metadata queue must not deadlock its own payload reads.
            // When no existing result/reader/lease can free memory, reject the
            // least useful pending entry and reserve space for higher priorities.
            bool canReleaseLater = false;
            for (const auto& pair : transfers) if (!pair.second.released) canReleaseLater = true;
            auto lowest = tasks.end();
            size_t pending = 0;
            for (auto it = tasks.begin(); it != tasks.end(); ++it) {
                if (it->second.state != State::Pending) { canReleaseLater = true; continue; }
                ++pending;
                if (lowest == tasks.end() || it->second.priority < lowest->second.priority ||
                    (it->second.priority == lowest->second.priority && it->second.order > lowest->second.order)) lowest = it;
            }
            if (haveCandidate && !canReleaseLater && pending > 1 && lowest != tasks.end()) {
                const auto charge = lowest->second.reserved;
                tasks.erase(lowest);
                used -= charge;
                ++stats.failed;
                lastError = "CPU request metadata displaced by a higher-priority page";
                continue;
            }
            if (haveCandidate && !canReleaseLater && pending == 1 && lowest != tasks.end()) {
                auto& task = lowest->second;
                task.state = State::Ready;
                task.result.pageId = task.key.page;
                task.result.sceneGeneration = task.key.generation;
                task.result.error = "CPU streaming budget retained by terminal failure history";
                task.completionOrder = ++order;
                ++stats.failed; lastError = task.result.error;
                continue;
            }
            if (nextWake == Clock::time_point::max()) wake.wait(lock);
            else wake.wait_until(lock, nextWake);
        }
        return false;
    }
    void RunWorker() {
        for (;;) {
            Key key{}; uint32_t attempt = 0;
            std::shared_ptr<const std::vector<uint8_t>> cached;
            bool reserveCache = false;
            std::function<FSplatPageReadFault(uint32_t,uint32_t)> fault;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (!TakeWork(key, attempt, cached, reserveCache, fault, lock)) return;
            }
            FSplatPageResult result;
            result.pageId = key.page; result.sceneGeneration = key.generation;
            std::shared_ptr<const std::vector<uint8_t>> newCache;
            uint64_t bytesRead = 0;
            bool success = false;
            try {
                const auto decision = fault ? fault(key.page, attempt) : FSplatPageReadFault{};
                if (decision.delayMilliseconds > 0) {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait_for(lock, std::chrono::milliseconds((std::min)(decision.delayMilliseconds, 60000u)),
                                  [this,key] { return Cancelled(key.generation); });
                }
                if (Cancelled(key.generation)) result.error = "Cancelled";
                else if (decision.failRead) result.error = "Injected range-read failure";
                else {
                    if (cached) { result.bytes = *cached; success = true; }
                    else success = Read(manifest.pages[key.page], key.generation, result.bytes, bytesRead, result.error);
                    if (success && !Cancelled(key.generation)) {
                        if (decision.truncateBytes) result.bytes.resize(result.bytes.size() -
                            static_cast<size_t>((std::min)(decision.truncateBytes, uint64_t(result.bytes.size()))));
                        if (decision.corruptChecksum && !result.bytes.empty()) result.bytes.back() ^= 1;
                        success = FSplatSceneFormat::DecodePage(manifest, key.page, result.bytes,
                                                               result.decodedGaussians, result.error);
                        if (success && reserveCache) newCache = std::make_shared<const std::vector<uint8_t>>(result.bytes);
                    } else success = false;
                }
            } catch (const std::exception& e) { success = false; result.error = e.what(); }
            catch (...) { success = false; result.error = "Unexpected page worker failure"; }
            // Release pinned cache references before eviction/reservation updates.
            cached.reset();
            if (!success || Cancelled(key.generation)) {
                std::vector<uint8_t>().swap(result.bytes);
                std::vector<FSplatGaussian>().swap(result.decodedGaussians);
                newCache.reset();
            }
            std::unique_lock<std::mutex> lock(mutex);
            stats.bytesRead += bytesRead;
            auto it = tasks.find(key);
            if (it == tasks.end()) { lastError = "Lost worker task ownership"; continue; }
            auto& task = it->second;
            if (Cancelled(key.generation) || task.obsolete) {
                // Reading payloads are still worker-owned, not in task.result.
                // Destroy them while charged and locked before returning credit.
                std::vector<uint8_t>().swap(result.bytes);
                std::vector<FSplatGaussian>().swap(result.decodedGaussians);
                newCache.reset();
                const auto charge = task.reserved;
                tasks.erase(it);
                used -= charge;
                ++stats.cancelled;
                lock.unlock(); wake.notify_all(); continue;
            }
            if (success) {
                const uint64_t payload = PayloadBytes(result);
                const uint64_t newCacheSize = newCache ? uint64_t(newCache->capacity()) : 0;
                const uint64_t retained = TaskCharge + payload + (newCache ? newCacheSize + CacheCharge : 0);
                if (retained > task.reserved) {
                    success = false;
                    result.error = "Page vector capacities exceed reserved budget";
                    std::vector<uint8_t>().swap(result.bytes);
                    std::vector<FSplatGaussian>().swap(result.decodedGaussians);
                    newCache.reset();
                } else {
                    if (newCache) {
                        cache.emplace(key.page, CacheEntry{newCache, ++order});
                        cacheBytes += newCacheSize;
                    }
                    used -= task.reserved - retained;
                    task.reserved = TaskCharge + payload;
                    task.result = std::move(result);
                    task.state = State::Ready;
                    task.completionOrder = ++order;
                    ++stats.completed;
                    newCache.reset();
                    lock.unlock(); wake.notify_all(); continue;
                }
            }
            used -= task.reserved - TaskCharge;
            task.reserved = TaskCharge;
            if (task.attempt < config.retries) {
                ++task.attempt; ++stats.retries;
                const uint64_t delay = (std::min)(uint64_t(60000),
                    uint64_t(config.retryDelayMilliseconds) << (std::min)(task.attempt - 1, 16u));
                task.due = Clock::now() + std::chrono::milliseconds(delay);
                task.state = State::Pending;
            } else {
                if (result.error.empty()) result.error = "Page load failed";
                if (result.error.capacity() > 512) result.error = result.error.substr(0, 512);
                lastError = result.error;
                task.result = std::move(result);
                task.state = State::Ready;
                task.completionOrder = ++order;
                ++stats.failed;
            }
            lock.unlock(); wake.notify_all();
        }
    }
    void Worker() noexcept {
        try { RunWorker(); }
        catch (...) {
            // An allocation failure in scheduler bookkeeping must not terminate
            // the process. Keep orphaned charges until Shutdown joins all workers.
            {
                std::lock_guard<std::mutex> lock(mutex);
                stopping = true; opened = false;
                try { lastError = "Page scheduler bookkeeping failed"; } catch (...) {}
            }
            wake.notify_all();
        }
    }
};

FSplatPageStreamer::FSplatPageStreamer() : m_impl(std::make_unique<FImpl>()) {}
FSplatPageStreamer::~FSplatPageStreamer() { Shutdown(); }

bool FSplatPageStreamer::EstimateMinimumProgressBytes(const FSplatSceneManifest& manifest,
    const FSplatStreamingConfig& config,uint64_t& bytes,std::string& error) {
    bytes=0;
    if(config.maxConcurrentReads==0||config.maxConcurrentReads>64){error="Invalid progress worker count";return false;}
    if(!FSplatSceneFormat::ValidateManifest(manifest,error))return false;
    uint64_t largestPage=0,largestEncoded=0;
    for(const auto& page:manifest.pages){
        largestPage=(std::max)(largestPage,page.byteSize+page.decodedByteSize+CacheCharge);
        largestEncoded=(std::max)(largestEncoded,page.byteSize+CacheCharge);
    }
    bytes=uint64_t((std::min)(manifest.pages.size(),MaximumTasks))*TaskCharge+
        uint64_t(config.maxConcurrentReads+1)*largestPage+
        uint64_t(config.maxConcurrentReads)*largestEncoded;
    error.clear();return true;
}

bool FSplatPageStreamer::Open(const FSplatSceneManifest& manifest, const std::string& directory,
                             const FSplatStreamingConfig& config, std::string& error) {
    Shutdown();
    auto& s = *m_impl;
    try {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.transfers.empty()) { error = "Release transferred page results before opening a scene"; return false; }
        if (config.cpuCacheBytes < TaskCharge || config.maxConcurrentReads == 0 || config.maxConcurrentReads > 64 ||
            config.retries > 32 || config.retryDelayMilliseconds > 60000) {
            error = "Invalid CPU cache/read/retry configuration"; return false;
        }
        if (!FSplatSceneFormat::ValidateManifest(manifest, error)) return false;
        auto root = std::filesystem::weakly_canonical(std::filesystem::u8path(directory));
        if (!std::filesystem::is_directory(root)) { error = "Scene directory does not exist"; return false; }
        for (const auto& page : manifest.pages) {
            if (page.pinned && (page.byteSize > config.cpuCacheBytes - TaskCharge ||
                page.decodedByteSize > config.cpuCacheBytes - TaskCharge - page.byteSize)) {
                error = "A mandatory coarse page exceeds the CPU streaming budget"; return false;
            }
        }
        s.manifest = manifest; s.directory = std::move(root); s.config = config;
        s.stats = {}; s.lastError.clear(); s.used = s.peak = s.cacheBytes = s.order = 0;
        s.generation = 0; s.hasGeneration = false;
        s.stopping = false; s.opened = true; s.memoryPressure = false;
        s.workers.reserve(config.maxConcurrentReads);
        for (uint32_t i = 0; i < config.maxConcurrentReads; ++i)
            s.workers.emplace_back([&s] { s.Worker(); });
        error.clear(); return true;
    } catch (const std::exception& e) {
        error = e.what();
    }
    Shutdown();
    return false;
}

void FSplatPageStreamer::Request(const std::vector<FSplatPageRequest>& requests) {
    auto& s = *m_impl;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.opened || s.stopping.load()) { s.lastError = "Page streamer is not open"; return; }
        for (const auto& request : requests) {
            ++s.stats.requests;
            if (request.pageId >= s.manifest.pages.size() || !std::isfinite(request.priority)) {
                ++s.stats.failed; s.lastError = "Invalid page request ID or priority"; continue;
            }
            if (s.hasCancellation.load() && request.sceneGeneration <= s.cancelledThrough.load()) {
                ++s.stats.cancelled; continue;
            }
            if (!s.hasGeneration.load() || request.sceneGeneration > s.generation.load()) {
                s.generation = request.sceneGeneration; s.hasGeneration = true;
                s.RemoveCancelledOwned();
            }
            if (request.sceneGeneration != s.generation.load()) { ++s.stats.cancelled; continue; }
            const Key key{request.pageId, request.sceneGeneration};
            if (s.transfers.find(key) != s.transfers.end()) continue;
            auto found = s.tasks.find(key);
            if (found != s.tasks.end()) {
                found->second.priority = (std::max)(found->second.priority, request.priority);
                found->second.obsolete = false;
                continue;
            }
            if (s.tasks.size() + s.transfers.size() >= MaximumTasks || !s.MakeRoom(TaskCharge)) {
                ++s.stats.failed; s.lastError = "CPU request metadata budget exhausted"; continue;
            }
            FImpl::Task task;
            task.key = key; task.priority = request.priority; task.order = ++s.order;
            task.due = Clock::now();
            const auto& page = s.manifest.pages[key.page];
            const uint64_t worst = page.byteSize + page.decodedByteSize;
            if (worst > s.config.cpuCacheBytes - TaskCharge) {
                task.state = FImpl::State::Ready;
                task.result.pageId = key.page; task.result.sceneGeneration = key.generation;
                task.result.error = "Page exceeds CPU streaming budget";
                task.completionOrder = ++s.order;
                ++s.stats.failed; s.lastError = task.result.error;
            }
            s.tasks.emplace(key, std::move(task));
            s.AddUsed(TaskCharge);
        }
    }
    s.wake.notify_all();
}

void FSplatPageStreamer::UpdateRequests(const std::vector<FSplatPageRequest>& requests) {
    auto& s=*m_impl;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        std::unordered_map<Key,double,KeyHash> wanted;
        for(const auto& r:requests)if(r.pageId<s.manifest.pages.size()&&std::isfinite(r.priority))
            wanted[{r.pageId,r.sceneGeneration}]=r.priority;
        for(auto it=s.tasks.begin();it!=s.tasks.end();){
            auto found=wanted.find(it->first);
            if(found!=wanted.end()){it->second.priority=found->second;it->second.obsolete=false;++it;}
            else if(it->second.state==FImpl::State::Reading){it->second.obsolete=true;++it;}
            else {auto charge=it->second.reserved;it=s.tasks.erase(it);s.used-=charge;++s.stats.cancelled;}
        }
    }
    Request(requests);
}

std::vector<FSplatPageResult> FSplatPageStreamer::DrainReady() {
    auto& s = *m_impl;
    std::vector<FSplatPageResult> output;
    std::lock_guard<std::mutex> lock(s.mutex);
    s.RemoveCancelledOwned();
    std::vector<Key> keys;
    for (const auto& pair : s.tasks) if (pair.second.state == FImpl::State::Ready) keys.push_back(pair.first);
    std::sort(keys.begin(), keys.end(), [&s](const Key& a, const Key& b) {
        return s.tasks.at(a).completionOrder < s.tasks.at(b).completionOrder;
    });
    output.reserve(keys.size());
    size_t prepared = 0;
    try {
        for (const auto& key : keys) {
            const auto& task = s.tasks.at(key);
            s.transfers.emplace(key, FImpl::Lease{task.reserved, PayloadBytes(task.result), !task.result.error.empty(), false});
            ++prepared;
        }
    } catch (...) {
        // No payload was moved yet, so failed lease allocation leaves ownership
        // entirely in the ready queue and can be retried on the next drain.
        for (size_t i = 0; i < prepared; ++i) s.transfers.erase(keys[i]);
        s.lastError = "Cannot allocate page transfer bookkeeping";
        return {};
    }
    for (const auto& key : keys) {
        auto it = s.tasks.find(key);
        output.push_back(std::move(it->second.result));
        s.tasks.erase(it);
    }
    return output;
}

bool FSplatPageStreamer::ReleaseTransferred(uint32_t pageId, uint64_t generation) {
    auto& s = *m_impl;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        auto found = s.transfers.find(Key{pageId,generation});
        if (found == s.transfers.end() || found->second.released) return false;
        if (found->second.terminalFailure && !s.Cancelled(generation)) {
            // Keep a bounded metadata tombstone until scene/request generation
            // changes. Per-frame requests cannot restart exhausted I/O retries.
            s.used -= found->second.reserved - TaskCharge;
            found->second.reserved = TaskCharge;
            found->second.payload = 0;
            found->second.released = true;
        } else {
            s.used -= found->second.reserved;
            s.transfers.erase(found);
        }
    }
    s.wake.notify_all();
    return true;
}

void FSplatPageStreamer::CancelGeneration(uint64_t generation) {
    auto& s = *m_impl;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.hasCancellation.load() || generation > s.cancelledThrough.load()) s.cancelledThrough = generation;
        s.hasCancellation = true;
        s.RemoveCancelledOwned();
    }
    s.wake.notify_all();
}

void FSplatPageStreamer::OnMemoryPressure() {
    auto& s = *m_impl;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.memoryPressure = true;
        // A worker's shared cache reference cannot be freed here. It remains
        // charged and is evicted when that reference retires on a later trim.
        for (auto it = s.cache.begin(); it != s.cache.end();) {
            if (it->second.bytes.use_count() != 1) { ++it; continue; }
            const auto bytes = uint64_t(it->second.bytes->capacity());
            it = s.cache.erase(it);
            s.used -= bytes + CacheCharge;
            s.cacheBytes -= bytes;
        }
    }
    s.wake.notify_all();
}

FSplatStreamingStats FSplatPageStreamer::GetStats() const {
    const auto& s = *m_impl;
    std::lock_guard<std::mutex> lock(s.mutex);
    auto stats = s.stats;
    stats.cpuCacheBytes = s.used;
    for (const auto& pair : s.tasks) {
        if (pair.second.state == FImpl::State::Pending) ++stats.pendingReads;
        else if (pair.second.state == FImpl::State::Reading) ++stats.activeReads;
        else stats.queuedBytes += PayloadBytes(pair.second.result);
    }
    return stats;
}

FSplatPageStreamerMemoryStats FSplatPageStreamer::GetMemoryStats() const {
    const auto& s = *m_impl;
    std::lock_guard<std::mutex> lock(s.mutex);
    FSplatPageStreamerMemoryStats stats;
    stats.totalReservedBytes = s.used; stats.peakReservedBytes = s.peak; stats.encodedCacheBytes = s.cacheBytes;
    stats.metadataBytes = uint64_t(s.tasks.size() + s.transfers.size()) * TaskCharge + uint64_t(s.cache.size()) * CacheCharge;
    for (const auto& pair : s.tasks) {
        if (pair.second.state == FImpl::State::Reading) stats.inFlightReservedBytes += pair.second.reserved - TaskCharge;
        else if (pair.second.state == FImpl::State::Ready) stats.readyBytes += PayloadBytes(pair.second.result);
    }
    for (const auto& pair : s.transfers) {
        stats.transferredBytes += pair.second.payload;
        if (!pair.second.released) ++stats.transferredPages;
        else ++stats.terminalFailurePages;
    }
    return stats;
}

std::string FSplatPageStreamer::GetLastError() const {
    const auto& s = *m_impl;
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.lastError;
}

void FSplatPageStreamer::SetFaultInjector(std::function<FSplatPageReadFault(uint32_t,uint32_t)> injector) {
    auto& s = *m_impl;
    std::lock_guard<std::mutex> lock(s.mutex);
    s.injector = std::move(injector);
}

void FSplatPageStreamer::Shutdown() {
    auto& s = *m_impl;
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.stopping = true; s.opened = false;
        if (s.hasGeneration.load()) {
            if (!s.hasCancellation.load() || s.generation.load() > s.cancelledThrough.load())
                s.cancelledThrough = s.generation.load();
            s.hasCancellation = true;
        }
        s.RemoveCancelledOwned();
        workers.swap(s.workers);
    }
    s.wake.notify_all();
    for (auto& worker : workers) if (worker.joinable()) worker.join();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        for (const auto& pair : s.tasks) s.used -= pair.second.reserved;
        s.tasks.clear();
        for (const auto& pair : s.cache) s.used -= uint64_t(pair.second.bytes->capacity()) + CacheCharge;
        s.cache.clear(); s.cacheBytes = 0;
        s.manifest = {}; s.directory.clear();
        // Transferred data belongs to the consumer. Its reservation cannot be
        // released by cancellation or scene shutdown while that memory is alive.
    }
}
} // namespace MonsterRender::Splat
