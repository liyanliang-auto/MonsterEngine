#include "Core/CoreMinimal.h"
#include "RHI/IRHIResource.h"

namespace MonsterRender::RHI {
    
    uint32 IRHITexture::getSize() const {
        // Calculate estimated texture size based on format and dimensions
        uint32 bytesPerPixel = 0;
        
        switch (m_desc.format) {
            case EPixelFormat::R8G8B8A8_UNORM:
            case EPixelFormat::B8G8R8A8_UNORM:
            case EPixelFormat::R8G8B8A8_SRGB:
            case EPixelFormat::B8G8R8A8_SRGB:
            case EPixelFormat::R32_FLOAT:
            case EPixelFormat::D32_FLOAT:
            case EPixelFormat::D24_UNORM_S8_UINT:
                bytesPerPixel = 4;
                break;
                
            case EPixelFormat::R32G32B32A32_FLOAT:
                bytesPerPixel = 16;
                break;
                
            case EPixelFormat::R32G32B32_FLOAT:
                bytesPerPixel = 12;
                break;
                
            case EPixelFormat::R32G32_FLOAT:
                bytesPerPixel = 8;
                break;
                
            case EPixelFormat::BC1_UNORM:
            case EPixelFormat::BC1_SRGB:
                // BC1 uses 4 bits per pixel
                return ((m_desc.width + 3) / 4) * ((m_desc.height + 3) / 4) * 8 * m_desc.arraySize;
                
            case EPixelFormat::BC3_UNORM:
            case EPixelFormat::BC3_SRGB:
                // BC3 uses 8 bits per pixel
                return ((m_desc.width + 3) / 4) * ((m_desc.height + 3) / 4) * 16 * m_desc.arraySize;
                
            default:
                MR_LOG_WARNING("Unknown texture format for size calculation");
                bytesPerPixel = 4; // Default to 4 bytes per pixel
                break;
        }
        
        uint32 totalSize = 0;
        uint32 width = m_desc.width;
        uint32 height = m_desc.height;
        uint32 depth = m_desc.depth;
        
        // Calculate size including all mip levels
        for (uint32 mip = 0; mip < m_desc.mipLevels; ++mip) {
            totalSize += width * height * depth * bytesPerPixel;
            
            width = (width > 1) ? width / 2 : 1;
            height = (height > 1) ? height / 2 : 1;
            depth = (depth > 1) ? depth / 2 : 1;
        }
        
        return totalSize * m_desc.arraySize;
    }
}
