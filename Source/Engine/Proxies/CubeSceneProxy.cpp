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
#include <fstream>
#include <vector>
#include <span>

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogCubeSceneProxy, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

FCubeSceneProxy::FCubeSceneProxy(const UCubeMeshComponent* InComponent)
    : FPrimitiveSceneProxy(InComponent, "CubeSceneProxy")
    , Device(nullptr)
    , RHIBackend(MonsterRender::RHI::ERHIBackend::Unknown)
    , TextureBlendFactor(0.2f)
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
    
    MR_LOG(LogCubeSceneProxy, Verbose, "CubeSceneProxy destroyed");
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
    
    // Debug: Log first vertex to verify data
    if (Vertices.Num() > 0)
    {
        const FCubeLitVertex& v = Vertices[0];
        MR_LOG(LogCubeSceneProxy, Log, "First vertex: pos=(%.2f,%.2f,%.2f), normal=(%.2f,%.2f,%.2f)",
               v.Position[0], v.Position[1], v.Position[2],
               v.Normal[0], v.Normal[1], v.Normal[2]);
    }
    
    MR_LOG(LogCubeSceneProxy, Log, "Vertex buffer created with %d vertices, size=%u bytes", 
           Vertices.Num(), BufferDesc.size);
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
    
    if (RHIBackend == MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        // Load SPIR-V shaders
        // First try lit shaders, fall back to original cube shaders
        std::vector<uint8> VsSpv = MonsterRender::ShaderCompiler::readFileBytes("Shaders/CubeLit.vert.spv");
        MR_LOG(LogCubeSceneProxy, Log, "Loaded CubeLit.vert.spv: %zu bytes", VsSpv.size());
        if (VsSpv.empty())
        {
            // Fall back to original shader
            VsSpv = MonsterRender::ShaderCompiler::readFileBytes("Shaders/Cube.vert.spv");
            MR_LOG(LogCubeSceneProxy, Log, "Fallback to Cube.vert.spv: %zu bytes", VsSpv.size());
        }
        
        if (VsSpv.empty())
        {
            MR_LOG(LogCubeSceneProxy, Error, "Failed to load vertex shader");
            return false;
        }
        
        std::vector<uint8> PsSpv = MonsterRender::ShaderCompiler::readFileBytes("Shaders/CubeLit.frag.spv");
        MR_LOG(LogCubeSceneProxy, Log, "Loaded CubeLit.frag.spv: %zu bytes", PsSpv.size());
        if (PsSpv.empty())
        {
            // Fall back to original shader
            PsSpv = MonsterRender::ShaderCompiler::readFileBytes("Shaders/Cube.frag.spv");
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
        std::ifstream VsFile("Shaders/CubeLit_GL.vert", std::ios::binary);
        if (!VsFile.is_open())
        {
            VsFile.open("Shaders/Cube_GL.vert", std::ios::binary);
        }
        
        if (!VsFile.is_open())
        {
            MR_LOG(LogCubeSceneProxy, Error, "Failed to open vertex shader");
            return false;
        }
        
        std::string VsSource((std::istreambuf_iterator<char>(VsFile)),
                              std::istreambuf_iterator<char>());
        VsFile.close();
        
        std::ifstream PsFile("Shaders/CubeLit_GL.frag", std::ios::binary);
        if (!PsFile.is_open())
        {
            PsFile.open("Shaders/Cube_GL.frag", std::ios::binary);
        }
        
        if (!PsFile.is_open())
        {
            MR_LOG(LogCubeSceneProxy, Error, "Failed to open fragment shader");
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
    
    // Rasterizer state - DEBUG: disable culling to see all faces
    PipelineDesc.rasterizerState.fillMode = MonsterRender::RHI::EFillMode::Solid;
    PipelineDesc.rasterizerState.cullMode = MonsterRender::RHI::ECullMode::None;
    PipelineDesc.rasterizerState.frontCounterClockwise = false;
    
    // Depth stencil state - DEBUG: disable depth test to ensure visibility
    PipelineDesc.depthStencilState.depthEnable = false;
    PipelineDesc.depthStencilState.depthWriteEnable = false;
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
    
    // Otherwise load default textures
    if (!Texture1)
    {
        MonsterRender::FTextureLoadInfo LoadInfo1;
        LoadInfo1.FilePath = "resources/textures/container.jpg";
        LoadInfo1.bGenerateMips = true;
        LoadInfo1.bSRGB = true;
        LoadInfo1.bFlipVertical = true;
        LoadInfo1.DesiredChannels = 4;
        
        Texture1 = MonsterRender::FTextureLoader::LoadFromFile(Device, LoadInfo1);
        if (!Texture1)
        {
            MR_LOG(LogCubeSceneProxy, Warning, "Failed to load container texture, creating placeholder");
            Texture1 = CreatePlaceholderTexture(Device, 256, true);
        }
    }
    
    if (!Texture2)
    {
        MonsterRender::FTextureLoadInfo LoadInfo2;
        LoadInfo2.FilePath = "resources/textures/awesomeface.png";
        LoadInfo2.bGenerateMips = true;
        LoadInfo2.bSRGB = true;
        LoadInfo2.bFlipVertical = true;
        LoadInfo2.DesiredChannels = 4;
        
        Texture2 = MonsterRender::FTextureLoader::LoadFromFile(Device, LoadInfo2);
        if (!Texture2)
        {
            MR_LOG(LogCubeSceneProxy, Warning, "Failed to load awesomeface texture, creating placeholder");
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
    // FMatrix is row-major, GPU expects column-major.
    // Transpose while copying.
    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            OutArray[Col * 4 + Row] = static_cast<float>(Matrix.M[Row][Col]);
        }
    }
}

} // namespace MonsterEngine
