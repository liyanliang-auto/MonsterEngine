/**
 * OpenGLSwapChain.cpp
 * 
 * OpenGL SwapChain implementation
 */

#include "Platform/OpenGL/OpenGLSwapChain.h"
#include "Platform/OpenGL/OpenGLDevice.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Core/Logging/LogMacros.h"

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogOpenGLSwapChain, Log, All);

namespace MonsterEngine::OpenGL {

using namespace MonsterRender::RHI;

// ============================================================================
// FOpenGLSwapChain Implementation
// ============================================================================

FOpenGLSwapChain::FOpenGLSwapChain(FOpenGLDevice* device, const SwapChainDesc& desc)
    : m_device(device)
{
    m_desc = desc;
    
    MR_LOG_INFO("Creating OpenGL SwapChain");
    
    // Create depth buffer
    if (!CreateDepthBuffer())
    {
        MR_LOG_ERROR("Failed to create depth buffer");
        return;
    }
    
    // Create backbuffer texture wrapper
    if (!CreateBackBufferTexture())
    {
        MR_LOG_ERROR("Failed to create backbuffer texture");
        return;
    }
    
    // Set initial vsync
    SetVSync(desc.vsync);
    
    m_initialized = true;
    
    MR_LOG_INFO("OpenGL SwapChain created successfully");
}

FOpenGLSwapChain::~FOpenGLSwapChain()
{
    MR_LOG_INFO("Destroying OpenGL SwapChain");
    
    DestroyDepthBuffer();
    
    m_backBufferTexture.Reset();
    m_depthTexture.Reset();
    
    m_initialized = false;
}

TSharedPtr<IRHITexture> FOpenGLSwapChain::GetCurrentBackBuffer()
{
    return m_backBufferTexture;
}

uint32 FOpenGLSwapChain::GetCurrentBackBufferIndex() const
{
    return m_currentBuffer;
}

uint32 FOpenGLSwapChain::GetBackBufferCount() const
{
    // OpenGL double buffering
    return 2;
}

EPixelFormat FOpenGLSwapChain::GetBackBufferFormat() const
{
    return m_desc.format;
}

void FOpenGLSwapChain::GetDimensions(uint32& outWidth, uint32& outHeight) const
{
    outWidth = m_desc.width;
    outHeight = m_desc.height;
}

ESwapChainStatus FOpenGLSwapChain::AcquireNextImage()
{
    // OpenGL doesn't have explicit image acquisition
    // The default framebuffer is always available
    return ESwapChainStatus::OK;
}

ESwapChainStatus FOpenGLSwapChain::Present()
{
    // Swap buffers through context manager
    m_device->GetContextManager().SwapBuffers();
    
    // Toggle buffer index for tracking
    m_currentBuffer = (m_currentBuffer + 1) % 2;
    
    return ESwapChainStatus::OK;
}

bool FOpenGLSwapChain::Resize(uint32 newWidth, uint32 newHeight)
{
    if (newWidth == m_desc.width && newHeight == m_desc.height)
    {
        return true;
    }
    
    MR_LOG_INFO("Resizing SwapChain");
    
    m_desc.width = newWidth;
    m_desc.height = newHeight;
    
    // Recreate depth buffer with new size
    DestroyDepthBuffer();
    if (!CreateDepthBuffer())
    {
        MR_LOG_ERROR("Failed to recreate depth buffer");
        return false;
    }
    
    // Update viewport
    glViewport(0, 0, newWidth, newHeight);
    
    return true;
}

void FOpenGLSwapChain::SetVSync(bool enabled)
{
    m_vsyncEnabled = enabled;
    m_device->GetContextManager().SetVSync(enabled ? 1 : 0);
    
    MR_LOG_DEBUG(enabled ? "VSync enabled" : "VSync disabled");
}

bool FOpenGLSwapChain::IsVSyncEnabled() const
{
    return m_vsyncEnabled;
}

void FOpenGLSwapChain::SetPresentMode(EPresentMode mode)
{
    m_presentMode = mode;
    
    // Map present mode to vsync setting
    switch (mode)
    {
        case EPresentMode::Immediate:
            SetVSync(false);
            break;
        case EPresentMode::VSync:
        case EPresentMode::FIFO:
            SetVSync(true);
            break;
        case EPresentMode::Mailbox:
            // OpenGL doesn't have mailbox mode, use vsync
            SetVSync(true);
            break;
    }
}

EPresentMode FOpenGLSwapChain::GetPresentMode() const
{
    return m_presentMode;
}

bool FOpenGLSwapChain::IsValid() const
{
    return m_initialized;
}

TSharedPtr<IRHITexture> FOpenGLSwapChain::GetDepthStencilTexture()
{
    return m_depthTexture;
}

void FOpenGLSwapChain::SetDebugName(const FString& name)
{
    m_desc.debugName = name;
}

void FOpenGLSwapChain::BindDefaultFramebuffer()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool FOpenGLSwapChain::CreateDepthBuffer()
{
    // Create a renderbuffer for depth
    glGenRenderbuffers(1, &m_depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_desc.width, m_desc.height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    
    GL_CHECK("CreateDepthBuffer");
    
    // Note: For the default framebuffer, depth is typically handled by the pixel format
    // This renderbuffer is for reference/compatibility
    
    MR_LOG_DEBUG("Created depth renderbuffer");
    
    return true;
}

void FOpenGLSwapChain::DestroyDepthBuffer()
{
    if (m_depthRenderbuffer)
    {
        glDeleteRenderbuffers(1, &m_depthRenderbuffer);
        m_depthRenderbuffer = 0;
    }
}

bool FOpenGLSwapChain::CreateBackBufferTexture()
{
    // For OpenGL, we don't create an actual texture for the backbuffer
    // The default framebuffer (0) is used directly
    // We create a placeholder texture descriptor for API compatibility
    
    // Note: In a full implementation, you might create an FBO-based
    // backbuffer for more control, but for simplicity we use the default FB
    
    return true;
}

} // namespace MonsterEngine::OpenGL
