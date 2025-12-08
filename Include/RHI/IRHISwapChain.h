#pragma once

/**
 * IRHISwapChain.h
 * 
 * SwapChain abstraction interface for MonsterEngine RHI
 * Reference: UE5 RHI Viewport concept (FRHIViewport)
 * 
 * The SwapChain manages the presentation of rendered frames to the display.
 * It abstracts the differences between Vulkan swapchains and OpenGL double buffering.
 */

#include "Core/CoreMinimal.h"
#include "Containers/String.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/IRHIResource.h"

namespace MonsterRender::RHI {

// Use MonsterEngine types
using MonsterEngine::FString;

/**
 * SwapChain creation description
 */
struct SwapChainDesc
{
    void* windowHandle = nullptr;       // Native window handle (HWND on Windows)
    uint32 width = 1280;                // Backbuffer width
    uint32 height = 720;                // Backbuffer height
    EPixelFormat format = EPixelFormat::B8G8R8A8_SRGB;  // Backbuffer format
    uint32 bufferCount = 2;             // Number of backbuffers (double/triple buffering)
    bool vsync = true;                  // Enable vertical sync
    bool fullscreen = false;            // Fullscreen mode
    bool hdr = false;                   // HDR output
    FString debugName;                  // Debug name
    
    SwapChainDesc() = default;
    
    SwapChainDesc(void* window, uint32 w, uint32 h)
        : windowHandle(window), width(w), height(h) {}
};

/**
 * Present mode enumeration
 */
enum class EPresentMode : uint8
{
    Immediate,      // No vsync, may tear
    VSync,          // Wait for vertical blank
    Mailbox,        // Triple buffering with vsync
    FIFO            // Queue frames for vsync
};

/**
 * SwapChain status
 */
enum class ESwapChainStatus : uint8
{
    OK,             // SwapChain is valid and ready
    OutOfDate,      // SwapChain needs to be recreated (window resized)
    Suboptimal,     // SwapChain works but may not be optimal
    Error           // SwapChain error
};

/**
 * SwapChain interface
 * Reference: UE5 FRHIViewport
 * 
 * Manages backbuffers and presentation to the display.
 */
class IRHISwapChain
{
public:
    IRHISwapChain() = default;
    virtual ~IRHISwapChain() = default;
    
    // Non-copyable
    IRHISwapChain(const IRHISwapChain&) = delete;
    IRHISwapChain& operator=(const IRHISwapChain&) = delete;
    
    /**
     * Get the current backbuffer texture for rendering
     * This is the texture that should be used as render target
     */
    virtual TSharedPtr<IRHITexture> GetCurrentBackBuffer() = 0;
    
    /**
     * Get the current backbuffer index
     */
    virtual uint32 GetCurrentBackBufferIndex() const = 0;
    
    /**
     * Get the number of backbuffers
     */
    virtual uint32 GetBackBufferCount() const = 0;
    
    /**
     * Get the backbuffer format
     */
    virtual EPixelFormat GetBackBufferFormat() const = 0;
    
    /**
     * Get the current dimensions
     */
    virtual void GetDimensions(uint32& outWidth, uint32& outHeight) const = 0;
    
    /**
     * Acquire the next backbuffer for rendering
     * Must be called before rendering to the swapchain
     * @return Status of the acquire operation
     */
    virtual ESwapChainStatus AcquireNextImage() = 0;
    
    /**
     * Present the current backbuffer to the display
     * @return Status of the present operation
     */
    virtual ESwapChainStatus Present() = 0;
    
    /**
     * Resize the swapchain
     * @param newWidth New width
     * @param newHeight New height
     * @return true if resize succeeded
     */
    virtual bool Resize(uint32 newWidth, uint32 newHeight) = 0;
    
    /**
     * Set vsync mode
     */
    virtual void SetVSync(bool enabled) = 0;
    
    /**
     * Get vsync state
     */
    virtual bool IsVSyncEnabled() const = 0;
    
    /**
     * Set present mode
     */
    virtual void SetPresentMode(EPresentMode mode) = 0;
    
    /**
     * Get present mode
     */
    virtual EPresentMode GetPresentMode() const = 0;
    
    /**
     * Check if the swapchain is valid
     */
    virtual bool IsValid() const = 0;
    
    /**
     * Get the depth stencil texture (if created with the swapchain)
     */
    virtual TSharedPtr<IRHITexture> GetDepthStencilTexture() = 0;
    
    /**
     * Get the swapchain description
     */
    virtual const SwapChainDesc& GetDesc() const = 0;
    
    /**
     * Set debug name
     */
    virtual void SetDebugName(const FString& name) = 0;
    
protected:
    SwapChainDesc m_desc;
};

/**
 * RHI Backend type enumeration
 */
enum class ERHIBackend : uint8
{
    None = 0,
    Unknown = 0,
    D3D11,
    D3D12,
    Vulkan,
    OpenGL,
    Metal
};

/**
 * Get the name of an RHI backend
 */
inline const char* GetRHIBackendName(ERHIBackend backend)
{
    switch (backend)
    {
        case ERHIBackend::Vulkan:  return "Vulkan";
        case ERHIBackend::OpenGL:  return "OpenGL";
        case ERHIBackend::D3D12:   return "D3D12";
        case ERHIBackend::Metal:   return "Metal";
        default:                   return "Unknown";
    }
}

} // namespace MonsterRender::RHI
