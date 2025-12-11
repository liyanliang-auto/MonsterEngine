// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ForwardShaderCompiler.h
 * @brief Shader compilation utilities for forward rendering pipeline
 * 
 * Provides shader compilation from GLSL to SPIR-V for forward rendering passes.
 * 
 * Reference UE5: Engine/Source/Runtime/RenderCore/Private/ShaderCompiler.cpp
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Core/Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

// Forward declare RHI types
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
    class IRHIPipelineState;
    class IRHIShader;
}}

namespace MonsterEngine
{

// Forward declarations
class FShader;
class FVertexShader;
class FPixelShader;

// ============================================================================
// Forward Shader Types
// ============================================================================

/**
 * @enum EForwardShaderType
 * @brief Types of shaders used in forward rendering
 */
enum class EForwardShaderType : uint8
{
    DepthOnly,          // Depth prepass shader
    ForwardLit,         // Forward lighting shader (PBR)
    ForwardUnlit,       // Unlit forward shader
    ShadowDepth,        // Shadow map generation shader
    Skybox,             // Skybox rendering shader
    Transparent,        // Transparent object shader
    
    NumTypes
};

/**
 * Get shader type name for debugging
 */
inline const char* GetForwardShaderTypeName(EForwardShaderType Type)
{
    switch (Type)
    {
        case EForwardShaderType::DepthOnly:    return "DepthOnly";
        case EForwardShaderType::ForwardLit:   return "ForwardLit";
        case EForwardShaderType::ForwardUnlit: return "ForwardUnlit";
        case EForwardShaderType::ShadowDepth:  return "ShadowDepth";
        case EForwardShaderType::Skybox:       return "Skybox";
        case EForwardShaderType::Transparent:  return "Transparent";
        default:                               return "Unknown";
    }
}

// ============================================================================
// Forward Shader Permutation
// ============================================================================

/**
 * @struct FForwardShaderPermutation
 * @brief Shader permutation flags for forward rendering
 * 
 * Different combinations of features require different shader variants.
 * Reference UE5: FShaderPermutationParameters
 */
struct FForwardShaderPermutation
{
    /** Use normal mapping */
    uint32 bUseNormalMap : 1;
    
    /** Use PBR materials */
    uint32 bUsePBR : 1;
    
    /** Use shadow mapping */
    uint32 bUseShadows : 1;
    
    /** Use cascaded shadow maps */
    uint32 bUseCSM : 1;
    
    /** Use alpha testing */
    uint32 bUseAlphaTest : 1;
    
    /** Use skinned mesh */
    uint32 bUseSkinning : 1;
    
    /** Use instancing */
    uint32 bUseInstancing : 1;
    
    /** Maximum number of lights */
    uint32 MaxLights : 4;
    
    FForwardShaderPermutation()
        : bUseNormalMap(0)
        , bUsePBR(1)
        , bUseShadows(1)
        , bUseCSM(0)
        , bUseAlphaTest(0)
        , bUseSkinning(0)
        , bUseInstancing(0)
        , MaxLights(4)
    {
    }
    
    /** Get unique hash for this permutation */
    uint32 GetHash() const
    {
        return (bUseNormalMap << 0) |
               (bUsePBR << 1) |
               (bUseShadows << 2) |
               (bUseCSM << 3) |
               (bUseAlphaTest << 4) |
               (bUseSkinning << 5) |
               (bUseInstancing << 6) |
               (MaxLights << 7);
    }
    
    /** Get preprocessor definitions for this permutation */
    TArray<TPair<String, String>> GetDefinitions() const;
};

// ============================================================================
// Forward Shader Compiler
// ============================================================================

/**
 * @class FForwardShaderCompiler
 * @brief Compiles and caches shaders for forward rendering
 * 
 * Features:
 * - GLSL to SPIR-V compilation
 * - Shader permutation management
 * - Pipeline state caching
 * 
 * Reference UE5: FShaderCompilingManager
 */
class FForwardShaderCompiler
{
public:
    FForwardShaderCompiler();
    ~FForwardShaderCompiler();
    
