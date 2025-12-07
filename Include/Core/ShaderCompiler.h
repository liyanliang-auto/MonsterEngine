#pragma once

#include "Core/CoreMinimal.h"
#include <vector>

namespace MonsterRender {

    enum class EShaderLanguage : uint32 {
        GLSL,
        HLSL
    };

    enum class EShaderStageKind : uint32 {
        Vertex,
        Fragment
    };

    struct ShaderCompileOptions {
        EShaderLanguage language = EShaderLanguage::GLSL;
        EShaderStageKind stage = EShaderStageKind::Vertex;
        String entryPoint = "main";
        TArray<String> definitions;
        bool generateDebugInfo = true;
    };

    /**
     * Lightweight shader compiler that shells out to glslc/dxc to produce SPIR-V.
     * Follows UE-like tool abstraction but minimal for now.
     */
    class ShaderCompiler {
    public:
        /** Compile from source file into SPIR-V bytecode. Returns empty vector on failure. */
        static std::vector<uint8> compileFromFile(const String& filePath, const ShaderCompileOptions& options);

        /** Read a binary file from disk. */
        static std::vector<uint8> readFileBytes(const String& filePath);

        /** Get last write time (seconds since epoch). Returns 0 on failure. */
        static uint64 getLastWriteTime(const String& filePath);

    private:
        static bool runProcess(const String& exe, const TArray<String>& args, const String& workingDir, String& stdOut, String& stdErr, int& exitCode);
        static String getTemporarySpirvPath(const String& filePath);
        static String getStageArgGLSLC(EShaderStageKind stage);
        static String getStageArgDXC(EShaderStageKind stage);
    };
}


