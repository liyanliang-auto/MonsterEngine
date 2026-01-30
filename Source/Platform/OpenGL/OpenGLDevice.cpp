/**
 * OpenGLDevice.cpp
 * 
 * OpenGL 4.6 RHI Device implementation
 * Reference: UE5 OpenGLDrv/Private/OpenGLDevice.cpp
 */

#include "Platform/OpenGL/OpenGLDevice.h"
#include "Platform/OpenGL/OpenGLCommandList.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Platform/OpenGL/OpenGLDescriptorPoolManager.h"
#include "Core/Logging/LogMacros.h"

// Define log categories
DEFINE_LOG_CATEGORY_STATIC(LogOpenGLDevice, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogOpenGLRHI, Log, All);

namespace MonsterEngine::OpenGL {

using namespace MonsterRender::RHI;

// Helper to convert FString (wide) to std::string (narrow)
static std::string FStringToStdString(const FString& str)
{
    if (str.IsEmpty()) return "";
    const wchar_t* wstr = *str;
    size_t len = wcslen(wstr);
    std::string result(len, '\0');
    for (size_t i = 0; i < len; ++i) {
        result[i] = static_cast<char>(wstr[i]); // Simple ASCII conversion
    }
    return result;
}

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
        OutputDebugStringA("OpenGL: Warning - already initialized\n");
        return true;
    }
    
    OutputDebugStringA("OpenGL: Initializing device...\n");
    
    // Check if there's already an OpenGL context (e.g., from GLFW)
    bool contextExists = false;
    
#if PLATFORM_WINDOWS
    HGLRC currentContext = wglGetCurrentContext();
    if (currentContext != nullptr) {
        OutputDebugStringA("OpenGL: Detected existing OpenGL context from GLFW\n");
        contextExists = true;
        
        // Load OpenGL functions using the existing context
        if (!LoadOpenGLFunctions()) {
            OutputDebugStringA("OpenGL: Failed to load OpenGL functions\n");
            return false;
        }
        
        // Initialize context manager with existing context (don't create new one)
        // This ensures m_contextManager.m_initialized is set to true
        HDC currentDC = wglGetCurrentDC();
        if (!m_contextManager.InitializeFromExistingContext(currentContext, currentDC, windowHandle))
        {
            OutputDebugStringA("OpenGL: Failed to initialize context manager from existing context\n");
            return false;
        }
    }
    else
#endif
    {
        // Initialize OpenGL context (create new one)
        if (!m_contextManager.Initialize(windowHandle, config))
        {
            OutputDebugStringA("OpenGL: Failed to initialize context manager\n");
            return false;
        }
    }
    
    // Query capabilities
    QueryCapabilities();
    
    // Create default resources
    CreateDefaultResources();
    
    // Create immediate command list
    m_immediateCommandList = MakeShared<FOpenGLCommandList>(this);
    
    // Create framebuffer for render targets
    m_currentFramebuffer = MakeUnique<FOpenGLFramebuffer>();
    
    // Create descriptor pool manager (UBO binding management)
    m_descriptorPoolManager = MakeUnique<FOpenGLDescriptorPoolManager>(this);
    if (!m_descriptorPoolManager) {
        OutputDebugStringA("OpenGL: Failed to create descriptor pool manager\n");
        return false;
    }
    OutputDebugStringA("OpenGL: Descriptor pool manager initialized\n");
    
    // Get initial backbuffer size
    m_backbufferWidth = 1280;
    m_backbufferHeight = 720;
    
    m_initialized = true;
    
    OutputDebugStringA("OpenGL: Device initialized successfully\n");
    
    return true;
}

void FOpenGLDevice::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }
    
    OutputDebugStringA("OpenGL: Info\n");
    
    // Wait for GPU to finish
    waitForIdle();
    
    // Destroy resources
    DestroyDefaultResources();
    
    // Destroy descriptor pool manager
    m_descriptorPoolManager.Reset();
    
    m_immediateCommandList.Reset();
    m_currentFramebuffer.Reset();
    
    // Shutdown context
    m_contextManager.Shutdown();
    
    m_initialized = false;
    
    OutputDebugStringA("OpenGL: Info\n");
}

const RHIDeviceCapabilities& FOpenGLDevice::getCapabilities() const
{
    return m_capabilities;
}

TSharedPtr<IRHIBuffer> FOpenGLDevice::createBuffer(const BufferDesc& desc)
{
    // Ensure OpenGL context is current before creating resources
    m_contextManager.MakeCurrent();
    return MakeShared<FOpenGLBuffer>(desc);
}

TSharedPtr<IRHITexture> FOpenGLDevice::createTexture(const TextureDesc& desc)
{
    // Ensure OpenGL context is current before creating resources
    m_contextManager.MakeCurrent();
    return MakeShared<FOpenGLTexture>(desc);
}

