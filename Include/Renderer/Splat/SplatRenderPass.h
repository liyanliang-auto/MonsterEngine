/**
 * @file SplatRenderPass.h
 * @brief Per-tile EWA alpha blend render pass for 3D Gaussian Splatting
 * 
 * Wraps splat_render.comp: 16x16 tile workgroup, reads sorted Gaussian list
 * from sort pipeline, performs front-to-back alpha blending, writes to a
 * storage image for subsequent display.
 */

#pragma once

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/IRHIResource.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHIDescriptorSet.h"

namespace MonsterRender::Splat
{
    // Shader binding points for splat_render.comp
    namespace ERenderBinding
    {
        constexpr uint32 TileRanges    = 0;  // uvec2 ranges[tileID]
        constexpr uint32 SortedIds     = 1;  // uint sortedIds[]
        constexpr uint32 Colors        = 2;  // vec4 colors[]
        constexpr uint32 ConicOpacity  = 3;  // vec4 conicOpacity[]
        constexpr uint32 PointsXY      = 4;  // vec2 pointsXY[]
        constexpr uint32 OutputImage   = 5;  // writeonly image2D (rgba8)
    }

    /** Push constants for splat_render.comp */
    struct FRenderPushConstants
    {
        uint32 imageWidth;
        uint32 imageHeight;
    };

    /**
     * FSplatRenderPass
     * 
     * Owns the output storage image and the compute pipeline for the EWA
     * alpha blend render. Accepts input buffers from the sort pipeline
     * (tile ranges, sorted IDs, preprocess outputs).
     */
    class FSplatRenderPass
    {
    public:
        FSplatRenderPass() = default;
        ~FSplatRenderPass();

        /**
         * Initialize the render pass: create output texture, descriptor layout,
         * pipeline layout, and compute pipeline.
         * 
         * @param device RHI device
         * @param width  Output image width in pixels
         * @param height Output image height in pixels
         * @return true on success
         */
        bool initialize(RHI::IRHIDevice* device, uint32 width, uint32 height);

        /**
         * Set input buffers for the render pass.
         * Must be called before each execute() to bind the latest sorted data.
         */
        void setTileRanges(const MonsterEngine::TSharedPtr<RHI::IRHIBuffer>& buffer);
        void setSortedIds(const MonsterEngine::TSharedPtr<RHI::IRHIBuffer>& buffer);
        void setColors(const MonsterEngine::TSharedPtr<RHI::IRHIBuffer>& buffer);
        void setConicOpacity(const MonsterEngine::TSharedPtr<RHI::IRHIBuffer>& buffer);
        void setPointsXY(const MonsterEngine::TSharedPtr<RHI::IRHIBuffer>& buffer);

        /**
         * Execute the render dispatch.
         * Dispatches one 16x16 workgroup per tile.
         * 
         * @param cmdList Command list to record into
         */
        void execute(RHI::IRHICommandList* cmdList);

        /** Access the output texture (for display / further processing) */
        MonsterEngine::TSharedPtr<RHI::IRHITexture> getOutputTexture() const { return m_outputTexture; }

        bool isInitialized() const { return m_bInitialized; }
        uint32 getWidth()  const { return m_imageWidth; }
        uint32 getHeight() const { return m_imageHeight; }

    private:
        bool createPipeline(RHI::IRHIDevice* device);
        bool createOutputTexture(RHI::IRHIDevice* device);

        bool m_bInitialized = false;
        uint32 m_imageWidth  = 0;
        uint32 m_imageHeight = 0;

        // Output storage image
        MonsterEngine::TSharedPtr<RHI::IRHITexture> m_outputTexture;

        // Descriptor sets (per-frame double-buffered to avoid updating
        // while command buffer is pending)
        MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> m_setLayout;
        MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout>      m_pipelineLayout;
        MonsterEngine::TSharedPtr<RHI::IRHIPipelineState>        m_pipelineState;
        static constexpr uint32 kMaxFramesInFlight = 2;
        MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet>        m_descriptorSets[kMaxFramesInFlight];
        uint32 m_currentDsIndex = 0;

        // Input buffers (stored for per-frame descriptor update in execute)
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_tileRangesBuf;
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_sortedIdsBuf;
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_colorsBuf;
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_conicOpacityBuf;
        MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_pointsXYBuf;
    };

} // namespace MonsterRender::Splat
