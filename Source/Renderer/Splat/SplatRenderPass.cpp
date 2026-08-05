/**
 * @file SplatRenderPass.cpp
 * @brief Implementation of the per-tile EWA alpha blend render pass
 */

#include "Renderer/Splat/SplatRenderPass.h"
#include "Core/Logging/LogMacros.h"
#include "Core/ShaderCompiler.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDescriptorSet.h"
#include "RHI/RHIDefinitions.h"
#include <cstdio>

namespace MonsterRender::Splat
{
    // ========================================================================
    // Helpers
    // ========================================================================

    namespace
    {
        /**
         * Read SPIR-V file and create a compute pipeline.
         */
        static bool loadAndCreateComputePipeline(
            RHI::IRHIDevice*                    device,
            const MonsterEngine::String&        spirvPath,
            MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> setLayout,
            MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout>&     outPipelineLayout,
            MonsterEngine::TSharedPtr<RHI::IRHIPipelineState>&       outPipeline)
        {
            using namespace RHI;

            // Load SPIR-V bytecode
            auto bytecodeVec = MonsterRender::ShaderCompiler::readFileBytes(spirvPath);
            if (bytecodeVec.empty())
            {
                MR_LOG(LogTemp, Error, "[SplatRender] Failed to read SPIR-V: %s", spirvPath.c_str());
                return false;
            }

            MonsterEngine::TSpan<const uint8> bytecode(bytecodeVec.data(), bytecodeVec.size());
            auto computeShader = device->createComputeShader(bytecode);
            if (!computeShader)
            {
                MR_LOG(LogTemp, Error, "[SplatRender] Failed to create compute shader");
                return false;
            }

            // Create pipeline layout
            {
                FPipelineLayoutDesc layoutDesc(spirvPath);
                layoutDesc.setLayouts.Add(setLayout);

                FPushConstantRange pushRange(RHI::EShaderStage::Compute, 0, sizeof(FRenderPushConstants));
                layoutDesc.pushConstantRanges.Add(pushRange);

                outPipelineLayout = device->createPipelineLayout(layoutDesc);
                if (!outPipelineLayout)
                {
                    MR_LOG(LogTemp, Error, "[SplatRender] Failed to create pipeline layout");
                    return false;
                }
            }

            // Create compute pipeline
            ComputePipelineStateDesc pipeDesc;
            pipeDesc.computeShader = computeShader;
            pipeDesc.pipelineLayout = outPipelineLayout;
            pipeDesc.debugName = "SplatRender";

            outPipeline = device->createComputePipelineState(pipeDesc);
            if (!outPipeline)
            {
                MR_LOG(LogTemp, Error, "[SplatRender] Failed to create compute pipeline");
                return false;
            }

            return true;
        }
    } // anonymous namespace

    // ========================================================================
    // FSplatRenderPass
    // ========================================================================

    FSplatRenderPass::~FSplatRenderPass()
    {
        m_outputTexture.Reset();
        for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
            m_descriptorSets[i].Reset();
        m_pipelineState.Reset();
        m_pipelineLayout.Reset();
        m_setLayout.Reset();
    }

    bool FSplatRenderPass::createOutputTexture(RHI::IRHIDevice* device)
    {
        using namespace RHI;

        TextureDesc desc;
        desc.width    = m_imageWidth;
        desc.height   = m_imageHeight;
        desc.depth    = 1;
        desc.mipLevels = 1;
        desc.arraySize = 1;
        desc.format   = EPixelFormat::R8G8B8A8_UNORM;
        desc.usage    = EResourceUsage::UnorderedAccess | EResourceUsage::ShaderResource;
        desc.debugName = "SplatRender_Output";

        m_outputTexture = device->createTexture(desc);
        if (!m_outputTexture)
        {
            MR_LOG(LogTemp, Error, "[SplatRender] Failed to create output texture %ux%u",
                   m_imageWidth, m_imageHeight);
            return false;
        }

        MR_LOG(LogTemp, Log, "[SplatRender] Output texture created: %ux%u rgba8",
               m_imageWidth, m_imageHeight);
        return true;
    }

    bool FSplatRenderPass::createPipeline(RHI::IRHIDevice* device)
    {
        using namespace RHI;

        // Descriptor set layout: 5 storage buffers + 1 storage image
        {
            FDescriptorSetLayoutDesc desc(0, "SplatRender_Set");
            desc.bindings.Add(FDescriptorSetLayoutBinding(
                ERenderBinding::TileRanges,   EDescriptorType::StorageBuffer,  EShaderStage::Compute));
            desc.bindings.Add(FDescriptorSetLayoutBinding(
                ERenderBinding::SortedIds,    EDescriptorType::StorageBuffer,  EShaderStage::Compute));
            desc.bindings.Add(FDescriptorSetLayoutBinding(
                ERenderBinding::Colors,       EDescriptorType::StorageBuffer,  EShaderStage::Compute));
            desc.bindings.Add(FDescriptorSetLayoutBinding(
                ERenderBinding::ConicOpacity, EDescriptorType::StorageBuffer,  EShaderStage::Compute));
            desc.bindings.Add(FDescriptorSetLayoutBinding(
                ERenderBinding::PointsXY,     EDescriptorType::StorageBuffer,  EShaderStage::Compute));
            desc.bindings.Add(FDescriptorSetLayoutBinding(
                ERenderBinding::OutputImage,  EDescriptorType::StorageTexture, EShaderStage::Compute));

            m_setLayout = device->createDescriptorSetLayout(desc);
            if (!m_setLayout)
            {
                MR_LOG(LogTemp, Error, "[SplatRender] Failed to create descriptor set layout");
                return false;
            }
        }

        // Pipeline
        if (!loadAndCreateComputePipeline(
                device,
                "Shaders/Splat/Render/compiled/splat_render.spv",
                m_setLayout,
                m_pipelineLayout,
                m_pipelineState))
        {
            return false;
        }

        MR_LOG(LogTemp, Log, "[SplatRender] Pipeline created successfully");
        return true;
    }

