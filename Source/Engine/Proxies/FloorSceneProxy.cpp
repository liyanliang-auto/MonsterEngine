// Copyright Monster Engine. All Rights Reserved.

/**
 * @file FloorSceneProxy.cpp
 * @brief Implementation of floor scene proxy for rendering
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/StaticMeshRender.cpp
 */

#include "Engine/Proxies/FloorSceneProxy.h"
#include "Engine/Components/FloorMeshComponent.h"
#include "Engine/LightSceneInfo.h"
#include "Engine/LightSceneProxy.h"
#include "Core/Logging/LogMacros.h"
#include "Core/ShaderCompiler.h"
#include "Math/MonsterMath.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <span>

#if PLATFORM_WINDOWS
 #include <Windows.h>
#endif

namespace MonsterEngine
{

// Define log category for floor proxy
DEFINE_LOG_CATEGORY_STATIC(LogFloorSceneProxy, Log, All);

// ============================================================================
// Helper Functions
// ============================================================================

static std::string normalizeFloorDirPath(const std::filesystem::path& InPath)
{
    std::string Result = InPath.lexically_normal().generic_string();
    if (!Result.empty() && Result.back() != '/')
    {
        Result.push_back('/');
    }
    return Result;
}

static std::string resolveFloorProjectRoot()
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
            return normalizeFloorDirPath(Cursor);
        }

        if (!Cursor.has_parent_path())
        {
            break;
        }
        Cursor = Cursor.parent_path();
    }

    return std::string();
}

// ============================================================================
// Construction / Destruction
// ============================================================================

FFloorSceneProxy::FFloorSceneProxy(const UFloorMeshComponent* InComponent)
    : FPrimitiveSceneProxy(InComponent, "FloorSceneProxy")
    , Device(nullptr)
    , RHIBackend(MonsterRender::RHI::ERHIBackend::Unknown)
    , VertexCount(6)
    , FloorSize(25.0f)
    , TextureTile(25.0f)
    , bResourcesInitialized(false)
    , bVisible(true)
{
    if (InComponent)
    {
        FloorSize = InComponent->GetFloorSize();
        TextureTile = InComponent->GetTextureTile();
        FloorTexture = InComponent->GetTexture();
        Sampler = InComponent->GetSampler();
    }
    
    MR_LOG(LogFloorSceneProxy, Verbose, "FloorSceneProxy created");
}

FFloorSceneProxy::~FFloorSceneProxy()
{
    // Release GPU resources
    VertexBuffer.Reset();
    TransformUniformBuffer.Reset();
    LightUniformBuffer.Reset();
    ShadowUniformBuffer.Reset();
    FloorTexture.Reset();
    Sampler.Reset();
    VertexShader.Reset();
    PixelShader.Reset();
    PipelineState.Reset();
    ShadowVertexShader.Reset();
    ShadowPixelShader.Reset();
    ShadowPipelineState.Reset();
    ShadowSampler.Reset();
    
    MR_LOG(LogFloorSceneProxy, Verbose, "FloorSceneProxy destroyed");
}

// ============================================================================
// FPrimitiveSceneProxy Interface
// ============================================================================

SIZE_T FFloorSceneProxy::GetTypeHash() const
{
    static SIZE_T TypeHash = 0x87654321;  // Unique hash for floor proxy
    return TypeHash;
}

// ============================================================================
// Rendering
// ============================================================================

