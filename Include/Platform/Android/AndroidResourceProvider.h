// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Core/Platform.h"
#include "Renderer/Splat/SplatResourceProvider.h"
#include <mutex>

struct AAssetManager;

namespace MonsterRender::Splat {
/** APK-backed immutable resource inventory with validated private file copies.
 * The caller must retain the Java AssetManager global reference until this
 * provider and all resource operations retire. Returned cache files are never
 * deleted by this provider, and must remain leased by the application while used.
 * Initialize/materialization are worker operations, not Surface handshake work.
 */
class FAndroidResourceProvider final : public IResourceProvider {
public:
    /** Cap each encoded asset allocation; includes builtin source, config and SPIR-V. */
    explicit FAndroidResourceProvider(uint64_t maximumResourceBytes = 64ull << 20);
    /** Read schema-1 APK asset-manifest.json and derive a content-versioned cache root.
     * privateRoot is app-owned files storage, never a shared/external directory.
     * On non-Android platforms returns an explicit unsupported-platform failure. */
    bool Initialize(AAssetManager* manager, const std::string& privateRoot, std::string& error);
    /** Read APK bytes and verify exact size/hash against the initialized inventory. */
    bool ReadBytes(const std::string&, std::vector<uint8_t>&, std::string& error) override;
    /** Validate a cache hit or fsync/atomically publish a verified APK copy. */
    bool ResolveValidatedFile(const std::string&, std::string& path, std::string& error) override;
    /** Raw inventory SHA256 for reproducible diagnostics; empty before initialization. */
    std::string InventoryIdentity() const;
private:
    bool ReadAsset(const std::string&, uint64_t cap, std::vector<uint8_t>&, std::string& error) const;
    bool ReadVerified(const std::string&, std::vector<uint8_t>&, std::string& error) const;
    mutable std::mutex mutex; ///< Protects initialization and cache publication.
    AAssetManager* manager = nullptr; ///< Borrowed from the retained Java AssetManager.
    std::string cacheRoot; ///< Private content-addressed root for this inventory.
    std::string inventoryIdentity; ///< Complete asset-manifest.json SHA256.
    std::vector<FResourceIdentity> inventory; ///< Frozen inventory including the inventory itself.
    uint64_t maximumResourceBytes; ///< Per-asset allocation cap.
};
} // namespace MonsterRender::Splat
