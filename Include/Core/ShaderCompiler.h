#pragma once

#include "Core/CoreMinimal.h"
#include <vector>
#include <memory>

namespace MonsterRender::Splat { class IResourceProvider; }

namespace MonsterRender {

    enum class EShaderLanguage : uint32 {
        GLSL,
        HLSL
    };

    enum class EShaderStageKind : uint32 {
        Vertex,
        Fragment,
        Compute
    };

    struct ShaderCompileOptions {
        EShaderLanguage language = EShaderLanguage::GLSL;
        EShaderStageKind stage = EShaderStageKind::Vertex;
        String entryPoint = "main";
        TArray<String> definitions;
        bool generateDebugInfo = true;
    };

    /**
     * Desktop compiler plus packaged-byte loading. Android uses precompiled
     * resources exclusively and never launches a runtime compiler.
     */
    class ShaderCompiler {
    public:
        /** Compile from source file into SPIR-V bytecode. Returns empty vector on failure. */
        static std::vector<uint8> compileFromFile(const String& filePath, const ShaderCompileOptions& options);

        /** Read a binary file from disk. */
        static std::vector<uint8> readFileBytes(const String& filePath);

        /** Bind immutable resources on the current rendering owner thread.
         * The shared provider stays alive until replaced/cleared on that thread.
         * Android requires this binding before initializing shared Splat passes;
         * pass nullptr after session shutdown to release provider ownership.
         * Desktop callers without a binding retain ordinary filesystem loading. */
        static void setResourceProvider(std::shared_ptr<Splat::IResourceProvider> provider);

        /** Get last write time (seconds since epoch). Returns 0 on failure. */
        static uint64 getLastWriteTime(const String& filePath);

    private:
        static bool runProcess(const String& exe, const TArray<String>& args, const String& workingDir, String& stdOut, String& stdErr, int& exitCode);
        static String getTemporarySpirvPath(const String& filePath);
        static String getStageArgGLSLC(EShaderStageKind stage);
        static String getStageArgDXC(EShaderStageKind stage);
    };
}


