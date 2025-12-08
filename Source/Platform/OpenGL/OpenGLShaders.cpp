/**
 * OpenGLShaders.cpp
 * 
 * OpenGL 4.6 shader and program implementation
 * Reference: UE5 OpenGLDrv/Private/OpenGLShaders.cpp
 */

#include "Platform/OpenGL/OpenGLShaders.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Core/HAL/LogMacros.h"

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogOpenGLShaders, Log, All);

namespace MonsterEngine::OpenGL {

// SPIR-V magic number
constexpr uint32 SPIRV_MAGIC = 0x07230203;

// ============================================================================
// Shader Utilities
// ============================================================================

bool IsSPIRVBytecode(const uint8* data, uint32 size)
{
    if (size < 4)
    {
        return false;
    }
    
    uint32 magic = *reinterpret_cast<const uint32*>(data);
    return magic == SPIRV_MAGIC;
}

const char* GetGLSLVersionString()
{
    return "#version 460 core\n";
}

FShaderCompileResult CompileGLSLShader(GLenum shaderType, const char* source)
{
    FShaderCompileResult result;
    
    GLuint shader = glCreateShader(shaderType);
    if (!shader)
    {
        result.errorMessage = TEXT("Failed to create shader object");
        return result;
    }
    
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint compileStatus = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    
    if (compileStatus == GL_TRUE)
    {
        result.success = true;
    }
    else
    {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        
        if (logLength > 0)
        {
            TArray<char> log;
            log.SetNum(logLength);
            glGetShaderInfoLog(shader, logLength, nullptr, log.GetData());
            result.errorMessage = FString(log.GetData());
        }
        else
        {
            result.errorMessage = TEXT("Unknown shader compilation error");
        }
    }
    
    glDeleteShader(shader);
    return result;
}

TUniquePtr<FOpenGLProgram> CreateProgramFromSources(
    const char* vertexSource, 
    const char* fragmentSource)
{
    auto program = MakeUnique<FOpenGLProgram>();
    
    FOpenGLShader vertexShader(GL_VERTEX_SHADER);
    if (!vertexShader.CompileFromSource(vertexSource))
    {
        MR_LOG_ERROR(LogOpenGLShaders, "Vertex shader compilation failed: %s", 
                     *vertexShader.GetErrorMessage());
        return nullptr;
    }
    
    FOpenGLShader fragmentShader(GL_FRAGMENT_SHADER);
    if (!fragmentShader.CompileFromSource(fragmentSource))
    {
        MR_LOG_ERROR(LogOpenGLShaders, "Fragment shader compilation failed: %s", 
                     *fragmentShader.GetErrorMessage());
        return nullptr;
    }
    
    program->AttachShader(&vertexShader);
    program->AttachShader(&fragmentShader);
    
    if (!program->Link())
    {
        MR_LOG_ERROR(LogOpenGLShaders, "Program linking failed: %s", 
                     *program->GetErrorMessage());
        return nullptr;
    }
    
    return program;
}

// ============================================================================
// FOpenGLShader Implementation
// ============================================================================

FOpenGLShader::FOpenGLShader(GLenum shaderType)
    : m_shaderType(shaderType)
{
    m_shader = glCreateShader(shaderType);
    if (!m_shader)
    {
        MR_LOG_ERROR(LogOpenGLShaders, "Failed to create shader object (type=0x%X)", shaderType);
    }
}

FOpenGLShader::~FOpenGLShader()
{
    if (m_shader)
    {
        glDeleteShader(m_shader);
        m_shader = 0;
    }
}

bool FOpenGLShader::CompileFromSource(const char* source, int32 sourceLength)
{
    if (!m_shader)
    {
        m_errorMessage = TEXT("Invalid shader object");
        return false;
    }
    
    if (!source)
    {
        m_errorMessage = TEXT("Null source pointer");
        return false;
    }
    
    // Set shader source
    if (sourceLength < 0)
    {
        glShaderSource(m_shader, 1, &source, nullptr);
    }
    else
    {
        glShaderSource(m_shader, 1, &source, &sourceLength);
    }
    
    // Compile
    glCompileShader(m_shader);
    
    // Check status
    GLint compileStatus = 0;
    glGetShaderiv(m_shader, GL_COMPILE_STATUS, &compileStatus);
    
    if (compileStatus == GL_TRUE)
    {
        m_compiled = true;
        m_errorMessage.Empty();
        MR_LOG_DEBUG(LogOpenGLShaders, "Shader compiled successfully: %s", *m_debugName);
        return true;
    }
    else
    {
        m_compiled = false;
        m_errorMessage = GetInfoLog();
        MR_LOG_ERROR(LogOpenGLShaders, "Shader compilation failed: %s\n%s", 
                     *m_debugName, *m_errorMessage);
        return false;
    }
}

bool FOpenGLShader::LoadFromSPIRV(const uint8* binary, uint32 binarySize, const char* entryPoint)
{
    if (!m_shader)
    {
        m_errorMessage = TEXT("Invalid shader object");
        return false;
    }
    
    if (!binary || binarySize == 0)
    {
        m_errorMessage = TEXT("Invalid SPIR-V binary");
        return false;
    }
    
    // Check for SPIR-V support
    if (!glShaderBinary || !glSpecializeShader)
    {
        m_errorMessage = TEXT("SPIR-V not supported (GL 4.6 required)");
        return false;
    }
    
    // Load SPIR-V binary
    glShaderBinary(1, &m_shader, GL_SHADER_BINARY_FORMAT_SPIR_V, binary, binarySize);
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        m_errorMessage = FString::Printf(TEXT("glShaderBinary failed: 0x%X"), error);
        return false;
    }
    
