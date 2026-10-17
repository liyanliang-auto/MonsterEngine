// Copyright Monster Engine. All Rights Reserved.

#include "Renderer/Splat/SplatPass.h"

#include "Core/Logging/LogMacros.h"
#include "Core/ShaderCompiler.h"
#include "RHI/RHIDefinitions.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

namespace MonsterRender {
namespace Splat {

// ============================================================================
// Destruction
// ============================================================================

FSplatPreprocessPass::~FSplatPreprocessPass()
{
    m_output.release();
    m_cameraBuffer.Reset();
    for (uint32 i = 0; i < 2; ++i) { m_cameraBuffers[i].Reset(); m_activeBuffers[i].Reset(); }
    m_pipelineState.Reset();
    m_pipelineLayout.Reset();
    for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
    {
        m_inputDescriptorSets[i].Reset();
        m_outputDescriptorSets[i].Reset();
    }
    m_inputSetLayout.Reset();
    m_outputSetLayout.Reset();
    m_input.release();
}

// ============================================================================
// Initialization
// ============================================================================

bool FSplatPreprocessPass::initialize(RHI::IRHIDevice* device, uint32 gaussianCount)
{
    if (!device)
    {
        MR_LOG(LogTemp, Error, "[SplatPass] Null RHI device");
        return false;
    }

    if (gaussianCount == 0 || uint64_t(gaussianCount) * 16 > UINT32_MAX)
    {
        MR_LOG(LogTemp, Error, "[SplatPass] Gaussian count is zero");
        return false;
    }

    m_gaussianCount = gaussianCount;

    // Step 1: Create descriptor set layouts
    if (!createDescriptorSetLayouts(device))
        return false;

    // Step 2: Create pipeline (compile shader + create compute pipeline)
    if (!createPipeline(device))
        return false;

    // Step 3: Create output buffers
    if (!createOutputBuffers(device))
        return false;

    // Step 4: Allocate and update descriptor sets
    if (!allocateAndUpdateDescriptorSets(device))
        return false;

    // Independent mapped inputs for the engine's two frame slots. Writes occur
    // only in execute, after the owning frame fence has been acquired by the app.
    for (uint32 frame = 0; frame < kMaxFramesInFlight; ++frame) {
        RHI::BufferDesc cameraDesc;
        cameraDesc.dedicatedAllocation = true;
        cameraDesc.size = sizeof(FCameraUniforms);
        cameraDesc.usage = RHI::EResourceUsage::UniformBuffer;
        cameraDesc.memoryUsage = RHI::EMemoryUsage::Dynamic;
        cameraDesc.cpuAccessible = true;
        cameraDesc.debugName = "Splat_CameraFrame";
        m_cameraBuffers[frame] = device->createBuffer(cameraDesc);
        RHI::BufferDesc activeDesc;
        activeDesc.dedicatedAllocation = true;
        activeDesc.size = uint64_t(m_gaussianCount) * sizeof(uint32);
        activeDesc.usage = RHI::EResourceUsage::StorageBuffer;
        activeDesc.memoryUsage = RHI::EMemoryUsage::Dynamic;
        activeDesc.cpuAccessible = true;
        activeDesc.debugName = "Splat_ActiveFrame";
        m_activeBuffers[frame] = device->createBuffer(activeDesc);
        if (!m_cameraBuffers[frame] || !m_activeBuffers[frame]) return false;
        m_inputDescriptorSets[frame]->updateUniformBuffer(EInputBinding::Camera,
            m_cameraBuffers[frame], 0, sizeof(FCameraUniforms));
        m_inputDescriptorSets[frame]->updateStorageBuffer(EInputBinding::ActiveIndices,
            m_activeBuffers[frame], 0, 0);
    }
    m_bInitialized = true;
    MR_LOG(LogTemp, Log, "[SplatPass] Initialized for %u Gaussians", gaussianCount);
    return true;
}

bool FSplatPreprocessPass::createDescriptorSetLayouts(RHI::IRHIDevice* device)
{
    using namespace RHI;

    // Set 0: Input data (6 bindings)
    {
        FDescriptorSetLayoutDesc desc(0, "Splat_InputSet");
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EInputBinding::Positions, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EInputBinding::Scales, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EInputBinding::Rotations, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EInputBinding::Opacities, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EInputBinding::SHCoefficients, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EInputBinding::Camera, EDescriptorType::UniformBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(EInputBinding::ActiveIndices, EDescriptorType::StorageBuffer, EShaderStage::Compute));

        m_inputSetLayout = device->createDescriptorSetLayout(desc);
        if (!m_inputSetLayout)
        {
            MR_LOG(LogTemp, Error, "[SplatPass] Failed to create input descriptor set layout");
            return false;
        }
    }

