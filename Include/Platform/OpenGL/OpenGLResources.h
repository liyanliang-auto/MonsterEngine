#pragma once

/**
 * OpenGLResources.h
 * 
 * OpenGL 4.6 resource implementations (Buffer, Texture, Sampler)
 * Reference: UE5 OpenGLDrv/Public/OpenGLResources.h
 */

#include "Core/CoreMinimal.h"
#include "RHI/IRHIResource.h"
#include "Platform/OpenGL/OpenGLDefinitions.h"

namespace MonsterEngine::OpenGL {

// Forward declarations
class FOpenGLDevice;

// ============================================================================
// OpenGL Buffer
// ============================================================================

/**
 * OpenGL buffer implementation
 * Reference: UE5 FOpenGLBuffer
 */
class FOpenGLBuffer : public MonsterRender::RHI::IRHIBuffer
{
public:
    FOpenGLBuffer(const MonsterRender::RHI::BufferDesc& desc);
    virtual ~FOpenGLBuffer();
    
    // IRHIBuffer interface
    virtual void* map() override;
    virtual void unmap() override;
    
    /**
     * Get the OpenGL buffer handle
     */
    GLuint GetGLBuffer() const { return m_buffer; }
    
    /**
     * Get the OpenGL buffer target
     */
    GLenum GetGLTarget() const { return m_target; }
    
    /**
     * Update buffer data
     */
    void UpdateData(const void* data, uint32 size, uint32 offset = 0);
    
    /**
     * Bind this buffer to its target
     */
    void Bind() const;
    
    /**
     * Unbind this buffer
     */
    void Unbind() const;
    
    /**
     * Bind to indexed target (for uniform/storage buffers)
     */
    void BindBase(uint32 index) const;
    
private:
    /**
     * Create the OpenGL buffer
     */
    bool CreateBuffer(const void* initialData);
    
    /**
     * Determine the GL target from usage flags
     */
    GLenum DetermineTarget() const;
    
    /**
     * Determine the GL usage hint
     */
    GLenum DetermineUsage() const;
    
private:
    GLuint m_buffer = 0;
    GLenum m_target = 0;
    GLenum m_usage = 0;
    void* m_mappedPtr = nullptr;
    bool m_persistentMapping = false;
};

// ============================================================================
// OpenGL Texture
// ============================================================================

/**
 * OpenGL texture implementation
 * Reference: UE5 FOpenGLTexture
 */
class FOpenGLTexture : public MonsterRender::RHI::IRHITexture
{
public:
    FOpenGLTexture(const MonsterRender::RHI::TextureDesc& desc);
    virtual ~FOpenGLTexture();
    
    /**
     * Get the OpenGL texture handle
     */
    GLuint GetGLTexture() const { return m_texture; }
    
    /**
     * Get the OpenGL texture target
     */
    GLenum GetGLTarget() const { return m_target; }
    
    /**
     * Get the OpenGL internal format
     */
    GLenum GetGLInternalFormat() const { return m_internalFormat; }
    
    /**
     * Get the OpenGL format
     */
    GLenum GetGLFormat() const { return m_format; }
    
    /**
     * Get the OpenGL type
     */
    GLenum GetGLType() const { return m_type; }
    
    /**
     * Bind this texture to a texture unit
     */
    void Bind(uint32 unit = 0) const;
    
    /**
     * Unbind this texture
     */
    void Unbind(uint32 unit = 0) const;
    
    /**
     * Update texture data (2D)
     */
    void UpdateData2D(const void* data, uint32 mipLevel = 0, 
                      uint32 x = 0, uint32 y = 0, 
                      uint32 width = 0, uint32 height = 0);
    
    /**
     * Generate mipmaps
     */
    void GenerateMipmaps();
    
    /**
     * Check if this is a depth/stencil texture
     */
    bool IsDepthStencil() const;
    
    /**
     * Check if this is a render target
     */
    bool IsRenderTarget() const;
    
private:
    /**
     * Create the OpenGL texture
     */
    bool CreateTexture(const void* initialData, uint32 dataSize);
    
    /**
     * Determine the GL target from texture type
     */
    GLenum DetermineTarget() const;
    
    /**
     * Convert pixel format to GL formats
     */
    void ConvertPixelFormat(MonsterRender::RHI::EPixelFormat format,
                           GLenum& internalFormat, GLenum& glFormat, GLenum& glType) const;
    
private:
    GLuint m_texture = 0;
    GLenum m_target = 0;
    GLenum m_internalFormat = 0;
    GLenum m_format = 0;
    GLenum m_type = 0;
};

// ============================================================================
// OpenGL Sampler
// ============================================================================

/**
 * Sampler description
 */
struct FSamplerDesc
{
    enum class EFilter : uint8
    {
        Nearest,
        Linear,
        Anisotropic
    };
    
    enum class EAddressMode : uint8
    {
        Wrap,
        Clamp,
        Mirror,
        Border
    };
    