bool FFloorSceneProxy::InitializeResources(MonsterRender::RHI::IRHIDevice* InDevice)
{
    if (bResourcesInitialized)
    {
        return true;
    }
    
    if (!InDevice)
    {
        MR_LOG(LogFloorSceneProxy, Error, "Cannot initialize resources: null device");
        return false;
    }
    
    Device = InDevice;
    RHIBackend = Device->getRHIBackend();
    
    MR_LOG(LogFloorSceneProxy, Log, "Initializing FloorSceneProxy resources for %s",
        MonsterRender::RHI::GetRHIBackendName(RHIBackend));
    
    // Create resources in order
    if (!CreateVertexBuffer())
    {
        MR_LOG(LogFloorSceneProxy, Error, "Failed to create vertex buffer");
        return false;
    }
    
    if (!CreateUniformBuffers())
    {
        MR_LOG(LogFloorSceneProxy, Error, "Failed to create uniform buffers");
        return false;
    }
    
    // Create sampler if not provided
    if (!Sampler)
    {
        MonsterRender::RHI::SamplerDesc SamplerDesc;
        SamplerDesc.filter = MonsterRender::RHI::ESamplerFilter::Trilinear;
        SamplerDesc.addressU = MonsterRender::RHI::ESamplerAddressMode::Wrap;
        SamplerDesc.addressV = MonsterRender::RHI::ESamplerAddressMode::Wrap;
        SamplerDesc.addressW = MonsterRender::RHI::ESamplerAddressMode::Wrap;
        SamplerDesc.maxAnisotropy = 16;
        SamplerDesc.debugName = "FloorProxy Sampler";
        Sampler = Device->createSampler(SamplerDesc);
        if (!Sampler)
        {
            MR_LOG(LogFloorSceneProxy, Error, "Failed to create sampler");
            return false;
        }
    }
    
    if (!CreateShaders())
    {
        MR_LOG(LogFloorSceneProxy, Error, "Failed to create shaders");
        return false;
    }
    
    if (!CreatePipelineState())
    {
        MR_LOG(LogFloorSceneProxy, Error, "Failed to create pipeline state");
        return false;
    }
    
    // Create shadow resources
    if (!CreateShadowShaders())
    {
        MR_LOG(LogFloorSceneProxy, Warning, "Failed to create shadow shaders - shadows disabled");
    }
    else if (!CreateShadowPipelineState())
    {
        MR_LOG(LogFloorSceneProxy, Warning, "Failed to create shadow pipeline - shadows disabled");
    }
    
    bResourcesInitialized = true;
    MR_LOG(LogFloorSceneProxy, Log, "FloorSceneProxy resources initialized successfully");
    
    return true;
}

void FFloorSceneProxy::Draw(
    MonsterRender::RHI::IRHICommandList* CmdList,
    const FMatrix& ViewMatrix,
    const FMatrix& ProjectionMatrix,
    const FVector& CameraPosition)
{
    // Draw without lighting (use empty light array)
    TArray<FLightSceneInfo*> EmptyLights;
    DrawWithLighting(CmdList, ViewMatrix, ProjectionMatrix, CameraPosition, EmptyLights);
}

void FFloorSceneProxy::DrawWithLighting(
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
    
    // Update uniform buffers
    UpdateTransformBuffer(ViewMatrix, ProjectionMatrix, CameraPosition);
    UpdateLightBuffer(AffectingLights);
    
    // Set pipeline state
    CmdList->setPipelineState(PipelineState);
    
    // Bind uniform buffers
    CmdList->setConstantBuffer(0, TransformUniformBuffer);
    CmdList->setConstantBuffer(3, LightUniformBuffer);
    
    // Bind texture to slot 6 (diffuseTexture in CubeLit shader)
    // Both texture and sampler must be valid for COMBINED_IMAGE_SAMPLER
    if (FloorTexture && Sampler)
    {
        CmdList->setShaderResource(6, FloorTexture);
        CmdList->setSampler(6, Sampler);
    }
    else
    {
        MR_LOG(LogFloorSceneProxy, Warning, "Floor texture or sampler is null - rendering without texture");
    }
    
    // Bind vertex buffer
    TArray<TSharedPtr<MonsterRender::RHI::IRHIBuffer>> VertexBuffers;
    VertexBuffers.Add(VertexBuffer);
    CmdList->setVertexBuffers(0, std::span<TSharedPtr<MonsterRender::RHI::IRHIBuffer>>(VertexBuffers.GetData(), VertexBuffers.Num()));
    
    // Draw 6 vertices (2 triangles)
    CmdList->draw(VertexCount, 0);
    
    MR_LOG(LogFloorSceneProxy, Verbose, "Floor drawn with %d lights", AffectingLights.Num());
}

