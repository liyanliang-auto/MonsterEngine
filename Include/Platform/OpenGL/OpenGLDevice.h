#pragma once

/**
 * OpenGLDevice.h
 * 
 * OpenGL 4.6 RHI Device implementation
 * Reference: UE5 OpenGLDrv/Public/OpenGLDrv.h
 */

#include "Core/CoreMinimal.h"
#include "RHI/IRHIDevice.h"
#include "Platform/OpenGL/OpenGLContext.h"
#include "Platform/OpenGL/OpenGLResources.h"
#include "Platform/OpenGL/OpenGLShaders.h"
#include "Platform/OpenGL/OpenGLPipeline.h"
#include "Platform/OpenGL/OpenGLSwapChain.h"

namespace MonsterEngine::OpenGL {

// Forward declarations
class FOpenGLCommandList;


/**
    FOpenGLDevice（继承 IRHIDevice）
    ├── FOpenGLContext            // GLFW + OpenGL 4.6 上下文（GLAD 加载扩展）
    ├── FOpenGLPipelineCache      // VAO / Program 缓存
    │     ├── TMap<uint64, GLuint> programCache
    │     └── TMap<uint64, GLuint> vaoCache
    ├── FOpenGLCommandList        // 即时模式命令列表（直接调用 glDraw*）
    └── FOpenGLSwapChain          // glfwSwapBuffers 封装
 */


/**
 * OpenGL 4.6 RHI Device
 * Reference: UE5 FOpenGLDynamicRHI
 */
class FOpenGLDevice : public MonsterRender::RHI::IRHIDevice
{
public:
    FOpenGLDevice();
    virtual ~FOpenGLDevice();
    
    /**
     * Initialize the OpenGL device
     * @param windowHandle Native window handle
     * @param config Context configuration
     * @return true if initialization succeeded
     */
    bool Initialize(void* windowHandle, const FOpenGLContextConfig& config = FOpenGLContextConfig());
    
    /**
     * Shutdown the device
     */
    void Shutdown();
    
    /**
     * Check if device is initialized
     */
    bool IsInitialized() const { return m_initialized; }
    
    // IRHIDevice interface
    virtual const MonsterRender::RHI::RHIDeviceCapabilities& getCapabilities() const override;
    virtual MonsterRender::RHI::ERHIBackend getBackendType() const override { return MonsterRender::RHI::ERHIBackend::OpenGL; }
    
    virtual TSharedPtr<MonsterRender::RHI::IRHIBuffer> createBuffer(
        const MonsterRender::RHI::BufferDesc& desc) override;
    
    virtual TSharedPtr<MonsterRender::RHI::IRHITexture> createTexture(
        const MonsterRender::RHI::TextureDesc& desc) override;
    
    virtual TSharedPtr<MonsterRender::RHI::IRHIVertexShader> createVertexShader(
        MonsterRender::TSpan<const uint8> bytecode) override;
    
    virtual TSharedPtr<MonsterRender::RHI::IRHIPixelShader> createPixelShader(
        MonsterRender::TSpan<const uint8> bytecode) override;
    
    virtual TSharedPtr<MonsterRender::RHI::IRHIPipelineState> createPipelineState(
        const MonsterRender::RHI::PipelineStateDesc& desc) override;
    
    virtual TSharedPtr<MonsterRender::RHI::IRHISampler> createSampler(
        const MonsterRender::RHI::SamplerDesc& desc) override;
    
    // Descriptor set management (Multi-descriptor set support)
    virtual TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> createDescriptorSetLayout(
        const MonsterRender::RHI::FDescriptorSetLayoutDesc& desc) override;
    
    virtual TSharedPtr<MonsterRender::RHI::IRHIPipelineLayout> createPipelineLayout(
        const MonsterRender::RHI::FPipelineLayoutDesc& desc) override;
    
    virtual TSharedPtr<MonsterRender::RHI::IRHIDescriptorSet> allocateDescriptorSet(
        TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> layout) override;
    
    // UE5-style vertex and index buffer creation
    virtual TSharedPtr<MonsterRender::RHI::FRHIVertexBuffer> CreateVertexBuffer(
        uint32 Size,
        MonsterRender::RHI::EBufferUsageFlags Usage,
        MonsterRender::RHI::FRHIResourceCreateInfo& CreateInfo) override;
    
