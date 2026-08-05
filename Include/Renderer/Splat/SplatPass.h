// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SplatPass.h
 * @brief Compute pass for the 3DGS preprocess stage.
 * 
 * Creates Vulkan compute pipeline, descriptor sets, and output buffers for
 * running the splat_preprocess compute shader on the GPU.
 * 
 * Usage:
 *   1. Load Gaussian data: FSplatPLYLoader::loadAndUpload() -> FSplatGPUData
 *   2. Create pass: FSplatPreprocessPass::initialize(device, gaussianCount)
 *   3. Set inputs:   pass.setInputs(gpuData)
 *   4. Set camera:   pass.updateCamera(cameraUniforms)
 *   5. Execute:      pass.execute(cmdList, gaussianCount)
 *   6. Read outputs: pass.getRadiiBuffer(), etc.
 */

#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDescriptorSet.h"
#include "Renderer/Splat/SplatTypes.h"
#include "Renderer/Splat/SplatPLYLoader.h"

namespace MonsterRender {
namespace Splat {

/**
 * GPU output buffers produced by the preprocess compute shader.
 * Each buffer has `gaussianCount` elements.
 */
struct FSplatPreprocessOutput
{
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> radii;         // int[],    sizeof(int32)  * count
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> depth;         // float[],  sizeof(float32) * count
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> rgb;           // vec4[],   sizeof(float32) * count * 4
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> conicOpacity;  // vec4[],   sizeof(float32) * count * 4
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> pointsXY;      // vec2[],   sizeof(float32) * count * 2
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> tilesTouched;  // uint[],   sizeof(uint32)  * count
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> bbox;          // uvec4[],  sizeof(uint32)  * count * 4

    void release()
    {
        radii.Reset();
        depth.Reset();
        rgb.Reset();
        conicOpacity.Reset();
        pointsXY.Reset();
        tilesTouched.Reset();
        bbox.Reset();
    }
};

/**
 * Preprocess compute pass.
 * Runs the full 3DGS preprocess stage on the GPU via Vulkan compute dispatch.
 */
class FSplatPreprocessPass
{
public:
    FSplatPreprocessPass() = default;
    ~FSplatPreprocessPass();

    /**
     * Initialize the compute pipeline, descriptor layouts, and output buffers.
     * Must be called once before execute().
     * 
     * @param device RHI device for resource creation
     * @param gaussianCount Total number of gaussians to process
     * @return true on success
     */
    bool initialize(RHI::IRHIDevice* device, uint32 gaussianCount);

    /**
     * Set input buffers from loaded Gaussian data.
     */
    void setInputs(const FSplatGPUData& input);

    /**
     * Update the camera uniform buffer with current frame camera data.
     */
    void updateCamera(const FCameraUniforms& camera);

    /**
     * Set near/far clipping planes for frustum culling.
     * Must be called before execute().
     */
    void setClipPlanes(float32 nearPlane, float32 farPlane);

    /**
     * Execute the compute dispatch.
     * 
     * @param cmdList Command list to record dispatch into
     * @param gaussianCount Number of gaussians (must be initialized count)
     */
    void execute(RHI::IRHICommandList* cmdList, uint32 gaussianCount);

    /** Access output buffers */
    const FSplatPreprocessOutput& getOutput() const { return m_output; }

    /** Check if initialized */
    bool isInitialized() const { return m_bInitialized; }

    /** Get gaussian count */
    uint32 getGaussianCount() const { return m_gaussianCount; }

private:
    bool createDescriptorSetLayouts(RHI::IRHIDevice* device);
    bool createPipeline(RHI::IRHIDevice* device);
    bool createOutputBuffers(RHI::IRHIDevice* device);
    bool allocateAndUpdateDescriptorSets(RHI::IRHIDevice* device);

    // ---- State ----
    bool m_bInitialized = false;
    uint32 m_gaussianCount = 0;

    // Input buffers (external, managed by FSplatGPUData)
    FSplatGPUData m_input;

    // Camera UBO
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_cameraBuffer;

    // Clip planes for frustum culling
    float32 m_nearPlane = 0.01f;
    float32 m_farPlane  = 1000.0f;

    // Output buffers
    FSplatPreprocessOutput m_output;

    // Descriptor set layouts
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> m_inputSetLayout;   // Set 0
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> m_outputSetLayout;  // Set 1

    // Pipeline layout
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout> m_pipelineLayout;

    // Compute pipeline state
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineState> m_pipelineState;

    // Allocated descriptor sets (per-frame double-buffered to avoid
    // updating sets while command buffer is pending)
    static constexpr uint32 kMaxFramesInFlight = 2;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> m_inputDescriptorSets[kMaxFramesInFlight];   // Set 0
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> m_outputDescriptorSets[kMaxFramesInFlight];  // Set 1
    uint32 m_currentDsIndex = 0;  // Toggles each execute() for per-frame selection
};

} // namespace Splat
} // namespace MonsterRender
