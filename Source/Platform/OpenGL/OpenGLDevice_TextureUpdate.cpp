// Copyright Monster Engine. All Rights Reserved.
// OpenGL Texture Update Implementation

#include "Platform/OpenGL/OpenGLDevice.h"
#include "Platform/OpenGL/OpenGLResources.h"
#include "Core/Log.h"
#include "Core/HAL/FMemory.h"
#include <glad/glad.h>

DEFINE_LOG_CATEGORY_STATIC(LogOpenGLTextureUpdate, Log, All);

namespace MonsterEngine::OpenGL {

/**
 * Update texture subresource data
 * Reference: UE5 FOpenGLDynamicRHI::RHIUpdateTexture2D
 */
bool FOpenGLDevice::updateTextureSubresource(
    TSharedPtr<MonsterRender::RHI::IRHITexture> texture,
    uint32 mipLevel,
    const void* data,
    SIZE_T dataSize)
{
    using namespace MonsterRender::RHI;
    
    if (!texture || !data || dataSize == 0) {
        MR_LOG(LogOpenGLTextureUpdate, Error, "Invalid parameters for texture update");
        return false;
    }
    
    // Cast to OpenGL texture
    FOpenGLTexture* glTexture = dynamic_cast<FOpenGLTexture*>(texture.get());
    if (!glTexture) {
        MR_LOG(LogOpenGLTextureUpdate, Error, "Texture is not an OpenGL texture");
        return false;
    }
    
    GLuint textureID = glTexture->GetGLTexture();
    if (textureID == 0) {
        MR_LOG(LogOpenGLTextureUpdate, Error, "Invalid OpenGL texture handle");
        return false;
    }
    
    const MonsterRender::RHI::TextureDesc& desc = texture->getDesc();
    
    // Validate mip level
    if (mipLevel >= desc.mipLevels) {
        MR_LOG(LogOpenGLTextureUpdate, Error, "Mip level %u exceeds texture mip count %u", 
               mipLevel, desc.mipLevels);
        return false;
    }
    
    // Calculate mip dimensions
    uint32 mipWidth = std::max(1u, desc.width >> mipLevel);
    uint32 mipHeight = std::max(1u, desc.height >> mipLevel);
    
    MR_LOG(LogOpenGLTextureUpdate, VeryVerbose, "Updating texture mip %u: %ux%u (%llu bytes)",
           mipLevel, mipWidth, mipHeight, static_cast<uint64>(dataSize));
    
    // Bind texture
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    // Determine OpenGL format and type
    GLenum glFormat = GL_RGBA;
    GLenum glType = GL_UNSIGNED_BYTE;
    GLenum glInternalFormat = GL_RGBA8;
    bool isCompressed = false;
    
    switch (desc.format) {
        case EPixelFormat::R8G8B8A8_UNORM:
            glFormat = GL_RGBA;
            glType = GL_UNSIGNED_BYTE;
            glInternalFormat = GL_RGBA8;
            break;
            
        case EPixelFormat::R8G8B8A8_SRGB:
            glFormat = GL_RGBA;
            glType = GL_UNSIGNED_BYTE;
            glInternalFormat = GL_SRGB8_ALPHA8;
            break;
            
        case EPixelFormat::BC1_UNORM:
            glInternalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
            isCompressed = true;
            break;
            
        case EPixelFormat::BC3_UNORM:
            glInternalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            isCompressed = true;
            break;
            
        case EPixelFormat::BC7_UNORM:
            glInternalFormat = GL_COMPRESSED_RGBA_BPTC_UNORM;
            isCompressed = true;
            break;
            
        default:
            MR_LOG(LogOpenGLTextureUpdate, Warning, "Unsupported texture format, using RGBA8");
            break;
    }
    
    // Upload texture data
    if (isCompressed) {
        // Compressed texture upload
        glCompressedTexSubImage2D(
            GL_TEXTURE_2D,
            mipLevel,
            0, 0,                    // x, y offset
            mipWidth, mipHeight,
            glInternalFormat,
            static_cast<GLsizei>(dataSize),
            data
        );
    } else {
        // Uncompressed texture upload
        glTexSubImage2D(
            GL_TEXTURE_2D,
            mipLevel,
            0, 0,                    // x, y offset
            mipWidth, mipHeight,
            glFormat,
            glType,
            data
        );
    }
    
    // Check for errors
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        MR_LOG(LogOpenGLTextureUpdate, Error, "OpenGL error during texture update: 0x%x", error);
        glBindTexture(GL_TEXTURE_2D, 0);
        return false;
    }
    
    // Unbind texture
    glBindTexture(GL_TEXTURE_2D, 0);
    
    MR_LOG(LogOpenGLTextureUpdate, Verbose, "Successfully updated texture mip %u", mipLevel);
    return true;
}

} // namespace MonsterEngine::OpenGL
