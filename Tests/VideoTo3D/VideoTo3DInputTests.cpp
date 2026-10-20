#include "VideoTo3D/ProjectInput.h"
#include <Windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <atomic>
#include <memory>

using namespace MonsterEngine::VideoTo3D;
namespace fs = std::filesystem;

static void Need(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void Write(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Need(static_cast<bool>(out), "fixture write failed");
}
static std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    Need(static_cast<bool>(in), "fixture read failed");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static std::string Sha256(const std::string& bytes) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    UCHAR digest[32]{};
    Need(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "SHA provider");
    const auto close = [&] { if (hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); };
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) { close(); throw std::runtime_error("SHA calculation"); }
    close();
    static constexpr char hex[] = "0123456789abcdef";
    std::string out; out.reserve(64);
    for (const auto b : digest) { out += hex[b >> 4]; out += hex[b & 15]; }
    return out;
}
static std::string FileIdentity(const fs::path& path) {
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    Need(handle != INVALID_HANDLE_VALUE, "open file identity");
    BY_HANDLE_FILE_INFORMATION info{};
    const bool ok = GetFileInformationByHandle(handle, &info) != 0;
    CloseHandle(handle);
    Need(ok, "read file identity");
    return std::to_string(info.dwVolumeSerialNumber) + ":" + std::to_string(info.nFileIndexHigh) + ":" +
           std::to_string(info.nFileIndexLow);
}
static fs::path Owned(const ProjectRepository& project, const InputFile& file) {
    return project.Directory() / fs::path(std::u8string(file.relativePath.begin(), file.relativePath.end()));
}
static void Error(InputErrorCode code, const std::function<void()>& action) {
    try { action(); }
    catch (const InputError& error) {
        if (error.Code() == code) return;
        throw std::runtime_error("wrong typed input error: " + std::to_string(static_cast<int>(error.Code())));
    }
    throw std::runtime_error("expected typed input error");
}
static fs::path Root() {
    return fs::temp_directory_path() / (L"MonsterInput-tests-" + std::to_wstring(GetCurrentProcessId()));
}
static std::atomic<unsigned> g_checkpoint{0};
static fs::path g_blockSignal;
static std::string g_originalSource;
static std::string g_mutatedSource;
extern "C" void MonsterVideoTo3DInputTestCheckpoint(unsigned checkpoint, const wchar_t* sourcePath) {
    g_checkpoint = checkpoint;
    if (checkpoint == 1 && !g_originalSource.empty()) {
        Need(sourcePath != nullptr, "mutation checkpoint missing source");
        Need(Read(sourcePath) == g_originalSource, "source changed before checkpoint");
        Write(sourcePath, g_mutatedSource);
        Need(Read(sourcePath) == g_mutatedSource, "mutation did not change real bytes");
    }
    if (checkpoint == 2 && !g_blockSignal.empty()) {
        Write(g_blockSignal, std::to_string(GetCurrentProcessId()));
        for (;;) Sleep(1000);
    }
}
static std::unique_ptr<ProjectRepository> Project(const fs::path& dir) {
    return ProjectRepository::Create(dir / L"project", "input tests");
}
static void Same(const InputManifest& a, const InputManifest& b) {
    Need(a.projectId == b.projectId && a.inputId == b.inputId && a.revision == b.revision && a.kind == b.kind, "identity changed");
    Need(a.files.size() == b.files.size(), "file count changed");
    for (size_t i = 0; i < a.files.size(); ++i)
        Need(a.files[i].relativePath == b.files[i].relativePath &&
             a.files[i].byteCount == b.files[i].byteCount && a.files[i].sha256 == b.files[i].sha256,
             "file identity changed");
}
static void Put32(std::string& bytes, size_t offset, unsigned value) {
    Need(offset + 4 <= bytes.size(), "fixture offset");
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<char>(value >> (8 * i));
}
static int Child(int argc, wchar_t** argv) {
    if (argc != 3 && argc != 4) return 80;
    try {
        if (std::wstring(argv[1]) == L"--import-block") {
            if (argc != 4) return 80;
            g_blockSignal = fs::path(argv[2]).parent_path() / L"checkpoint2.pid";
            auto repository = ProjectRepository::Open(argv[2]);
            ProjectInput::Import(*repository, InputKind::Video, argv[3]);
            return 83; // Reaching publication would invalidate the interruption test.
        }
        if (std::wstring(argv[1]) == L"--inspect-prior") {
            const auto parent = fs::path(argv[2]).parent_path();
            auto repository = ProjectRepository::Open(argv[2]);
            const auto inspected = ProjectInput::Inspect(*repository);
            if (inspected.files.size() != 1 ||
                Read(repository->Directory() / L"input.mstudio") != Read(parent / L"expected-manifest.bin") ||
                Read(Owned(*repository, inspected.files[0])) != Read(parent / L"expected-media.bin")) return 84;
            Write(parent / L"reopen-proof.txt", std::to_string(GetCurrentProcessId()) + " " + inspected.inputId);
            return 0;
        }
        if (std::wstring(argv[1]) == L"--create-project") {
            auto repository = ProjectRepository::Create(argv[2], "CLI fixture");
            return 0;
        }
        auto repository = ProjectRepository::Open(argv[2]);
        if (std::wstring(argv[1]) == L"--conflict") return 81;
        std::cout << "READY" << std::endl;
        Sleep(30000);
        return 0;
    } catch (const ProjectError& error) {
        if (std::wstring(argv[1]) == L"--conflict" && error.Code() == ProjectErrorCode::OwnershipConflict) return 0;
        return 82;
    }
}
struct RunningChild {
    HANDLE process{nullptr};
    DWORD pid{0};
    ~RunningChild() { if (process) { TerminateProcess(process, 90); WaitForSingleObject(process, 5000); CloseHandle(process); } }
};
static RunningChild SpawnImport(const fs::path& project, const fs::path& source) {
    wchar_t self[MAX_PATH]{};
    Need(GetModuleFileNameW(nullptr, self, MAX_PATH) != 0, "test exe path");
    std::wstring command = L"\"" + std::wstring(self) + L"\" --import-block \"" + project.wstring() + L"\" \"" + source.wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    Need(CreateProcessW(self, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info) != 0,
         "import child launch failed");
    CloseHandle(info.hThread);
    return {info.hProcess, info.dwProcessId};
}
static DWORD Spawn(const fs::path& directory, const wchar_t* mode) {
    wchar_t self[MAX_PATH]{};
    Need(GetModuleFileNameW(nullptr, self, MAX_PATH) != 0, "test exe path");
    std::wstring command = L"\"" + std::wstring(self) + L"\" " + mode + L" \"" + directory.wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    Need(CreateProcessW(self, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != 0,
         "child launch failed");
    CloseHandle(process.hThread);
    Need(WaitForSingleObject(process.hProcess, 3000) == WAIT_OBJECT_0, "child timed out");
    DWORD exitCode = 99;
    Need(GetExitCodeProcess(process.hProcess, &exitCode) != 0, "child exit code");
    CloseHandle(process.hProcess);
    return exitCode;
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) return Child(argc, argv);
    int passed = 0, failed = 0;
    const auto root = Root();
    fs::remove_all(root); fs::create_directories(root);
    auto test = [&](const char* name, const std::function<void(const fs::path&)>& body) {
        const auto dir = root / std::to_wstring(passed + failed);
        fs::create_directories(dir);
        try { body(dir); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cout << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("no_input_is_typed", [](const fs::path& dir) {
        auto project = Project(dir);
        Error(InputErrorCode::NoInput, [&] { ProjectInput::Inspect(*project); });
    });
    test("video_owned_reopen_source_deleted_and_v1_unchanged", [](const fs::path& dir) {
        auto project = Project(dir);
        const auto before = Read(project->Directory() / L"project.mstudio");
        const auto source = dir / L"\u89c6\u9891 sample.mp4";
        Write(source, std::string("video bytes\0\1\2", 14));
        const auto sourceBytes = Read(source);
        const auto imported = ProjectInput::Import(*project, InputKind::Video, source);
        Need(imported.kind == InputKind::Video && imported.files.size() == 1 && imported.revision == 1, "video identity");
        Need(imported.files[0].byteCount == sourceBytes.size() && imported.files[0].sha256.size() == 64, "video digest and size");
        Need(Read(Owned(*project, imported.files[0])) == sourceBytes, "owned video bytes");
        Need(Read(source) == sourceBytes && Read(project->Directory() / L"project.mstudio") == before, "source or v1 changed");
        project.reset(); fs::remove(source);
        project = ProjectRepository::Open(dir / L"project");
        Same(imported, ProjectInput::Inspect(*project));
    });
    test("photos_owned_reopen_and_replace_preserves_old_generation", [](const fs::path& dir) {
        auto project = Project(dir);
        const auto before = Read(project->Directory() / L"project.mstudio");
        const auto photos = dir / L"\u7167\u7247 set"; fs::create_directories(photos);
        Write(photos / L"A.JPG", "first"); Write(photos / L"\u4e2d\u6587.png", std::string("second\0byte", 11));
        const auto initial = ProjectInput::Import(*project, InputKind::Photos, photos);
        Need(initial.kind == InputKind::Photos && initial.files.size() == 2, "photo identities");
        fs::remove_all(photos);
        Same(initial, ProjectInput::Inspect(*project));
        const auto video = dir / L"later.mov"; Write(video, "replacement");
        const auto next = ProjectInput::Import(*project, InputKind::Video, video);
        Need(next.revision == initial.revision + 1 && next.inputId != initial.inputId, "replacement revision");
        Same(next, ProjectInput::Inspect(*project));
        for (const auto& item : initial.files) Need(fs::exists(Owned(*project, item)), "old generation deleted");
        Need(Read(project->Directory() / L"project.mstudio") == before, "v1 modified by replacement");
        project.reset(); project = ProjectRepository::Open(dir / L"project"); Same(next, ProjectInput::Inspect(*project));
    });
    test("invalid_sources_and_failed_replace_retain_current", [](const fs::path& dir) {
        auto project = Project(dir);
        const auto old = dir / L"old.mp4"; Write(old, "old");
        const auto current = ProjectInput::Import(*project, InputKind::Video, old);
        const auto manifestBytes = Read(project->Directory() / L"input.mstudio");
        Error(InputErrorCode::InvalidSource, [&] { ProjectInput::Import(*project, InputKind::Video, dir / L"missing.mp4"); });
        const auto unsupported = dir / L"new.txt"; Write(unsupported, "no");
        Error(InputErrorCode::InvalidSource, [&] { ProjectInput::Import(*project, InputKind::Video, unsupported); });
        const auto empty = dir / L"empty"; fs::create_directories(empty);
        Error(InputErrorCode::InvalidSource, [&] { ProjectInput::Import(*project, InputKind::Photos, empty); });
        Write(empty / L"bad.mp4", "no");
        Error(InputErrorCode::InvalidSource, [&] { ProjectInput::Import(*project, InputKind::Photos, empty); });
        Need(Read(project->Directory() / L"input.mstudio") == manifestBytes, "failed import changed current manifest");
        Same(current, ProjectInput::Inspect(*project));
    });
    test("nested_photo_entry_and_photo_count_limit_rejected", [](const fs::path& dir) {
        auto project = Project(dir);
        const auto photos = dir / L"photos"; fs::create_directories(photos);
        Write(photos / L"one.jpg", "one");
        fs::create_directories(photos / L"nested");
        Error(InputErrorCode::InvalidSource, [&] { ProjectInput::Import(*project, InputKind::Photos, photos); });
        fs::remove_all(photos / L"nested");
        for (int i = 1; i <= 4096; ++i) Write(photos / (std::to_wstring(i) + L".jpg"), "");
        Error(InputErrorCode::LimitExceeded, [&] { ProjectInput::Import(*project, InputKind::Photos, photos); });
        Error(InputErrorCode::NoInput, [&] { ProjectInput::Inspect(*project); });
    });
    test("alias_hardlink_and_reparse_rejected", [](const fs::path& dir) {
        auto project = Project(dir);
        const auto inside = project->Directory() / L"inside.mp4"; Write(inside, "inside");
        Error(InputErrorCode::UnsafePath, [&] { ProjectInput::Import(*project, InputKind::Video, inside); });
        const auto outside = dir / L"alias.mp4";
        Need(CreateHardLinkW(outside.c_str(), inside.c_str(), nullptr) != 0, "hardlink fixture");
        Error(InputErrorCode::UnsafePath, [&] { ProjectInput::Import(*project, InputKind::Video, outside); });
        const auto link = dir / L"link.mp4";
        Need(CreateSymbolicLinkW(link.c_str(), inside.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0,
             "symlink fixture creation failed");
        Error(InputErrorCode::UnsafePath, [&] { ProjectInput::Import(*project, InputKind::Video, link); });
    });
    test("publication_denial_retains_previous_input_and_orphans_are_ignored", [](const fs::path& dir) {
        auto project = Project(dir);
        const auto first = dir / L"first.mp4"; Write(first, "first");
        const auto current = ProjectInput::Import(*project, InputKind::Video, first);
        const auto manifestPath = project->Directory() / L"input.mstudio";
        const auto bytes = Read(manifestPath);
        Write(project->Directory() / L".monsterstudio-input-stale.tmp", "orphan");
        fs::create_directories(project->Directory() / L"media" / L"unreferenced");
        const auto second = dir / L"second.mov"; Write(second, "second");
        Need(SetFileAttributesW(manifestPath.c_str(), FILE_ATTRIBUTE_READONLY) != 0, "readonly fixture");
        try {
            Error(InputErrorCode::IoFailure, [&] { ProjectInput::Import(*project, InputKind::Video, second); });
        } catch (...) {
            SetFileAttributesW(manifestPath.c_str(), FILE_ATTRIBUTE_NORMAL);
            throw;
        }
        Need(SetFileAttributesW(manifestPath.c_str(), FILE_ATTRIBUTE_NORMAL) != 0, "restore attributes");
        Need(Read(manifestPath) == bytes, "publication failure changed current manifest");
        Same(current, ProjectInput::Inspect(*project));
        project.reset(); project = ProjectRepository::Open(dir / L"project");
        Same(current, ProjectInput::Inspect(*project));
    });
    test("owned_media_reparse_is_rejected_on_inspect", [](const fs::path& dir) {
        auto project = Project(dir);
        const auto source = dir / L"safe.mp4"; Write(source, "safe");
        ProjectInput::Import(*project, InputKind::Video, source);
        const auto media = project->Directory() / L"media";
        const auto outside = dir / L"moved-media";
        fs::rename(media, outside);
        Need(CreateSymbolicLinkW(media.c_str(), outside.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0,
             "directory symlink fixture creation failed");
        Error(InputErrorCode::UnsafePath, [&] { ProjectInput::Inspect(*project); });
        fs::remove(media);
        fs::rename(outside, media);
        Need(ProjectInput::Inspect(*project).files.size() == 1, "restored media not inspectable");
    });
    test("malformed_manifest_and_missing_or_modified_media_rejected", [](const fs::path& dir) {
        auto project = Project(dir);
        const auto source = dir / L"v.mp4"; Write(source, "untouched source");
        const auto current = ProjectInput::Import(*project, InputKind::Video, source);
        const auto path = project->Directory() / L"input.mstudio";
        const auto original = Read(path);
        auto wrong = original; wrong[8] = 2; Write(path, wrong);
        Error(InputErrorCode::CorruptManifest, [&] { ProjectInput::Inspect(*project); });
        wrong = original; wrong.push_back('x'); Write(path, wrong);
        Error(InputErrorCode::CorruptManifest, [&] { ProjectInput::Inspect(*project); });
        wrong = original; Put32(wrong, 104, 4097); Write(path, wrong);
        Error(InputErrorCode::CorruptManifest, [&] { ProjectInput::Inspect(*project); });
        wrong = original; Put32(wrong, 108, 5000); Write(path, wrong);
        Error(InputErrorCode::CorruptManifest, [&] { ProjectInput::Inspect(*project); });
        wrong = original; wrong[112] = '.'; Write(path, wrong);
        Error(InputErrorCode::CorruptManifest, [&] { ProjectInput::Inspect(*project); });
        Write(path, original.substr(0, original.size() - 1));
        Error(InputErrorCode::CorruptManifest, [&] { ProjectInput::Inspect(*project); });
        wrong = original; wrong[16] = wrong[16] == 'a' ? 'b' : 'a'; Write(path, wrong);
        Error(InputErrorCode::CorruptManifest, [&] { ProjectInput::Inspect(*project); });
        Write(path, original);
        const auto owned = Owned(*project, current.files[0]);
        Write(owned, "changed");
        Error(InputErrorCode::CorruptManifest, [&] { ProjectInput::Inspect(*project); });
        fs::remove(owned);
        Error(InputErrorCode::CorruptManifest, [&] { ProjectInput::Inspect(*project); });
    });
    test("exclusive_lease_denies_second_process", [](const fs::path& dir) {
        auto project = Project(dir);
        Need(Spawn(project->Directory(), L"--conflict") == 0, "second process acquired lease");
    });
    test("source_write_is_denied_while_copy_is_in_progress", [](const fs::path& dir) {
        auto project = Project(dir);
        const auto source = dir / L"moving.mp4";
        Write(source, std::string(32 * 1024 * 1024, 'q'));
        std::atomic<int> attempted{0};
        std::thread mutation([&] {
            const auto media = project->Directory() / L"media";
            for (int tries = 0; tries < 10000 && attempted == 0; ++tries) {
                std::error_code error;
                if (fs::exists(media, error)) {
                    for (const auto& generation : fs::directory_iterator(media, error)) {
                        if (!error && fs::exists(generation.path() / source.filename(), error)) {
                            HANDLE handle = CreateFileW(source.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                        nullptr, OPEN_EXISTING, 0, nullptr);
                            if (handle == INVALID_HANDLE_VALUE) attempted = GetLastError() == ERROR_SHARING_VIOLATION ? 1 : 3;
                            else { attempted = 2; CloseHandle(handle); }
                            return;
                        }
                    }
                }
                Sleep(1);
            }
        });
        try {
            const auto manifest = ProjectInput::Import(*project, InputKind::Video, source);
            mutation.join();
            Need(attempted == 1, "source write was not rejected during owned copy");
            Need(manifest.files[0].byteCount == 32ULL * 1024 * 1024, "source bytes changed");
        } catch (...) { mutation.join(); throw; }
    });
    test("changed_source_after_copy_is_rejected_and_prior_input_reopens", [](const fs::path& dir) {
        auto project = Project(dir);
        const auto old = dir / L"old.mp4"; Write(old, "old validated bytes");
        const auto prior = ProjectInput::Import(*project, InputKind::Video, old);
        const auto manifestBefore = Read(project->Directory() / L"input.mstudio");
        const auto mediaBefore = Read(Owned(*project, prior.files[0]));
        Write(dir / L"expected-manifest.bin", manifestBefore);
        Write(dir / L"expected-media.bin", mediaBefore);
        const auto candidate = dir / L"candidate.mp4";
        g_originalSource = "original replacement bytes";
        g_mutatedSource = "CHANGED! replacement bytes";
        Write(candidate, g_originalSource);
        const auto originalSha = Sha256(g_originalSource);
        const auto originalFileId = FileIdentity(candidate);
        g_checkpoint = 0;
        try { Error(InputErrorCode::SourceChanged, [&] { ProjectInput::Import(*project, InputKind::Video, candidate); }); }
        catch (...) { g_originalSource.clear(); g_mutatedSource.clear(); throw; }
        Need(g_checkpoint == 1 && Read(candidate) == g_mutatedSource, "mutation checkpoint not proven");
        const auto changedSha = Sha256(Read(candidate));
        Need(originalSha != changedSha, "source SHA unchanged");
        std::cout << "MUTATION checkpoint=1 source_before_sha256=" << originalSha
                  << " source_after_sha256=" << changedSha << " file_id_before=" << originalFileId
                  << " file_id_after=" << FileIdentity(candidate)
                  << " result=SourceChanged prior=" << prior.inputId
                  << " manifest_sha256=" << Sha256(manifestBefore) << " media_sha256=" << Sha256(mediaBefore) << '\n';
        g_originalSource.clear(); g_mutatedSource.clear();
        Need(Read(project->Directory() / L"input.mstudio") == manifestBefore &&
             Read(Owned(*project, prior.files[0])) == mediaBefore, "prior input changed after source mutation");
        project.reset();
        Need(Spawn(dir / L"project", L"--inspect-prior") == 0, "distinct process failed prior input verification");
        const auto proof = Read(dir / L"reopen-proof.txt");
        Need(!proof.empty(), "reopen proof missing");
        std::cout << "MUTATION_REOPEN " << proof << '\n';
    });
    test("terminated_importer_before_publication_retains_prior_and_releases_lease", [](const fs::path& dir) {
        auto project = Project(dir);
        const auto old = dir / L"old.mp4"; Write(old, "prior generation bytes");
        const auto prior = ProjectInput::Import(*project, InputKind::Video, old);
        const auto manifestBefore = Read(project->Directory() / L"input.mstudio");
        const auto mediaBefore = Read(Owned(*project, prior.files[0]));
        Write(dir / L"expected-manifest.bin", manifestBefore);
        Write(dir / L"expected-media.bin", mediaBefore);
        const auto replacement = dir / L"replacement.mp4"; Write(replacement, "replacement generation bytes");
        project.reset();
        auto importer = SpawnImport(dir / L"project", replacement);
        const auto signal = dir / L"checkpoint2.pid";
        for (int i = 0; i < 500 && !fs::exists(signal); ++i) {
            Need(WaitForSingleObject(importer.process, 0) == WAIT_TIMEOUT, "importer exited before staging checkpoint");
            Sleep(10);
        }
        Need(fs::exists(signal), "importer never reached prepublication checkpoint");
        Need(std::stoul(Read(signal)) == importer.pid, "checkpoint PID mismatch");
        Need(WaitForSingleObject(importer.process, 0) == WAIT_TIMEOUT, "importer not alive at checkpoint");
        Need(TerminateProcess(importer.process, 91) != 0, "importer termination failed");
        Need(WaitForSingleObject(importer.process, 5000) == WAIT_OBJECT_0, "terminated importer did not exit");
        DWORD exitCode = 0;
        Need(GetExitCodeProcess(importer.process, &exitCode) != 0 && exitCode == 91, "importer termination exit mismatch");
        Need(Spawn(dir / L"project", L"--inspect-prior") == 0, "distinct process could not reopen prior input and lease");
        const auto proof = Read(dir / L"reopen-proof.txt");
        Need(std::stoul(proof) != importer.pid, "reopen reused importer PID");
        project = ProjectRepository::Open(dir / L"project");
        Same(prior, ProjectInput::Inspect(*project));
        Need(Read(project->Directory() / L"input.mstudio") == manifestBefore &&
             Read(Owned(*project, prior.files[0])) == mediaBefore, "prior input bytes changed after importer death");
        std::cout << "INTERRUPTION importer_pid=" << importer.pid << " exit=" << exitCode
                  << " reopen=" << proof << " checkpoint=2 prior=" << prior.inputId
                  << " manifest_sha256_before=" << Sha256(manifestBefore)
                  << " manifest_sha256_after=" << Sha256(Read(project->Directory() / L"input.mstudio"))
                  << " media_sha256_before=" << Sha256(mediaBefore)
                  << " media_sha256_after=" << Sha256(Read(Owned(*project, prior.files[0]))) << '\n';
    });
    fs::remove_all(root);
    std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
