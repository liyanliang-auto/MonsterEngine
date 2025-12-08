/**
 * OpenGLCommandList.cpp
 * 
 * OpenGL 4.6 command list implementation
 * Reference: UE5 OpenGLDrv/Private/OpenGLCommands.cpp
 */

#include "Platform/OpenGL/OpenGLCommandList.h"
#include "Platform/OpenGL/OpenGLDevice.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Core/Logging/LogMacros.h"

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogOpenGLCommands, Log, All);

namespace MonsterEngine::OpenGL {

using namespace MonsterRender::RHI;

// ============================================================================
// FOpenGLCommandList Implementation
// ============================================================================

FOpenGLCommandList::FOpenGLCommandList(FOpenGLDevice* device)
    : m_device(device)
{
    m_framebuffer = MakeUnique<FOpenGLFramebuffer>();
}

FOpenGLCommandList::~FOpenGLCommandList()
{
}

void FOpenGLCommandList::begin()
{
    if (m_recording)
    {
        MR_LOG_WARNING("Command list already recording");
        return;
    }
    
    m_recording = true;
    
    // Reset state
    m_currentPipeline = nullptr;
    m_numVertexBuffers = 0;
    m_indexBuffer = nullptr;
    m_numRenderTargets = 0;
    m_depthStencilTarget = nullptr;
    m_framebufferDirty = true;
    
    for (uint32 i = 0; i < MaxConstantBuffers; ++i)
    {
        m_constantBuffers[i] = nullptr;
    }
    
    for (uint32 i = 0; i < MaxTextureSlots; ++i)
    {
        m_textures[i] = nullptr;
        m_samplers[i] = nullptr;
    }
}

void FOpenGLCommandList::end()
{
    if (!m_recording)
    {
        MR_LOG_WARNING("Command list not recording");
        return;
    }
    
    m_recording = false;
}

void FOpenGLCommandList::reset()
{
    m_recording = false;
    m_currentPipeline = nullptr;
    m_numVertexBuffers = 0;
    m_indexBuffer = nullptr;
    m_numRenderTargets = 0;
    m_depthStencilTarget = nullptr;
    m_framebufferDirty = true;
}

void FOpenGLCommandList::setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState)
{
    auto* glPipeline = static_cast<FOpenGLPipelineState*>(pipelineState.get());
    
    if (m_currentPipeline != glPipeline)
    {
        m_currentPipeline = glPipeline;
        
        if (m_currentPipeline)
        {
            m_currentPipeline->Bind();
            
            // Get primitive topology from pipeline desc
            const auto& desc = m_currentPipeline->getDesc();
            switch (desc.primitiveTopology)
            {
                case EPrimitiveTopology::PointList:     m_primitiveTopology = GL_POINTS; break;
                case EPrimitiveTopology::LineList:      m_primitiveTopology = GL_LINES; break;
                case EPrimitiveTopology::LineStrip:     m_primitiveTopology = GL_LINE_STRIP; break;
                case EPrimitiveTopology::TriangleList:  m_primitiveTopology = GL_TRIANGLES; break;
                case EPrimitiveTopology::TriangleStrip: m_primitiveTopology = GL_TRIANGLE_STRIP; break;
            }
        }
    }
}

void FOpenGLCommandList::setVertexBuffers(uint32 startSlot, MonsterRender::TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers)
{
    for (uint32 i = 0; i < vertexBuffers.size(); ++i)
    {
        uint32 slot = startSlot + i;
        if (slot >= MaxVertexBuffers)
        {
            break;
        }
        
        auto* glBuffer = static_cast<FOpenGLBuffer*>(vertexBuffers[i].get());
        m_vertexBuffers[slot].buffer = glBuffer;
        m_vertexBuffers[slot].offset = 0;
        
        // Get stride from pipeline if available
        if (m_currentPipeline)
        {
            m_vertexBuffers[slot].stride = m_currentPipeline->getDesc().vertexLayout.stride;
        }
    }
    
    m_numVertexBuffers = startSlot + static_cast<uint32>(vertexBuffers.size());
    
    // Bind vertex buffers immediately
    BindVertexBuffers();
}

void FOpenGLCommandList::setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit)
{
    m_indexBuffer = static_cast<FOpenGLBuffer*>(indexBuffer.get());
    m_indexType = is32Bit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    
    if (m_indexBuffer)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer->GetGLBuffer());
    }
    else
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
}

