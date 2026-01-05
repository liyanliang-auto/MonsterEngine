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
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/RHIResources.h"
#include "RHI/IRHIResource.h"
#include "Containers/SparseArray.h"
#include "Math/MonsterMath.h"

#include <cstring>
#include <cmath>

namespace MonsterRender
{

using namespace MonsterEngine;
using namespace MonsterEngine::Math;

// Use global log category
using MonsterRender::LogCubeSceneApp;

// ============================================================================
// PBR Uniform Buffer Structures (must match shader layout)
// ============================================================================

// View uniform buffer (Set 0, Binding 0)
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

// Light data structure
struct FPBRLightData
{
    FVector4f Position;
    FVector4f Color;
    FVector4f Direction;
    FVector4f Attenuation;
};

// Light uniform buffer (Set 0, Binding 1)
struct FPBRLightUniforms
{
    static constexpr int32 MAX_LIGHTS = 8;
    FPBRLightData Lights[MAX_LIGHTS];
    int32 NumLights;
    float AmbientIntensity;
    float Padding[2];
};

// Material uniform buffer (Set 1, Binding 0)
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

// Object uniform buffer (Set 2, Binding 0)
struct FPBRObjectUniforms
{
    FMatrix44f ModelMatrix;
    FMatrix44f NormalMatrix;
    FVector4f ObjectBoundsMin;
    FVector4f ObjectBoundsMax;
};

// PBR Vertex Structure
struct FPBRVertex
{
    FVector3f Position;
    FVector3f Normal;
    FVector4f Tangent;
    FVector2f TexCoord0;
    FVector2f TexCoord1;
    FVector4f Color;
};

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

// ============================================================================
// PBR Helmet Initialization
// ============================================================================

bool CubeSceneApplication::initializeHelmetPBR()
{
    MR_LOG(LogCubeSceneApp, Log, "Initializing PBR helmet rendering...");
    
    if (!m_device)
    {
        MR_LOG(LogCubeSceneApp, Error, "Cannot initialize PBR: no RHI device");
        return false;
    }
    
    // Step 1: Load glTF model
    if (!loadHelmetModel())
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to load helmet model");
        return false;
    }
    
    // Step 2: Create PBR pipeline
    if (!createPBRPipeline())
    {
        MR_LOG(LogCubeSceneApp, Warning, "Failed to create PBR pipeline, helmet rendering disabled");
        return false;
    }
    
    // Step 3: Create textures from glTF
    if (!createHelmetTextures())
    {
        MR_LOG(LogCubeSceneApp, Warning, "Failed to create helmet textures");
        // Continue anyway - can render without textures
    }
    
    // Step 4: Create vertex/index buffers
    if (!createHelmetBuffers())
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to create helmet buffers");
        return false;
    }
    
    // Step 5: Create uniform buffers
    if (!createPBRUniformBuffers())
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to create PBR uniform buffers");
        return false;
    }
    
    // Initialize helmet model matrix
    m_helmetModelMatrix = FMatrix::Identity;
    m_helmetRotationAngle = 0.0f;
    
    m_bHelmetInitialized = true;
    MR_LOG(LogCubeSceneApp, Log, "PBR helmet rendering initialized successfully");
    
    return true;
}

bool CubeSceneApplication::loadHelmetModel()
{
    MR_LOG(LogCubeSceneApp, Log, "Loading DamagedHelmet glTF model...");
    
    // Path to DamagedHelmet model
    String modelPath = "resources/models/DamagedHelmet/DamagedHelmet.gltf";
    
    // Create glTF loader
    FGLTFLoader loader;
    FGLTFLoadOptions options;
    options.bLoadTextures = true;
    options.bGenerateTangents = true;
    options.bGenerateNormals = true;
    options.bComputeBounds = true;
    
    // Load model
    m_helmetModel = loader.LoadFromFile(modelPath, options);
    
    if (!m_helmetModel || !m_helmetModel->IsValid())
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to load glTF model: %s - %s",
               modelPath.c_str(), loader.GetLastErrorMessage().c_str());
        return false;
    }
    
    MR_LOG(LogCubeSceneApp, Log, "Loaded glTF model: %s", modelPath.c_str());
    MR_LOG(LogCubeSceneApp, Log, "  Meshes: %d", m_helmetModel->GetMeshCount());
    MR_LOG(LogCubeSceneApp, Log, "  Materials: %d", m_helmetModel->GetMaterialCount());
    MR_LOG(LogCubeSceneApp, Log, "  Textures: %d", m_helmetModel->GetTextureCount());
    MR_LOG(LogCubeSceneApp, Log, "  Vertices: %u", m_helmetModel->GetTotalVertexCount());
    MR_LOG(LogCubeSceneApp, Log, "  Triangles: %u", m_helmetModel->GetTotalTriangleCount());
    
    return true;
}

