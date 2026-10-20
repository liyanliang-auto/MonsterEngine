// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include "VideoTo3D/StudioProject.h"

#include <filesystem>
#include <memory>
#include <string>

namespace MonsterEngine::VideoTo3D {

/** Single-thread-owned project store with a lifetime exclusive directory lease. */
class ProjectRepository final {
public:
    /** Create a v1 project in an empty/new directory and acquire its lifetime lease. */
    static std::unique_ptr<ProjectRepository> Create(const std::filesystem::path& directory,
                                                     const std::string& name);
    /** Validate and open an existing project without changing active run states. */
    static std::unique_ptr<ProjectRepository> Open(const std::filesystem::path& directory);
    /** Check all schema, identity, UTF-8, path, collection and encoded-size bounds. */
    static void ValidateSnapshot(const ProjectSnapshot& snapshot);
    /** Decode and validate bounded snapshot bytes without file I/O or project ownership. */
    static ProjectSnapshot DecodeSnapshotBytes(const std::string& bytes);
    /** Generate a canonical lowercase UUID text identifier. */
    static std::string NewId();

    /** Release the process-owned project lease. */
    ~ProjectRepository();
    ProjectRepository(const ProjectRepository&) = delete;
    ProjectRepository& operator=(const ProjectRepository&) = delete;

    /** Current durable snapshot; callers must serialize access to this repository. */
    const ProjectSnapshot& Snapshot() const noexcept;
    /** Physical project directory held by this repository. */
    const std::filesystem::path& Directory() const noexcept;
    /** Atomically publish a validated next revision, retaining existing run identities. */
    void Commit(const ProjectSnapshot& next);

private:
    struct Impl;
    explicit ProjectRepository(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace MonsterEngine::VideoTo3D
