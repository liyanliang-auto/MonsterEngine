// Temporary file for CommitState implementation
// This will be inserted into OpenGLCommandList.cpp

void FOpenGLCommandList::CommitState()
{
    // Reference: UE5 FOpenGLDynamicRHI::CommitNonComputeShaderConstants()
    
    // Commit constant buffers
    if (m_pendingState.DirtyConstantBufferMask != 0)
    {
        for (uint32 slot = 0; slot < MaxConstantBuffers; ++slot)
        {
            if (m_pendingState.DirtyConstantBufferMask & (1u << slot))
            {
                FOpenGLBuffer* glBuffer = m_pendingState.ConstantBuffers[slot];
                GLuint newHandle = glBuffer ? glBuffer->GetGLBuffer() : 0;
                
                // Only call OpenGL API if state changed
                if (m_cachedState.ConstantBufferHandles[slot] != newHandle)
                {
                    glBindBufferBase(GL_UNIFORM_BUFFER, slot, newHandle);
                    m_cachedState.ConstantBufferHandles[slot] = newHandle;
                }
            }
        }
        m_pendingState.DirtyConstantBufferMask = 0;
    }
    
    // Commit textures
    if (m_pendingState.DirtyTextureMask != 0)
    {
        for (uint32 slot = 0; slot < MaxTextureSlots; ++slot)
        {
            if (m_pendingState.DirtyTextureMask & (1u << slot))
            {
                FOpenGLTexture* glTexture = m_pendingState.Textures[slot];
                GLuint newHandle = glTexture ? glTexture->GetGLTexture() : 0;
                
                // Only call OpenGL API if state changed
                if (m_cachedState.TextureHandles[slot] != newHandle)
                {
                    glActiveTexture(GL_TEXTURE0 + slot);
                    glBindTexture(GL_TEXTURE_2D, newHandle);
                    m_cachedState.TextureHandles[slot] = newHandle;
                }
            }
        }
        m_pendingState.DirtyTextureMask = 0;
    }
    
    // Commit samplers
    if (m_pendingState.DirtySamplerMask != 0)
    {
        for (uint32 slot = 0; slot < MaxTextureSlots; ++slot)
        {
            if (m_pendingState.DirtySamplerMask & (1u << slot))
            {
                FOpenGLSampler* glSampler = m_pendingState.Samplers[slot];
                GLuint newHandle = glSampler ? glSampler->GetGLSampler() : 0;
                
                // Only call OpenGL API if state changed
                if (m_cachedState.SamplerHandles[slot] != newHandle)
                {
                    glBindSampler(slot, newHandle);
                    m_cachedState.SamplerHandles[slot] = newHandle;
                }
            }
        }
        m_pendingState.DirtySamplerMask = 0;
    }
}