    // Singleton access
    static FForwardShaderCompiler& Get();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initialize the shader compiler
     * @param Device RHI device for shader creation
     * @param ShaderDirectory Base directory for shader files
     * @return true if initialization succeeded
     */
    bool Initialize(MonsterRender::RHI::IRHIDevice* Device, const String& ShaderDirectory);
    
    /**
     * Shutdown and release resources
     */
    void Shutdown();
    
    /**
     * Check if initialized
     */
    bool IsInitialized() const { return m_initialized; }
    
    // ========================================================================
    // Shader Compilation
    // ========================================================================
    
    /**
     * Compile a forward shader with specified permutation
     * @param Type Shader type
     * @param Permutation Shader permutation flags
     * @return Pipeline state, or nullptr on failure
     */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> CompileForwardShader(
        EForwardShaderType Type,
        const FForwardShaderPermutation& Permutation = FForwardShaderPermutation());
    
    /**
     * Get cached pipeline state for a shader type and permutation
     * @param Type Shader type
     * @param Permutation Shader permutation flags
     * @return Cached pipeline state, or nullptr if not found
     */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> GetCachedPipeline(
        EForwardShaderType Type,
        const FForwardShaderPermutation& Permutation = FForwardShaderPermutation()) const;
    
    /**
     * Precompile all common shader permutations
     * @return Number of shaders compiled
     */
    int32 PrecompileShaders();
    
    /**
     * Clear all cached shaders
     */
    void ClearCache();
    
    // ========================================================================
    // Shader Paths
    // ========================================================================
    
    /**
     * Get the shader file path for a shader type
     * @param Type Shader type
     * @param bVertex True for vertex shader, false for fragment shader
     * @return Shader file path
     */
    String GetShaderPath(EForwardShaderType Type, bool bVertex) const;
    
    /**
     * Set the base shader directory
     */
    void SetShaderDirectory(const String& Path) { m_shaderDirectory = Path; }
    
    /**
     * Get the base shader directory
     */
    const String& GetShaderDirectory() const { return m_shaderDirectory; }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    /**
     * Get number of cached pipelines
     */
    int32 GetCachedPipelineCount() const { return m_pipelineCache.Num(); }
    
    /**
     * Get total compilation time
     */
    float GetTotalCompilationTime() const { return m_totalCompilationTime; }
    
protected:
    /**
     * Compile vertex and fragment shaders
     */
    bool CompileShaderPair(
        const String& VertexPath,
        const String& FragmentPath,
        const TArray<TPair<String, String>>& Definitions,
        TSharedPtr<MonsterRender::RHI::IRHIShader>& OutVertexShader,
        TSharedPtr<MonsterRender::RHI::IRHIShader>& OutFragmentShader);
    
    /**
     * Create pipeline state from shaders
     */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> CreatePipelineState(
        EForwardShaderType Type,
        TSharedPtr<MonsterRender::RHI::IRHIShader> VertexShader,
        TSharedPtr<MonsterRender::RHI::IRHIShader> FragmentShader);
    
    /**
     * Get cache key for shader type and permutation
     */
    uint64 GetCacheKey(EForwardShaderType Type, const FForwardShaderPermutation& Permutation) const;
    
protected:
    /** RHI device */
    MonsterRender::RHI::IRHIDevice* m_device = nullptr;
    
    /** Base shader directory */
    String m_shaderDirectory;
    
    /** Pipeline cache (key -> pipeline state) */
    TMap<uint64, TSharedPtr<MonsterRender::RHI::IRHIPipelineState>> m_pipelineCache;
    
    /** Whether initialized */
    bool m_initialized = false;
    
    /** Statistics */
    float m_totalCompilationTime = 0.0f;
    int32 m_compiledShaderCount = 0;
    
    /** Singleton instance */
    static FForwardShaderCompiler* s_instance;
};

} // namespace MonsterEngine

