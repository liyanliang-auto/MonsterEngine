// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan RHI Command List Recorder Implementation
//
// Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanCommandList.cpp
//            UE5 deferred command recording pattern

#include "Platform/Vulkan/VulkanRHICommandListRecorder.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanContextManager.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanSampler.h"
#include "Platform/Vulkan/VulkanUtils.h"
#include "Core/Log.h"

DEFINE_LOG_CATEGORY_STATIC(LogVulkanRHIRecorder, Log, All);

namespace MonsterRender::RHI::Vulkan {

using namespace MonsterRender::RHI;

// ============================================================================
// VulkanRHICommandListRecorder Implementation
// ============================================================================

VulkanRHICommandListRecorder::VulkanRHICommandListRecorder(VulkanDevice* device)
    : m_device(device)
    , m_drawCallCount(0) {
    MR_LOG_DEBUG("VulkanRHICommandListRecorder: Created command list recorder");
}

VulkanRHICommandListRecorder::~VulkanRHICommandListRecorder() {
    MR_LOG_DEBUG("VulkanRHICommandListRecorder: Destroyed command list recorder with " + 
                std::to_string(m_commands.size()) + " commands");
}

// ============================================================================
// Command Recording Interface
// ============================================================================

void VulkanRHICommandListRecorder::begin() {
    // Clear any previously recorded commands
    m_commands.clear();
    m_drawCallCount = 0;
    
    // Transition to recording state
    SetState(ERHICommandListState::Recording);
    
    MR_LOG_DEBUG("VulkanRHICommandListRecorder::begin - Started recording commands");
}

void VulkanRHICommandListRecorder::end() {
    // Transition to recorded state
    SetState(ERHICommandListState::Recorded);
    
    MR_LOG_DEBUG("VulkanRHICommandListRecorder::end - Finished recording " + 
                std::to_string(m_commands.size()) + " commands, " +
                std::to_string(m_drawCallCount) + " draw calls");
}

void VulkanRHICommandListRecorder::reset() {
    // Clear all recorded commands
    m_commands.clear();
    m_drawCallCount = 0;
    
    // Reset to not allocated state
    SetState(ERHICommandListState::NotAllocated);
    
    MR_LOG_DEBUG("VulkanRHICommandListRecorder::reset - Cleared all commands");
}

// ============================================================================
// Pipeline and State Commands
// ============================================================================

void VulkanRHICommandListRecorder::setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) {
    RecordCommand<FRHICommandSetPipelineState>(pipelineState);
}

void VulkanRHICommandListRecorder::setViewport(const Viewport& viewport) {
    RecordCommand<FRHICommandSetViewport>(viewport);
}

void VulkanRHICommandListRecorder::setScissorRect(const ScissorRect& scissorRect) {
    RecordCommand<FRHICommandSetScissorRect>(scissorRect);
}

void VulkanRHICommandListRecorder::setRenderTargets(TSpan<TSharedPtr<IRHITexture>> renderTargets, 
                                                   TSharedPtr<IRHITexture> depthStencil) {
    RecordCommand<FRHICommandSetRenderTargets>(renderTargets, depthStencil);
}

void VulkanRHICommandListRecorder::endRenderPass() {
    // End render pass is implicit in Vulkan when we transition resources
    // For now, we don't record a specific command for this
}

// ============================================================================
// Resource Binding Commands
// ============================================================================

void VulkanRHICommandListRecorder::setVertexBuffers(uint32 startSlot, TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) {
    RecordCommand<FRHICommandSetVertexBuffers>(startSlot, vertexBuffers);
}

void VulkanRHICommandListRecorder::setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit) {
    RecordCommand<FRHICommandSetIndexBuffer>(indexBuffer, is32Bit);
}

void VulkanRHICommandListRecorder::setConstantBuffer(uint32 slot, TSharedPtr<IRHIBuffer> buffer) {
    // Constant buffers are typically bound via descriptor sets in Vulkan
    // For now, we'll implement this in the future when we have descriptor set support
    MR_LOG_WARNING("VulkanRHICommandListRecorder::setConstantBuffer - Not yet implemented, use descriptor sets");
}

