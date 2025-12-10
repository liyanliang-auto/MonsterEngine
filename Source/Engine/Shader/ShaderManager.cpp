/**
 * @file ShaderManager.cpp
 * @brief Implementation of FShaderManager class
 */

#include "Engine/Shader/ShaderManager.h"
#include "Engine/Shader/Shader.h"
#include "Core/Logging/LogMacros.h"
#include "Core/ShaderCompiler.h"
#include "RHI/IRHIDevice.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogShaderManager, Log, All);

// Singleton instance
FShaderManager* FShaderManager::s_instance = nullptr;

// ============================================================================
// FShaderManager Implementation
// ============================================================================

FShaderManager::FShaderManager()
    : m_device(nullptr)
    , m_hotReloadEnabled(true)
    , m_initialized(false)
    , m_totalCompilationTime(0.0f)
    , m_successfulCompilations(0)
    , m_failedCompilations(0)
{
    // Set default shader directory
    m_shaderDirectory = TEXT("Shaders");
    m_cacheDirectory = TEXT("ShaderCache");
}

FShaderManager::~FShaderManager()
{
    Shutdown();
}

FShaderManager& FShaderManager::Get()
{
    if (!s_instance)
    {
        s_instance = new FShaderManager();
    }
    return *s_instance;
}

// ============================================================================
// Initialization
// ============================================================================

bool FShaderManager::Initialize(MonsterRender::RHI::IRHIDevice* Device)
{
    if (m_initialized)
    {
        MR_LOG_WARNING("FShaderManager: Already initialized");
        return true;
    }
    
    if (!Device)
    {
        MR_LOG_ERROR("FShaderManager: Device is null");
        return false;
    }
    
    m_device = Device;
    
    // Add default include paths
    AddIncludePath(m_shaderDirectory);
    AddIncludePath(m_shaderDirectory + TEXT("/Common"));
    
    // Create default shaders
    if (!CreateDefaultShaders())
    {
        MR_LOG_WARNING("FShaderManager: Failed to create default shaders");
        // Continue anyway - default shaders are optional
    }
    
    m_initialized = true;
    MR_LOG_DEBUG("FShaderManager: Initialized");
    
    return true;
}

void FShaderManager::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }
    
    MR_LOG_DEBUG("FShaderManager: Shutting down (cached shaders: %d)", m_shaderCache.Num());
    
    // Clear all cached shaders
    ClearCache();
    
    // Clear default shaders
    m_defaultVertexShader.Reset();
    m_defaultPixelShader.Reset();
    m_errorShader.Reset();
    
    m_device = nullptr;
    m_initialized = false;
    
    MR_LOG_DEBUG("FShaderManager: Shutdown complete");
}

// ============================================================================
// Shader Compilation
// ============================================================================

TSharedPtr<FVertexShader> FShaderManager::CompileVertexShader(
    const String& SourcePath,
    const String& EntryPoint,
    const TArray<TPair<String, String>>& Definitions)
{
    FShaderCompileOptions Options;
    Options.SourcePath = ResolveShaderPath(SourcePath);
    Options.EntryPoint = EntryPoint;
    Options.Frequency = EShaderFrequency::Vertex;
    Options.Definitions = Definitions;
    Options.IncludePaths = m_includePaths;
    
    auto Shader = CompileShaderInternal(Options);
    if (Shader)
    {
        return StaticCastSharedPtr<FVertexShader>(Shader);
    }
    return nullptr;
}

TSharedPtr<FPixelShader> FShaderManager::CompilePixelShader(
    const String& SourcePath,
    const String& EntryPoint,
    const TArray<TPair<String, String>>& Definitions)
{
    FShaderCompileOptions Options;
    Options.SourcePath = ResolveShaderPath(SourcePath);
    Options.EntryPoint = EntryPoint;
    Options.Frequency = EShaderFrequency::Pixel;
    Options.Definitions = Definitions;
    Options.IncludePaths = m_includePaths;
    
    auto Shader = CompileShaderInternal(Options);
    if (Shader)
    {
        return StaticCastSharedPtr<FPixelShader>(Shader);
    }
    return nullptr;
}

