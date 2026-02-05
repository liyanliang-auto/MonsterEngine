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
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanPendingState.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/RHIResources.h"
#include "RHI/IRHIResource.h"
#include "RHI/IRHIDescriptorSet.h"
#include "Platform/Vulkan/VulkanDescriptorSetLayout.h"
#include "Platform/Vulkan/VulkanDescriptorSet.h"
#include "Containers/SparseArray.h"
#include "Math/MonsterMath.h"

#include <cstring>
#include <cmath>
#include <cstdio>
#include <vector>

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
    MR_LOG(LogCubeSceneApp, Log, "=== Initializing PBR helmet rendering ===");
    
    if (!m_device)
    {
        MR_LOG(LogCubeSceneApp, Error, "Cannot initialize PBR: no RHI device");
        return false;
    }
    
    MR_LOG(LogCubeSceneApp, Log, "RHI device available, starting PBR initialization...");
    
    // Create default textures first (needed for missing PBR maps)
    if (!createDefaultTextures())
    {
        MR_LOG(LogCubeSceneApp, Warning, "Failed to create default textures");
    }
    MR_LOG(LogCubeSceneApp, Log, "Step 1: Default textures created");
    
    if (!loadHelmetModel())
    {
        MR_LOG(LogCubeSceneApp, Error, "Step 2: Failed to load helmet model");
        return false;
    }
    MR_LOG(LogCubeSceneApp, Log, "Step 2: Helmet model loaded");
    
    if (!createPBRPipeline())
    {
        MR_LOG(LogCubeSceneApp, Error, "Step 3: Failed to create PBR pipeline");
        return false;
    }
    MR_LOG(LogCubeSceneApp, Log, "Step 3: PBR pipeline created");
    
    if (!createHelmetTextures())
    {
        MR_LOG(LogCubeSceneApp, Warning, "Step 4: Failed to create helmet textures, using defaults");
    }
    else
    {
        MR_LOG(LogCubeSceneApp, Log, "Step 4: Helmet textures created");
    }
    
    if (!createHelmetBuffers())
    {
        MR_LOG(LogCubeSceneApp, Error, "Step 5: Failed to create helmet buffers");
        return false;
    }
    MR_LOG(LogCubeSceneApp, Log, "Step 5: Helmet buffers created");
    
    if (!createPBRUniformBuffers())
    {
        MR_LOG(LogCubeSceneApp, Error, "Step 6: Failed to create PBR uniform buffers");
        return false;
    }
    MR_LOG(LogCubeSceneApp, Log, "Step 6: PBR uniform buffers created");
    
    if (!createPBRDescriptorSets())
    {
        MR_LOG(LogCubeSceneApp, Error, "Step 7: Failed to create PBR descriptor sets");
        return false;
    }
    MR_LOG(LogCubeSceneApp, Log, "Step 7: PBR descriptor sets created");
    
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
        
        MR_LOG(LogCubeSceneApp, Log, "PBR Pipeline: renderTargetFormats.size() = %d", 
               pipelineDesc.renderTargetFormats.Num());
        MR_LOG(LogCubeSceneApp, Log, "PBR Pipeline: swapchain format = %d", 
               static_cast<int>(m_device->getSwapChainFormat()));
        
        m_pbrPipelineState = m_device->createPipelineState(pipelineDesc);
        if (!m_pbrPipelineState)
        {
            MR_LOG(LogCubeSceneApp, Error, "Failed to create PBR pipeline state");
            return false;
        }
        MR_LOG(LogCubeSceneApp, Log, "PBR Vulkan pipeline created");
    }
    else if (backend == RHI::ERHIBackend::OpenGL)
    {
        // OpenGL path: load GLSL shaders
        if (!createOpenGLPBRProgram())
        {
            MR_LOG(LogCubeSceneApp, Error, "Failed to create OpenGL PBR program");
            return false;
        }
        MR_LOG(LogCubeSceneApp, Log, "PBR OpenGL pipeline created");
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
        
        // Calculate mip levels: floor(log2(max(width, height))) + 1
        uint32 maxDim = std::max(img.Width, img.Height);
        uint32 mipLevels = static_cast<uint32>(std::floor(std::log2(maxDim))) + 1;
        
        RHI::TextureDesc desc;
        desc.width = img.Width;
        desc.height = img.Height;
        desc.depth = 1;
        desc.mipLevels = mipLevels;
        desc.arraySize = 1;
        desc.format = RHI::EPixelFormat::R8G8B8A8_UNORM;
        desc.usage = RHI::EResourceUsage::ShaderResource | RHI::EResourceUsage::TransferDst | RHI::EResourceUsage::TransferSrc;
        desc.initialData = img.Data.GetData();
        desc.initialDataSize = img.Data.Num();
        desc.debugName = name;
        
        MR_LOG(LogCubeSceneApp, Log, "Creating texture '%s' (%ux%u) with %u mip levels", 
               name, img.Width, img.Height, mipLevels);
        
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

bool CubeSceneApplication::createPBRDescriptorSets()
{
    MR_LOG(LogCubeSceneApp, Log, "Creating PBR descriptor sets...");
    
    RHI::ERHIBackend backend = m_device->getRHIBackend();
    
    if (backend == RHI::ERHIBackend::Vulkan)
    {
        // ====================================================================
        // Set 0: Per-Frame (View + Light UBOs)
        // ====================================================================
        {
            RHI::FDescriptorSetLayoutDesc set0Desc;
            set0Desc.setIndex = 0;
            
            // Binding 0: ViewUniformBuffer
            RHI::FDescriptorSetLayoutBinding viewBinding;
            viewBinding.binding = 0;
            viewBinding.descriptorType = RHI::EDescriptorType::UniformBuffer;
            viewBinding.descriptorCount = 1;
            viewBinding.shaderStages = RHI::EShaderStage::Vertex | RHI::EShaderStage::Fragment;
            set0Desc.bindings.push_back(viewBinding);
            
            // Binding 1: LightUniformBuffer
            RHI::FDescriptorSetLayoutBinding lightBinding;
            lightBinding.binding = 1;
            lightBinding.descriptorType = RHI::EDescriptorType::UniformBuffer;
            lightBinding.descriptorCount = 1;
            lightBinding.shaderStages = RHI::EShaderStage::Fragment;
            set0Desc.bindings.push_back(lightBinding);
            
            m_pbrSet0Layout = m_device->createDescriptorSetLayout(set0Desc);
            if (!m_pbrSet0Layout)
            {
                MR_LOG(LogCubeSceneApp, Error, "Failed to create PBR Set 0 layout");
                return false;
            }
        }
        
        // ====================================================================
        // Set 1: Per-Material (Material UBO + 5 Textures)
        // ====================================================================
        {
            RHI::FDescriptorSetLayoutDesc set1Desc;
            set1Desc.setIndex = 1;
            
            // Binding 0: MaterialUniformBuffer
            RHI::FDescriptorSetLayoutBinding matBinding;
            matBinding.binding = 0;
            matBinding.descriptorType = RHI::EDescriptorType::UniformBuffer;
            matBinding.descriptorCount = 1;
            matBinding.shaderStages = RHI::EShaderStage::Fragment;
            set1Desc.bindings.push_back(matBinding);
            
            // Bindings 1-5: Texture samplers
            for (uint32 i = 1; i <= 5; ++i)
            {
                RHI::FDescriptorSetLayoutBinding texBinding;
                texBinding.binding = i;
                texBinding.descriptorType = RHI::EDescriptorType::CombinedTextureSampler;
                texBinding.descriptorCount = 1;
                texBinding.shaderStages = RHI::EShaderStage::Fragment;
                set1Desc.bindings.push_back(texBinding);
            }
            
            m_pbrSet1Layout = m_device->createDescriptorSetLayout(set1Desc);
            if (!m_pbrSet1Layout)
            {
                MR_LOG(LogCubeSceneApp, Error, "Failed to create PBR Set 1 layout");
                return false;
            }
        }
        
        // ====================================================================
        // Set 2: Per-Object (Object UBO)
        // ====================================================================
        {
            RHI::FDescriptorSetLayoutDesc set2Desc;
            set2Desc.setIndex = 2;
            
            // Binding 0: ObjectUniformBuffer
            RHI::FDescriptorSetLayoutBinding objBinding;
            objBinding.binding = 0;
            objBinding.descriptorType = RHI::EDescriptorType::UniformBuffer;
            objBinding.descriptorCount = 1;
            objBinding.shaderStages = RHI::EShaderStage::Vertex;
            set2Desc.bindings.push_back(objBinding);
            
            m_pbrSet2Layout = m_device->createDescriptorSetLayout(set2Desc);
            if (!m_pbrSet2Layout)
            {
                MR_LOG(LogCubeSceneApp, Error, "Failed to create PBR Set 2 layout");
                return false;
            }
        }
        
        // ====================================================================
        // Create Pipeline Layout
        // ====================================================================
        {
            RHI::FPipelineLayoutDesc pipelineLayoutDesc;
            pipelineLayoutDesc.setLayouts.push_back(m_pbrSet0Layout);
            pipelineLayoutDesc.setLayouts.push_back(m_pbrSet1Layout);
            pipelineLayoutDesc.setLayouts.push_back(m_pbrSet2Layout);
            
            m_pbrPipelineLayout = m_device->createPipelineLayout(pipelineLayoutDesc);
            if (!m_pbrPipelineLayout)
            {
                MR_LOG(LogCubeSceneApp, Error, "Failed to create PBR pipeline layout");
                return false;
            }
        }
        
        // ====================================================================
        // Allocate Descriptor Sets
        // ====================================================================
        m_pbrPerFrameDescriptorSet = m_device->allocateDescriptorSet(m_pbrSet0Layout);
        m_pbrPerMaterialDescriptorSet = m_device->allocateDescriptorSet(m_pbrSet1Layout);
        m_pbrPerObjectDescriptorSet = m_device->allocateDescriptorSet(m_pbrSet2Layout);
        
        if (!m_pbrPerFrameDescriptorSet || !m_pbrPerMaterialDescriptorSet || !m_pbrPerObjectDescriptorSet)
        {
            MR_LOG(LogCubeSceneApp, Error, "Failed to allocate PBR descriptor sets");
            return false;
        }
        
        // ====================================================================
        // Update Descriptor Sets with resources
        // ====================================================================
        
        // Set 0: View + Light UBOs
        MR_LOG(LogCubeSceneApp, Verbose, "DEBUG: Starting Set 0 descriptor updates");
        if (m_pbrPerFrameDescriptorSet)
        {
            MR_LOG(LogCubeSceneApp, Verbose, "DEBUG: Updating Set 0 - View UBO (binding 0)");
            if (!m_pbrViewUniformBuffer) {
                MR_LOG(LogCubeSceneApp, Error, "ERROR: View uniform buffer is null!");
            }
            m_pbrPerFrameDescriptorSet->updateUniformBuffer(0, m_pbrViewUniformBuffer, 0, sizeof(FPBRViewUniforms));
            
            MR_LOG(LogCubeSceneApp, Verbose, "DEBUG: Updating Set 0 - Light UBO (binding 1)");
            if (!m_pbrLightUniformBuffer) {
                MR_LOG(LogCubeSceneApp, Error, "ERROR: Light uniform buffer is null!");
            }
            m_pbrPerFrameDescriptorSet->updateUniformBuffer(1, m_pbrLightUniformBuffer, 0, sizeof(FPBRLightUniforms));
            
            MR_LOG(LogCubeSceneApp, Verbose, "DEBUG: Set 0 descriptor updates complete");
        }
        else
        {
            MR_LOG(LogCubeSceneApp, Error, "ERROR: m_pbrPerFrameDescriptorSet is null!");
        }
        
        // Set 1: Material UBO + Textures
        if (m_pbrPerMaterialDescriptorSet)
        {
            m_pbrPerMaterialDescriptorSet->updateUniformBuffer(0, m_pbrMaterialUniformBuffer, 0, sizeof(FPBRMaterialUniforms));
            
            // Bind textures with fallback to default textures
            auto bindTextureWithDefault = [this](uint32 binding, 
                TSharedPtr<RHI::IRHITexture> tex, 
                TSharedPtr<RHI::IRHITexture> defaultTex) 
            {
                TSharedPtr<RHI::IRHITexture> texToUse = tex ? tex : defaultTex;
                if (texToUse && m_pbrSampler)
                {
                    m_pbrPerMaterialDescriptorSet->updateCombinedTextureSampler(binding, texToUse, m_pbrSampler);
                }
            };
            
            // Binding 1: BaseColor - use white if missing
            bindTextureWithDefault(1, m_helmetBaseColorTexture, m_defaultWhiteTexture);
            // Binding 2: Normal - use flat normal if missing
            bindTextureWithDefault(2, m_helmetNormalTexture, m_defaultNormalTexture);
            // Binding 3: MetallicRoughness - use white (metallic=1, roughness=1) if missing
            bindTextureWithDefault(3, m_helmetMetallicRoughnessTexture, m_defaultWhiteTexture);
            // Binding 4: Occlusion - use white (no occlusion) if missing
            bindTextureWithDefault(4, m_helmetOcclusionTexture, m_defaultWhiteTexture);
            // Binding 5: Emissive - use black (no emission) if missing
            bindTextureWithDefault(5, m_helmetEmissiveTexture, m_defaultBlackTexture);
        }
        
        // Set 2: Object UBO
        if (m_pbrPerObjectDescriptorSet)
        {
            m_pbrPerObjectDescriptorSet->updateUniformBuffer(0, m_pbrObjectUniformBuffer, 0, sizeof(FPBRObjectUniforms));
        }
        
        MR_LOG(LogCubeSceneApp, Log, "PBR descriptor sets created successfully");
    }
    else if (backend == RHI::ERHIBackend::OpenGL)
    {
        // OpenGL uses uniform locations directly, no descriptor sets needed
        // Uniform binding will be done in renderHelmetWithPBR()
        MR_LOG(LogCubeSceneApp, Log, "OpenGL backend: descriptor sets not required");
    }
    
    return true;
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
    // DamagedHelmet model faces +Z by default, rotate to face camera (-Z direction)
    constexpr double PI = 3.14159265358979323846;
    
    // X-axis rotation (45 degrees tilt)
    double xAngleRad = -90.0 * (PI / 180.0);
    double cosX = std::cos(xAngleRad), sinX = std::sin(xAngleRad);
    FMatrix rotX = FMatrix::Identity;
    rotX.M[1][1] = cosX; rotX.M[1][2] = -sinX;
    rotX.M[2][1] = sinX; rotX.M[2][2] = cosX;
    
    // Y-axis rotation (90 degrees to face camera + animation)
    double yAngleRad = (-20.0 + m_helmetRotationAngle) * (PI / 180.0);
    double cosY = std::cos(yAngleRad), sinY = std::sin(yAngleRad);
    FMatrix rotY = FMatrix::Identity;
    rotY.M[0][0] = cosY; rotY.M[0][2] = sinY;
    rotY.M[2][0] = -sinY; rotY.M[2][2] = cosY;
    
    // Position helmet at center of view
    m_helmetModelMatrix = FMatrix::MakeScale(FVector(1, 1, 1)) * rotX * rotY * 
                          FMatrix::MakeTranslation(FVector(0, 0, 0));
    
    // Update view UBO
    // Note: Transpose matrices before upload because GLSL uses column-major layout
    // but our TMatrix is row-major (UE5 convention)
    if (m_pbrViewUniformBuffer)
    {
        FPBRViewUniforms view;
        view.ViewMatrix = ToMatrix44f(viewMatrix.GetTransposed());
        view.ProjectionMatrix = ToMatrix44f(projectionMatrix.GetTransposed());
        view.ViewProjectionMatrix = ToMatrix44f((viewMatrix * projectionMatrix).GetTransposed());
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
    // Note: Transpose matrices before upload for GLSL column-major layout
    if (m_pbrObjectUniformBuffer)
    {
        FPBRObjectUniforms obj;
        obj.ModelMatrix = ToMatrix44f(m_helmetModelMatrix.GetTransposed());
        FMatrix normalMat = m_helmetModelMatrix;
        normalMat.M[3][0] = normalMat.M[3][1] = normalMat.M[3][2] = 0;
        obj.NormalMatrix = ToMatrix44f(normalMat.GetTransposed());
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
    // Log entry once
    static bool bLoggedEntry = false;
    if (!bLoggedEntry)
    {
        MR_LOG(LogCubeSceneApp, Log, "renderHelmetWithPBR called: enabled=%d, initialized=%d",
               m_bHelmetPBREnabled, m_bHelmetInitialized);
        bLoggedEntry = true;
    }
    
    if (!m_bHelmetPBREnabled || !m_bHelmetInitialized)
    {
        return;
    }
    if (!cmdList || !m_helmetVertexBuffer || !m_helmetIndexBuffer)
    {
        MR_LOG(LogCubeSceneApp, Warning, "renderHelmetWithPBR: Missing resources");
        return;
    }
    
    updatePBRUniforms(viewMatrix, projectionMatrix, cameraPosition);
    
    RHI::ERHIBackend backend = m_device->getRHIBackend();
    
    if (backend == RHI::ERHIBackend::Vulkan && m_pbrPipelineState)
    {
        // NOTE: Do NOT call setRenderTargets here!
        // This function is called from within RDG MainRenderPass or traditional render pass.
        // Calling setRenderTargets would start a new render pass and break the current one.
        // The viewport and scissor are already set by the caller (RDG or traditional path).
        
        // Disable descriptor set cache for PBR rendering
        // We use pre-updated descriptor sets instead of the automatic cache system
        auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);
        auto* cmdContext = vulkanDevice->getCommandListContext();
        if (cmdContext && cmdContext->getPendingState()) {
            cmdContext->getPendingState()->setDescriptorSetCacheEnabled(false);
            MR_LOG(LogCubeSceneApp, Verbose, "DEBUG: Disabled descriptor set cache for PBR rendering");
        }
        
        // Set pipeline state
        cmdList->setPipelineState(m_pbrPipelineState);
        
        // Bind descriptor sets (Set 0, 1, 2)
        if (m_pbrPipelineLayout && m_pbrPerFrameDescriptorSet && 
            m_pbrPerMaterialDescriptorSet && m_pbrPerObjectDescriptorSet)
        {
            // Log descriptor set handles
            auto* vulkanSet0 = dynamic_cast<RHI::Vulkan::VulkanDescriptorSet*>(m_pbrPerFrameDescriptorSet.get());
            if (vulkanSet0) {
                MR_LOG(LogCubeSceneApp, Verbose, "DEBUG: Binding Set 0 with handle 0x%llx", (uint64)vulkanSet0->getHandle());
            }
            
            TArray<TSharedPtr<RHI::IRHIDescriptorSet>> descriptorSets;
            descriptorSets.push_back(m_pbrPerFrameDescriptorSet);    // Set 0
            descriptorSets.push_back(m_pbrPerMaterialDescriptorSet); // Set 1
            descriptorSets.push_back(m_pbrPerObjectDescriptorSet);   // Set 2
            
            cmdList->bindDescriptorSets(
                m_pbrPipelineLayout,
                0,  // firstSet
                TSpan<TSharedPtr<RHI::IRHIDescriptorSet>>(descriptorSets.GetData(), descriptorSets.Num())
            );
        }
        
        // Bind vertex and index buffers
        TArray<TSharedPtr<RHI::IRHIBuffer>> vbs;
        vbs.push_back(m_helmetVertexBuffer);
        cmdList->setVertexBuffers(0, TSpan<TSharedPtr<RHI::IRHIBuffer>>(vbs.GetData(), vbs.Num()));
        cmdList->setIndexBuffer(m_helmetIndexBuffer, true);
        
        // Draw indexed
        cmdList->drawIndexed(m_helmetIndexCount, 0, 0);
        
        // Re-enable descriptor set cache after PBR rendering
        auto* vulkanDevice2 = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);
        auto* cmdContext2 = vulkanDevice2->getCommandListContext();
        if (cmdContext2 && cmdContext2->getPendingState()) {
            cmdContext2->getPendingState()->setDescriptorSetCacheEnabled(true);
            MR_LOG(LogCubeSceneApp, Verbose, "DEBUG: Re-enabled descriptor set cache");
        }
        
        MR_LOG(LogCubeSceneApp, Log, "PBR helmet rendered: %u indices", m_helmetIndexCount);
    }
    else if (backend == RHI::ERHIBackend::OpenGL)
    {
        // OpenGL path: bind uniforms directly via uniform locations
        // TODO: Implement OpenGL uniform binding
        MR_LOG(LogCubeSceneApp, Verbose, "OpenGL PBR rendering not yet implemented");
    }
}

// ============================================================================
// Default Textures Creation
// ============================================================================

bool CubeSceneApplication::createDefaultTextures()
{
    MR_LOG(LogCubeSceneApp, Log, "Creating default PBR textures...");
    
    // Default white texture (1x1 RGBA white)
    {
        uint8 whitePixel[4] = { 255, 255, 255, 255 };
        
        RHI::TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arraySize = 1;
        desc.format = RHI::EPixelFormat::R8G8B8A8_UNORM;
        desc.usage = RHI::EResourceUsage::ShaderResource | RHI::EResourceUsage::TransferDst;
        desc.initialData = whitePixel;
        desc.initialDataSize = sizeof(whitePixel);
        desc.debugName = "DefaultWhiteTexture";
        
        m_defaultWhiteTexture = m_device->createTexture(desc);
        if (!m_defaultWhiteTexture)
        {
            MR_LOG(LogCubeSceneApp, Warning, "Failed to create default white texture");
        }
    }
    
    // Default normal texture (1x1 flat normal: RGB = 128, 128, 255)
    {
        uint8 normalPixel[4] = { 128, 128, 255, 255 };
        
        RHI::TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arraySize = 1;
        desc.format = RHI::EPixelFormat::R8G8B8A8_UNORM;
        desc.usage = RHI::EResourceUsage::ShaderResource | RHI::EResourceUsage::TransferDst;
        desc.initialData = normalPixel;
        desc.initialDataSize = sizeof(normalPixel);
        desc.debugName = "DefaultNormalTexture";
        
        m_defaultNormalTexture = m_device->createTexture(desc);
        if (!m_defaultNormalTexture)
        {
            MR_LOG(LogCubeSceneApp, Warning, "Failed to create default normal texture");
        }
    }
    
    // Default black texture (1x1 RGBA black)
    {
        uint8 blackPixel[4] = { 0, 0, 0, 255 };
        
        RHI::TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arraySize = 1;
        desc.format = RHI::EPixelFormat::R8G8B8A8_UNORM;
        desc.usage = RHI::EResourceUsage::ShaderResource | RHI::EResourceUsage::TransferDst;
        desc.initialData = blackPixel;
        desc.initialDataSize = sizeof(blackPixel);
        desc.debugName = "DefaultBlackTexture";
        
        m_defaultBlackTexture = m_device->createTexture(desc);
        if (!m_defaultBlackTexture)
        {
            MR_LOG(LogCubeSceneApp, Warning, "Failed to create default black texture");
        }
    }
    
    MR_LOG(LogCubeSceneApp, Log, "Default PBR textures created");
    return m_defaultWhiteTexture && m_defaultNormalTexture && m_defaultBlackTexture;
}

// ============================================================================
// OpenGL PBR Program Creation
// ============================================================================

bool CubeSceneApplication::createOpenGLPBRProgram()
{
    MR_LOG(LogCubeSceneApp, Log, "Creating OpenGL PBR shader program...");
    
    RHI::ERHIBackend backend = m_device->getRHIBackend();
    if (backend != RHI::ERHIBackend::OpenGL)
    {
        MR_LOG(LogCubeSceneApp, Log, "Not OpenGL backend, skipping GL program creation");
        return true;
    }
    
#if PLATFORM_WINDOWS || PLATFORM_ANDROID
    // Read GLSL shader source files
    String vertexPath = "Shaders/PBR/PBR_GL.vert";
    String fragmentPath = "Shaders/PBR/PBR_GL.frag";
    
    std::vector<uint8> vertexSourceVec = ShaderCompiler::readFileBytes(vertexPath);
    std::vector<uint8> fragmentSourceVec = ShaderCompiler::readFileBytes(fragmentPath);
    
    if (vertexSourceVec.empty() || fragmentSourceVec.empty())
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to read OpenGL PBR shader files");
        return false;
    }
    
    // Convert to TArray and null-terminate the source strings
    TArray<uint8> vertexSource;
    TArray<uint8> fragmentSource;
    vertexSource.Reserve(static_cast<int32>(vertexSourceVec.size() + 1));
    fragmentSource.Reserve(static_cast<int32>(fragmentSourceVec.size() + 1));
    
    for (uint8 byte : vertexSourceVec) vertexSource.push_back(byte);
    for (uint8 byte : fragmentSourceVec) fragmentSource.push_back(byte);
    vertexSource.push_back(0);
    fragmentSource.push_back(0);
    
    // Create shaders using device interface
    TSpan<const uint8> vertSpan(vertexSource.GetData(), vertexSource.Num() - 1);
    TSpan<const uint8> fragSpan(fragmentSource.GetData(), fragmentSource.Num() - 1);
    
    auto vertexShader = m_device->createVertexShader(vertSpan);
    auto fragmentShader = m_device->createPixelShader(fragSpan);
    
    if (!vertexShader || !fragmentShader)
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to compile OpenGL PBR shaders");
        return false;
    }
    
    // Store shaders for later use
    m_pbrVertexShader = vertexShader;
    m_pbrFragmentShader = fragmentShader;
    
    // Create pipeline state for OpenGL
    RHI::PipelineStateDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.pixelShader = fragmentShader;
    pipelineDesc.primitiveTopology = RHI::EPrimitiveTopology::TriangleList;
    
    // Vertex layout (same as Vulkan)
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
    pipelineDesc.debugName = "PBR Helmet Pipeline (OpenGL)";
    
    m_pbrPipelineState = m_device->createPipelineState(pipelineDesc);
    if (!m_pbrPipelineState)
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to create OpenGL PBR pipeline state");
        return false;
    }
    
    MR_LOG(LogCubeSceneApp, Log, "OpenGL PBR shader program created successfully");
