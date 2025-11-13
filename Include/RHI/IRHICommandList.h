#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/IRHIResource.h"

namespace MonsterRender::RHI {
    
    // Forward declarations
    class IRHIDevice;
    
    /**
     * Command list interface for recording GPU commands
     * Follows a deferred execution model similar to D3D12/Vulkan
     */
    class IRHICommandList {
    public:
        IRHICommandList() = default;
        virtual ~IRHICommandList() = default;
        
        // Non-copyable, movable
        IRHICommandList(const IRHICommandList&) = delete;
        IRHICommandList& operator=(const IRHICommandList&) = delete;
        IRHICommandList(IRHICommandList&&) = default;
        IRHICommandList& operator=(IRHICommandList&&) = default;
        
        /**
         * Begin recording commands
         * Must be called before any draw/dispatch commands
         */
        virtual void begin() = 0;
        
        /**
         * End recording commands
         * After this call, the command list can be submitted for execution
         */
        virtual void end() = 0;
        
        /**
         * Reset the command list for reuse
         */
        virtual void reset() = 0;
        
        // Resource binding
        /**
         * Set the graphics pipeline state
         */
        virtual void setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) = 0;
        
        /**
         * Set vertex buffers
         */
        virtual void setVertexBuffers(uint32 startSlot, TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) = 0;
        
        /**
         * Set index buffer
         */
        virtual void setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit = true) = 0;
        
        // Render state
        /**
         * Set viewport
         */
        virtual void setViewport(const Viewport& viewport) = 0;
        
        /**
         * Set scissor rectangle
         */
        virtual void setScissorRect(const ScissorRect& scissorRect) = 0;
        
        /**
         * Set render targets and begin render pass
         */
        virtual void setRenderTargets(TSpan<TSharedPtr<IRHITexture>> renderTargets, 
                                    TSharedPtr<IRHITexture> depthStencil = nullptr) = 0;
        
        /**
         * End the active render pass
         */
        virtual void endRenderPass() = 0;
        
        // Draw commands
        /**
         * Draw primitives
         */
        virtual void draw(uint32 vertexCount, uint32 startVertexLocation = 0) = 0;
        
        /**
         * Draw indexed primitives
         */
        virtual void drawIndexed(uint32 indexCount, uint32 startIndexLocation = 0, 
                               int32 baseVertexLocation = 0) = 0;
        
        /**
         * Draw instanced primitives
         */
        virtual void drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                                 uint32 startVertexLocation = 0, uint32 startInstanceLocation = 0) = 0;
        
        /**
         * Draw indexed instanced primitives
         */
        virtual void drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount,
                                        uint32 startIndexLocation = 0, int32 baseVertexLocation = 0,
                                        uint32 startInstanceLocation = 0) = 0;
        
        // Clear commands
        /**
         * Clear render target
         */
        virtual void clearRenderTarget(TSharedPtr<IRHITexture> renderTarget, 
                                     const float32 clearColor[4]) = 0;
        
        /**
         * Clear depth stencil
         */
        virtual void clearDepthStencil(TSharedPtr<IRHITexture> depthStencil, 
                                     bool clearDepth = true, bool clearStencil = false,
                                     float32 depth = 1.0f, uint8 stencil = 0) = 0;
        
        // Resource transitions (for explicit APIs like D3D12/Vulkan)
        /**
         * Transition resource state
         * This is a no-op for implicit APIs like D3D11/OpenGL
         */
        virtual void transitionResource(TSharedPtr<IRHIResource> resource, 
                                      EResourceUsage stateBefore, EResourceUsage stateAfter) = 0;
        
        // Synchronization
        /**
         * Insert a resource barrier
         */
        virtual void resourceBarrier() = 0;
        
        // Debug support
        /**
         * Begin debug event (for profiling/debugging tools like PIX, RenderDoc)
         */
        virtual void beginEvent(const String& name) = 0;
        
        /**
         * End debug event
         */
        virtual void endEvent() = 0;
        
        /**
         * Insert debug marker
         */
        virtual void setMarker(const String& name) = 0;
    };
    
    /**
     * RAII helper for debug events
     */
    class ScopedDebugEvent {
    public:
        ScopedDebugEvent(IRHICommandList* cmdList, const String& name) 
            : m_commandList(cmdList) {
            if (m_commandList) {
                m_commandList->beginEvent(name);
            }
        }
        
        ~ScopedDebugEvent() {
            if (m_commandList) {
                m_commandList->endEvent();
            }
        }
        
    private:
        IRHICommandList* m_commandList;
    };
    
    // Macro for scoped debug events
    #define MR_SCOPED_DEBUG_EVENT(cmdList, name) \
        MonsterRender::RHI::ScopedDebugEvent _scopedEvent(cmdList, name)
}
