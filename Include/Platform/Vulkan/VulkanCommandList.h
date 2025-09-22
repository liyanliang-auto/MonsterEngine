#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHICommandList.h"
#include "Platform/Vulkan/VulkanRHI.h"

namespace MonsterRender::RHI::Vulkan {
    
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
        
        // IRHICommandList interface implementation
        void begin() override;
        void end() override;
        void reset() override;
        
        // IRHICommandList interface implementation
        void setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) override;
        void setVertexBuffers(uint32 startSlot, TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) override;
        void setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit = true) override;
        
        void setViewport(const Viewport& viewport) override;
        void setScissorRect(const ScissorRect& scissorRect) override;
        void setRenderTargets(TSpan<TSharedPtr<IRHITexture>> renderTargets, 
                            TSharedPtr<IRHITexture> depthStencil = nullptr) override;
        
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
        
        // Vulkan-specific getters
        VkCommandBuffer getCommandBuffer() const { return m_commandBuffer; }
        VkCommandBuffer getVulkanCommandBuffer() const { return m_commandBuffer; }
        bool isRecording() const { return m_isRecording; }
        
    private:
        // Helper methods
        void ensureNotRecording(const char* operation) const;
        void ensureRecording(const char* operation) const;
        
    private:
        VulkanDevice* m_device = nullptr;
        VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
        
        bool m_isRecording = false;
        bool m_inRenderPass = false;
        
        // State tracking
        TSharedPtr<IRHIPipelineState> m_currentPipelineState;
        TArray<TSharedPtr<IRHITexture>> m_boundRenderTargets;
        TSharedPtr<IRHITexture> m_boundDepthStencil;
        
        // Debug state
        uint32 m_eventDepth = 0;
    };
}
