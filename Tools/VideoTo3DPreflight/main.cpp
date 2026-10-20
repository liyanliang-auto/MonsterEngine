// Copyright Monster Engine. All Rights Reserved.
#include "VideoTo3D/ProjectPreflight.h"

#include <Windows.h>

#include <array>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>

using namespace MonsterEngine::VideoTo3D;

namespace {

// Every invocation, including --help and usage failures, emits one UTF-8 JSON object.
// Exit: 0 successful check/help; 2 invalid CLI arguments; 3 failed service check;
// 4 unexpected internal failure. No command performs migration or starts M2 work.
constexpr std::string_view Usage =
    "MonsterVideoTo3DPreflight space --directory <absolute-path> "
    "--media-copies <uint64> --prepared-images <uint64> "
    "--reconstruction-database <uint64> --checkpoints <uint64> "
    "--temporary-exports <uint64> --lod <uint64> --new-assets <uint64> "
    "--retained-overlap <uint64> --reserve <uint64>\n"
    "MonsterVideoTo3DPreflight write --directory <absolute-path>\n"
    "MonsterVideoTo3DPreflight compatibility --directory <absolute-path>\n"
    "MonsterVideoTo3DPreflight --help\n"
    "Budgets are explicit unsigned decimal bytes, including zero. The first eight "
    "categories are disjoint incremental additional peak bytes; do not charge "
    "already-resident bytes again. retained-overlap covers only incremental overlap "
    "absent from other categories; reserve is extra headroom. Capacity is an "
    "observation, never a reservation. Compatibility validates metadata structure, "
    "not media integrity. Exit: 0 success/help; 2 usage; 3 failed check; 4 internal failure.";

struct UsageError final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Options {
    std::string command;
    std::string directoryUtf8;
    std::filesystem::path directory;
    SpaceBudget budget;
};

std::string Utf8(std::wstring_view text) {
    if (text.empty()) return {};
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw UsageError("Argument exceeds the supported Unicode conversion size");
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count == 0) throw UsageError("Argument contains invalid UTF-16");
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), count, nullptr, nullptr) != count)
        throw UsageError("Argument Unicode conversion failed");
    return result;
}

std::string Json(std::string_view value) {
    if (!value.empty() &&
        (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
         MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
             static_cast<int>(value.size()), nullptr, 0) == 0))
        throw std::runtime_error("Invalid UTF-8 in result");
    constexpr char digits[] = "0123456789abcdef";
    std::string result = "\"";
    for (const unsigned char ch : value) {
        if (ch == '"') result += "\\\"";
        else if (ch == '\\') result += "\\\\";
        else if (ch < 0x20) {
            result += "\\u00";
            result += digits[ch >> 4];
            result += digits[ch & 15];
        } else result += static_cast<char>(ch);
    }
    result += '"';
    return result;
}

std::uint64_t Decimal(std::wstring_view text) {
    if (text.empty()) throw UsageError("Budget values must be nonempty decimal uint64 bytes");
    std::uint64_t result = 0;
    for (const wchar_t ch : text) {
        if (ch < L'0' || ch > L'9')
            throw UsageError("Budget values accept ASCII decimal digits only, without signs");
        const auto digit = static_cast<std::uint64_t>(ch - L'0');
        if (result > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
            throw UsageError("Budget value exceeds uint64 range");
        result = result * 10 + digit;
    }
    return result;
}

struct BudgetOption {
    std::wstring_view name;
    std::optional<std::uint64_t> SpaceBudget::* field;
};

constexpr std::array<BudgetOption, 9> BudgetOptions{{
    {L"--media-copies", &SpaceBudget::mediaCopies},
    {L"--prepared-images", &SpaceBudget::preparedImages},
    {L"--reconstruction-database", &SpaceBudget::reconstructionDatabase},
    {L"--checkpoints", &SpaceBudget::checkpoints},
    {L"--temporary-exports", &SpaceBudget::temporaryExports},
    {L"--lod", &SpaceBudget::lod},
    {L"--new-assets", &SpaceBudget::newAssets},
    {L"--retained-overlap", &SpaceBudget::retainedOverlap},
    {L"--reserve", &SpaceBudget::reserve}
}};

void Parse(int argc, wchar_t** argv, Options& options) {
    // Validate every argument before echoing any Unicode into JSON error context.
    for (int i = 1; i < argc; ++i) (void)Utf8(argv[i]);
    if (argc < 2) throw UsageError("Expected space, write, compatibility, or --help");
    options.command = Utf8(argv[1]);
    if (options.command == "--help" && argc == 2) return;
    if (options.command != "space" && options.command != "write" && options.command != "compatibility")
        throw UsageError("Unknown command or invalid --help arguments");
    bool directorySeen = false;
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc) throw UsageError("Every option requires a value");
        const std::wstring_view key(argv[i]);
        const std::wstring_view value(argv[i + 1]);
        if (key == L"--directory") {
            if (directorySeen) throw UsageError("Duplicate --directory");
            if (value.empty()) throw UsageError("--directory requires a nonempty absolute path");
            options.directoryUtf8 = Utf8(value);
            options.directory = std::filesystem::path(value);
            directorySeen = true;
            continue;
        }
        bool recognized = false;
        if (options.command == "space") {
            for (const auto& option : BudgetOptions) {
                if (key != option.name) continue;
                auto& field = options.budget.*(option.field);
                if (field.has_value()) throw UsageError("Duplicate budget option");
                field = Decimal(value);
                recognized = true;
                break;
            }
        }
        if (!recognized) throw UsageError("Unknown option for this command");
    }
    if (!directorySeen) throw UsageError("--directory is required");
    if (options.command == "space") {
        for (const auto& option : BudgetOptions)
            if (!(options.budget.*(option.field)).has_value())
                throw UsageError("space requires all nine explicit byte budgets, including zero values");
    }
}