bool CubeSceneApplication::createPBRPipeline()
{
    MR_LOG(LogCubeSceneApp, Log, "Creating PBR pipeline...");
    
    RHI::ERHIBackend backend = m_device->getRHIBackend();
    
    if (backend == RHI::ERHIBackend::Vulkan)
    {
        // TODO: Implement Vulkan PBR pipeline creation
        // This requires proper pipeline state object creation with:
        // - Vertex/Fragment shaders
        // - Vertex input layout
        // - Rasterization state
        // - Depth stencil state
        // - Blend state
        // - Descriptor set layouts
        MR_LOG(LogCubeSceneApp, Log, "PBR Vulkan pipeline creation deferred - using placeholder");
    }
    else
    {
        // OpenGL path - shaders are loaded at draw time
        MR_LOG(LogCubeSceneApp, Log, "PBR OpenGL pipeline will be configured at draw time");
    }
    
    // Create PBR sampler
    RHI::SamplerDesc samplerDesc;
    samplerDesc.filter = RHI::ESamplerFilter::Trilinear;
    samplerDesc.addressU = RHI::ESamplerAddressMode::Wrap;
    samplerDesc.addressV = RHI::ESamplerAddressMode::Wrap;
    samplerDesc.addressW = RHI::ESamplerAddressMode::Wrap;
    samplerDesc.maxAnisotropy = 16;
    samplerDesc.minLOD = 0.0f;
    samplerDesc.maxLOD = 12.0f;
    
    m_pbrSampler = m_device->createSampler(samplerDesc);
    if (!m_pbrSampler)
    {
        MR_LOG(LogCubeSceneApp, Warning, "Failed to create PBR sampler");
    }
    
    return true;
}