    bool FSplatRenderPass::initialize(RHI::IRHIDevice* device, uint32 width, uint32 height)
    {
        using namespace RHI;

        if (!device || width == 0 || height == 0)
        {
            MR_LOG(LogTemp, Error, "[SplatRender] Invalid parameters");
            return false;
        }

        m_imageWidth  = width;
        m_imageHeight = height;

        if (!createOutputTexture(device) || !createPipeline(device))
            return false;

        // Allocate per-frame descriptor sets (double-buffered)
        // Pre-bind output texture (unchanged across frames)
        for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
        {
            m_descriptorSets[i] = device->allocateDescriptorSet(m_setLayout);
            if (!m_descriptorSets[i])
            {
                MR_LOG(LogTemp, Error, "[SplatRender] Failed to allocate descriptor set %u", i);
                return false;
            }
            m_descriptorSets[i]->updateStorageImage(ERenderBinding::OutputImage, m_outputTexture);
        }

        m_bInitialized = true;
        MR_LOG(LogTemp, Log, "[SplatRender] Initialized for %ux%u", width, height);
        return true;
    }

    void FSplatRenderPass::setTileRanges(const MonsterEngine::TSharedPtr<RHI::IRHIBuffer>& buffer)
    {
        m_tileRangesBuf = buffer;
        // Only update the descriptor set that will be used by the next execute() call.
        // Updating ALL per-frame sets would touch the set still in use by the previous
        // frame's pending command buffer (VUID-03047).
        uint32 nextIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
        if (m_descriptorSets[nextIndex] && m_tileRangesBuf)
            m_descriptorSets[nextIndex]->updateStorageBuffer(ERenderBinding::TileRanges, m_tileRangesBuf, 0, 0);
    }

    void FSplatRenderPass::setSortedIds(const MonsterEngine::TSharedPtr<RHI::IRHIBuffer>& buffer)
    {
        m_sortedIdsBuf = buffer;
        uint32 nextIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
        if (m_descriptorSets[nextIndex] && m_sortedIdsBuf)
            m_descriptorSets[nextIndex]->updateStorageBuffer(ERenderBinding::SortedIds, m_sortedIdsBuf, 0, 0);
    }

    void FSplatRenderPass::setColors(const MonsterEngine::TSharedPtr<RHI::IRHIBuffer>& buffer)
    {
        m_colorsBuf = buffer;
        uint32 nextIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
        if (m_descriptorSets[nextIndex] && m_colorsBuf)
            m_descriptorSets[nextIndex]->updateStorageBuffer(ERenderBinding::Colors, m_colorsBuf, 0, 0);
    }

    void FSplatRenderPass::setConicOpacity(const MonsterEngine::TSharedPtr<RHI::IRHIBuffer>& buffer)
    {
        m_conicOpacityBuf = buffer;
        uint32 nextIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
        if (m_descriptorSets[nextIndex] && m_conicOpacityBuf)
            m_descriptorSets[nextIndex]->updateStorageBuffer(ERenderBinding::ConicOpacity, m_conicOpacityBuf, 0, 0);
    }

    void FSplatRenderPass::setPointsXY(const MonsterEngine::TSharedPtr<RHI::IRHIBuffer>& buffer)
    {
        m_pointsXYBuf = buffer;
        uint32 nextIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
        if (m_descriptorSets[nextIndex] && m_pointsXYBuf)
            m_descriptorSets[nextIndex]->updateStorageBuffer(ERenderBinding::PointsXY, m_pointsXYBuf, 0, 0);
    }

    void FSplatRenderPass::execute(RHI::IRHICommandList* cmdList)
    {
        using namespace RHI;

        if (!m_bInitialized || !cmdList)
            return;

        // Toggle per-frame descriptor set index
        m_currentDsIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
        auto& ds = m_descriptorSets[m_currentDsIndex];

        // Push constants
        FRenderPushConstants pc;
        pc.imageWidth  = m_imageWidth;
        pc.imageHeight = m_imageHeight;

        // Dispatch groups: one 16x16 workgroup per tile
        uint32 tilesX = (m_imageWidth  + 15u) / 16u;
        uint32 tilesY = (m_imageHeight + 15u) / 16u;

        cmdList->setPipelineState(m_pipelineState);
        cmdList->bindDescriptorSet(m_pipelineLayout, 0, ds);
        cmdList->pushConstants(m_pipelineLayout, EShaderStage::Compute, 0,
                               sizeof(FRenderPushConstants), &pc);
        cmdList->dispatch(tilesX, tilesY, 1);

        MR_LOG(LogTemp, Log, "[SplatRender] Dispatched %ux%u WG (%u tiles)",
               tilesX, tilesY, tilesX * tilesY);
#if 0  // Set to 1 to re-enable per-frame stderr render diagnostics
        fprintf(stderr, "[STDERR] SplatRender: dispatch %ux%u WG (%u tiles) img=%ux%u dsIdx=%u\n",
                tilesX, tilesY, tilesX * tilesY, m_imageWidth, m_imageHeight, m_currentDsIndex);
#endif
    }

} // namespace MonsterRender::Splat