    // Set 1: Output buffers (7 bindings)
    {
        FDescriptorSetLayoutDesc desc(1, "Splat_OutputSet");
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EOutputBinding::Radii, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EOutputBinding::Depth, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EOutputBinding::RGB, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EOutputBinding::ConicOpacity, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EOutputBinding::PointsXY, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EOutputBinding::TilesTouched, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EOutputBinding::BBox, EDescriptorType::StorageBuffer, EShaderStage::Compute));

        m_outputSetLayout = device->createDescriptorSetLayout(desc);
        if (!m_outputSetLayout)
        {
            MR_LOG(LogTemp, Error, "[SplatPass] Failed to create output descriptor set layout");
            return false;
        }
    }

    return true;
}

bool FSplatPreprocessPass::createPipeline(RHI::IRHIDevice* device)
{
    using namespace RHI;

    // Create pipeline layout with push constants
    {
        FPipelineLayoutDesc layoutDesc("SplatPreprocess");
        layoutDesc.setLayouts.Add(m_inputSetLayout);
        layoutDesc.setLayouts.Add(m_outputSetLayout);
        layoutDesc.pushConstantRanges.Add(
            FPushConstantRange(EShaderStage::Compute, 0, sizeof(FPreprocessPushConstants)));

        m_pipelineLayout = device->createPipelineLayout(layoutDesc);
        if (!m_pipelineLayout)
        {
            MR_LOG(LogTemp, Error, "[SplatPass] Failed to create pipeline layout");
            return false;
        }
    }

    // Load SPIR-V bytecode
    String spvPath = SplatShaderPath("Shaders/Splat/compiled/splat_preprocess.spv");
    auto bytecodeVec = MonsterRender::ShaderCompiler::readFileBytes(spvPath);
    if (bytecodeVec.empty())
    {
        MR_LOG(LogTemp, Error, "[SplatPass] Failed to load SPIR-V from: %s", spvPath.c_str());
        return false;
    }

    // Create compute shader
    TSpan<const uint8> bytecode(bytecodeVec.data(), bytecodeVec.size());
    auto shader = device->createComputeShader(bytecode);
    if (!shader)
    {
        MR_LOG(LogTemp, Error, "[SplatPass] Failed to create compute shader");
        return false;
    }

    // Create compute pipeline state
    ComputePipelineStateDesc pipeDesc;
    pipeDesc.computeShader = shader;
    pipeDesc.pipelineLayout = m_pipelineLayout;
    pipeDesc.debugName = "Splat_Preprocess";

    m_pipelineState = device->createComputePipelineState(pipeDesc);
    if (!m_pipelineState)
    {
        MR_LOG(LogTemp, Error, "[SplatPass] Failed to create compute pipeline state");
        return false;
    }

    MR_LOG(LogTemp, Log, "[SplatPass] Compute pipeline created (SPIR-V: %zu bytes)", bytecodeVec.size());
    return true;
}

bool FSplatPreprocessPass::createOutputBuffers(RHI::IRHIDevice* device)
{
    using namespace RHI;

    auto createStorageBuf = [device](uint64_t size, const char* name) -> MonsterEngine::TSharedPtr<IRHIBuffer>
    {
        BufferDesc desc;
        desc.dedicatedAllocation = true;
        desc.size = size;
        desc.usage = EResourceUsage::StorageBuffer | EResourceUsage::TransferDst | EResourceUsage::TransferSrc;
        desc.memoryUsage = EMemoryUsage::Default;
        desc.debugName = name;
        return device->createBuffer(desc);
    };

    uint64_t n = m_gaussianCount;

    m_output.radii        = createStorageBuf(n * sizeof(int32),   "Splat_Radii");
    m_output.depth        = createStorageBuf(n * sizeof(float32), "Splat_Depth");
    m_output.rgb          = createStorageBuf(ESplatProjectionColor::Bytes(n, m_output.projectionColorStorageMode), "Splat_RGB");
    m_output.conicOpacity = createStorageBuf(n * 4 * sizeof(float32), "Splat_ConicOpacity");
    m_output.pointsXY     = createStorageBuf(n * 2 * sizeof(float32), "Splat_PointsXY");
    m_output.tilesTouched = createStorageBuf(n * sizeof(uint32),  "Splat_TilesTouched");
    m_output.bbox         = createStorageBuf(n * 4 * sizeof(uint32),  "Splat_BBox");

    if (!m_output.radii || !m_output.depth || !m_output.rgb || !m_output.conicOpacity ||
        !m_output.pointsXY || !m_output.tilesTouched || !m_output.bbox)
    {
        MR_LOG(LogTemp, Error, "[SplatPass] Failed to create one or more output buffers");
        return false;
    }

    return true;
}

