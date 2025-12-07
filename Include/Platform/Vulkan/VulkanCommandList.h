#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHICommandList.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

namespace MonsterRender::RHI::Vulkan {

// Use MonsterEngine containers
using MonsterEngine::TArray;
using MonsterEngine::TMap;
    
    // Forward declarations
    class VulkanDevice;
    
    /**
     * Vulkan implementation of command list
     */
    class VulkanCommandList : public IRHICommandList {
    public:
        VulkanCommandList(VulkanDevice* device);
        virtual ~VulkanCommandList();
        
        // Non-copyable, non-movable
        VulkanCommandList(const VulkanCommandList&) = delete;
        VulkanCommandList& operator=(const VulkanCommandList&) = delete;
        VulkanCommandList(VulkanCommandList&&) = delete;
        VulkanCommandList& operator=(VulkanCommandList&&) = delete;
        
        // Initialization
        bool initialize();
        
        /**
         * Bind external command buffer (for use with per-frame command buffer system)
         * This allows VulkanCommandList to use an external VkCommandBuffer
         * @param cmdBuffer External command buffer to use
         * @param externallyManaged If true, VulkanCommandList won't free this buffer on destroy
         */
        void bindExternalCommandBuffer(VkCommandBuffer cmdBuffer, bool externallyManaged = true);
        
        /**
         * Check if using external command buffer
         */
        bool isUsingExternalCommandBuffer() const { return m_externalCommandBuffer; }
        
        // IRHICommandList interface implementation
        void begin() override;
        void end() override;
        void reset() override;
        