bool CubeSceneApplication::createHelmetTextures()
{
    MR_LOG(LogCubeSceneApp, Log, "Creating helmet textures...");
    
    if (!m_helmetModel || m_helmetModel->Images.Num() == 0)
    {
        MR_LOG(LogCubeSceneApp, Warning, "No images in helmet model");
        return true; // Not a fatal error
    }
    
    // Helper lambda to create texture from glTF image
    auto createTextureFromImage = [this](const FGLTFImage& image, const char* name) -> TSharedPtr<RHI::IRHITexture>
    {
        if (!image.bIsLoaded || image.Data.Num() == 0)
        {
            MR_LOG(LogCubeSceneApp, Warning, "Image %s not loaded", name);
            return nullptr;
        }
        
        RHI::TextureDesc texDesc;
        texDesc.width = image.Width;
        texDesc.height = image.Height;
        texDesc.depth = 1;
        texDesc.mipLevels = 1;
        texDesc.arraySize = 1;
        texDesc.format = RHI::EPixelFormat::R8G8B8A8_UNORM;
        texDesc.usage = RHI::EResourceUsage::ShaderResource;
        texDesc.debugName = name;
        
        TSharedPtr<RHI::IRHITexture> texture = m_device->createTexture(texDesc);
        if (!texture)
        {
            MR_LOG(LogCubeSceneApp, Error, "Failed to create texture: %s", name);
            return nullptr;
        }
        
        MR_LOG(LogCubeSceneApp, Log, "Created texture: %s (%ux%u)", name, image.Width, image.Height);
        return texture;
    };
    
    // Find textures by material reference
    if (m_helmetModel->Materials.Num() > 0)
    {
        const FGLTFMaterial& material = m_helmetModel->Materials[0];
        
        // Base color texture
        if (material.BaseColorTexture.IsValid())
        {
            int32 texIndex = material.BaseColorTexture.TextureIndex;
            if (texIndex >= 0 && texIndex < m_helmetModel->Textures.Num())
            {
                int32 imageIndex = m_helmetModel->Textures[texIndex].ImageIndex;
                if (imageIndex >= 0 && imageIndex < m_helmetModel->Images.Num())
                {
                    m_helmetBaseColorTexture = createTextureFromImage(
                        m_helmetModel->Images[imageIndex], "Helmet_BaseColor");
                }
            }
        }
        
        // Normal texture
        if (material.NormalTexture.IsValid())
        {
            int32 texIndex = material.NormalTexture.TextureIndex;
            if (texIndex >= 0 && texIndex < m_helmetModel->Textures.Num())
            {
                int32 imageIndex = m_helmetModel->Textures[texIndex].ImageIndex;
                if (imageIndex >= 0 && imageIndex < m_helmetModel->Images.Num())
                {
                    m_helmetNormalTexture = createTextureFromImage(
                        m_helmetModel->Images[imageIndex], "Helmet_Normal");
                }
            }
        }
        
        // Metallic-roughness texture
        if (material.MetallicRoughnessTexture.IsValid())
        {
            int32 texIndex = material.MetallicRoughnessTexture.TextureIndex;
            if (texIndex >= 0 && texIndex < m_helmetModel->Textures.Num())
            {
                int32 imageIndex = m_helmetModel->Textures[texIndex].ImageIndex;
                if (imageIndex >= 0 && imageIndex < m_helmetModel->Images.Num())
                {
                    m_helmetMetallicRoughnessTexture = createTextureFromImage(
                        m_helmetModel->Images[imageIndex], "Helmet_MetallicRoughness");
                }
            }
        }
        
        // Occlusion texture
        if (material.OcclusionTexture.IsValid())
        {
            int32 texIndex = material.OcclusionTexture.TextureIndex;
            if (texIndex >= 0 && texIndex < m_helmetModel->Textures.Num())
            {
                int32 imageIndex = m_helmetModel->Textures[texIndex].ImageIndex;
                if (imageIndex >= 0 && imageIndex < m_helmetModel->Images.Num())
                {
                    m_helmetOcclusionTexture = createTextureFromImage(
                        m_helmetModel->Images[imageIndex], "Helmet_Occlusion");
                }
            }
        }
        
        // Emissive texture
        if (material.EmissiveTexture.IsValid())
        {
            int32 texIndex = material.EmissiveTexture.TextureIndex;
            if (texIndex >= 0 && texIndex < m_helmetModel->Textures.Num())
            {
                int32 imageIndex = m_helmetModel->Textures[texIndex].ImageIndex;
                if (imageIndex >= 0 && imageIndex < m_helmetModel->Images.Num())
                {
                    m_helmetEmissiveTexture = createTextureFromImage(
                        m_helmetModel->Images[imageIndex], "Helmet_Emissive");
                }
            }
        }
    }
    
    MR_LOG(LogCubeSceneApp, Log, "Helmet textures created");
    return true;
}

