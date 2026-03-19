#pragma once

#include "Core/CoreMinimal.h"
#include "Core/FGraphEvent.h"
#include "Core/Assert.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/IRHIResource.h"
#include "RHI/RHIResources.h"
#include "RHI/IRHIDescriptorSet.h"
#include "RDG/RDGDefinitions.h"
#include <atomic>

namespace MonsterRender::RHI {
    
    /**
     * Command list state enumeration
     * Tracks the lifecycle of a command list from allocation to execution
     * Reference: UE5 command list state management
     */
    enum class ERHICommandListState : uint8 {
        NotAllocated,    // Command list has not been allocated from pool
        Recording,       // Currently recording commands
        Recorded,        // Recording finished, ready for submission
        Executing,       // Currently being executed on RHI thread
        Executed         // Execution completed
    };
    
    /**
     * Recording thread type
     * Identifies which thread is recording commands
     * Reference: UE5 ERecordingThread
     */
    enum class ERecordingThread : uint8 {
        Render,          // Main render thread
        Any              // Any worker thread (for parallel recording)
    };
    
	//IRHICommandList（命令列表接口）
	//	├── 生命周期：begin / end / reset
	//	├── 状态绑定：setPipelineState / setViewport / setScissorRect
	//	├── 资源绑定：setVertexBuffers / setIndexBuffer / setConstantBuffer
	//	│            setShaderResource / setSampler / bindDescriptorSets / pushConstants
	//	├── 绘制命令：draw / drawIndexed / drawInstanced / drawIndexedInstanced
	//	├── 渲染目标：setRenderTargets / endRenderPass / clearRenderTarget / clearDepthStencil
	//	├── 资源屏障：transitionResource / resourceBarrier
	//	└── 调试支持：beginEvent / endEvent / setMarker

    // Forward declarations
    class IRHIDevice;
    
    /**
     * Command list interface for recording GPU commands
     * Follows a deferred execution model similar to D3D12/Vulkan
     * 
     * Enhanced for parallel recording support:
     * - State tracking (NotAllocated -> Recording -> Recorded -> Executing -> Executed)
     * - Dispatch prerequisites for dependency management
     * - Completion events for synchronization
     * - Thread-safe parallel recording
     * 
     * Reference: UE5 FRHICommandListBase
     */
    class IRHICommandList {
    public:
        IRHICommandList()
            : m_state(ERHICommandListState::NotAllocated)
            , m_recordingThread(ERecordingThread::Render)
            , m_uid(s_nextUID.fetch_add(1, std::memory_order_relaxed))
        {}
        
        virtual ~IRHICommandList() = default;
        
        // Non-copyable, movable
        IRHICommandList(const IRHICommandList&) = delete;
        IRHICommandList& operator=(const IRHICommandList&) = delete;
        IRHICommandList(IRHICommandList&&) = default;
        IRHICommandList& operator=(IRHICommandList&&) = default;
        
        // ========================================================================
        // Lifecycle Management
        // ========================================================================
        
        /**
         * Begin recording commands
         * Must be called before any draw/dispatch commands
         * Transitions state: NotAllocated/Executed -> Recording
         */
        virtual void begin() {
            m_state = ERHICommandListState::Recording;
        }
        
        /**
         * End recording commands
         * After this call, the command list can be submitted for execution
         * Transitions state: Recording -> Recorded
         */
        virtual void end() {
            m_state = ERHICommandListState::Recorded;
        }
        
        /**
         * Reset the command list for reuse
         * Clears all recorded commands and resets state
         * Transitions state: Any -> NotAllocated
         */
        virtual void reset() {
            m_state = ERHICommandListState::NotAllocated;
            m_dispatchPrerequisites.clear();
            m_completionEvent.reset();
        }
        
        /**
         * Finish recording and mark ready for dispatch
         * Must be called as the last command in a parallel rendering task
         * Not safe to continue using the command list after this call
         * 
         * Reference: UE5 FRHICommandListBase::FinishRecording()
         * 
         * @note Never call on the immediate command list
         */
        virtual void FinishRecording() {
            MR_ASSERT(m_state == ERHICommandListState::Recording);
            m_state = ERHICommandListState::Recorded;
            
            // Signal completion event if it exists
            if (m_completionEvent) {
                m_completionEvent->Complete();
            }
        }
        
        // ========================================================================
        // State Query
        // ========================================================================
        
        /**
         * Check if command list is currently recording
         */
        bool IsRecording() const {
            return m_state == ERHICommandListState::Recording;
        }
        
        /**
         * Check if command list has finished recording
         */
        bool IsRecorded() const {
            return m_state == ERHICommandListState::Recorded;
        }
        
        /**
         * Check if command list is currently executing
         */
        bool IsExecuting() const {
            return m_state == ERHICommandListState::Executing;
        }
        
        /**
         * Get current command list state
         */
        ERHICommandListState GetState() const {
            return m_state;
        }
        
        /**
         * Get unique identifier for this command list
         */
        uint32 GetUID() const {
            return m_uid;
        }
        
        /**
         * Get recording thread type
         */
        ERecordingThread GetRecordingThread() const {
            return m_recordingThread;
        }
        
        // ========================================================================
        // Dispatch Prerequisites and Events
        // ========================================================================
        
