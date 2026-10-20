#include "VideoTo3D/WindowsProcessHost.h"
#include "VideoTo3D/ToolAdmission.h"
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace MonsterEngine::VideoTo3D;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
static fs::path self;
static void Require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
static std::string Facts(const ProcessResult& r) { return " launched="+std::to_string(r.launched)+" exited="+std::to_string(r.exited)+" timeout="+std::to_string(r.timedOut)+" cancelled="+std::to_string(r.cancelled)+" retired="+std::to_string(r.treeRetired)+" win32="+std::to_string(r.win32Error)+" error="+r.error; }
static void Write(HANDLE stream, const std::string& bytes) {
    DWORD count = 0;
    Require(WriteFile(stream, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) && count == bytes.size(), "fixture pipe write");
}
static ProcessRequest Request(std::vector<std::wstring> args) {
    ProcessRequest request; request.executable = self; request.arguments = std::move(args); request.timeout = 5s; return request;
}
static void Completed(const ProcessResult& result) {
    Require(result.launched && result.exited && result.exitCode == 0 && result.treeRetired && !result.win32Error && result.error.empty() && !result.timedOut && !result.cancelled, "clean lifecycle expected");
}
static int Fixture(int argc, wchar_t** argv) {
    std::wstring mode = argv[1];
    if (mode == L"--fixture-args") {
        const std::vector<std::wstring> expected{L"", L"two words", L"quote\"inside", L"tail\\", L"slash\\\"quote", L"\u4e2d\u6587"};
        if (argc != 8) return 31;
        for (int index = 0; index < 6; ++index) if (argv[index + 2] != expected[index]) return 32;
        return 0;
    }
    if (mode == L"--fixture-env") {
        wchar_t buffer[256]{};
        if (!GetEnvironmentVariableW(L"MONSTER_TEST_ENV", buffer, 256) || std::wstring(buffer) != L"\u4e2d\u6587 value") return 33;
        if (fs::current_path() != fs::path(argv[2])) return 34;
        return 0;
    }
    if (mode == L"--fixture-flood") {
        std::thread error([] { for (int i=0; i<128; ++i) Write(GetStdHandle(STD_ERROR_HANDLE), std::string(8192, 'e')); });
        for (int i=0; i<128; ++i) Write(GetStdHandle(STD_OUTPUT_HANDLE), std::string(8192, 'o'));
        error.join(); return 0;
    }
    if (mode == L"--fixture-sleep") { Sleep(30000); return 0; }
    if (mode == L"--fixture-touch") {
        if (argc != 3) return 40;
        std::ofstream(fs::path(argv[2]), std::ios::binary) << "tool code executed";
        Write(GetStdHandle(STD_OUTPUT_HANDLE), "tool code executed");
        return 0;
    }
    if (mode == L"--fixture-tree") {
        std::wstring command = L"\"" + self.native() + L"\" --fixture-sleep";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
        if (!CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) return 35;
        Write(GetStdHandle(STD_OUTPUT_HANDLE), std::to_string(child.dwProcessId));
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
        if (argc > 2) Sleep(30000);
        return 0;
    }
    if (mode == L"--fixture-exit") return 37;
    // A version fixture deliberately has no claim of being a real tool.
    if (mode == L"--help" || mode == L"-version" || mode == L"help") { Write(GetStdHandle(STD_OUTPUT_HANDLE), "TEST_FIXTURE_ONLY\xff\x80"); return 0; }
    return 39;
}
int wmain(int argc, wchar_t** argv) {
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768); self = executable;
    if (argc > 1) return Fixture(argc, argv);
    int passed = 0, failed = 0;
    auto test = [&](const char* name, auto body) { try { body(); ++passed; std::cout << "PASS " << name << '\n'; } catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; } };
    auto temporary = fs::temp_directory_path() / (L"MonsterVideo3D_\u4e2d\u6587 " + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(temporary);
    test("argv_unicode_empty_quotes_trailing_slashes", [] { Completed(WindowsProcessHost::Run(Request({L"--fixture-args", L"", L"two words", L"quote\"inside", L"tail\\", L"slash\\\"quote", L"\u4e2d\u6587"}))); });
    test("unicode_working_directory_environment", [&] { auto request=Request({L"--fixture-env", temporary.native()}); request.workingDirectory=temporary; request.environmentOverrides[L"monster_test_env"]=L"\u4e2d\u6587 value"; Completed(WindowsProcessHost::Run(request)); });
    test("simultaneous_pipe_flood_remains_bounded", [] { auto request=Request({L"--fixture-flood"}); request.maxCapturedBytesPerStream=137; auto r=WindowsProcessHost::Run(request); Completed(r); Require(r.stdoutTotalBytes==1048576 && r.stderrTotalBytes==1048576 && r.stdoutBytes==std::string(137,'o') && r.stderrBytes==std::string(137,'e') && r.stdoutTruncated && r.stderrTruncated,"drain all bytes with bounded prefixes"); });
    test("zero_capture_still_drains", [] { auto q=Request({L"--fixture-flood"}); q.maxCapturedBytesPerStream=0; auto r=WindowsProcessHost::Run(q); Completed(r); Require(r.stdoutBytes.empty() && r.stderrBytes.empty() && r.stdoutTotalBytes==1048576 && r.stderrTotalBytes==1048576 && r.stdoutTruncated && r.stderrTruncated,"zero capture"); });
    test("timeout_retires_process", [] { auto q=Request({L"--fixture-sleep"}); q.timeout=100ms; auto r=WindowsProcessHost::Run(q); Require(r.launched && r.exited && r.timedOut && r.treeRetired && !r.cancelled,"timeout lifecycle"+Facts(r)); });
    test("cancel_retires_process", [] { std::atomic_bool cancel=false; std::jthread thread([&] { Sleep(150); cancel=true; }); auto r=WindowsProcessHost::Run(Request({L"--fixture-sleep"}),&cancel); Require(r.launched && r.exited && r.cancelled && r.treeRetired && !r.timedOut,"cancel lifecycle"+Facts(r)); });
    test("pre_cancel_never_launches", [] { std::atomic_bool cancel=true; auto r=WindowsProcessHost::Run(Request({L"--fixture-sleep"}),&cancel); Require(!r.launched && r.cancelled && r.treeRetired,"pre cancel"); });
    test("zero_timeout_never_launches_or_writes", [&] { auto marker=temporary/L"zero-timeout-side-effect.txt"; auto q=Request({L"--fixture-touch",marker.native()}); q.timeout=0ms; auto r=WindowsProcessHost::Run(q); Require(!r.launched && !r.exited && r.timedOut && !r.cancelled && r.treeRetired && !r.win32Error && r.error.empty() && r.stdoutBytes.empty() && !fs::exists(marker),"zero deadline allowed tool execution"+Facts(r)); });
    test("pre_cancel_wins_over_zero_timeout", [&] { auto marker=temporary/L"cancel-side-effect.txt"; auto q=Request({L"--fixture-touch",marker.native()}); q.timeout=0ms; std::atomic_bool cancel=true; auto r=WindowsProcessHost::Run(q,&cancel); Require(!r.launched && !r.exited && r.cancelled && !r.timedOut && r.treeRetired && !r.win32Error && !fs::exists(marker),"cancel priority or no-launch invariant"+Facts(r)); });
    for (bool timeout : {false,true}) test(timeout ? "timeout_retires_descendant" : "normal_exit_retires_descendant", [=] { auto q=Request(timeout ? std::vector<std::wstring>{L"--fixture-tree",L"wait"} : std::vector<std::wstring>{L"--fixture-tree"}); if(timeout) q.timeout=500ms; auto r=WindowsProcessHost::Run(q); Require(r.launched && r.exited && r.treeRetired && r.timedOut==timeout && !r.stdoutBytes.empty(),"tree lifecycle"+Facts(r)); DWORD pid=std::stoul(r.stdoutBytes); HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid); if(process) { auto wait=WaitForSingleObject(process,0); CloseHandle(process); Require(wait==WAIT_OBJECT_0,"descendant remains alive wait="+std::to_string(wait)+Facts(r)); } else Require(GetLastError()==ERROR_INVALID_PARAMETER,"cannot inspect descendant"); });
    test("nonzero_exit_is_not_host_error", [] { auto r=WindowsProcessHost::Run(Request({L"--fixture-exit"})); Require(r.launched && r.exited && r.exitCode==37 && r.treeRetired && r.error.empty(),"nonzero status"); });
    test("missing_executable_never_launches", [&] { auto q=Request({}); q.executable=temporary/L"absent.exe"; auto r=WindowsProcessHost::Run(q); Require(!r.launched && r.treeRetired && r.win32Error,"missing"); });
    test("reject_invalid_requests_before_launch", [] { std::vector<ProcessRequest> invalid; auto q=Request({}); q.executable=L"relative.exe"; invalid.push_back(q); q=Request({std::wstring(L"bad\0arg",7)}); invalid.push_back(q); q=Request({}); q.environmentOverrides[L"bad=name"]=L"x"; invalid.push_back(q); q=Request({}); q.timeout=-1ms; invalid.push_back(q); q=Request({std::wstring(32768,L'x')}); invalid.push_back(q); for(const auto& value:invalid) { auto r=WindowsProcessHost::Run(value); Require(!r.launched && r.treeRetired && r.win32Error,"invalid request admitted"); } });
    auto file=temporary/L"hash.bin";
    test("sha256_known_vector_case_insensitive", [&] { std::ofstream(file,std::ios::binary)<<"abc"; auto r=InspectToolIdentity(ToolKind::Brush,file,"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"); Require(r.status==IdentityStatus::Match && r.actualSha256=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 vector"); });
    test("identity_unpinned_mismatch_changed_missing", [&] { Require(InspectToolIdentity(ToolKind::Brush,file,"").status==IdentityStatus::Unpinned,"unpinned"); auto pin=InspectToolIdentity(ToolKind::Brush,file,"").actualSha256; std::ofstream(file,std::ios::binary)<<"changed"; Require(InspectToolIdentity(ToolKind::Brush,file,pin).status==IdentityStatus::Mismatch,"changed file admitted"); Require(InspectToolIdentity(ToolKind::Brush,temporary/L"missing",pin).status==IdentityStatus::Missing,"missing"); });
    test("identity_rejects_invalid_pin_path_role", [&] { for(const auto& pin: {std::string("bad"),std::string(64,'z')}) Require(InspectToolIdentity(ToolKind::Brush,file,pin).status==IdentityStatus::Error,"invalid pin"); Require(InspectToolIdentity(ToolKind::Brush,L"relative","").status==IdentityStatus::Error,"relative"); Require(InspectToolIdentity(static_cast<ToolKind>(99),file,"").status==IdentityStatus::Error,"role"); Require(InspectToolIdentity(ToolKind::Brush,temporary,"").status==IdentityStatus::Error,"directory"); });
    test("identity_streams_multiple_blocks", [&] { std::ofstream out(file,std::ios::binary); out<<std::string(1000000,'a'); out.close(); Require(InspectToolIdentity(ToolKind::FFmpeg,file,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0").status==IdentityStatus::Match,"million a vector"); });
    test("json_controls_and_enum_names", [] { Require(EscapeJson(std::string("\"\\\b\f\n\r\t\0\x1f",9))=="\\\"\\\\\\b\\f\\n\\r\\t\\u0000\\u001f","JSON escapes"); Require(ToolKindName(ToolKind::Colmap)=="COLMAP" && ToolKindName(static_cast<ToolKind>(99))=="UNKNOWN" && IdentityStatusName(IdentityStatus::Match)=="Match" && IdentityStatusName(static_cast<IdentityStatus>(99))=="UNKNOWN","enum names"); });
    fs::remove_all(temporary);
    std::cout << "TOTAL " << passed+failed << " PASSED " << passed << " FAILED " << failed << '\n'; return failed ? 1 : 0;
}
