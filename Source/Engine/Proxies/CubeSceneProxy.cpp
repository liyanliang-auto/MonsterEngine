// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CubeSceneProxy.cpp
 * @brief Implementation of cube scene proxy for rendering
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/StaticMeshRender.cpp
 */

#include "Engine/Proxies/CubeSceneProxy.h"
#include "Engine/LightSceneInfo.h"
#include "Engine/LightSceneProxy.h"
#include "Core/Logging/LogMacros.h"
#include "Core/ShaderCompiler.h"
#include "Core/Color.h"
#include "Renderer/FTextureLoader.h"
#include "Math/MonsterMath.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <span>

#if PLATFORM_WINDOWS
 #include <Windows.h>
#endif

namespace MonsterEngine
{

// Use global log category (defined in LogCategories.cpp)
using MonsterRender::LogCubeSceneProxy;

// ============================================================================
// Construction / Destruction
// ============================================================================

static std::string normalizeDirPath(const std::filesystem::path& InPath)
{
    std::string Result = InPath.lexically_normal().generic_string();
    if (!Result.empty() && Result.back() != '/')
    {
        Result.push_back('/');
    }
    return Result;
}

static std::string resolveProjectRootFromExecutable()
{
    std::filesystem::path ExePath;

#if PLATFORM_WINDOWS
    char ModulePath[MAX_PATH] = {};
    DWORD Len = ::GetModuleFileNameA(nullptr, ModulePath, MAX_PATH);
    if (Len == 0 || Len >= MAX_PATH)
    {
        return std::string();
    }
    ExePath = std::filesystem::path(ModulePath);
#else
    ExePath = std::filesystem::current_path();
#endif

    std::filesystem::path Cursor = ExePath.parent_path();
    for (int32 i = 0; i < 8; ++i)
    {
        std::filesystem::path ShadersDir = Cursor / "Shaders";
        if (std::filesystem::exists(ShadersDir) && std::filesystem::is_directory(ShadersDir))
        {
            return normalizeDirPath(Cursor);
        }

        if (!Cursor.has_parent_path())
        {
            break;
        }
        Cursor = Cursor.parent_path();
    }

    return std::string();
}

FCubeSceneProxy::FCubeSceneProxy(const UCubeMeshComponent* InComponent)
    : FPrimitiveSceneProxy(InComponent, "CubeSceneProxy")
    , Device(nullptr)
    , RHIBackend(MonsterRender::RHI::ERHIBackend::Unknown)
    , TextureBlendFactor(0.5f)
    , CubeSize(0.5f)
    , bResourcesInitialized(false)
    , bVisible(true)
{
    if (InComponent)
    {
        TextureBlendFactor = InComponent->GetTextureBlendFactor();
        CubeSize = InComponent->GetCubeSize();
        Texture1 = InComponent->GetTexture1();
        Texture2 = InComponent->GetTexture2();
    }
    
    MR_LOG(LogCubeSceneProxy, Verbose, "CubeSceneProxy created");
}

FCubeSceneProxy::~FCubeSceneProxy()
{
    MR_LOG(LogCubeSceneProxy, Log, "CubeSceneProxy destructor called - releasing GPU resources");
    
    // Release GPU resources
    VertexBuffer.Reset();
    TransformUniformBuffer.Reset();
    LightUniformBuffer.Reset();
    Texture1.Reset();
    Texture2.Reset();
    Sampler.Reset();
    VertexShader.Reset();
    PixelShader.Reset();
    PipelineState.Reset();
    
    MR_LOG(LogCubeSceneProxy, Log, "CubeSceneProxy destroyed - all resources released");
}

// ============================================================================
// FPrimitiveSceneProxy Interface
// ============================================================================

SIZE_T FCubeSceneProxy::GetTypeHash() const
{
    static SIZE_T TypeHash = 0x12345678;  // Unique hash for cube proxy
    return TypeHash;
}

// ============================================================================
// Rendering
// ============================================================================

bool FCubeSceneProxy::InitializeResources(MonsterRender::RHI::IRHIDevice* InDevice)
{
    if (bResourcesInitialized)
    {
        return true;
    }
    
    if (!InDevice)
    {
        MR_LOG(LogCubeSceneProxy, Error, "Cannot initialize resources: null device");
        return false;
    }
    
    Device = InDevice;
    RHIBackend = Device->getRHIBackend();
    
    MR_LOG(LogCubeSceneProxy, Log, "Initializing CubeSceneProxy resources for %s",
        MonsterRender::RHI::GetRHIBackendName(RHIBackend));
    
    // Create resources in order
    if (!CreateVertexBuffer())
    {
        MR_LOG(LogCubeSceneProxy, Error, "Failed to create vertex buffer");
        return false;
    }
    
    if (!CreateUniformBuffers())
    {
        MR_LOG(LogCubeSceneProxy, Error, "Failed to create uniform buffers");
        return false;
    }
    
    if (!LoadTextures())
    {
        MR_LOG(LogCubeSceneProxy, Error, "Failed to load textures");
        return false;
    }
    
    // Create sampler
    MonsterRender::RHI::SamplerDesc SamplerDesc;
    SamplerDesc.filter = MonsterRender::RHI::ESamplerFilter::Trilinear;
    SamplerDesc.addressU = MonsterRender::RHI::ESamplerAddressMode::Wrap;
    SamplerDesc.addressV = MonsterRender::RHI::ESamplerAddressMode::Wrap;
    SamplerDesc.addressW = MonsterRender::RHI::ESamplerAddressMode::Wrap;
    SamplerDesc.maxAnisotropy = 16;
    SamplerDesc.debugName = "CubeProxy Sampler";
    Sampler = Device->createSampler(SamplerDesc);
    if (!Sampler)
    {
        MR_LOG(LogCubeSceneProxy, Error, "Failed to create sampler");
        return false;
    }
    
    if (!CreateShaders())
    {
        MR_LOG(LogCubeSceneProxy, Error, "Failed to create shaders");
        return false;
    }
    
    if (!CreatePipelineState())
    {
        MR_LOG(LogCubeSceneProxy, Error, "Failed to create pipeline state");
        return false;
    }
    
    bResourcesInitialized = true;
    MR_LOG(LogCubeSceneProxy, Log, "CubeSceneProxy resources initialized successfully");
    
    return true;
}

void FCubeSceneProxy::Draw(
    MonsterRender::RHI::IRHICommandList* CmdList,
    const FMatrix& ViewMatrix,
    const FMatrix& ProjectionMatrix,
    const FVector& CameraPosition)
{
    // Draw without lighting (use empty light array)
    TArray<FLightSceneInfo*> EmptyLights;
    DrawWithLighting(CmdList, ViewMatrix, ProjectionMatrix, CameraPosition, EmptyLights);
}

void FCubeSceneProxy::DrawWithLighting(
    MonsterRender::RHI::IRHICommandList* CmdList,
    const FMatrix& ViewMatrix,
    const FMatrix& ProjectionMatrix,
    const FVector& CameraPosition,
    const TArray<FLightSceneInfo*>& AffectingLights)
{
    if (!CmdList || !bResourcesInitialized || !bVisible)
    {
        return;
    }
    
    // Note: Viewport and scissor should be set by the caller (e.g., renderSceneToViewport)
    // We don't override them here to support different render target sizes
    
    // Update uniform buffers
    UpdateTransformBuffer(ViewMatrix, ProjectionMatrix, CameraPosition);
    UpdateLightBuffer(AffectingLights);
    
    // Set pipeline state
    CmdList->setPipelineState(PipelineState);
    
    // Bind uniform buffers
    CmdList->setConstantBuffer(0, TransformUniformBuffer);
    CmdList->setConstantBuffer(3, LightUniformBuffer);
    
    // Bind textures
    if (Texture1)
    {
        CmdList->setShaderResource(1, Texture1);
    }
    if (Texture2)
    {
        CmdList->setShaderResource(2, Texture2);
    }
    
    // Bind vertex buffer
    TArray<TSharedPtr<MonsterRender::RHI::IRHIBuffer>> VertexBuffers;
    VertexBuffers.Add(VertexBuffer);
    CmdList->setVertexBuffers(0, std::span<TSharedPtr<MonsterRender::RHI::IRHIBuffer>>(VertexBuffers.GetData(), VertexBuffers.Num()));
    
    // Draw 36 vertices (6 faces * 2 triangles * 3 vertices)
    CmdList->draw(36, 0);
    
    MR_LOG(LogCubeSceneProxy, Verbose, "Cube drawn with %d lights", AffectingLights.Num());
}

void FCubeSceneProxy::UpdateModelMatrix(const FMatrix& NewLocalToWorld)
{
    SetLocalToWorld(NewLocalToWorld);
}

// ============================================================================
// Resource Creation
// ============================================================================

bool FCubeSceneProxy::CreateVertexBuffer()
{
    MR_LOG(LogCubeSceneProxy, Log, "Creating cube vertex buffer with normals...");
    
    // Generate cube vertices with normals
    TArray<FCubeLitVertex> Vertices;
    UCubeMeshComponent::GenerateCubeVertices(Vertices, CubeSize);
    
    // Create vertex buffer
    MonsterRender::RHI::BufferDesc BufferDesc;
    BufferDesc.size = static_cast<uint32>(sizeof(FCubeLitVertex) * Vertices.Num());
    BufferDesc.usage = MonsterRender::RHI::EResourceUsage::VertexBuffer;
    BufferDesc.cpuAccessible = true;
    BufferDesc.debugName = "CubeProxy Vertex Buffer";
    
    VertexBuffer = Device->createBuffer(BufferDesc);
    if (!VertexBuffer)
    {
        return false;
    }
    
    // Upload vertex data
    void* MappedData = VertexBuffer->map();
    if (!MappedData)
    {
        return false;
    }
    
    std::memcpy(MappedData, Vertices.GetData(), BufferDesc.size);
    VertexBuffer->unmap();
    
    // Debug: Log first 6 vertices to verify data (first face)
    MR_LOG(LogCubeSceneProxy, Log, "=== Vertex buffer debug (first 6 vertices) ===");
    for (int32 i = 0; i < 6 && i < Vertices.Num(); ++i)
    {
        const FCubeLitVertex& v = Vertices[i];
        MR_LOG(LogCubeSceneProxy, Log, "  V[%d]: pos=(%.2f,%.2f,%.2f), normal=(%.2f,%.2f,%.2f)",
               i, v.Position[0], v.Position[1], v.Position[2],
               v.Normal[0], v.Normal[1], v.Normal[2]);
    }
    
    MR_LOG(LogCubeSceneProxy, Log, "Vertex buffer created with %d vertices, size=%u bytes, stride=%u", 
           Vertices.Num(), BufferDesc.size, static_cast<uint32>(sizeof(FCubeLitVertex)));
    return true;
}

bool FCubeSceneProxy::CreateUniformBuffers()
{
    MR_LOG(LogCubeSceneProxy, Log, "Creating uniform buffers...");
    
    // Transform uniform buffer
    MonsterRender::RHI::BufferDesc TransformBufferDesc;
    TransformBufferDesc.size = sizeof(FCubeLitUniformBuffer);
    TransformBufferDesc.usage = MonsterRender::RHI::EResourceUsage::UniformBuffer;
    TransformBufferDesc.cpuAccessible = true;
    TransformBufferDesc.debugName = "CubeProxy Transform UBO";
    
    TransformUniformBuffer = Device->createBuffer(TransformBufferDesc);
    if (!TransformUniformBuffer)
    {
        return false;
    }
    
    // Light uniform buffer
    MonsterRender::RHI::BufferDesc LightBufferDesc;
    LightBufferDesc.size = sizeof(FCubeLightUniformBuffer);
    LightBufferDesc.usage = MonsterRender::RHI::EResourceUsage::UniformBuffer;
    LightBufferDesc.cpuAccessible = true;
    LightBufferDesc.debugName = "CubeProxy Light UBO";
    
    LightUniformBuffer = Device->createBuffer(LightBufferDesc);
    if (!LightUniformBuffer)
    {
        return false;
    }
    
    MR_LOG(LogCubeSceneProxy, Log, "Uniform buffers created");
    return true;
}

bool FCubeSceneProxy::CreateShaders()
{
    MR_LOG(LogCubeSceneProxy, Log, "Loading shaders for %s...",
        MonsterRender::RHI::GetRHIBackendName(RHIBackend));
    
    // Use absolute path to handle different working directories (e.g., RenderDoc)
    std::string ProjectRoot = resolveProjectRootFromExecutable();
    if (ProjectRoot.empty())
    {
        MR_LOG(LogCubeSceneProxy, Warning, "Failed to resolve project root from executable path, falling back to current working directory");
    }
    
    if (RHIBackend == MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        // Load SPIR-V shaders
        // First try lit shaders, fall back to original cube shaders
        std::string VsPath = ProjectRoot + "Shaders/CubeLit.vert.spv";
        std::vector<uint8> VsSpv = MonsterRender::ShaderCompiler::readFileBytes(VsPath.c_str());
        MR_LOG(LogCubeSceneProxy, Log, "Loaded CubeLit.vert.spv: %zu bytes", VsSpv.size());
        if (VsSpv.empty())
        {
            // Fall back to original shader
            VsPath = ProjectRoot + "Shaders/Cube.vert.spv";
            VsSpv = MonsterRender::ShaderCompiler::readFileBytes(VsPath.c_str());
            MR_LOG(LogCubeSceneProxy, Log, "Fallback to Cube.vert.spv: %zu bytes", VsSpv.size());
        }
        
        if (VsSpv.empty())
        {
            MR_LOG(LogCubeSceneProxy, Error, "Failed to load vertex shader");
            return false;
        }
        
        std::string PsPath = ProjectRoot + "Shaders/CubeLit.frag.spv";
        std::vector<uint8> PsSpv = MonsterRender::ShaderCompiler::readFileBytes(PsPath.c_str());
        MR_LOG(LogCubeSceneProxy, Log, "Loaded CubeLit.frag.spv: %zu bytes", PsSpv.size());
        if (PsSpv.empty())
        {
            // Fall back to original shader
            PsPath = ProjectRoot + "Shaders/Cube.frag.spv";
            PsSpv = MonsterRender::ShaderCompiler::readFileBytes(PsPath.c_str());
            MR_LOG(LogCubeSceneProxy, Log, "Fallback to Cube.frag.spv: %zu bytes", PsSpv.size());
        }
        
        if (PsSpv.empty())
        {
            MR_LOG(LogCubeSceneProxy, Error, "Failed to load fragment shader");
            return false;
        }
        
        VertexShader = Device->createVertexShader(std::span<const uint8>(VsSpv.data(), VsSpv.size()));
        PixelShader = Device->createPixelShader(std::span<const uint8>(PsSpv.data(), PsSpv.size()));
    }
    else if (RHIBackend == MonsterRender::RHI::ERHIBackend::OpenGL)
    {
        // Load GLSL shaders
        std::string VsPath = ProjectRoot + "Shaders/CubeLit_GL.vert";
        std::ifstream VsFile(VsPath, std::ios::binary);
        if (!VsFile.is_open())
        {
            VsPath = ProjectRoot + "Shaders/Cube_GL.vert";
            VsFile.open(VsPath, std::ios::binary);
        }
        
        if (!VsFile.is_open())
        {
            MR_LOG(LogCubeSceneProxy, Error, "Failed to open vertex shader: %s", VsPath.c_str());
            return false;
        }
        
        std::string VsSource((std::istreambuf_iterator<char>(VsFile)),
                              std::istreambuf_iterator<char>());
        VsFile.close();
        
        std::string PsPath = ProjectRoot + "Shaders/CubeLit_GL.frag";
        std::ifstream PsFile(PsPath, std::ios::binary);
        if (!PsFile.is_open())
        {
            PsPath = ProjectRoot + "Shaders/Cube_GL.frag";
            PsFile.open(PsPath, std::ios::binary);
        }
        
        if (!PsFile.is_open())
        {
            MR_LOG(LogCubeSceneProxy, Error, "Failed to open fragment shader: %s", PsPath.c_str());
            return false;
        }
        
        std::string PsSource((std::istreambuf_iterator<char>(PsFile)),
                              std::istreambuf_iterator<char>());
        PsFile.close();
        
        std::vector<uint8> VsData(VsSource.begin(), VsSource.end());
        VsData.push_back(0);
        
        std::vector<uint8> PsData(PsSource.begin(), PsSource.end());
        PsData.push_back(0);
        
        VertexShader = Device->createVertexShader(std::span<const uint8>(VsData.data(), VsData.size()));
        PixelShader = Device->createPixelShader(std::span<const uint8>(PsData.data(), PsData.size()));
    }
    else
    {
        MR_LOG(LogCubeSceneProxy, Error, "Unsupported RHI backend");
        return false;
    }
    
    if (!VertexShader || !PixelShader)
    {
        MR_LOG(LogCubeSceneProxy, Error, "Failed to create shaders");
        return false;
    }
    
    MR_LOG(LogCubeSceneProxy, Log, "Shaders loaded successfully");
    return true;
}

bool FCubeSceneProxy::CreatePipelineState()
{
    MR_LOG(LogCubeSceneProxy, Log, "Creating pipeline state...");
    
    MonsterRender::RHI::EPixelFormat RenderTargetFormat = Device->getSwapChainFormat();
    MonsterRender::RHI::EPixelFormat DepthFormat = Device->getDepthFormat();
    
    MonsterRender::RHI::PipelineStateDesc PipelineDesc;
    PipelineDesc.vertexShader = VertexShader;
    PipelineDesc.pixelShader = PixelShader;
    PipelineDesc.primitiveTopology = MonsterRender::RHI::EPrimitiveTopology::TriangleList;
    
    // Vertex layout: position (vec3) + normal (vec3) + texcoord (vec2)
    // Stride = 3*4 + 3*4 + 2*4 = 32 bytes
    MonsterRender::RHI::VertexAttribute PosAttr;
    PosAttr.location = 0;
    PosAttr.format = MonsterRender::RHI::EVertexFormat::Float3;
    PosAttr.offset = 0;
    PosAttr.semanticName = "POSITION";
    
    MonsterRender::RHI::VertexAttribute NormalAttr;
    NormalAttr.location = 1;
    NormalAttr.format = MonsterRender::RHI::EVertexFormat::Float3;
    NormalAttr.offset = sizeof(float) * 3;
    NormalAttr.semanticName = "NORMAL";
    
    MonsterRender::RHI::VertexAttribute TexCoordAttr;
    TexCoordAttr.location = 2;
    TexCoordAttr.format = MonsterRender::RHI::EVertexFormat::Float2;
    TexCoordAttr.offset = sizeof(float) * 6;
    TexCoordAttr.semanticName = "TEXCOORD";
    
    PipelineDesc.vertexLayout.attributes.push_back(PosAttr);
    PipelineDesc.vertexLayout.attributes.push_back(NormalAttr);
    PipelineDesc.vertexLayout.attributes.push_back(TexCoordAttr);
    PipelineDesc.vertexLayout.stride = sizeof(FCubeLitVertex);
    
    // Rasterizer state - enable backface culling
    // UE5 row-vector convention with transpose upload:
    // Combined with viewport Y-flip, the winding order is CW for front faces
    PipelineDesc.rasterizerState.fillMode = MonsterRender::RHI::EFillMode::Solid;
    PipelineDesc.rasterizerState.cullMode = MonsterRender::RHI::ECullMode::Back;
    PipelineDesc.rasterizerState.frontCounterClockwise = false;  // CW is front face
    
    // Depth testing - enable for proper occlusion
    PipelineDesc.depthStencilState.depthEnable = true;
    PipelineDesc.depthStencilState.depthWriteEnable = true;
    PipelineDesc.depthStencilState.depthCompareOp = MonsterRender::RHI::ECompareOp::Less;
    
    // Blend state
    PipelineDesc.blendState.blendEnable = false;
    
    // Render target format
    PipelineDesc.renderTargetFormats.push_back(RenderTargetFormat);
    PipelineDesc.depthStencilFormat = DepthFormat;
    
    PipelineDesc.debugName = "CubeProxy Pipeline";
    
    PipelineState = Device->createPipelineState(PipelineDesc);
    if (!PipelineState)
    {
        MR_LOG(LogCubeSceneProxy, Error, "Failed to create pipeline state");
        return false;
    }
    
    MR_LOG(LogCubeSceneProxy, Log, "Pipeline state created");
    return true;
}

// Forward declaration for helper function
static TSharedPtr<MonsterRender::RHI::IRHITexture> CreatePlaceholderTexture(
    MonsterRender::RHI::IRHIDevice* Device, uint32 Size, bool bCheckerboard);

bool FCubeSceneProxy::LoadTextures()
{
    MR_LOG(LogCubeSceneProxy, Log, "Loading textures...");
    
    // If textures were already set from component, use those
    if (Texture1 && Texture2)
    {
        MR_LOG(LogCubeSceneProxy, Log, "Using textures from component");
        return true;
    }
    
    // Resolve project root from executable path to handle different working directories (e.g., RenderDoc)
    // This searches upward from exe location for a directory containing "Shaders/"
    std::string ProjectRoot = resolveProjectRootFromExecutable();
    if (ProjectRoot.empty())
    {
        MR_LOG(LogCubeSceneProxy, Warning, "Failed to resolve project root from executable path, falling back to current working directory");
        ProjectRoot = normalizeDirPath(std::filesystem::current_path());
    }
    
    MR_LOG(LogCubeSceneProxy, Log, "Using project root for textures: %s", ProjectRoot.c_str());
    
    if (!Texture1)
    {
        MonsterRender::FTextureLoadInfo LoadInfo1;
        LoadInfo1.FilePath = ProjectRoot + "resources/textures/container.jpg";
        LoadInfo1.bGenerateMips = true;
        LoadInfo1.bSRGB = true;
        LoadInfo1.bFlipVertical = true;
        LoadInfo1.DesiredChannels = 4;
        
        Texture1 = MonsterRender::FTextureLoader::LoadFromFile(Device, LoadInfo1);
        if (!Texture1)
        {
            MR_LOG(LogCubeSceneProxy, Warning, "Failed to load container texture from: %s, creating placeholder", LoadInfo1.FilePath.c_str());
            Texture1 = CreatePlaceholderTexture(Device, 256, true);
        }
    }
    
    if (!Texture2)
    {
        MonsterRender::FTextureLoadInfo LoadInfo2;
        LoadInfo2.FilePath = ProjectRoot + "resources/textures/awesomeface.png";
        LoadInfo2.bGenerateMips = true;
        LoadInfo2.bSRGB = true;
        LoadInfo2.bFlipVertical = true;
        LoadInfo2.DesiredChannels = 4;
        
        Texture2 = MonsterRender::FTextureLoader::LoadFromFile(Device, LoadInfo2);
        if (!Texture2)
        {
            MR_LOG(LogCubeSceneProxy, Warning, "Failed to load awesomeface texture from: %s, creating placeholder", LoadInfo2.FilePath.c_str());
            Texture2 = CreatePlaceholderTexture(Device, 256, false);
        }
    }
    
    // Set texture layouts to SHADER_READ_ONLY_OPTIMAL for Vulkan
    // This is needed because texture loading may not set the correct layout
    if (Device->getRHIBackend() == MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        if (Texture1)
        {
            auto* vulkanTex1 = static_cast<MonsterRender::RHI::Vulkan::VulkanTexture*>(Texture1.Get());
            if (vulkanTex1)
            {
                vulkanTex1->setCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }
        if (Texture2)
        {
            auto* vulkanTex2 = static_cast<MonsterRender::RHI::Vulkan::VulkanTexture*>(Texture2.Get());
            if (vulkanTex2)
            {
                vulkanTex2->setCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }
    }
    
    MR_LOG(LogCubeSceneProxy, Log, "Textures loaded");
    return Texture1 != nullptr && Texture2 != nullptr;
}

// Helper function to create placeholder texture
static TSharedPtr<MonsterRender::RHI::IRHITexture> CreatePlaceholderTexture(
    MonsterRender::RHI::IRHIDevice* Device, uint32 Size, bool bCheckerboard)
{
    TArray<uint8> Data(Size * Size * 4);
    
    for (uint32 Y = 0; Y < Size; ++Y)
    {
        for (uint32 X = 0; X < Size; ++X)
        {
            uint32 Idx = (Y * Size + X) * 4;
            
            if (bCheckerboard)
            {
                bool IsWhite = ((X / 32) + (Y / 32)) % 2 == 0;
                uint8 Color = IsWhite ? 255 : 100;
                Data[Idx + 0] = Color;
                Data[Idx + 1] = Color;
                Data[Idx + 2] = Color;
            }
            else
            {
                Data[Idx + 0] = static_cast<uint8>((X * 255) / Size);
                Data[Idx + 1] = static_cast<uint8>((Y * 255) / Size);
                Data[Idx + 2] = 128;
            }
            Data[Idx + 3] = 255;
        }
    }
    
    MonsterRender::RHI::TextureDesc Desc;
    Desc.width = Size;
    Desc.height = Size;
    Desc.depth = 1;
    Desc.mipLevels = 1;
    Desc.arraySize = 1;
    Desc.format = MonsterRender::RHI::EPixelFormat::R8G8B8A8_SRGB;
    Desc.usage = MonsterRender::RHI::EResourceUsage::ShaderResource;
    Desc.debugName = bCheckerboard ? "Placeholder Checkerboard" : "Placeholder Gradient";
    
    auto Texture = Device->createTexture(Desc);
    if (Texture)
    {
        MonsterRender::FTextureData PlaceholderData;
        PlaceholderData.Width = Size;
        PlaceholderData.Height = Size;
        PlaceholderData.Channels = 4;
        PlaceholderData.MipLevels = 1;
        PlaceholderData.Pixels = Data.GetData();
        PlaceholderData.MipData.push_back(Data.GetData());
        PlaceholderData.MipSizes.push_back(Size * Size * 4);
        
        MonsterRender::RHI::IRHICommandList* CmdList = Device->getImmediateCommandList();
        if (CmdList)
        {
            MonsterRender::FTextureLoader::UploadTextureData(Device, CmdList, Texture, PlaceholderData);
        }
        
        PlaceholderData.Pixels = nullptr;
        PlaceholderData.MipData.clear();
    }
    
    return Texture;
}

void FCubeSceneProxy::UpdateTransformBuffer(
    const FMatrix& ViewMatrix,
    const FMatrix& ProjectionMatrix,
    const FVector& CameraPosition)
{
    FCubeLitUniformBuffer UBO;
    
    // Model matrix from local to world transform
    FMatrix ModelMatrix = GetLocalToWorld();
    
    // DEBUG: Log matrix values - all 4 rows of View matrix
    MR_LOG(LogCubeSceneProxy, Log, "UpdateTransformBuffer:");
    MR_LOG(LogCubeSceneProxy, Log, "  Model[0]: %.2f, %.2f, %.2f, %.2f", 
           ModelMatrix.M[0][0], ModelMatrix.M[0][1], ModelMatrix.M[0][2], ModelMatrix.M[0][3]);
    MR_LOG(LogCubeSceneProxy, Log, "  View[0]: %.2f, %.2f, %.2f, %.2f",
           ViewMatrix.M[0][0], ViewMatrix.M[0][1], ViewMatrix.M[0][2], ViewMatrix.M[0][3]);
    MR_LOG(LogCubeSceneProxy, Log, "  View[1]: %.2f, %.2f, %.2f, %.2f",
           ViewMatrix.M[1][0], ViewMatrix.M[1][1], ViewMatrix.M[1][2], ViewMatrix.M[1][3]);
    MR_LOG(LogCubeSceneProxy, Log, "  View[2]: %.2f, %.2f, %.2f, %.2f",
           ViewMatrix.M[2][0], ViewMatrix.M[2][1], ViewMatrix.M[2][2], ViewMatrix.M[2][3]);
    MR_LOG(LogCubeSceneProxy, Log, "  View[3]: %.2f, %.2f, %.2f, %.2f",
           ViewMatrix.M[3][0], ViewMatrix.M[3][1], ViewMatrix.M[3][2], ViewMatrix.M[3][3]);
    MR_LOG(LogCubeSceneProxy, Log, "  Proj[0]: %.2f, %.2f, %.2f, %.2f",
           ProjectionMatrix.M[0][0], ProjectionMatrix.M[0][1], ProjectionMatrix.M[0][2], ProjectionMatrix.M[0][3]);
    MR_LOG(LogCubeSceneProxy, Log, "  Proj[1]: %.2f, %.2f, %.2f, %.2f",
           ProjectionMatrix.M[1][0], ProjectionMatrix.M[1][1], ProjectionMatrix.M[1][2], ProjectionMatrix.M[1][3]);
    MR_LOG(LogCubeSceneProxy, Log, "  Proj[2]: %.2f, %.2f, %.2f, %.2f",
           ProjectionMatrix.M[2][0], ProjectionMatrix.M[2][1], ProjectionMatrix.M[2][2], ProjectionMatrix.M[2][3]);
    MR_LOG(LogCubeSceneProxy, Log, "  CameraPos: %.2f, %.2f, %.2f",
           CameraPosition.X, CameraPosition.Y, CameraPosition.Z);
    
    MatrixToFloatArray(ModelMatrix, UBO.Model);
    
    // View and projection matrices
    MatrixToFloatArray(ViewMatrix, UBO.View);
    MatrixToFloatArray(ProjectionMatrix, UBO.Projection);
    
    // Normal matrix (inverse transpose of model matrix upper-left 3x3)
    FMatrix NormalMatrix = GetLocalToWorld().Inverse().GetTransposed();
    MatrixToFloatArray(NormalMatrix, UBO.NormalMatrix);
    
    // Camera position
    UBO.CameraPosition[0] = static_cast<float>(CameraPosition.X);
    UBO.CameraPosition[1] = static_cast<float>(CameraPosition.Y);
    UBO.CameraPosition[2] = static_cast<float>(CameraPosition.Z);
    UBO.CameraPosition[3] = 1.0f;
    
    // Texture blend factor
    UBO.TextureBlend[0] = TextureBlendFactor;
    UBO.TextureBlend[1] = 0.0f;
    UBO.TextureBlend[2] = 0.0f;
    UBO.TextureBlend[3] = 0.0f;
    
    // Upload to GPU
    void* MappedData = TransformUniformBuffer->map();
    if (MappedData)
    {
        std::memcpy(MappedData, &UBO, sizeof(FCubeLitUniformBuffer));
        TransformUniformBuffer->unmap();
    }
}

void FCubeSceneProxy::UpdateLightBuffer(const TArray<FLightSceneInfo*>& Lights)
{
    FCubeLightUniformBuffer LightUBO;
    std::memset(&LightUBO, 0, sizeof(FCubeLightUniformBuffer));
    
    // Default ambient light
    LightUBO.AmbientColor[0] = 0.1f;
    LightUBO.AmbientColor[1] = 0.1f;
    LightUBO.AmbientColor[2] = 0.1f;
    LightUBO.AmbientColor[3] = 1.0f;
    
    // Process lights (up to 8)
    int32 NumLights = FMath::Min(Lights.Num(), 8);
    LightUBO.NumLights = NumLights;
    
    for (int32 i = 0; i < NumLights; ++i)
    {
        FLightSceneInfo* Light = Lights[i];
        if (!Light || !Light->Proxy)
        {
            continue;
        }
        
        FLightSceneProxy* Proxy = Light->Proxy;
        
        // Get light type
        ELightType LightType = Proxy->GetLightType();
        
        // Position/Direction
        if (LightType == ELightType::Directional)
        {
            FVector Direction = Proxy->GetDirection();
            LightUBO.Lights[i].Position[0] = static_cast<float>(Direction.X);
            LightUBO.Lights[i].Position[1] = static_cast<float>(Direction.Y);
            LightUBO.Lights[i].Position[2] = static_cast<float>(Direction.Z);
            LightUBO.Lights[i].Position[3] = 0.0f;  // 0 = directional
        }
        else
        {
            FVector Position = Proxy->GetPosition();
            LightUBO.Lights[i].Position[0] = static_cast<float>(Position.X);
            LightUBO.Lights[i].Position[1] = static_cast<float>(Position.Y);
            LightUBO.Lights[i].Position[2] = static_cast<float>(Position.Z);
            LightUBO.Lights[i].Position[3] = 1.0f;  // 1 = point/spot
        }
        
        // Color and intensity
        const FLinearColor& Color = Proxy->GetColor();
        float Intensity = Proxy->GetIntensity();
        LightUBO.Lights[i].Color[0] = Color.R;
        LightUBO.Lights[i].Color[1] = Color.G;
        LightUBO.Lights[i].Color[2] = Color.B;
        LightUBO.Lights[i].Color[3] = Intensity;
        
        // Additional params
        LightUBO.Lights[i].Params[0] = Proxy->GetRadius();
        LightUBO.Lights[i].Params[1] = Proxy->GetSourceRadius();
        LightUBO.Lights[i].Params[2] = 0.0f;
        LightUBO.Lights[i].Params[3] = 0.0f;
    }
    
    // If no lights, add a default directional light
    if (NumLights == 0)
    {
        LightUBO.NumLights = 1;
        
        // Default sun-like directional light
        LightUBO.Lights[0].Position[0] = -0.5f;
        LightUBO.Lights[0].Position[1] = -1.0f;
        LightUBO.Lights[0].Position[2] = -0.3f;
        LightUBO.Lights[0].Position[3] = 0.0f;
        
        LightUBO.Lights[0].Color[0] = 1.0f;
        LightUBO.Lights[0].Color[1] = 0.95f;
        LightUBO.Lights[0].Color[2] = 0.9f;
        LightUBO.Lights[0].Color[3] = 1.0f;
        
        LightUBO.Lights[0].Params[0] = 0.0f;
        LightUBO.Lights[0].Params[1] = 0.0f;
        LightUBO.Lights[0].Params[2] = 0.0f;
        LightUBO.Lights[0].Params[3] = 0.0f;
    }
    
    // Upload to GPU
    void* MappedData = LightUniformBuffer->map();
    if (MappedData)
    {
        std::memcpy(MappedData, &LightUBO, sizeof(FCubeLightUniformBuffer));
        LightUniformBuffer->unmap();
    }
}

void FCubeSceneProxy::MatrixToFloatArray(const FMatrix& Matrix, float* OutArray)
{
    // UE5 row-vector convention with GLSL v * M:
    // GLSL mat4 is column-major, need to transpose CPU row-major matrix
    // so that GPU rows match CPU rows for correct v * M computation
    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            OutArray[Col * 4 + Row] = static_cast<float>(Matrix.M[Row][Col]);
        }
    }
}

// ============================================================================
// Shadow Rendering Support
// ============================================================================

void FCubeSceneProxy::DrawWithShadows(
    MonsterRender::RHI::IRHICommandList* CmdList,
    const FMatrix& ViewMatrix,
    const FMatrix& ProjectionMatrix,
    const FVector& CameraPosition,
    const TArray<FLightSceneInfo*>& AffectingLights,
    const FMatrix& LightViewProjection,
    TSharedPtr<MonsterRender::RHI::IRHITexture> ShadowMap,
    const FVector4& ShadowParams)
{
    if (!CmdList || !bResourcesInitialized || !bVisible)
    {
        return;
    }
    
    // Create shadow resources if not yet created
    if (!ShadowPipelineState)
    {
        if (!CreateShadowShaders())
        {
            MR_LOG(LogCubeSceneProxy, Warning, "Failed to create shadow shaders, falling back to non-shadow rendering");
            DrawWithLighting(CmdList, ViewMatrix, ProjectionMatrix, CameraPosition, AffectingLights);
            return;
        }
        
        if (!CreateShadowPipelineState())
        {
            MR_LOG(LogCubeSceneProxy, Warning, "Failed to create shadow pipeline, falling back to non-shadow rendering");
            DrawWithLighting(CmdList, ViewMatrix, ProjectionMatrix, CameraPosition, AffectingLights);
            return;
        }
    }
    
    // Create shadow uniform buffer if needed
    if (!ShadowUniformBuffer)
    {
        MonsterRender::RHI::BufferDesc BufferDesc;
        BufferDesc.size = sizeof(FCubeShadowUniformBuffer);
        BufferDesc.usage = MonsterRender::RHI::EResourceUsage::UniformBuffer;
        BufferDesc.cpuAccessible = true;
        BufferDesc.debugName = "CubeProxy Shadow UBO";
        ShadowUniformBuffer = Device->createBuffer(BufferDesc);
        
        if (!ShadowUniformBuffer)
        {
            MR_LOG(LogCubeSceneProxy, Error, "Failed to create shadow uniform buffer");
            DrawWithLighting(CmdList, ViewMatrix, ProjectionMatrix, CameraPosition, AffectingLights);
            return;
        }
    }
    
    // Create shadow sampler if needed
    if (!ShadowSampler)
    {
        MonsterRender::RHI::SamplerDesc SamplerDesc;
        SamplerDesc.filter = MonsterRender::RHI::ESamplerFilter::Bilinear;
        SamplerDesc.addressU = MonsterRender::RHI::ESamplerAddressMode::Border;
        SamplerDesc.addressV = MonsterRender::RHI::ESamplerAddressMode::Border;
        SamplerDesc.addressW = MonsterRender::RHI::ESamplerAddressMode::Border;
        SamplerDesc.borderColor[0] = 1.0f;
        SamplerDesc.borderColor[1] = 1.0f;
        SamplerDesc.borderColor[2] = 1.0f;
        SamplerDesc.borderColor[3] = 1.0f;
        SamplerDesc.debugName = "CubeProxy Shadow Sampler";
        ShadowSampler = Device->createSampler(SamplerDesc);
    }
    
    // Update uniform buffers
    UpdateTransformBuffer(ViewMatrix, ProjectionMatrix, CameraPosition);
    UpdateLightBuffer(AffectingLights);
    
    // Get shadow map size
    uint32 ShadowMapWidth = ShadowMap ? ShadowMap->getWidth() : 1024;
    uint32 ShadowMapHeight = ShadowMap ? ShadowMap->getHeight() : 1024;
    UpdateShadowBuffer(LightViewProjection, ShadowParams, ShadowMapWidth, ShadowMapHeight);
    
    // Set shadow pipeline state
    CmdList->setPipelineState(ShadowPipelineState);
    
    // Bind uniform buffers
    CmdList->setConstantBuffer(0, TransformUniformBuffer);
    CmdList->setConstantBuffer(3, LightUniformBuffer);
    CmdList->setConstantBuffer(4, ShadowUniformBuffer);
    
    // Bind textures
    if (Texture1)
    {
        CmdList->setShaderResource(1, Texture1);
    }
    if (Texture2)
    {
        CmdList->setShaderResource(2, Texture2);
    }
    
    // Bind shadow map
    if (ShadowMap)
    {
        CmdList->setShaderResource(5, ShadowMap);
    }
    
    // Bind a default texture to binding 6 (diffuseTexture) for cube rendering
    // The shader will detect alpha = 0 and use texture1/texture2 instead
    // This ensures the descriptor set is complete for both cube and floor rendering
    if (Texture1)
    {
        CmdList->setShaderResource(6, Texture1);
    }
    
    // Bind vertex buffer
    TArray<TSharedPtr<MonsterRender::RHI::IRHIBuffer>> VertexBuffers;
    VertexBuffers.Add(VertexBuffer);
    CmdList->setVertexBuffers(0, std::span<TSharedPtr<MonsterRender::RHI::IRHIBuffer>>(VertexBuffers.GetData(), VertexBuffers.Num()));
    
    // Draw cube
    CmdList->draw(36, 0);
    
    MR_LOG(LogCubeSceneProxy, Verbose, "Cube drawn with shadows, %d lights", AffectingLights.Num());
}

void FCubeSceneProxy::UpdateShadowBuffer(
    const FMatrix& LightViewProjection,
    const FVector4& ShadowParams,
    uint32 ShadowMapWidth,
    uint32 ShadowMapHeight)
{
    if (!ShadowUniformBuffer)
    {
        return;
    }
    
    void* MappedData = ShadowUniformBuffer->map();
    if (!MappedData)
    {
        return;
    }
    
    FCubeShadowUniformBuffer* ShadowUBO = static_cast<FCubeShadowUniformBuffer*>(MappedData);
    
    // Set light view-projection matrix
    MatrixToFloatArray(LightViewProjection, ShadowUBO->LightViewProjection);
    
    // Set shadow parameters
    ShadowUBO->ShadowParams[0] = static_cast<float>(ShadowParams.X);  // Depth bias
    ShadowUBO->ShadowParams[1] = static_cast<float>(ShadowParams.Y);  // Slope bias
    ShadowUBO->ShadowParams[2] = static_cast<float>(ShadowParams.Z);  // Normal bias
    ShadowUBO->ShadowParams[3] = static_cast<float>(ShadowParams.W);  // Shadow distance
    
    // Set shadow map size
    ShadowUBO->ShadowMapSize[0] = static_cast<float>(ShadowMapWidth);
    ShadowUBO->ShadowMapSize[1] = static_cast<float>(ShadowMapHeight);
    ShadowUBO->ShadowMapSize[2] = 1.0f / static_cast<float>(ShadowMapWidth);
    ShadowUBO->ShadowMapSize[3] = 1.0f / static_cast<float>(ShadowMapHeight);
    
    ShadowUniformBuffer->unmap();
}

bool FCubeSceneProxy::CreateShadowShaders()
{
    MR_LOG(LogCubeSceneProxy, Log, "Creating shadow-enabled shaders...");
    
    std::string ProjectRoot = resolveProjectRootFromExecutable();
    if (ProjectRoot.empty())
    {
        MR_LOG(LogCubeSceneProxy, Error, "Failed to resolve project root");
        return false;
    }
    
    if (RHIBackend == MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        // Load and compile SPIR-V shaders for Vulkan
        MonsterRender::ShaderCompileOptions VsOpts;
        VsOpts.language = MonsterRender::EShaderLanguage::GLSL;
        VsOpts.stage = MonsterRender::EShaderStageKind::Vertex;
        
        MonsterRender::ShaderCompileOptions PsOpts;
        PsOpts.language = MonsterRender::EShaderLanguage::GLSL;
        PsOpts.stage = MonsterRender::EShaderStageKind::Fragment;
        
        std::string VsPath = ProjectRoot + "Shaders/CubeLitShadow.vert";
        std::string PsPath = ProjectRoot + "Shaders/CubeLitShadow.frag";
        
        std::vector<uint8> VsSpv = MonsterRender::ShaderCompiler::compileFromFile(VsPath, VsOpts);
        std::vector<uint8> PsSpv = MonsterRender::ShaderCompiler::compileFromFile(PsPath, PsOpts);
        
        if (VsSpv.empty() || PsSpv.empty())
        {
            MR_LOG(LogCubeSceneProxy, Error, "Failed to compile shadow shaders");
            return false;
        }
        
        ShadowVertexShader = Device->createVertexShader(std::span<const uint8>(VsSpv.data(), VsSpv.size()));
        ShadowPixelShader = Device->createPixelShader(std::span<const uint8>(PsSpv.data(), PsSpv.size()));
    }
    else if (RHIBackend == MonsterRender::RHI::ERHIBackend::OpenGL)
    {
        // Load GLSL shaders for OpenGL
        std::string VsPath = ProjectRoot + "Shaders/CubeLitShadow_GL.vert";
        std::string PsPath = ProjectRoot + "Shaders/CubeLitShadow_GL.frag";
        
        std::ifstream VsFile(VsPath, std::ios::binary);
        std::ifstream PsFile(PsPath, std::ios::binary);
        
        if (!VsFile.is_open() || !PsFile.is_open())
        {
            MR_LOG(LogCubeSceneProxy, Error, "Failed to open shadow shader files");
            return false;
        }
        
        std::string VsSource((std::istreambuf_iterator<char>(VsFile)), std::istreambuf_iterator<char>());
        std::string PsSource((std::istreambuf_iterator<char>(PsFile)), std::istreambuf_iterator<char>());
        VsFile.close();
        PsFile.close();
        
        std::vector<uint8> VsData(VsSource.begin(), VsSource.end());
        VsData.push_back(0);
        std::vector<uint8> PsData(PsSource.begin(), PsSource.end());
        PsData.push_back(0);
        
        ShadowVertexShader = Device->createVertexShader(std::span<const uint8>(VsData.data(), VsData.size()));
        ShadowPixelShader = Device->createPixelShader(std::span<const uint8>(PsData.data(), PsData.size()));
    }
    else
    {
        MR_LOG(LogCubeSceneProxy, Error, "Unsupported RHI backend for shadow shaders");
        return false;
    }
    
    if (!ShadowVertexShader || !ShadowPixelShader)
    {
        MR_LOG(LogCubeSceneProxy, Error, "Failed to create shadow shaders");
        return false;
    }
    
    MR_LOG(LogCubeSceneProxy, Log, "Shadow shaders created successfully");
    return true;
}

bool FCubeSceneProxy::CreateShadowPipelineState()
{
    MR_LOG(LogCubeSceneProxy, Log, "Creating shadow pipeline state...");
    
    MonsterRender::RHI::EPixelFormat RenderTargetFormat = Device->getSwapChainFormat();
    MonsterRender::RHI::EPixelFormat DepthFormat = Device->getDepthFormat();
    
    MonsterRender::RHI::PipelineStateDesc PipelineDesc;
    PipelineDesc.vertexShader = ShadowVertexShader;
    PipelineDesc.pixelShader = ShadowPixelShader;
    PipelineDesc.primitiveTopology = MonsterRender::RHI::EPrimitiveTopology::TriangleList;
    
    // Vertex layout: position (vec3) + normal (vec3) + texcoord (vec2)
    MonsterRender::RHI::VertexAttribute PosAttr;
    PosAttr.location = 0;
    PosAttr.format = MonsterRender::RHI::EVertexFormat::Float3;
    PosAttr.offset = 0;
    PosAttr.semanticName = "POSITION";
    
    MonsterRender::RHI::VertexAttribute NormalAttr;
    NormalAttr.location = 1;
    NormalAttr.format = MonsterRender::RHI::EVertexFormat::Float3;
    NormalAttr.offset = sizeof(float) * 3;
    NormalAttr.semanticName = "NORMAL";
    
    MonsterRender::RHI::VertexAttribute TexCoordAttr;
    TexCoordAttr.location = 2;
    TexCoordAttr.format = MonsterRender::RHI::EVertexFormat::Float2;
    TexCoordAttr.offset = sizeof(float) * 6;
    TexCoordAttr.semanticName = "TEXCOORD";
    
    PipelineDesc.vertexLayout.attributes.push_back(PosAttr);
    PipelineDesc.vertexLayout.attributes.push_back(NormalAttr);
    PipelineDesc.vertexLayout.attributes.push_back(TexCoordAttr);
    PipelineDesc.vertexLayout.stride = sizeof(FCubeLitVertex);
    
    // UE5 row-vector convention: CW is front face
    PipelineDesc.rasterizerState.fillMode = MonsterRender::RHI::EFillMode::Solid;
    PipelineDesc.rasterizerState.cullMode = MonsterRender::RHI::ECullMode::Back;
    PipelineDesc.rasterizerState.frontCounterClockwise = false;
    
    // Depth testing
    PipelineDesc.depthStencilState.depthEnable = true;
    PipelineDesc.depthStencilState.depthWriteEnable = true;
    PipelineDesc.depthStencilState.depthCompareOp = MonsterRender::RHI::ECompareOp::Less;
    
    // Blend state
    PipelineDesc.blendState.blendEnable = false;
    
    // Render target format
    PipelineDesc.renderTargetFormats.push_back(RenderTargetFormat);
    PipelineDesc.depthStencilFormat = DepthFormat;
    
    PipelineDesc.debugName = "CubeProxy Shadow Pipeline";
    
    ShadowPipelineState = Device->createPipelineState(PipelineDesc);
    if (!ShadowPipelineState)
    {
        MR_LOG(LogCubeSceneProxy, Error, "Failed to create shadow pipeline state");
        return false;
    }
    
    MR_LOG(LogCubeSceneProxy, Log, "Shadow pipeline state created");
    return true;
}

} // namespace MonsterEngine