void FFloorSceneProxy::DrawWithShadows(
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
    
    // Fall back to non-shadow rendering if shadow resources not available
    if (!ShadowPipelineState || !ShadowMap)
    {
        DrawWithLighting(CmdList, ViewMatrix, ProjectionMatrix, CameraPosition, AffectingLights);
        return;
    }
    
    // Update uniform buffers
    UpdateTransformBuffer(ViewMatrix, ProjectionMatrix, CameraPosition);
    UpdateLightBuffer(AffectingLights);
    
    // Get shadow map dimensions
    uint32 ShadowMapWidth = ShadowMap->getWidth();
    uint32 ShadowMapHeight = ShadowMap->getHeight();
    UpdateShadowBuffer(LightViewProjection, ShadowParams, ShadowMapWidth, ShadowMapHeight);
    
    // Set shadow pipeline state
    CmdList->setPipelineState(ShadowPipelineState);
    
    // Bind uniform buffers
    CmdList->setConstantBuffer(0, TransformUniformBuffer);
    CmdList->setConstantBuffer(3, LightUniformBuffer);
    CmdList->setConstantBuffer(4, ShadowUniformBuffer);
    
    // Bind texture to slot 6 (diffuseTexture in CubeLit shader)
    // Both texture and sampler must be valid for COMBINED_IMAGE_SAMPLER
    if (FloorTexture && Sampler)
    {
        CmdList->setShaderResource(6, FloorTexture);
        CmdList->setSampler(6, Sampler);
    }
    else
    {
        MR_LOG(LogFloorSceneProxy, Warning, "Floor texture or sampler is null in shadow pass - rendering without texture");
    }
    
    // Bind shadow map to slot 5
    if (ShadowMap && ShadowSampler)
    {
        CmdList->setShaderResource(5, ShadowMap);
        CmdList->setSampler(5, ShadowSampler);
    }
    
    // Bind vertex buffer
    TArray<TSharedPtr<MonsterRender::RHI::IRHIBuffer>> VertexBuffers;
    VertexBuffers.Add(VertexBuffer);
    CmdList->setVertexBuffers(0, std::span<TSharedPtr<MonsterRender::RHI::IRHIBuffer>>(VertexBuffers.GetData(), VertexBuffers.Num()));
    
    // Draw 6 vertices (2 triangles)
    CmdList->draw(VertexCount, 0);
    
    MR_LOG(LogFloorSceneProxy, Verbose, "Floor drawn with shadows and %d lights", AffectingLights.Num());
}

void FFloorSceneProxy::UpdateModelMatrix(const FMatrix& NewLocalToWorld)
{
    SetLocalToWorld(NewLocalToWorld);
}

void FFloorSceneProxy::SetTexture(TSharedPtr<MonsterRender::RHI::IRHITexture> InTexture)
{
    FloorTexture = InTexture;
}

void FFloorSceneProxy::SetSampler(TSharedPtr<MonsterRender::RHI::IRHISampler> InSampler)
{
    Sampler = InSampler;
}

// ============================================================================
// Resource Creation
// ============================================================================

bool FFloorSceneProxy::CreateVertexBuffer()
{
    MR_LOG(LogFloorSceneProxy, Log, "Creating floor vertex buffer...");
    
    // Floor vertices: 2 triangles forming a large plane
    // Format: Position(3) + Normal(3) + TexCoord(2) = 8 floats per vertex
    // Texture coordinates are scaled by TextureTile to create tiling effect
    float planeVertices[] = {
        // Positions               // Normals          // TexCoords
         FloorSize, -0.5f,  FloorSize, 0.0f, 1.0f, 0.0f, TextureTile, 0.0f,
        -FloorSize, -0.5f,  FloorSize, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        -FloorSize, -0.5f, -FloorSize, 0.0f, 1.0f, 0.0f, 0.0f, TextureTile,

         FloorSize, -0.5f,  FloorSize, 0.0f, 1.0f, 0.0f, TextureTile, 0.0f,
        -FloorSize, -0.5f, -FloorSize, 0.0f, 1.0f, 0.0f, 0.0f, TextureTile,
         FloorSize, -0.5f, -FloorSize, 0.0f, 1.0f, 0.0f, TextureTile, TextureTile
    };
    
    uint32 vertexDataSize = sizeof(planeVertices);
    VertexCount = 6;
    
    // Create vertex buffer
    MonsterRender::RHI::BufferDesc BufferDesc;
    BufferDesc.size = vertexDataSize;
    BufferDesc.usage = MonsterRender::RHI::EResourceUsage::VertexBuffer;
    BufferDesc.cpuAccessible = true;
    BufferDesc.debugName = "FloorProxy Vertex Buffer";
    
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
    
    std::memcpy(MappedData, planeVertices, vertexDataSize);
    VertexBuffer->unmap();
    
    MR_LOG(LogFloorSceneProxy, Log, "Vertex buffer created with %d vertices, size=%u bytes", 
           VertexCount, vertexDataSize);
    return true;
}

