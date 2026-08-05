// Copyright Monster Engine. All Rights Reserved.

#include "Renderer/Splat/SplatPass.h"

#include "Core/Logging/LogMacros.h"
#include "Core/ShaderCompiler.h"
#include "RHI/RHIDefinitions.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace MonsterRender {
namespace Splat {

// ============================================================================
// Destruction
// ============================================================================

FSplatPreprocessPass::~FSplatPreprocessPass()
{
    m_output.release();
    m_cameraBuffer.Reset();
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

    if (gaussianCount == 0)
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

    // Create camera UBO (initial dummy data)
    {
        RHI::BufferDesc desc;
        desc.size = sizeof(FCameraUniforms);
        desc.usage = RHI::EResourceUsage::UniformBuffer;
        desc.memoryUsage = RHI::EMemoryUsage::Dynamic;
        desc.cpuAccessible = true;
        desc.debugName = "Splat_CameraUBO";
        m_cameraBuffer = device->createBuffer(desc);
        if (!m_cameraBuffer)
        {
            MR_LOG(LogTemp, Error, "[SplatPass] Failed to create camera UBO");
            return false;
        }

        // Pre-bake Camera UBO binding on ALL per-frame input descriptor sets.
        // This is required because on the first frame ensureSortPassesInitialized()
        // calls execute() which toggles m_currentDsIndex, and then the normal pass
        // calls execute() again, potentially using a set whose Camera binding was
        // never written. Pre-baking here ensures both sets start with a valid binding.
        for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
        {
            if (m_inputDescriptorSets[i])
            {
                m_inputDescriptorSets[i]->updateUniformBuffer(
                    EInputBinding::Camera, m_cameraBuffer, 0, sizeof(FCameraUniforms));
            }
        }
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
    String spvPath = "Shaders/Splat/compiled/splat_preprocess.spv";
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

    auto createStorageBuf = [device](uint32 size, const char* name) -> MonsterEngine::TSharedPtr<IRHIBuffer>
    {
        BufferDesc desc;
        desc.size = size;
        desc.usage = EResourceUsage::StorageBuffer | EResourceUsage::TransferDst | EResourceUsage::TransferSrc;
        desc.memoryUsage = EMemoryUsage::Default;
        desc.debugName = name;
        return device->createBuffer(desc);
    };

    uint32 n = m_gaussianCount;

    m_output.radii        = createStorageBuf(n * sizeof(int32),   "Splat_Radii");
    m_output.depth        = createStorageBuf(n * sizeof(float32), "Splat_Depth");
    m_output.rgb          = createStorageBuf(n * 4 * sizeof(float32), "Splat_RGB");
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

    // Update BOTH per-frame input descriptor sets with the newly provided buffers
    for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
    {
        auto& ds = m_inputDescriptorSets[i];
        ds->updateStorageBuffer(EInputBinding::Positions,      input.positions,      0, 0);
        ds->updateStorageBuffer(EInputBinding::Scales,         input.scales,         0, 0);
        ds->updateStorageBuffer(EInputBinding::Rotations,      input.rotations,      0, 0);
        ds->updateStorageBuffer(EInputBinding::Opacities,      input.opacities,      0, 0);
        ds->updateStorageBuffer(EInputBinding::SHCoefficients, input.shCoefficients, 0, 0);
    }
}

    void FSplatPreprocessPass::updateCamera(const FCameraUniforms& camera)
    {
        // Write camera data into the persistent UBO
        if (m_cameraBuffer)
        {
            void* data = m_cameraBuffer->map();
            if (data)
            {
                std::memcpy(data, &camera, sizeof(FCameraUniforms));
                m_cameraBuffer->unmap();
            }

            MR_LOG(LogTemp, Verbose, "[SplatPass] updateCamera: camPos=(%.3f, %.3f, %.3f) near=%.4f far=%.4f",
                   camera.camPos[0], camera.camPos[1], camera.camPos[2], m_nearPlane, m_farPlane);

        // Pre-bake camera UBO binding for the NEXT frame's input descriptor set.
        // Only updating the next set (not all kMaxFramesInFlight sets) avoids
        // VUID-03047: updating a set still in use by a pending command buffer.
        uint32 nextIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
        if (m_inputDescriptorSets[nextIndex])
        {
            m_inputDescriptorSets[nextIndex]->updateUniformBuffer(
                EInputBinding::Camera, m_cameraBuffer, 0, sizeof(FCameraUniforms));
        }
    }
}

void FSplatPreprocessPass::setClipPlanes(float32 nearPlane, float32 farPlane)
{
    m_nearPlane = nearPlane;
    m_farPlane  = farPlane;
}

// ============================================================================
// Execute
// ============================================================================

void FSplatPreprocessPass::execute(RHI::IRHICommandList* cmdList, uint32 gaussianCount)
{
    if (!m_bInitialized)
    {
        MR_LOG(LogTemp, Error, "[SplatPass] Not initialized");
        return;
    }

    if (!cmdList)
    {
        MR_LOG(LogTemp, Error, "[SplatPass] Null command list");
        return;
    }

    // Toggle per-frame descriptor set index to avoid updating sets
    // that are still in use by the previous frame's pending command buffer.
    m_currentDsIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
    auto& inDs  = m_inputDescriptorSets[m_currentDsIndex];
    auto& outDs = m_outputDescriptorSets[m_currentDsIndex];

    // Camera UBO binding is pre-baked in updateCamera() for the next frame.
    // Output descriptor sets are pre-baked in allocateAndUpdateDescriptorSets().

    // Set up push constants
    FPreprocessPushConstants pc;
    pc.gaussianCount = gaussianCount;
    pc.nearPlane = m_nearPlane;
    pc.farPlane = m_farPlane;
    pc.culling = 1; // Enable frustum culling

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
    cmdList->dispatch(groupCountX, 1, 1);

    MR_LOG(LogTemp, Verbose, "[SplatPass] Dispatched %u groups (%u Gaussians)",
           groupCountX, gaussianCount);
#if 0  // Set to 1 to re-enable per-frame stderr preprocess diagnostics
    fprintf(stderr, "[STDERR] Preprocess: dispatch %u groups (%u gaussians) near=%.3f far=%.3f culling=%u pipeline=%d\n",
            groupCountX, gaussianCount, pc.nearPlane, pc.farPlane, pc.culling, (int)bPipeOk);
#endif
}

} // namespace Splat
} // namespace MonsterRender
