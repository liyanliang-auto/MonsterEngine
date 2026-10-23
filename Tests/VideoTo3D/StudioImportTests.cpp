#include "StudioWindow.h"
#include "resource.h"
#include <Aclapi.h>
#include <atomic>
#include <chrono>
#include <commctrl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <objbase.h>
#include <stdexcept>
using namespace MonsterEngine::VideoTo3D;
namespace fs = std::filesystem;
static void Require(bool b, const char *s)
{
    if (!b)
        throw std::runtime_error(s);
}
static std::string Read(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    Require(bool(f), "read fixture");
    return {std::istreambuf_iterator<char>(f), {}};
}
static void Write(const fs::path &p, const std::string &s)
{
    std::ofstream f(p, std::ios::binary);
    f.write(s.data(), s.size());
    Require(bool(f), "write fixture");
}
static std::wstring Text(HWND h)
{
    std::wstring s(GetWindowTextLengthW(h) + 1, L'\0');
    s.resize(GetWindowTextW(h, s.data(), int(s.size())));
    return s;
}
static std::wstring Label(UINT id)
{
    wchar_t b[2048]{};
    LoadStringW(GetModuleHandleW(nullptr), id, b, 2048);
    return b;
}
static void Pump()
{
    MSG m{};
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE))
    {
        if (m.message != WM_QUIT)
        {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
}
static void Until(const std::function<bool()> &fn)
{
    const auto end = GetTickCount64() + 15000;
    while (!fn())
    {
        Pump();
        Require(GetTickCount64() < end, "message loop timeout");
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
    Pump();
}
struct StudioWindowTestAccess
{
    using Op = StudioWindow::Operation;
    static void Send(StudioWindow &w, Op op, const fs::path &p = {}, const std::string &t = {})
    {
        w.Enqueue({op, ++w.generation_, p, t});
    }
    static bool Busy(StudioWindow &w)
    {
        return w.busy_;
    }
    static auto Snapshot(StudioWindow &w)
    {
        return w.snapshot_;
    }
    static auto Input(StudioWindow &w)
    {
        return w.input_;
    }
    static auto Error(StudioWindow &w)
    {
        return w.inputError_;
    }
    static void Hook(StudioWindow &w, std::function<void(UINT)> fn)
    {
        w.workerStageHook_ = std::move(fn);
    }
    static void Stale(StudioWindow &w)
    {
        StudioWindow::Completion c{};
        c.generation = w.generation_ - 1;
        c.snapshot.projectId = "stale";
        c.stage = IDS_VIDEO_ERROR;
        {
            std::lock_guard l(w.mutex_);
            w.completions_.push_back(c);
        }
        w.OnCompletion();
    }
    static bool Done(StudioWindow &w)
    {
        std::lock_guard l(w.mutex_);
        return w.workerDone_;
    }
};
using A = StudioWindowTestAccess;
struct Gate
{
    UINT stage;
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr),
           release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<DWORD> thread{0};
    explicit Gate(UINT s) : stage(s)
    {
        Require(entered && release, "create gate");
    }
    void operator()(UINT s)
    {
        if (s == stage)
        {
            thread = GetCurrentThreadId();
            SetEvent(entered);
            Require(WaitForSingleObject(release, 15000) == WAIT_OBJECT_0, "gate release timeout");
        }
    }
    void Wait()
    {
        Until([&] { return WaitForSingleObject(entered, 0) == WAIT_OBJECT_0; });
        Require(thread != GetCurrentThreadId(), "work ran on UI thread");
    }
    void Release()
    {
        SetEvent(release);
    }
    ~Gate()
    {
        Release();
        CloseHandle(entered);
        CloseHandle(release);
    }
};
struct Window
{
    StudioWindow w{GetModuleHandleW(nullptr)};
    HWND handle{};
    Window()
    {
        Require(w.Create(), "create real window");
        handle = w.Handle();
    }
    void Wait()
    {
        Until([&] { return !A::Busy(w); });
    }
    void Create(const fs::path &p)
    {
        A::Send(w, A::Op::Create, p, "Studio fixture");
        Wait();
        Require(!A::Snapshot(w).projectId.empty(), "create failed");
    }
    void Open(const fs::path &p)
    {
        A::Send(w, A::Op::Open, p);
        Wait();
        Require(!A::Snapshot(w).projectId.empty(), "open failed");
    }
    void Import(const fs::path &p)
    {
        A::Send(w, A::Op::ImportVideo, p);
        Wait();
    }
    void Close()
    {
        if (IsWindow(handle))
        {
            SendMessageW(handle, WM_CLOSE, 0, 0);
            Until([&] { return !IsWindow(handle); });
        }
        Require(A::Done(w), "worker did not stop before destruction");
    }
    ~Window()
    {
        if (IsWindow(handle))
        {
            SendMessageW(handle, WM_CLOSE, 0, 0);
            for (int i = 0; i < 400 && IsWindow(handle); ++i)
            {
                Pump();
                Sleep(5);
            }
        }
    }
};
static fs::path Seed(const fs::path &d)
{
    fs::create_directories(d);
    Write(d / L"source.mov", "retained-video-source-0123456789");
    auto q = ReconstructionQueue::Create(d / L"project", "Seed");
    q->AddPlannedRun("retained plan");
    q->ImportVideo(d / L"source.mov");
    return d / L"project";
}
static void Preserved(Window &v, const fs::path &p, const std::string &project, const std::string &manifest,
                      const std::string &id)
{
    Require(Read(p / L"project.mstudio") == project, "project/queue bytes changed");
    Require(Read(p / L"input.mstudio") == manifest, "published manifest changed");
    Require(A::Input(v.w) && A::Input(v.w)->inputId == id, "displayed previous input lost");
    Require(Text(GetDlgItem(v.handle, IDC_STATUS)).find(Label(IDS_STATUS_ERROR)) == 0,
            "error status missing");
}
class DeniedWrite
{
    fs::path p_;
    PSECURITY_DESCRIPTOR original_{};
    PACL replacement_{};

  public:
    explicit DeniedWrite(const fs::path &p) : p_(p)
    {
        PACL acl{};
        Require(GetNamedSecurityInfoW(const_cast<wchar_t *>(p.c_str()), SE_FILE_OBJECT,
                                      DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr,
                                      &original_) == ERROR_SUCCESS,
                "read ACL");
        SID_IDENTIFIER_AUTHORITY a = SECURITY_WORLD_SID_AUTHORITY;
        PSID sid{};
        Require(AllocateAndInitializeSid(&a, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &sid) != 0, "SID");
        EXPLICIT_ACCESSW e{};
        e.grfAccessPermissions = FILE_ADD_FILE;
        e.grfAccessMode = DENY_ACCESS;
        e.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        e.Trustee.ptstrName = static_cast<LPWSTR>(sid);
        auto code = SetEntriesInAclW(1, &e, acl, &replacement_);
        FreeSid(sid);
        Require(code == ERROR_SUCCESS, "compose ACL");
        Require(SetNamedSecurityInfoW(const_cast<wchar_t *>(p.c_str()), SE_FILE_OBJECT,
                                      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                      nullptr, nullptr, replacement_, nullptr) == ERROR_SUCCESS,
                "deny write");
    }
    ~DeniedWrite()
    {
        PACL acl{};
        BOOL present{}, def{};
        GetSecurityDescriptorDacl(original_, &present, &acl, &def);
        SECURITY_DESCRIPTOR_CONTROL c{};
        DWORD revision{};
        GetSecurityDescriptorControl(original_, &c, &revision);
        if (SetNamedSecurityInfoW(const_cast<wchar_t *>(p_.c_str()), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION |
                                      ((c & SE_DACL_PROTECTED) ? PROTECTED_DACL_SECURITY_INFORMATION
                                                               : UNPROTECTED_DACL_SECURITY_INFORMATION),
                                  nullptr, nullptr, acl, nullptr) != ERROR_SUCCESS)
            std::terminate();
        LocalFree(original_);
        LocalFree(replacement_);
    }
};
static BOOL CALLBACK CancelOwned(HWND h, LPARAM owner)
{
    wchar_t cls[80]{};
    GetClassNameW(h, cls, 80);
    if (GetWindow(h, GW_OWNER) == HWND(owner) && std::wstring(cls) == L"#32770")
        PostMessageW(h, WM_COMMAND, IDCANCEL, 0);
    return TRUE;
}
static void CALLBACK CancelTimer(HWND owner, UINT, UINT_PTR, DWORD)
{
    EnumThreadWindows(GetCurrentThreadId(), CancelOwned, LPARAM(owner));
}
int wmain(int argc, wchar_t **argv)
{
    std::cout << std::unitbuf;
    try
    {
        Require(argc == 3 && std::wstring(argv[1]) == L"--runtime",
                "required --runtime new absolute directory");
        const fs::path root = argv[2];
        Require(root.is_absolute() && !fs::exists(root), "runtime must be new absolute directory");
        fs::create_directories(root);
        Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM init");
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        int passed = 0, failed = 0;
        auto test = [&](const char *name, const std::function<void(const fs::path &)> &fn) {
            const auto d = root / std::to_wstring(passed + failed);
            fs::create_directory(d);
            try
            {
                fn(d);
                ++passed;
                std::cout << "PASS " << name << '\n';
            }
            catch (const std::exception &e)
            {
                ++failed;
                std::cout << "FAIL " << name << ": " << e.what() << '\n';
            }
            Pump();
        };
        test("no_project_controls_idle_close", [](const fs::path &) {
            Window v;
            Require(!IsWindowEnabled(GetDlgItem(v.handle, IDC_IMPORT_VIDEO)),
                    "import enabled without project");
            Require(Text(GetDlgItem(v.handle, IDC_INPUT_INFO)) == Label(IDS_INPUT_NONE), "no input text");
            v.Close();
        });
        test("create_plan_move_cancel_reopen", [](const fs::path &d) {
            const auto p = d / L"project";
            std::string id;
            {
                Window v;
                v.Create(p);
                Require(IsWindowEnabled(GetDlgItem(v.handle, IDC_IMPORT_VIDEO)), "import disabled");
                for (auto text : {"one", "two"})
                {
                    A::Send(v.w, A::Op::Add, {}, text);
                    v.Wait();
                }
                auto s = A::Snapshot(v.w);
                id = s.projectId;
                Require(s.runs.size() == 2, "add plans");
                A::Send(v.w, A::Op::MoveUp, {}, s.runs[1].runId);
                v.Wait();
                Require(A::Snapshot(v.w).runs[0].requestBytes == "two", "move plan");
                A::Send(v.w, A::Op::Cancel, {}, s.runs[1].runId);
                v.Wait();
                Require(A::Snapshot(v.w).runs[0].state == RunState::Canceled, "cancel plan");
                v.Close();
            }
            {
                Window v;
                v.Open(p);
                Require(A::Snapshot(v.w).projectId == id && A::Snapshot(v.w).runs.size() == 2,
                        "reopen plans");
                Require(!A::Input(v.w) && A::Error(v.w).empty(), "absent input misreported");
                v.Close();
            }
        });
        test("lego_import_identity_owned_path_reopen", [](const fs::path &d) {
            wchar_t source[32768]{};
            Require(GetEnvironmentVariableW(L"MONSTER_STUDIO_TEST_VIDEO", source, 32768) > 0,
                    "set local LEGO source environment");
            const fs::path p = d / L"project";
            std::string id;
            {
                Window v;
                v.Create(p);
                const auto bytes = Read(p / L"project.mstudio");
                v.Import(source);
                auto in = A::Input(v.w);
                Require(in && in->kind == InputKind::Video && in->files.size() == 1, "video input missing");
                Require(in->files[0].byteCount == 33577480 &&
                            in->files[0].sha256 ==
                                "04935f00cf94b5399e2584034bf04c0d235e25faa2de442fe6d296cc64eeab04",
                        "LEGO identity mismatch");
                id = in->inputId;
                Require(fs::exists(p / fs::u8path(in->files[0].relativePath)), "owned file missing");
                Require(Read(p / L"project.mstudio") == bytes, "import changed project bytes");
                Require(Text(GetDlgItem(v.handle, IDC_STATUS)) == Label(IDS_IMPORT_SUCCESS),
                        "import success missing");
                const auto ui = Text(GetDlgItem(v.handle, IDC_INPUT_INFO));
                Require(ui.find(L"33577480") != std::wstring::npos &&
                            ui.find(L"04935f00") != std::wstring::npos &&
                            ui.find(fs::path(p).make_preferred().wstring()) != std::wstring::npos,
                        "owned identity not displayed");
                v.Close();
            }
            {
                Window v;
                v.Open(p);
                Require(A::Input(v.w) && A::Input(v.w)->inputId == id, "reopen identity lost");
                v.Close();
            }
        });
        test("invalid_sources_preserve_previous_input", [](const fs::path &d) {
            auto p = Seed(d);
            Window v;
            v.Open(p);
            auto pr = Read(p / L"project.mstudio"), m = Read(p / L"input.mstudio"),
                 id = A::Input(v.w)->inputId;
            Write(d / L"zero.mov", "");
            Write(d / L"unsupported.txt", "unsupported");
            for (auto src : {d / L"missing.mov", d / L"zero.mov", d / L"unsupported.txt", d})
            {
                v.Import(src);
                Preserved(v, p, pr, m, id);
            }
            v.Close();
        });
        test("real_preflight_acl_failure_preserves", [](const fs::path &d) {
            auto p = Seed(d);
            Window v;
            v.Open(p);
            auto pr = Read(p / L"project.mstudio"), m = Read(p / L"input.mstudio"),
                 id = A::Input(v.w)->inputId;
            {
                DeniedWrite deny(p);
                v.Import(d / L"source.mov");
                Preserved(v, p, pr, m, id);
                Require(Text(GetDlgItem(v.handle, IDC_STATUS)).find(L"预检") != std::wstring::npos,
                        "preflight error detail missing");
            }
            v.Close();
        });
        test("source_removed_after_preflight_import_failure", [](const fs::path &d) {
            auto p = Seed(d);
            Gate gate(IDS_IMPORT_COPYING);
            Window v;
            v.Open(p);
            auto pr = Read(p / L"project.mstudio"), m = Read(p / L"input.mstudio"),
                 id = A::Input(v.w)->inputId;
            A::Hook(v.w, [&](UINT s) { gate(s); });
            A::Send(v.w, A::Op::ImportVideo, d / L"source.mov");
            gate.Wait();
            fs::remove(d / L"source.mov");
            gate.Release();
            v.Wait();
            Preserved(v, p, pr, m, id);
            v.Close();
        });
        test("worker_exception_becomes_error", [](const fs::path &d) {
            auto p = Seed(d);
            Window v;
            v.Open(p);
            auto pr = Read(p / L"project.mstudio"), m = Read(p / L"input.mstudio"),
                 id = A::Input(v.w)->inputId;
            A::Hook(v.w, [](UINT s) {
                if (s == IDS_IMPORT_PREFLIGHT)
                    throw std::runtime_error("controlled worker exception");
            });
            v.Import(d / L"source.mov");
            Preserved(v, p, pr, m, id);
            Require(Text(GetDlgItem(v.handle, IDC_STATUS)).find(L"controlled worker exception") !=
                        std::wstring::npos,
                    "exception detail lost");
            v.Close();
        });
        test("inspection_is_background_and_corruption_retains_project", [](const fs::path &d) {
            auto p = Seed(d);
            auto repository = ProjectRepository::Open(p);
            auto in = ProjectInput::Inspect(*repository);
            repository.reset();
            Write(p / fs::u8path(in.files[0].relativePath), "corruption");
            Gate gate(IDS_INPUT_INSPECTING);
            Window v;
            A::Hook(v.w, [&](UINT s) { gate(s); });
            A::Send(v.w, A::Op::Open, p);
            gate.Wait();
            Require(IsWindow(v.handle) && A::Busy(v.w), "inspection not pending");
            SendMessageW(v.handle, WM_SIZE, 0, 0);
            gate.Release();
            v.Wait();
            Require(!A::Snapshot(v.w).projectId.empty() && !A::Input(v.w) && !A::Error(v.w).empty(),
                    "corruption not distinguished from no input");
            Require(Text(GetDlgItem(v.handle, IDC_INPUT_INFO)).find(Label(IDS_INPUT_ERROR)) == 0,
                    "inspection error missing");
            v.Close();
        });
        test("responsive_busy_duplicate_switch_and_stale_completion", [](const fs::path &d) {
            auto p = Seed(d);
            Gate gate(IDS_IMPORT_PREFLIGHT);
            Window v;
            v.Open(p);
            auto project = A::Snapshot(v.w).projectId;
            A::Hook(v.w, [&](UINT s) { gate(s); });
            A::Send(v.w, A::Op::ImportVideo, d / L"source.mov");
            gate.Wait();
            Require(A::Busy(v.w), "not busy");
            for (int c : {IDC_CREATE, IDC_OPEN, IDC_IMPORT_VIDEO, IDC_ADD})
                Require(!IsWindowEnabled(GetDlgItem(v.handle, c)), "busy control enabled");
            auto start = GetTickCount64();
            SendMessageW(v.handle, WM_SIZE, 0, 0);
            SendMessageW(v.handle, WM_COMMAND, MAKEWPARAM(IDC_OPEN, BN_CLICKED), 0);
            SendMessageW(v.handle, WM_COMMAND, MAKEWPARAM(IDC_IMPORT_VIDEO, BN_CLICKED), 0);
            Require(GetTickCount64() - start < 500, "UI blocked by worker");
            A::Stale(v.w);
            Require(A::Snapshot(v.w).projectId == project && A::Busy(v.w), "stale completion applied");
            gate.Release();
            v.Wait();
            Require(A::Input(v.w) && A::Input(v.w)->revision == 2 && A::Snapshot(v.w).projectId == project,
                    "duplicate import or switch happened");
            v.Close();
        });
        for (UINT stage : {IDS_IMPORT_PREFLIGHT, IDS_IMPORT_COPYING})
            test(stage == IDS_IMPORT_PREFLIGHT ? "close_during_preflight_joins_and_keeps_commit"
                                               : "close_during_copy_gate_joins_and_keeps_commit",
                 [stage](const fs::path &d) {
                     auto p = Seed(d);
                     Gate gate(stage);
                     {
                         Window v;
                         v.Open(p);
                         A::Hook(v.w, [&](UINT s) { gate(s); });
                         A::Send(v.w, A::Op::ImportVideo, d / L"source.mov");
                         gate.Wait();
                         SendMessageW(v.handle, WM_CLOSE, 0, 0);
                         Require(IsWindow(v.handle) && !A::Done(v.w), "closed before worker safe stop");
                         Require(Text(GetDlgItem(v.handle, IDC_STATUS)) == Label(IDS_CLOSE_WAIT),
                                 "close implies cancellation");
                         bool locked = false;
                         try
                         {
                             auto r = ProjectRepository::Open(p);
                         }
                         catch (const ProjectError &)
                         {
                             locked = true;
                         }
                         Require(locked, "lease released while in flight");
                         gate.Release();
                         Until([&] { return !IsWindow(v.handle); });
                         Require(A::Done(v.w), "worker not complete at destruction");
                     }
                     auto r = ProjectRepository::Open(p);
                     Require(ProjectInput::Inspect(*r).revision == 2, "committed import lost on close");
                 });
        test("native_file_dialog_cancel_preserves", [](const fs::path &d) {
            auto p = Seed(d);
            Window v;
            v.Open(p);
            auto pr = Read(p / L"project.mstudio"), m = Read(p / L"input.mstudio"),
                 id = A::Input(v.w)->inputId;
            Require(SetTimer(v.handle, 88, 100, CancelTimer) != 0, "cancel timer");
            SendMessageW(v.handle, WM_COMMAND, MAKEWPARAM(IDC_IMPORT_VIDEO, BN_CLICKED), 0);
            KillTimer(v.handle, 88);
            Require(!A::Busy(v.w), "cancel enqueued work");
            Require(Read(p / L"project.mstudio") == pr && Read(p / L"input.mstudio") == m &&
                        A::Input(v.w)->inputId == id,
                    "cancel mutated input");
            v.Close();
        });
        std::cout << "SUMMARY " << passed << " passed " << failed << " failed\n";
        CoUninitialize();
        return failed ? 1 : 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FATAL " << e.what() << '\n';
        return 2;
    }
}