void FOpenGLCommandList::setConstantBuffer(uint32 slot, TSharedPtr<IRHIBuffer> buffer)
{
    if (slot >= MaxConstantBuffers)
    {
        return;
    }
    
    auto* glBuffer = static_cast<FOpenGLBuffer*>(buffer.get());
    m_constantBuffers[slot] = glBuffer;
    
    if (glBuffer)
    {
        glBindBufferBase(GL_UNIFORM_BUFFER, slot, glBuffer->GetGLBuffer());
    }
    else
    {
        glBindBufferBase(GL_UNIFORM_BUFFER, slot, 0);
    }
}

void FOpenGLCommandList::setShaderResource(uint32 slot, TSharedPtr<IRHITexture> texture)
{
    if (slot >= MaxTextureSlots)
    {
        return;
    }
    
    auto* glTexture = static_cast<FOpenGLTexture*>(texture.get());
    m_textures[slot] = glTexture;
    
    if (glTexture)
    {
        glTexture->Bind(slot);
    }
    else
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void FOpenGLCommandList::setSampler(uint32 slot, TSharedPtr<IRHISampler> sampler)
{
    if (slot >= MaxTextureSlots)
    {
        return;
    }
    
    auto* glSampler = static_cast<FOpenGLSampler*>(sampler.get());
    m_samplers[slot] = glSampler;
    
    if (glSampler)
    {
        glSampler->Bind(slot);
    }
    else
    {
        glBindSampler(slot, 0);
    }
}

void FOpenGLCommandList::setViewport(const Viewport& viewport)
{
    m_viewport = viewport;
    
    glViewport(
        static_cast<GLint>(viewport.x),
        static_cast<GLint>(viewport.y),
        static_cast<GLsizei>(viewport.width),
        static_cast<GLsizei>(viewport.height)
    );
    
    glDepthRange(viewport.minDepth, viewport.maxDepth);
}

void FOpenGLCommandList::setScissorRect(const ScissorRect& scissorRect)
{
    m_scissorRect = scissorRect;
    
    glScissor(
        scissorRect.left,
        scissorRect.top,
        scissorRect.right - scissorRect.left,
        scissorRect.bottom - scissorRect.top
    );
}

void FOpenGLCommandList::setRenderTargets(MonsterRender::TSpan<TSharedPtr<IRHITexture>> renderTargets, 
                                          TSharedPtr<IRHITexture> depthStencil)
{
    // Clear previous targets
    for (uint32 i = 0; i < MaxRenderTargets; ++i)
    {
        m_renderTargets[i] = nullptr;
    }
    
    // Set new targets
    m_numRenderTargets = static_cast<uint32>(renderTargets.size());
    for (uint32 i = 0; i < m_numRenderTargets; ++i)
    {
        m_renderTargets[i] = static_cast<FOpenGLTexture*>(renderTargets[i].get());
    }
    
    m_depthStencilTarget = static_cast<FOpenGLTexture*>(depthStencil.get());
    m_framebufferDirty = true;
    
    UpdateFramebuffer();
}

void FOpenGLCommandList::endRenderPass()
{
    // OpenGL doesn't have explicit render passes
    // This is a no-op, but we could use it to unbind render targets
}

void FOpenGLCommandList::draw(uint32 vertexCount, uint32 startVertexLocation)
{
    glDrawArrays(m_primitiveTopology, startVertexLocation, vertexCount);
    GL_CHECK("glDrawArrays");
}

void FOpenGLCommandList::drawIndexed(uint32 indexCount, uint32 startIndexLocation, int32 baseVertexLocation)
{
    const void* indexOffset = reinterpret_cast<const void*>(
        static_cast<uintptr_t>(startIndexLocation * (m_indexType == GL_UNSIGNED_INT ? 4 : 2))
    );
    
    if (baseVertexLocation != 0 && glDrawElementsBaseVertex)
    {
        glDrawElementsBaseVertex(m_primitiveTopology, indexCount, m_indexType, indexOffset, baseVertexLocation);
    }
    else
    {
        glDrawElements(m_primitiveTopology, indexCount, m_indexType, indexOffset);
    }
    
    GL_CHECK("glDrawElements");
}

void FOpenGLCommandList::drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                                       uint32 startVertexLocation, uint32 startInstanceLocation)
{
    if (startInstanceLocation != 0 && glDrawArraysInstancedBaseInstance)
    {
        // GL 4.2+ function
        typedef void (APIENTRY* PFNGLDRAWARRAYSINSTANCEDBASEINSTANCEPROC)(GLenum, GLint, GLsizei, GLsizei, GLuint);
        static auto glDrawArraysInstancedBaseInstance = 
            (PFNGLDRAWARRAYSINSTANCEDBASEINSTANCEPROC)wglGetProcAddress("glDrawArraysInstancedBaseInstance");
        
        if (glDrawArraysInstancedBaseInstance)
        {
            glDrawArraysInstancedBaseInstance(m_primitiveTopology, startVertexLocation, 
                                              vertexCountPerInstance, instanceCount, startInstanceLocation);
        }
        else
        {
            MR_LOG_WARNING("glDrawArraysInstancedBaseInstance not available");
            glDrawArraysInstanced(m_primitiveTopology, startVertexLocation, 
                                 vertexCountPerInstance, instanceCount);
        }
    }
    else
    {
        glDrawArraysInstanced(m_primitiveTopology, startVertexLocation, 
                             vertexCountPerInstance, instanceCount);
    }
    
    GL_CHECK("glDrawArraysInstanced");
}

