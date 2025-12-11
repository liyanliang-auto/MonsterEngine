#pragma once

/**
 * @file ShaderManager.h
 * @brief Shader compilation and caching manager
 * 
 * FShaderManager handles shader compilation, caching, and hot-reloading.
 * It provides a centralized interface for shader management.
 * 
 * Reference UE5: FShaderCompilingManager
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Core/Templates/SharedPointer.h"
#include "Engine/Shader/Shader.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

// Forward declare RHI types
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
}}

namespace MonsterEngine
{

// Forward declarations
class FShader;
class FVertexShader;
class FPixelShader;

// ============================================================================
// Shader Cache Entry
// ============================================================================

/**
 * @struct FShaderCacheEntry
 * @brief Cached shader entry with metadata
 */
struct FShaderCacheEntry
{
    /** Cached shader */
    TSharedPtr<FShader> Shader;
    
    /** Source file path */
    String SourcePath;
    
    /** Last modification time */
    uint64 LastModifiedTime = 0;
    
    /** Bytecode hash */
    uint64 BytecodeHash = 0;
    
    /** Whether entry is valid */
    bool bValid = false;
};

// ============================================================================
// Shader Manager
// ============================================================================

/**
 * @class FShaderManager
 * @brief Centralized shader compilation and caching
 * 
 * Features:
 * - Shader compilation from source
 * - Bytecode caching
 * - Hot-reload support
 * - Default shader management
 * 
 * Reference UE5: FShaderCompilingManager
 */
class FShaderManager
{
public:
    FShaderManager();
    ~FShaderManager();
    
    // Singleton access
    static FShaderManager& Get();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initialize the shader manager
     * @param Device RHI device for shader creation
     * @return true if initialization succeeded
     */
    bool Initialize(MonsterRender::RHI::IRHIDevice* Device);
    
    /**
     * Shutdown the shader manager
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
     * Compile a vertex shader from source file
     * @param SourcePath Path to shader source file
     * @param EntryPoint Entry point function name
     * @param Definitions Preprocessor definitions
     * @return Compiled shader, or nullptr on failure
     */
    TSharedPtr<FVertexShader> CompileVertexShader(
        const String& SourcePath,
        const String& EntryPoint = "main",
        const TArray<TPair<String, String>>& Definitions = TArray<TPair<String, String>>());
    
    /**
     * Compile a pixel shader from source file
     * @param SourcePath Path to shader source file
     * @param EntryPoint Entry point function name
     * @param Definitions Preprocessor definitions
     * @return Compiled shader, or nullptr on failure
     */
    TSharedPtr<FPixelShader> CompilePixelShader(
        const String& SourcePath,
        const String& EntryPoint = "main",
        const TArray<TPair<String, String>>& Definitions = TArray<TPair<String, String>>());
    
    /**
     * Compile a shader with full options
     * @param Options Compile options
     * @return Compiled shader, or nullptr on failure
     */
    TSharedPtr<FShader> CompileShader(const FShaderCompileOptions& Options);
    
    /**
     * Create shader from pre-compiled bytecode
     * @param Bytecode SPIR-V bytecode
     * @param Frequency Shader stage
     * @param Name Shader name for identification
     * @return Created shader, or nullptr on failure
     */
    TSharedPtr<FShader> CreateShaderFromBytecode(
        TSpan<const uint8> Bytecode,
        EShaderFrequency Frequency,
        const FName& Name = FName());
    
    // ========================================================================
    // Shader Cache
    // ========================================================================
    
    /**
     * Get a cached shader by name
     * @param Name Shader name
     * @return Cached shader, or nullptr if not found
     */
    TSharedPtr<FShader> GetCachedShader(const FName& Name) const;
    
    /**
     * Add shader to cache
     * @param Name Shader name
     * @param Shader Shader to cache
     */
    void CacheShader(const FName& Name, TSharedPtr<FShader> Shader);
    
    /**
     * Remove shader from cache
     * @param Name Shader name
     */
    void RemoveCachedShader(const FName& Name);
    
    /**
     * Clear all cached shaders
     */
    void ClearCache();
    
    /**
     * Get number of cached shaders
     */
    int32 GetCachedShaderCount() const { return m_shaderCache.Num(); }
    
