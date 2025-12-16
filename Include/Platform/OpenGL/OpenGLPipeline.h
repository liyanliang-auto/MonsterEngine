#pragma once

/**
 * OpenGLPipeline.h
 * 
 * OpenGL 4.6 pipeline state management
 * Reference: UE5 OpenGLDrv/Public/OpenGLState.h
 */

#include "Core/CoreMinimal.h"
#include "RHI/IRHIResource.h"
#include "Platform/OpenGL/OpenGLDefinitions.h"
#include "Platform/OpenGL/OpenGLShaders.h"
#include "Platform/OpenGL/OpenGLResources.h"

namespace MonsterEngine::OpenGL {

// Forward declarations
class FOpenGLVertexShader;
class FOpenGLPixelShader;

// ============================================================================
// OpenGL Pipeline State
// ============================================================================

/**
 * OpenGL pipeline state object
 * Encapsulates all render state for a draw call
 * Reference: UE5 FOpenGLBoundShaderState
 */
class FOpenGLPipelineState : public MonsterRender::RHI::IRHIPipelineState
{
public:
    FOpenGLPipelineState(const MonsterRender::RHI::PipelineStateDesc& desc);
    virtual ~FOpenGLPipelineState();
    
    // IRHIResource interface - backend identification
    MonsterRender::RHI::ERHIBackend getBackendType() const override { return MonsterRender::RHI::ERHIBackend::OpenGL; }
    
    /**
     * Bind this pipeline state
     */
    void Bind();
    
    /**
     * Get the linked program
     */
    FOpenGLProgram* GetProgram() { return m_program.get(); }
    const FOpenGLProgram* GetProgram() const { return m_program.get(); }
    
    /**
     * Get the VAO for this pipeline
     */
    FOpenGLVertexArray* GetVertexArray() { return m_vertexArray.get(); }
    const FOpenGLVertexArray* GetVertexArray() const { return m_vertexArray.get(); }
    
    /**
     * Check if pipeline is valid
     */
    bool IsValid() const { return m_valid; }
    
private:
    /**
     * Create and link the shader program
     */
    bool CreateProgram();
    
    /**
     * Setup vertex input layout
     */
    void SetupVertexLayout();
    
    /**
     * Apply blend state
     */
    void ApplyBlendState();
    
    /**
     * Apply rasterizer state
     */
    void ApplyRasterizerState();
    
    /**
     * Apply depth stencil state
     */
    void ApplyDepthStencilState();
    
    /**
     * Convert primitive topology to GL enum
     */
    GLenum ConvertPrimitiveTopology(MonsterRender::RHI::EPrimitiveTopology topology) const;
    
    /**
     * Convert blend factor to GL enum
     */
    GLenum ConvertBlendFactor(MonsterRender::RHI::EBlendFactor factor) const;
    
    /**
     * Convert blend op to GL enum
     */
    GLenum ConvertBlendOp(MonsterRender::RHI::EBlendOp op) const;
    
    /**
     * Convert comparison func to GL enum
     */
    GLenum ConvertCompareFunc(MonsterRender::RHI::EComparisonFunc func) const;
    
    /**
     * Convert vertex format to GL parameters
     */
    void ConvertVertexFormat(MonsterRender::RHI::EVertexFormat format, 
                            GLint& size, GLenum& type, GLboolean& normalized) const;
    
private:
    TUniquePtr<FOpenGLProgram> m_program;
    TUniquePtr<FOpenGLVertexArray> m_vertexArray;
    
    // Cached state
    GLenum m_primitiveTopology = GL_TRIANGLES;
    bool m_valid = false;
    
    // Blend state cache
    struct
    {
        bool enabled = false;
        GLenum srcColor = GL_ONE;
        GLenum dstColor = GL_ZERO;
        GLenum colorOp = GL_FUNC_ADD;
        GLenum srcAlpha = GL_ONE;
        GLenum dstAlpha = GL_ZERO;
        GLenum alphaOp = GL_FUNC_ADD;
    } m_blendState;
    
    // Rasterizer state cache
    struct
    {
        GLenum fillMode = GL_FILL;
        GLenum cullMode = GL_BACK;
        GLenum frontFace = GL_CCW;
        bool depthClamp = false;
        bool scissorEnable = false;
    } m_rasterizerState;
    
    // Depth stencil state cache
    struct
    {
        bool depthEnable = true;
        bool depthWrite = true;
        GLenum depthFunc = GL_LESS;
        bool stencilEnable = false;
    } m_depthStencilState;
};

// ============================================================================
// OpenGL State Cache
// ============================================================================

/**
 * OpenGL state cache to minimize redundant state changes
 * Reference: UE5 FOpenGLContextState
 */
class FOpenGLStateCache
{
public:
    FOpenGLStateCache();
    
