// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CubeSceneApplicationPBR.cpp
 * @brief PBR helmet rendering implementation for CubeSceneApplication
 * 
 * This file contains the implementation of PBR (Physically Based Rendering)
 * for the DamagedHelmet glTF model.
 * 
 * Reference: Google Filament gltf_viewer, UE5 BasePass
 */

#include "CubeSceneApplication.h"
#include "Engine/Asset/GLTFLoader.h"
#include "Engine/Asset/GLTFTypes.h"
#include "Engine/LightSceneInfo.h"
#include "Engine/LightSceneProxy.h"
#include "Engine/Scene.h"
#include "Core/Logging/LogMacros.h"
#include "Core/ShaderCompiler.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/RHIResources.h"
#include "RHI/IRHIResource.h"
#include "RHI/IRHIDescriptorSet.h"
#include "Containers/SparseArray.h"
#include "Math/MonsterMath.h"

#include <cstring>
#include <cmath>
#include <cstdio>

namespace MonsterRender
{

using namespace MonsterEngine;
using namespace MonsterEngine::Math;

using MonsterRender::LogCubeSceneApp;

// ============================================================================
// PBR Uniform Buffer Structures (must match shader layout)
// ============================================================================

struct FPBRViewUniforms
{
    FMatrix44f ViewMatrix;
    FMatrix44f ProjectionMatrix;
    FMatrix44f ViewProjectionMatrix;
    FMatrix44f InvViewMatrix;
    FMatrix44f InvProjectionMatrix;
    FMatrix44f InvViewProjectionMatrix;
    FVector4f CameraPosition;
    FVector4f CameraDirection;
    FVector4f ViewportSize;
    FVector4f TimeParams;
};

struct FPBRLightData
{
    FVector4f Position;
    FVector4f Color;
    FVector4f Direction;
    FVector4f Attenuation;
};

struct FPBRLightUniforms
{
    static constexpr int32 MAX_LIGHTS = 8;
    FPBRLightData Lights[MAX_LIGHTS];
    int32 NumLights;
    float AmbientIntensity;
    float Padding[2];
};

struct FPBRMaterialUniforms
{
    FVector4f BaseColorFactor;
    FVector4f EmissiveFactor;
    float MetallicFactor;
    float RoughnessFactor;
    float ReflectanceFactor;
    float AmbientOcclusion;
    float AlphaCutoff;
    float ClearCoat;
    float ClearCoatRoughness;
    float Padding;
};

struct FPBRObjectUniforms
{
    FMatrix44f ModelMatrix;
    FMatrix44f NormalMatrix;
    FVector4f ObjectBoundsMin;
    FVector4f ObjectBoundsMax;
};

struct FPBRVertex
{
    FVector3f Position;
    FVector3f Normal;
    FVector4f Tangent;
    FVector2f TexCoord0;
    FVector2f TexCoord1;
    FVector4f Color;
};

static constexpr int32 LIGHT_TYPE_DIRECTIONAL = 0;
static constexpr int32 LIGHT_TYPE_POINT = 1;

// ============================================================================
// Helper Functions
// ============================================================================

static FMatrix44f ToMatrix44f(const FMatrix& m)
{
    FMatrix44f result;
    for (int32 i = 0; i < 4; ++i)
    {
        for (int32 j = 0; j < 4; ++j)
        {
            result.M[i][j] = static_cast<float>(m.M[i][j]);
        }
    }
    return result;
}

static bool LoadShaderBytecode(const String& path, TArray<uint8>& outBytecode)
{
    std::vector<uint8> data = ShaderCompiler::readFileBytes(path);
    if (data.empty()) return false;
    outBytecode.SetNum(static_cast<int32>(data.size()));
    std::memcpy(outBytecode.GetData(), data.data(), data.size());
    return true;
}

// ============================================================================
// PBR Helmet Initialization
// ============================================================================

bool CubeSceneApplication::initializeHelmetPBR()
{
    printf("[PBR] initializeHelmetPBR() called\n");
    MR_LOG(LogCubeSceneApp, Log, "=== Initializing PBR helmet rendering ===");
    
    if (!m_device)
    {
        printf("[PBR] ERROR: No RHI device\n");
        MR_LOG(LogCubeSceneApp, Error, "Cannot initialize PBR: no RHI device");
        return false;
    }
    
    printf("[PBR] Device available, loading model...\n");
    
    if (!loadHelmetModel()) return false;
    if (!createPBRPipeline()) return false;
    if (!createHelmetTextures()) { /* Continue without textures */ }
    if (!createHelmetBuffers()) return false;
    if (!createPBRUniformBuffers()) return false;
    
    m_helmetModelMatrix = FMatrix::Identity;
    m_helmetRotationAngle = 0.0f;
    m_bHelmetInitialized = true;
    
    MR_LOG(LogCubeSceneApp, Log, "PBR helmet rendering initialized successfully");
    return true;
}

bool CubeSceneApplication::loadHelmetModel()
{
    MR_LOG(LogCubeSceneApp, Log, "Loading DamagedHelmet glTF model...");
    
    String modelPath = "resources/models/DamagedHelmet/DamagedHelmet.gltf";
    FGLTFLoader loader;
    FGLTFLoadOptions options;
    options.bLoadTextures = true;
    options.bGenerateTangents = true;
    options.bGenerateNormals = true;
    options.bComputeBounds = true;
    
    m_helmetModel = loader.LoadFromFile(modelPath, options);
    
    if (!m_helmetModel || !m_helmetModel->IsValid())
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to load glTF model: %s", modelPath.c_str());
        return false;
    }
    
