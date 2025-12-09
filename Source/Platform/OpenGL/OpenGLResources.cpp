/**
 * OpenGLResources.cpp
 * 
 * OpenGL 4.6 resource implementations
 * Reference: UE5 OpenGLDrv/Private/OpenGLBuffer.cpp, OpenGLTexture.cpp
 */

#include "Platform/OpenGL/OpenGLResources.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Core/Logging/LogMacros.h"

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogOpenGLResources, Log, All);

namespace MonsterEngine::OpenGL {

using namespace MonsterRender::RHI;

// ============================================================================
// FOpenGLBuffer Implementation
// ============================================================================

FOpenGLBuffer::FOpenGLBuffer(const BufferDesc& desc)
    : IRHIBuffer(desc)
{
    m_target = DetermineTarget();
    m_usage = DetermineUsage();
    
    if (!CreateBuffer(nullptr))
    {
        OutputDebugStringA("OpenGL: Error\n");
    }
}

FOpenGLBuffer::~FOpenGLBuffer()
{
    if (m_mappedPtr)
    {
        unmap();
    }
    
    if (m_buffer)
    {
        glDeleteBuffers(1, &m_buffer);
        m_buffer = 0;
    }
}

void* FOpenGLBuffer::map()
{
    if (m_mappedPtr)
    {
        return m_mappedPtr;
    }
    
    if (!m_desc.cpuAccessible)
    {
        OutputDebugStringA("OpenGL: Warning\n");
        return nullptr;
    }
    
    Bind();
    
    GLbitfield access = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
    if (m_persistentMapping)
    {
        access |= GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    }
    
    m_mappedPtr = glMapBufferRange(m_target, 0, m_desc.size, access);
    
    if (!m_mappedPtr)
    {
        OutputDebugStringA("OpenGL: Error\n");
    }
    
    return m_mappedPtr;
}

void FOpenGLBuffer::unmap()
{
    if (!m_mappedPtr)
    {
        return;
    }
    
    Bind();
    glUnmapBuffer(m_target);
    m_mappedPtr = nullptr;
}

void FOpenGLBuffer::UpdateData(const void* data, uint32 size, uint32 offset)
{
    if (!data || size == 0)
    {
        return;
    }
    
    if (offset + size > m_desc.size)
    {
        OutputDebugStringA("OpenGL: Error\n");
        return;
    }
    
    Bind();
    glBufferSubData(m_target, offset, size, data);
    GL_CHECK("glBufferSubData");
}

void FOpenGLBuffer::Bind() const
{
    glBindBuffer(m_target, m_buffer);
}

void FOpenGLBuffer::Unbind() const
{
    glBindBuffer(m_target, 0);
}

void FOpenGLBuffer::BindBase(uint32 index) const
{
    glBindBufferBase(m_target, index, m_buffer);
}

bool FOpenGLBuffer::CreateBuffer(const void* initialData)
{
    glGenBuffers(1, &m_buffer);
    if (!m_buffer)
    {
        OutputDebugStringA("OpenGL: Error\n");
        return false;
    }
    
    Bind();
    
    // Use immutable storage if available and buffer is not dynamic
    bool useImmutableStorage = (glBufferStorage != nullptr) && 
                               (m_desc.memoryUsage != EMemoryUsage::Dynamic);
    
    if (useImmutableStorage)
    {
        GLbitfield flags = 0;
        
        if (m_desc.cpuAccessible)
        {
            flags |= GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
            
            // Enable persistent mapping for frequently updated buffers
            if (m_desc.memoryUsage == EMemoryUsage::Upload)
            {
                flags |= GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
                m_persistentMapping = true;
            }
        }
        
        if (m_desc.memoryUsage == EMemoryUsage::Dynamic)
        {
            flags |= GL_DYNAMIC_STORAGE_BIT;
        }
        
        glBufferStorage(m_target, m_desc.size, initialData, flags);
    }
    else
    {
        glBufferData(m_target, m_desc.size, initialData, m_usage);
    }
    
    GL_CHECK("CreateBuffer");
    
    // Set debug label
    if (glObjectLabel && !m_debugName.empty())
    {
        glObjectLabel(GL_BUFFER, m_buffer, -1, m_debugName.c_str());
    }
    
    OutputDebugStringA("OpenGL: Debug\n");
    
    return true;
}

GLenum FOpenGLBuffer::DetermineTarget() const
{
    if (hasResourceUsage(m_desc.usage, EResourceUsage::VertexBuffer))
        return GL_ARRAY_BUFFER;
    if (hasResourceUsage(m_desc.usage, EResourceUsage::IndexBuffer))
        return GL_ELEMENT_ARRAY_BUFFER;
    if (hasResourceUsage(m_desc.usage, EResourceUsage::UniformBuffer))
        return GL_UNIFORM_BUFFER;
    if (hasResourceUsage(m_desc.usage, EResourceUsage::StorageBuffer))
        return GL_SHADER_STORAGE_BUFFER;
    if (hasResourceUsage(m_desc.usage, EResourceUsage::TransferSrc))
        return GL_COPY_READ_BUFFER;
    if (hasResourceUsage(m_desc.usage, EResourceUsage::TransferDst))
        return GL_COPY_WRITE_BUFFER;
    
    return GL_ARRAY_BUFFER;  // Default
}

GLenum FOpenGLBuffer::DetermineUsage() const
{
    switch (m_desc.memoryUsage)
    {
        case EMemoryUsage::Default:
            return GL_STATIC_DRAW;
        case EMemoryUsage::Upload:
            return GL_STREAM_DRAW;
        case EMemoryUsage::Readback:
            return GL_STREAM_READ;
        case EMemoryUsage::Dynamic:
            return GL_DYNAMIC_DRAW;
        default:
            return GL_STATIC_DRAW;
    }
}

// ============================================================================
// FOpenGLTexture Implementation
// ============================================================================

FOpenGLTexture::FOpenGLTexture(const TextureDesc& desc)
    : IRHITexture(desc)
{
    m_target = DetermineTarget();
    ConvertPixelFormat(desc.format, m_internalFormat, m_format, m_type);
    
    if (!CreateTexture(desc.initialData, desc.initialDataSize))
    {
        OutputDebugStringA("OpenGL: Error\n");
    }
}

FOpenGLTexture::~FOpenGLTexture()
{
    if (m_texture)
    {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
}

void FOpenGLTexture::Bind(uint32 unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(m_target, m_texture);
}

void FOpenGLTexture::Unbind(uint32 unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(m_target, 0);
}

void FOpenGLTexture::UpdateData2D(const void* data, uint32 mipLevel, 
                                   uint32 x, uint32 y, 
                                   uint32 width, uint32 height)
{
    if (!data)
    {
        return;
    }
    
    if (width == 0) width = m_desc.width >> mipLevel;
    if (height == 0) height = m_desc.height >> mipLevel;
    
    Bind(0);
    glTexSubImage2D(m_target, mipLevel, x, y, width, height, m_format, m_type, data);
    GL_CHECK("glTexSubImage2D");
}

void FOpenGLTexture::GenerateMipmaps()
{
    if (m_desc.mipLevels <= 1)
    {
        return;
    }
    
    Bind(0);
    glGenerateMipmap(m_target);
    GL_CHECK("glGenerateMipmap");
}

bool FOpenGLTexture::IsDepthStencil() const
{
    return hasResourceUsage(m_desc.usage, EResourceUsage::DepthStencil);
}

bool FOpenGLTexture::IsRenderTarget() const
{
    return hasResourceUsage(m_desc.usage, EResourceUsage::RenderTarget);
}

bool FOpenGLTexture::CreateTexture(const void* initialData, uint32 dataSize)
{
    // Check if required OpenGL functions are loaded
    if (!glGenTextures || !glBindTexture || !glTexStorage2D || !glTexSubImage2D)
    {
        OutputDebugStringA("OpenGL: Required texture functions not loaded!\n");
        return false;
    }
    
    glGenTextures(1, &m_texture);
    if (!m_texture)
    {
        OutputDebugStringA("OpenGL: Failed to generate texture\n");
        return false;
    }
    
    Bind(0);
    
    // Use immutable storage (glTexStorage) for better performance
    switch (m_target)
    {
        case GL_TEXTURE_1D:
            glTexStorage1D(m_target, m_desc.mipLevels, m_internalFormat, m_desc.width);
            if (initialData)
            {
                glTexSubImage1D(m_target, 0, 0, m_desc.width, m_format, m_type, initialData);
            }
            break;
            
        case GL_TEXTURE_2D:
            glTexStorage2D(m_target, m_desc.mipLevels, m_internalFormat, m_desc.width, m_desc.height);
            if (initialData)
            {
                glTexSubImage2D(m_target, 0, 0, 0, m_desc.width, m_desc.height, m_format, m_type, initialData);
            }
            break;
            
        case GL_TEXTURE_3D:
            glTexStorage3D(m_target, m_desc.mipLevels, m_internalFormat, m_desc.width, m_desc.height, m_desc.depth);
            if (initialData)
            {
                glTexSubImage3D(m_target, 0, 0, 0, 0, m_desc.width, m_desc.height, m_desc.depth, m_format, m_type, initialData);
            }
            break;
            
        case GL_TEXTURE_2D_ARRAY:
        case GL_TEXTURE_CUBE_MAP:
            glTexStorage2D(m_target, m_desc.mipLevels, m_internalFormat, m_desc.width, m_desc.height);
            // Initial data upload for arrays/cubemaps is more complex
            break;
            
        default:
            OutputDebugStringA("OpenGL: Error\n");
            return false;
    }
    
    GL_CHECK("CreateTexture");
    
    // Set default sampler parameters
    glTexParameteri(m_target, GL_TEXTURE_MIN_FILTER, m_desc.mipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(m_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(m_target, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(m_target, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(m_target, GL_TEXTURE_WRAP_R, GL_REPEAT);
    
    // Generate mipmaps if initial data was provided
    if (initialData && m_desc.mipLevels > 1)
    {
        glGenerateMipmap(m_target);
    }
    
    // Set debug label
    if (glObjectLabel && !m_debugName.empty())
    {
        glObjectLabel(GL_TEXTURE, m_texture, -1, m_debugName.c_str());
    }
    
    OutputDebugStringA("OpenGL: Debug\n");
    
    return true;
}

GLenum FOpenGLTexture::DetermineTarget() const
{
    if (m_desc.depth > 1)
    {
        return GL_TEXTURE_3D;
    }
    if (m_desc.arraySize > 1)
    {
        if (m_desc.arraySize == 6)
        {
            return GL_TEXTURE_CUBE_MAP;
        }
        return GL_TEXTURE_2D_ARRAY;
    }
    if (m_desc.height == 1)
    {
        return GL_TEXTURE_1D;
    }
    return GL_TEXTURE_2D;
}

void FOpenGLTexture::ConvertPixelFormat(EPixelFormat format,
                                         GLenum& internalFormat, 
                                         GLenum& glFormat, 
                                         GLenum& glType) const
{
    switch (format)
    {
        case EPixelFormat::R8_UNORM:
            internalFormat = GL_R8;
            glFormat = GL_RED;
            glType = GL_UNSIGNED_BYTE;
            break;
            
        case EPixelFormat::R8_SRGB:
            internalFormat = GL_R8;  // No sRGB for single channel
            glFormat = GL_RED;
            glType = GL_UNSIGNED_BYTE;
            break;
            
        case EPixelFormat::R8G8_UNORM:
            internalFormat = GL_RG8;
            glFormat = GL_RG;
            glType = GL_UNSIGNED_BYTE;
            break;
            
        case EPixelFormat::R8G8B8A8_UNORM:
            internalFormat = GL_RGBA8;
            glFormat = GL_RGBA;
            glType = GL_UNSIGNED_BYTE;
            break;
            
        case EPixelFormat::R8G8B8A8_SRGB:
            internalFormat = GL_SRGB8_ALPHA8;
            glFormat = GL_RGBA;
            glType = GL_UNSIGNED_BYTE;
            break;
            
        case EPixelFormat::B8G8R8A8_UNORM:
            internalFormat = GL_RGBA8;
            glFormat = GL_BGRA;
            glType = GL_UNSIGNED_BYTE;
            break;
            
        case EPixelFormat::B8G8R8A8_SRGB:
            internalFormat = GL_SRGB8_ALPHA8;
            glFormat = GL_BGRA;
            glType = GL_UNSIGNED_BYTE;
            break;
            
        case EPixelFormat::R32_FLOAT:
            internalFormat = GL_R32F;
            glFormat = GL_RED;
            glType = GL_FLOAT;
            break;
            
        case EPixelFormat::R32G32_FLOAT:
            internalFormat = GL_RG32F;
            glFormat = GL_RG;
            glType = GL_FLOAT;
            break;
            
        case EPixelFormat::R32G32B32_FLOAT:
            internalFormat = GL_RGB32F;
            glFormat = GL_RGB;
            glType = GL_FLOAT;
            break;
            
        case EPixelFormat::R32G32B32A32_FLOAT:
            internalFormat = GL_RGBA32F;
            glFormat = GL_RGBA;
            glType = GL_FLOAT;
            break;
            
        case EPixelFormat::D32_FLOAT:
            internalFormat = GL_DEPTH_COMPONENT32F;
            glFormat = GL_DEPTH_COMPONENT;
            glType = GL_FLOAT;
            break;
            
        case EPixelFormat::D24_UNORM_S8_UINT:
            internalFormat = GL_DEPTH24_STENCIL8;
            glFormat = GL_DEPTH_STENCIL;
            glType = GL_UNSIGNED_INT_24_8;
            break;
            
        case EPixelFormat::BC1_UNORM:
            internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
            glFormat = GL_RGBA;
            glType = GL_UNSIGNED_BYTE;
            break;
            
        case EPixelFormat::BC1_SRGB:
            internalFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
            glFormat = GL_RGBA;
            glType = GL_UNSIGNED_BYTE;
            break;
            
        case EPixelFormat::BC3_UNORM:
            internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            glFormat = GL_RGBA;
            glType = GL_UNSIGNED_BYTE;
            break;
            
        case EPixelFormat::BC3_SRGB:
            internalFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
            glFormat = GL_RGBA;
            glType = GL_UNSIGNED_BYTE;
            break;
            
        default:
            OutputDebugStringA("OpenGL: Warning\n");
            internalFormat = GL_RGBA8;
            glFormat = GL_RGBA;
            glType = GL_UNSIGNED_BYTE;
            break;
    }
}

// ============================================================================
// FOpenGLSampler Implementation
// ============================================================================

FOpenGLSampler::FOpenGLSampler(const FSamplerDesc& desc)
    : m_desc(desc)
{
    if (!CreateSampler())
    {
        OutputDebugStringA("OpenGL: Error\n");
    }
}

FOpenGLSampler::~FOpenGLSampler()
{
    if (m_sampler)
    {
        glDeleteSamplers(1, &m_sampler);
        m_sampler = 0;
    }
}

void FOpenGLSampler::Bind(uint32 unit) const
{
    glBindSampler(unit, m_sampler);
}

void FOpenGLSampler::Unbind(uint32 unit) const
{
    glBindSampler(unit, 0);
}

bool FOpenGLSampler::CreateSampler()
{
    glGenSamplers(1, &m_sampler);
    if (!m_sampler)
    {
        return false;
    }
    
    // Set filter modes
    glSamplerParameteri(m_sampler, GL_TEXTURE_MIN_FILTER, 
                        ConvertMinFilter(m_desc.minFilter, m_desc.mipFilter));
    glSamplerParameteri(m_sampler, GL_TEXTURE_MAG_FILTER, 
                        ConvertMagFilter(m_desc.magFilter));
    
    // Set address modes
    glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_S, ConvertAddressMode(m_desc.addressU));
    glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_T, ConvertAddressMode(m_desc.addressV));
    glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_R, ConvertAddressMode(m_desc.addressW));
    
    // Set LOD parameters
    glSamplerParameterf(m_sampler, GL_TEXTURE_MIN_LOD, m_desc.minLOD);
    glSamplerParameterf(m_sampler, GL_TEXTURE_MAX_LOD, m_desc.maxLOD);
    glSamplerParameterf(m_sampler, GL_TEXTURE_LOD_BIAS, m_desc.mipLODBias);
    
    // Set anisotropy
    if (m_desc.maxAnisotropy > 1)
    {
        glSamplerParameterf(m_sampler, GL_TEXTURE_MAX_ANISOTROPY, 
                           static_cast<float>(m_desc.maxAnisotropy));
    }
    
    // Set comparison mode
    if (m_desc.compareEnable)
    {
        glSamplerParameteri(m_sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(m_sampler, GL_TEXTURE_COMPARE_FUNC, 
                           ConvertCompareFunc(m_desc.compareFunc));
    }
    
    // Set border color
    glSamplerParameterfv(m_sampler, GL_TEXTURE_BORDER_COLOR, m_desc.borderColor);
    
    GL_CHECK("CreateSampler");
    
    return true;
}

GLenum FOpenGLSampler::ConvertMinFilter(FSamplerDesc::EFilter min, FSamplerDesc::EFilter mip) const
{
    if (min == FSamplerDesc::EFilter::Nearest)
    {
        if (mip == FSamplerDesc::EFilter::Nearest)
            return GL_NEAREST_MIPMAP_NEAREST;
        else
            return GL_NEAREST_MIPMAP_LINEAR;
    }
    else
    {
        if (mip == FSamplerDesc::EFilter::Nearest)
            return GL_LINEAR_MIPMAP_NEAREST;
        else
            return GL_LINEAR_MIPMAP_LINEAR;
    }
}

GLenum FOpenGLSampler::ConvertMagFilter(FSamplerDesc::EFilter mag) const
{
    return (mag == FSamplerDesc::EFilter::Nearest) ? GL_NEAREST : GL_LINEAR;
}

GLenum FOpenGLSampler::ConvertAddressMode(FSamplerDesc::EAddressMode mode) const
{
    switch (mode)
    {
        case FSamplerDesc::EAddressMode::Wrap:   return GL_REPEAT;
        case FSamplerDesc::EAddressMode::Clamp:  return GL_CLAMP_TO_EDGE;
        case FSamplerDesc::EAddressMode::Mirror: return GL_MIRRORED_REPEAT;
        case FSamplerDesc::EAddressMode::Border: return GL_CLAMP_TO_BORDER;
        default: return GL_REPEAT;
    }
}

GLenum FOpenGLSampler::ConvertCompareFunc(EComparisonFunc func) const
{
    switch (func)
    {
        case EComparisonFunc::Never:        return GL_NEVER;
        case EComparisonFunc::Less:         return GL_LESS;
        case EComparisonFunc::Equal:        return GL_EQUAL;
        case EComparisonFunc::LessEqual:    return GL_LEQUAL;
        case EComparisonFunc::Greater:      return GL_GREATER;
        case EComparisonFunc::NotEqual:     return GL_NOTEQUAL;
        case EComparisonFunc::GreaterEqual: return GL_GEQUAL;
        case EComparisonFunc::Always:       return GL_ALWAYS;
        default: return GL_LESS;
    }
}

// ============================================================================
// FOpenGLFramebuffer Implementation
// ============================================================================

FOpenGLFramebuffer::FOpenGLFramebuffer()
{
    glGenFramebuffers(1, &m_framebuffer);
}

FOpenGLFramebuffer::~FOpenGLFramebuffer()
{
    if (m_framebuffer)
    {
        glDeleteFramebuffers(1, &m_framebuffer);
        m_framebuffer = 0;
    }
}

void FOpenGLFramebuffer::SetColorAttachment(uint32 index, FOpenGLTexture* texture, 
                                             uint32 mipLevel, uint32 layer)
{
    if (index >= MaxColorAttachments)
    {
        OutputDebugStringA("OpenGL: Error\n");
        return;
    }
    
    Bind();
    
    if (texture)
    {
        GLenum attachment = GL_COLOR_ATTACHMENT0 + index;
        
        if (texture->GetGLTarget() == GL_TEXTURE_2D)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 
                                   texture->GetGLTexture(), mipLevel);
        }
        else
        {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, 
                                      texture->GetGLTexture(), mipLevel, layer);
        }
        
        m_colorAttachments[index] = texture;
        if (index >= m_numColorAttachments)
        {
            m_numColorAttachments = index + 1;
        }
    }
    else
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D, 0, 0);
        m_colorAttachments[index] = nullptr;
    }
    
    UpdateDrawBuffers();
}

