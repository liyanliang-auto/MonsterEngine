// Copyright Monster Engine. All Rights Reserved.
#include "Platform/Android/AndroidResourceProvider.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#if PLATFORM_ANDROID
#include <android/asset_manager.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace MonsterRender::Splat {
namespace {
#if PLATFORM_ANDROID
struct FFileDescriptor {
    int value = -1;
    ~FFileDescriptor() { if (value >= 0) ::close(value); }
};
// Every directory below the app-private root is a real directory, never a link.
// This protects cache reuse as well as newly-created destination paths.
void MakePrivateDirectories(const std::filesystem::path& root, const std::filesystem::path& relative) {
    auto current = root;
    for (const auto& component : relative) {
        current /= component;
        if (::mkdir(current.c_str(), 0700) != 0 && errno != EEXIST) throw std::runtime_error("Cannot create resource cache directory");
        struct stat status{};
        if (::lstat(current.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) || status.st_uid != ::getuid() || (status.st_mode & 0022))
            throw std::runtime_error("Unsafe resource cache directory");
    }
}
void PublishFile(const std::filesystem::path& target, const std::vector<uint8_t>& bytes) {
    std::string temporary = target.string() + ".tmp-XXXXXX";
    FFileDescriptor output; output.value = ::mkstemp(temporary.data());
    if (output.value < 0) throw std::runtime_error("Cannot create resource cache temporary file");
    try {
        size_t done = 0;
        while (done < bytes.size()) {
            const auto written = ::write(output.value, bytes.data() + done, std::min<size_t>(bytes.size() - done, 1u << 20));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) throw std::runtime_error("Resource cache write failed");
            done += static_cast<size_t>(written);
        }
        if (::fsync(output.value) != 0) throw std::runtime_error("Resource cache flush failed");
        FFileDescriptor directory;
        directory.value = ::open(target.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (directory.value < 0) throw std::runtime_error("Cannot open resource cache parent");
        if (::rename(temporary.c_str(), target.c_str()) != 0) throw std::runtime_error("Resource cache atomic publication failed");
        if (::fsync(directory.value) != 0) throw std::runtime_error("Resource cache directory flush failed");
    } catch (...) { ::unlink(temporary.c_str()); throw; }
}
#endif
}

FAndroidResourceProvider::FAndroidResourceProvider(uint64_t cap) : maximumResourceBytes(cap) {}