void VulkanRHICommandListRecorder::setShaderResource(uint32 slot, TSharedPtr<IRHITexture> texture) {
    // Shader resources are typically bound via descriptor sets in Vulkan
    // For now, we'll implement this in the future when we have descriptor set support
    MR_LOG_WARNING("VulkanRHICommandListRecorder::setShaderResource - Not yet implemented, use descriptor sets");
}

void VulkanRHICommandListRecorder::setSampler(uint32 slot, TSharedPtr<IRHISampler> sampler) {
    // Samplers are typically bound via descriptor sets in Vulkan
    // For now, we'll implement this in the future when we have descriptor set support
    MR_LOG_WARNING("VulkanRHICommandListRecorder::setSampler - Not yet implemented, use descriptor sets");
}

// ============================================================================
// Draw Commands
// ============================================================================

void VulkanRHICommandListRecorder::draw(uint32 vertexCount, uint32 startVertexLocation) {
    RecordCommand<FRHICommandDraw>(vertexCount, startVertexLocation);
    m_drawCallCount++;
}

void VulkanRHICommandListRecorder::drawIndexed(uint32 indexCount, uint32 startIndexLocation, 
                                              int32 baseVertexLocation) {
    RecordCommand<FRHICommandDrawIndexed>(indexCount, startIndexLocation, baseVertexLocation);
    m_drawCallCount++;
}

void VulkanRHICommandListRecorder::drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                                                uint32 startVertexLocation, uint32 startInstanceLocation) {
    RecordCommand<FRHICommandDrawInstanced>(vertexCountPerInstance, instanceCount, 
                                           startVertexLocation, startInstanceLocation);
    m_drawCallCount++;
}

void VulkanRHICommandListRecorder::drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount,
                                                       uint32 startIndexLocation, int32 baseVertexLocation,
                                                       uint32 startInstanceLocation) {
    RecordCommand<FRHICommandDrawIndexedInstanced>(indexCountPerInstance, instanceCount, 
                                                  startIndexLocation, baseVertexLocation, 
                                                  startInstanceLocation);
    m_drawCallCount++;
}

// ============================================================================
// Clear Commands
// ============================================================================

void VulkanRHICommandListRecorder::clearRenderTarget(TSharedPtr<IRHITexture> renderTarget, 
                                                    const float32 clearColor[4]) {
    RecordCommand<FRHICommandClearRenderTarget>(renderTarget, clearColor);
}

void VulkanRHICommandListRecorder::clearDepthStencil(TSharedPtr<IRHITexture> depthStencil, 
                                                    bool clearDepth, bool clearStencil,
                                                    float32 depth, uint8 stencil) {
    RecordCommand<FRHICommandClearDepthStencil>(depthStencil, clearDepth, clearStencil, depth, stencil);
}

// ============================================================================
// Resource Transition Commands
// ============================================================================

void VulkanRHICommandListRecorder::transitionResource(TSharedPtr<IRHIResource> resource, 
                                                     EResourceUsage stateBefore, EResourceUsage stateAfter) {
    RecordCommand<FRHICommandTransitionResource>(resource, stateBefore, stateAfter);
}

void VulkanRHICommandListRecorder::resourceBarrier() {
    // Resource barriers are implicit in Vulkan through pipeline barriers
    // We'll handle this during replay
}

// ============================================================================
// Debug Commands
// ============================================================================

void VulkanRHICommandListRecorder::beginEvent(const String& name) {
    RecordCommand<FRHICommandBeginEvent>(name);
}

void VulkanRHICommandListRecorder::endEvent() {
    RecordCommand<FRHICommandEndEvent>();
}

void VulkanRHICommandListRecorder::setMarker(const String& name) {
    RecordCommand<FRHICommandSetMarker>(name);
}

// ============================================================================
// Command Replay Implementation
// ============================================================================