void FOpenGLFramebuffer::SetDepthStencilAttachment(FOpenGLTexture* texture, 
                                                    uint32 mipLevel, uint32 layer)
{
    Bind();
    
    if (texture)
    {
        GLenum attachment = GL_DEPTH_ATTACHMENT;
        
        // Check if it has stencil
        EPixelFormat format = texture->getFormat();
        if (format == EPixelFormat::D24_UNORM_S8_UINT)
        {
            attachment = GL_DEPTH_STENCIL_ATTACHMENT;
        }
        
        if (texture->GetGLTarget() == GL_TEXTURE_2D)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 
                                   texture->GetGLTexture(), mipLevel);
        }
        else
        {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, 
                                      texture->GetGLTexture(), mipLevel, layer);
        }
        
        m_depthStencilAttachment = texture;
    }
    else
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        m_depthStencilAttachment = nullptr;
    }
}

void FOpenGLFramebuffer::ClearAttachments()
{
    Bind();
    
    for (uint32 i = 0; i < MaxColorAttachments; ++i)
    {
        if (m_colorAttachments[i])
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, 0, 0);
            m_colorAttachments[i] = nullptr;
        }
    }
    
    if (m_depthStencilAttachment)
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        m_depthStencilAttachment = nullptr;
    }
    
    m_numColorAttachments = 0;
}