bool FAndroidResourceProvider::ReadAsset(const std::string& logical, uint64_t cap, std::vector<uint8_t>& bytes, std::string& error) const {
    bytes.clear();
#if PLATFORM_ANDROID
    try {
        if (!manager || !IsResourceLogicalPath(logical)) throw std::runtime_error("Invalid APK resource request");
        std::unique_ptr<AAsset, decltype(&AAsset_close)> asset(AAssetManager_open(manager, logical.c_str(), AASSET_MODE_STREAMING), AAsset_close);
        if (!asset) throw std::runtime_error("APK resource not found");
        const auto size = AAsset_getLength64(asset.get());
        if (size < 0 || uint64_t(size) > cap || uint64_t(size) > uint64_t(std::numeric_limits<size_t>::max())) throw std::runtime_error("APK resource exceeds size admission");
        std::vector<uint8_t> result(static_cast<size_t>(size)); size_t done = 0;
        while (done < result.size()) {
            const int got = AAsset_read(asset.get(), result.data() + done, std::min<size_t>(result.size() - done, 1u << 20));
            if (got <= 0) throw std::runtime_error("Short APK resource read");
            done += static_cast<size_t>(got);
        }
        uint8_t extra;
        if (AAsset_read(asset.get(), &extra, 1) != 0) throw std::runtime_error("APK resource length changed");
        bytes = std::move(result); error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
#else
    (void)logical; (void)cap;
    error = "Android AssetManager is unavailable on this platform"; return false;
#endif
}

bool FAndroidResourceProvider::Initialize(AAssetManager* assets, const std::string& privateRoot, std::string& error) {
    std::lock_guard lock(mutex);
    // Initialization is single-shot so returned paths and the shared inventory
    // cannot silently acquire a different identity while a renderer is using them.
    if (!inventoryIdentity.empty()) { error = "Resource provider is already initialized"; return false; }
    manager = assets;
#if PLATFORM_ANDROID
    try {
        if (!assets || privateRoot.empty() || !maximumResourceBytes) throw std::runtime_error("Invalid Android resource provider policy");
        std::vector<uint8_t> manifest;
        if (!ReadAsset("asset-manifest.json", 4u << 20, manifest, error)) { manager = nullptr; return false; }
        std::vector<FResourceIdentity> parsed;
        if (!ParseResourceInventory(manifest, parsed, error) || !ValidateResourceInventory(parsed, maximumResourceBytes, error)) { manager = nullptr; return false; }
        const auto identity = FSplatSceneFormat::SHA256(manifest);
        parsed.push_back({"asset-manifest.json", uint64_t(manifest.size()), identity});
        if (!ValidateResourceInventory(parsed, maximumResourceBytes, error)) { manager = nullptr; return false; }
        const auto base = std::filesystem::canonical(std::filesystem::u8path(privateRoot));
        struct stat status{};
        // filesDir can be 0771; only its app-owned primary group may have write access.
        if (::lstat(base.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) || status.st_uid != ::getuid() ||
            (status.st_mode & 0002) || ((status.st_mode & 0020) && status.st_gid != ::getgid()))
            throw std::runtime_error("Resource cache root must be app-private storage");
        const auto relative = std::filesystem::path("monster-resources-v1") / identity;
        MakePrivateDirectories(base, relative);
        cacheRoot = (base / relative).string(); inventory = std::move(parsed); inventoryIdentity = identity;
        error.clear(); return true;
    } catch (const std::exception& exception) {
        manager = nullptr; cacheRoot.clear(); inventory.clear(); inventoryIdentity.clear();
        error = exception.what(); return false;
    }
#else
    (void)privateRoot; manager = nullptr;
    error = "Android AssetManager is unavailable on this platform"; return false;
#endif
}

bool FAndroidResourceProvider::ReadVerified(const std::string& logical, std::vector<uint8_t>& bytes, std::string& error) const {
    bytes.clear();
    const auto found = std::find_if(inventory.begin(), inventory.end(), [&](const auto& item) { return item.path == logical; });
    if (inventoryIdentity.empty() || found == inventory.end()) { error = "Resource absent from initialized APK inventory"; return false; }
    std::vector<uint8_t> result;
    if (!ReadAsset(logical, std::min(maximumResourceBytes, found->bytes), result, error) || !ValidateResourceBytes(*found, result, error)) return false;
    bytes = std::move(result); error.clear(); return true;
}

bool FAndroidResourceProvider::ReadBytes(const std::string& logical, std::vector<uint8_t>& bytes, std::string& error) {
    std::lock_guard lock(mutex);
    try { return ReadVerified(logical, bytes, error); }
    catch (const std::exception& exception) { bytes.clear(); error = exception.what(); return false; }
}

bool FAndroidResourceProvider::ResolveValidatedFile(const std::string& logical, std::string& path, std::string& error) {
    std::lock_guard lock(mutex); path.clear();
#if PLATFORM_ANDROID
    try {
        if (inventoryIdentity.empty() || !IsResourceLogicalPath(logical)) throw std::runtime_error("Invalid resource materialization request");
        const auto target = std::filesystem::path(cacheRoot) / logical;
        MakePrivateDirectories(std::filesystem::path(cacheRoot), std::filesystem::path(logical).parent_path());
        FFileResourceProvider files(cacheRoot, inventory, maximumResourceBytes);
        // Reject a final-component symlink even if it resolves inside the cache.
        struct stat status{};
        const bool regular = ::lstat(target.c_str(), &status) == 0 && S_ISREG(status.st_mode) && status.st_uid == ::getuid();
        if (regular && files.ResolveValidatedFile(logical, path, error)) return true;
        std::vector<uint8_t> bytes;
        if (!ReadVerified(logical, bytes, error)) return false;
        PublishFile(target, bytes);
        // Verify the published file again before exposing an ordinary-file path.
        return files.ResolveValidatedFile(logical, path, error);
    } catch (const std::exception& exception) { path.clear(); error = exception.what(); return false; }
#else
    (void)logical;
    error = "Android resource materialization is unavailable on this platform"; return false;
#endif
}

std::string FAndroidResourceProvider::InventoryIdentity() const {
    std::lock_guard lock(mutex); return inventoryIdentity;
}
} // namespace MonsterRender::Splat
