#include "Core/ShaderCompiler.h"
#include "Core/Log.h"

#if PLATFORM_WINDOWS
    #include <Windows.h>
#endif

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace MonsterRender {

    static String joinArgs(const TArray<String>& args) {
        String s;
        for (size_t i = 0; i < args.size(); ++i) {
            s += args[i];
            if (i + 1 < args.size()) s += ' ';
        }
        return s;
    }

    TArray<uint8> ShaderCompiler::compileFromFile(const String& filePath, const ShaderCompileOptions& options) {
        TArray<uint8> result;

        // Decide compiler and args
        bool useGLSLC = options.language == EShaderLanguage::GLSL;
        String exe = useGLSLC ? "glslc" : "dxc";
        TArray<String> args;

        if (useGLSLC) {
            // glslc -fshader-stage=vert file -o file.spv -g
            args.push_back("-fshader-stage=" + getStageArgGLSLC(options.stage));
            if (options.generateDebugInfo) args.push_back("-g");
            for (auto& def : options.definitions) args.push_back("-D" + def);
            args.push_back(filePath);
            String outPath = getTemporarySpirvPath(filePath);
            args.push_back("-o");
            args.push_back(outPath);

            String so, se; int ec = -1;
            if (!runProcess(exe, args, "", so, se, ec) || ec != 0) {
                MR_LOG_ERROR("glslc failed: " + se);
                return result;
            }
            result = readFileBytes(outPath);
        } else {
            // dxc -spirv -T vs_6_0 -E main file -Fo file.spv -Zi
            args.push_back("-spirv");
            args.push_back("-E"); args.push_back(options.entryPoint);
            args.push_back("-T"); args.push_back(getStageArgDXC(options.stage));
            if (options.generateDebugInfo) args.push_back("-Zi");
            for (auto& def : options.definitions) { args.push_back("-D"); args.push_back(def); }
            args.push_back(filePath);
            args.push_back("-Fo"); args.push_back(getTemporarySpirvPath(filePath));

            String so, se; int ec = -1;
            if (!runProcess(exe, args, "", so, se, ec) || ec != 0) {
                MR_LOG_ERROR("dxc failed: " + se);
                return result;
            }
            result = readFileBytes(getTemporarySpirvPath(filePath));
        }

        return result;
    }

    TArray<uint8> ShaderCompiler::readFileBytes(const String& filePath) {
        TArray<uint8> data;
        std::ifstream f(filePath, std::ios::binary);
        if (!f) return data;
        f.seekg(0, std::ios::end);
        size_t size = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);
        data.resize(size);
        if (size > 0) f.read(reinterpret_cast<char*>(data.data()), size);
        return data;
    }

    uint64 ShaderCompiler::getLastWriteTime(const String& filePath) {
        try {
            auto tp = fs::last_write_time(filePath);
#if defined(_WIN32)
            auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(tp);
#else
            auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(tp);
#endif
            return static_cast<uint64>(sctp.time_since_epoch().count());
        } catch (...) {
            return 0;
        }
    }

    String ShaderCompiler::getTemporarySpirvPath(const String& filePath) {
        return filePath + ".spv";
    }

    String ShaderCompiler::getStageArgGLSLC(EShaderStageKind stage) {
        switch (stage) {
            case EShaderStageKind::Vertex:   return "vert";
            case EShaderStageKind::Fragment: return "frag";
            default: return "vert";
        }
    }

    String ShaderCompiler::getStageArgDXC(EShaderStageKind stage) {
        switch (stage) {
            case EShaderStageKind::Vertex:   return "vs_6_0";
            case EShaderStageKind::Fragment: return "ps_6_0";
            default: return "vs_6_0";
        }
    }

    bool ShaderCompiler::runProcess(const String& exe, const TArray<String>& args, const String& workingDir, String& stdOut, String& stdErr, int& exitCode) {
#if PLATFORM_WINDOWS
        String cmd = '"' + exe + '"' + ' ' + joinArgs(args);
        STARTUPINFOA si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        // Use cmd.exe /C to leverage PATH lookup
        String fullCmd = "cmd.exe /C " + cmd;
        if (!CreateProcessA(nullptr, fullCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, workingDir.empty() ? nullptr : workingDir.c_str(), &si, &pi)) {
            MR_LOG_ERROR("Failed to launch compiler process: " + exe);
            exitCode = -1;
            return false;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        exitCode = static_cast<int>(code);
        return true;
#else
        (void)exe; (void)args; (void)workingDir; (void)stdOut; (void)stdErr; (void)exitCode;
        return false;
#endif
    }
}


