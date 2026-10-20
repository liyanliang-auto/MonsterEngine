// Copyright Monster Engine. All Rights Reserved.
#include "VideoTo3D/ToolAdmission.h"
#include "VideoTo3D/WindowsProcessHost.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace MonsterEngine::VideoTo3D;
namespace {
constexpr const char* BrushRevision = "3edecbb2fe79d3e2c87eeab85b15e0b1dd10d486";

std::string Utf8(std::wstring_view value) {
    if (value.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Invalid UTF-16 input");
    std::string result(count, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count, nullptr, nullptr))
        throw std::runtime_error("UTF-8 conversion failed");
    return result;
}

std::string Quote(std::string_view value) { return "\"" + EscapeJson(value) + "\""; }
std::string PathJson(const std::filesystem::path& path) { return Quote(Utf8(path.native())); }
const char* Boolean(bool value) { return value ? "true" : "false"; }

// Process streams have no encoding contract. Hex preserves every retained byte,
// including invalid UTF-8 and capture prefixes ending inside a multibyte sequence.
std::string Hex(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (unsigned char byte : value) {
        result.push_back(digits[byte >> 4]); result.push_back(digits[byte & 15]);
    }
    return result;
}

std::string UtcNow() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char text[32]{};
    std::snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds);
    return text;
}

struct Options {
    std::array<std::filesystem::path, 4> paths;
    std::array<std::string, 4> hashes;
    std::filesystem::path output;
    std::filesystem::path audit{L"Tools/VideoTo3D/brush-admission.json"};
};

Options Parse(int argc, wchar_t** argv) {
    Options options;
    const std::array<std::wstring, 4> names{L"ffprobe", L"ffmpeg", L"colmap", L"brush"};
    std::map<std::wstring, bool> seen;
    for (int i = 1; i < argc; ++i) {
        std::wstring key = argv[i];
        if (seen[key]) throw std::runtime_error("Duplicate option");
        seen[key] = true;
        if (i + 1 == argc) throw std::runtime_error("Option requires a value");
        std::wstring value = argv[++i];
        if (value.empty()) throw std::runtime_error("Option value must not be empty");
        if (key == L"--output") { options.output = value; continue; }
        if (key == L"--brush-audit") { options.audit = value; continue; }
        bool known = false;
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (key == L"--" + names[index]) { options.paths[index] = value; known = true; }
            if (key == L"--" + names[index] + L"-sha256") {
                options.hashes[index] = Utf8(value); known = true;
            }
        }
        if (!known) throw std::runtime_error("Unknown option");
    }
    if (options.output.empty()) throw std::runtime_error("--output is required");
    return options;
}

std::string ReadAudit(const std::filesystem::path& path) {
    struct Reader {
        HANDLE handle{INVALID_HANDLE_VALUE};
        ~Reader() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    } reader;
    reader.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (reader.handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot lock Brush audit descriptor for reading");
    const auto identity = InspectToolIdentity(ToolKind::Brush,
        std::filesystem::absolute(path), MONSTER_ADMISSION_AUDIT_SHA256);
    if (identity.status != IdentityStatus::Match)
        throw std::runtime_error("Brush audit rejected: expected exact reviewed schema-1 descriptor; raw input preserved");
    std::string bytes;
    std::array<char, 4096> buffer{};
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(reader.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
            throw std::runtime_error("Brush audit descriptor read failed");
        if (!count) break;
        if (bytes.size() + count > 1048576) throw std::runtime_error("Brush audit descriptor exceeds bound");
        bytes.append(buffer.data(), count);
    }
    // Writes/deletion are denied for the descriptor during the complete read.
    if (InspectToolIdentity(ToolKind::Brush, std::filesystem::absolute(path),
        MONSTER_ADMISSION_AUDIT_SHA256).status != IdentityStatus::Match)
        throw std::runtime_error("Brush audit descriptor changed while reading");
    return bytes;
}

bool SameInput(const std::filesystem::path& output, const std::filesystem::path& input) {
    if (input.empty()) return false;
    std::error_code error;
    if (std::filesystem::exists(output) && std::filesystem::exists(input) &&
        std::filesystem::equivalent(output, input, error)) return true;
    if (error) throw std::runtime_error("Cannot inspect report/input file identity");
    auto left = std::filesystem::weakly_canonical(output).native();
    auto right = std::filesystem::weakly_canonical(input).native();
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
        static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

void Publish(const std::filesystem::path& destination, const std::string& bytes) {
    auto parent = destination.parent_path();
    if (!std::filesystem::is_directory(parent)) throw std::runtime_error("Output parent directory must exist");
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned index = 0; index < 100 && file == INVALID_HANDLE_VALUE; ++index) {
        temporary = destination;
        temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(index);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS)
            throw std::runtime_error("Cannot create temporary report");
    }
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot reserve temporary report");
    bool complete = true;
    std::size_t written = 0;
    while (written < bytes.size()) {
        DWORD count = 0;
        const DWORD requested = static_cast<DWORD>((std::min)(bytes.size() - written, std::size_t(1048576)));
        if (!WriteFile(file, bytes.data() + written, requested, &count, nullptr) || !count) { complete = false; break; }
        written += count;
    }
    if (complete && !FlushFileBuffers(file)) complete = false;
    if (!CloseHandle(file)) complete = false;
    if (!complete) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Report write/flush failed; existing output preserved");
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Report publication failed; existing output preserved");
    }
}