    // ========================================================================
    // Hot Reload
    // ========================================================================
    
    /**
     * Check for shader file changes and recompile if needed
     * @return Number of shaders recompiled
     */
    int32 CheckForChanges();
    
    /**
     * Force recompile a specific shader
     * @param Name Shader name
     * @return true if recompilation succeeded
     */
    bool RecompileShader(const FName& Name);
    
    /**
     * Enable/disable hot reload
     */
    void SetHotReloadEnabled(bool bEnabled) { m_hotReloadEnabled = bEnabled; }
    bool IsHotReloadEnabled() const { return m_hotReloadEnabled; }
    
    // ========================================================================
    // Default Shaders
    // ========================================================================
    
    /**
     * Get the default vertex shader
     */
    TSharedPtr<FVertexShader> GetDefaultVertexShader() const { return m_defaultVertexShader; }
    
    /**
     * Get the default pixel shader
     */
    TSharedPtr<FPixelShader> GetDefaultPixelShader() const { return m_defaultPixelShader; }
    
    /**
     * Get the error shader (used when compilation fails)
     */
    TSharedPtr<FPixelShader> GetErrorShader() const { return m_errorShader; }
    
    /**
     * Create default shaders
     * @return true if creation succeeded
     */
    bool CreateDefaultShaders();
    
    // ========================================================================
    // Shader Paths
    // ========================================================================
    
    /**
     * Set the base shader directory
     */
    void SetShaderDirectory(const String& Path) { m_shaderDirectory = Path; }
    
    /**
     * Get the base shader directory
     */
    const String& GetShaderDirectory() const { return m_shaderDirectory; }
    
    /**
     * Resolve a shader path (relative to shader directory)
     */
    String ResolveShaderPath(const String& RelativePath) const;
    
    /**
     * Add an include path for shader compilation
     */
    void AddIncludePath(const String& Path);
    
    /**
     * Get all include paths
     */
    const TArray<String>& GetIncludePaths() const { return m_includePaths; }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    /**
     * Get total compilation time (in seconds)
     */
    float GetTotalCompilationTime() const { return m_totalCompilationTime; }
    
    /**
     * Get number of successful compilations
     */
    int32 GetSuccessfulCompilations() const { return m_successfulCompilations; }
    
    /**
     * Get number of failed compilations
     */
    int32 GetFailedCompilations() const { return m_failedCompilations; }
    
protected:
    /**
     * Internal shader compilation
     */
    TSharedPtr<FShader> CompileShaderInternal(const FShaderCompileOptions& Options);
    
    /**
     * Load shader bytecode from cache file
     */
    bool LoadCachedBytecode(const String& CachePath, TArray<uint8>& OutBytecode);
    
    /**
     * Save shader bytecode to cache file
     */
    bool SaveCachedBytecode(const String& CachePath, const TArray<uint8>& Bytecode);
    
    /**
     * Get cache file path for a shader
     */
    String GetCacheFilePath(const String& SourcePath, const FShaderCompileOptions& Options) const;
    
protected:
    /** RHI device */
    MonsterRender::RHI::IRHIDevice* m_device = nullptr;
    
    /** Shader cache (name -> entry) */
    TMap<FName, FShaderCacheEntry> m_shaderCache;
    
    /** Base shader directory */
    String m_shaderDirectory;
    
    /** Include paths for compilation */
    TArray<String> m_includePaths;
    
    /** Shader bytecode cache directory */
    String m_cacheDirectory;
    
    /** Default vertex shader */
    TSharedPtr<FVertexShader> m_defaultVertexShader;
    
    /** Default pixel shader */
    TSharedPtr<FPixelShader> m_defaultPixelShader;
    
    /** Error shader (magenta) */
    TSharedPtr<FPixelShader> m_errorShader;
    
    /** Whether hot reload is enabled */
    bool m_hotReloadEnabled = true;
    
    /** Whether initialized */
    bool m_initialized = false;
    
    /** Statistics */
    float m_totalCompilationTime = 0.0f;
    int32 m_successfulCompilations = 0;
    int32 m_failedCompilations = 0;
    
    /** Singleton instance */
    static FShaderManager* s_instance;
};

} // namespace MonsterEngine