void FOpenGLCommandList::drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount,
                                              uint32 startIndexLocation, int32 baseVertexLocation,
                                              uint32 startInstanceLocation)
{
    const void* indexOffset = reinterpret_cast<const void*>(
        static_cast<uintptr_t>(startIndexLocation * (m_indexType == GL_UNSIGNED_INT ? 4 : 2))
    );
    
    if (glDrawElementsInstancedBaseVertexBaseInstance && 
        (baseVertexLocation != 0 || startInstanceLocation != 0))
    {
        glDrawElementsInstancedBaseVertexBaseInstance(
            m_primitiveTopology, indexCountPerInstance, m_indexType, indexOffset,
            instanceCount, baseVertexLocation, startInstanceLocation
        );
    }
    else if (glDrawElementsInstancedBaseVertex && baseVertexLocation != 0)
    {
        glDrawElementsInstancedBaseVertex(
            m_primitiveTopology, indexCountPerInstance, m_indexType, indexOffset,
            instanceCount, baseVertexLocation
        );
    }
    else
    {
        glDrawElementsInstanced(m_primitiveTopology, indexCountPerInstance, 
                               m_indexType, indexOffset, instanceCount);
    }
    
    GL_CHECK("glDrawElementsInstanced");
}

void FOpenGLCommandList::clearRenderTarget(TSharedPtr<IRHITexture> renderTarget, const float32 clearColor[4])
{
    // If we have a specific render target, we need to bind it first
    // For now, assume we're clearing the currently bound framebuffer
    
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    
    GL_CHECK("clearRenderTarget");
}

void FOpenGLCommandList::clearDepthStencil(TSharedPtr<IRHITexture> depthStencil, 
                                           bool clearDepth, bool clearStencil,
                                           float32 depth, uint8 stencil)
{
    GLbitfield clearMask = 0;
    
    if (clearDepth)
    {
        glClearDepth(depth);
        clearMask |= GL_DEPTH_BUFFER_BIT;
        
        // Ensure depth writes are enabled for clear
        glDepthMask(GL_TRUE);
    }
    
    if (clearStencil)
    {
        glClearStencil(stencil);
        clearMask |= GL_STENCIL_BUFFER_BIT;
        
        // Ensure stencil writes are enabled for clear
        glStencilMask(0xFF);
    }
    
    if (clearMask != 0)
    {
        glClear(clearMask);
    }
    
    GL_CHECK("clearDepthStencil");
}

void FOpenGLCommandList::transitionResource(TSharedPtr<IRHIResource> resource, 
                                            EResourceUsage stateBefore, 
                                            EResourceUsage stateAfter)
{
    // OpenGL handles resource transitions automatically
    // This is a no-op, but we could insert memory barriers if needed
    
    // For certain transitions, we might want to insert a barrier
    if (glMemoryBarrier)
    {
        GLbitfield barriers = 0;
        
        if (hasResourceUsage(stateAfter, EResourceUsage::ShaderResource))
        {
            barriers |= GL_TEXTURE_FETCH_BARRIER_BIT;
        }
        if (hasResourceUsage(stateAfter, EResourceUsage::UnorderedAccess))
        {
            barriers |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT;
        }
        if (hasResourceUsage(stateAfter, EResourceUsage::RenderTarget))
        {
            barriers |= GL_FRAMEBUFFER_BARRIER_BIT;
        }
        
        if (barriers != 0)
        {
            glMemoryBarrier(barriers);
        }
    }
}