    /**
     * Reset all cached state
     */
    void Reset();
    
    /**
     * Invalidate all cached state (force re-application)
     */
    void Invalidate();
    
    // Viewport
    void SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth);
    
    // Scissor
    void SetScissorRect(int32 x, int32 y, int32 width, int32 height);
    void SetScissorEnabled(bool enabled);
    
    // Blend
    void SetBlendEnabled(bool enabled);
    void SetBlendFunc(GLenum srcColor, GLenum dstColor, GLenum srcAlpha, GLenum dstAlpha);
    void SetBlendEquation(GLenum colorOp, GLenum alphaOp);
    void SetBlendColor(float r, float g, float b, float a);
    
    // Depth
    void SetDepthTestEnabled(bool enabled);
    void SetDepthWriteEnabled(bool enabled);
    void SetDepthFunc(GLenum func);
    void SetDepthRange(float nearVal, float farVal);
    
    // Stencil
    void SetStencilTestEnabled(bool enabled);
    void SetStencilFunc(GLenum func, int32 ref, uint32 mask);
    void SetStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass);
    void SetStencilMask(uint32 mask);
    
    // Rasterizer
    void SetCullMode(GLenum mode);
    void SetFrontFace(GLenum face);
    void SetPolygonMode(GLenum mode);
    void SetPolygonOffset(float factor, float units);
    void SetPolygonOffsetEnabled(bool enabled);
    
    // Color
    void SetColorMask(bool r, bool g, bool b, bool a);
    
    // Bindings
    void SetProgram(GLuint program);
    void SetVertexArray(GLuint vao);
    void SetFramebuffer(GLuint fbo);
    void SetActiveTexture(uint32 unit);
    void SetTexture(uint32 unit, GLenum target, GLuint texture);
    void SetSampler(uint32 unit, GLuint sampler);
    void SetBuffer(GLenum target, GLuint buffer);
    void SetUniformBuffer(uint32 index, GLuint buffer);
    
private:
    // Viewport state
    struct
    {
        float x = 0, y = 0, width = 0, height = 0;
        float minDepth = 0, maxDepth = 1;
    } m_viewport;
    
    // Scissor state
    struct
    {
        int32 x = 0, y = 0, width = 0, height = 0;
        bool enabled = false;
    } m_scissor;
    
    // Blend state
    struct
    {
        bool enabled = false;
        GLenum srcColor = GL_ONE, dstColor = GL_ZERO;
        GLenum srcAlpha = GL_ONE, dstAlpha = GL_ZERO;
        GLenum colorOp = GL_FUNC_ADD, alphaOp = GL_FUNC_ADD;
        float color[4] = { 0, 0, 0, 0 };
    } m_blend;
    
    // Depth state
    struct
    {
        bool testEnabled = true;
        bool writeEnabled = true;
        GLenum func = GL_LESS;
        float nearVal = 0, farVal = 1;
    } m_depth;
    
    // Stencil state
    struct
    {
        bool enabled = false;
        GLenum func = GL_ALWAYS;
        int32 ref = 0;
        uint32 readMask = 0xFFFFFFFF;
        uint32 writeMask = 0xFFFFFFFF;
        GLenum sfail = GL_KEEP, dpfail = GL_KEEP, dppass = GL_KEEP;
    } m_stencil;
    
    // Rasterizer state
    struct
    {
        GLenum cullMode = GL_BACK;
        bool cullEnabled = true;
        GLenum frontFace = GL_CCW;
        GLenum polygonMode = GL_FILL;
        float offsetFactor = 0, offsetUnits = 0;
        bool offsetEnabled = false;
    } m_rasterizer;
    
    // Color state
    struct
    {
        bool r = true, g = true, b = true, a = true;
    } m_colorMask;
    
    // Bindings
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_fbo = 0;
    uint32 m_activeTexture = 0;
    
    static constexpr uint32 MaxTextureUnits = 32;
    struct TextureBinding
    {
        GLenum target = 0;
        GLuint texture = 0;
        GLuint sampler = 0;
    } m_textures[MaxTextureUnits];
    
    static constexpr uint32 MaxBufferTargets = 16;
    GLuint m_buffers[MaxBufferTargets] = {};
    
    static constexpr uint32 MaxUniformBuffers = 16;
    GLuint m_uniformBuffers[MaxUniformBuffers] = {};
};

} // namespace MonsterEngine::OpenGL