    EFilter minFilter = EFilter::Linear;
    EFilter magFilter = EFilter::Linear;
    EFilter mipFilter = EFilter::Linear;
    EAddressMode addressU = EAddressMode::Wrap;
    EAddressMode addressV = EAddressMode::Wrap;
    EAddressMode addressW = EAddressMode::Wrap;
    float mipLODBias = 0.0f;
    float minLOD = -1000.0f;
    float maxLOD = 1000.0f;
    uint32 maxAnisotropy = 1;
    bool compareEnable = false;
    MonsterRender::RHI::EComparisonFunc compareFunc = MonsterRender::RHI::EComparisonFunc::Less;
    float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    
    FSamplerDesc() = default;
};

/**
 * OpenGL sampler implementation
 * Reference: UE5 FOpenGLSamplerState
 */
class FOpenGLSampler : public MonsterRender::RHI::IRHISampler
{
public:
    FOpenGLSampler(const FSamplerDesc& desc);
    virtual ~FOpenGLSampler();
    
    /**
     * Get the OpenGL sampler handle
     */
    GLuint GetGLSampler() const { return m_sampler; }
    
    /**
     * Bind this sampler to a texture unit
     */
    void Bind(uint32 unit) const;
    
    /**
     * Unbind this sampler
     */
    void Unbind(uint32 unit) const;
    
    /**
     * Get the sampler description
     */
    const FSamplerDesc& GetDesc() const { return m_desc; }
    
private:
    /**
     * Create the OpenGL sampler
     */
    bool CreateSampler();
    
    /**
     * Convert filter to GL enum
     */
    GLenum ConvertMinFilter(FSamplerDesc::EFilter min, FSamplerDesc::EFilter mip) const;
    GLenum ConvertMagFilter(FSamplerDesc::EFilter mag) const;
    GLenum ConvertAddressMode(FSamplerDesc::EAddressMode mode) const;
    GLenum ConvertCompareFunc(MonsterRender::RHI::EComparisonFunc func) const;
    
private:
    GLuint m_sampler = 0;
    FSamplerDesc m_desc;
};

// ============================================================================
// OpenGL Framebuffer
// ============================================================================

/**
 * OpenGL framebuffer for render targets
 */
class FOpenGLFramebuffer
{
public:
    FOpenGLFramebuffer();
    ~FOpenGLFramebuffer();
    
    /**
     * Set color attachment
     */
    void SetColorAttachment(uint32 index, FOpenGLTexture* texture, uint32 mipLevel = 0, uint32 layer = 0);
    
    /**
     * Set depth/stencil attachment
     */
    void SetDepthStencilAttachment(FOpenGLTexture* texture, uint32 mipLevel = 0, uint32 layer = 0);
    
    /**
     * Clear all attachments
     */
    void ClearAttachments();
    
    /**
     * Bind this framebuffer
     */
    void Bind(GLenum target = GL_FRAMEBUFFER) const;
    
    /**
     * Unbind (bind default framebuffer)
     */
    static void BindDefault(GLenum target = GL_FRAMEBUFFER);
    
    /**
     * Check if framebuffer is complete
     */
    bool IsComplete() const;
    
    /**
     * Get the OpenGL framebuffer handle
     */
    GLuint GetGLFramebuffer() const { return m_framebuffer; }
    
    /**
     * Get number of color attachments
     */
    uint32 GetNumColorAttachments() const { return m_numColorAttachments; }
    
private:
    /**
     * Update draw buffers
     */
    void UpdateDrawBuffers();
    
private:
    GLuint m_framebuffer = 0;
    uint32 m_numColorAttachments = 0;
    static constexpr uint32 MaxColorAttachments = 8;
    FOpenGLTexture* m_colorAttachments[MaxColorAttachments] = {};
    FOpenGLTexture* m_depthStencilAttachment = nullptr;
};

// ============================================================================
// OpenGL Vertex Array Object
// ============================================================================

/**
 * OpenGL VAO wrapper
 */
class FOpenGLVertexArray
{
public:
    FOpenGLVertexArray();
    ~FOpenGLVertexArray();
    
    /**
     * Bind this VAO
     */
    void Bind() const;
    
    /**
     * Unbind (bind VAO 0)
     */
    static void Unbind();
    
    /**
     * Get the OpenGL VAO handle
     */
    GLuint GetGLVertexArray() const { return m_vao; }
    
    /**
     * Setup vertex attribute
     */
    void SetVertexAttribute(uint32 index, GLint size, GLenum type, 
                           GLboolean normalized, GLsizei stride, 
                           const void* offset);
    
    /**
     * Enable vertex attribute
     */
    void EnableAttribute(uint32 index);
    
    /**
     * Disable vertex attribute
     */
    void DisableAttribute(uint32 index);
    
    /**
     * Set vertex attribute divisor (for instancing)
     */
    void SetAttributeDivisor(uint32 index, uint32 divisor);
    
private:
    GLuint m_vao = 0;
};

} // namespace MonsterEngine::OpenGL