bool FFloorSceneProxy::CreateUniformBuffers()
{
    MR_LOG(LogFloorSceneProxy, Log, "Creating uniform buffers...");
    
    // Transform uniform buffer
    MonsterRender::RHI::BufferDesc TransformBufferDesc;
    TransformBufferDesc.size = sizeof(FFloorUniformBuffer);
    TransformBufferDesc.usage = MonsterRender::RHI::EResourceUsage::UniformBuffer;
    TransformBufferDesc.cpuAccessible = true;
    TransformBufferDesc.debugName = "FloorProxy Transform UBO";
    
    TransformUniformBuffer = Device->createBuffer(TransformBufferDesc);
    if (!TransformUniformBuffer)
    {
        return false;
    }
    
    // Light uniform buffer
    MonsterRender::RHI::BufferDesc LightBufferDesc;
    LightBufferDesc.size = sizeof(FFloorLightUniformBuffer);
    LightBufferDesc.usage = MonsterRender::RHI::EResourceUsage::UniformBuffer;
    LightBufferDesc.cpuAccessible = true;
    LightBufferDesc.debugName = "FloorProxy Light UBO";
    
    LightUniformBuffer = Device->createBuffer(LightBufferDesc);
    if (!LightUniformBuffer)
    {
        return false;
    }
    
    // Shadow uniform buffer
    MonsterRender::RHI::BufferDesc ShadowBufferDesc;
    ShadowBufferDesc.size = sizeof(FFloorShadowUniformBuffer);
    ShadowBufferDesc.usage = MonsterRender::RHI::EResourceUsage::UniformBuffer;
    ShadowBufferDesc.cpuAccessible = true;
    ShadowBufferDesc.debugName = "FloorProxy Shadow UBO";
    
    ShadowUniformBuffer = Device->createBuffer(ShadowBufferDesc);
    if (!ShadowUniformBuffer)
    {
        return false;
    }
    
    MR_LOG(LogFloorSceneProxy, Log, "Uniform buffers created");
    return true;
}

bool FFloorSceneProxy::CreateShaders()
{
    MR_LOG(LogFloorSceneProxy, Log, "Creating floor shaders...");
    
    std::string ProjectRoot = resolveFloorProjectRoot();
    if (ProjectRoot.empty())
    {
        MR_LOG(LogFloorSceneProxy, Warning, "Failed to resolve project root");
    }
    
    if (RHIBackend == MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        // Load SPIR-V shaders - use CubeLit shaders which have compatible vertex format
        std::string VsPath = ProjectRoot + "Shaders/CubeLit.vert.spv";
        std::vector<uint8> VsSpv = MonsterRender::ShaderCompiler::readFileBytes(VsPath.c_str());
        if (VsSpv.empty())
        {
            MR_LOG(LogFloorSceneProxy, Error, "Failed to load vertex shader: %s", VsPath.c_str());
            return false;
        }
        
        std::string PsPath = ProjectRoot + "Shaders/CubeLit.frag.spv";
        std::vector<uint8> PsSpv = MonsterRender::ShaderCompiler::readFileBytes(PsPath.c_str());
        if (PsSpv.empty())
        {
            MR_LOG(LogFloorSceneProxy, Error, "Failed to load pixel shader: %s", PsPath.c_str());
            return false;
        }
        
        VertexShader = Device->createVertexShader(std::span<const uint8>(VsSpv.data(), VsSpv.size()));
        PixelShader = Device->createPixelShader(std::span<const uint8>(PsSpv.data(), PsSpv.size()));
    }
    else
    {
        MR_LOG(LogFloorSceneProxy, Error, "Unsupported RHI backend for floor shaders");
        return false;
    }
    
    if (!VertexShader || !PixelShader)
    {
        MR_LOG(LogFloorSceneProxy, Error, "Failed to create shaders");
        return false;
    }
    
    MR_LOG(LogFloorSceneProxy, Log, "Shaders created successfully");
    return true;
}