bool FSplatPreprocessPass::allocateAndUpdateDescriptorSets(RHI::IRHIDevice* device)
{
    // Allocate per-frame descriptor sets (double-buffered) to avoid
    // updating sets while the previous frame's command buffer is pending.
    for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
    {
        m_inputDescriptorSets[i] = device->allocateDescriptorSet(m_inputSetLayout);
        m_outputDescriptorSets[i] = device->allocateDescriptorSet(m_outputSetLayout);

        if (!m_inputDescriptorSets[i] || !m_outputDescriptorSets[i])
        {
            MR_LOG(LogTemp, Error, "[SplatPass] Failed to allocate descriptor set %u", i);
            return false;
        }

        // Pre-bake OUTPUT descriptor sets — output buffers are static,
        // so we update them once at initialization instead of every execute().
        auto& outDs = m_outputDescriptorSets[i];
        outDs->updateStorageBuffer(EOutputBinding::Radii,        m_output.radii,        0, 0);
        outDs->updateStorageBuffer(EOutputBinding::Depth,        m_output.depth,        0, 0);
        outDs->updateStorageBuffer(EOutputBinding::RGB,          m_output.rgb,          0, 0);
        outDs->updateStorageBuffer(EOutputBinding::ConicOpacity, m_output.conicOpacity, 0, 0);
        outDs->updateStorageBuffer(EOutputBinding::PointsXY,     m_output.pointsXY,     0, 0);
        outDs->updateStorageBuffer(EOutputBinding::TilesTouched, m_output.tilesTouched, 0, 0);
        outDs->updateStorageBuffer(EOutputBinding::BBox,         m_output.bbox,         0, 0);
    }

    return true;
}

// ============================================================================
// Input / Camera Setup
// ============================================================================

void FSplatPreprocessPass::setInputs(const FSplatGPUData& input)
{
    m_input = input;

}

void FSplatPreprocessPass::updateCamera(const FCameraUniforms& camera) { m_camera = camera; }

bool FSplatPreprocessPass::setActiveIndices(const std::vector<uint32_t>& indices, bool enabled) {
    if (indices.size() > m_gaussianCount) return false;
    for (auto source : indices) if (source >= m_input.gaussianCount) return false;
    m_activeIndices = indices; m_useActiveIndices = enabled;
    return true;
}

void FSplatPreprocessPass::setClipPlanes(float32 nearPlane, float32 farPlane)
{
    m_nearPlane = nearPlane;
    m_farPlane  = farPlane;
}

// ============================================================================
// Execute
// ============================================================================