std::string ProbeJson(ToolKind kind, const std::filesystem::path& path,
    const std::string& hash, const std::filesystem::path& workingDirectory, bool& allPassed) {
    const auto identity = InspectToolIdentity(kind, path, hash);
    ProcessRequest request;
    request.executable = path;
    request.workingDirectory = workingDirectory;
    request.arguments = {kind == ToolKind::Colmap ? L"help" :
        kind == ToolKind::Brush ? L"--help" : L"-version"};
    ProcessResult result;
    const auto started = UtcNow();
    bool attempted = identity.status == IdentityStatus::Match;
    if (attempted) result = WindowsProcessHost::Run(request);
    bool passed = attempted && result.launched && result.exited && result.exitCode == 0 &&
        result.treeRetired && !result.timedOut && !result.cancelled && result.error.empty() && result.win32Error == 0;
    // A post-probe mismatch invalidates this probe; it still cannot close the launch race.
    auto after = attempted ? InspectToolIdentity(kind, path, hash) : identity;
    passed = passed && after.status == IdentityStatus::Match;
    allPassed = allPassed && passed;
    std::ostringstream out;
    out << "{\"kind\":" << Quote(ToolKindName(kind)) << ",\"scope\":\"VERSION_PROBE_ONLY\","
        << "\"executable\":" << PathJson(path) << ",\"expectedSha256\":" << Quote(hash)
        << ",\"observedSha256\":" << Quote(identity.actualSha256)
        << ",\"postProbeObservedSha256\":" << Quote(after.actualSha256)
        << ",\"postProbeIdentityStatus\":" << Quote(IdentityStatusName(after.status))
        << ",\"identityStatus\":" << Quote(IdentityStatusName(identity.status))
        << ",\"identityError\":" << Quote(identity.error)
        << ",\"status\":" << Quote(passed ? "PROBE_COMPLETED" : "INCOMPLETE")
        << ",\"attempted\":" << Boolean(attempted)
        << ",\"startedAtUtc\":" << Quote(started) << ",\"finishedAtUtc\":" << Quote(UtcNow())
        << ",\"argv\":[" << PathJson(path) << ',' << Quote(Utf8(request.arguments[0])) << ']'
        << ",\"workingDirectory\":" << PathJson(workingDirectory)
        << ",\"timeoutMilliseconds\":" << request.timeout.count()
        << ",\"captureLimitPerStream\":" << request.maxCapturedBytesPerStream
        << ",\"launched\":" << Boolean(result.launched) << ",\"exited\":" << Boolean(result.exited)
        << ",\"exitCode\":" << (result.exited ? std::to_string(result.exitCode) : "null")
        << ",\"treeRetired\":" << Boolean(result.treeRetired)
        << ",\"timedOut\":" << Boolean(result.timedOut) << ",\"cancelled\":" << Boolean(result.cancelled)
        << ",\"win32Error\":" << result.win32Error << ",\"error\":" << Quote(result.error)
        << ",\"streamEncoding\":\"hex-raw-bytes\",\"stdoutHex\":" << Quote(Hex(result.stdoutBytes))
        << ",\"stderrHex\":" << Quote(Hex(result.stderrBytes))
        << ",\"stdoutTotalBytes\":" << result.stdoutTotalBytes << ",\"stderrTotalBytes\":" << result.stderrTotalBytes
        << ",\"stdoutTruncated\":" << Boolean(result.stdoutTruncated)
        << ",\"stderrTruncated\":" << Boolean(result.stderrTruncated) << '}';
    return out.str();
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--help") {
        std::cout << "MonsterVideoTo3DAdmission: Windows developer M1 probe\n"
            "--output PATH [--brush-audit PATH]\n"
            "[--ffprobe PATH --ffprobe-sha256 HEX] [--ffmpeg PATH --ffmpeg-sha256 HEX]\n"
            "[--colmap PATH --colmap-sha256 HEX] [--brush PATH --brush-sha256 HEX]\n"
            "Only identity-matched absolute executable paths run. Missing tools remain incomplete.\n"
            "Exit 0: all version/help probes completed; 2: incomplete; 1: input/report error.\n";
        return 0;
    }
    try {
        const auto started = UtcNow();
        auto options = Parse(argc, argv);
        const auto workingDirectory = std::filesystem::current_path();
        options.output = std::filesystem::absolute(options.output);
        options.audit = std::filesystem::absolute(options.audit);
        // Validate audit before any external execution. The known descriptor was reviewed
        // against Brush's immutable CLI source; --help does not begin a training run.
        const auto audit = ReadAudit(options.audit);
        if (SameInput(options.output, options.audit)) throw std::runtime_error("Output must not overwrite audit input");
        for (const auto& path : options.paths)
            if (SameInput(options.output, path))
                throw std::runtime_error("Output must not overwrite an executable input");
        bool allPassed = true;
        std::ostringstream report;
        report << "{\"schemaVersion\":1,\"scope\":\"VERSION_PROBE_ONLY\",\"startedAtUtc\":" << Quote(started)
            << ",\"sourceBuildIdentity\":{\"configureHead\":" << Quote(MONSTER_ADMISSION_SOURCE_HEAD)
            << ",\"configuredAtUtc\":" << Quote(MONSTER_ADMISSION_CONFIGURED_UTC)
            << ",\"configuration\":" << Quote(MONSTER_ADMISSION_BUILD_CONFIG)
            << ",\"compiler\":" << Quote(MONSTER_ADMISSION_COMPILER)
            << ",\"worktreeIdentity\":\"NOT_VERIFIED_HEAD_DOES_NOT_IDENTIFY_UNCOMMITTED_SOURCE\"}"
            << ",\"brushAudit\":{\"path\":" << PathJson(options.audit)
            << ",\"sha256\":" << Quote(MONSTER_ADMISSION_AUDIT_SHA256)
            << ",\"revision\":" << Quote(BrushRevision)
            << ",\"auditStatus\":\"SUPPORTED_BY_SOURCE\",\"rawDescriptorUtf8\":" << Quote(audit) << '}'
            << ",\"tools\":[";
        const std::array<ToolKind, 4> kinds{ToolKind::FFprobe, ToolKind::FFmpeg, ToolKind::Colmap, ToolKind::Brush};
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            if (i) report << ',';
            report << ProbeJson(kinds[i], options.paths[i], options.hashes[i], workingDirectory, allPassed);
        }
        report << "],\"finishedAtUtc\":" << Quote(UtcNow())
            << ",\"versionProbeAdmission\":" << Quote(allPassed ? "PROBES_COMPLETED" : "INCOMPLETE")
            << ",\"productAdmission\":\"BLOCKED\",\"gates\":{";
        const std::array<const char*, 12> gates{"mediaDecode", "cameraReconstruction", "training", "safePause",
            "completeCheckpointRestore", "gaussianImport", "cameraSHAlignment", "viewerRender", "imageQuality",
            "hardwareSupport", "offlineDistribution", "release"};
        for (std::size_t i = 0; i < gates.size(); ++i) {
            if (i) report << ',';
            report << Quote(gates[i]) << ":\"NOT_VERIFIED\"";
        }
        report << "},\"limitations\":["
            "\"Version/help output and exit status do not prove tool-role authenticity, training or product acceptance.\","
            "\"Executable SHA256 is observed before and after launch; the hash handle is not held across launch and replacement races are not excluded.\","
            "\"Brush binary-to-source/build binding is NOT_VERIFIED even when its caller-supplied executable pin matches.\","
            "\"Reviewed Brush source lacks product safe pause and complete checkpoint restore.\","
            "\"Probe timeout/capture limits are engineering bounds, not approved product resource budgets.\","
            "\"Report paths and tool output may contain private local information.\"]}\n";
        Publish(options.output, report.str());
        std::cout << (allPassed ? "Version/help probes completed; product admission BLOCKED.\n" :
            "Admission incomplete; product admission BLOCKED.\n");
        return allPassed ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "Admission error: " << error.what() << '\n';
        return 1;
    }
}
