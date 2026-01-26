// Copyright Monster Engine. All Rights Reserved.
// OpenGL Async Texture Update Implementation using PBO

#include "Platform/OpenGL/OpenGLDevice.h"
#include "Platform/OpenGL/OpenGLResources.h"
#include "Core/Log.h"
#include "Core/HAL/FMemory.h"
#include "glad/glad.h"

DEFINE_LOG_CATEGORY_STATIC(LogOpenGLAsyncTextureUpdate, Log, All);

namespace MonsterEngine::OpenGL {

using namespace MonsterRender::RHI;

/**
 * Update texture subresource asynchronously using PBO
 * Reference: OpenGL PBO for asynchronous texture uploads
 */
bool FOpenGLDevice::updateTextureSubresourceAsync(
    TSharedPtr<IRHITexture> texture,
    uint32 mipLevel,
    const void* data,
    SIZE_T dataSize,
    uint64* outFenceValue)
{
    if (!texture || !data || dataSize == 0) {
        MR_LOG(LogOpenGLAsyncTextureUpdate, Error, "Invalid parameters for async texture update");
        return false;
    }
    
    // Cast to OpenGL texture
    FOpenGLTexture* glTexture = dynamic_cast<FOpenGLTexture*>(texture.get());
    if (!glTexture) {
        MR_LOG(LogOpenGLAsyncTextureUpdate, Error, "Texture is not an OpenGL texture");
        return false;
    }
    
    GLuint textureID = glTexture->GetGLTexture();
    if (textureID == 0) {
        MR_LOG(LogOpenGLAsyncTextureUpdate, Error, "Invalid OpenGL texture handle");
        return false;
    }
    
    const TextureDesc& desc = texture->getDesc();
    
    // Validate mip level
    if (mipLevel >= desc.mipLevels) {
        MR_LOG(LogOpenGLAsyncTextureUpdate, Error, "Mip level %u exceeds texture mip count %u", 
               mipLevel, desc.mipLevels);
        return false;
    }
    
    MR_LOG(LogOpenGLAsyncTextureUpdate, VeryVerbose, "Async updating texture mip %u (%llu bytes)",
           mipLevel, static_cast<uint64>(dataSize));
    
    // Create PBO for async upload
    GLuint pbo = beginAsyncUploadPBO(dataSize);
    if (pbo == 0) {
        MR_LOG(LogOpenGLAsyncTextureUpdate, Error, "Failed to create PBO for async upload");
        return false;
    }
    
    // Map PBO and copy data
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    void* mappedData = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
    if (!mappedData) {
        MR_LOG(LogOpenGLAsyncTextureUpdate, Error, "Failed to map PBO");
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        destroyAsyncUploadPBO(pbo);
        return false;
    }
    
    // Copy data to PBO
    FMemory::Memcpy(mappedData, data, dataSize);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    
    // Submit async upload
    bool success = submitAsyncUploadPBO(pbo, texture, mipLevel);
    if (!success) {
        MR_LOG(LogOpenGLAsyncTextureUpdate, Error, "Failed to submit async PBO upload");
        destroyAsyncUploadPBO(pbo);
        return false;
    }
    
    // Return PBO as fence value if requested
    if (outFenceValue) {
        *outFenceValue = static_cast<uint64>(pbo);
    }
    
    MR_LOG(LogOpenGLAsyncTextureUpdate, Verbose, "Successfully submitted async texture mip %u upload", mipLevel);
    return true;
}

} // namespace MonsterEngine::OpenGL
