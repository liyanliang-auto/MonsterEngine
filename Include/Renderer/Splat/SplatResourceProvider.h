// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace MonsterRender::Splat {
/** Platform-owned resource access. Logical paths are provider-relative identities,
 * never content URIs or environment-dependent desktop paths. Calls may perform
 * I/O and therefore must not run on an Android UI/render owner for large files. */
class IResourceProvider {
public:
    virtual ~IResourceProvider() = default;
    /** Read a bounded resource. On failure clear bytes and provide a diagnostic;
     * providers enforce their resource-size cap before allocating. */
    virtual bool ReadBytes(const std::string& logicalPath,
        std::vector<uint8_t>& bytes, std::string& error) = 0;
    /** Return an existing validated ordinary file for random-access consumers.
     * Reject escapes/symlinks outside the provider root; APK assets may require
     * prior transactional materialization. Failure clears path and sets error.
     * The provider/caller lease must keep the returned file alive through use. */
    virtual bool ResolveValidatedFile(const std::string& logicalPath,
        std::string& path, std::string& error) = 0;
};

/** One immutable entry in the generated asset inventory. */
struct FResourceIdentity {
    std::string path; ///< Provider-relative logical path, using '/' separators.
    uint64_t bytes = 0; ///< Exact encoded length, checked before allocation.
    std::string sha256; ///< Lowercase SHA256 of the complete encoded bytes.
};
/** Reject absolute paths, traversal, empty components and nonportable separators. */
bool IsResourceLogicalPath(const std::string& path);
/** Strict bounded parser for asset-manifest.json schema 1; rejects duplicate identities.
 * The enclosing APK is the inventory trust boundary; its raw hash identifies a cache version. */
bool ParseResourceInventory(const std::vector<uint8_t>& bytes,
    std::vector<FResourceIdentity>& entries, std::string& error);
/** Validate path/size/hash metadata and reject repeated logical names. */
bool ValidateResourceInventory(const std::vector<FResourceIdentity>& entries,
    uint64_t maximumResourceBytes, std::string& error);
/** Validate a single payload against its immutable inventory identity. */
bool ValidateResourceBytes(const FResourceIdentity& identity,
    const std::vector<uint8_t>& bytes, std::string& error);

/** Root-confined ordinary-file provider. An empty inventory permits desktop files
 * within the explicit root; a supplied inventory additionally requires exact hashes.
 * The root and resources must remain immutable while consumers use returned paths. */
class FFileResourceProvider final : public IResourceProvider {
public:
    /** Explicit root and hard allocation cap; invalid policy fails each operation. */
    explicit FFileResourceProvider(std::string root,
        std::vector<FResourceIdentity> inventory = {}, uint64_t maximumResourceBytes = 64ull << 20);
    /** Read complete bounded bytes, checking an inventory identity when supplied. */
    bool ReadBytes(const std::string&, std::vector<uint8_t>&, std::string& error) override;
    /** Resolve only an existing regular file inside the canonical root. */
    bool ResolveValidatedFile(const std::string&, std::string& path, std::string& error) override;
private:
    std::string root; ///< Explicit ordinary filesystem root.
    std::vector<FResourceIdentity> inventory; ///< Frozen optional identity allowlist.
    uint64_t maximumResourceBytes; ///< Per-read allocation upper bound.
    bool Resolve(const std::string&, std::string&, std::string& error) const;
};
} // namespace MonsterRender::Splat
