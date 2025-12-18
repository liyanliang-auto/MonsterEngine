/**
 * OpenGLPipeline.cpp
 * 
 * OpenGL 4.6 pipeline state implementation
 * Reference: UE5 OpenGLDrv/Private/OpenGLState.cpp
 */

#include "Platform/OpenGL/OpenGLPipeline.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Core/Logging/LogMacros.h"

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogOpenGLPipeline, Log, All);

namespace MonsterEngine::OpenGL {

using namespace MonsterRender::RHI;

// ============================================================================
// FOpenGLPipelineState Implementation
// ============================================================================

FOpenGLPipelineState::FOpenGLPipelineState(const PipelineStateDesc& desc)
    : IRHIPipelineState(desc)
{
    // Convert and cache state
    m_primitiveTopology = ConvertPrimitiveTopology(desc.primitiveTopology);
    
    // Blend state
    m_blendState.enabled = desc.blendState.blendEnable;
    m_blendState.srcColor = ConvertBlendFactor(desc.blendState.srcColorBlend);
    m_blendState.dstColor = ConvertBlendFactor(desc.blendState.destColorBlend);
    m_blendState.colorOp = ConvertBlendOp(desc.blendState.colorBlendOp);
    m_blendState.srcAlpha = ConvertBlendFactor(desc.blendState.srcAlphaBlend);
    m_blendState.dstAlpha = ConvertBlendFactor(desc.blendState.destAlphaBlend);
    m_blendState.alphaOp = ConvertBlendOp(desc.blendState.alphaBlendOp);
    
    // Rasterizer state
    // UE5 Pattern: OpenGL front-face convention
    // Reference: UE5 OpenGLDrv - glFrontFace handling
    // 
    // Engine unified convention: frontCounterClockwise=false means CW is front
    // OpenGL: When using glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE), the Y-axis
    // is flipped similar to Vulkan, so we use GL_CW when frontCounterClockwise=false
    m_rasterizerState.fillMode = (desc.rasterizerState.fillMode == EFillMode::Wireframe) ? GL_LINE : GL_FILL;
    switch (desc.rasterizerState.cullMode)
    {
        case ECullMode::None:  m_rasterizerState.cullMode = 0; break;
        case ECullMode::Front: m_rasterizerState.cullMode = GL_FRONT; break;
        case ECullMode::Back:  m_rasterizerState.cullMode = GL_BACK; break;
    }
    // Front face: CW when frontCounterClockwise=false (engine default)
    m_rasterizerState.frontFace = desc.rasterizerState.frontCounterClockwise ? GL_CCW : GL_CW;
    m_rasterizerState.depthClamp = desc.rasterizerState.depthClampEnable;
    m_rasterizerState.scissorEnable = desc.rasterizerState.scissorEnable;
    
    // Depth stencil state
    m_depthStencilState.depthEnable = desc.depthStencilState.depthEnable;
    m_depthStencilState.depthWrite = desc.depthStencilState.depthWriteEnable;
    m_depthStencilState.depthFunc = ConvertCompareFunc(desc.depthStencilState.depthFunc);
    m_depthStencilState.stencilEnable = desc.depthStencilState.stencilEnable;
    
    // Create program and VAO
    if (CreateProgram())
    {
        SetupVertexLayout();
        m_valid = true;
    }
    else
    {
        OutputDebugStringA("OpenGL: Error\n");
    }
}

FOpenGLPipelineState::~FOpenGLPipelineState() = default;

void FOpenGLPipelineState::Bind()
{
    if (!m_valid)
    {
        return;
    }
    
    // Bind program
    if (m_program)
    {
        m_program->Use();
    }
    
    // Bind VAO
    if (m_vertexArray)
    {
        m_vertexArray->Bind();
    }
    
    // Apply states
    ApplyBlendState();
    ApplyRasterizerState();
    ApplyDepthStencilState();
}

bool FOpenGLPipelineState::CreateProgram()
{
    m_program = MakeUnique<FOpenGLProgram>();
    
    // Get shaders from desc
    auto* vertexShader = static_cast<FOpenGLVertexShader*>(m_desc.vertexShader.get());
    auto* pixelShader = static_cast<FOpenGLPixelShader*>(m_desc.pixelShader.get());
    
    if (!vertexShader || !vertexShader->GetShader() || !vertexShader->GetShader()->IsCompiled())
    {
        OutputDebugStringA("OpenGL: Error\n");
        return false;
    }
    
    if (!pixelShader || !pixelShader->GetShader() || !pixelShader->GetShader()->IsCompiled())
    {
        OutputDebugStringA("OpenGL: Error\n");
        return false;
    }
    
    // Attach shaders
    m_program->AttachShader(vertexShader->GetGLShader());
    m_program->AttachShader(pixelShader->GetGLShader());
    
    // Bind attribute locations based on vertex layout
    const auto& layout = m_desc.vertexLayout;
    for (const auto& attr : layout.attributes)
    {
        if (!attr.semanticName.empty())
        {
            m_program->BindAttribLocation(attr.location, attr.semanticName.c_str());
        }
    }
    
    // Link program
    if (!m_program->Link())
    {
        return false;
    }
    
    // Bind uniform blocks to their binding points
    // This is required for uniform buffers to work correctly
    GLuint transformBlockIdx = glGetUniformBlockIndex(m_program->GetGLProgram(), "TransformUB");
    GLuint lightBlockIdx = glGetUniformBlockIndex(m_program->GetGLProgram(), "LightUB");
    
    MR_LOG(LogOpenGLPipeline, Log, "Uniform block indices: TransformUB=%u, LightUB=%u (INVALID=%u)", 
           transformBlockIdx, lightBlockIdx, GL_INVALID_INDEX);
    
    if (transformBlockIdx != GL_INVALID_INDEX)
    {
        glUniformBlockBinding(m_program->GetGLProgram(), transformBlockIdx, 0);
    }
    if (lightBlockIdx != GL_INVALID_INDEX)
    {
        glUniformBlockBinding(m_program->GetGLProgram(), lightBlockIdx, 3);
    }
    
    // Also set texture sampler uniforms
    m_program->Use();
    GLint loc = glGetUniformLocation(m_program->GetGLProgram(), "texture1");
    if (loc >= 0) glUniform1i(loc, 1);  // texture1 at slot 1
    loc = glGetUniformLocation(m_program->GetGLProgram(), "texture2");
    if (loc >= 0) glUniform1i(loc, 2);  // texture2 at slot 2
    loc = glGetUniformLocation(m_program->GetGLProgram(), "textureBlend");
    if (loc >= 0) glUniform1f(loc, 0.0f);  // Default blend factor
    
    m_program->SetDebugName(m_desc.debugName);
    
    return true;
}

void FOpenGLPipelineState::SetupVertexLayout()
{
    m_vertexArray = MakeUnique<FOpenGLVertexArray>();
    m_vertexArray->Bind();
    
    const auto& layout = m_desc.vertexLayout;
    
    for (const auto& attr : layout.attributes)
    {
        GLint size;
        GLenum type;
        GLboolean normalized;
        ConvertVertexFormat(attr.format, size, type, normalized);
        
        m_vertexArray->EnableAttribute(attr.location);
        m_vertexArray->SetVertexAttribute(
            attr.location,
            size,
            type,
            normalized,
            layout.stride,
            reinterpret_cast<const void*>(static_cast<uintptr_t>(attr.offset))
        );
    }
    
    FOpenGLVertexArray::Unbind();
}

void FOpenGLPipelineState::ApplyBlendState()
{
    if (m_blendState.enabled)
    {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(m_blendState.srcColor, m_blendState.dstColor,
                           m_blendState.srcAlpha, m_blendState.dstAlpha);
        glBlendEquationSeparate(m_blendState.colorOp, m_blendState.alphaOp);
    }
    else
    {
        glDisable(GL_BLEND);
    }
}

void FOpenGLPipelineState::ApplyRasterizerState()
{
    // Cull mode
    if (m_rasterizerState.cullMode != 0)
    {
        glEnable(GL_CULL_FACE);
        glCullFace(m_rasterizerState.cullMode);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }
    
    // Front face
    glFrontFace(m_rasterizerState.frontFace);
    
    // Fill mode
    glPolygonMode(GL_FRONT_AND_BACK, m_rasterizerState.fillMode);
    
    // Depth clamp
    if (m_rasterizerState.depthClamp)
    {
        glEnable(GL_DEPTH_CLAMP);
    }
    else
    {
        glDisable(GL_DEPTH_CLAMP);
    }
    
    // Scissor
    if (m_rasterizerState.scissorEnable)
    {
        glEnable(GL_SCISSOR_TEST);
    }
    else
    {
        glDisable(GL_SCISSOR_TEST);
    }
}

void FOpenGLPipelineState::ApplyDepthStencilState()
{
    // Depth test
    if (m_depthStencilState.depthEnable)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(m_depthStencilState.depthFunc);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
    
    // Depth write
    glDepthMask(m_depthStencilState.depthWrite ? GL_TRUE : GL_FALSE);
    
    // Stencil test
    if (m_depthStencilState.stencilEnable)
    {
        glEnable(GL_STENCIL_TEST);
    }
    else
    {
        glDisable(GL_STENCIL_TEST);
    }
}

GLenum FOpenGLPipelineState::ConvertPrimitiveTopology(EPrimitiveTopology topology) const
{
    switch (topology)
    {
        case EPrimitiveTopology::PointList:     return GL_POINTS;
        case EPrimitiveTopology::LineList:      return GL_LINES;
        case EPrimitiveTopology::LineStrip:     return GL_LINE_STRIP;
        case EPrimitiveTopology::TriangleList:  return GL_TRIANGLES;
        case EPrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        default: return GL_TRIANGLES;
    }
}

GLenum FOpenGLPipelineState::ConvertBlendFactor(EBlendFactor factor) const
{
    switch (factor)
    {
        case EBlendFactor::Zero:         return GL_ZERO;
        case EBlendFactor::One:          return GL_ONE;
        case EBlendFactor::SrcColor:     return GL_SRC_COLOR;
        case EBlendFactor::InvSrcColor:  return GL_ONE_MINUS_SRC_COLOR;
        case EBlendFactor::SrcAlpha:     return GL_SRC_ALPHA;
        case EBlendFactor::InvSrcAlpha:  return GL_ONE_MINUS_SRC_ALPHA;
        case EBlendFactor::DestColor:    return GL_DST_COLOR;
        case EBlendFactor::InvDestColor: return GL_ONE_MINUS_DST_COLOR;
        case EBlendFactor::DestAlpha:    return GL_DST_ALPHA;
        case EBlendFactor::InvDestAlpha: return GL_ONE_MINUS_DST_ALPHA;
        default: return GL_ONE;
    }
}

GLenum FOpenGLPipelineState::ConvertBlendOp(EBlendOp op) const
{
    switch (op)
    {
        case EBlendOp::Add:             return GL_FUNC_ADD;
        case EBlendOp::Subtract:        return GL_FUNC_SUBTRACT;
        case EBlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case EBlendOp::Min:             return GL_MIN;
        case EBlendOp::Max:             return GL_MAX;
        default: return GL_FUNC_ADD;
    }
}

GLenum FOpenGLPipelineState::ConvertCompareFunc(EComparisonFunc func) const
{
    switch (func)
    {
        case EComparisonFunc::Never:        return GL_NEVER;
        case EComparisonFunc::Less:         return GL_LESS;
        case EComparisonFunc::Equal:        return GL_EQUAL;
        case EComparisonFunc::LessEqual:    return GL_LEQUAL;
        case EComparisonFunc::Greater:      return GL_GREATER;
        case EComparisonFunc::NotEqual:     return GL_NOTEQUAL;
        case EComparisonFunc::GreaterEqual: return GL_GEQUAL;
        case EComparisonFunc::Always:       return GL_ALWAYS;
        default: return GL_LESS;
    }
}

void FOpenGLPipelineState::ConvertVertexFormat(EVertexFormat format, 
                                                GLint& size, GLenum& type, 
                                                GLboolean& normalized) const
{
    normalized = GL_FALSE;
    
    switch (format)
    {
        case EVertexFormat::Float1:
            size = 1; type = GL_FLOAT; break;
        case EVertexFormat::Float2:
            size = 2; type = GL_FLOAT; break;
        case EVertexFormat::Float3:
            size = 3; type = GL_FLOAT; break;
        case EVertexFormat::Float4:
            size = 4; type = GL_FLOAT; break;
        case EVertexFormat::Int1:
            size = 1; type = GL_INT; break;
        case EVertexFormat::Int2:
            size = 2; type = GL_INT; break;
        case EVertexFormat::Int3:
            size = 3; type = GL_INT; break;
        case EVertexFormat::Int4:
            size = 4; type = GL_INT; break;
        case EVertexFormat::UInt1:
            size = 1; type = GL_UNSIGNED_INT; break;
        case EVertexFormat::UInt2:
            size = 2; type = GL_UNSIGNED_INT; break;
        case EVertexFormat::UInt3:
            size = 3; type = GL_UNSIGNED_INT; break;
        case EVertexFormat::UInt4:
            size = 4; type = GL_UNSIGNED_INT; break;
        default:
            size = 4; type = GL_FLOAT; break;
    }
}

// ============================================================================
// FOpenGLStateCache Implementation
// ============================================================================

FOpenGLStateCache::FOpenGLStateCache()
{
    Reset();
}

void FOpenGLStateCache::Reset()
{
    // Reset to OpenGL defaults
    m_viewport = { 0, 0, 0, 0, 0, 1 };
    m_scissor = { 0, 0, 0, 0, false };
    m_blend = { false, GL_ONE, GL_ZERO, GL_ONE, GL_ZERO, GL_FUNC_ADD, GL_FUNC_ADD, { 0, 0, 0, 0 } };
    m_depth = { true, true, GL_LESS, 0, 1 };
    m_stencil = { false, GL_ALWAYS, 0, 0xFFFFFFFF, 0xFFFFFFFF, GL_KEEP, GL_KEEP, GL_KEEP };
    m_rasterizer = { GL_BACK, true, GL_CCW, GL_FILL, 0, 0, false };
    m_colorMask = { true, true, true, true };
    
    m_program = 0;
    m_vao = 0;
    m_fbo = 0;
    m_activeTexture = 0;
    
    for (uint32 i = 0; i < MaxTextureUnits; ++i)
    {
        m_textures[i] = { 0, 0, 0 };
    }
    
    for (uint32 i = 0; i < MaxBufferTargets; ++i)
    {
        m_buffers[i] = 0;
    }
    
    for (uint32 i = 0; i < MaxUniformBuffers; ++i)
    {
        m_uniformBuffers[i] = 0;
    }
}

void FOpenGLStateCache::Invalidate()
{
    // Force all state to be re-applied by setting invalid values
    m_viewport.width = -1;
    m_scissor.width = -1;
    m_program = 0xFFFFFFFF;
    m_vao = 0xFFFFFFFF;
    m_fbo = 0xFFFFFFFF;
}

void FOpenGLStateCache::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
{
    if (m_viewport.x != x || m_viewport.y != y || 
        m_viewport.width != width || m_viewport.height != height)
    {
        m_viewport.x = x;
        m_viewport.y = y;
        m_viewport.width = width;
        m_viewport.height = height;
        glViewport(static_cast<GLint>(x), static_cast<GLint>(y), 
                   static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    }
    
    if (m_viewport.minDepth != minDepth || m_viewport.maxDepth != maxDepth)
    {
        m_viewport.minDepth = minDepth;
        m_viewport.maxDepth = maxDepth;
        glDepthRange(minDepth, maxDepth);
    }
}

void FOpenGLStateCache::SetScissorRect(int32 x, int32 y, int32 width, int32 height)
{
    if (m_scissor.x != x || m_scissor.y != y || 
        m_scissor.width != width || m_scissor.height != height)
    {
        m_scissor.x = x;
        m_scissor.y = y;
        m_scissor.width = width;
        m_scissor.height = height;
        glScissor(x, y, width, height);
    }
}

void FOpenGLStateCache::SetScissorEnabled(bool enabled)
{
    if (m_scissor.enabled != enabled)
    {
        m_scissor.enabled = enabled;
        if (enabled)
            glEnable(GL_SCISSOR_TEST);
        else
            glDisable(GL_SCISSOR_TEST);
    }
}

void FOpenGLStateCache::SetBlendEnabled(bool enabled)
{
    if (m_blend.enabled != enabled)
    {
        m_blend.enabled = enabled;
        if (enabled)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
    }
}

void FOpenGLStateCache::SetBlendFunc(GLenum srcColor, GLenum dstColor, GLenum srcAlpha, GLenum dstAlpha)
{
    if (m_blend.srcColor != srcColor || m_blend.dstColor != dstColor ||
        m_blend.srcAlpha != srcAlpha || m_blend.dstAlpha != dstAlpha)
    {
        m_blend.srcColor = srcColor;
        m_blend.dstColor = dstColor;
        m_blend.srcAlpha = srcAlpha;
        m_blend.dstAlpha = dstAlpha;
        glBlendFuncSeparate(srcColor, dstColor, srcAlpha, dstAlpha);
    }
}

void FOpenGLStateCache::SetBlendEquation(GLenum colorOp, GLenum alphaOp)
{
    if (m_blend.colorOp != colorOp || m_blend.alphaOp != alphaOp)
    {
        m_blend.colorOp = colorOp;
        m_blend.alphaOp = alphaOp;
        glBlendEquationSeparate(colorOp, alphaOp);
    }
}

void FOpenGLStateCache::SetBlendColor(float r, float g, float b, float a)
{
    if (m_blend.color[0] != r || m_blend.color[1] != g ||
        m_blend.color[2] != b || m_blend.color[3] != a)
    {
        m_blend.color[0] = r;
        m_blend.color[1] = g;
        m_blend.color[2] = b;
        m_blend.color[3] = a;
        glBlendColor(r, g, b, a);
    }
}

void FOpenGLStateCache::SetDepthTestEnabled(bool enabled)
{
    if (m_depth.testEnabled != enabled)
    {
        m_depth.testEnabled = enabled;
        if (enabled)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
    }
}

void FOpenGLStateCache::SetDepthWriteEnabled(bool enabled)
{
    if (m_depth.writeEnabled != enabled)
    {
        m_depth.writeEnabled = enabled;
        glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    }
}

void FOpenGLStateCache::SetDepthFunc(GLenum func)
{
    if (m_depth.func != func)
    {
        m_depth.func = func;
        glDepthFunc(func);
    }
}

void FOpenGLStateCache::SetDepthRange(float nearVal, float farVal)
{
    if (m_depth.nearVal != nearVal || m_depth.farVal != farVal)
    {
        m_depth.nearVal = nearVal;
        m_depth.farVal = farVal;
        glDepthRange(nearVal, farVal);
    }
}

void FOpenGLStateCache::SetStencilTestEnabled(bool enabled)
{
    if (m_stencil.enabled != enabled)
    {
        m_stencil.enabled = enabled;
        if (enabled)
            glEnable(GL_STENCIL_TEST);
        else
            glDisable(GL_STENCIL_TEST);
    }
}

void FOpenGLStateCache::SetStencilFunc(GLenum func, int32 ref, uint32 mask)
{
    if (m_stencil.func != func || m_stencil.ref != ref || m_stencil.readMask != mask)
    {
        m_stencil.func = func;
        m_stencil.ref = ref;
        m_stencil.readMask = mask;
        glStencilFunc(func, ref, mask);
    }
}

void FOpenGLStateCache::SetStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass)
{
    if (m_stencil.sfail != sfail || m_stencil.dpfail != dpfail || m_stencil.dppass != dppass)
    {
        m_stencil.sfail = sfail;
        m_stencil.dpfail = dpfail;
        m_stencil.dppass = dppass;
        glStencilOp(sfail, dpfail, dppass);
    }
}

void FOpenGLStateCache::SetStencilMask(uint32 mask)
{
    if (m_stencil.writeMask != mask)
    {
        m_stencil.writeMask = mask;
        glStencilMask(mask);
    }
}

void FOpenGLStateCache::SetCullMode(GLenum mode)
{
    if (mode == 0)
    {
        if (m_rasterizer.cullEnabled)
        {
            m_rasterizer.cullEnabled = false;
            glDisable(GL_CULL_FACE);
        }
    }
    else
    {
        if (!m_rasterizer.cullEnabled)
        {
            m_rasterizer.cullEnabled = true;
            glEnable(GL_CULL_FACE);
        }
        if (m_rasterizer.cullMode != mode)
        {
            m_rasterizer.cullMode = mode;
            glCullFace(mode);
        }
    }
}

void FOpenGLStateCache::SetFrontFace(GLenum face)
{
    if (m_rasterizer.frontFace != face)
    {
        m_rasterizer.frontFace = face;
        glFrontFace(face);
    }
}

void FOpenGLStateCache::SetPolygonMode(GLenum mode)
{
    if (m_rasterizer.polygonMode != mode)
    {
        m_rasterizer.polygonMode = mode;
        glPolygonMode(GL_FRONT_AND_BACK, mode);
    }
}

void FOpenGLStateCache::SetPolygonOffset(float factor, float units)
{
    if (m_rasterizer.offsetFactor != factor || m_rasterizer.offsetUnits != units)
    {
        m_rasterizer.offsetFactor = factor;
        m_rasterizer.offsetUnits = units;
        glPolygonOffset(factor, units);
    }
}

void FOpenGLStateCache::SetPolygonOffsetEnabled(bool enabled)
{
    if (m_rasterizer.offsetEnabled != enabled)
    {
        m_rasterizer.offsetEnabled = enabled;
        if (enabled)
            glEnable(GL_POLYGON_OFFSET_FILL);
        else
            glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

void FOpenGLStateCache::SetColorMask(bool r, bool g, bool b, bool a)
{
    if (m_colorMask.r != r || m_colorMask.g != g || m_colorMask.b != b || m_colorMask.a != a)
    {
        m_colorMask.r = r;
        m_colorMask.g = g;
        m_colorMask.b = b;
        m_colorMask.a = a;
        glColorMask(r ? GL_TRUE : GL_FALSE, g ? GL_TRUE : GL_FALSE,
                   b ? GL_TRUE : GL_FALSE, a ? GL_TRUE : GL_FALSE);
    }
}

void FOpenGLStateCache::SetProgram(GLuint program)
{
    if (m_program != program)
    {
        m_program = program;
        glUseProgram(program);
    }
}

void FOpenGLStateCache::SetVertexArray(GLuint vao)
{
    if (m_vao != vao)
    {
        m_vao = vao;
        glBindVertexArray(vao);
    }
}

void FOpenGLStateCache::SetFramebuffer(GLuint fbo)
{
    if (m_fbo != fbo)
    {
        m_fbo = fbo;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }
}

void FOpenGLStateCache::SetActiveTexture(uint32 unit)
{
    if (m_activeTexture != unit)
    {
        m_activeTexture = unit;
        glActiveTexture(GL_TEXTURE0 + unit);
    }
}

void FOpenGLStateCache::SetTexture(uint32 unit, GLenum target, GLuint texture)
{
    if (unit >= MaxTextureUnits)
    {
        return;
    }
    
    if (m_textures[unit].target != target || m_textures[unit].texture != texture)
    {
        SetActiveTexture(unit);
        m_textures[unit].target = target;
        m_textures[unit].texture = texture;
        glBindTexture(target, texture);
    }
}

void FOpenGLStateCache::SetSampler(uint32 unit, GLuint sampler)
{
    if (unit >= MaxTextureUnits)
    {
        return;
    }
    
    if (m_textures[unit].sampler != sampler)
    {
        m_textures[unit].sampler = sampler;
        glBindSampler(unit, sampler);
    }
}

void FOpenGLStateCache::SetBuffer(GLenum target, GLuint buffer)
{
    // Map target to index
    uint32 index = 0;
    switch (target)
    {
        case GL_ARRAY_BUFFER:         index = 0; break;
        case GL_ELEMENT_ARRAY_BUFFER: index = 1; break;
        case GL_UNIFORM_BUFFER:       index = 2; break;
        case GL_SHADER_STORAGE_BUFFER: index = 3; break;
        case GL_COPY_READ_BUFFER:     index = 4; break;
        case GL_COPY_WRITE_BUFFER:    index = 5; break;
        default: index = 0; break;
    }
    
    if (index < MaxBufferTargets && m_buffers[index] != buffer)
    {
        m_buffers[index] = buffer;
        glBindBuffer(target, buffer);
    }
}

void FOpenGLStateCache::SetUniformBuffer(uint32 index, GLuint buffer)
{
    if (index < MaxUniformBuffers && m_uniformBuffers[index] != buffer)
    {
        m_uniformBuffers[index] = buffer;
        glBindBufferBase(GL_UNIFORM_BUFFER, index, buffer);
    }
}

} // namespace MonsterEngine::OpenGL
