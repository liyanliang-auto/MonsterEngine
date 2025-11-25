#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHICommandList.h"

namespace MonsterRender::RHI::Vulkan {
    
    // Forward declarations
    class VulkanDevice;
    class FVulkanCommandListContext;
    
    /**
     * FVulkanRHICommandListImmediate - Vulkan RHI Command List Implementation (UE5 Pattern)
     * 
     * This is the main command list interface that applications use.
     * It's NOT actually immediate - it defers commands and delegates to the active per-frame command buffer.
     * 
     * Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanCommandList.h
     * The key insight: "Immediate" command lists in UE5 are actually deferred - they record commands
     * that get executed later. The actual GPU submission happens in batches.
     * 
     * Architecture:
     * - This class is a facade that provides the RHI interface
     * - It delegates all command recording to the active FVulkanCmdBuffer via FVulkanCommandListContext
     * - It maintains no recording state itself - all state is in the context
     * - Multiple "immediate" command lists can exist, but only one is active per-frame
     */
    class FVulkanRHICommandListImmediate : public IRHICommandList {
    public:
        FVulkanRHICommandListImmediate(VulkanDevice* device);
        virtual ~FVulkanRHICommandListImmediate();
        
        // Non-copyable, non-movable
        FVulkanRHICommandListImmediate(const FVulkanRHICommandListImmediate&) = delete;
        FVulkanRHICommandListImmediate& operator=(const FVulkanRHICommandListImmediate&) = delete;
        FVulkanRHICommandListImmediate(FVulkanRHICommandListImmediate&&) = delete;
        FVulkanRHICommandListImmediate& operator=(FVulkanRHICommandListImmediate&&) = delete;
        
        // IRHICommandList interface - all methods delegate to the context
        void begin() override;
        void end() override;
        void reset() override;
        
        void setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) override;
        void setVertexBuffers(uint32 startSlot, TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) override;
        void setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit = true) override;
        
        // Resource binding methods
        void setConstantBuffer(uint32 slot, TSharedPtr<IRHIBuffer> buffer) override;
        void setShaderResource(uint32 slot, TSharedPtr<IRHITexture> texture) override;
        void setSampler(uint32 slot, TSharedPtr<IRHISampler> sampler) override;
        
        void setViewport(const Viewport& viewport) override;
        void setScissorRect(const ScissorRect& scissorRect) override;
        void setRenderTargets(TSpan<TSharedPtr<IRHITexture>> renderTargets, 
                            TSharedPtr<IRHITexture> depthStencil = nullptr) override;
        
        void endRenderPass() override;
        
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
        
        void beginEvent(const String& name) override;
        void endEvent() override;
        void setMarker(const String& name) override;
        
    private:
        VulkanDevice* m_device;
        FVulkanCommandListContext* m_context;  // Reference to the current context (not owned)
        
        // Helper to get the current context
        FVulkanCommandListContext* getCurrentContext() const;
        
        // Resource binding state (for descriptor sets)
        struct BoundResource {
            TSharedPtr<IRHIBuffer> buffer;
            TSharedPtr<IRHITexture> texture;
            TSharedPtr<IRHISampler> sampler;
            bool isDirty = true;
        };
        TMap<uint32, BoundResource> m_boundResources; // slot -> resource
        bool m_descriptorsDirty = true;
    };
}

