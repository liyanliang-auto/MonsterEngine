#pragma once

/**
 * OpenGLShaders.h
 * 
 * OpenGL 4.6 shader and program management
 * Reference: UE5 OpenGLDrv/Public/OpenGLShaders.h
 */

#include "Core/CoreMinimal.h"
#include "Containers/String.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "RHI/IRHIResource.h"
#include "Platform/OpenGL/OpenGLDefinitions.h"

namespace MonsterEngine::OpenGL {

// Use MonsterEngine types
using MonsterEngine::FString;
using MonsterEngine::TArray;
using MonsterEngine::TMap;
// Note: TSpan is in global namespace (from CoreTypes.h)

// ============================================================================
// Shader Compilation Result
// ============================================================================

/**
 * Result of shader compilation
 */
struct FShaderCompileResult
{
    bool success = false;
    FString errorMessage;
    TArray<uint8> bytecode;  // For SPIR-V
};

// ============================================================================
// OpenGL Shader
// ============================================================================

/**
 * OpenGL shader object (vertex, fragment, etc.)
 * Reference: UE5 FOpenGLShader
 */
class FOpenGLShader
{
public:
    FOpenGLShader(GLenum shaderType);
    ~FOpenGLShader();
    
    // Non-copyable
    FOpenGLShader(const FOpenGLShader&) = delete;
    FOpenGLShader& operator=(const FOpenGLShader&) = delete;
    
    /**
     * Compile shader from GLSL source
     */
    bool CompileFromSource(const char* source, int32 sourceLength = -1);
    
    /**
     * Load shader from SPIR-V binary (GL 4.6)
     */
    bool LoadFromSPIRV(const uint8* binary, uint32 binarySize, const char* entryPoint = "main");
    
    /**
     * Get the OpenGL shader handle
     */
    GLuint GetGLShader() const { return m_shader; }
    
    /**
     * Get the shader type
     */
    GLenum GetShaderType() const { return m_shaderType; }
    
    /**
     * Check if shader is compiled
     */
    bool IsCompiled() const { return m_compiled; }
    
    /**
     * Get compilation error message
     */
    const FString& GetErrorMessage() const { return m_errorMessage; }
    
    /**
     * Set debug name
     */
    void SetDebugName(const FString& name);
    
private:
    /**
     * Get shader info log
     */
    FString GetInfoLog() const;
    
private:
    GLuint m_shader = 0;
    GLenum m_shaderType = 0;
    bool m_compiled = false;
    FString m_errorMessage;
    FString m_debugName;
};

// ============================================================================
// OpenGL Vertex Shader
// ============================================================================

/**
 * OpenGL vertex shader
 */
class FOpenGLVertexShader : public MonsterRender::RHI::IRHIVertexShader
{
public:
    FOpenGLVertexShader();
    virtual ~FOpenGLVertexShader();
    
    /**
     * Initialize from GLSL source
     */
    bool InitFromSource(const char* source, int32 sourceLength = -1);
    
    /**
     * Initialize from SPIR-V binary
     */
    bool InitFromSPIRV(const uint8* binary, uint32 binarySize, const char* entryPoint = "main");
    
    /**
     * Initialize from bytecode (auto-detect format)
     */
    bool InitFromBytecode(MonsterRender::TSpan<const uint8> bytecode);
    
    /**
     * Get the internal shader object
     */
    FOpenGLShader* GetShader() { return m_shader.get(); }
    const FOpenGLShader* GetShader() const { return m_shader.get(); }
    
    /**
     * Get the OpenGL shader handle
     */
    GLuint GetGLShader() const { return m_shader ? m_shader->GetGLShader() : 0; }
    
private:
    TUniquePtr<FOpenGLShader> m_shader;
};

// ============================================================================
// OpenGL Pixel/Fragment Shader
// ============================================================================

/**
 * OpenGL pixel/fragment shader
 */
class FOpenGLPixelShader : public MonsterRender::RHI::IRHIPixelShader
{
public:
    FOpenGLPixelShader();
    virtual ~FOpenGLPixelShader();
    
    /**
     * Initialize from GLSL source
     */
    bool InitFromSource(const char* source, int32 sourceLength = -1);
    
    /**
     * Initialize from SPIR-V binary
     */
    bool InitFromSPIRV(const uint8* binary, uint32 binarySize, const char* entryPoint = "main");
    
