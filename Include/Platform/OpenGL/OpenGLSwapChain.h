#pragma once

/**
 * OpenGLSwapChain.h
 * 
 * OpenGL SwapChain implementation
 * Reference: UE5 OpenGL Viewport
 * 
 * Note: OpenGL doesn't have explicit swapchains like Vulkan.
 * This class wraps the default framebuffer and double buffering.
 */

#include "Core/CoreMinimal.h"
#include "RHI/IRHISwapChain.h"
#include "Platform/OpenGL/OpenGLDefinitions.h"
#include "Platform/OpenGL/OpenGLResources.h"

namespace MonsterEngine::OpenGL {

// Forward declarations
class FOpenGLDevice;
class FOpenGLContextManager;

/**
 * OpenGL SwapChain implementation
 * Wraps the default framebuffer and manages presentation
 */
class FOpenGLSwapChain : public MonsterRender::RHI::IRHISwapChain
{
public:
    FOpenGLSwapChain(FOpenGLDevice* device, const MonsterRender::RHI::SwapChainDesc& desc);
    virtual ~FOpenGLSwapChain();
    
    // IRHISwapChain interface
    virtual TSharedPtr<MonsterRender::RHI::IRHITexture> GetCurrentBackBuffer() override;
    virtual uint32 GetCurrentBackBufferIndex() const override;
    virtual uint32 GetBackBufferCount() const override;
    virtual MonsterRender::RHI::EPixelFormat GetBackBufferFormat() const override;
    virtual void GetDimensions(uint32& outWidth, uint32& outHeight) const override;
    
    virtual MonsterRender::RHI::ESwapChainStatus AcquireNextImage() override;
    virtual MonsterRender::RHI::ESwapChainStatus Present() override;
    virtual bool Resize(uint32 newWidth, uint32 newHeight) override;
    
    virtual void SetVSync(bool enabled) override;
    virtual bool IsVSyncEnabled() const override;
    virtual void SetPresentMode(MonsterRender::RHI::EPresentMode mode) override;
    virtual MonsterRender::RHI::EPresentMode GetPresentMode() const override;
    
    virtual bool IsValid() const override;
    virtual TSharedPtr<MonsterRender::RHI::IRHITexture> GetDepthStencilTexture() override;
    virtual const MonsterRender::RHI::SwapChainDesc& GetDesc() const override { return m_desc; }
    virtual void SetDebugName(const FString& name) override;
    
    // OpenGL-specific methods
    
    /**
     * Bind the default framebuffer for rendering
     */
    void BindDefaultFramebuffer();
    
    /**
     * Get the default framebuffer ID (usually 0)
     */
    GLuint GetDefaultFramebuffer() const { return 0; }
    
    /**
     * Get the depth renderbuffer
     */
    GLuint GetDepthRenderbuffer() const { return m_depthRenderbuffer; }
    
private:
    /**
     * Create depth buffer
     */
    bool CreateDepthBuffer();
    
    /**
     * Destroy depth buffer
     */
    void DestroyDepthBuffer();
    
    /**
     * Create backbuffer texture wrapper
     */
    bool CreateBackBufferTexture();
    
private:
    FOpenGLDevice* m_device = nullptr;
    
    // Backbuffer (wraps default framebuffer)
    TSharedPtr<MonsterRender::RHI::IRHITexture> m_backBufferTexture;
    
    // Depth buffer (renderbuffer for default framebuffer)
    GLuint m_depthRenderbuffer = 0;
    TSharedPtr<MonsterRender::RHI::IRHITexture> m_depthTexture;
    
    // State
    uint32 m_currentBuffer = 0;
    bool m_vsyncEnabled = true;
    MonsterRender::RHI::EPresentMode m_presentMode = MonsterRender::RHI::EPresentMode::VSync;
    bool m_initialized = false;
};

} // namespace MonsterEngine::OpenGL
