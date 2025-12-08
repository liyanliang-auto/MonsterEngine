/**
 * OpenGLDevice.cpp
 * 
 * OpenGL 4.6 RHI Device implementation
 * Reference: UE5 OpenGLDrv/Private/OpenGLDevice.cpp
 */

#include "Platform/OpenGL/OpenGLDevice.h"
#include "Platform/OpenGL/OpenGLCommandList.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Core/Logging/LogMacros.h"

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogOpenGLDevice, Log, All);

namespace MonsterEngine::OpenGL {

using namespace MonsterRender::RHI;

// ============================================================================
// FOpenGLDevice Implementation
// ============================================================================

FOpenGLDevice::FOpenGLDevice()
{
}

FOpenGLDevice::~FOpenGLDevice()
{
    Shutdown();
}

bool FOpenGLDevice::Initialize(void* windowHandle, const FOpenGLContextConfig& config)
{
    if (m_initialized)
    {
        MR_LOG_WARNING(LogOpenGLDevice, "OpenGL device already initialized");
        return true;
    }
    
    MR_LOG_INFO(LogOpenGLDevice, "Initializing OpenGL device...");
    
    // Initialize OpenGL context
    if (!m_contextManager.Initialize(windowHandle, config))
    {
        MR_LOG_ERROR(LogOpenGLDevice, "Failed to initialize OpenGL context");
        return false;
    }
    
    // Query capabilities
    QueryCapabilities();
    
    // Create default resources
    CreateDefaultResources();
    
    // Create immediate command list
    m_immediateCommandList = MakeShared<FOpenGLCommandList>(this);
    
    // Create framebuffer for render targets
    m_currentFramebuffer = MakeUnique<FOpenGLFramebuffer>();
    
    // Get initial backbuffer size
    // For now, we'll need to set this from the window
    m_backbufferWidth = 1280;
    m_backbufferHeight = 720;
    
    m_initialized = true;
    
    MR_LOG_INFO(LogOpenGLDevice, "OpenGL device initialized successfully");
    MR_LOG_INFO(LogOpenGLDevice, "  Device: %s", *m_capabilities.deviceName);
    MR_LOG_INFO(LogOpenGLDevice, "  Vendor: %s", *m_capabilities.vendorName);
    
    return true;
}

void FOpenGLDevice::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }
    
    MR_LOG_INFO(LogOpenGLDevice, "Shutting down OpenGL device...");
    
    // Wait for GPU to finish
    waitForIdle();
    
    // Destroy resources
    DestroyDefaultResources();
    
    m_immediateCommandList.Reset();
    m_currentFramebuffer.Reset();
    
    // Shutdown context
    m_contextManager.Shutdown();
    
    m_initialized = false;
    
    MR_LOG_INFO(LogOpenGLDevice, "OpenGL device shutdown complete");
}

const RHIDeviceCapabilities& FOpenGLDevice::getCapabilities() const
{
    return m_capabilities;
}

TSharedPtr<IRHIBuffer> FOpenGLDevice::createBuffer(const BufferDesc& desc)
{
    return MakeShared<FOpenGLBuffer>(desc);
}

TSharedPtr<IRHITexture> FOpenGLDevice::createTexture(const TextureDesc& desc)
{
    return MakeShared<FOpenGLTexture>(desc);
}

TSharedPtr<IRHIVertexShader> FOpenGLDevice::createVertexShader(MonsterRender::TSpan<const uint8> bytecode)
{
    auto shader = MakeShared<FOpenGLVertexShader>();
    
    if (!shader->InitFromBytecode(bytecode))
    {
        MR_LOG_ERROR("Failed to create vertex shader");
        return nullptr;
    }
    
    return shader;
}

TSharedPtr<IRHIPixelShader> FOpenGLDevice::createPixelShader(MonsterRender::TSpan<const uint8> bytecode)
{
    auto shader = MakeShared<FOpenGLPixelShader>();
    
    if (!shader->InitFromBytecode(bytecode))
    {
        MR_LOG_ERROR("Failed to create pixel shader");
        return nullptr;
    }
    
    return shader;
}

TSharedPtr<IRHIPipelineState> FOpenGLDevice::createPipelineState(const PipelineStateDesc& desc)
{
    auto pipeline = MakeShared<FOpenGLPipelineState>(desc);
    
    if (!pipeline->IsValid())
    {
        MR_LOG_ERROR("Failed to create pipeline state");
        return nullptr;
    }
    
    return pipeline;
}