void Diagnostic(std::ostream& output, const PreflightDiagnostic& diagnostic) {
    output << "\"code\":" << Json(PreflightCodeName(diagnostic.code))
        << ",\"message\":" << Json(diagnostic.message)
        << ",\"nativeError\":" << diagnostic.nativeError;
}

std::ostringstream Begin(const Options& options, const PreflightDiagnostic& diagnostic) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::boolalpha << "{\"schemaVersion\":1,\"command\":" << Json(options.command)
        << ",\"directory\":" << Json(options.directoryUtf8) << ',';
    Diagnostic(output, diagnostic);
    return output;
}

void FileResult(std::ostream& output, const CompatibilityFileResult& file) {
    output << '{';
    Diagnostic(output, file.diagnostic);
    output << ",\"schemaVersion\":";
    if (file.schemaVersion) output << *file.schemaVersion;
    else output << "null";
    output << ",\"projectId\":" << Json(file.projectId)
        << ",\"fullyValidatedStructure\":" << file.fullyValidatedStructure << '}';
}

void Emit(std::ostringstream& output) {
    // Assemble before writing, so serialization failures cannot leave partial JSON.
    std::cout << output.str() << "}\n";
}

int Run(const Options& options) {
    if (options.command == "--help") {
        auto output = Begin(options, {PreflightCode::Ok, "Windows development preflight CLI", 0});
        output << ",\"usage\":" << Json(Usage);
        Emit(output);
        return 0;
    }
    PreflightCode code;
    if (options.command == "space") {
        const auto result = QuerySpacePreflight(options.directory, options.budget);
        auto output = Begin(options, result.diagnostic);
        output << ",\"incrementalBytes\":" << result.incrementalBytes
            << ",\"reserveBytes\":" << result.reserveBytes
            << ",\"requiredBytes\":" << result.requiredBytes
            << ",\"pointInTimeObservation\":true,\"capacityReserved\":false,\"sample\":";
        if (result.sample) {
            output << "{\"availableToCaller\":" << result.sample->availableToCaller
                << ",\"volumeFree\":" << result.sample->volumeFree
                << ",\"volumeTotal\":" << result.sample->volumeTotal << '}';
        } else output << "null";
        Emit(output);
        code = result.diagnostic.code;
    } else if (options.command == "write") {
        const auto result = ProbeDirectoryWrite(options.directory);
        auto output = Begin(options, result.diagnostic);
        output << ",\"bytesWritten\":" << result.bytesWritten
            << ",\"created\":" << result.created << ",\"flushed\":" << result.flushed
            << ",\"closed\":" << result.closed << ",\"removed\":" << result.removed
            << ",\"residualPath\":" << Json(Utf8(result.residualPath.native()));
        Emit(output);
        code = result.diagnostic.code;
    } else {
        const auto result = InspectProjectCompatibility(options.directory);
        auto output = Begin(options, result.diagnostic);
        output << ",\"inputPresent\":" << result.inputPresent
            << ",\"mediaIntegrityVerified\":" << result.mediaIntegrityVerified
            << ",\"stableObservation\":" << result.stableObservation
            << ",\"inspectionScope\":\"metadataStructureOnly\",\"project\":";
        FileResult(output, result.project);
        output << ",\"input\":";
        FileResult(output, result.input);
        Emit(output);
        code = result.diagnostic.code;
    }
    return code == PreflightCode::Ok ? 0 : 3;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    try {
        DWORD mode = 0;
        const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) &&
            !SetConsoleOutputCP(CP_UTF8))
            throw std::runtime_error("Cannot configure UTF-8 console output");
        Parse(argc, argv, options);
        return Run(options);
    } catch (const UsageError& error) {
        auto output = Begin(options, {PreflightCode::InvalidArgument, error.what(), 0});
        output << ",\"usage\":" << Json(Usage);
        Emit(output);
        return 2;
    } catch (const PreflightFailure& error) {
        auto output = Begin(options, error.Diagnostic());
        Emit(output);
        return 3;
    } catch (...) {
        auto output = Begin(options, {PreflightCode::IoFailure,
            "Unexpected internal failure; no complete preflight result is available", 0});
        Emit(output);
        return 4;
    }
}
