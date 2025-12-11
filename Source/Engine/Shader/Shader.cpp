/**
 * @file Shader.cpp
 * @brief Implementation of FShader class
 */

#include "Engine/Shader/Shader.h"
#include "Core/Logging/LogMacros.h"
#include "Core/ShaderCompiler.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHIResource.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogShader, Log, All);

// ============================================================================
// FShader Implementation
// ============================================================================

FShader::FShader()
    : m_frequency(EShaderFrequency::Vertex)
    , m_bytecodeHash(0)
    , m_isValid(false)
{
}

FShader::FShader(EShaderFrequency InFrequency)
    : m_frequency(InFrequency)
    , m_bytecodeHash(0)
    , m_isValid(false)
{
}

FShader::~FShader()
{
    MR_LOG_DEBUG("FShader: Destroying shader '%s'", *m_shaderName.ToString());
    m_rhiShader.Reset();
}

// ============================================================================
// RHI Shader Access
// ============================================================================

TSharedPtr<MonsterRender::RHI::IRHIVertexShader> FShader::GetVertexShader() const
{
    if (m_frequency == EShaderFrequency::Vertex && m_rhiShader)
    {
        // Cast to vertex shader
        return StaticCastSharedPtr<MonsterRender::RHI::IRHIVertexShader>(m_rhiShader);
    }
    return nullptr;
}

TSharedPtr<MonsterRender::RHI::IRHIPixelShader> FShader::GetPixelShader() const
{
    if (m_frequency == EShaderFrequency::Pixel && m_rhiShader)
    {
        // Cast to pixel shader
        return StaticCastSharedPtr<MonsterRender::RHI::IRHIPixelShader>(m_rhiShader);
    }
    return nullptr;
}

// ============================================================================
// Compilation
// ============================================================================

bool FShader::Compile(MonsterRender::RHI::IRHIDevice* Device, const FShaderCompileOptions& Options)
{
    if (!Device)
    {
        MR_LOG_ERROR("FShader::Compile: Device is null");
        return false;
    }
    
    MR_LOG_DEBUG("FShader::Compile: Compiling shader '%s' from '%s'", 
                 *m_shaderName.ToString(), *Options.SourcePath);
    
    // Store source path
    m_sourcePath = Options.SourcePath;
    m_frequency = Options.Frequency;
    
    // Convert to MonsterRender shader options
    MonsterRender::ShaderCompileOptions CompileOptions;
    CompileOptions.language = MonsterRender::EShaderLanguage::GLSL;
    CompileOptions.entryPoint = Options.EntryPoint;
    CompileOptions.generateDebugInfo = Options.bGenerateDebugInfo;
    
    // Set shader stage
    switch (Options.Frequency)
    {
        case EShaderFrequency::Vertex:
            CompileOptions.stage = MonsterRender::EShaderStageKind::Vertex;
            break;
        case EShaderFrequency::Pixel:
            CompileOptions.stage = MonsterRender::EShaderStageKind::Fragment;
            break;
        default:
            MR_LOG_ERROR("FShader::Compile: Unsupported shader frequency");
            return false;
    }
    
    // Add definitions
    for (const auto& Def : Options.Definitions)
    {
        CompileOptions.definitions.Add(Def.Key + "=" + Def.Value);
    }
    
    // Compile shader
    std::vector<uint8> Bytecode = MonsterRender::ShaderCompiler::compileFromFile(
        Options.SourcePath, CompileOptions);
    
    if (Bytecode.empty())
    {
        MR_LOG_ERROR("FShader::Compile: Failed to compile shader");
        m_isValid = false;
        return false;
    }
    
    // Store bytecode
    m_bytecode.Empty();
    m_bytecode.Reserve(static_cast<int32>(Bytecode.size()));
    for (uint8 Byte : Bytecode)
    {
        m_bytecode.Add(Byte);
    }
    
    // Calculate hash
    CalculateBytecodeHash();
    
    // Create RHI shader
    TSpan<const uint8> BytecodeSpan(m_bytecode.GetData(), m_bytecode.Num());
    
    switch (m_frequency)
    {
        case EShaderFrequency::Vertex:
            m_rhiShader = Device->createVertexShader(BytecodeSpan);
            break;
        case EShaderFrequency::Pixel:
            m_rhiShader = Device->createPixelShader(BytecodeSpan);
            break;
        default:
            MR_LOG_ERROR("FShader::Compile: Unsupported shader frequency for RHI creation");
            return false;
    }
    
    if (!m_rhiShader)
    {
        MR_LOG_ERROR("FShader::Compile: Failed to create RHI shader");
        m_isValid = false;
        return false;
    }
    
    // Perform reflection
    PerformReflection();
    
    m_isValid = true;
    MR_LOG_DEBUG("FShader::Compile: Successfully compiled shader '%s' (%d bytes)", 
                 *m_shaderName.ToString(), m_bytecode.Num());
    
    return true;
}