TSharedPtr<IRHISampler> FOpenGLDevice::createSampler(const SamplerDesc& desc)
{
    // Convert RHI SamplerDesc to OpenGL FSamplerDesc
    FSamplerDesc glDesc;
    
    // Note: This is a simplified conversion - you may need to expand this
    // based on your SamplerDesc definition
    glDesc.minFilter = FSamplerDesc::EFilter::Linear;
    glDesc.magFilter = FSamplerDesc::EFilter::Linear;
    glDesc.mipFilter = FSamplerDesc::EFilter::Linear;
    glDesc.addressU = FSamplerDesc::EAddressMode::Wrap;
    glDesc.addressV = FSamplerDesc::EAddressMode::Wrap;
    glDesc.addressW = FSamplerDesc::EAddressMode::Wrap;
    
    return MakeShared<FOpenGLSampler>(glDesc);
}

TSharedPtr<IRHICommandList> FOpenGLDevice::createCommandList()
{
    return MakeShared<FOpenGLCommandList>(this);
}

void FOpenGLDevice::executeCommandLists(MonsterRender::TSpan<TSharedPtr<IRHICommandList>> commandLists)
{
    for (auto& cmdList : commandLists)
    {
        if (auto* glCmdList = static_cast<FOpenGLCommandList*>(cmdList.get()))
        {
            glCmdList->Execute();
        }
    }
}

IRHICommandList* FOpenGLDevice::getImmediateCommandList()
{
    return m_immediateCommandList.get();
}

void FOpenGLDevice::waitForIdle()
{
    if (glFinish)
    {
        glFinish();
    }
}

void FOpenGLDevice::present()
{
    m_contextManager.SwapBuffers();
}

void FOpenGLDevice::getMemoryStats(uint64& usedBytes, uint64& availableBytes)
{
    // OpenGL doesn't have a standard way to query memory usage
    // Some extensions like GL_NVX_gpu_memory_info can be used on NVIDIA
    usedBytes = 0;
    availableBytes = m_capabilities.dedicatedVideoMemory;
    
    // Try NVIDIA extension
    if (m_contextManager.IsExtensionSupported("GL_NVX_gpu_memory_info"))
    {
        GLint totalMemKB = 0;
        GLint availMemKB = 0;
        
        // GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX = 0x9048
        // GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX = 0x9049
        glGetIntegerv(0x9048, &totalMemKB);
        glGetIntegerv(0x9049, &availMemKB);
        
        availableBytes = static_cast<uint64>(availMemKB) * 1024;
        usedBytes = static_cast<uint64>(totalMemKB - availMemKB) * 1024;
    }
}

void FOpenGLDevice::collectGarbage()
{
    // OpenGL handles resource cleanup automatically
    // We could track and delete unused resources here if needed
}

void FOpenGLDevice::setDebugName(const String& name)
{
    m_debugName = name;
}

void FOpenGLDevice::setValidationEnabled(bool enabled)
{
    m_validationEnabled = enabled;
    
    if (enabled && glDebugMessageCallback)
    {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    }
    else if (glDebugMessageCallback)
    {
        glDisable(GL_DEBUG_OUTPUT);
    }
}

TSharedPtr<IRHISwapChain> FOpenGLDevice::createSwapChain(const SwapChainDesc& desc)
{
    return MakeShared<FOpenGLSwapChain>(this, desc);
}

EPixelFormat FOpenGLDevice::getSwapChainFormat() const
{
    // OpenGL default framebuffer is typically RGBA8 SRGB
    return EPixelFormat::R8G8B8A8_SRGB;
}

EPixelFormat FOpenGLDevice::getDepthFormat() const
{
    return EPixelFormat::D32_FLOAT;
}

void FOpenGLDevice::SetRenderTargets(MonsterRender::TSpan<FOpenGLTexture*> colorTargets, FOpenGLTexture* depthStencil)
{
    if (colorTargets.empty() && !depthStencil)
    {
        // Bind default framebuffer
        BindDefaultFramebuffer();
        return;
    }
    
    m_currentFramebuffer->ClearAttachments();
    
    for (uint32 i = 0; i < colorTargets.size(); ++i)
    {
        m_currentFramebuffer->SetColorAttachment(i, colorTargets[i]);
    }
    
    if (depthStencil)
    {
        m_currentFramebuffer->SetDepthStencilAttachment(depthStencil);
    }
    
    m_currentFramebuffer->Bind();
    
    if (!m_currentFramebuffer->IsComplete())
    {
        MR_LOG_ERROR(LogOpenGLDevice, "Framebuffer is not complete!");
    }
}

void FOpenGLDevice::BindDefaultFramebuffer()
{
    FOpenGLFramebuffer::BindDefault();
}

void FOpenGLDevice::GetBackbufferSize(uint32& width, uint32& height) const
{
    width = m_backbufferWidth;
    height = m_backbufferHeight;
}

