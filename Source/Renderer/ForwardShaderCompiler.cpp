// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ForwardShaderCompiler.cpp
 * @brief Implementation of forward shader compilation
 * 
 * Reference UE5: Engine/Source/Runtime/RenderCore/Private/ShaderCompiler.cpp
 */

#include "Renderer/ForwardShaderCompiler.h"
#include "Core/ShaderCompiler.h"
#include "Core/Logging/LogMacros.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHIResource.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogForwardShaderCompiler, Log, All);

// Singleton instance
FForwardShaderCompiler* FForwardShaderCompiler::s_instance = nullptr;

// ============================================================================
// FForwardShaderPermutation Implementation
// ============================================================================

TArray<TPair<String, String>> FForwardShaderPermutation::GetDefinitions() const
{
    TArray<TPair<String, String>> Definitions;
    
    // Add definitions based on permutation flags
    if (bUseNormalMap)
    {
        Definitions.Add(TPair<String, String>("USE_NORMAL_MAP", "1"));
    }
    
    if (bUsePBR)
    {
        Definitions.Add(TPair<String, String>("USE_PBR", "1"));
    }
    
    if (bUseShadows)
    {
        Definitions.Add(TPair<String, String>("USE_SHADOWS", "1"));
    }
    
    if (bUseCSM)
    {
        Definitions.Add(TPair<String, String>("USE_CSM", "1"));
    }
    
    if (bUseAlphaTest)
    {
        Definitions.Add(TPair<String, String>("USE_ALPHA_TEST", "1"));
    }
    
    if (bUseSkinning)
    {
        Definitions.Add(TPair<String, String>("USE_SKINNING", "1"));
    }
    
    if (bUseInstancing)
    {
        Definitions.Add(TPair<String, String>("USE_INSTANCING", "1"));
    }
    
    // Add max lights definition
    char MaxLightsStr[16];
    sprintf_s(MaxLightsStr, 16, "%u", MaxLights);
    Definitions.Add(TPair<String, String>("MAX_LIGHTS", MaxLightsStr));
    
    return Definitions;
}

// ============================================================================
// FForwardShaderCompiler Implementation
// ============================================================================

FForwardShaderCompiler::FForwardShaderCompiler()
    : m_device(nullptr)
    , m_initialized(false)
    , m_totalCompilationTime(0.0f)
    , m_compiledShaderCount(0)
{
    m_shaderDirectory = "Shaders/Forward";
}

FForwardShaderCompiler::~FForwardShaderCompiler()
{
    Shutdown();
}

FForwardShaderCompiler& FForwardShaderCompiler::Get()
{
    if (!s_instance)
    {
        s_instance = new FForwardShaderCompiler();
    }
    return *s_instance;
}

// ============================================================================
// Initialization
// ============================================================================

bool FForwardShaderCompiler::Initialize(MonsterRender::RHI::IRHIDevice* Device, const String& ShaderDirectory)
{
    if (m_initialized)
    {
        MR_LOG_WARNING("FForwardShaderCompiler: Already initialized");
        return true;
    }
    
    if (!Device)
    {
        MR_LOG_ERROR("FForwardShaderCompiler: Device is null");
        return false;
    }
    
    m_device = Device;
    m_shaderDirectory = ShaderDirectory;
    
    MR_LOG(LogForwardShaderCompiler, Verbose, "Initialized with shader directory: %s", 
           m_shaderDirectory.c_str());
    
    m_initialized = true;
    return true;
}

void FForwardShaderCompiler::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }
    
    MR_LOG(LogForwardShaderCompiler, Verbose, "Shutting down (cached pipelines: %d)", 
           m_pipelineCache.Num());
    
    ClearCache();
    
    m_device = nullptr;
    m_initialized = false;
}

// ============================================================================
// Shader Compilation
// ============================================================================