    virtual TSharedPtr<MonsterRender::RHI::FRHIIndexBuffer> CreateIndexBuffer(
        uint32 Stride,
        uint32 Size,
        MonsterRender::RHI::EBufferUsageFlags Usage,
        MonsterRender::RHI::FRHIResourceCreateInfo& CreateInfo) override;
    
    virtual MonsterRender::RHI::IRHICommandList* getImmediateCommandList() override;
    
    virtual void waitForIdle() override;
    virtual void present() override;
    
    virtual void getMemoryStats(uint64& usedBytes, uint64& availableBytes) override;
    virtual void collectGarbage() override;
    
    virtual void setDebugName(const String& name) override;
    virtual void setValidationEnabled(bool enabled) override;
    
    // SwapChain and RHI type
    virtual TSharedPtr<MonsterRender::RHI::IRHISwapChain> createSwapChain(
        const MonsterRender::RHI::SwapChainDesc& desc) override;
    virtual MonsterRender::RHI::ERHIBackend getRHIBackend() const override { 
        return MonsterRender::RHI::ERHIBackend::OpenGL; 
    }
    virtual MonsterRender::RHI::EPixelFormat getSwapChainFormat() const override;
    virtual MonsterRender::RHI::EPixelFormat getDepthFormat() const override;
    
    // OpenGL-specific methods
    
    /**
     * Get the context manager
     */
    FOpenGLContextManager& GetContextManager() { return m_contextManager; }
    const FOpenGLContextManager& GetContextManager() const { return m_contextManager; }
    
    /**
     * Get the state cache
     */
    FOpenGLStateCache& GetStateCache() { return m_stateCache; }
    const FOpenGLStateCache& GetStateCache() const { return m_stateCache; }
    
    /**
     * Get the default framebuffer (backbuffer)
     */
    FOpenGLFramebuffer* GetDefaultFramebuffer() { return nullptr; }  // Default FBO is 0
    
    /**
     * Set the current render targets
     */
    void SetRenderTargets(MonsterRender::TSpan<FOpenGLTexture*> colorTargets, FOpenGLTexture* depthStencil);
    
    /**
     * Bind the default framebuffer
     */
    void BindDefaultFramebuffer();
    
    /**
     * Get backbuffer dimensions
     */
    void GetBackbufferSize(uint32& width, uint32& height) const;
    
    /**
     * Set VSync
     */
    void SetVSync(bool enabled);
    
    /**
     * Create a sampler from OpenGL-specific description
     */
    TSharedPtr<FOpenGLSampler> CreateSamplerGL(const FSamplerDesc& desc);
    
    /**
     * Get descriptor pool manager
     */
    class FOpenGLDescriptorPoolManager* getDescriptorPoolManager() const { 
        return m_descriptorPoolManager.get(); 
    }
    
private:
    /**
     * Query device capabilities
     */
    void QueryCapabilities();
    
    /**
     * Create default resources
     */
    void CreateDefaultResources();
    
    /**
     * Destroy default resources
     */
    void DestroyDefaultResources();
    
private:
    bool m_initialized = false;
    FOpenGLContextManager m_contextManager;
    FOpenGLStateCache m_stateCache;
    
    // Immediate command list for direct execution
    TSharedPtr<FOpenGLCommandList> m_immediateCommandList;
    
    // Current render target framebuffer
    TUniquePtr<FOpenGLFramebuffer> m_currentFramebuffer;
    
    // Default resources
    TSharedPtr<FOpenGLSampler> m_defaultSampler;
    TSharedPtr<FOpenGLTexture> m_defaultTexture;
    
    // Descriptor pool manager (UBO binding management)
    TUniquePtr<class FOpenGLDescriptorPoolManager> m_descriptorPoolManager;
    
    // Backbuffer info
    uint32 m_backbufferWidth = 0;
    uint32 m_backbufferHeight = 0;
    
    // Debug
    bool m_validationEnabled = false;
    String m_debugName;
};

/**
 * Create an OpenGL RHI device
 * @param windowHandle Native window handle
 * @param config Context configuration
 * @return Shared pointer to the device, or nullptr on failure
 */
TSharedPtr<FOpenGLDevice> CreateOpenGLDevice(void* windowHandle, 
                                              const FOpenGLContextConfig& config = FOpenGLContextConfig());

} // namespace MonsterEngine::OpenGL
