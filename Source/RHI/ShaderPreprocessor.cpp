#include "RHI/ShaderPreprocessor.h"
#include "Core/Logging/Logging.h"
#include <regex>
#include <string>

using namespace MonsterRender;
using namespace MonsterRender::RHI;

FString FShaderPreprocessor::PreprocessShader(
    const FString& source,
    ERHIBackend targetAPI,
    uint32 maxBindingsPerSet)
{
    // Vulkan: keep original shader
    if (targetAPI == ERHIBackend::Vulkan)
    {
        MR_LOG(LogRHI, Log, "PreprocessShader: Vulkan target, no preprocessing needed");
        return source;
    }

    // OpenGL: convert Vulkan layout to OpenGL layout
    if (targetAPI == ERHIBackend::OpenGL)
    {
        MR_LOG(LogRHI, Log, "PreprocessShader: OpenGL target, converting Vulkan layout");
        return ConvertVulkanLayoutToOpenGL(source, maxBindingsPerSet);
    }

    // Other APIs: not supported yet
    MR_LOG(LogRHI, Warning, "PreprocessShader: Unsupported target API, returning original shader");
    return source;
}

FString FShaderPreprocessor::ConvertVulkanLayoutToOpenGL(
    const FString& source,
    uint32 maxBindingsPerSet)
{
    // Convert FString to std::string for regex processing
    std::string sourceStr = source.ToAnsiString();
    std::string outputStr;

    // Regex pattern to match: layout(set = X, binding = Y, ...)
    // Captures:
    //   [1] = set value
    //   [2] = binding value
    //   [3] = other qualifiers (e.g., ", std140")
    std::regex layoutRegex(
        R"(layout\s*\(\s*set\s*=\s*(\d+)\s*,\s*binding\s*=\s*(\d+)([^)]*)\))"
    );

    auto begin = std::sregex_iterator(sourceStr.begin(), sourceStr.end(), layoutRegex);
    auto end = std::sregex_iterator();

    size_t lastPos = 0;

    for (std::sregex_iterator i = begin; i != end; ++i)
    {
        std::smatch match = *i;

        // Extract set and binding
        uint32 set = std::stoi(match[1].str());
        uint32 binding = std::stoi(match[2].str());
        std::string otherQualifiers = match[3].str();

        // Calculate global binding using linear flatten formula
        uint32 globalBinding = set * maxBindingsPerSet + binding;

        MR_LOG(LogRHI, VeryVerbose, 
            "ConvertVulkanLayoutToOpenGL: (set=%u, binding=%u) -> globalBinding=%u",
            set, binding, globalBinding);

        // Append content before the match
        outputStr += sourceStr.substr(lastPos, match.position() - lastPos);

        // Replace with OpenGL syntax (remove 'set', keep 'binding')
        outputStr += "layout(binding = " + std::to_string(globalBinding) + otherQualifiers + ")";

        lastPos = match.position() + match.length();
    }

    // Append remaining content
    outputStr += sourceStr.substr(lastPos);

    // Convert back to FString
    return FString(MonsterEngine::StringConv::AnsiToWide(outputStr.c_str()).c_str());
}
