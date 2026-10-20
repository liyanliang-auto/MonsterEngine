// Copyright Monster Engine. All Rights Reserved.
#include "VideoTo3D/ProjectInput.h"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace MonsterEngine::VideoTo3D;

namespace {

struct UsageError final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class Command { ImportVideo, ImportPhotos, Inspect };

struct Options {
    Command command;
    std::filesystem::path project;
    std::filesystem::path source;
};

Options Parse(int argc, wchar_t** argv) {
    if (argc < 2) throw UsageError("Expected import-video, import-photos, or inspect");
    const std::wstring_view verb(argv[1]);
    Command command;
    if (verb == L"import-video") command = Command::ImportVideo;
    else if (verb == L"import-photos") command = Command::ImportPhotos;
    else if (verb == L"inspect") command = Command::Inspect;
    else throw UsageError("Unknown command; use --help for syntax");

    Options options{command, {}, {}};
    bool projectSeen = false;
    bool sourceSeen = false;
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc) throw UsageError("Each option needs a path value");
        const std::wstring_view key(argv[i]);
        const std::wstring_view value(argv[i + 1]);
        if (value.empty()) throw UsageError("Path values must not be empty");
        if (key == L"--project" && !projectSeen) {
            options.project = std::filesystem::path(value);
            projectSeen = true;
        } else if (key == L"--source" && !sourceSeen) {
            options.source = std::filesystem::path(value);
            sourceSeen = true;
        } else {
            throw UsageError("Unknown or duplicate option");
        }
    }
    if (!projectSeen) throw UsageError("--project DIR is required");
    if (command == Command::Inspect && sourceSeen)
        throw UsageError("inspect does not accept --source");
    if (command != Command::Inspect && !sourceSeen)
        throw UsageError("Import requires --source PATH");
    return options;
}

std::string EscapeJson(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char c : value) {
        switch (c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (c < 0x20) {
                escaped += "\\u00";
                escaped += digits[c >> 4];
                escaped += digits[c & 15];
            } else {
                escaped += static_cast<char>(c);
            }
        }
    }
    return escaped;
}

void PrintError(std::string_view code, std::string_view message) {
    std::cerr << "{\"status\":\"error\",\"code\":\"" << code
              << "\",\"message\":\"" << EscapeJson(message) << "\"}\n";
}

const char* ErrorName(InputErrorCode code) {
    switch (code) {
    case InputErrorCode::NoInput: return "NO_INPUT";
    case InputErrorCode::InvalidSource: return "INVALID_SOURCE";
    case InputErrorCode::UnsafePath: return "UNSAFE_PATH";
    case InputErrorCode::SourceChanged: return "SOURCE_CHANGED";
    case InputErrorCode::CorruptManifest: return "CORRUPT_MANIFEST";
    case InputErrorCode::LimitExceeded: return "LIMIT_EXCEEDED";
    case InputErrorCode::OwnershipConflict: return "OWNERSHIP_CONFLICT";
    case InputErrorCode::IoFailure: return "IO_FAILURE";
    }
    return "IO_FAILURE";
}

int ExitCode(InputErrorCode code) {
    switch (code) {
    case InputErrorCode::NoInput: return 10;
    case InputErrorCode::InvalidSource: return 4;
    case InputErrorCode::UnsafePath: return 5;
    case InputErrorCode::SourceChanged: return 6;
    case InputErrorCode::CorruptManifest: return 7;
    case InputErrorCode::LimitExceeded: return 8;
    case InputErrorCode::OwnershipConflict: return 3;
    case InputErrorCode::IoFailure: return 9;
    }
    return 9;
}

const char* ProjectErrorName(ProjectErrorCode code) {
    switch (code) {
    case ProjectErrorCode::InvalidInput: return "PROJECT_INVALID_INPUT";
    case ProjectErrorCode::IncompatibleSchema: return "PROJECT_INCOMPATIBLE_SCHEMA";
    case ProjectErrorCode::LimitExceeded: return "PROJECT_LIMIT_EXCEEDED";
    case ProjectErrorCode::OwnershipConflict: return "PROJECT_OWNERSHIP_CONFLICT";
    case ProjectErrorCode::UnsafePath: return "PROJECT_UNSAFE_PATH";
    case ProjectErrorCode::IoFailure: return "PROJECT_IO_FAILURE";
    case ProjectErrorCode::Conflict: return "PROJECT_CONFLICT";
    }
    return "PROJECT_ERROR";
}

void PrintManifest(const InputManifest& manifest) {
    std::cout << "{\"status\":\"ok\",\"scope\":\"OWNED_INPUT_METADATA_ONLY\","
              << "\"schemaVersion\":" << manifest.schemaVersion
              << ",\"projectId\":\"" << EscapeJson(manifest.projectId)
              << "\",\"inputId\":\"" << EscapeJson(manifest.inputId)
              << "\",\"revision\":" << manifest.revision
              << ",\"kind\":\"" << (manifest.kind == InputKind::Video ? "video" : "photos")
              << "\",\"files\":[";
    bool first = true;
    for (const auto& file : manifest.files) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"relativePath\":\"" << EscapeJson(file.relativePath)
                  << "\",\"byteCount\":" << file.byteCount
                  << ",\"sha256\":\"" << EscapeJson(file.sha256) << "\"}";
    }
    std::cout << "]}\n";
}

void PrintHelp() {
    std::cout << "MonsterVideoTo3DInput: Windows development input CLI\n"
              << "  import-video  --project DIR --source FILE\n"
              << "  import-photos --project DIR --source DIR\n"
              << "  inspect       --project DIR\n"
              << "  --help\n"
              << "Exit: 0 success; 2 usage; 3 project/lease; 4 invalid source;"
                 " 5 unsafe path; 6 changed source; 7 corrupt input;"
                 " 8 limit; 9 I/O; 10 no input.\n"
              << "Import copies owned bytes and records identity only; it does not decode media.\n";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    DWORD consoleMode = 0;
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != INVALID_HANDLE_VALUE && output != nullptr && GetConsoleMode(output, &consoleMode) &&
        !SetConsoleOutputCP(CP_UTF8)) {
        PrintError("IO_FAILURE", "Cannot configure UTF-8 console output");
        return 9;
    }
    if (argc == 2 && std::wstring_view(argv[1]) == L"--help") {
        PrintHelp();
        return 0;
    }
    try {
        const Options options = Parse(argc, argv);
        auto repository = ProjectRepository::Open(options.project);
        InputManifest manifest;
        if (options.command == Command::Inspect) {
            manifest = ProjectInput::Inspect(*repository);
        } else {
            manifest = ProjectInput::Import(*repository,
                options.command == Command::ImportVideo ? InputKind::Video : InputKind::Photos,
                options.source);
        }
        PrintManifest(manifest);
        return 0;
    } catch (const UsageError& error) {
        PrintError("USAGE", error.what());
        return 2;
    } catch (const InputError& error) {
        PrintError(ErrorName(error.Code()), error.what());
        return ExitCode(error.Code());
    } catch (const ProjectError& error) {
        PrintError(ProjectErrorName(error.Code()), error.what());
        return 3;
    } catch (const std::exception& error) {
        PrintError("IO_FAILURE", error.what());
        return 9;
    }
}