bool FFloorSceneProxy::CreatePipelineState()
{
    MR_LOG(LogFloorSceneProxy, Log, "Creating floor pipeline state...");
    
    MonsterRender::RHI::PipelineStateDesc PipelineDesc;
    PipelineDesc.vertexShader = VertexShader;
    PipelineDesc.pixelShader = PixelShader;
    PipelineDesc.primitiveTopology = MonsterRender::RHI::EPrimitiveTopology::TriangleList;
    PipelineDesc.depthStencilState.depthEnable = true;
    PipelineDesc.depthStencilState.depthWriteEnable = true;
    PipelineDesc.depthStencilState.depthFunc = MonsterRender::RHI::EComparisonFunc::Less;
    PipelineDesc.rasterizerState.cullMode = MonsterRender::RHI::ECullMode::Back;
    PipelineDesc.rasterizerState.frontCounterClockwise = true;
    PipelineDesc.rasterizerState.fillMode = MonsterRender::RHI::EFillMode::Solid;
    PipelineDesc.debugName = "FloorProxy Pipeline State";
    
    // Vertex input layout: Position(3) + Normal(3) + TexCoord(2) = 32 bytes stride
    PipelineDesc.vertexLayout.stride = 32;
    
    MonsterRender::RHI::VertexAttribute positionAttr;
    positionAttr.location = 0;
    positionAttr.format = MonsterRender::RHI::EVertexFormat::Float3;
    positionAttr.offset = 0;
    positionAttr.semanticName = "POSITION";
    PipelineDesc.vertexLayout.attributes.Add(positionAttr);
    
    MonsterRender::RHI::VertexAttribute normalAttr;
    normalAttr.location = 1;
    normalAttr.format = MonsterRender::RHI::EVertexFormat::Float3;
    normalAttr.offset = 12;  // 3 floats * 4 bytes
    normalAttr.semanticName = "NORMAL";
    PipelineDesc.vertexLayout.attributes.Add(normalAttr);
    
    MonsterRender::RHI::VertexAttribute texCoordAttr;
    texCoordAttr.location = 2;
    texCoordAttr.format = MonsterRender::RHI::EVertexFormat::Float2;
    texCoordAttr.offset = 24;  // 6 floats * 4 bytes
    texCoordAttr.semanticName = "TEXCOORD";
    PipelineDesc.vertexLayout.attributes.Add(texCoordAttr);
    
    PipelineState = Device->createPipelineState(PipelineDesc);
    if (!PipelineState)
    {
        MR_LOG(LogFloorSceneProxy, Error, "Failed to create pipeline state");
        return false;
    }
    
    MR_LOG(LogFloorSceneProxy, Log, "Pipeline state created successfully");
    return true;
}

bool FFloorSceneProxy::CreateShadowShaders()
{
    MR_LOG(LogFloorSceneProxy, Log, "Creating shadow shaders...");
    
    std::string ProjectRoot = resolveFloorProjectRoot();
    if (ProjectRoot.empty())
    {
        MR_LOG(LogFloorSceneProxy, Warning, "Failed to resolve project root for shadow shaders");
        return false;
    }
    
    if (RHIBackend == MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        // Load shadow SPIR-V shaders - use CubeLitShadow shaders
        std::string VsPath = ProjectRoot + "Shaders/CubeLitShadow.vert.spv";
        std::vector<uint8> VsSpv = MonsterRender::ShaderCompiler::readFileBytes(VsPath.c_str());
        if (VsSpv.empty())
        {
            MR_LOG(LogFloorSceneProxy, Warning, "Shadow vertex shader not found: %s", VsPath.c_str());
            return false;
        }
        
        std::string PsPath = ProjectRoot + "Shaders/CubeLitShadow.frag.spv";
        std::vector<uint8> PsSpv = MonsterRender::ShaderCompiler::readFileBytes(PsPath.c_str());
        if (PsSpv.empty())
        {
            MR_LOG(LogFloorSceneProxy, Warning, "Shadow pixel shader not found: %s", PsPath.c_str());
            return false;
        }
        
        ShadowVertexShader = Device->createVertexShader(std::span<const uint8>(VsSpv.data(), VsSpv.size()));
        ShadowPixelShader = Device->createPixelShader(std::span<const uint8>(PsSpv.data(), PsSpv.size()));
    }
    else
    {
        return false;
    }
    
    if (!ShadowVertexShader || !ShadowPixelShader)
    {
        return false;
    }
    
    // Create shadow sampler with clamp to edge (border not always supported)
    MonsterRender::RHI::SamplerDesc ShadowSamplerDesc;
    ShadowSamplerDesc.filter = MonsterRender::RHI::ESamplerFilter::Bilinear;
    ShadowSamplerDesc.addressU = MonsterRender::RHI::ESamplerAddressMode::Clamp;
    ShadowSamplerDesc.addressV = MonsterRender::RHI::ESamplerAddressMode::Clamp;
    ShadowSamplerDesc.addressW = MonsterRender::RHI::ESamplerAddressMode::Clamp;
    ShadowSamplerDesc.debugName = "FloorProxy Shadow Sampler";
    ShadowSampler = Device->createSampler(ShadowSamplerDesc);
    
    MR_LOG(LogFloorSceneProxy, Log, "Shadow shaders created successfully");
    return true;
}