bool FShader::CreateFromBytecode(MonsterRender::RHI::IRHIDevice* Device,
                                  TSpan<const uint8> Bytecode,
                                  EShaderFrequency Frequency)
{
    if (!Device)
    {
        MR_LOG_ERROR("FShader::CreateFromBytecode: Device is null");
        return false;
    }
    
    if (Bytecode.empty())
    {
        MR_LOG_ERROR("FShader::CreateFromBytecode: Bytecode is empty");
        return false;
    }
    
    m_frequency = Frequency;
    
    // Store bytecode
    m_bytecode.Empty();
    m_bytecode.Reserve(static_cast<int32>(Bytecode.size()));
    for (size_t i = 0; i < Bytecode.size(); ++i)
    {
        m_bytecode.Add(Bytecode[i]);
    }
    
    // Calculate hash
    CalculateBytecodeHash();
    
    // Create RHI shader
    switch (m_frequency)
    {
        case EShaderFrequency::Vertex:
            m_rhiShader = Device->createVertexShader(Bytecode);
            break;
        case EShaderFrequency::Pixel:
            m_rhiShader = Device->createPixelShader(Bytecode);
            break;
        default:
            MR_LOG_ERROR("FShader::CreateFromBytecode: Unsupported shader frequency");
            return false;
    }
    
    if (!m_rhiShader)
    {
        MR_LOG_ERROR("FShader::CreateFromBytecode: Failed to create RHI shader");
        m_isValid = false;
        return false;
    }
    
    // Perform reflection
    PerformReflection();
    
    m_isValid = true;
    MR_LOG_DEBUG("FShader::CreateFromBytecode: Created shader from bytecode (%d bytes)", 
                 m_bytecode.Num());
    
    return true;
}

// ============================================================================
// Parameter Reflection
// ============================================================================

const FShaderParameterInfo* FShader::FindParameter(const FName& Name) const
{
    for (const auto& Param : m_parameters)
    {
        if (Param.Name == Name)
        {
            return &Param;
        }
    }
    return nullptr;
}

const FShaderUniformBufferParameter* FShader::FindUniformBuffer(const FName& Name) const
{
    for (const auto& Buffer : m_uniformBuffers)
    {
        if (Buffer.BufferName == Name)
        {
            return &Buffer;
        }
    }
    return nullptr;
}

void FShader::PerformReflection()
{
    // Clear existing reflection data
    m_parameters.Empty();
    m_uniformBuffers.Empty();
    
    // TODO: Implement SPIR-V reflection using spirv-cross or similar
    // For now, we'll rely on manual parameter registration
    
    MR_LOG_DEBUG("FShader::PerformReflection: Reflection not yet implemented");
}

void FShader::CalculateBytecodeHash()
{
    // Simple FNV-1a hash
    m_bytecodeHash = 14695981039346656037ULL;
    for (int32 i = 0; i < m_bytecode.Num(); ++i)
    {
        m_bytecodeHash ^= m_bytecode[i];
        m_bytecodeHash *= 1099511628211ULL;
    }
}

} // namespace MonsterEngine
