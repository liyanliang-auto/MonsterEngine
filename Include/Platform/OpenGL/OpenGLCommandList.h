#pragma once

/**
 * OpenGLCommandList.h
 * 
 * OpenGL 4.6 command list implementation
 * Reference: UE5 OpenGLDrv/Private/OpenGLCommands.cpp
 */

#include "Core/CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "RHI/IRHICommandList.h"
#include "Platform/OpenGL/OpenGLDefinitions.h"
#include "Platform/OpenGL/OpenGLResources.h"
#include "Platform/OpenGL/OpenGLPipeline.h"

namespace MonsterEngine::OpenGL {

// Use MonsterEngine types
using MonsterEngine::TSharedPtr;
using MonsterEngine::FString;
// Note: TSpan is in global namespace (from CoreTypes.h)

// Forward declarations
class FOpenGLDevice;

/**
 * OpenGL command list
 * Note: OpenGL doesn't have true command lists like D3D12/Vulkan.
 * This implementation executes commands immediately or defers them
 * for batch execution.
 * 
 * Reference: UE5 FOpenGLDynamicRHI command methods
 */
class FOpenGLCommandList : public MonsterRender::RHI::IRHICommandList
{
public:
    FOpenGLCommandList(FOpenGLDevice* device);
    virtual ~FOpenGLCommandList();
    
    // IRHICommandList interface
    virtual void begin() override;
    virtual void end() override;
    virtual void reset() override;
    
    virtual void setPipelineState(TSharedPtr<MonsterRender::RHI::IRHIPipelineState> pipelineState) override;
    virtual void setVertexBuffers(uint32 startSlot, MonsterRender::TSpan<TSharedPtr<MonsterRender::RHI::IRHIBuffer>> vertexBuffers) override;
    virtual void setIndexBuffer(TSharedPtr<MonsterRender::RHI::IRHIBuffer> indexBuffer, bool is32Bit = true) override;
    virtual void setConstantBuffer(uint32 slot, TSharedPtr<MonsterRender::RHI::IRHIBuffer> buffer) override;
    virtual void setShaderResource(uint32 slot, TSharedPtr<MonsterRender::RHI::IRHITexture> texture) override;
    virtual void setSampler(uint32 slot, TSharedPtr<MonsterRender::RHI::IRHISampler> sampler) override;
    
    virtual void setViewport(const MonsterRender::RHI::Viewport& viewport) override;
    virtual void setScissorRect(const MonsterRender::RHI::ScissorRect& scissorRect) override;
    virtual void setRenderTargets(MonsterRender::TSpan<TSharedPtr<MonsterRender::RHI::IRHITexture>> renderTargets, 
                                  TSharedPtr<MonsterRender::RHI::IRHITexture> depthStencil = nullptr) override;
    virtual void endRenderPass() override;
    
    virtual void draw(uint32 vertexCount, uint32 startVertexLocation = 0) override;
    virtual void drawIndexed(uint32 indexCount, uint32 startIndexLocation = 0, 
                            int32 baseVertexLocation = 0) override;
    virtual void drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                              uint32 startVertexLocation = 0, uint32 startInstanceLocation = 0) override;
    virtual void drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount,
                                     uint32 startIndexLocation = 0, int32 baseVertexLocation = 0,
                                     uint32 startInstanceLocation = 0) override;
    
    virtual void clearRenderTarget(TSharedPtr<MonsterRender::RHI::IRHITexture> renderTarget, 
                                  const float32 clearColor[4]) override;
    virtual void clearDepthStencil(TSharedPtr<MonsterRender::RHI::IRHITexture> depthStencil, 
                                  bool clearDepth = true, bool clearStencil = false,
                                  float32 depth = 1.0f, uint8 stencil = 0) override;
    
    virtual void transitionResource(TSharedPtr<MonsterRender::RHI::IRHIResource> resource, 
                                   MonsterRender::RHI::EResourceUsage stateBefore, 
                                   MonsterRender::RHI::EResourceUsage stateAfter) override;
    virtual void resourceBarrier() override;
    
    virtual void beginEvent(const String& name) override;
    virtual void endEvent() override;
    virtual void setMarker(const String& name) override;
    
    // OpenGL-specific methods
    
    /**
     * Execute all recorded commands
     * For OpenGL, this is mostly a no-op since commands execute immediately
     */
    void Execute();
    
    /**
     * Flush any pending GL commands
     */
    void Flush();
    
    /**
     * Get the current primitive topology
     */
    GLenum GetPrimitiveTopology() const { return m_primitiveTopology; }
    
    /**
     * Get the current index type
     */
    GLenum GetIndexType() const { return m_indexType; }
    
private:
    /**
     * Bind current vertex buffers to VAO
     */
    void BindVertexBuffers();
    
    /**
     * Update framebuffer attachments
     */
    void UpdateFramebuffer();
    
private:
    FOpenGLDevice* m_device = nullptr;
    
    // Recording state
    bool m_recording = false;
    
    // Current pipeline state
    FOpenGLPipelineState* m_currentPipeline = nullptr;
    GLenum m_primitiveTopology = GL_TRIANGLES;
    
    // Current vertex/index buffers
    static constexpr uint32 MaxVertexBuffers = 16;
    struct VertexBufferBinding
    {
        FOpenGLBuffer* buffer = nullptr;
        uint32 offset = 0;
        uint32 stride = 0;
    } m_vertexBuffers[MaxVertexBuffers];
    uint32 m_numVertexBuffers = 0;
    
    FOpenGLBuffer* m_indexBuffer = nullptr;
    GLenum m_indexType = GL_UNSIGNED_INT;
    
    // Current constant buffers
    static constexpr uint32 MaxConstantBuffers = 16;
    FOpenGLBuffer* m_constantBuffers[MaxConstantBuffers] = {};
    
    // Current textures and samplers
    static constexpr uint32 MaxTextureSlots = 32;
    FOpenGLTexture* m_textures[MaxTextureSlots] = {};
    FOpenGLSampler* m_samplers[MaxTextureSlots] = {};
    
    // Current render targets
    static constexpr uint32 MaxRenderTargets = 8;
    FOpenGLTexture* m_renderTargets[MaxRenderTargets] = {};
    uint32 m_numRenderTargets = 0;
    FOpenGLTexture* m_depthStencilTarget = nullptr;
    
    // Framebuffer for render targets
    TUniquePtr<FOpenGLFramebuffer> m_framebuffer;
    bool m_framebufferDirty = false;
    
    // Viewport and scissor
    MonsterRender::RHI::Viewport m_viewport;
    MonsterRender::RHI::ScissorRect m_scissorRect;
    
    // Debug event stack depth
    int32 m_debugEventDepth = 0;
};

} // namespace MonsterEngine::OpenGL