bool FFloorSceneProxy::CreateShadowPipelineState()
{
    MR_LOG(LogFloorSceneProxy, Log, "Creating shadow pipeline state...");
    
    MonsterRender::RHI::PipelineStateDesc PipelineDesc;
    PipelineDesc.vertexShader = ShadowVertexShader;
    PipelineDesc.pixelShader = ShadowPixelShader;
    PipelineDesc.primitiveTopology = MonsterRender::RHI::EPrimitiveTopology::TriangleList;
    PipelineDesc.depthStencilState.depthEnable = true;
    PipelineDesc.depthStencilState.depthWriteEnable = true;
    PipelineDesc.depthStencilState.depthFunc = MonsterRender::RHI::EComparisonFunc::Less;
    PipelineDesc.rasterizerState.cullMode = MonsterRender::RHI::ECullMode::Back;
    PipelineDesc.rasterizerState.frontCounterClockwise = true;
    PipelineDesc.rasterizerState.fillMode = MonsterRender::RHI::EFillMode::Solid;
    PipelineDesc.debugName = "FloorProxy Shadow Pipeline State";
    
    // Same vertex layout: Position(3) + Normal(3) + TexCoord(2) = 32 bytes stride
    PipelineDesc.vertexLayout.stride = 32;
    
    MonsterRender::RHI::VertexAttribute positionAttr;
    positionAttr.location = 0;
    positionAttr.format = MonsterRender::RHI::EVertexFormat::Float3;
    positionAttr.offset = 0;
    positionAttr.semanticName = "POSITION";
    PipelineDesc.vertexLayout.attributes.Add(positionAttr);
    
    MonsterRender::RHI::VertexAttribute normalAttr;
    normalAttr.location = 1;
    normalAttr.format = MonsterRender::RHI::EVertexFormat::Float3;
    normalAttr.offset = 12;
    normalAttr.semanticName = "NORMAL";
    PipelineDesc.vertexLayout.attributes.Add(normalAttr);
    
    MonsterRender::RHI::VertexAttribute texCoordAttr;
    texCoordAttr.location = 2;
    texCoordAttr.format = MonsterRender::RHI::EVertexFormat::Float2;
    texCoordAttr.offset = 24;
    texCoordAttr.semanticName = "TEXCOORD";
    PipelineDesc.vertexLayout.attributes.Add(texCoordAttr);
    
    ShadowPipelineState = Device->createPipelineState(PipelineDesc);
    if (!ShadowPipelineState)
    {
        return false;
    }
    
    MR_LOG(LogFloorSceneProxy, Log, "Shadow pipeline state created successfully");
    return true;
}

// ============================================================================
// Uniform Buffer Updates
// ============================================================================