bool CubeSceneApplication::createHelmetBuffers()
{
    MR_LOG(LogCubeSceneApp, Log, "Creating helmet vertex/index buffers...");
    
    if (!m_helmetModel || m_helmetModel->Meshes.Num() == 0)
    {
        MR_LOG(LogCubeSceneApp, Error, "No meshes in helmet model");
        return false;
    }
    
    // Get first mesh and first primitive
    const FGLTFMesh& mesh = m_helmetModel->Meshes[0];
    if (mesh.Primitives.Num() == 0)
    {
        MR_LOG(LogCubeSceneApp, Error, "No primitives in helmet mesh");
        return false;
    }
    
    const FGLTFPrimitive& primitive = mesh.Primitives[0];
    
    // Build vertex buffer data
    uint32 vertexCount = primitive.GetVertexCount();
    TArray<FPBRVertex> vertices;
    vertices.SetNum(vertexCount);
    
    for (uint32 i = 0; i < vertexCount; ++i)
    {
        FPBRVertex& v = vertices[i];
        
        // Position
        v.Position = primitive.Positions[i];
        
        // Normal
        if (primitive.HasNormals() && i < static_cast<uint32>(primitive.Normals.Num()))
        {
            v.Normal = primitive.Normals[i];
        }
        else
        {
            v.Normal = FVector3f(0.0f, 1.0f, 0.0f);
        }
        
        // Tangent
        if (primitive.HasTangents() && i < static_cast<uint32>(primitive.Tangents.Num()))
        {
            v.Tangent = primitive.Tangents[i];
        }
        else
        {
            v.Tangent = FVector4f(1.0f, 0.0f, 0.0f, 1.0f);
        }
        
        // TexCoord0
        if (primitive.HasTexCoords() && i < static_cast<uint32>(primitive.TexCoords0.Num()))
        {
            v.TexCoord0 = primitive.TexCoords0[i];
        }
        else
        {
            v.TexCoord0 = FVector2f(0.0f, 0.0f);
        }
        
        // TexCoord1
        if (i < static_cast<uint32>(primitive.TexCoords1.Num()))
        {
            v.TexCoord1 = primitive.TexCoords1[i];
        }
        else
        {
            v.TexCoord1 = v.TexCoord0;
        }
        
        // Color
        if (primitive.HasColors() && i < static_cast<uint32>(primitive.Colors.Num()))
        {
            v.Color = primitive.Colors[i];
        }
        else
        {
            v.Color = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }
    
    // Create vertex buffer
    RHI::BufferDesc vertexBufferDesc;
    vertexBufferDesc.size = vertices.Num() * sizeof(FPBRVertex);
    vertexBufferDesc.usage = RHI::EResourceUsage::VertexBuffer;
    vertexBufferDesc.memoryUsage = RHI::EMemoryUsage::Default;
    vertexBufferDesc.debugName = "Helmet_VertexBuffer";
    
    m_helmetVertexBuffer = m_device->createBuffer(vertexBufferDesc);
    if (!m_helmetVertexBuffer)
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to create helmet vertex buffer");
        return false;
    }
    
    m_helmetVertexCount = vertexCount;
    
    // Create index buffer
    uint32 indexCount = primitive.GetIndexCount();
    
    RHI::BufferDesc indexBufferDesc;
    indexBufferDesc.size = indexCount * sizeof(uint32);
    indexBufferDesc.usage = RHI::EResourceUsage::IndexBuffer;
    indexBufferDesc.memoryUsage = RHI::EMemoryUsage::Default;
    indexBufferDesc.debugName = "Helmet_IndexBuffer";
    
    m_helmetIndexBuffer = m_device->createBuffer(indexBufferDesc);
    if (!m_helmetIndexBuffer)
    {
        MR_LOG(LogCubeSceneApp, Error, "Failed to create helmet index buffer");
        return false;
    }
    
    m_helmetIndexCount = indexCount;
    
    MR_LOG(LogCubeSceneApp, Log, "Helmet buffers created: %u vertices, %u indices",
           m_helmetVertexCount, m_helmetIndexCount);
    
    return true;
}