    MR_LOG(LogCubeSceneApp, Log, "Loaded glTF: Meshes=%d, Materials=%d, Textures=%d",
           m_helmetModel->GetMeshCount(), m_helmetModel->GetMaterialCount(), 
           m_helmetModel->GetTextureCount());
    return true;
}

bool CubeSceneApplication::createPBRPipeline()
{
    MR_LOG(LogCubeSceneApp, Log, "Creating PBR pipeline...");
    
    RHI::ERHIBackend backend = m_device->getRHIBackend();
    
    if (backend == RHI::ERHIBackend::Vulkan)
    {
        TArray<uint8> vertexShaderCode, fragmentShaderCode;
        if (!LoadShaderBytecode("Shaders/PBR/PBR.vert.spv", vertexShaderCode) ||
            !LoadShaderBytecode("Shaders/PBR/PBR.frag.spv", fragmentShaderCode))
        {
            MR_LOG(LogCubeSceneApp, Error, "Failed to load PBR shaders");
            return false;
        }
        
        TSpan<const uint8> vertexSpan(vertexShaderCode.GetData(), vertexShaderCode.Num());
        TSpan<const uint8> fragmentSpan(fragmentShaderCode.GetData(), fragmentShaderCode.Num());
        
        auto vertexShader = m_device->createVertexShader(vertexSpan);
        auto fragmentShader = m_device->createPixelShader(fragmentSpan);
        
        if (!vertexShader || !fragmentShader)
        {
            MR_LOG(LogCubeSceneApp, Error, "Failed to create PBR shaders");
            return false;
        }
        
        RHI::PipelineStateDesc pipelineDesc;
        pipelineDesc.vertexShader = vertexShader;
        pipelineDesc.pixelShader = fragmentShader;
        pipelineDesc.primitiveTopology = RHI::EPrimitiveTopology::TriangleList;
        
        // Vertex layout
        RHI::VertexAttribute attr;
        attr.location = 0; attr.format = RHI::EVertexFormat::Float3; 
        attr.offset = offsetof(FPBRVertex, Position);
        pipelineDesc.vertexLayout.attributes.push_back(attr);
        
        attr.location = 1; attr.format = RHI::EVertexFormat::Float3; 
        attr.offset = offsetof(FPBRVertex, Normal);
        pipelineDesc.vertexLayout.attributes.push_back(attr);
        
        attr.location = 2; attr.format = RHI::EVertexFormat::Float4; 
        attr.offset = offsetof(FPBRVertex, Tangent);
        pipelineDesc.vertexLayout.attributes.push_back(attr);
        
        attr.location = 3; attr.format = RHI::EVertexFormat::Float2; 
        attr.offset = offsetof(FPBRVertex, TexCoord0);
        pipelineDesc.vertexLayout.attributes.push_back(attr);
        
        attr.location = 4; attr.format = RHI::EVertexFormat::Float2; 
        attr.offset = offsetof(FPBRVertex, TexCoord1);
        pipelineDesc.vertexLayout.attributes.push_back(attr);
        
        attr.location = 5; attr.format = RHI::EVertexFormat::Float4; 
        attr.offset = offsetof(FPBRVertex, Color);
        pipelineDesc.vertexLayout.attributes.push_back(attr);
        
        pipelineDesc.vertexLayout.stride = sizeof(FPBRVertex);
        pipelineDesc.rasterizerState.fillMode = RHI::EFillMode::Solid;
        pipelineDesc.rasterizerState.cullMode = RHI::ECullMode::Back;
        pipelineDesc.rasterizerState.frontCounterClockwise = false;
        pipelineDesc.depthStencilState.depthEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::ECompareOp::Less;
        pipelineDesc.blendState.blendEnable = false;
        pipelineDesc.renderTargetFormats.push_back(m_device->getSwapChainFormat());
        pipelineDesc.depthStencilFormat = m_device->getDepthFormat();
        pipelineDesc.debugName = "PBR Helmet Pipeline";
        
        m_pbrPipelineState = m_device->createPipelineState(pipelineDesc);
        if (!m_pbrPipelineState)
        {
            MR_LOG(LogCubeSceneApp, Error, "Failed to create PBR pipeline state");
            return false;
        }
        MR_LOG(LogCubeSceneApp, Log, "PBR Vulkan pipeline created");
    }
    
    // Create sampler
    RHI::SamplerDesc samplerDesc;
    samplerDesc.filter = RHI::ESamplerFilter::Trilinear;
    samplerDesc.addressU = RHI::ESamplerAddressMode::Wrap;
    samplerDesc.addressV = RHI::ESamplerAddressMode::Wrap;
    samplerDesc.addressW = RHI::ESamplerAddressMode::Wrap;
    samplerDesc.maxAnisotropy = 16;
    m_pbrSampler = m_device->createSampler(samplerDesc);
    
    return true;
}