void FOpenGLDevice::SetVSync(bool enabled)
{
    m_contextManager.SetVSync(enabled ? 1 : 0);
}

TSharedPtr<FOpenGLSampler> FOpenGLDevice::CreateSamplerGL(const FSamplerDesc& desc)
{
    return MakeShared<FOpenGLSampler>(desc);
}

void FOpenGLDevice::QueryCapabilities()
{
    // Get strings
    m_capabilities.deviceName = m_contextManager.GetRendererString();
    m_capabilities.vendorName = m_contextManager.GetVendorString();
    
    // Query limits
    GLint value = 0;
    
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
    m_capabilities.maxTexture1DSize = value;
    m_capabilities.maxTexture2DSize = value;
    
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &value);
    m_capabilities.maxTexture3DSize = value;
    
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &value);
    m_capabilities.maxTextureCubeSize = value;
    
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &value);
    m_capabilities.maxTextureArrayLayers = value;
    
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &value);
    m_capabilities.maxRenderTargets = value;
    
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &value);
    m_capabilities.maxVertexInputAttributes = value;
    m_capabilities.maxVertexInputBindings = value;
    
    // Feature support
    m_capabilities.supportsGeometryShader = true;  // GL 4.6 always supports this
    m_capabilities.supportsTessellation = true;
    m_capabilities.supportsComputeShader = true;
    m_capabilities.supportsMultiDrawIndirect = (glMultiDrawArraysIndirect != nullptr);
    m_capabilities.supportsTimestampQuery = (glQueryCounter != nullptr);
    
    // Try to get video memory (vendor-specific)
    m_capabilities.dedicatedVideoMemory = 0;
    
    if (m_contextManager.IsExtensionSupported("GL_NVX_gpu_memory_info"))
    {
        GLint totalMemKB = 0;
        glGetIntegerv(0x9048, &totalMemKB);  // GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
        m_capabilities.dedicatedVideoMemory = static_cast<uint64>(totalMemKB) * 1024;
    }
    else if (m_contextManager.IsExtensionSupported("GL_ATI_meminfo"))
    {
        GLint vboFreeMemKB[4] = {};
        glGetIntegerv(0x87FB, vboFreeMemKB);  // GL_VBO_FREE_MEMORY_ATI
        m_capabilities.dedicatedVideoMemory = static_cast<uint64>(vboFreeMemKB[0]) * 1024;
    }
    
    MR_LOG_DEBUG(LogOpenGLDevice, "Device capabilities:");
    MR_LOG_DEBUG(LogOpenGLDevice, "  Max texture 2D size: %u", m_capabilities.maxTexture2DSize);
    MR_LOG_DEBUG(LogOpenGLDevice, "  Max texture 3D size: %u", m_capabilities.maxTexture3DSize);
    MR_LOG_DEBUG(LogOpenGLDevice, "  Max render targets: %u", m_capabilities.maxRenderTargets);
    MR_LOG_DEBUG(LogOpenGLDevice, "  Max vertex attributes: %u", m_capabilities.maxVertexInputAttributes);
    MR_LOG_DEBUG(LogOpenGLDevice, "  Video memory: %llu MB", m_capabilities.dedicatedVideoMemory / (1024 * 1024));
}

void FOpenGLDevice::CreateDefaultResources()
{
    // Create default sampler (linear filtering, wrap addressing)
    FSamplerDesc defaultSamplerDesc;
    m_defaultSampler = MakeShared<FOpenGLSampler>(defaultSamplerDesc);
    
    // Create default white texture (1x1)
    TextureDesc defaultTexDesc;
    defaultTexDesc.width = 1;
    defaultTexDesc.height = 1;
    defaultTexDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    defaultTexDesc.usage = EResourceUsage::ShaderResource;
    defaultTexDesc.debugName = TEXT("DefaultWhiteTexture");
    
    uint32 whitePixel = 0xFFFFFFFF;
    defaultTexDesc.initialData = &whitePixel;
    defaultTexDesc.initialDataSize = sizeof(whitePixel);
    
    m_defaultTexture = MakeShared<FOpenGLTexture>(defaultTexDesc);
}

void FOpenGLDevice::DestroyDefaultResources()
{
    m_defaultSampler.Reset();
    m_defaultTexture.Reset();
}

// ============================================================================
// Factory Function
// ============================================================================

TSharedPtr<FOpenGLDevice> CreateOpenGLDevice(void* windowHandle, const FOpenGLContextConfig& config)
{
    auto device = MakeShared<FOpenGLDevice>();
    
    if (!device->Initialize(windowHandle, config))
    {
        MR_LOG_ERROR(LogOpenGLDevice, "Failed to create OpenGL device");
        return nullptr;
    }
    
    return device;
}

} // namespace MonsterEngine::OpenGL