        /**
         * Add a graph event as a dispatch dependency
         * The command list will not be dispatched to the RHI thread until
         * all its dispatch prerequisites have been completed
         * 
         * Reference: UE5 FRHICommandListBase::AddDispatchPrerequisite()
         * 
         * @param Prereq Graph event that must complete before dispatch
         * @note Not safe to call after FinishRecording()
         */
        void AddDispatchPrerequisite(const MonsterEngine::FGraphEventRef& Prereq) {
            MR_ASSERT(m_state == ERHICommandListState::Recording);
            if (Prereq) {
                m_dispatchPrerequisites.push_back(Prereq);
            }
        }
        
        /**
         * Get all dispatch prerequisites
         */
        const MonsterEngine::FGraphEventArray& GetDispatchPrerequisites() const {
            return m_dispatchPrerequisites;
        }
        
        /**
         * Set the completion event for this command list
         * This event will be signaled when the command list finishes execution
         */
        void SetCompletionEvent(const MonsterEngine::FGraphEventRef& Event) {
            m_completionEvent = Event;
        }
        
        /**
         * Get the completion event for this command list
         */
        MonsterEngine::FGraphEventRef GetCompletionEvent() const {
            return m_completionEvent;
        }
        
        // ========================================================================
        // Resource Binding
        // ========================================================================
        
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
        
        // ========================================================================
        // Multi-descriptor set binding (New API)
        // ========================================================================
        
        /**
         * Bind multiple descriptor sets to the pipeline
         * This is the new API for multi-descriptor set support
         * Reference: UE5 RHISetDescriptorSets / Vulkan vkCmdBindDescriptorSets
         * 
         * @param pipelineLayout Pipeline layout defining the descriptor set layouts
         * @param firstSet First set number to bind (typically 0)
         * @param descriptorSets Array of descriptor sets to bind
         */
        virtual void bindDescriptorSets(TSharedPtr<IRHIPipelineLayout> pipelineLayout,
                                       uint32 firstSet,
                                       TSpan<TSharedPtr<IRHIDescriptorSet>> descriptorSets) {
            // Default implementation - platforms override as needed
            (void)pipelineLayout; (void)firstSet; (void)descriptorSets;
        }
        
        /**
         * Bind a single descriptor set to the pipeline
         * Convenience method for binding one descriptor set
         * Reference: UE5 RHISetDescriptorSet
         * 
         * @param pipelineLayout Pipeline layout defining the descriptor set layouts
         * @param setIndex Set index to bind (0, 1, 2, ...)
         * @param descriptorSet Descriptor set to bind
         */
        virtual void bindDescriptorSet(TSharedPtr<IRHIPipelineLayout> pipelineLayout,
                                      uint32 setIndex,
                                      TSharedPtr<IRHIDescriptorSet> descriptorSet) {
            // Default implementation - call bindDescriptorSets with single set
            TArray<TSharedPtr<IRHIDescriptorSet>> sets = { descriptorSet };
            bindDescriptorSets(pipelineLayout, setIndex, TSpan<TSharedPtr<IRHIDescriptorSet>>(sets));
        }
        
        /**
         * Push constants to the pipeline (fast uniform updates)
         * Reference: Vulkan vkCmdPushConstants / UE5 push constants
         * 
         * @param pipelineLayout Pipeline layout defining push constant ranges
         * @param shaderStages Shader stages to update
         * @param offset Offset in bytes into the push constant block
         * @param size Size in bytes of the data
         * @param data Pointer to the data
         */
        virtual void pushConstants(TSharedPtr<IRHIPipelineLayout> pipelineLayout,
                                  EShaderStage shaderStages,
                                  uint32 offset,
                                  uint32 size,
                                  const void* data) {
            // Default implementation - platforms override as needed
            (void)pipelineLayout; (void)shaderStages; (void)offset; (void)size; (void)data;
        }
        
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
         * Transition resource state (legacy EResourceUsage version)
         * This is a no-op for implicit APIs like D3D11/OpenGL
         */
        virtual void transitionResource(TSharedPtr<IRHIResource> resource, 
                                      EResourceUsage stateBefore, EResourceUsage stateAfter) = 0;
        
        /**
         * Transition resource state (ERHIAccess version for RDG)
         * This is a no-op for implicit APIs like D3D11/OpenGL
         */
        virtual void transitionResource(TSharedPtr<IRHIResource> resource, 
                                      MonsterRender::RDG::ERHIAccess stateBefore, 
                                      MonsterRender::RDG::ERHIAccess stateAfter) {
            // Default implementation - can be overridden by platform-specific command lists
            (void)resource; (void)stateBefore; (void)stateAfter;
        }
        
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
        
    protected:
        /**
         * Set command list state (for derived classes)
         */
        void SetState(ERHICommandListState NewState) {
            m_state = NewState;
        }
        
        /**
         * Set recording thread type (for derived classes)
         */
        void SetRecordingThread(ERecordingThread Thread) {
            m_recordingThread = Thread;
        }
        
    private:
        /** Current state of the command list */
        ERHICommandListState m_state;
        
        /** Thread that is recording commands */
        ERecordingThread m_recordingThread;
        
        /** Unique identifier for this command list */
        uint32 m_uid;
        
        /** Graph events that must complete before this command list can be dispatched */
        MonsterEngine::FGraphEventArray m_dispatchPrerequisites;
        
        /** Event that will be signaled when this command list completes execution */
        MonsterEngine::FGraphEventRef m_completionEvent;
        
        /** Global counter for generating unique IDs */
        static std::atomic<uint32> s_nextUID;
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
