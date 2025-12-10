#pragma once

/**
 * @file Shader.h
 * @brief Shader class definitions
 * 
 * FShader wraps RHI shader objects and provides a higher-level interface
 * for shader management, compilation, and parameter binding.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Public/Shader.h
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Core/Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

// Forward declare RHI types
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
    class IRHIShader;
    class IRHIVertexShader;
    class IRHIPixelShader;
    enum class EShaderStage : uint8;
}}

namespace MonsterEngine
{

// Forward declarations
class FShaderManager;

// ============================================================================
// Shader Stage Enumeration
// ============================================================================

/**
 * @enum EShaderFrequency
 * @brief Shader stage/frequency types
 * 
 * Reference UE5: EShaderFrequency
 */
enum class EShaderFrequency : uint8
{
    Vertex,         // Vertex shader
    Hull,           // Hull/Tessellation control shader
    Domain,         // Domain/Tessellation evaluation shader
    Geometry,       // Geometry shader
    Pixel,          // Pixel/Fragment shader
    Compute,        // Compute shader
    
    NumFrequencies
};

/**
 * Convert EShaderFrequency to string
 */
inline const TCHAR* GetShaderFrequencyName(EShaderFrequency Frequency)
{
    switch (Frequency)
    {
        case EShaderFrequency::Vertex:   return TEXT("Vertex");
        case EShaderFrequency::Hull:     return TEXT("Hull");
        case EShaderFrequency::Domain:   return TEXT("Domain");
        case EShaderFrequency::Geometry: return TEXT("Geometry");
        case EShaderFrequency::Pixel:    return TEXT("Pixel");
        case EShaderFrequency::Compute:  return TEXT("Compute");
        default:                         return TEXT("Unknown");
    }
}

// ============================================================================
// Shader Parameter Types
// ============================================================================

/**
 * @enum EShaderParameterType
 * @brief Types of shader parameters
 */
enum class EShaderParameterType : uint8
{
    Unknown,
    UniformBuffer,      // Constant/Uniform buffer
    Texture,            // Texture resource
    Sampler,            // Sampler state
    UAV,                // Unordered access view
    SRV,                // Shader resource view
    
    NumTypes
};

/**
 * @struct FShaderParameterInfo
 * @brief Information about a shader parameter binding
 */
struct FShaderParameterInfo
{
    /** Parameter name */
    FName Name;
    
    /** Parameter type */
    EShaderParameterType Type = EShaderParameterType::Unknown;
    
    /** Binding slot/register */
    uint32 BindingSlot = 0;
    
    /** Binding set (for Vulkan descriptor sets) */
    uint32 BindingSet = 0;
    
    /** Size in bytes (for uniform buffers) */
    uint32 Size = 0;
    
    /** Array count (1 for non-arrays) */
    uint32 ArrayCount = 1;
    
    FShaderParameterInfo() = default;
    
    FShaderParameterInfo(const FName& InName, EShaderParameterType InType, uint32 InSlot)
        : Name(InName)
        , Type(InType)
        , BindingSlot(InSlot)
    {}
};

/**
 * @struct FShaderUniformBufferParameter
 * @brief Uniform buffer parameter with member info
 */
struct FShaderUniformBufferParameter
{
    /** Buffer name */
    FName BufferName;
    
    /** Binding slot */
    uint32 BindingSlot = 0;
    
    /** Total buffer size */
    uint32 BufferSize = 0;
    
    /** Member parameters within the buffer */
    TArray<FShaderParameterInfo> Members;
};

// ============================================================================
// Shader Compile Options
// ============================================================================

/**
 * @struct FShaderCompileOptions
 * @brief Options for shader compilation
 */
struct FShaderCompileOptions
{
    /** Shader source file path */
    String SourcePath;
    
    /** Entry point function name */
    String EntryPoint = TEXT("main");
    
    /** Shader stage */
    EShaderFrequency Frequency = EShaderFrequency::Vertex;
    
    /** Preprocessor definitions */
    TArray<TPair<String, String>> Definitions;
    
    /** Include paths */
    TArray<String> IncludePaths;
    
    /** Generate debug info */
    bool bGenerateDebugInfo = true;
    
    /** Optimize shader */
    bool bOptimize = true;
    
    /** Target shader model (e.g., "5_0", "6_0") */
    String ShaderModel = TEXT("5_0");
    
    /** Add a preprocessor definition */
    void AddDefinition(const String& Name, const String& Value = TEXT("1"))
    {
        Definitions.Add(MakePair(Name, Value));
    }
};

// ============================================================================
// FShader Base Class
// ============================================================================

/**
 * @class FShader
 * @brief Base class for all shader types
 * 
 * Wraps RHI shader objects and provides:
 * - Shader compilation
 * - Parameter reflection
 * - Caching support
 * 
 * Reference UE5: FShader
 */
class FShader
{
public:
    FShader();
    FShader(EShaderFrequency InFrequency);
    virtual ~FShader();
    
    // Non-copyable
    FShader(const FShader&) = delete;
    FShader& operator=(const FShader&) = delete;
    
    // ========================================================================
    // Shader Properties
    // ========================================================================
    