bool CubeSceneApplication::createHelmetTextures()
{
    MR_LOG(LogCubeSceneApp, Log, "Creating helmet textures...");
    
    if (!m_helmetModel || m_helmetModel->Images.Num() == 0)
    {
        MR_LOG(LogCubeSceneApp, Warning, "No images in helmet model");
        return true;
    }
    
    auto createTex = [this](const FGLTFImage& img, const char* name) -> TSharedPtr<RHI::IRHITexture>
    {
        if (!img.bIsLoaded || img.Data.Num() == 0) return nullptr;
        
        RHI::TextureDesc desc;
        desc.width = img.Width;
        desc.height = img.Height;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arraySize = 1;
        desc.format = RHI::EPixelFormat::R8G8B8A8_UNORM;
        desc.usage = RHI::EResourceUsage::ShaderResource | RHI::EResourceUsage::TransferDst;
        desc.initialData = img.Data.GetData();
        desc.initialDataSize = img.Data.Num();
        desc.debugName = name;
        return m_device->createTexture(desc);
    };
    
    if (m_helmetModel->Materials.Num() > 0)
    {
        const FGLTFMaterial& mat = m_helmetModel->Materials[0];
        
        auto getImage = [&](const FGLTFTextureInfo& texInfo) -> const FGLTFImage*
        {
            if (!texInfo.IsValid()) return nullptr;
            int32 texIdx = texInfo.TextureIndex;
            if (texIdx < 0 || texIdx >= m_helmetModel->Textures.Num()) return nullptr;
            int32 imgIdx = m_helmetModel->Textures[texIdx].ImageIndex;
            if (imgIdx < 0 || imgIdx >= m_helmetModel->Images.Num()) return nullptr;
            return &m_helmetModel->Images[imgIdx];
        };
        
        if (auto* img = getImage(mat.BaseColorTexture))
            m_helmetBaseColorTexture = createTex(*img, "Helmet_BaseColor");
        if (auto* img = getImage(mat.NormalTexture))
            m_helmetNormalTexture = createTex(*img, "Helmet_Normal");
        if (auto* img = getImage(mat.MetallicRoughnessTexture))
            m_helmetMetallicRoughnessTexture = createTex(*img, "Helmet_MR");
        if (auto* img = getImage(mat.OcclusionTexture))
            m_helmetOcclusionTexture = createTex(*img, "Helmet_AO");
        if (auto* img = getImage(mat.EmissiveTexture))
            m_helmetEmissiveTexture = createTex(*img, "Helmet_Emissive");
    }
    
    MR_LOG(LogCubeSceneApp, Log, "Helmet textures created");
    return true;
}