TSharedPtr<IRHIVertexShader> FOpenGLDevice::createVertexShader(MonsterRender::TSpan<const uint8> bytecode)
{
    auto shader = MakeShared<FOpenGLVertexShader>();
    
    if (!shader->InitFromBytecode(bytecode))
    {
        OutputDebugStringA("OpenGL: Error\n");
        return nullptr;
    }
    
    return shader;
}

TSharedPtr<IRHIPixelShader> FOpenGLDevice::createPixelShader(MonsterRender::TSpan<const uint8> bytecode)
{
    auto shader = MakeShared<FOpenGLPixelShader>();
    
    if (!shader->InitFromBytecode(bytecode))
    {
        OutputDebugStringA("OpenGL: Error\n");
        return nullptr;
    }
    
    return shader;
}

TSharedPtr<IRHIPipelineState> FOpenGLDevice::createPipelineState(const PipelineStateDesc& desc)
{
    auto pipeline = MakeShared<FOpenGLPipelineState>(desc);
    
    if (!pipeline->IsValid())
    {
        OutputDebugStringA("OpenGL: Error\n");
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

// ========================================================================
// UE5-style Vertex and Index Buffer Creation
// ========================================================================

TSharedPtr<FRHIVertexBuffer> FOpenGLDevice::CreateVertexBuffer(
    uint32 Size,
    EBufferUsageFlags Usage,
    FRHIResourceCreateInfo& CreateInfo)
{
    MR_LOG_DEBUG("Creating OpenGL vertex buffer: " + CreateInfo.DebugName + 
                 " (size: " + std::to_string(Size) + " bytes)");
    
    // Create the OpenGL vertex buffer
    auto vertexBuffer = MakeShared<FOpenGLVertexBuffer>(Size, 0, Usage);
    
    // Initialize with optional initial data
    if (!vertexBuffer->Initialize(CreateInfo.BulkData, CreateInfo.BulkDataSize))
    {
        MR_LOG_ERROR("Failed to create OpenGL vertex buffer: " + CreateInfo.DebugName);
        return nullptr;
    }
    
    // Set debug name
    vertexBuffer->SetDebugName(CreateInfo.DebugName);
    
    MR_LOG_DEBUG("Successfully created OpenGL vertex buffer: " + CreateInfo.DebugName);
    return vertexBuffer;
}

TSharedPtr<FRHIIndexBuffer> FOpenGLDevice::CreateIndexBuffer(
    uint32 Stride,
    uint32 Size,
    EBufferUsageFlags Usage,
    FRHIResourceCreateInfo& CreateInfo)
{
    MR_LOG_DEBUG("Creating OpenGL index buffer: " + CreateInfo.DebugName + 
                 " (size: " + std::to_string(Size) + " bytes, " +
                 (Stride == 4 ? "32-bit" : "16-bit") + " indices)");
    
    // Create the OpenGL index buffer
    auto indexBuffer = MakeShared<FOpenGLIndexBuffer>(Stride, Size, Usage);
    
    // Initialize with optional initial data
    if (!indexBuffer->Initialize(CreateInfo.BulkData, CreateInfo.BulkDataSize))
    {
        MR_LOG_ERROR("Failed to create OpenGL index buffer: " + CreateInfo.DebugName);
        return nullptr;
    }
    
    // Set debug name
    indexBuffer->SetDebugName(CreateInfo.DebugName);
    
    MR_LOG_DEBUG("Successfully created OpenGL index buffer: " + CreateInfo.DebugName);
    return indexBuffer;
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
        OutputDebugStringA("OpenGL: Error\n");
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
    // Get strings (convert FString to std::string)
    m_capabilities.deviceName = FStringToStdString(m_contextManager.GetRendererString());
    m_capabilities.vendorName = FStringToStdString(m_contextManager.GetVendorString());
    
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
    
    // Query descriptor binding limits (critical for RHI compatibility)
    GLint maxUBOBindings = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxUBOBindings);
    
    GLint maxTextureUnits = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTextureUnits);
    
    // Validate against engine requirements
    // Reference: UE5 validates device capabilities against minimum requirements
    const uint32 requiredUBOBindings = MonsterRender::RHI::RHILimits::MAX_TOTAL_UBO_BINDING_POINTS;
    const uint32 requiredTextureUnits = MonsterRender::RHI::RHILimits::MAX_DESCRIPTOR_SETS * MonsterRender::RHI::RHILimits::MAX_TEXTURE_UNITS_PER_SET;
    
    if (static_cast<uint32>(maxUBOBindings) < requiredUBOBindings) {
        MR_LOG(LogOpenGLDevice, Warning, 
               "GL_MAX_UNIFORM_BUFFER_BINDINGS (%d) is less than required (%u). Descriptor binding may fail.",
               maxUBOBindings, requiredUBOBindings);
    } else {
        MR_LOG(LogOpenGLDevice, Log, 
               "GL_MAX_UNIFORM_BUFFER_BINDINGS: %d (required: %u)",
               maxUBOBindings, requiredUBOBindings);
    }
    
    if (static_cast<uint32>(maxTextureUnits) < requiredTextureUnits) {
        MR_LOG(LogOpenGLDevice, Warning, 
               "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS (%d) is less than required (%u). Texture binding may fail.",
               maxTextureUnits, requiredTextureUnits);
    } else {
        MR_LOG(LogOpenGLDevice, Log, 
               "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: %d (required: %u)",
               maxTextureUnits, requiredTextureUnits);
    }
    
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
    
    OutputDebugStringA("OpenGL: Device capabilities queried\n");
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
    defaultTexDesc.debugName = "DefaultWhiteTexture";
    
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
// Descriptor Set Management
// ============================================================================

TSharedPtr<IRHIDescriptorSetLayout> FOpenGLDevice::createDescriptorSetLayout(
    const FDescriptorSetLayoutDesc& desc)
{
    MR_LOG(LogOpenGLRHI, Verbose, 
           "Creating OpenGL descriptor set layout for set %u with %llu bindings",
           desc.setIndex, static_cast<uint64>(desc.bindings.size()));
    
    TSharedPtr<FOpenGLDescriptorSetLayout> layout = MakeShared<FOpenGLDescriptorSetLayout>(this, desc);
    
    if (!layout->isValid()) {
        MR_LOG(LogOpenGLRHI, Error, "Failed to create OpenGL descriptor set layout");
        return nullptr;
    }
    
    return layout;
}

TSharedPtr<IRHIPipelineLayout> FOpenGLDevice::createPipelineLayout(
    const FPipelineLayoutDesc& desc)
{
    MR_LOG(LogOpenGLRHI, Verbose, 
           "Creating OpenGL pipeline layout with %llu descriptor sets",
           static_cast<uint64>(desc.setLayouts.size()));
    
    TSharedPtr<FOpenGLPipelineLayout> layout = MakeShared<FOpenGLPipelineLayout>(this, desc);
    
    if (!layout->isValid()) {
        MR_LOG(LogOpenGLRHI, Error, "Failed to create OpenGL pipeline layout");
        return nullptr;
    }
    
    return layout;
}

TSharedPtr<IRHIDescriptorSet> FOpenGLDevice::allocateDescriptorSet(
    TSharedPtr<IRHIDescriptorSetLayout> layout)
{
    if (!layout) {
        MR_LOG(LogOpenGLRHI, Error, "allocateDescriptorSet: Invalid layout");
        return nullptr;
    }
    
    auto* glLayout = dynamic_cast<FOpenGLDescriptorSetLayout*>(layout.get());
    if (!glLayout) {
        MR_LOG(LogOpenGLRHI, Error, "allocateDescriptorSet: Layout is not an OpenGL layout");
        return nullptr;
    }
    
    if (!m_descriptorPoolManager) {
        MR_LOG(LogOpenGLRHI, Error, "allocateDescriptorSet: Descriptor pool manager not initialized");
        return nullptr;
    }
    
    // Allocate descriptor set from pool manager
    auto descriptorSet = m_descriptorPoolManager->allocateDescriptorSet(
        TSharedPtr<FOpenGLDescriptorSetLayout>(glLayout, [](FOpenGLDescriptorSetLayout*){}));
    
    if (!descriptorSet) {
        MR_LOG(LogOpenGLRHI, Error, "Failed to allocate OpenGL descriptor set");
        return nullptr;
    }
    
    MR_LOG(LogOpenGLRHI, VeryVerbose, "Allocated OpenGL descriptor set for set %u",
           glLayout->getSetIndex());
    
    return descriptorSet;
}

// ============================================================================
// Factory Function
// ============================================================================

TSharedPtr<FOpenGLDevice> CreateOpenGLDevice(void* windowHandle, const FOpenGLContextConfig& config)
{
    auto device = MakeShared<FOpenGLDevice>();
    
    if (!device->Initialize(windowHandle, config))
    {
        OutputDebugStringA("OpenGL: Error\n");
        return nullptr;
    }
    
    return device;
}

// ============================================================================
// Texture Update Methods (Stub implementations)
// ============================================================================

bool FOpenGLDevice::updateTextureSubresource(
    TSharedPtr<MonsterRender::RHI::IRHITexture> texture,
    uint32 mipLevel,
    const void* data,
    SIZE_T dataSize)
{
    // TODO: Implement texture update when glad library is available
    MR_LOG(LogOpenGLDevice, Warning, "updateTextureSubresource not yet implemented (requires glad)");
    return false;
}

bool FOpenGLDevice::updateTextureSubresourceAsync(
    TSharedPtr<MonsterRender::RHI::IRHITexture> texture,
    uint32 mipLevel,
    const void* data,
    SIZE_T dataSize,
    uint64* outFenceValue)
{
    // TODO: Implement async texture update when glad library is available
    MR_LOG(LogOpenGLDevice, Warning, "updateTextureSubresourceAsync not yet implemented (requires glad)");
    return false;
}

} // namespace MonsterEngine::OpenGL
