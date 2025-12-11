#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/IRHIResource.h"
#include "RHI/RHIResources.h"

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
         * Set vertex buffers (generic IRHIBuffer)
         */
        virtual void setVertexBuffers(uint32 startSlot, TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) = 0;
        
        /**
         * Set index buffer (generic IRHIBuffer)
         */
        virtual void setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit = true) = 0;
        
        /**
         * Bind vertex buffer for rendering (UE5-style)
         * @param StreamIndex Vertex stream index (0 for position, 1 for tangent, etc.)
         * @param VertexBuffer Vertex buffer to bind
         * @param Offset Offset in bytes from start of buffer
         * @param Stride Stride between vertices in bytes
         */
        virtual void SetStreamSource(uint32 StreamIndex, TSharedPtr<FRHIVertexBuffer> VertexBuffer, 
                                     uint32 Offset = 0, uint32 Stride = 0) {
            // Default implementation - can be overridden by platform-specific command lists
            (void)StreamIndex; (void)VertexBuffer; (void)Offset; (void)Stride;
        }
        
        /**
         * Bind index buffer for rendering (UE5-style)
         * @param IndexBuffer Index buffer to bind
         */
        virtual void SetIndexBuffer(TSharedPtr<FRHIIndexBuffer> IndexBuffer) {
            // Default implementation - can be overridden by platform-specific command lists
            (void)IndexBuffer;
        }
        
        /**
         * Set constant buffer (uniform buffer) at specified slot
         * Similar to D3D12's SetGraphicsRootConstantBufferView
         * @param slot Binding slot matching shader layout(binding = N)
         * @param buffer Constant buffer containing uniform data
         */
        virtual void setConstantBuffer(uint32 slot, TSharedPtr<IRHIBuffer> buffer) = 0;
        
        /**
         * Set shader resource (texture) at specified slot
         * Similar to D3D12's SetGraphicsRootDescriptorTable for SRVs
         * @param slot Binding slot matching shader layout(binding = N)
         * @param texture Texture resource to bind
         */
        virtual void setShaderResource(uint32 slot, TSharedPtr<IRHITexture> texture) = 0;
        
        /**
         * Set sampler at specified slot
         * @param slot Binding slot matching shader layout(binding = N)  
         * @param sampler Sampler state to bind (or nullptr for default sampler)
         */
        virtual void setSampler(uint32 slot, TSharedPtr<IRHISampler> sampler) = 0;
        
        // Render state
        /**
         * Set depth-stencil state
         * @param bDepthTestEnable Enable depth testing
         * @param bDepthWriteEnable Enable depth writing
         * @param CompareFunc Depth comparison function (0=Never, 1=Less, 2=Equal, 3=LessEqual, 4=Greater, 5=NotEqual, 6=GreaterEqual, 7=Always)
         */
        virtual void setDepthStencilState(bool bDepthTestEnable, bool bDepthWriteEnable, uint8 CompareFunc) {
            // Default implementation - platforms override as needed
            (void)bDepthTestEnable; (void)bDepthWriteEnable; (void)CompareFunc;
        }
        
        /**
         * Set blend state
         * @param bBlendEnable Enable blending
         * @param SrcColorBlend Source color blend factor
         * @param DstColorBlend Destination color blend factor
         * @param ColorBlendOp Color blend operation
         * @param SrcAlphaBlend Source alpha blend factor
         * @param DstAlphaBlend Destination alpha blend factor
         * @param AlphaBlendOp Alpha blend operation
         * @param ColorWriteMask Color write mask (RGBA bits)
         */
        virtual void setBlendState(bool bBlendEnable, 
                                   uint8 SrcColorBlend, uint8 DstColorBlend, uint8 ColorBlendOp,
                                   uint8 SrcAlphaBlend, uint8 DstAlphaBlend, uint8 AlphaBlendOp,
                                   uint8 ColorWriteMask) {
            // Default implementation - platforms override as needed
            (void)bBlendEnable; (void)SrcColorBlend; (void)DstColorBlend; (void)ColorBlendOp;
            (void)SrcAlphaBlend; (void)DstAlphaBlend; (void)AlphaBlendOp; (void)ColorWriteMask;
        }
        
        /**
         * Set rasterizer state
         * @param FillMode Fill mode (0=Solid, 1=Wireframe)
         * @param CullMode Cull mode (0=None, 1=Front, 2=Back)
         * @param bFrontCounterClockwise Front face winding
         * @param DepthBias Depth bias value
         * @param SlopeScaledDepthBias Slope scaled depth bias
         */
        virtual void setRasterizerState(uint8 FillMode, uint8 CullMode, bool bFrontCounterClockwise,
                                        float DepthBias, float SlopeScaledDepthBias) {
            // Default implementation - platforms override as needed
            (void)FillMode; (void)CullMode; (void)bFrontCounterClockwise;
            (void)DepthBias; (void)SlopeScaledDepthBias;
        }
        
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