TSharedPtr<FShader> FShaderManager::CompileShader(const FShaderCompileOptions& Options)
{
    FShaderCompileOptions ResolvedOptions = Options;
    ResolvedOptions.SourcePath = ResolveShaderPath(Options.SourcePath);
    
    // Add default include paths
    for (const auto& Path : m_includePaths)
    {
        bool bFound = false;
        for (const auto& ExistingPath : ResolvedOptions.IncludePaths)
        {
            if (ExistingPath == Path)
            {
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            ResolvedOptions.IncludePaths.Add(Path);
        }
    }
    
    return CompileShaderInternal(ResolvedOptions);
}

TSharedPtr<FShader> FShaderManager::CreateShaderFromBytecode(
    TSpan<const uint8> Bytecode,
    EShaderFrequency Frequency,
    const FName& Name)
{
    if (!m_device)
    {
        MR_LOG_ERROR("FShaderManager: Device not initialized");
        return nullptr;
    }
    
    TSharedPtr<FShader> Shader;
    
    switch (Frequency)
    {
        case EShaderFrequency::Vertex:
            Shader = MakeShared<FVertexShader>();
            break;
        case EShaderFrequency::Pixel:
            Shader = MakeShared<FPixelShader>();
            break;
        default:
            Shader = MakeShared<FShader>(Frequency);
            break;
    }
    
    if (Name.IsValid())
    {
        Shader->SetShaderName(Name);
    }
    
    if (!Shader->CreateFromBytecode(m_device, Bytecode, Frequency))
    {
        MR_LOG_ERROR("FShaderManager: Failed to create shader from bytecode");
        return nullptr;
    }
    
    return Shader;
}

TSharedPtr<FShader> FShaderManager::CompileShaderInternal(const FShaderCompileOptions& Options)
{
    if (!m_device)
    {
        MR_LOG_ERROR("FShaderManager: Device not initialized");
        return nullptr;
    }
    
    MR_LOG_DEBUG("FShaderManager: Compiling shader '%s'", *Options.SourcePath);
    
    // Create appropriate shader type
    TSharedPtr<FShader> Shader;
    switch (Options.Frequency)
    {
        case EShaderFrequency::Vertex:
            Shader = MakeShared<FVertexShader>();
            break;
        case EShaderFrequency::Pixel:
            Shader = MakeShared<FPixelShader>();
            break;
        default:
            Shader = MakeShared<FShader>(Options.Frequency);
            break;
    }
    
    // Extract name from path
    FName ShaderName = FName(*Options.SourcePath);
    Shader->SetShaderName(ShaderName);
    
    // Compile
    if (!Shader->Compile(m_device, Options))
    {
        m_failedCompilations++;
        MR_LOG_ERROR("FShaderManager: Failed to compile shader '%s'", *Options.SourcePath);
        return nullptr;
    }
    
    m_successfulCompilations++;
    
    // Cache the shader
    CacheShader(ShaderName, Shader);
    
    return Shader;
}

// ============================================================================
// Shader Cache
// ============================================================================

TSharedPtr<FShader> FShaderManager::GetCachedShader(const FName& Name) const
{
    const FShaderCacheEntry* Entry = m_shaderCache.Find(Name);
    if (Entry && Entry->bValid)
    {
        return Entry->Shader;
    }
    return nullptr;
}

void FShaderManager::CacheShader(const FName& Name, TSharedPtr<FShader> Shader)
{
    if (!Shader)
    {
        return;
    }
    
    FShaderCacheEntry Entry;
    Entry.Shader = Shader;
    Entry.SourcePath = Shader->GetSourcePath();
    Entry.BytecodeHash = Shader->GetBytecodeHash();
    Entry.bValid = true;
    
    // Get file modification time
    if (!Entry.SourcePath.empty())
    {
        Entry.LastModifiedTime = MonsterRender::ShaderCompiler::getLastWriteTime(Entry.SourcePath);
    }
    
    m_shaderCache.Add(Name, Entry);
    
    MR_LOG_DEBUG("FShaderManager: Cached shader '%s'", *Name.ToString());
}

void FShaderManager::RemoveCachedShader(const FName& Name)
{
    if (m_shaderCache.Remove(Name) > 0)
    {
        MR_LOG_DEBUG("FShaderManager: Removed cached shader '%s'", *Name.ToString());
    }
}

void FShaderManager::ClearCache()
{
    int32 Count = m_shaderCache.Num();
    m_shaderCache.Empty();
    MR_LOG_DEBUG("FShaderManager: Cleared %d cached shaders", Count);
}

// ============================================================================
// Hot Reload
// ============================================================================

int32 FShaderManager::CheckForChanges()
{
    if (!m_hotReloadEnabled)
    {
        return 0;
    }
    
    int32 RecompiledCount = 0;
    
    // Check each cached shader for changes
    for (auto& Pair : m_shaderCache)
    {
        FShaderCacheEntry& Entry = Pair.Value;
        
        if (Entry.SourcePath.empty())
        {
            continue;
        }
        
        uint64 CurrentModTime = MonsterRender::ShaderCompiler::getLastWriteTime(Entry.SourcePath);
        
        if (CurrentModTime > Entry.LastModifiedTime)
        {
            MR_LOG_DEBUG("FShaderManager: Detected change in '%s'", *Entry.SourcePath);
            
            if (RecompileShader(Pair.Key))
            {
                RecompiledCount++;
            }
        }
    }
    
    if (RecompiledCount > 0)
    {
        MR_LOG_DEBUG("FShaderManager: Recompiled %d shaders", RecompiledCount);
    }
    
    return RecompiledCount;
}

bool FShaderManager::RecompileShader(const FName& Name)
{
    FShaderCacheEntry* Entry = m_shaderCache.Find(Name);
    if (!Entry || Entry->SourcePath.empty())
    {
        return false;
    }
    
    // Get original shader info
    TSharedPtr<FShader> OldShader = Entry->Shader;
    if (!OldShader)
    {
        return false;
    }
    
    // Prepare compile options
    FShaderCompileOptions Options;
    Options.SourcePath = Entry->SourcePath;
    Options.Frequency = OldShader->GetFrequency();
    Options.IncludePaths = m_includePaths;
    
    // Recompile
    TSharedPtr<FShader> NewShader = CompileShaderInternal(Options);
    if (!NewShader)
    {
        MR_LOG_ERROR("FShaderManager: Failed to recompile shader '%s'", *Name.ToString());
        return false;
    }
    
    // Update cache entry
    Entry->Shader = NewShader;
    Entry->BytecodeHash = NewShader->GetBytecodeHash();
    Entry->LastModifiedTime = MonsterRender::ShaderCompiler::getLastWriteTime(Entry->SourcePath);
    
    MR_LOG_DEBUG("FShaderManager: Recompiled shader '%s'", *Name.ToString());
    return true;
}

// ============================================================================
// Default Shaders
// ============================================================================

bool FShaderManager::CreateDefaultShaders()
{
    // Default vertex shader source (embedded)
    static const char* DefaultVertexShaderSource = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragNormal = mat3(ubo.model) * inNormal;
    fragTexCoord = inTexCoord;
}
)";

    // Default pixel shader source (embedded)
    static const char* DefaultPixelShaderSource = R"(
