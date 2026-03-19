// Copyright Monster Engine. All Rights Reserved.

#pragma once

#include "RHI/IRHICommandList.h"

namespace MonsterEngine {
namespace RHI {

/**
 * Mock command list for testing purposes
 * Implements IRHICommandList interface without actual GPU operations
 */
class MockCommandList : public MonsterRender::RHI::IRHICommandList {
public:
    MockCommandList(MonsterRender::RHI::ERecordingThread recordingThread = MonsterRender::RHI::ERecordingThread::Render)
    {
        MR_LOG_DEBUG("MockCommandList::MockCommandList - Created mock command list");
        // Recording thread is set by base class
        (void)recordingThread;
    }
    
    virtual ~MockCommandList() override {
        MR_LOG_DEBUG("MockCommandList::~MockCommandList - Destroyed mock command list");
    }
    
    // IRHICommandList interface implementation
    virtual void begin() override {
        MR_LOG_DEBUG("MockCommandList::begin - Begin recording");
        IRHICommandList::begin();
    }
    
    virtual void end() override {
        MR_LOG_DEBUG("MockCommandList::end - End recording");
        IRHICommandList::end();
    }
    
    virtual void reset() override {
        MR_LOG_DEBUG("MockCommandList::reset - Reset command list");
        IRHICommandList::reset();
    }
    
    // Pipeline and state management
    virtual void setPipelineState(TSharedPtr<MonsterRender::RHI::IRHIPipelineState> pipelineState) override {
        MR_LOG_DEBUG("MockCommandList::setPipelineState - Set pipeline state");
    }
    
    virtual void setVertexBuffers(uint32 startSlot, TSpan<TSharedPtr<MonsterRender::RHI::IRHIBuffer>> vertexBuffers) override {
        MR_LOG_DEBUG("MockCommandList::setVertexBuffers - Set vertex buffers");
    }
    
    virtual void setIndexBuffer(TSharedPtr<MonsterRender::RHI::IRHIBuffer> indexBuffer, bool is16Bit = false) override {
        MR_LOG_DEBUG("MockCommandList::setIndexBuffer - Set index buffer");
    }
    
    virtual void setViewport(const MonsterRender::RHI::Viewport& viewport) override {
        MR_LOG_DEBUG("MockCommandList::setViewport - Set viewport");
    }
    
    virtual void setScissorRect(const MonsterRender::RHI::ScissorRect& scissorRect) override {
        MR_LOG_DEBUG("MockCommandList::setScissorRect - Set scissor rect");
    }
    
    virtual void setConstantBuffer(uint32 slot, TSharedPtr<MonsterRender::RHI::IRHIBuffer> constantBuffer) override {
        MR_LOG_DEBUG("MockCommandList::setConstantBuffer - Set constant buffer");
    }
    
    virtual void setShaderResource(uint32 slot, TSharedPtr<MonsterRender::RHI::IRHITexture> texture) override {
        MR_LOG_DEBUG("MockCommandList::setShaderResource - Set shader resource");
    }
    
    virtual void setSampler(uint32 slot, TSharedPtr<MonsterRender::RHI::IRHISampler> sampler) override {
        MR_LOG_DEBUG("MockCommandList::setSampler - Set sampler");
    }
    
    // Render pass management
    virtual void setRenderTargets(TSpan<TSharedPtr<MonsterRender::RHI::IRHITexture>> renderTargets, 
                                  TSharedPtr<MonsterRender::RHI::IRHITexture> depthStencil) override {
        MR_LOG_DEBUG("MockCommandList::setRenderTargets - Set render targets");
    }
    
    virtual void endRenderPass() override {
        MR_LOG_DEBUG("MockCommandList::endRenderPass - End render pass");
    }
    
    // Draw commands
    virtual void draw(uint32 vertexCount, uint32 startVertex = 0) override {
        MR_LOG_DEBUG("MockCommandList::draw - Draw " + std::to_string(vertexCount) + " vertices");
    }
    
    virtual void drawIndexed(uint32 indexCount, uint32 startIndex = 0, int32 baseVertex = 0) override {
        MR_LOG_DEBUG("MockCommandList::drawIndexed - Draw indexed " + std::to_string(indexCount) + " indices");
    }
    
    virtual void drawInstanced(uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0) override {
        MR_LOG_DEBUG("MockCommandList::drawInstanced - Draw instanced");
    }
    
    virtual void drawIndexedInstanced(uint32 indexCount, uint32 instanceCount, uint32 startIndex = 0, 
                                     int32 baseVertex = 0, uint32 startInstance = 0) override {
        MR_LOG_DEBUG("MockCommandList::drawIndexedInstanced - Draw indexed instanced");
    }
    
    // Clear operations
    virtual void clearRenderTarget(TSharedPtr<MonsterRender::RHI::IRHITexture> renderTarget, 
                                   const float32 clearColor[4]) override {
        MR_LOG_DEBUG("MockCommandList::clearRenderTarget - Clear render target");
    }
    
    virtual void clearDepthStencil(TSharedPtr<MonsterRender::RHI::IRHITexture> depthStencil, 
                                   bool clearDepth, bool clearStencil, 
                                   float32 depth = 1.0f, uint8 stencil = 0) override {
        MR_LOG_DEBUG("MockCommandList::clearDepthStencil - Clear depth stencil");
    }
    
    // Resource transitions
    virtual void transitionResource(TSharedPtr<MonsterRender::RHI::IRHIResource> resource, 
                                   MonsterRender::RHI::EResourceUsage before, 
                                   MonsterRender::RHI::EResourceUsage after) override {
        MR_LOG_DEBUG("MockCommandList::transitionResource - Transition resource");
    }
    
    virtual void resourceBarrier() override {
        MR_LOG_DEBUG("MockCommandList::resourceBarrier - Resource barrier");
    }
    
    // Debug markers
    virtual void beginEvent(const String& name) override {
        MR_LOG_DEBUG("MockCommandList::beginEvent - Begin event: " + name);
    }
    
    virtual void endEvent() override {
        MR_LOG_DEBUG("MockCommandList::endEvent - End event");
    }
    
    virtual void setMarker(const String& name) override {
        MR_LOG_DEBUG("MockCommandList::setMarker - Set marker: " + name);
    }
};

} // namespace RHI
} // namespace MonsterEngine
