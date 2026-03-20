#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHICommandList.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Containers/Array.h"
#include <variant>

namespace MonsterRender::RHI::Vulkan {

// Forward declarations
class VulkanDevice;
class FVulkanCommandBuffer;
class FVulkanContext;

/**
 * RHI Command Types
 * Enumeration of all supported RHI commands that can be recorded
 * Reference: UE5 command recording pattern
 */
enum class ERHICommandType : uint8 {
    // Pipeline and state commands
    SetPipelineState,
    SetViewport,
    SetScissorRect,
    SetRenderTargets,
    EndRenderPass,
    
    // Resource binding commands
    SetVertexBuffers,
    SetIndexBuffer,
    SetConstantBuffer,
    SetShaderResource,
    SetSampler,
    BindDescriptorSets,
    PushConstants,
    
    // Draw commands
    Draw,
    DrawIndexed,
    DrawInstanced,
    DrawIndexedInstanced,
    
    // Clear commands
    ClearRenderTarget,
    ClearDepthStencil,
    
    // Resource transition commands
    TransitionResource,
    ResourceBarrier,
    
    // Debug commands
    BeginEvent,
    EndEvent,
    SetMarker
};

/**
 * Base RHI Command Structure
 * All recorded commands derive from this base
 */
struct FRHICommandBase {
    ERHICommandType type;
    
    explicit FRHICommandBase(ERHICommandType inType) : type(inType) {}
    virtual ~FRHICommandBase() = default;
};

/**
 * Specific RHI Command Structures
 * Each command type has its own parameter structure
 */

// Pipeline State Command
struct FRHICommandSetPipelineState : FRHICommandBase {
    TSharedPtr<IRHIPipelineState> pipelineState;
    
    FRHICommandSetPipelineState(TSharedPtr<IRHIPipelineState> ps)
        : FRHICommandBase(ERHICommandType::SetPipelineState)
        , pipelineState(ps) {}
};

// Viewport Command
struct FRHICommandSetViewport : FRHICommandBase {
    Viewport viewport;
    
    FRHICommandSetViewport(const Viewport& vp)
        : FRHICommandBase(ERHICommandType::SetViewport)
        , viewport(vp) {}
};

// Scissor Rect Command
struct FRHICommandSetScissorRect : FRHICommandBase {
    ScissorRect scissorRect;
    
    FRHICommandSetScissorRect(const ScissorRect& sr)
        : FRHICommandBase(ERHICommandType::SetScissorRect)
        , scissorRect(sr) {}
};

// Vertex Buffers Command
struct FRHICommandSetVertexBuffers : FRHICommandBase {
    uint32 startSlot;
    MonsterEngine::TArray<TSharedPtr<IRHIBuffer>> vertexBuffers;
    
    FRHICommandSetVertexBuffers(uint32 slot, TSpan<TSharedPtr<IRHIBuffer>> buffers)
        : FRHICommandBase(ERHICommandType::SetVertexBuffers)
        , startSlot(slot) {
        for (const auto& buf : buffers) {
            vertexBuffers.push_back(buf);
        }
    }
};

// Index Buffer Command
struct FRHICommandSetIndexBuffer : FRHICommandBase {
    TSharedPtr<IRHIBuffer> indexBuffer;
    bool is32Bit;
    
    FRHICommandSetIndexBuffer(TSharedPtr<IRHIBuffer> ib, bool is32)
        : FRHICommandBase(ERHICommandType::SetIndexBuffer)
        , indexBuffer(ib)
        , is32Bit(is32) {}
};

// Draw Command
struct FRHICommandDraw : FRHICommandBase {
    uint32 vertexCount;
    uint32 startVertexLocation;
    
    FRHICommandDraw(uint32 count, uint32 start)
        : FRHICommandBase(ERHICommandType::Draw)
        , vertexCount(count)
        , startVertexLocation(start) {}
};

// Draw Indexed Command
struct FRHICommandDrawIndexed : FRHICommandBase {
    uint32 indexCount;
    uint32 startIndexLocation;
    int32 baseVertexLocation;
    
    FRHICommandDrawIndexed(uint32 count, uint32 start, int32 base)
        : FRHICommandBase(ERHICommandType::DrawIndexed)
        , indexCount(count)
        , startIndexLocation(start)
        , baseVertexLocation(base) {}
};

// Draw Instanced Command
struct FRHICommandDrawInstanced : FRHICommandBase {
    uint32 vertexCountPerInstance;
    uint32 instanceCount;
    uint32 startVertexLocation;
    uint32 startInstanceLocation;
    
    FRHICommandDrawInstanced(uint32 vertCount, uint32 instCount, uint32 startVert, uint32 startInst)
        : FRHICommandBase(ERHICommandType::DrawInstanced)
        , vertexCountPerInstance(vertCount)
        , instanceCount(instCount)
        , startVertexLocation(startVert)
        , startInstanceLocation(startInst) {}
};

// Draw Indexed Instanced Command
struct FRHICommandDrawIndexedInstanced : FRHICommandBase {
    uint32 indexCountPerInstance;
    uint32 instanceCount;
    uint32 startIndexLocation;
    int32 baseVertexLocation;
    uint32 startInstanceLocation;
    
    FRHICommandDrawIndexedInstanced(uint32 idxCount, uint32 instCount, uint32 startIdx, int32 baseVert, uint32 startInst)
        : FRHICommandBase(ERHICommandType::DrawIndexedInstanced)
        , indexCountPerInstance(idxCount)
        , instanceCount(instCount)
        , startIndexLocation(startIdx)
        , baseVertexLocation(baseVert)
        , startInstanceLocation(startInst) {}
};

// Render Targets Command
struct FRHICommandSetRenderTargets : FRHICommandBase {
    MonsterEngine::TArray<TSharedPtr<IRHITexture>> renderTargets;
    TSharedPtr<IRHITexture> depthStencil;
    