bool CubeSceneApplication::createHelmetBuffers()
{
    MR_LOG(LogCubeSceneApp, Log, "Creating helmet buffers...");
    
    if (!m_helmetModel || m_helmetModel->Meshes.Num() == 0) return false;
    
    const FGLTFMesh& mesh = m_helmetModel->Meshes[0];
    if (mesh.Primitives.Num() == 0) return false;
    
    const FGLTFPrimitive& prim = mesh.Primitives[0];
    uint32 vertexCount = prim.GetVertexCount();
    
    TArray<FPBRVertex> vertices;
    vertices.SetNum(vertexCount);
    
    for (uint32 i = 0; i < vertexCount; ++i)
    {
        FPBRVertex& v = vertices[i];
        v.Position = prim.Positions[i];
        v.Normal = (prim.HasNormals() && i < (uint32)prim.Normals.Num()) ? 
                   prim.Normals[i] : FVector3f(0, 1, 0);
        v.Tangent = (prim.HasTangents() && i < (uint32)prim.Tangents.Num()) ? 
                    prim.Tangents[i] : FVector4f(1, 0, 0, 1);
        v.TexCoord0 = (prim.HasTexCoords() && i < (uint32)prim.TexCoords0.Num()) ? 
                      prim.TexCoords0[i] : FVector2f(0, 0);
        v.TexCoord1 = (i < (uint32)prim.TexCoords1.Num()) ? 
                      prim.TexCoords1[i] : v.TexCoord0;
        v.Color = (prim.HasColors() && i < (uint32)prim.Colors.Num()) ? 
                  prim.Colors[i] : FVector4f(1, 1, 1, 1);
    }
    
    RHI::BufferDesc vbDesc;
    vbDesc.size = vertices.Num() * sizeof(FPBRVertex);
    vbDesc.usage = RHI::EResourceUsage::VertexBuffer | RHI::EResourceUsage::TransferDst;
    vbDesc.memoryUsage = RHI::EMemoryUsage::Default;
    vbDesc.initialData = vertices.GetData();
    vbDesc.initialDataSize = vbDesc.size;
    vbDesc.debugName = "Helmet_VB";
    
    m_helmetVertexBuffer = m_device->createBuffer(vbDesc);
    m_helmetVertexCount = vertexCount;
    
    uint32 indexCount = prim.GetIndexCount();
    RHI::BufferDesc ibDesc;
    ibDesc.size = indexCount * sizeof(uint32);
    ibDesc.usage = RHI::EResourceUsage::IndexBuffer | RHI::EResourceUsage::TransferDst;
    ibDesc.memoryUsage = RHI::EMemoryUsage::Default;
    ibDesc.initialData = prim.Indices.GetData();
    ibDesc.initialDataSize = ibDesc.size;
    ibDesc.debugName = "Helmet_IB";
    
    m_helmetIndexBuffer = m_device->createBuffer(ibDesc);
    m_helmetIndexCount = indexCount;
    
    MR_LOG(LogCubeSceneApp, Log, "Helmet buffers: %u verts, %u indices", 
           m_helmetVertexCount, m_helmetIndexCount);
    return m_helmetVertexBuffer && m_helmetIndexBuffer;
}

bool CubeSceneApplication::createPBRUniformBuffers()
{
    MR_LOG(LogCubeSceneApp, Log, "Creating PBR uniform buffers...");
    
    auto createUBO = [this](uint32 size, const char* name) -> TSharedPtr<RHI::IRHIBuffer>
    {
        RHI::BufferDesc desc;
        desc.size = size;
        desc.usage = RHI::EResourceUsage::UniformBuffer;
        desc.memoryUsage = RHI::EMemoryUsage::Dynamic;
        desc.cpuAccessible = true;
        desc.debugName = name;
        return m_device->createBuffer(desc);
    };
    
    m_pbrViewUniformBuffer = createUBO(sizeof(FPBRViewUniforms), "PBR_ViewUBO");
    m_pbrLightUniformBuffer = createUBO(sizeof(FPBRLightUniforms), "PBR_LightUBO");
    m_pbrMaterialUniformBuffer = createUBO(sizeof(FPBRMaterialUniforms), "PBR_MatUBO");
    m_pbrObjectUniformBuffer = createUBO(sizeof(FPBRObjectUniforms), "PBR_ObjUBO");
    
    return m_pbrViewUniformBuffer && m_pbrLightUniformBuffer && 
           m_pbrMaterialUniformBuffer && m_pbrObjectUniformBuffer;
}

// ============================================================================
// PBR Uniform Update
// ============================================================================