    /**
     * Get shader frequency/stage
     */
    EShaderFrequency GetFrequency() const { return m_frequency; }
    
    /**
     * Get shader name
     */
    const FName& GetShaderName() const { return m_shaderName; }
    
    /**
     * Set shader name
     */
    void SetShaderName(const FName& Name) { m_shaderName = Name; }
    
    /**
     * Get source file path
     */
    const String& GetSourcePath() const { return m_sourcePath; }
    
    /**
     * Check if shader is valid/compiled
     */
    bool IsValid() const { return m_isValid; }
    
    // ========================================================================
    // RHI Shader Access
    // ========================================================================
    
    /**
     * Get the underlying RHI shader
     */
    TSharedPtr<MonsterRender::RHI::IRHIShader> GetRHIShader() const { return m_rhiShader; }
    
    /**
     * Get as vertex shader (returns null if not vertex shader)
     */
    TSharedPtr<MonsterRender::RHI::IRHIVertexShader> GetVertexShader() const;
    
    /**
     * Get as pixel shader (returns null if not pixel shader)
     */
    TSharedPtr<MonsterRender::RHI::IRHIPixelShader> GetPixelShader() const;
    
    // ========================================================================
    // Compilation
    // ========================================================================
    
    /**
     * Compile shader from source file
     * @param Device RHI device
     * @param Options Compile options
     * @return true if compilation succeeded
     */
    bool Compile(MonsterRender::RHI::IRHIDevice* Device, const FShaderCompileOptions& Options);
    
    /**
     * Compile shader from bytecode
     * @param Device RHI device
     * @param Bytecode SPIR-V or DXBC bytecode
     * @param Frequency Shader stage
     * @return true if creation succeeded
     */
    bool CreateFromBytecode(MonsterRender::RHI::IRHIDevice* Device, 
                            TSpan<const uint8> Bytecode,
                            EShaderFrequency Frequency);
    
    // ========================================================================
    // Parameter Reflection
    // ========================================================================
    
    /**
     * Get all parameter bindings
     */
    const TArray<FShaderParameterInfo>& GetParameters() const { return m_parameters; }
    
    /**
     * Get uniform buffer parameters
     */
    const TArray<FShaderUniformBufferParameter>& GetUniformBuffers() const { return m_uniformBuffers; }
    
    /**
     * Find parameter by name
     * @param Name Parameter name
     * @return Parameter info, or nullptr if not found
     */
    const FShaderParameterInfo* FindParameter(const FName& Name) const;
    
    /**
     * Find uniform buffer by name
     * @param Name Buffer name
     * @return Buffer info, or nullptr if not found
     */
    const FShaderUniformBufferParameter* FindUniformBuffer(const FName& Name) const;
    
    // ========================================================================
    // Bytecode Access
    // ========================================================================
    
    /**
     * Get compiled bytecode
     */
    const TArray<uint8>& GetBytecode() const { return m_bytecode; }
    
    /**
     * Get bytecode hash for caching
     */
    uint64 GetBytecodeHash() const { return m_bytecodeHash; }
    
protected:
    /**
     * Perform shader reflection to extract parameter info
     */
    void PerformReflection();
    
    /**
     * Calculate bytecode hash
     */
    void CalculateBytecodeHash();
    
protected:
    /** Shader frequency/stage */
    EShaderFrequency m_frequency = EShaderFrequency::Vertex;
    
    /** Shader name */
    FName m_shaderName;
    
    /** Source file path */
    String m_sourcePath;
    
    /** Compiled bytecode */
    TArray<uint8> m_bytecode;
    
    /** Bytecode hash for caching */
    uint64 m_bytecodeHash = 0;
    
    /** RHI shader object */
    TSharedPtr<MonsterRender::RHI::IRHIShader> m_rhiShader;
    
    /** Parameter bindings from reflection */
    TArray<FShaderParameterInfo> m_parameters;
    
    /** Uniform buffer parameters */
    TArray<FShaderUniformBufferParameter> m_uniformBuffers;
    
    /** Whether shader is valid */
    bool m_isValid = false;
};

// ============================================================================
// Specialized Shader Types
// ============================================================================

/**
 * @class FVertexShader
 * @brief Vertex shader specialization
 */
class FVertexShader : public FShader
{
public:
    FVertexShader() : FShader(EShaderFrequency::Vertex) {}
};

/**
 * @class FPixelShader
 * @brief Pixel/Fragment shader specialization
 */
class FPixelShader : public FShader
{
public:
    FPixelShader() : FShader(EShaderFrequency::Pixel) {}
};

/**
 * @class FComputeShader
 * @brief Compute shader specialization
 */
class FComputeShader : public FShader
{
public:
    FComputeShader() : FShader(EShaderFrequency::Compute) {}
};

// ============================================================================
// Shader Refs
// ============================================================================

using FShaderRef = TSharedPtr<FShader>;
using FVertexShaderRef = TSharedPtr<FVertexShader>;
using FPixelShaderRef = TSharedPtr<FPixelShader>;
using FComputeShaderRef = TSharedPtr<FComputeShader>;

} // namespace MonsterEngine