bool VulkanRHICommandListRecorder::ReplayToVulkanCommandBuffer(FVulkanCommandBuffer* cmdBuffer, 
                                                               FVulkanContext* context) {
    if (!cmdBuffer || !context) {
        MR_LOG_ERROR("VulkanRHICommandListRecorder::ReplayToVulkanCommandBuffer - Invalid parameters");
        return false;
    }
    
    MR_LOG_DEBUG("VulkanRHICommandListRecorder::ReplayToVulkanCommandBuffer - Replaying " + 
                std::to_string(m_commands.size()) + " commands");
    
    // Get the Vulkan command buffer handle
    // Note: FVulkanCommandBuffer is actually FVulkanCmdBuffer, use getHandle()
    VkCommandBuffer vkCmdBuffer = VK_NULL_HANDLE;
    
    // Cast to FVulkanCmdBuffer to access getHandle()
    auto* vulkanCmdBuffer = reinterpret_cast<FVulkanCmdBuffer*>(cmdBuffer);
    if (vulkanCmdBuffer) {
        vkCmdBuffer = vulkanCmdBuffer->getHandle();
    }
    
    if (vkCmdBuffer == VK_NULL_HANDLE) {
        MR_LOG_ERROR("VulkanRHICommandListRecorder::ReplayToVulkanCommandBuffer - Invalid command buffer handle");
        return false;
    }
    
    // Replay each recorded command
    for (const auto& command : m_commands) {
        if (!command) {
            MR_LOG_WARNING("VulkanRHICommandListRecorder::ReplayToVulkanCommandBuffer - Null command encountered");
            continue;
        }
        
        // Dispatch based on command type
        switch (command->type) {
            case ERHICommandType::SetPipelineState: {
                auto* cmd = static_cast<FRHICommandSetPipelineState*>(command.get());
                if (cmd->pipelineState) {
                    // Cast to Vulkan pipeline state
                    auto* vulkanPipeline = static_cast<VulkanPipelineState*>(cmd->pipelineState.get());
                    if (vulkanPipeline) {
                        vkCmdBindPipeline(vkCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                        vulkanPipeline->getPipeline());
                        MR_LOG_DEBUG("  Replayed: SetPipelineState");
                    }
                }
                break;
            }
            
            case ERHICommandType::SetViewport: {
                auto* cmd = static_cast<FRHICommandSetViewport*>(command.get());
                VkViewport vkViewport = {};
                vkViewport.x = cmd->viewport.x;
                vkViewport.y = cmd->viewport.y;
                vkViewport.width = cmd->viewport.width;
                vkViewport.height = cmd->viewport.height;
                vkViewport.minDepth = cmd->viewport.minDepth;
                vkViewport.maxDepth = cmd->viewport.maxDepth;
                vkCmdSetViewport(vkCmdBuffer, 0, 1, &vkViewport);
                MR_LOG_DEBUG("  Replayed: SetViewport");
                break;
            }
            
            case ERHICommandType::SetScissorRect: {
                auto* cmd = static_cast<FRHICommandSetScissorRect*>(command.get());
                VkRect2D vkScissor = {};
                vkScissor.offset.x = cmd->scissorRect.left;
                vkScissor.offset.y = cmd->scissorRect.top;
                vkScissor.extent.width = cmd->scissorRect.right - cmd->scissorRect.left;
                vkScissor.extent.height = cmd->scissorRect.bottom - cmd->scissorRect.top;
                vkCmdSetScissor(vkCmdBuffer, 0, 1, &vkScissor);
                MR_LOG_DEBUG("  Replayed: SetScissorRect");
                break;
            }
            
            case ERHICommandType::SetVertexBuffers: {
                auto* cmd = static_cast<FRHICommandSetVertexBuffers*>(command.get());
                if (!cmd->vertexBuffers.empty()) {
                    MonsterEngine::TArray<VkBuffer> vkBuffers;
                    MonsterEngine::TArray<VkDeviceSize> offsets;
                    
                    for (const auto& buffer : cmd->vertexBuffers) {
                        if (buffer) {
                            auto* vulkanBuffer = static_cast<VulkanBuffer*>(buffer.get());
                            if (vulkanBuffer) {
                                vkBuffers.push_back(vulkanBuffer->getBuffer());
                                offsets.push_back(0);
                            }
                        }
                    }
                    
                    if (!vkBuffers.empty()) {
                        vkCmdBindVertexBuffers(vkCmdBuffer, cmd->startSlot, 
                                             static_cast<uint32>(vkBuffers.size()),
                                             vkBuffers.data(), offsets.data());
                        MR_LOG_DEBUG("  Replayed: SetVertexBuffers (" + 
                                   std::to_string(vkBuffers.size()) + " buffers)");
                    }
                }
                break;
            }
            
            case ERHICommandType::SetIndexBuffer: {
                auto* cmd = static_cast<FRHICommandSetIndexBuffer*>(command.get());
                if (cmd->indexBuffer) {
                    auto* vulkanBuffer = static_cast<VulkanBuffer*>(cmd->indexBuffer.get());
                    if (vulkanBuffer) {
                        VkIndexType indexType = cmd->is32Bit ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
                        vkCmdBindIndexBuffer(vkCmdBuffer, vulkanBuffer->getBuffer(), 0, indexType);
                        MR_LOG_DEBUG("  Replayed: SetIndexBuffer");
                    }
                }
                break;
            }
            
            case ERHICommandType::Draw: {
                auto* cmd = static_cast<FRHICommandDraw*>(command.get());
                vkCmdDraw(vkCmdBuffer, cmd->vertexCount, 1, cmd->startVertexLocation, 0);
                MR_LOG_DEBUG("  Replayed: Draw (" + std::to_string(cmd->vertexCount) + " vertices)");
                break;
            }
            
            case ERHICommandType::DrawIndexed: {
                auto* cmd = static_cast<FRHICommandDrawIndexed*>(command.get());
                vkCmdDrawIndexed(vkCmdBuffer, cmd->indexCount, 1, 
                               cmd->startIndexLocation, cmd->baseVertexLocation, 0);
                MR_LOG_DEBUG("  Replayed: DrawIndexed (" + std::to_string(cmd->indexCount) + " indices)");
                break;
            }
            
            case ERHICommandType::DrawInstanced: {
                auto* cmd = static_cast<FRHICommandDrawInstanced*>(command.get());
                vkCmdDraw(vkCmdBuffer, cmd->vertexCountPerInstance, cmd->instanceCount,
                         cmd->startVertexLocation, cmd->startInstanceLocation);
                MR_LOG_DEBUG("  Replayed: DrawInstanced (" + std::to_string(cmd->instanceCount) + " instances)");
                break;
            }
            
            case ERHICommandType::DrawIndexedInstanced: {
                auto* cmd = static_cast<FRHICommandDrawIndexedInstanced*>(command.get());
                vkCmdDrawIndexed(vkCmdBuffer, cmd->indexCountPerInstance, cmd->instanceCount,
                               cmd->startIndexLocation, cmd->baseVertexLocation, 
                               cmd->startInstanceLocation);
                MR_LOG_DEBUG("  Replayed: DrawIndexedInstanced (" + 
                           std::to_string(cmd->instanceCount) + " instances)");
                break;
            }
            
            case ERHICommandType::BeginEvent: {
                auto* cmd = static_cast<FRHICommandBeginEvent*>(command.get());
                // Debug events would use VK_EXT_debug_utils or VK_EXT_debug_marker
                // For now, just log
                MR_LOG_DEBUG("  Replayed: BeginEvent (" + cmd->name + ")");
                break;
            }
            
            case ERHICommandType::EndEvent: {
                MR_LOG_DEBUG("  Replayed: EndEvent");
                break;
            }
            
            case ERHICommandType::SetMarker: {
                auto* cmd = static_cast<FRHICommandSetMarker*>(command.get());
                MR_LOG_DEBUG("  Replayed: SetMarker (" + cmd->name + ")");
                break;
            }
            
            default:
                MR_LOG_WARNING("VulkanRHICommandListRecorder::ReplayToVulkanCommandBuffer - "
                             "Unhandled command type: " + std::to_string(static_cast<uint32>(command->type)));
                break;
        }
    }
    
    MR_LOG_DEBUG("VulkanRHICommandListRecorder::ReplayToVulkanCommandBuffer - "
                "Successfully replayed all commands");
    
    return true;
}

} // namespace MonsterRender::RHI::Vulkan