void FOpenGLFramebuffer::Bind(GLenum target) const
{
    glBindFramebuffer(target, m_framebuffer);
}

void FOpenGLFramebuffer::BindDefault(GLenum target)
{
    glBindFramebuffer(target, 0);
}

bool FOpenGLFramebuffer::IsComplete() const
{
    Bind();
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    return status == GL_FRAMEBUFFER_COMPLETE;
}

void FOpenGLFramebuffer::UpdateDrawBuffers()
{
    if (m_numColorAttachments == 0)
    {
        GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
        return;
    }
    
    GLenum drawBuffers[MaxColorAttachments];
    for (uint32 i = 0; i < m_numColorAttachments; ++i)
    {
        drawBuffers[i] = m_colorAttachments[i] ? (GL_COLOR_ATTACHMENT0 + i) : GL_NONE;
    }
    
    glDrawBuffers(m_numColorAttachments, drawBuffers);
}

// ============================================================================
// FOpenGLVertexArray Implementation
// ============================================================================

FOpenGLVertexArray::FOpenGLVertexArray()
{
    glGenVertexArrays(1, &m_vao);
}

FOpenGLVertexArray::~FOpenGLVertexArray()
{
    if (m_vao)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
}

void FOpenGLVertexArray::Bind() const
{
    glBindVertexArray(m_vao);
}

void FOpenGLVertexArray::Unbind()
{
    glBindVertexArray(0);
}

void FOpenGLVertexArray::SetVertexAttribute(uint32 index, GLint size, GLenum type, 
                                             GLboolean normalized, GLsizei stride, 
                                             const void* offset)
{
    glVertexAttribPointer(index, size, type, normalized, stride, offset);
}

void FOpenGLVertexArray::EnableAttribute(uint32 index)
{
    glEnableVertexAttribArray(index);
}

void FOpenGLVertexArray::DisableAttribute(uint32 index)
{
    glDisableVertexAttribArray(index);
}

void FOpenGLVertexArray::SetAttributeDivisor(uint32 index, uint32 divisor)
{
    glVertexAttribDivisor(index, divisor);
}

} // namespace MonsterEngine::OpenGL