    // Specialize the shader
    glSpecializeShader(m_shader, entryPoint, 0, nullptr, nullptr);
    
    // Check compilation status
    GLint compileStatus = 0;
    glGetShaderiv(m_shader, GL_COMPILE_STATUS, &compileStatus);
    
    if (compileStatus == GL_TRUE)
    {
        m_compiled = true;
        m_errorMessage.Empty();
        MR_LOG_DEBUG(LogOpenGLShaders, "SPIR-V shader loaded successfully: %s", *m_debugName);
        return true;
    }
    else
    {
        m_compiled = false;
        m_errorMessage = GetInfoLog();
        MR_LOG_ERROR(LogOpenGLShaders, "SPIR-V shader specialization failed: %s\n%s", 
                     *m_debugName, *m_errorMessage);
        return false;
    }
}

void FOpenGLShader::SetDebugName(const FString& name)
{
    m_debugName = name;
    
    if (glObjectLabel && m_shader && !name.IsEmpty())
    {
        glObjectLabel(GL_SHADER, m_shader, -1, TCHAR_TO_ANSI(*name));
    }
}

FString FOpenGLShader::GetInfoLog() const
{
    if (!m_shader)
    {
        return FString();
    }
    
    GLint logLength = 0;
    glGetShaderiv(m_shader, GL_INFO_LOG_LENGTH, &logLength);
    
    if (logLength <= 0)
    {
        return FString();
    }
    
    TArray<char> log;
    log.SetNum(logLength);
    glGetShaderInfoLog(m_shader, logLength, nullptr, log.GetData());
    
    return FString(log.GetData());
}

// ============================================================================
// FOpenGLVertexShader Implementation
// ============================================================================

FOpenGLVertexShader::FOpenGLVertexShader()
{
    m_shader = MakeUnique<FOpenGLShader>(GL_VERTEX_SHADER);
}

FOpenGLVertexShader::~FOpenGLVertexShader() = default;

bool FOpenGLVertexShader::InitFromSource(const char* source, int32 sourceLength)
{
    if (!m_shader)
    {
        return false;
    }
    
    m_shader->SetDebugName(m_debugName);
    return m_shader->CompileFromSource(source, sourceLength);
}

bool FOpenGLVertexShader::InitFromSPIRV(const uint8* binary, uint32 binarySize, const char* entryPoint)
{
    if (!m_shader)
    {
        return false;
    }
    
    m_shader->SetDebugName(m_debugName);
    return m_shader->LoadFromSPIRV(binary, binarySize, entryPoint);
}