bool FSplatPreprocessPass::execute(RHI::IRHICommandList* cmdList, uint32 gaussianCount)
{
    if (!m_bInitialized)
    {
        MR_LOG(LogTemp, Error, "[SplatPass] Not initialized");
        return false;
    }

    if (!cmdList)
    {
        MR_LOG(LogTemp, Error, "[SplatPass] Null command list");
        return false;
    }

    // Toggle per-frame descriptor set index to avoid updating sets
    // that are still in use by the previous frame's pending command buffer.
    m_currentDsIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
    auto& inDs  = m_inputDescriptorSets[m_currentDsIndex];
    auto& outDs = m_outputDescriptorSets[m_currentDsIndex];

    if (gaussianCount > m_gaussianCount || !m_input.isValid()) return false;
    if (m_input.shStorageMode > 1 || m_input.shDegree < 0 || m_input.shDegree > 3 ||
        m_input.shCoefficients->getSize() < SplatSHStorageBytes(uint64_t(m_input.gaussianCount) *
            (m_input.shDegree + 1) * (m_input.shDegree + 1) * 3, m_input.shStorageMode)) return false;
    void* cameraData = m_cameraBuffers[m_currentDsIndex]->map();
    if (!cameraData) return false;
    FCameraUniforms camera = m_camera;
    camera.shDegree = std::min(camera.shDegree, m_input.shDegree);
    std::memcpy(cameraData, &camera, sizeof(camera));
    m_cameraBuffers[m_currentDsIndex]->unmap();
    if (m_useActiveIndices && gaussianCount) {
        void* indices = m_activeBuffers[m_currentDsIndex]->map();
        if (!indices || m_activeIndices.size() != gaussianCount) {
            if (indices) m_activeBuffers[m_currentDsIndex]->unmap();
            return false;
        }
        std::memcpy(indices, m_activeIndices.data(), size_t(gaussianCount) * sizeof(uint32));
        m_activeBuffers[m_currentDsIndex]->unmap();
    }
    inDs->updateStorageBuffer(EInputBinding::Positions, m_input.positions, 0, 0);
    inDs->updateStorageBuffer(EInputBinding::Scales, m_input.scales, 0, 0);
    inDs->updateStorageBuffer(EInputBinding::Rotations, m_input.rotations, 0, 0);
    inDs->updateStorageBuffer(EInputBinding::Opacities, m_input.opacities, 0, 0);
    inDs->updateStorageBuffer(EInputBinding::SHCoefficients, m_input.shCoefficients, 0, 0);
    // Set up push constants
    FPreprocessPushConstants pc{};
    pc.gaussianCount = gaussianCount;
    pc.nearPlane = m_nearPlane;
    pc.farPlane = m_farPlane;
    pc.culling = m_distanceCulling ? 1u : 0u;
    pc.gaussianScale = m_gaussianScale;
    pc.tightTileCulling = m_tightTileCulling;
    pc.useActiveIndices = m_useActiveIndices ? 1u : 0u;
    pc.sourceCapacity = m_input.gaussianCount;
    pc.shStorageMode = m_input.shStorageMode;
    pc.projectionColorStorageMode = m_output.projectionColorStorageMode;
    pc.shStride = uint32((m_input.shDegree + 1) * (m_input.shDegree + 1));

    // Dispatch
    uint32 groupCountX = (gaussianCount + 255) / 256;

    // ---- Diagnostics: confirm compute pipeline binding + dispatch inputs ----
    bool bPipeOk = (m_pipelineState.Get() != nullptr);
    bool bInOk   = (inDs.Get() != nullptr);
    bool bOutOk  = (outDs.Get() != nullptr);
    MR_LOG(LogTemp, Verbose, "[SplatPass] Preprocess BEFORE dispatch: pipelineValid=%d inDsValid=%d outDsValid=%d groupCountX=%u gaussianCount=%u near=%.4f far=%.4f culling=%d",
           (int)bPipeOk, (int)bInOk, (int)bOutOk, groupCountX, gaussianCount, pc.nearPlane, pc.farPlane, pc.culling);

    cmdList->setPipelineState(m_pipelineState);
    cmdList->bindDescriptorSet(m_pipelineLayout, 0, inDs);
    cmdList->bindDescriptorSet(m_pipelineLayout, 1, outDs);
    cmdList->pushConstants(m_pipelineLayout, RHI::EShaderStage::Compute, 0, 
                           sizeof(FPreprocessPushConstants), &pc);
    if (groupCountX) cmdList->dispatch(std::min(groupCountX, 65535u), (groupCountX + 65534u) / 65535u, 1);

    MR_LOG(LogTemp, Verbose, "[SplatPass] Dispatched %u groups (%u Gaussians)",
           groupCountX, gaussianCount);
#if 0  // Set to 1 to re-enable per-frame stderr preprocess diagnostics
    fprintf(stderr, "[STDERR] Preprocess: dispatch %u groups (%u gaussians) near=%.3f far=%.3f culling=%u pipeline=%d\n",
            groupCountX, gaussianCount, pc.nearPlane, pc.farPlane, pc.culling, (int)bPipeOk);
#endif
    return true;
}

std::vector<MonsterEngine::TSharedPtr<RHI::IRHIBuffer>> FSplatPreprocessPass::GetOwnedBuffers() const {
    return {m_output.radii, m_output.depth, m_output.rgb, m_output.conicOpacity,
        m_output.pointsXY, m_output.tilesTouched, m_output.bbox,
        m_cameraBuffers[0], m_cameraBuffers[1], m_activeBuffers[0], m_activeBuffers[1]};
}

} // namespace Splat
} // namespace MonsterRender
