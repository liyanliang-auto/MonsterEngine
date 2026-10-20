#include "VideoTo3D/ReconstructionQueue.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace MonsterEngine::VideoTo3D;
namespace fs = std::filesystem;
static void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void Reject(const std::function<void()>& action, const char* why) {
    try { action(); } catch (const ProjectError&) { return; }
    throw std::runtime_error(why);
}
static std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static void Write(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Require(static_cast<bool>(out), "fixture write failed");
}
static void Put32(std::string& bytes, size_t offset, unsigned value) {
    Require(offset + 4 <= bytes.size(), "fixture offset");
    for (unsigned i = 0; i != 4; ++i) bytes[offset + i] = static_cast<char>(value >> (i * 8));
}
static fs::path Root() {
    return fs::temp_directory_path() / (L"MonsterStudio-tests-" + std::to_wstring(GetCurrentProcessId()));
}
static RunEvent Event(const ProjectSnapshot& snapshot, size_t index, uint64_t sequence, RunEventKind kind) {
    const auto& run = snapshot.runs.at(index);
    return {snapshot.projectId, run.runId, run.stageId, run.generation, sequence, kind};
}
static void SameFileAndMemory(ReconstructionQueue& queue, const std::string& before, uint64_t revision) {
    Require(Read(queue.Directory() / L"project.mstudio") == before, "rejected operation changed bytes");
    Require(queue.Snapshot().revision == revision, "rejected operation changed memory");
}
static int Child(int argc, wchar_t** argv) {
    if (argc < 3) return 80;
    try {
        auto owner = ProjectRepository::Open(fs::path(argv[2]));
        if (std::wstring(argv[1]) == L"--conflict") return 81;
        std::cout << "READY" << std::endl;
        Sleep(30000);
        return 0;
    } catch (const ProjectError& e) {
        if (std::wstring(argv[1]) == L"--conflict" && e.Code() == ProjectErrorCode::OwnershipConflict) return 0;
        return 82;
    }
}
static DWORD Spawn(const fs::path& directory, const wchar_t* mode, PROCESS_INFORMATION& process) {
    wchar_t self[32768]{};
    Require(GetModuleFileNameW(nullptr, self, 32768) != 0, "self path");
    std::wstring command = L"\"" + std::wstring(self) + L"\" " + mode + L" \"" + directory.wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    Require(CreateProcessW(self, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                           nullptr, &startup, &process) != 0, "child launch");
    Require(WaitForSingleObject(process.hProcess, 300) == WAIT_TIMEOUT || std::wstring(mode) == L"--conflict", "owner exited early");
    return process.dwProcessId;
}
int wmain(int argc, wchar_t** argv) {
    if (argc > 1) return Child(argc, argv);
    int pass = 0, fail = 0;
    const fs::path root = Root();
    fs::remove_all(root); fs::create_directories(root);
    auto test = [&](const char* name, const std::function<void(const fs::path&)>& body) {
        const auto dir = root / std::to_wstring(pass + fail);
        try { body(dir); ++pass; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++fail; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    test("unicode_create_reopen_and_empty_location", [](const fs::path& dir) {
        fs::create_directories(dir);
        auto chinese = dir / L"\u9879\u76ee \u7a7a\u683c";
        auto queue = ReconstructionQueue::Create(chinese, "\xe9\x9f\xb3\xe4\xb9\x90 \xe9\xa1\xb9\xe7\x9b\xae");
        const auto id = queue->Snapshot().projectId;
        Require(queue->Snapshot().revision == 0 && queue->Snapshot().runs.empty(), "initial snapshot");
        queue.reset(); queue = ReconstructionQueue::Open(chinese);
        Require(queue->Snapshot().projectId == id && queue->Snapshot().name == "\xe9\x9f\xb3\xe4\xb9\x90 \xe9\xa1\xb9\xe7\x9b\xae", "unicode round trip");
        Require(!Read(chinese / L"project.mstudio").empty(), "no disk snapshot");
    });
    test("reject_nonempty_location_without_overwrite", [](const fs::path& dir) {
        fs::create_directories(dir); Write(dir / L"other.bin", "keep");
        Reject([&] { ProjectRepository::Create(dir, "name"); }, "nonempty location accepted");
        Require(Read(dir / L"other.bin") == "keep" && !fs::exists(dir / L"project.mstudio"), "unrelated input changed");
    });
    test("queue_order_cancel_revision_restart", [](const fs::path& dir) {
        auto q = ReconstructionQueue::Create(dir, "queue");
        const auto a = q->AddPlannedRun("first"); const auto b = q->AddPlannedRun("second"); const auto c = q->AddPlannedRun("third");
        Require(q->Snapshot().revision == 3, "add revisions");
        Require(!q->MoveWaiting(a, -1) && q->Snapshot().revision == 3, "boundary move");
        Require(q->CancelWaiting(b) && !q->CancelWaiting(b), "cancel semantics");
        Require(q->MoveWaiting(c, -1), "move across terminal slot");
        Require(q->Snapshot().runs[0].runId == c && q->Snapshot().runs[1].runId == b && q->Snapshot().runs[2].runId == a, "stable terminal slot");
        const auto revision = q->Snapshot().revision;
        Reject([&] { q->MoveWaiting(c, 0); }, "invalid direction accepted");
        Reject([&] { q->MoveWaiting(b, 1); }, "terminal move accepted");
        Reject([&] { q->CancelWaiting("unknown"); }, "unknown cancel accepted");
        Require(q->Snapshot().revision == revision, "rejected queue change revision");
        q.reset(); q = ReconstructionQueue::Open(dir);
        Require(q->Snapshot().revision == revision && q->Snapshot().runs[0].runId == c &&
                q->Snapshot().runs[1].state == RunState::Canceled && q->Snapshot().runs[2].requestBytes == "first", "queue reopen");
        Require(q->AddPlannedRun("fourth") != b, "canceled ID reused");
    });
    test("request_validation_and_immutable_commit", [](const fs::path& dir) {
        auto q = ReconstructionQueue::Create(dir, "queue"); auto id = q->AddPlannedRun("original");
        const auto before = Read(dir / L"project.mstudio"); const auto rev = q->Snapshot().revision;
        Reject([&] { q->AddPlannedRun(""); }, "empty request accepted");
        Reject([&] { q->AddPlannedRun(std::string(kMaxRequestBytes + 1, 'x')); }, "oversized request accepted");
        Reject([&] { q->AddPlannedRun(std::string("bad\0text", 8)); }, "NUL request accepted");
        SameFileAndMemory(*q, before, rev);
        q.reset(); auto repo = ProjectRepository::Open(dir);
        auto next = repo->Snapshot(); next.revision++; next.runs[0].requestBytes = "changed";
        Reject([&] { repo->Commit(next); }, "immutable request changed");
        next = repo->Snapshot(); next.revision++; next.runs.clear();
        Reject([&] { repo->Commit(next); }, "history erased");
        next = repo->Snapshot(); next.revision += 2;
        Reject([&] { repo->Commit(next); }, "revision gap accepted");
        Require(Read(dir / L"project.mstudio") == before && repo->Snapshot().runs[0].runId == id, "rejected commit changed state");
    });
    test("snapshot_validation_bounds_ids_paths_states", [](const fs::path&) {
        ProjectSnapshot s; s.projectId = ProjectRepository::NewId(); s.name = "valid";
        PlannedRun r; r.runId = ProjectRepository::NewId(); r.stageId = ProjectRepository::NewId(); r.requestBytes = "request";
        s.runs.push_back(r); ProjectRepository::ValidateSnapshot(s);
        auto bad = s; bad.schemaVersion = 2; Reject([&] { ProjectRepository::ValidateSnapshot(bad); }, "unknown schema");
        bad = s; bad.name = ".."; Reject([&] { ProjectRepository::ValidateSnapshot(bad); }, "dot name");
        bad = s; bad.runs[0].stageId = bad.runs[0].runId; Reject([&] { ProjectRepository::ValidateSnapshot(bad); }, "duplicate IDs");
        bad = s; bad.runs[0].generation = 0; Reject([&] { ProjectRepository::ValidateSnapshot(bad); }, "zero generation");
        bad = s; bad.runs[0].state = RunState::Running; Reject([&] { ProjectRepository::ValidateSnapshot(bad); }, "inconsistent state");
        bad = s; bad.runs[0].evidenceReferences = {"../escape"}; Reject([&] { ProjectRepository::ValidateSnapshot(bad); }, "path escape");
        bad = s; bad.runs[0].evidenceReferences = {"C:/escape"}; Reject([&] { ProjectRepository::ValidateSnapshot(bad); }, "drive path");
        bad = s; bad.runs[0].requestBytes = std::string(kMaxRequestBytes + 1, 'x'); Reject([&] { ProjectRepository::ValidateSnapshot(bad); }, "request bound");
        bad = s; bad.runs.resize(kMaxRuns + 1, r); Reject([&] { ProjectRepository::ValidateSnapshot(bad); }, "run bound");
    });
    test("on_disk_corruption_retained_and_lock_released", [](const fs::path& dir) {
        auto q = ReconstructionQueue::Create(dir, "format"); q->AddPlannedRun("request"); q.reset();
        const auto path = dir / L"project.mstudio"; const auto valid = Read(path);
        std::vector<std::string> corrupt;
        auto s = valid; s[0] = 'X'; corrupt.push_back(s);
        s = valid; Put32(s, 8, 2); corrupt.push_back(s);
        s = valid.substr(0, valid.size() - 1); corrupt.push_back(s);
        s = valid + "junk"; corrupt.push_back(s);
        s = valid; Put32(s, 20, 0xffffffff); corrupt.push_back(s);
        s = std::string(kMaxSnapshotBytes + 1, 'x'); corrupt.push_back(s);
        for (const auto& bytes : corrupt) {
            Write(path, bytes);
            Reject([&] { ProjectRepository::Open(dir); }, "corrupt input accepted");
            Require(Read(path) == bytes, "corrupt input bytes changed");
        }
        Write(path, valid); auto opened = ProjectRepository::Open(dir);
        Require(opened->Snapshot().runs.size() == 1, "validation failure retained lease");
    });
    test("owned_stage_cleanup_preserves_arbitrary_remnants", [](const fs::path& dir) {
        auto repo = ProjectRepository::Create(dir, "stage"); const auto id = repo->Snapshot().projectId;
        const auto valid = Read(dir / L"project.mstudio"); repo.reset();
        const auto stageId = ProjectRepository::NewId();
        const auto owned = dir / (L".monsterstudio-" + std::wstring(id.begin(), id.end()) + L"-" +
                                  std::wstring(stageId.begin(), stageId.end()) + L".tmp");
        Write(owned, valid);
        // An unrecognized temporary file is user data even when its contents look like a snapshot.
        Write(dir / L".monsterstudio-unknown.tmp", valid);
        Write(dir / L"ordinary.tmp", "keep");
        auto opened = ProjectRepository::Open(dir);
        Require(!fs::exists(owned) && fs::exists(dir / L".monsterstudio-unknown.tmp") &&
                Read(dir / L"ordinary.tmp") == "keep", "stage cleanup scope");
    });
    test("event_identity_sequence_terminal_guards", [](const fs::path& dir) {
        auto q = ReconstructionQueue::Create(dir, "events"); q->AddPlannedRun("request");
        auto event = Event(q->Snapshot(), 0, 1, RunEventKind::Started);
        const auto bytes = Read(dir / L"project.mstudio"); const auto revision = q->Snapshot().revision;
        auto bad = event; bad.projectId = ProjectRepository::NewId(); Reject([&] { q->ApplyEvent(bad); }, "foreign project");
        bad = event; bad.runId = ProjectRepository::NewId(); Reject([&] { q->ApplyEvent(bad); }, "foreign run");
        bad = event; bad.stageId = ProjectRepository::NewId(); Reject([&] { q->ApplyEvent(bad); }, "foreign stage");
        bad = event; bad.generation++; Reject([&] { q->ApplyEvent(bad); }, "foreign generation");
        bad = event; bad.sequence = 0; Reject([&] { q->ApplyEvent(bad); }, "zero sequence");
        bad = event; bad.kind = RunEventKind::Canceled; Reject([&] { q->ApplyEvent(bad); }, "illegal edge");
        SameFileAndMemory(*q, bytes, revision);
        q->ApplyEvent(event);
        Require(q->Snapshot().runs[0].state == RunState::Running && q->Snapshot().runs[0].requestBytes == "request", "start event");
        Reject([&] { q->ApplyEvent(event); }, "duplicate event");
        q->ApplyEvent(Event(q->Snapshot(), 0, 5, RunEventKind::PauseRequested));
        Reject([&] { q->ApplyEvent(Event(q->Snapshot(), 0, 4, RunEventKind::Failed)); }, "out of order event");
        q->ApplyEvent(Event(q->Snapshot(), 0, 6, RunEventKind::CancelRequested));
        q->ApplyEvent(Event(q->Snapshot(), 0, 7, RunEventKind::Canceled));
        Require(q->Snapshot().runs[0].lastEventSequence == 7, "sequence not persisted");
        Reject([&] { q->ApplyEvent(Event(q->Snapshot(), 0, 8, RunEventKind::Started)); }, "terminal restart");
    });
    test("restart_interrupts_active_once", [](const fs::path& dir) {
        auto q = ReconstructionQueue::Create(dir, "restart");
        for (int i = 0; i < 4; ++i) q->AddPlannedRun("request" + std::to_string(i));
        for (size_t i = 0; i < 3; ++i) q->ApplyEvent(Event(q->Snapshot(), i, 1, RunEventKind::Started));
        q->ApplyEvent(Event(q->Snapshot(), 1, 2, RunEventKind::PauseRequested));
        q->ApplyEvent(Event(q->Snapshot(), 2, 2, RunEventKind::CancelRequested));
        const auto before = q->Snapshot(); q.reset(); q = ReconstructionQueue::Open(dir);
        Require(q->Snapshot().revision == before.revision + 1, "recovery not one transaction");
        for (size_t i = 0; i < 3; ++i) Require(q->Snapshot().runs[i].state == RunState::Interrupted &&
            q->Snapshot().runs[i].lastEventSequence == before.runs[i].lastEventSequence &&
            q->Snapshot().runs[i].requestBytes == before.runs[i].requestBytes, "recovery identity changed");
        Require(q->Snapshot().runs[3].state == RunState::Pending, "pending changed on recovery");
        const auto revision = q->Snapshot().revision; q.reset(); q = ReconstructionQueue::Open(dir);
        Require(q->Snapshot().revision == revision, "recovery repeated");
    });
    test("real_replace_denial_retains_snapshot", [](const fs::path& dir) {
        auto q = ReconstructionQueue::Create(dir, "denial"); const auto before = Read(dir / L"project.mstudio");
        HANDLE hold = CreateFileW((dir / L"project.mstudio").c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(hold != INVALID_HANDLE_VALUE, "fixture file handle");
        Reject([&] { q->AddPlannedRun("must fail"); }, "replace unexpectedly succeeded");
        CloseHandle(hold);
        SameFileAndMemory(*q, before, 0);
        Require(q->AddPlannedRun("after release").size() == 36, "retry after denial");
    });
    test("managed_file_reparse_rejected_without_outside_write", [](const fs::path& dir) {
        auto repo = ProjectRepository::Create(dir, "reparse"); repo.reset();
        const auto primary = dir / L"project.mstudio";
        const auto outside = dir.parent_path() / (dir.filename().wstring() + L"-outside.mstudio");
        const auto original = Read(primary);
        Write(outside, original);
        fs::remove(primary);
        const DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        Require(CreateSymbolicLinkW(primary.c_str(), outside.c_str(), flags) != 0,
                "host does not permit test symlink creation");
        Reject([&] { ProjectRepository::Open(dir); }, "managed file reparse accepted");
        Require(Read(outside) == original, "outside file changed");
        fs::remove(primary);
        Write(primary, original);
    });
    test("failed_recovery_commit_keeps_active_bytes", [](const fs::path& dir) {
        auto q = ReconstructionQueue::Create(dir, "recovery");
        q->AddPlannedRun("planned"); q->ApplyEvent(Event(q->Snapshot(), 0, 1, RunEventKind::Started));
        q.reset();
        const auto primary = dir / L"project.mstudio";
        const auto before = Read(primary);
        HANDLE hold = CreateFileW(primary.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(hold != INVALID_HANDLE_VALUE, "recovery fixture handle");
        Reject([&] { ReconstructionQueue::Open(dir); }, "recovery claimed success despite replace denial");
        CloseHandle(hold);
        Require(Read(primary) == before, "failed recovery changed active bytes");
        q = ReconstructionQueue::Open(dir);
        Require(q->Snapshot().runs[0].state == RunState::Interrupted, "recovery retry failed");
    });
    test("physical_alias_lease_and_crash_release", [](const fs::path& dir) {
        auto repo = ProjectRepository::Create(dir, "lease"); repo.reset();
        PROCESS_INFORMATION owner{}; Spawn(dir, L"--hold", owner);
        Sleep(200);
        const auto before = Read(dir / L"project.mstudio");
        Reject([&] { ProjectRepository::Open(dir / L"."); }, "physical alias acquired concurrent lease");
        PROCESS_INFORMATION contender{}; Spawn(dir, L"--conflict", contender);
        Require(WaitForSingleObject(contender.hProcess, 5000) == WAIT_OBJECT_0, "contender did not exit");
        DWORD exitCode = 1; GetExitCodeProcess(contender.hProcess, &exitCode);
        CloseHandle(contender.hThread); CloseHandle(contender.hProcess);
        Require(exitCode == 0 && Read(dir / L"project.mstudio") == before, "two-process contention changed data");
        TerminateProcess(owner.hProcess, 91); WaitForSingleObject(owner.hProcess, 5000);
        CloseHandle(owner.hThread); CloseHandle(owner.hProcess);
        repo = ProjectRepository::Open(dir);
        Require(repo->Snapshot().revision == 0, "crash did not release lease");
    });
    fs::remove_all(root);
    std::cout << "TOTAL " << pass + fail << " PASSED " << pass << " FAILED " << fail << '\n';
    return fail ? 1 : 0;
}