void FOpenGLCommandList::resourceBarrier()
{
    if (glMemoryBarrier)
    {
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }
}

void FOpenGLCommandList::beginEvent(const String& name)
{
    if (glPushDebugGroup)
    {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name.c_str());
    }
    ++m_debugEventDepth;
}

void FOpenGLCommandList::endEvent()
{
    if (m_debugEventDepth > 0)
    {
        if (glPopDebugGroup)
        {
            glPopDebugGroup();
        }
        --m_debugEventDepth;
    }
}

void FOpenGLCommandList::setMarker(const String& name)
{
    if (glDebugMessageInsert)
    {
        glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0,
                            GL_DEBUG_SEVERITY_NOTIFICATION, -1, name.c_str());
    }
}

void FOpenGLCommandList::Execute()
{
    // For OpenGL, commands are executed immediately
    // This method is here for API compatibility with D3D12/Vulkan
    
    // Flush any pending commands
    Flush();
}

void FOpenGLCommandList::Flush()
{
    if (glFlush)
    {
        glFlush();
    }
}

void FOpenGLCommandList::BindVertexBuffers()
{
    if (!m_currentPipeline || !m_currentPipeline->GetVertexArray())
    {
        return;
    }
    
    // Bind VAO
    m_currentPipeline->GetVertexArray()->Bind();
    
    // Bind vertex buffers
    for (uint32 i = 0; i < m_numVertexBuffers; ++i)
    {
        if (m_vertexBuffers[i].buffer)
        {
            glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffers[i].buffer->GetGLBuffer());
            
            // Re-setup vertex attributes with the bound buffer
            // This is needed because VAO stores the buffer binding
            const auto& layout = m_currentPipeline->getDesc().vertexLayout;
            for (const auto& attr : layout.attributes)
            {
                GLint size;
                GLenum type;
                GLboolean normalized = GL_FALSE;
                
                switch (attr.format)
                {
                    case EVertexFormat::Float1: size = 1; type = GL_FLOAT; break;
                    case EVertexFormat::Float2: size = 2; type = GL_FLOAT; break;
                    case EVertexFormat::Float3: size = 3; type = GL_FLOAT; break;
                    case EVertexFormat::Float4: size = 4; type = GL_FLOAT; break;
                    case EVertexFormat::Int1:   size = 1; type = GL_INT; break;
                    case EVertexFormat::Int2:   size = 2; type = GL_INT; break;
                    case EVertexFormat::Int3:   size = 3; type = GL_INT; break;
                    case EVertexFormat::Int4:   size = 4; type = GL_INT; break;
                    case EVertexFormat::UInt1:  size = 1; type = GL_UNSIGNED_INT; break;
                    case EVertexFormat::UInt2:  size = 2; type = GL_UNSIGNED_INT; break;
                    case EVertexFormat::UInt3:  size = 3; type = GL_UNSIGNED_INT; break;
                    case EVertexFormat::UInt4:  size = 4; type = GL_UNSIGNED_INT; break;
                    default: size = 4; type = GL_FLOAT; break;
                }
                
                glEnableVertexAttribArray(attr.location);
                glVertexAttribPointer(
                    attr.location, size, type, normalized,
                    layout.stride,
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(attr.offset))
                );
            }
        }
    }
}

void FOpenGLCommandList::UpdateFramebuffer()
{
    if (!m_framebufferDirty)
    {
        return;
    }
    
    m_framebufferDirty = false;
    
    // If no render targets, bind default framebuffer
    if (m_numRenderTargets == 0 && !m_depthStencilTarget)
    {
        FOpenGLFramebuffer::BindDefault();
        return;
    }
    
    // Update framebuffer attachments
    m_framebuffer->ClearAttachments();
    
    for (uint32 i = 0; i < m_numRenderTargets; ++i)
    {
        if (m_renderTargets[i])
        {
            m_framebuffer->SetColorAttachment(i, m_renderTargets[i]);
        }
    }
    
    if (m_depthStencilTarget)
    {
        m_framebuffer->SetDepthStencilAttachment(m_depthStencilTarget);
    }
    
    m_framebuffer->Bind();
    
    if (!m_framebuffer->IsComplete())
    {
        MR_LOG_ERROR("Framebuffer is not complete!");
    }
}

} // namespace MonsterEngine::OpenGL