    /**
     * Initialize from bytecode (auto-detect format)
     */
    bool InitFromBytecode(MonsterRender::TSpan<const uint8> bytecode);
    
    /**
     * Get the internal shader object
     */
    FOpenGLShader* GetShader() { return m_shader.get(); }
    const FOpenGLShader* GetShader() const { return m_shader.get(); }
    
    /**
     * Get the OpenGL shader handle
     */
    GLuint GetGLShader() const { return m_shader ? m_shader->GetGLShader() : 0; }
    
private:
    TUniquePtr<FOpenGLShader> m_shader;
};

// ============================================================================
// OpenGL Program (Linked Shaders)
// ============================================================================

/**
 * OpenGL shader program
 * Reference: UE5 FOpenGLLinkedProgram
 */
class FOpenGLProgram
{
public:
    FOpenGLProgram();
    ~FOpenGLProgram();
    
    // Non-copyable
    FOpenGLProgram(const FOpenGLProgram&) = delete;
    FOpenGLProgram& operator=(const FOpenGLProgram&) = delete;
    
    /**
     * Attach a shader to the program
     */
    void AttachShader(FOpenGLShader* shader);
    void AttachShader(GLuint shader);
    
    /**
     * Detach a shader from the program
     */
    void DetachShader(FOpenGLShader* shader);
    void DetachShader(GLuint shader);
    
    /**
     * Link the program
     */
    bool Link();
    
    /**
     * Use this program
     */
    void Use() const;
    
    /**
     * Stop using any program
     */
    static void UseNone();
    
    /**
     * Get the OpenGL program handle
     */
    GLuint GetGLProgram() const { return m_program; }
    
    /**
     * Check if program is linked
     */
    bool IsLinked() const { return m_linked; }
    
    /**
     * Get link error message
     */
    const FString& GetErrorMessage() const { return m_errorMessage; }
    
    /**
     * Set debug name
     */
    void SetDebugName(const FString& name);
    
    // Uniform setters
    void SetUniform1i(const char* name, int32 value);
    void SetUniform1f(const char* name, float value);
    void SetUniform2f(const char* name, float x, float y);
    void SetUniform3f(const char* name, float x, float y, float z);
    void SetUniform4f(const char* name, float x, float y, float z, float w);
    void SetUniform4fv(const char* name, const float* values, int32 count = 1);
    void SetUniformMatrix3fv(const char* name, const float* values, int32 count = 1, bool transpose = false);
    void SetUniformMatrix4fv(const char* name, const float* values, int32 count = 1, bool transpose = false);
    
    /**
     * Get uniform location (cached)
     */
    GLint GetUniformLocation(const char* name);
    
    /**
     * Get attribute location
     */
    GLint GetAttribLocation(const char* name) const;
    
    /**
     * Bind attribute location (must be called before linking)
     */
    void BindAttribLocation(uint32 index, const char* name);
    
    /**
     * Bind fragment data location (must be called before linking)
     */
    void BindFragDataLocation(uint32 colorNumber, const char* name);
    
    /**
     * Get uniform block index
     */
    GLuint GetUniformBlockIndex(const char* name) const;
    
    /**
     * Bind uniform block to binding point
     */
    void UniformBlockBinding(GLuint blockIndex, GLuint bindingPoint);
    void UniformBlockBinding(const char* blockName, GLuint bindingPoint);
    
private:
    /**
     * Get program info log
     */
    FString GetInfoLog() const;
    
private:
    GLuint m_program = 0;
    bool m_linked = false;
    FString m_errorMessage;
    FString m_debugName;
    
    // Uniform location cache
    TMap<FString, GLint> m_uniformLocationCache;
};

// ============================================================================
// Shader Utilities
// ============================================================================

/**
 * Check if bytecode is SPIR-V format
 */
bool IsSPIRVBytecode(const uint8* data, uint32 size);

/**
 * Get GLSL version string for OpenGL 4.6
 */
const char* GetGLSLVersionString();

/**
 * Compile GLSL shader from source
 */
FShaderCompileResult CompileGLSLShader(GLenum shaderType, const char* source);

/**
 * Create a simple program from vertex and fragment shader sources
 */
TUniquePtr<FOpenGLProgram> CreateProgramFromSources(
    const char* vertexSource, 
    const char* fragmentSource);

} // namespace MonsterEngine::OpenGL