void CubeSceneApplication::updatePBRUniforms(
    const FMatrix& viewMatrix,
    const FMatrix& projectionMatrix,
    const FVector& cameraPosition)
{
    if (!m_bHelmetInitialized) return;
    
    // Update rotation
    m_helmetRotationAngle += m_helmetRotationSpeed * m_deltaTime;
    if (m_helmetRotationAngle > 360.0f) m_helmetRotationAngle -= 360.0f;
    
    // Build model matrix
    double angleRad = m_helmetRotationAngle * (3.14159265358979323846 / 180.0);
    double cosA = std::cos(angleRad), sinA = std::sin(angleRad);
    FMatrix rotMat = FMatrix::Identity;
    rotMat.M[0][0] = cosA; rotMat.M[0][2] = sinA;
    rotMat.M[2][0] = -sinA; rotMat.M[2][2] = cosA;
    m_helmetModelMatrix = FMatrix::MakeScale(FVector(1, 1, 1)) * rotMat * 
                          FMatrix::MakeTranslation(FVector(0, 1.5, 0));
    
    // Update view UBO
    if (m_pbrViewUniformBuffer)
    {
        FPBRViewUniforms view;
        view.ViewMatrix = ToMatrix44f(viewMatrix);
        view.ProjectionMatrix = ToMatrix44f(projectionMatrix);
        view.ViewProjectionMatrix = ToMatrix44f(viewMatrix * projectionMatrix);
        view.CameraPosition = FVector4f((float)cameraPosition.X, (float)cameraPosition.Y, 
                                        (float)cameraPosition.Z, 1.0f);
        view.ViewportSize = FVector4f((float)m_windowWidth, (float)m_windowHeight,
                                      1.0f/m_windowWidth, 1.0f/m_windowHeight);
        view.TimeParams = FVector4f(m_totalTime, std::sin(m_totalTime), 
                                    std::cos(m_totalTime), m_deltaTime);
        
        void* data = m_pbrViewUniformBuffer->map();
        if (data) { std::memcpy(data, &view, sizeof(view)); m_pbrViewUniformBuffer->unmap(); }
    }
    
    // Update light UBO
    if (m_pbrLightUniformBuffer)
    {
        FPBRLightUniforms lights;
        std::memset(&lights, 0, sizeof(lights));
        lights.AmbientIntensity = 0.03f;
        
        // Get directional lights from scene
        const TArray<FLightSceneInfo*>& dirLights = m_scene ? m_scene->GetDirectionalLights() : TArray<FLightSceneInfo*>();
        
        for (int32 i = 0; i < dirLights.Num() && lights.NumLights < FPBRLightUniforms::MAX_LIGHTS; ++i)
        {
            if (!dirLights[i] || !dirLights[i]->Proxy) continue;
            auto* proxy = dirLights[i]->Proxy;
            FPBRLightData& ld = lights.Lights[lights.NumLights++];
            
            FVector pos = proxy->GetPosition();
            FVector dir = proxy->GetDirection();
            FLinearColor col = proxy->GetColor();
            int32 type = proxy->IsDirectionalLight() ? LIGHT_TYPE_DIRECTIONAL : LIGHT_TYPE_POINT;
            
            ld.Position = FVector4f((float)(type == LIGHT_TYPE_DIRECTIONAL ? dir.X : pos.X),
                                    (float)(type == LIGHT_TYPE_DIRECTIONAL ? dir.Y : pos.Y),
                                    (float)(type == LIGHT_TYPE_DIRECTIONAL ? dir.Z : pos.Z),
                                    (float)type);
            ld.Color = FVector4f(col.R, col.G, col.B, proxy->GetIntensity());
            ld.Direction = FVector4f((float)dir.X, (float)dir.Y, (float)dir.Z, 0);
            ld.Attenuation = FVector4f(10.0f, 0.9f, 0.8f, 0.0f);  // Default attenuation
        }
        
        void* data = m_pbrLightUniformBuffer->map();
        if (data) { std::memcpy(data, &lights, sizeof(lights)); m_pbrLightUniformBuffer->unmap(); }
    }
    
    // Update material UBO
    if (m_pbrMaterialUniformBuffer)
    {
        FPBRMaterialUniforms mat;
        mat.BaseColorFactor = FVector4f(1, 1, 1, 1);
        mat.EmissiveFactor = FVector4f(1, 1, 1, 0);
        mat.MetallicFactor = 1.0f;
        mat.RoughnessFactor = 1.0f;
        mat.ReflectanceFactor = 0.5f;
        mat.AmbientOcclusion = 1.0f;
        mat.AlphaCutoff = 0.5f;
        mat.ClearCoat = 0.0f;
        mat.ClearCoatRoughness = 0.0f;
        mat.Padding = 0.0f;
        
        if (m_helmetModel && m_helmetModel->Materials.Num() > 0)
        {
            const auto& m = m_helmetModel->Materials[0];
            mat.BaseColorFactor = FVector4f(m.BaseColorFactor.X, m.BaseColorFactor.Y,
                                            m.BaseColorFactor.Z, m.BaseColorFactor.W);
            mat.MetallicFactor = m.MetallicFactor;
            mat.RoughnessFactor = m.RoughnessFactor;
        }
        
        void* data = m_pbrMaterialUniformBuffer->map();
        if (data) { std::memcpy(data, &mat, sizeof(mat)); m_pbrMaterialUniformBuffer->unmap(); }
    }
    
    // Update object UBO
    if (m_pbrObjectUniformBuffer)
    {
        FPBRObjectUniforms obj;
        obj.ModelMatrix = ToMatrix44f(m_helmetModelMatrix);
        FMatrix normalMat = m_helmetModelMatrix;
        normalMat.M[3][0] = normalMat.M[3][1] = normalMat.M[3][2] = 0;
        obj.NormalMatrix = ToMatrix44f(normalMat);
        obj.ObjectBoundsMin = FVector4f(-1, -1, -1, 0);
        obj.ObjectBoundsMax = FVector4f(1, 1, 1, 0);
        
        void* data = m_pbrObjectUniformBuffer->map();
        if (data) { std::memcpy(data, &obj, sizeof(obj)); m_pbrObjectUniformBuffer->unmap(); }
    }
}