#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform MaterialParams {
    vec4 baseColor;
    float metallic;
    float roughness;
    float padding1;
    float padding2;
} material;

layout(binding = 2) uniform sampler2D baseColorTexture;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    vec4 texColor = texture(baseColorTexture, fragTexCoord);
    vec3 color = material.baseColor.rgb * texColor.rgb * (0.3 + 0.7 * NdotL);
    
    outColor = vec4(color, material.baseColor.a * texColor.a);
}
)";

    // Error shader source (magenta)
    static const char* ErrorPixelShaderSource = R"(
#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    // Checkerboard pattern in magenta/black for error visualization
    float checker = mod(floor(fragTexCoord.x * 10.0) + floor(fragTexCoord.y * 10.0), 2.0);
    vec3 color = mix(vec3(1.0, 0.0, 1.0), vec3(0.0, 0.0, 0.0), checker);
    outColor = vec4(color, 1.0);
}
)";

    MR_LOG_DEBUG("FShaderManager: Creating default shaders");
    
    // For now, we'll skip creating default shaders from embedded source
    // as it requires writing to temp files and compiling
    // This will be implemented when we have a proper shader compilation pipeline
    
    MR_LOG_DEBUG("FShaderManager: Default shader creation deferred (requires file-based compilation)");
    
    return true;
}

// ============================================================================
// Shader Paths
// ============================================================================

String FShaderManager::ResolveShaderPath(const String& RelativePath) const
{
    // If already absolute path, return as-is
    if (RelativePath.length() > 1 && 
        (RelativePath[1] == ':' || RelativePath[0] == '/'))
    {
        return RelativePath;
    }
    
    // Combine with shader directory
    return m_shaderDirectory + TEXT("/") + RelativePath;
}

void FShaderManager::AddIncludePath(const String& Path)
{
    // Check if already exists
    for (const auto& ExistingPath : m_includePaths)
    {
        if (ExistingPath == Path)
        {
            return;
        }
    }
    
    m_includePaths.Add(Path);
    MR_LOG_DEBUG("FShaderManager: Added include path '%s'", *Path);
}

// ============================================================================
// Cache File Operations
// ============================================================================

bool FShaderManager::LoadCachedBytecode(const String& CachePath, TArray<uint8>& OutBytecode)
{
    std::vector<uint8> Data = MonsterRender::ShaderCompiler::readFileBytes(CachePath);
    if (Data.empty())
    {
        return false;
    }
    
    OutBytecode.Empty();
    OutBytecode.Reserve(static_cast<int32>(Data.size()));
    for (uint8 Byte : Data)
    {
        OutBytecode.Add(Byte);
    }
    
    return true;
}

bool FShaderManager::SaveCachedBytecode(const String& CachePath, const TArray<uint8>& Bytecode)
{
    // TODO: Implement bytecode caching to disk
    return false;
}

String FShaderManager::GetCacheFilePath(const String& SourcePath, const FShaderCompileOptions& Options) const
{
    // Generate cache file name based on source path and options hash
    // TODO: Implement proper cache path generation
    return m_cacheDirectory + TEXT("/") + SourcePath + TEXT(".spv");
}

} // namespace MonsterEngine