        // IRHICommandList interface implementation
        void setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) override;
        void setVertexBuffers(uint32 startSlot, TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) override;
        void setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit = true) override;
        
        // Resource binding methods (IRHICommandList interface)
        void setConstantBuffer(uint32 slot, TSharedPtr<IRHIBuffer> buffer) override;
        void setShaderResource(uint32 slot, TSharedPtr<IRHITexture> texture) override;
        void setSampler(uint32 slot, TSharedPtr<IRHISampler> sampler) override;
        
        void setViewport(const Viewport& viewport) override;
        void setScissorRect(const ScissorRect& scissorRect) override;
        void setRenderTargets(TSpan<TSharedPtr<IRHITexture>> renderTargets, 
                            TSharedPtr<IRHITexture> depthStencil = nullptr) override;
        
        /**
         * End the active render pass (must call after setRenderTargets and before next render pass)
         */
        void endRenderPass();
        
        void draw(uint32 vertexCount, uint32 startVertexLocation = 0) override;
        void drawIndexed(uint32 indexCount, uint32 startIndexLocation = 0, 
                        int32 baseVertexLocation = 0) override;
        void drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                          uint32 startVertexLocation = 0, uint32 startInstanceLocation = 0) override;
        void drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount,
                                 uint32 startIndexLocation = 0, int32 baseVertexLocation = 0,
                                 uint32 startInstanceLocation = 0) override;
        
        void clearRenderTarget(TSharedPtr<IRHITexture> renderTarget, 
                             const float32 clearColor[4]) override;
        void clearDepthStencil(TSharedPtr<IRHITexture> depthStencil, 
                             bool clearDepth = true, bool clearStencil = false,
                             float32 depth = 1.0f, uint8 stencil = 0) override;
        
        void transitionResource(TSharedPtr<IRHIResource> resource, 
                              EResourceUsage stateBefore, EResourceUsage stateAfter) override;
        void resourceBarrier() override;
        
        // Debug and profiling
        void beginEvent(const String& name) override;
        void endEvent() override;
        void setMarker(const String& name) override;
        
        // UE5-style resource binding APIs
        /**
         * Set shader uniform buffer (UE5: SetShaderUniformBuffer)
         * @param slot Binding slot index
         * @param buffer Uniform buffer to bind
         */
        void setShaderUniformBuffer(uint32 slot, TSharedPtr<IRHIBuffer> buffer);
        
        /**
         * Set shader texture (UE5: SetShaderTexture)
         * @param slot Binding slot index
         * @param texture Texture to bind
         */
        void setShaderTexture(uint32 slot, TSharedPtr<IRHITexture> texture);
        
        /**
         * Set shader sampler (UE5: SetShaderSampler)
         * @param slot Binding slot index
         * @param sampler Sampler to bind (currently combined with texture)
         */
        void setShaderSampler(uint32 slot, TSharedPtr<IRHITexture> texture);
        
        // ============================================================================
        // Texture Upload Operations (UE5-style RHI texture transfer)
        // ============================================================================
        
        /**
         * Copy buffer data to texture (UE5: CopyToTexture)
         * Used for uploading texture data from staging buffer to GPU texture
         * 
         * @param srcBuffer Source buffer containing texture data
         * @param srcOffset Offset in source buffer
         * @param dstTexture Destination texture
         * @param mipLevel Target mip level
         * @param arrayLayer Target array layer
         * @param width Width of region to copy
         * @param height Height of region to copy
         * @param depth Depth of region to copy
         */
        void copyBufferToTexture(
            TSharedPtr<IRHIBuffer> srcBuffer,
            uint64 srcOffset,
            TSharedPtr<IRHITexture> dstTexture,
            uint32 mipLevel,
            uint32 arrayLayer,
            uint32 width,
            uint32 height,
            uint32 depth = 1
        );
        
        /**
         * Transition texture layout (UE5: TransitionTexture)
         * Changes image layout and inserts necessary pipeline barriers
         * 
         * @param texture Texture to transition
         * @param oldLayout Old layout
         * @param newLayout New layout
         * @param srcAccessMask Source access mask
         * @param dstAccessMask Destination access mask
         * @param srcStageMask Source pipeline stage
         * @param dstStageMask Destination pipeline stage
         */
        void transitionTextureLayout(
            TSharedPtr<IRHITexture> texture,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            VkAccessFlags srcAccessMask,
            VkAccessFlags dstAccessMask,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask
        );
        
        /**
         * Simplified texture layout transition with common defaults
         * 
         * @param texture Texture to transition
         * @param oldLayout Old layout
         * @param newLayout New layout
         */
        void transitionTextureLayoutSimple(
            TSharedPtr<IRHITexture> texture,
            VkImageLayout oldLayout,
            VkImageLayout newLayout
        );
        
        // Vulkan-specific getters
        VkCommandBuffer getCommandBuffer() const { return m_commandBuffer; }
        VkCommandBuffer getVulkanCommandBuffer() const { return m_commandBuffer; }
        bool isRecording() const { return m_isRecording; }
        
    private:
        // Helper methods
        void ensureNotRecording(const char* operation) const;
        void ensureRecording(const char* operation) const;
        void updateAndBindDescriptorSets();
        
    private:
        VulkanDevice* m_device = nullptr;
        VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
        
        bool m_isRecording = false;
        bool m_inRenderPass = false;
        bool m_externalCommandBuffer = false;  // Track if using external command buffer
        
        // State tracking
        TSharedPtr<IRHIPipelineState> m_currentPipelineState;
        TArray<TSharedPtr<IRHITexture>> m_boundRenderTargets;
        TSharedPtr<IRHITexture> m_boundDepthStencil;
        
        // Resource binding state (for descriptor sets)
        struct BoundResource {
            TSharedPtr<IRHIBuffer> buffer;
            TSharedPtr<IRHITexture> texture;
            TSharedPtr<IRHISampler> sampler;
            bool isDirty = true;
        };
        TMap<uint32, BoundResource> m_boundResources; // slot -> resource
        TMap<uint32, TSharedPtr<IRHISampler>> m_boundSamplers; // slot -> sampler
        VkDescriptorSet m_currentDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
        bool m_descriptorsDirty = true; // Track if descriptors need to be updated
        
        // Debug state
        uint32 m_eventDepth = 0;
        bool m_debugUtilsAvailable = false;
    };
}