bool FOpenGLVertexShader::InitFromBytecode(MonsterRender::TSpan<const uint8> bytecode)
{
    if (bytecode.empty())
    {
        return false;
    }
    
    // Check if it's SPIR-V
    if (IsSPIRVBytecode(bytecode.data(), static_cast<uint32>(bytecode.size())))
    {
        return InitFromSPIRV(bytecode.data(), static_cast<uint32>(bytecode.size()));
    }
    else
    {
        // Assume it's GLSL source
        return InitFromSource(reinterpret_cast<const char*>(bytecode.data()), 
                             static_cast<int32>(bytecode.size()));
    }
}

// ============================================================================
// FOpenGLPixelShader Implementation
// ============================================================================

FOpenGLPixelShader::FOpenGLPixelShader()
{
    m_shader = MakeUnique<FOpenGLShader>(GL_FRAGMENT_SHADER);
}

FOpenGLPixelShader::~FOpenGLPixelShader() = default;

bool FOpenGLPixelShader::InitFromSource(const char* source, int32 sourceLength)
{
    if (!m_shader)
    {
        return false;
    }
    
    m_shader->SetDebugName(m_debugName);
    return m_shader->CompileFromSource(source, sourceLength);
}

bool FOpenGLPixelShader::InitFromSPIRV(const uint8* binary, uint32 binarySize, const char* entryPoint)
{
    if (!m_shader)
    {
        return false;
    }
    
    m_shader->SetDebugName(m_debugName);
    return m_shader->LoadFromSPIRV(binary, binarySize, entryPoint);
}

bool FOpenGLPixelShader::InitFromBytecode(MonsterRender::TSpan<const uint8> bytecode)
{
    if (bytecode.empty())
    {
        return false;
    }
    
    // Check if it's SPIR-V
    if (IsSPIRVBytecode(bytecode.data(), static_cast<uint32>(bytecode.size())))
    {
        return InitFromSPIRV(bytecode.data(), static_cast<uint32>(bytecode.size()));
    }
    else
    {
        // Assume it's GLSL source
        return InitFromSource(reinterpret_cast<const char*>(bytecode.data()), 
                             static_cast<int32>(bytecode.size()));
    }
}

// ============================================================================
// FOpenGLProgram Implementation
// ============================================================================

FOpenGLProgram::FOpenGLProgram()
{
    m_program = glCreateProgram();
    if (!m_program)
    {
        MR_LOG_ERROR(LogOpenGLShaders, "Failed to create program object");
    }
}

