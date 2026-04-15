#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHIDefinitions.h"

namespace MonsterRender::RHI {

using MonsterEngine::FString;

/**
 * Shader preprocessor for converting Vulkan GLSL to OpenGL GLSL
 * 
 * This preprocessor removes the 'set' keyword from layout declarations
 * and recalculates binding values using the linear flatten formula:
 * globalBinding = set * MAX_BINDINGS_PER_SET + binding
 * 
 * Reference: Godot 4 shader preprocessing approach
 */
class FShaderPreprocessor
{
public:
    /**
     * Preprocess shader source code for the target API
     * 
     * @param source Shader source code (Vulkan GLSL syntax)
     * @param targetAPI Target graphics API (Vulkan or OpenGL)
     * @param maxBindingsPerSet Maximum bindings per descriptor set (default: 8)
     * @return Preprocessed shader source code
     */
    static FString PreprocessShader(
        const FString& source,
        ERHIBackend targetAPI,
        uint32 maxBindingsPerSet = RHILimits::MAX_BINDINGS_PER_SET
    );

private:
    /**
     * Convert Vulkan layout syntax to OpenGL layout syntax
     * 
     * Input:  layout(set = 0, binding = 4, std140)
     * Output: layout(binding = 4, std140)  // For OpenGL
     * 
     * @param source Shader source code
     * @param maxBindingsPerSet Maximum bindings per descriptor set
     * @return Converted shader source code
     */
    static FString ConvertVulkanLayoutToOpenGL(
        const FString& source,
        uint32 maxBindingsPerSet
    );
};

} // namespace MonsterRender::RHI
