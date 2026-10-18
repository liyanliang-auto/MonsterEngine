#include "Core/ShaderCompiler.h"
#include "Core/Log.h"
#include "Renderer/Splat/SplatResourceProvider.h"

#if PLATFORM_WINDOWS
    #include <Windows.h>
#endif

#include <fstream>
#include <filesystem>
#include <limits>

namespace fs = std::filesystem;

namespace MonsterRender {

    // Bind on the session owner rather than changing a process-global path that
    // could redirect a different renderer or outlive the AssetManager lease.
    static thread_local std::shared_ptr<Splat::IResourceProvider> resourceProvider;

    void ShaderCompiler::setResourceProvider(std::shared_ptr<Splat::IResourceProvider> provider) {
        resourceProvider = std::move(provider);
    }

    static String joinArgs(const TArray<String>& args) {
        String s;
        for (size_t i = 0; i < args.size(); ++i) {
            s += args[i];
            if (i + 1 < args.size()) s += ' ';
        }
        return s;
    }

    std::vector<uint8> ShaderCompiler::compileFromFile(const String& filePath, const ShaderCompileOptions& options) {
#if PLATFORM_ANDROID
        (void)options;
        if (filePath.size() >= 4 && filePath.compare(filePath.size() - 4, 4, ".spv") == 0)
            return readFileBytes(filePath);
        MR_LOG_ERROR("Runtime shader compilation is unavailable on Android; use packaged SPIR-V");
        return {};
#else
        std::vector<uint8> result;

        // Decide compiler and args
        bool useGLSLC = options.language == EShaderLanguage::GLSL;
        String exe = useGLSLC ? "glslc" : "dxc";
        TArray<String> args;

        // Offline cache: if output exists and newer than source, reuse
        String outPath = getTemporarySpirvPath(filePath);
        uint64 srcTime = getLastWriteTime(filePath);
        uint64 outTime = getLastWriteTime(outPath);
        if (outTime && outTime >= srcTime) {
            return readFileBytes(outPath);
        }

        if (useGLSLC) {
            // glslc -fshader-stage=vert file -o file.spv -g
            args.push_back("-fshader-stage=" + getStageArgGLSLC(options.stage));
            if (options.generateDebugInfo) args.push_back("-g");
            for (auto& def : options.definitions) args.push_back("-D" + def);
            args.push_back(filePath);
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
            args.push_back("-Fo"); args.push_back(outPath);

            String so, se; int ec = -1;
            if (!runProcess(exe, args, "", so, se, ec) || ec != 0) {
                MR_LOG_ERROR("dxc failed: " + se);
                return result;
            }
            result = readFileBytes(outPath);
        }

        return result;
#endif
    }

    std::vector<uint8> ShaderCompiler::readFileBytes(const String& filePath) {
        std::vector<uint8> data;
        try {
            if (resourceProvider) {
                std::string error;
                if (!resourceProvider->ReadBytes(filePath, data, error)) {
                    MR_LOG_ERROR("Packaged shader resource read failed: " + error);
                    return {};
                }
                return data;
            }
#if PLATFORM_ANDROID
            MR_LOG_ERROR("Android shader resource provider is not bound on the owner thread");
            return {};
#else
            std::ifstream f(fs::u8path(filePath), std::ios::binary | std::ios::ate);
            if (!f) return {};
            const auto length = f.tellg();
            // Byte loading is used for shader binaries/sources, never entire scenes.
            if (length < 0 || uint64_t(length) > (64ull << 20) || uint64_t(length) > uint64_t(std::numeric_limits<std::streamsize>::max())) return {};
            data.resize(static_cast<size_t>(length)); f.seekg(0);
            if (!f || (length && !f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(length))) || f.peek() != std::char_traits<char>::eof()) return {};
            return data;
#endif
        } catch (const std::exception&) {
            MR_LOG_ERROR("Shader byte read failed");
            return {};
        }
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
            case EShaderStageKind::Compute:  return "comp";
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