FOpenGLProgram::~FOpenGLProgram()
{
    if (m_program)
    {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

void FOpenGLProgram::AttachShader(FOpenGLShader* shader)
{
    if (shader && shader->GetGLShader())
    {
        AttachShader(shader->GetGLShader());
    }
}

void FOpenGLProgram::AttachShader(GLuint shader)
{
    if (m_program && shader)
    {
        glAttachShader(m_program, shader);
    }
}

void FOpenGLProgram::DetachShader(FOpenGLShader* shader)
{
    if (shader && shader->GetGLShader())
    {
        DetachShader(shader->GetGLShader());
    }
}

void FOpenGLProgram::DetachShader(GLuint shader)
{
    if (m_program && shader)
    {
        glDetachShader(m_program, shader);
    }
}

bool FOpenGLProgram::Link()
{
    if (!m_program)
    {
        m_errorMessage = TEXT("Invalid program object");
        return false;
    }
    
    glLinkProgram(m_program);
    
    GLint linkStatus = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linkStatus);
    
    if (linkStatus == GL_TRUE)
    {
        m_linked = true;
        m_errorMessage.Empty();
        m_uniformLocationCache.Empty();  // Clear cache on re-link
        MR_LOG_DEBUG(LogOpenGLShaders, "Program linked successfully: %s", *m_debugName);
        return true;
    }
    else
    {
        m_linked = false;
        m_errorMessage = GetInfoLog();
        MR_LOG_ERROR(LogOpenGLShaders, "Program linking failed: %s\n%s", 
                     *m_debugName, *m_errorMessage);
        return false;
    }
}

void FOpenGLProgram::Use() const
{
    glUseProgram(m_program);
}

void FOpenGLProgram::UseNone()
{
    glUseProgram(0);
}

void FOpenGLProgram::SetDebugName(const FString& name)
{
    m_debugName = name;
    
    if (glObjectLabel && m_program && !name.IsEmpty())
    {
        glObjectLabel(GL_PROGRAM, m_program, -1, TCHAR_TO_ANSI(*name));
    }
}

void FOpenGLProgram::SetUniform1i(const char* name, int32 value)
{
    GLint location = GetUniformLocation(name);
    if (location >= 0)
    {
        glUniform1i(location, value);
    }
}

void FOpenGLProgram::SetUniform1f(const char* name, float value)
{
    GLint location = GetUniformLocation(name);
    if (location >= 0)
    {
        glUniform1f(location, value);
    }
}

void FOpenGLProgram::SetUniform2f(const char* name, float x, float y)
{
    GLint location = GetUniformLocation(name);
    if (location >= 0)
    {
        glUniform2f(location, x, y);
    }
}

void FOpenGLProgram::SetUniform3f(const char* name, float x, float y, float z)
{
    GLint location = GetUniformLocation(name);
    if (location >= 0)
    {
        glUniform3f(location, x, y, z);
    }
}

void FOpenGLProgram::SetUniform4f(const char* name, float x, float y, float z, float w)
{
    GLint location = GetUniformLocation(name);
    if (location >= 0)
    {
        glUniform4f(location, x, y, z, w);
    }
}

void FOpenGLProgram::SetUniform4fv(const char* name, const float* values, int32 count)
{
    GLint location = GetUniformLocation(name);
    if (location >= 0)
    {
        glUniform4fv(location, count, values);
    }
}

void FOpenGLProgram::SetUniformMatrix3fv(const char* name, const float* values, int32 count, bool transpose)
{
    GLint location = GetUniformLocation(name);
    if (location >= 0)
    {
        glUniformMatrix3fv(location, count, transpose ? GL_TRUE : GL_FALSE, values);
    }
}

void FOpenGLProgram::SetUniformMatrix4fv(const char* name, const float* values, int32 count, bool transpose)
{
    GLint location = GetUniformLocation(name);
    if (location >= 0)
    {
        glUniformMatrix4fv(location, count, transpose ? GL_TRUE : GL_FALSE, values);
    }
}

GLint FOpenGLProgram::GetUniformLocation(const char* name)
{
    FString nameStr(name);
    
    // Check cache first
    GLint* cached = m_uniformLocationCache.Find(nameStr);
    if (cached)
    {
        return *cached;
    }
    
    // Query and cache
    GLint location = glGetUniformLocation(m_program, name);
    m_uniformLocationCache.Add(nameStr, location);
    
    if (location < 0)
    {
        MR_LOG_WARNING(LogOpenGLShaders, "Uniform '%s' not found in program '%s'", 
                       name, *m_debugName);
    }
    
    return location;
}

GLint FOpenGLProgram::GetAttribLocation(const char* name) const
{
    return glGetAttribLocation(m_program, name);
}

void FOpenGLProgram::BindAttribLocation(uint32 index, const char* name)
{
    glBindAttribLocation(m_program, index, name);
}

void FOpenGLProgram::BindFragDataLocation(uint32 colorNumber, const char* name)
{
    glBindFragDataLocation(m_program, colorNumber, name);
}

GLuint FOpenGLProgram::GetUniformBlockIndex(const char* name) const
{
    return glGetUniformBlockIndex(m_program, name);
}

void FOpenGLProgram::UniformBlockBinding(GLuint blockIndex, GLuint bindingPoint)
{
    glUniformBlockBinding(m_program, blockIndex, bindingPoint);
}

void FOpenGLProgram::UniformBlockBinding(const char* blockName, GLuint bindingPoint)
{
    GLuint blockIndex = GetUniformBlockIndex(blockName);
    if (blockIndex != GL_INVALID_INDEX)
    {
        UniformBlockBinding(blockIndex, bindingPoint);
    }
    else
    {
        MR_LOG_WARNING(LogOpenGLShaders, "Uniform block '%s' not found in program '%s'", 
                       blockName, *m_debugName);
    }
}

FString FOpenGLProgram::GetInfoLog() const
{
    if (!m_program)
    {
        return FString();
    }
    
    GLint logLength = 0;
    glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &logLength);
    
    if (logLength <= 0)
    {
        return FString();
    }
    
    TArray<char> log;
    log.SetNum(logLength);
    glGetProgramInfoLog(m_program, logLength, nullptr, log.GetData());
    
    return FString(log.GetData());
}

} // namespace MonsterEngine::OpenGL