// ============================================================================
// PBR Helmet Rendering
// ============================================================================

void CubeSceneApplication::renderHelmetWithPBR(
    RHI::IRHICommandList* cmdList,
    const FMatrix& viewMatrix,
    const FMatrix& projectionMatrix,
    const FVector& cameraPosition)
{
    if (!m_bHelmetPBREnabled || !m_bHelmetInitialized) return;
    if (!cmdList || !m_helmetVertexBuffer || !m_helmetIndexBuffer) return;
    
    updatePBRUniforms(viewMatrix, projectionMatrix, cameraPosition);
    
    RHI::ERHIBackend backend = m_device->getRHIBackend();
    
    if (backend == RHI::ERHIBackend::Vulkan && m_pbrPipelineState)
    {
        cmdList->setPipelineState(m_pbrPipelineState);
        
        TArray<TSharedPtr<RHI::IRHIBuffer>> vbs;
        vbs.push_back(m_helmetVertexBuffer);
        cmdList->setVertexBuffers(0, TSpan<TSharedPtr<RHI::IRHIBuffer>>(vbs.GetData(), vbs.Num()));
        cmdList->setIndexBuffer(m_helmetIndexBuffer, true);
        cmdList->drawIndexed(m_helmetIndexCount, 0, 0);
        
        MR_LOG(LogCubeSceneApp, Verbose, "PBR helmet rendered: %u indices", m_helmetIndexCount);
    }
}

// ============================================================================
// PBR Helmet Shutdown
// ============================================================================

void CubeSceneApplication::shutdownHelmetPBR()
{
    MR_LOG(LogCubeSceneApp, Log, "Shutting down PBR helmet resources...");
    
    m_pbrPerFrameDescriptorSet.Reset();
    m_pbrPerMaterialDescriptorSet.Reset();
    m_pbrPerObjectDescriptorSet.Reset();
    m_pbrViewUniformBuffer.Reset();
    m_pbrLightUniformBuffer.Reset();
    m_pbrMaterialUniformBuffer.Reset();
    m_pbrObjectUniformBuffer.Reset();
    m_helmetBaseColorTexture.Reset();
    m_helmetNormalTexture.Reset();
    m_helmetMetallicRoughnessTexture.Reset();
    m_helmetOcclusionTexture.Reset();
    m_helmetEmissiveTexture.Reset();
    m_pbrSampler.Reset();
    m_helmetVertexBuffer.Reset();
    m_helmetIndexBuffer.Reset();
    m_pbrPipelineState.Reset();
    m_pbrVertexShader.Reset();
    m_pbrFragmentShader.Reset();
    m_helmetModel.Reset();
    
    m_bHelmetInitialized = false;
    m_helmetIndexCount = 0;
    m_helmetVertexCount = 0;
    
    MR_LOG(LogCubeSceneApp, Log, "PBR helmet resources released");
}

} // namespace MonsterRender