void FFloorSceneProxy::UpdateTransformBuffer(
    const FMatrix& ViewMatrix,
    const FMatrix& ProjectionMatrix,
    const FVector& CameraPosition)
{
    if (!TransformUniformBuffer)
    {
        return;
    }
    
    FFloorUniformBuffer UBOData;
    
    // Get model matrix from local to world transform
    FMatrix ModelMatrix = GetLocalToWorld();
    
    // Calculate normal matrix (inverse transpose of model matrix)
    FMatrix NormalMatrix = ModelMatrix.GetTransposed().Inverse();
    
    // Convert matrices to float arrays (column-major for GPU)
    MatrixToFloatArray(ModelMatrix, UBOData.Model);
    MatrixToFloatArray(ViewMatrix, UBOData.View);
    MatrixToFloatArray(ProjectionMatrix, UBOData.Projection);
    MatrixToFloatArray(NormalMatrix, UBOData.NormalMatrix);
    
    // Camera position
    UBOData.CameraPosition[0] = static_cast<float>(CameraPosition.X);
    UBOData.CameraPosition[1] = static_cast<float>(CameraPosition.Y);
    UBOData.CameraPosition[2] = static_cast<float>(CameraPosition.Z);
    UBOData.CameraPosition[3] = 1.0f;
    
    // Upload to GPU
    void* MappedData = TransformUniformBuffer->map();
    if (MappedData)
    {
        std::memcpy(MappedData, &UBOData, sizeof(UBOData));
        TransformUniformBuffer->unmap();
    }
}

void FFloorSceneProxy::UpdateLightBuffer(const TArray<FLightSceneInfo*>& Lights)
{
    if (!LightUniformBuffer)
    {
        return;
    }
    
    FFloorLightUniformBuffer LightUBO;
    std::memset(&LightUBO, 0, sizeof(LightUBO));
    
    // Ambient color
    LightUBO.AmbientColor[0] = 0.1f;
    LightUBO.AmbientColor[1] = 0.1f;
    LightUBO.AmbientColor[2] = 0.15f;
    LightUBO.AmbientColor[3] = 1.0f;
    
    // Process lights (max 8)
    int32 LightCount = FMath::Min(Lights.Num(), 8);
    for (int32 i = 0; i < LightCount; ++i)
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
    
    LightUBO.NumLights = LightCount;
    
    // Upload to GPU
    void* MappedData = LightUniformBuffer->map();
    if (MappedData)
    {
        std::memcpy(MappedData, &LightUBO, sizeof(LightUBO));
        LightUniformBuffer->unmap();
    }
}

void FFloorSceneProxy::UpdateShadowBuffer(
    const FMatrix& LightViewProjection,
    const FVector4& ShadowParams,
    uint32 ShadowMapWidth,
    uint32 ShadowMapHeight)
{
    if (!ShadowUniformBuffer)
    {
        return;
    }
    
    FFloorShadowUniformBuffer ShadowUBO;
    
    // Light view projection matrix
    MatrixToFloatArray(LightViewProjection, ShadowUBO.LightViewProjection);
    
    // Shadow parameters
    ShadowUBO.ShadowParams[0] = static_cast<float>(ShadowParams.X);  // bias
    ShadowUBO.ShadowParams[1] = static_cast<float>(ShadowParams.Y);  // slope bias
    ShadowUBO.ShadowParams[2] = static_cast<float>(ShadowParams.Z);  // normal bias
    ShadowUBO.ShadowParams[3] = static_cast<float>(ShadowParams.W);  // shadow distance
    
    // Shadow map size
    ShadowUBO.ShadowMapSize[0] = static_cast<float>(ShadowMapWidth);
    ShadowUBO.ShadowMapSize[1] = static_cast<float>(ShadowMapHeight);
    ShadowUBO.ShadowMapSize[2] = 1.0f / static_cast<float>(ShadowMapWidth);
    ShadowUBO.ShadowMapSize[3] = 1.0f / static_cast<float>(ShadowMapHeight);
    
    // Upload to GPU
    void* MappedData = ShadowUniformBuffer->map();
    if (MappedData)
    {
        std::memcpy(MappedData, &ShadowUBO, sizeof(ShadowUBO));
        ShadowUniformBuffer->unmap();
    }
}

void FFloorSceneProxy::MatrixToFloatArray(const FMatrix& Matrix, float* OutArray)
{
    // Convert to column-major order for GPU
    for (int32 Col = 0; Col < 4; ++Col)
    {
        for (int32 Row = 0; Row < 4; ++Row)
        {
            OutArray[Col * 4 + Row] = static_cast<float>(Matrix.M[Row][Col]);
        }
    }
}

} // namespace MonsterEngine