    FRHICommandSetRenderTargets(TSpan<TSharedPtr<IRHITexture>> rts, TSharedPtr<IRHITexture> ds)
        : FRHICommandBase(ERHICommandType::SetRenderTargets)
        , depthStencil(ds) {
        for (const auto& rt : rts) {
            renderTargets.push_back(rt);
        }
    }
};

// Clear Render Target Command
struct FRHICommandClearRenderTarget : FRHICommandBase {
    TSharedPtr<IRHITexture> renderTarget;
    float32 clearColor[4];
    
    FRHICommandClearRenderTarget(TSharedPtr<IRHITexture> rt, const float32 color[4])
        : FRHICommandBase(ERHICommandType::ClearRenderTarget)
        , renderTarget(rt) {
        clearColor[0] = color[0];
        clearColor[1] = color[1];
        clearColor[2] = color[2];
        clearColor[3] = color[3];
    }
};

// Clear Depth Stencil Command
struct FRHICommandClearDepthStencil : FRHICommandBase {
    TSharedPtr<IRHITexture> depthStencil;
    bool clearDepth;
    bool clearStencil;
    float32 depth;
    uint8 stencil;
    
    FRHICommandClearDepthStencil(TSharedPtr<IRHITexture> ds, bool cd, bool cs, float32 d, uint8 s)
        : FRHICommandBase(ERHICommandType::ClearDepthStencil)
        , depthStencil(ds)
        , clearDepth(cd)
        , clearStencil(cs)
        , depth(d)
        , stencil(s) {}
};

// Resource Transition Command
struct FRHICommandTransitionResource : FRHICommandBase {
    TSharedPtr<IRHIResource> resource;
    EResourceUsage stateBefore;
    EResourceUsage stateAfter;
    
    FRHICommandTransitionResource(TSharedPtr<IRHIResource> res, EResourceUsage before, EResourceUsage after)
        : FRHICommandBase(ERHICommandType::TransitionResource)
        , resource(res)
        , stateBefore(before)
        , stateAfter(after) {}
};

// Debug Event Commands
struct FRHICommandBeginEvent : FRHICommandBase {
    String name;
    
    FRHICommandBeginEvent(const String& n)
        : FRHICommandBase(ERHICommandType::BeginEvent)
        , name(n) {}
};

struct FRHICommandEndEvent : FRHICommandBase {
    FRHICommandEndEvent()
        : FRHICommandBase(ERHICommandType::EndEvent) {}
};

struct FRHICommandSetMarker : FRHICommandBase {
    String name;
    
    FRHICommandSetMarker(const String& n)
        : FRHICommandBase(ERHICommandType::SetMarker)
        , name(n) {}
};

/**
 * VulkanRHICommandListRecorder
 * 
 * Records RHI commands for later replay to Vulkan command buffers
 * This enables parallel command recording on multiple threads
 * 
 * Architecture:
 * - Records high-level RHI commands into a command array
 * - Supports ReplayToVulkanCommandBuffer() for translation
 * - Thread-safe for parallel recording (each thread has its own recorder)
 * - Minimal overhead during recording (just stores parameters)
 * 
 * Reference: UE5 FRHICommandList deferred recording pattern
 */
class VulkanRHICommandListRecorder : public IRHICommandList {
public:
    VulkanRHICommandListRecorder(VulkanDevice* device);
    virtual ~VulkanRHICommandListRecorder();
    
    // Non-copyable, movable
    VulkanRHICommandListRecorder(const VulkanRHICommandListRecorder&) = delete;
    VulkanRHICommandListRecorder& operator=(const VulkanRHICommandListRecorder&) = delete;
    VulkanRHICommandListRecorder(VulkanRHICommandListRecorder&&) = default;
    VulkanRHICommandListRecorder& operator=(VulkanRHICommandListRecorder&&) = default;
    
    // ========================================================================
    // IRHICommandList Interface - Recording Phase
    // ========================================================================
    
    void begin() override;
    void end() override;
    void reset() override;
    
    void setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) override;
    void setVertexBuffers(uint32 startSlot, TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) override;
    void setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit = true) override;
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
    
    // ========================================================================
    // Command Replay Interface
    // ========================================================================
    
    /**
     * Replay all recorded commands to a Vulkan command buffer
     * This is called during the parallel translation phase
     * 
     * @param cmdBuffer Vulkan command buffer to replay commands into
     * @param context Vulkan context for resource access
     * @return true if replay succeeded, false otherwise
     */
    bool ReplayToVulkanCommandBuffer(FVulkanCommandBuffer* cmdBuffer, FVulkanContext* context);
    
    /**
     * Get the number of recorded commands
     */
    uint32 GetCommandCount() const { return static_cast<uint32>(m_commands.size()); }
    
    /**
     * Get the number of draw calls recorded
     */
    uint32 GetDrawCallCount() const { return m_drawCallCount; }
    
private:
    VulkanDevice* m_device;
    
    // Command storage using polymorphic command pattern
    MonsterEngine::TArray<TUniquePtr<FRHICommandBase>> m_commands;
    
    // Statistics
    uint32 m_drawCallCount;
    
    // Helper to add a command to the list
    template<typename T, typename... Args>
    void RecordCommand(Args&&... args) {
        m_commands.push_back(MakeUnique<T>(std::forward<Args>(args)...));
    }
};

} // namespace MonsterRender::RHI::Vulkan