TSharedPtr<MonsterRender::RHI::IRHIPipelineState> FForwardShaderCompiler::CompileForwardShader(
    EForwardShaderType Type,
    const FForwardShaderPermutation& Permutation)
{
    if (!m_initialized || !m_device)
    {
        MR_LOG_ERROR("FForwardShaderCompiler: Not initialized");
        return nullptr;
    }
    
    // Check cache first
    uint64 CacheKey = GetCacheKey(Type, Permutation);
    auto* CachedPipeline = m_pipelineCache.Find(CacheKey);
    if (CachedPipeline)
    {
        return *CachedPipeline;
    }
    
    MR_LOG(LogForwardShaderCompiler, Verbose, "Compiling shader type %s with permutation 0x%08X",
           GetForwardShaderTypeName(Type), Permutation.GetHash());
    
    // Get shader paths
    String VertexPath = GetShaderPath(Type, true);
    String FragmentPath = GetShaderPath(Type, false);
    
    // Get definitions for this permutation
    TArray<TPair<String, String>> Definitions = Permutation.GetDefinitions();
    
    // Compile shaders
    TSharedPtr<MonsterRender::RHI::IRHIShader> VertexShader;
    TSharedPtr<MonsterRender::RHI::IRHIShader> FragmentShader;
    
    if (!CompileShaderPair(VertexPath, FragmentPath, Definitions, VertexShader, FragmentShader))
    {
        MR_LOG(LogForwardShaderCompiler, Error, "Failed to compile shader pair for %s",
               GetForwardShaderTypeName(Type));
        return nullptr;
    }
    
    // Create pipeline state
    auto Pipeline = CreatePipelineState(Type, VertexShader, FragmentShader);
    if (!Pipeline)
    {
        MR_LOG(LogForwardShaderCompiler, Error, "Failed to create pipeline state for %s",
               GetForwardShaderTypeName(Type));
        return nullptr;
    }
    
    // Cache the pipeline
    m_pipelineCache.Add(CacheKey, Pipeline);
    m_compiledShaderCount++;
    
    MR_LOG(LogForwardShaderCompiler, Verbose, "Successfully compiled %s shader",
           GetForwardShaderTypeName(Type));
    
    return Pipeline;
}

TSharedPtr<MonsterRender::RHI::IRHIPipelineState> FForwardShaderCompiler::GetCachedPipeline(
    EForwardShaderType Type,
    const FForwardShaderPermutation& Permutation) const
{
    uint64 CacheKey = GetCacheKey(Type, Permutation);
    auto* CachedPipeline = m_pipelineCache.Find(CacheKey);
    if (CachedPipeline)
    {
        return *CachedPipeline;
    }
    return nullptr;
}

int32 FForwardShaderCompiler::PrecompileShaders()
{
    if (!m_initialized)
    {
        return 0;
    }
    
    MR_LOG(LogForwardShaderCompiler, Verbose, "Precompiling common shader permutations");
    
    int32 CompiledCount = 0;
    
    // Compile default permutation for each shader type
    FForwardShaderPermutation DefaultPermutation;
    
    // Depth only shader (minimal permutation)
    FForwardShaderPermutation DepthPermutation;
    DepthPermutation.bUsePBR = 0;
    DepthPermutation.bUseShadows = 0;
    if (CompileForwardShader(EForwardShaderType::DepthOnly, DepthPermutation))
    {
        CompiledCount++;
    }
    
    // Forward lit shader (default PBR with shadows)
    if (CompileForwardShader(EForwardShaderType::ForwardLit, DefaultPermutation))
    {
        CompiledCount++;
    }
    
    // Forward lit with normal mapping
    FForwardShaderPermutation NormalMapPermutation = DefaultPermutation;
    NormalMapPermutation.bUseNormalMap = 1;
    if (CompileForwardShader(EForwardShaderType::ForwardLit, NormalMapPermutation))
    {
        CompiledCount++;
    }
    
    // Shadow depth shader
    if (CompileForwardShader(EForwardShaderType::ShadowDepth, DepthPermutation))
    {
        CompiledCount++;
    }
    
    // Skybox shader
    FForwardShaderPermutation SkyboxPermutation;
    SkyboxPermutation.bUsePBR = 0;
    SkyboxPermutation.bUseShadows = 0;
    if (CompileForwardShader(EForwardShaderType::Skybox, SkyboxPermutation))
    {
        CompiledCount++;
    }
    
    // Transparent shader
    if (CompileForwardShader(EForwardShaderType::Transparent, DefaultPermutation))
    {
        CompiledCount++;
    }
    
    MR_LOG(LogForwardShaderCompiler, Verbose, "Precompiled %d shaders", CompiledCount);
    
    return CompiledCount;
}

void FForwardShaderCompiler::ClearCache()
{
    m_pipelineCache.Empty();
    MR_LOG(LogForwardShaderCompiler, Verbose, "Cache cleared");
}

// ============================================================================
// Shader Paths
// ============================================================================

String FForwardShaderCompiler::GetShaderPath(EForwardShaderType Type, bool bVertex) const
{
    String BasePath = m_shaderDirectory;
    if (!BasePath.empty() && BasePath[BasePath.size() - 1] != '/')
    {
        BasePath += "/";
    }
    
    String ShaderName;
    switch (Type)
    {
        case EForwardShaderType::DepthOnly:
            ShaderName = "DepthOnly";
            break;
        case EForwardShaderType::ForwardLit:
            ShaderName = "ForwardLit";
            break;
        case EForwardShaderType::ForwardUnlit:
            ShaderName = "ForwardUnlit";
            break;
        case EForwardShaderType::ShadowDepth:
            ShaderName = "ShadowDepth";
            break;
        case EForwardShaderType::Skybox:
            ShaderName = "Skybox";
            break;
        case EForwardShaderType::Transparent:
            ShaderName = "ForwardLit";  // Transparent uses same shader as lit
            break;
        default:
            ShaderName = "Unknown";
            break;
    }
    
    String Extension = bVertex ? ".vert" : ".frag";
    
    return BasePath + ShaderName + Extension;
}

