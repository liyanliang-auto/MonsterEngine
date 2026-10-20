// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include <filesystem>
#include <memory>

namespace MonsterEngine::VideoTo3D::PreflightDetail {

/** Normalize only absolute drive paths into extended DOS form.
 * Bounds: 255 UTF-16 units/component and 16384 units including the extended prefix,
 * excluding the NUL. Reject UNC/device/relative paths, invalid UTF-16, alternate
 * streams, DOS device names, dot components and ambiguous separators/suffixes.
 */
std::filesystem::path NormalizePreflightPath(const std::filesystem::path& path);

/** Hold read-sharing-only handles for every directory from volume root to leaf.
 * Reparse points, mapped/network drives, SUBST and short-name aliases are rejected.
 * Case-only aliases are accepted. No directories or lock entries are created.
 * Consumers must keep this guard alive throughout all path-based I/O. File readers
 * must separately pin file handles, reject reparse/hardlinks and deny write/delete
 * sharing to establish stable contents. This guard does not freeze child entries.
 */
class DirectoryGuard final {
public:
    /** Validate and pin an existing directory and every ancestor, or throw PreflightFailure. */
    explicit DirectoryGuard(const std::filesystem::path& path);
    /** Close the read-only directory identity guards. */
    ~DirectoryGuard();
    DirectoryGuard(const DirectoryGuard&) = delete;
    DirectoryGuard& operator=(const DirectoryGuard&) = delete;
    /** Return the validated extended absolute path. */
    const std::filesystem::path& Path() const noexcept;
    /** Return the pinned leaf Windows HANDLE as void*, valid for this lifetime. */
    void* NativeHandle() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace MonsterEngine::VideoTo3D::PreflightDetail