#endif
    
    return true;
}

void CubeSceneApplication::_setOpenGLPBRUniforms(
    const FMatrix& viewMatrix,
    const FMatrix& projectionMatrix,
    const FVector& cameraPosition)
{
    // OpenGL uniform setting is handled through the pipeline state
    // The actual uniform values are set via the uniform buffers
    // which are mapped and updated in updatePBRUniforms()
    
    // For OpenGL, we need to bind textures to texture units
    // This is done through the command list interface
    
    MR_LOG(LogCubeSceneApp, Verbose, "OpenGL PBR uniforms set");
}

// ============================================================================
// PBR Helmet Shutdown
// ============================================================================

void CubeSceneApplication::shutdownHelmetPBR()
{
    MR_LOG(LogCubeSceneApp, Log, "Shutting down PBR helmet resources...");
    
    // Release descriptor sets first
    m_pbrPerFrameDescriptorSet.Reset();
    m_pbrPerMaterialDescriptorSet.Reset();
    m_pbrPerObjectDescriptorSet.Reset();
    
    // Release pipeline layout and descriptor set layouts
    m_pbrPipelineLayout.Reset();
    m_pbrSet0Layout.Reset();
    m_pbrSet1Layout.Reset();
    m_pbrSet2Layout.Reset();
    
    // Release uniform buffers
    m_pbrViewUniformBuffer.Reset();
    m_pbrLightUniformBuffer.Reset();
    m_pbrMaterialUniformBuffer.Reset();
    m_pbrObjectUniformBuffer.Reset();
    
    // Release textures
    m_helmetBaseColorTexture.Reset();
    m_helmetNormalTexture.Reset();
    m_helmetMetallicRoughnessTexture.Reset();
    m_helmetOcclusionTexture.Reset();
    m_helmetEmissiveTexture.Reset();
    m_pbrSampler.Reset();
    
    // Release default textures
    m_defaultWhiteTexture.Reset();
    m_defaultNormalTexture.Reset();
    m_defaultBlackTexture.Reset();
    
    // Release buffers
    m_helmetVertexBuffer.Reset();
    m_helmetIndexBuffer.Reset();
    
    // Release pipeline and shaders
    m_pbrPipelineState.Reset();
    m_pbrVertexShader.Reset();
    m_pbrFragmentShader.Reset();
    
    // Release model
    m_helmetModel.Reset();
    
    m_bHelmetInitialized = false;
    m_helmetIndexCount = 0;
    m_helmetVertexCount = 0;
    
    MR_LOG(LogCubeSceneApp, Log, "PBR helmet resources released");
}

} // namespace MonsterRender