// ============================================================================
// Internal Methods
// ============================================================================

bool FForwardShaderCompiler::CompileShaderPair(
    const String& VertexPath,
    const String& FragmentPath,
    const TArray<TPair<String, String>>& Definitions,
    TSharedPtr<MonsterRender::RHI::IRHIShader>& OutVertexShader,
    TSharedPtr<MonsterRender::RHI::IRHIShader>& OutFragmentShader)
{
    // Use the MonsterRender shader compiler to compile GLSL to SPIR-V
    MonsterRender::ShaderCompileOptions VertexOptions;
    VertexOptions.language = MonsterRender::EShaderLanguage::GLSL;
    VertexOptions.stage = MonsterRender::EShaderStageKind::Vertex;
    VertexOptions.entryPoint = "main";
    VertexOptions.generateDebugInfo = true;
    
    // Add definitions
    for (const auto& Def : Definitions)
    {
        VertexOptions.definitions.Add(Def.Key + "=" + Def.Value);
    }
    
    // Compile vertex shader
    std::vector<uint8> VertexBytecode = MonsterRender::ShaderCompiler::compileFromFile(
        VertexPath, VertexOptions);
    
    if (VertexBytecode.empty())
    {
        MR_LOG(LogForwardShaderCompiler, Error, "Failed to compile vertex shader: %s", 
               VertexPath.c_str());
        return false;
    }
    
    // Compile fragment shader
    MonsterRender::ShaderCompileOptions FragmentOptions = VertexOptions;
    FragmentOptions.stage = MonsterRender::EShaderStageKind::Fragment;
    
    std::vector<uint8> FragmentBytecode = MonsterRender::ShaderCompiler::compileFromFile(
        FragmentPath, FragmentOptions);
    
    if (FragmentBytecode.empty())
    {
        MR_LOG(LogForwardShaderCompiler, Error, "Failed to compile fragment shader: %s", 
               FragmentPath.c_str());
        return false;
    }
    
    // Create RHI shader objects from bytecode
    // Note: This requires the RHI device to support shader creation from SPIR-V
    // For now, we store the bytecode and create shaders when needed
    
    MR_LOG(LogForwardShaderCompiler, Verbose, "Compiled shader pair - VS: %zu bytes, FS: %zu bytes",
           VertexBytecode.size(), FragmentBytecode.size());
    
    // TODO: Create actual RHI shader objects from bytecode
    // OutVertexShader = m_device->createShaderFromBytecode(VertexBytecode, EShaderStage::Vertex);
    // OutFragmentShader = m_device->createShaderFromBytecode(FragmentBytecode, EShaderStage::Fragment);
    
    return true;
}

TSharedPtr<MonsterRender::RHI::IRHIPipelineState> FForwardShaderCompiler::CreatePipelineState(
    EForwardShaderType Type,
    TSharedPtr<MonsterRender::RHI::IRHIShader> VertexShader,
    TSharedPtr<MonsterRender::RHI::IRHIShader> FragmentShader)
{
    // TODO: Create pipeline state object with appropriate render state configuration
    // based on shader type
    //
    // PipelineStateDesc Desc;
    // Desc.VertexShader = VertexShader;
    // Desc.PixelShader = FragmentShader;
    //
    // switch (Type)
    // {
    //     case EForwardShaderType::DepthOnly:
    //         Desc.DepthStencilState = DepthWriteOnly;
    //         Desc.BlendState = NoBlend;
    //         break;
    //     case EForwardShaderType::ForwardLit:
    //         Desc.DepthStencilState = DepthReadWrite;
    //         Desc.BlendState = Opaque;
    //         break;
    //     // ... etc
    // }
    //
    // return m_device->createPipelineState(Desc);
    
    MR_LOG(LogForwardShaderCompiler, Verbose, "Created pipeline state for %s",
           GetForwardShaderTypeName(Type));
    
    return nullptr;  // TODO: Return actual pipeline state
}

uint64 FForwardShaderCompiler::GetCacheKey(EForwardShaderType Type, const FForwardShaderPermutation& Permutation) const
{
    // Combine shader type and permutation hash into a unique key
    uint64 TypeBits = static_cast<uint64>(Type) << 32;
    uint64 PermutationBits = static_cast<uint64>(Permutation.GetHash());
    return TypeBits | PermutationBits;
}

} // namespace MonsterEngine