bool CubeSceneApplication::createPBRUniformBuffers()
{
    MR_LOG(LogCubeSceneApp, Log, "Creating PBR uniform buffers...");
    
    // View uniform buffer (Set 0, Binding 0)
    {
        RHI::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(FPBRViewUniforms);
        bufferDesc.usage = RHI::EResourceUsage::UniformBuffer;
        bufferDesc.memoryUsage = RHI::EMemoryUsage::Dynamic;
        bufferDesc.cpuAccessible = true;
        bufferDesc.debugName = "PBR_ViewUniformBuffer";
        
        m_pbrViewUniformBuffer = m_device->createBuffer(bufferDesc);
        if (!m_pbrViewUniformBuffer)
        {
            MR_LOG(LogCubeSceneApp, Error, "Failed to create view uniform buffer");
            return false;
        }
    }
    
    // Light uniform buffer (Set 0, Binding 1)
    {
        RHI::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(FPBRLightUniforms);
        bufferDesc.usage = RHI::EResourceUsage::UniformBuffer;
        bufferDesc.memoryUsage = RHI::EMemoryUsage::Dynamic;
        bufferDesc.cpuAccessible = true;
        bufferDesc.debugName = "PBR_LightUniformBuffer";
        
        m_pbrLightUniformBuffer = m_device->createBuffer(bufferDesc);
        if (!m_pbrLightUniformBuffer)
        {
            MR_LOG(LogCubeSceneApp, Error, "Failed to create light uniform buffer");
            return false;
        }
    }
    
    // Material uniform buffer (Set 1, Binding 0)
    {
        RHI::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(FPBRMaterialUniforms);
        bufferDesc.usage = RHI::EResourceUsage::UniformBuffer;
        bufferDesc.memoryUsage = RHI::EMemoryUsage::Dynamic;
        bufferDesc.cpuAccessible = true;
        bufferDesc.debugName = "PBR_MaterialUniformBuffer";
        
        m_pbrMaterialUniformBuffer = m_device->createBuffer(bufferDesc);
        if (!m_pbrMaterialUniformBuffer)
        {
            MR_LOG(LogCubeSceneApp, Error, "Failed to create material uniform buffer");
            return false;
        }
    }
    
    // Object uniform buffer (Set 2, Binding 0)
    {
        RHI::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(FPBRObjectUniforms);
        bufferDesc.usage = RHI::EResourceUsage::UniformBuffer;
        bufferDesc.memoryUsage = RHI::EMemoryUsage::Dynamic;
        bufferDesc.cpuAccessible = true;
        bufferDesc.debugName = "PBR_ObjectUniformBuffer";
        
        m_pbrObjectUniformBuffer = m_device->createBuffer(bufferDesc);
        if (!m_pbrObjectUniformBuffer)
        {
            MR_LOG(LogCubeSceneApp, Error, "Failed to create object uniform buffer");
            return false;
        }
    }
    
    MR_LOG(LogCubeSceneApp, Log, "PBR uniform buffers created");
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
    if (!m_bHelmetInitialized)
    {
        return;
    }
    
    // Update helmet rotation
    m_helmetRotationAngle += m_helmetRotationSpeed * m_deltaTime;
    if (m_helmetRotationAngle > 360.0f)
    {
        m_helmetRotationAngle -= 360.0f;
    }
    
    // Build model matrix: scale, rotate, translate
    FMatrix scaleMatrix = FMatrix::MakeScale(FVector(1.0, 1.0, 1.0));
    // Create rotation around Y axis
    double angleRad = static_cast<double>(m_helmetRotationAngle) * (3.14159265358979323846 / 180.0);
    double cosA = std::cos(angleRad);
    double sinA = std::sin(angleRad);
    FMatrix rotationMatrix = FMatrix::Identity;
    rotationMatrix.M[0][0] = cosA;
    rotationMatrix.M[0][2] = sinA;
    rotationMatrix.M[2][0] = -sinA;
    rotationMatrix.M[2][2] = cosA;
    FMatrix translationMatrix = FMatrix::MakeTranslation(FVector(0.0, 1.5, 0.0));
    
    m_helmetModelMatrix = scaleMatrix * rotationMatrix * translationMatrix;
    
    // Note: Actual buffer updates would happen here when pipeline is fully implemented
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
    if (!m_bHelmetPBREnabled || !m_bHelmetInitialized)
    {
        return;
    }
    
    if (!cmdList || !m_helmetVertexBuffer || !m_helmetIndexBuffer)
    {
        return;
    }
    
    // Update uniform buffers
    updatePBRUniforms(viewMatrix, projectionMatrix, cameraPosition);
    
    // TODO: Implement actual PBR rendering when pipeline is ready
    // For now, just log that we would render
    MR_LOG(LogCubeSceneApp, Verbose, "PBR helmet render called: %u vertices, %u indices",
           m_helmetVertexCount, m_helmetIndexCount);
}

// ============================================================================
// PBR Helmet Shutdown
// ============================================================================

void CubeSceneApplication::shutdownHelmetPBR()
{
    MR_LOG(LogCubeSceneApp, Log, "Shutting down PBR helmet resources...");
    
    // Release descriptor sets
    m_pbrPerFrameDescriptorSet.Reset();
    m_pbrPerMaterialDescriptorSet.Reset();
    m_pbrPerObjectDescriptorSet.Reset();
    
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
    
    // Release buffers
    m_helmetVertexBuffer.Reset();
    m_helmetIndexBuffer.Reset();
    
    // Release pipeline
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
