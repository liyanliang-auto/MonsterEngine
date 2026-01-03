// Copyright Monster Engine. All Rights Reserved.

/**
 * @file PBRRenderer.cpp
 * @brief Implementation of FPBRRenderer class
 */

#include "Renderer/PBR/PBRRenderer.h"
#include "Renderer/PBR/PBRMaterial.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIResource.h"
#include "RHI/IRHIDescriptorSet.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/RHIResources.h"
#include "Core/Log.h"
#include <cmath>

DEFINE_LOG_CATEGORY_STATIC(LogPBRRenderer, Log, All);

namespace MonsterEngine
{
namespace Renderer
{

using namespace MonsterRender::RHI;

// ============================================================================
// Constructor / Destructor
// ============================================================================

FPBRRenderer::FPBRRenderer()
    : m_device(nullptr)
    , m_initialized(false)
    , m_perFrameDirty(true)
{
}

FPBRRenderer::~FPBRRenderer()
{
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool FPBRRenderer::initialize(IRHIDevice* device)
{
    if (m_initialized) {
        MR_LOG(LogPBRRenderer, Warning, "PBR Renderer already initialized");
        return true;
    }
    
    if (!device) {
        MR_LOG(LogPBRRenderer, Error, "Invalid device");
        return false;
    }
    
    m_device = device;
    
    // Initialize descriptor set manager
    if (!m_descriptorSetManager.initialize(device)) {
        MR_LOG(LogPBRRenderer, Error, "Failed to initialize descriptor set manager");
        return false;
    }
    
    // Create per-frame uniform buffers
    if (!_createPerFrameBuffers()) {
        MR_LOG(LogPBRRenderer, Error, "Failed to create per-frame buffers");
        return false;
    }
    
    // Create per-object uniform buffer
    if (!_createPerObjectBuffer()) {
        MR_LOG(LogPBRRenderer, Error, "Failed to create per-object buffer");
        return false;
    }
    
    // Create descriptor sets
    if (!_createDescriptorSets()) {
        MR_LOG(LogPBRRenderer, Error, "Failed to create descriptor sets");
        return false;
    }
    
    m_initialized = true;
    MR_LOG(LogPBRRenderer, Log, "PBR Renderer initialized successfully");
    return true;
}

void FPBRRenderer::shutdown()
{
    m_perObjectDescriptorSet.reset();
    m_perFrameDescriptorSet.reset();
    m_objectUniformBuffer.reset();
    m_lightUniformBuffer.reset();
    m_viewUniformBuffer.reset();
    m_descriptorSetManager.shutdown();
    m_device = nullptr;
    m_initialized = false;
    
    MR_LOG(LogPBRRenderer, Log, "PBR Renderer shutdown");
}

// ============================================================================
// Frame Management
// ============================================================================

void FPBRRenderer::beginFrame(uint32 frameIndex)
{
    m_context.FrameIndex = frameIndex;
    m_descriptorSetManager.beginFrame(frameIndex);
    m_perFrameDirty = true;
}

void FPBRRenderer::endFrame()
{
    // Nothing to do for now
}

// ============================================================================
// View Setup
// ============================================================================

void FPBRRenderer::setViewMatrices(const Math::FMatrix& viewMatrix,
                                    const Math::FMatrix& projectionMatrix,
                                    const Math::FVector& cameraPosition)
{
    m_context.ViewData.ViewMatrix = viewMatrix;
    m_context.ViewData.ProjectionMatrix = projectionMatrix;
    m_context.ViewData.ViewProjectionMatrix = viewMatrix * projectionMatrix;
    m_context.ViewData.InvViewMatrix = viewMatrix.Inverse();
    
    m_context.ViewData.CameraPosition = FVector4f(
        static_cast<float>(cameraPosition.X),
        static_cast<float>(cameraPosition.Y),
        static_cast<float>(cameraPosition.Z),
        1.0f);
    
    // Extract forward direction from view matrix (negative Z in view space)
    m_context.ViewData.CameraForward = FVector4f(
        -static_cast<float>(viewMatrix.M[2][0]),
        -static_cast<float>(viewMatrix.M[2][1]),
        -static_cast<float>(viewMatrix.M[2][2]),
        0.0f);
    
    m_perFrameDirty = true;
}

void FPBRRenderer::setViewport(uint32 width, uint32 height)
{
    m_context.ViewportWidth = width;
    m_context.ViewportHeight = height;
    
    float invWidth = width > 0 ? 1.0f / static_cast<float>(width) : 0.0f;
    float invHeight = height > 0 ? 1.0f / static_cast<float>(height) : 0.0f;
    
    m_context.ViewData.ViewportSize = FVector4f(
        static_cast<float>(width),
        static_cast<float>(height),
        invWidth,
        invHeight);
    
    m_perFrameDirty = true;
}

void FPBRRenderer::setTime(float time, float deltaTime)
{
    m_context.Time = time;
    m_context.DeltaTime = deltaTime;
    
    m_context.ViewData.TimeParams = FVector4f(
        time,
        std::sin(time),
        std::cos(time),
        deltaTime);
    
    m_perFrameDirty = true;
}

// ============================================================================
// Lighting Setup
// ============================================================================

void FPBRRenderer::setDirectionalLight(const Math::FVector& direction,
                                        const Math::FVector& color,
                                        float intensity)
{
    FVector3f dir(
        static_cast<float>(direction.X),
        static_cast<float>(direction.Y),
        static_cast<float>(direction.Z));
    FVector3f col(
        static_cast<float>(color.X),
        static_cast<float>(color.Y),
        static_cast<float>(color.Z));
    
    m_context.LightData.setDirectionalLight(dir, col, intensity);
    m_perFrameDirty = true;
}

void FPBRRenderer::setAmbientLight(const Math::FVector& color, float intensity)
{
    FVector3f col(
        static_cast<float>(color.X),
        static_cast<float>(color.Y),
        static_cast<float>(color.Z));
    
    m_context.LightData.setAmbientLight(col, intensity);
    m_perFrameDirty = true;
}

void FPBRRenderer::setExposure(float exposure)
{
    m_context.ViewData.ExposureParams.X = exposure;
    m_perFrameDirty = true;
}

// ============================================================================
// Rendering
// ============================================================================

void FPBRRenderer::updatePerFrameBuffers()
{
    if (!m_initialized || !m_perFrameDirty) {
        return;
    }
    
    // Update view uniform buffer
    if (m_viewUniformBuffer) {
        void* mappedData = m_viewUniformBuffer->map();
        if (mappedData) {
            memcpy(mappedData, &m_context.ViewData, sizeof(FViewUniformBuffer));
            m_viewUniformBuffer->unmap();
        }
    }
    
    // Update light uniform buffer
    if (m_lightUniformBuffer) {
        void* mappedData = m_lightUniformBuffer->map();
        if (mappedData) {
            memcpy(mappedData, &m_context.LightData, sizeof(FLightUniformBuffer));
            m_lightUniformBuffer->unmap();
        }
    }
    
    // Update per-frame descriptor set
    if (m_perFrameDescriptorSet) {
        m_perFrameDescriptorSet->updateUniformBuffer(
            static_cast<uint32>(EPBRPerFrameBinding::ViewUBO),
            m_viewUniformBuffer);
        m_perFrameDescriptorSet->updateUniformBuffer(
            static_cast<uint32>(EPBRPerFrameBinding::LightUBO),
            m_lightUniformBuffer);
    }
    
    m_perFrameDirty = false;
}

void FPBRRenderer::bindPerFrameDescriptorSet(IRHICommandList* cmdList)
{
    if (!cmdList || !m_perFrameDescriptorSet) {
        return;
    }
    
    auto pipelineLayout = m_descriptorSetManager.getPipelineLayout();
    if (pipelineLayout) {
        cmdList->bindDescriptorSet(pipelineLayout,
                                   static_cast<uint32>(EPBRDescriptorSet::PerFrame),
                                   m_perFrameDescriptorSet);
    }
}

void FPBRRenderer::drawObject(IRHICommandList* cmdList,
                               FPBRMaterial* material,
                               const Math::FMatrix& modelMatrix,
                               IRHIBuffer* vertexBuffer,
                               IRHIBuffer* indexBuffer,
                               uint32 vertexCount,
                               uint32 indexCount)
{
    if (!cmdList || !material || !vertexBuffer) {
        MR_LOG(LogPBRRenderer, Warning, "drawObject: Invalid parameters");
        return;
    }
    
    auto pipelineLayout = m_descriptorSetManager.getPipelineLayout();
    if (!pipelineLayout) {
        MR_LOG(LogPBRRenderer, Error, "drawObject: No pipeline layout");
        return;
    }
    
    // Update material GPU resources if dirty
    material->updateGPUResources();
    
    // Update per-object uniform buffer
    _updateObjectBuffer(modelMatrix);
    
    // Bind per-material descriptor set (Set 1)
    auto materialDescSet = material->getDescriptorSet();
    if (materialDescSet) {
        cmdList->bindDescriptorSet(pipelineLayout,
                                   static_cast<uint32>(EPBRDescriptorSet::PerMaterial),
                                   materialDescSet);
    }
    
    // Bind per-object descriptor set (Set 2)
    if (m_perObjectDescriptorSet) {
        m_perObjectDescriptorSet->updateUniformBuffer(
            static_cast<uint32>(EPBRPerObjectBinding::ObjectUBO),
            m_objectUniformBuffer);
        
        cmdList->bindDescriptorSet(pipelineLayout,
                                   static_cast<uint32>(EPBRDescriptorSet::PerObject),
                                   m_perObjectDescriptorSet);
    }
    
    // Bind vertex buffer
    TArray<TSharedPtr<IRHIBuffer>> vertexBuffers;
    // Note: Need to wrap raw pointer - this is a simplified version
    // In production, vertex buffers should be managed with shared pointers
    
    // Draw
    if (indexBuffer && indexCount > 0) {
        cmdList->drawIndexed(indexCount, 0, 0);
    } else {
        cmdList->draw(vertexCount, 0);
    }
}

// ============================================================================
// Private Methods
// ============================================================================

bool FPBRRenderer::_createPerFrameBuffers()
{
    if (!m_device) {
        return false;
    }
    
    // Create view uniform buffer
    BufferDesc viewDesc;
    viewDesc.size = sizeof(FViewUniformBuffer);
    viewDesc.usage = EResourceUsage::UniformBuffer;
    viewDesc.memoryUsage = EMemoryUsage::Dynamic;
    viewDesc.cpuAccessible = true;
    viewDesc.debugName = "PBR_ViewUniformBuffer";
    
    m_viewUniformBuffer = m_device->createBuffer(viewDesc);
    if (!m_viewUniformBuffer) {
        MR_LOG(LogPBRRenderer, Error, "Failed to create view uniform buffer");
        return false;
    }
    
    // Create light uniform buffer
    BufferDesc lightDesc;
    lightDesc.size = sizeof(FLightUniformBuffer);
    lightDesc.usage = EResourceUsage::UniformBuffer;
    lightDesc.memoryUsage = EMemoryUsage::Dynamic;
    lightDesc.cpuAccessible = true;
    lightDesc.debugName = "PBR_LightUniformBuffer";
    
    m_lightUniformBuffer = m_device->createBuffer(lightDesc);
    if (!m_lightUniformBuffer) {
        MR_LOG(LogPBRRenderer, Error, "Failed to create light uniform buffer");
        return false;
    }
    
    MR_LOG(LogPBRRenderer, Log, "Created per-frame uniform buffers (View: %llu bytes, Light: %llu bytes)",
           static_cast<uint64>(sizeof(FViewUniformBuffer)),
           static_cast<uint64>(sizeof(FLightUniformBuffer)));
    
    return true;
}

bool FPBRRenderer::_createPerObjectBuffer()
{
    if (!m_device) {
        return false;
    }
    
    BufferDesc desc;
    desc.size = sizeof(FObjectUniformBuffer);
    desc.usage = EResourceUsage::UniformBuffer;
    desc.memoryUsage = EMemoryUsage::Dynamic;
    desc.cpuAccessible = true;
    desc.debugName = "PBR_ObjectUniformBuffer";
    
    m_objectUniformBuffer = m_device->createBuffer(desc);
    if (!m_objectUniformBuffer) {
        MR_LOG(LogPBRRenderer, Error, "Failed to create object uniform buffer");
        return false;
    }
    
    MR_LOG(LogPBRRenderer, Log, "Created per-object uniform buffer (%llu bytes)",
           static_cast<uint64>(sizeof(FObjectUniformBuffer)));
    
    return true;
}

bool FPBRRenderer::_createDescriptorSets()
{
    // Allocate per-frame descriptor set
    m_perFrameDescriptorSet = m_descriptorSetManager.getPerFrameDescriptorSet();
    if (!m_perFrameDescriptorSet) {
        MR_LOG(LogPBRRenderer, Error, "Failed to allocate per-frame descriptor set");
        return false;
    }
    
    // Allocate per-object descriptor set
    m_perObjectDescriptorSet = m_descriptorSetManager.getPerObjectDescriptorSet();
    if (!m_perObjectDescriptorSet) {
        MR_LOG(LogPBRRenderer, Error, "Failed to allocate per-object descriptor set");
        return false;
    }
    
    MR_LOG(LogPBRRenderer, Log, "Created PBR descriptor sets");
    return true;
}

void FPBRRenderer::_updateObjectBuffer(const Math::FMatrix& modelMatrix)
{
    if (!m_objectUniformBuffer) {
        return;
    }
    
    // Update object data
    m_objectData.updateFromModelMatrix(modelMatrix, m_context.ViewData.ViewProjectionMatrix);
    
    // Upload to GPU
    void* mappedData = m_objectUniformBuffer->map();
    if (mappedData) {
        memcpy(mappedData, &m_objectData, sizeof(FObjectUniformBuffer));
        m_objectUniformBuffer->unmap();
    }
}

} // namespace Renderer
} // namespace MonsterEngine
