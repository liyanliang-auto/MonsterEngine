#pragma once

/**
 * OpenGLFunctions.h
 * 
 * OpenGL 4.6 function pointer declarations and loader
 * Reference: UE5 OpenGLDrv/Private/Windows/OpenGLWindows.h
 * 
 * This file contains:
 * - OpenGL function pointer type definitions
 * - Function pointer declarations
 * - Function loader interface
 */

#include "Platform/OpenGL/OpenGLDefinitions.h"

namespace MonsterEngine::OpenGL {

// ============================================================================
// OpenGL Function Pointer Types
// ============================================================================

// Core functions (from opengl32.dll)
typedef void (APIENTRY* PFNGLCLEARPROC)(GLbitfield mask);
typedef void (APIENTRY* PFNGLCLEARCOLORPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (APIENTRY* PFNGLCLEARDEPTHPROC)(GLdouble depth);
typedef void (APIENTRY* PFNGLCLEARSTENCILPROC)(GLint s);
typedef void (APIENTRY* PFNGLDEPTHFUNCPROC)(GLenum func);
typedef void (APIENTRY* PFNGLDEPTHMASKPROC)(GLboolean flag);
typedef void (APIENTRY* PFNGLDEPTHRANGEPROC)(GLdouble nearVal, GLdouble farVal);
typedef void (APIENTRY* PFNGLENABLEPROC)(GLenum cap);
typedef void (APIENTRY* PFNGLDISABLEPROC)(GLenum cap);
typedef void (APIENTRY* PFNGLFINISHPROC)(void);
typedef void (APIENTRY* PFNGLFLUSHPROC)(void);
typedef void (APIENTRY* PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (APIENTRY* PFNGLSCISSORPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (APIENTRY* PFNGLCULLFACEPROC)(GLenum mode);
typedef void (APIENTRY* PFNGLFRONTFACEPROC)(GLenum mode);
typedef void (APIENTRY* PFNGLPOLYGONMODEPROC)(GLenum face, GLenum mode);
typedef void (APIENTRY* PFNGLPOLYGONOFFSETPROC)(GLfloat factor, GLfloat units);
typedef void (APIENTRY* PFNGLBLENDFUNCPROC)(GLenum sfactor, GLenum dfactor);
typedef void (APIENTRY* PFNGLCOLORMASKPROC)(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
typedef void (APIENTRY* PFNGLSTENCILFUNCPROC)(GLenum func, GLint ref, GLuint mask);
typedef void (APIENTRY* PFNGLSTENCILMASKPROC)(GLuint mask);
typedef void (APIENTRY* PFNGLSTENCILOPPROC)(GLenum fail, GLenum zfail, GLenum zpass);
typedef void (APIENTRY* PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);
typedef void (APIENTRY* PFNGLDRAWELEMENTSPROC)(GLenum mode, GLsizei count, GLenum type, const void* indices);
typedef GLenum (APIENTRY* PFNGLGETERRORPROC)(void);
typedef const GLubyte* (APIENTRY* PFNGLGETSTRINGPROC)(GLenum name);
typedef void (APIENTRY* PFNGLGETINTEGERVPROC)(GLenum pname, GLint* data);
typedef void (APIENTRY* PFNGLGETFLOATVPROC)(GLenum pname, GLfloat* data);
typedef GLboolean (APIENTRY* PFNGLISENABLEDPROC)(GLenum cap);
typedef void (APIENTRY* PFNGLPIXELSTOREFPROC)(GLenum pname, GLfloat param);
typedef void (APIENTRY* PFNGLPIXELSTOREIPROC)(GLenum pname, GLint param);
typedef void (APIENTRY* PFNGLREADPIXELSPROC)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels);
typedef void (APIENTRY* PFNGLTEXIMAGE1DPROC)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const void* pixels);
typedef void (APIENTRY* PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
typedef void (APIENTRY* PFNGLTEXPARAMETERFPROC)(GLenum target, GLenum pname, GLfloat param);
typedef void (APIENTRY* PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void (APIENTRY* PFNGLGENTEXTURESPROC)(GLsizei n, GLuint* textures);
typedef void (APIENTRY* PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint* textures);
typedef void (APIENTRY* PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);

// Extension functions (loaded via wglGetProcAddress)
// Buffer functions
typedef void (APIENTRY* PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
typedef void (APIENTRY* PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);
typedef void (APIENTRY* PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (APIENTRY* PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
typedef void (APIENTRY* PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
typedef void (APIENTRY* PFNGLBUFFERSTORAGEPROC)(GLenum target, GLsizeiptr size, const void* data, GLbitfield flags);
typedef void* (APIENTRY* PFNGLMAPBUFFERPROC)(GLenum target, GLenum access);
typedef void* (APIENTRY* PFNGLMAPBUFFERRANGEPROC)(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
typedef GLboolean (APIENTRY* PFNGLUNMAPBUFFERPROC)(GLenum target);
typedef void (APIENTRY* PFNGLCOPYBUFFERSUBDATAPROC)(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size);
typedef void (APIENTRY* PFNGLBINDBUFFERBASEPROC)(GLenum target, GLuint index, GLuint buffer);
typedef void (APIENTRY* PFNGLBINDBUFFERRANGEPROC)(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);

// Vertex array functions
typedef void (APIENTRY* PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void (APIENTRY* PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint* arrays);
typedef void (APIENTRY* PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (APIENTRY* PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (APIENTRY* PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (APIENTRY* PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
typedef void (APIENTRY* PFNGLVERTEXATTRIBIPOINTERPROC)(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer);
typedef void (APIENTRY* PFNGLVERTEXATTRIBDIVISORPROC)(GLuint index, GLuint divisor);
typedef void (APIENTRY* PFNGLVERTEXATTRIBBINDINGPROC)(GLuint attribindex, GLuint bindingindex);
typedef void (APIENTRY* PFNGLVERTEXATTRIBFORMATPROC)(GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset);
typedef void (APIENTRY* PFNGLVERTEXATTRIBIFORMATPROC)(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset);
typedef void (APIENTRY* PFNGLBINDVERTEXBUFFERPROC)(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride);
typedef void (APIENTRY* PFNGLVERTEXBINDINGDIVISORPROC)(GLuint bindingindex, GLuint divisor);

// Texture functions (extended)
typedef void (APIENTRY* PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void (APIENTRY* PFNGLTEXIMAGE3DPROC)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void* pixels);
typedef void (APIENTRY* PFNGLTEXSUBIMAGE1DPROC)(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void* pixels);
typedef void (APIENTRY* PFNGLTEXSUBIMAGE2DPROC)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels);
typedef void (APIENTRY* PFNGLTEXSUBIMAGE3DPROC)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void* pixels);
typedef void (APIENTRY* PFNGLTEXSTORAGE1DPROC)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width);
typedef void (APIENTRY* PFNGLTEXSTORAGE2DPROC)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (APIENTRY* PFNGLTEXSTORAGE3DPROC)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth);
typedef void (APIENTRY* PFNGLCOMPRESSEDTEXIMAGE2DPROC)(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void* data);
typedef void (APIENTRY* PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data);
typedef void (APIENTRY* PFNGLGENERATEMIPMAPPROC)(GLenum target);
typedef void (APIENTRY* PFNGLTEXTUREVIEWPROC)(GLuint texture, GLenum target, GLuint origtexture, GLenum internalformat, GLuint minlevel, GLuint numlevels, GLuint minlayer, GLuint numlayers);
typedef void (APIENTRY* PFNGLCOPYIMAGESUBDATAPROC)(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth);

// Sampler functions
typedef void (APIENTRY* PFNGLGENSAMPLERSPROC)(GLsizei count, GLuint* samplers);
typedef void (APIENTRY* PFNGLDELETESAMPLERSPROC)(GLsizei count, const GLuint* samplers);
typedef void (APIENTRY* PFNGLBINDSAMPLERPROC)(GLuint unit, GLuint sampler);
typedef void (APIENTRY* PFNGLSAMPLERPARAMETERIPROC)(GLuint sampler, GLenum pname, GLint param);
typedef void (APIENTRY* PFNGLSAMPLERPARAMETERFPROC)(GLuint sampler, GLenum pname, GLfloat param);
typedef void (APIENTRY* PFNGLSAMPLERPARAMETERFVPROC)(GLuint sampler, GLenum pname, const GLfloat* param);

// Framebuffer functions
typedef void (APIENTRY* PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void (APIENTRY* PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* framebuffers);
typedef void (APIENTRY* PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef GLenum (APIENTRY* PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (APIENTRY* PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (APIENTRY* PFNGLFRAMEBUFFERTEXTUREPROC)(GLenum target, GLenum attachment, GLuint texture, GLint level);
typedef void (APIENTRY* PFNGLFRAMEBUFFERTEXTURELAYERPROC)(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer);
typedef void (APIENTRY* PFNGLGENRENDERBUFFERSPROC)(GLsizei n, GLuint* renderbuffers);
typedef void (APIENTRY* PFNGLDELETERENDERBUFFERSPROC)(GLsizei n, const GLuint* renderbuffers);
typedef void (APIENTRY* PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint renderbuffer);
typedef void (APIENTRY* PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (APIENTRY* PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC)(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (APIENTRY* PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef void (APIENTRY* PFNGLDRAWBUFFERSPROC)(GLsizei n, const GLenum* bufs);
typedef void (APIENTRY* PFNGLREADBUFFERPROC)(GLenum src);
typedef void (APIENTRY* PFNGLBLITFRAMEBUFFERPROC)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
typedef void (APIENTRY* PFNGLCLEARBUFFERFVPROC)(GLenum buffer, GLint drawbuffer, const GLfloat* value);
typedef void (APIENTRY* PFNGLCLEARBUFFERIVPROC)(GLenum buffer, GLint drawbuffer, const GLint* value);
typedef void (APIENTRY* PFNGLCLEARBUFFERUIVPROC)(GLenum buffer, GLint drawbuffer, const GLuint* value);
typedef void (APIENTRY* PFNGLCLEARBUFFERFIPROC)(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil);

// Shader functions
typedef GLuint (APIENTRY* PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRY* PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (APIENTRY* PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void (APIENTRY* PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (APIENTRY* PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void (APIENTRY* PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void (APIENTRY* PFNGLSHADERBINARYPROC)(GLsizei count, const GLuint* shaders, GLenum binaryformat, const void* binary, GLsizei length);
typedef void (APIENTRY* PFNGLSPECIALIZESHADERPROC)(GLuint shader, const GLchar* pEntryPoint, GLuint numSpecializationConstants, const GLuint* pConstantIndex, const GLuint* pConstantValue);

// Program functions
typedef GLuint (APIENTRY* PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRY* PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void (APIENTRY* PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRY* PFNGLDETACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRY* PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRY* PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (APIENTRY* PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void (APIENTRY* PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void (APIENTRY* PFNGLVALIDATEPROGRAMPROC)(GLuint program);
typedef GLint (APIENTRY* PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef GLuint (APIENTRY* PFNGLGETUNIFORMBLOCKINDEXPROC)(GLuint program, const GLchar* uniformBlockName);
typedef void (APIENTRY* PFNGLUNIFORMBLOCKBINDINGPROC)(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);
typedef void (APIENTRY* PFNGLBINDATTRIBLOCATIONPROC)(GLuint program, GLuint index, const GLchar* name);
typedef GLint (APIENTRY* PFNGLGETATTRIBLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void (APIENTRY* PFNGLBINDFRAGDATALOCATIONPROC)(GLuint program, GLuint color, const GLchar* name);

// Uniform functions
typedef void (APIENTRY* PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void (APIENTRY* PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void (APIENTRY* PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void (APIENTRY* PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (APIENTRY* PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (APIENTRY* PFNGLUNIFORM1IVPROC)(GLint location, GLsizei count, const GLint* value);
typedef void (APIENTRY* PFNGLUNIFORM1FVPROC)(GLint location, GLsizei count, const GLfloat* value);
typedef void (APIENTRY* PFNGLUNIFORM2FVPROC)(GLint location, GLsizei count, const GLfloat* value);
typedef void (APIENTRY* PFNGLUNIFORM3FVPROC)(GLint location, GLsizei count, const GLfloat* value);
typedef void (APIENTRY* PFNGLUNIFORM4FVPROC)(GLint location, GLsizei count, const GLfloat* value);
typedef void (APIENTRY* PFNGLUNIFORMMATRIX3FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
typedef void (APIENTRY* PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);

// Draw functions (extended)
typedef void (APIENTRY* PFNGLDRAWARRAYSINSTANCEDPROC)(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);
typedef void (APIENTRY* PFNGLDRAWELEMENTSINSTANCEDPROC)(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instancecount);
typedef void (APIENTRY* PFNGLDRAWELEMENTSBASEVERTEXPROC)(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint basevertex);
typedef void (APIENTRY* PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXPROC)(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instancecount, GLint basevertex);
typedef void (APIENTRY* PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXBASEINSTANCEPROC)(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instancecount, GLint basevertex, GLuint baseinstance);
typedef void (APIENTRY* PFNGLDRAWARRAYSINDIRECTPROC)(GLenum mode, const void* indirect);
typedef void (APIENTRY* PFNGLDRAWELEMENTSINDIRECTPROC)(GLenum mode, GLenum type, const void* indirect);
typedef void (APIENTRY* PFNGLMULTIDRAWARRAYSINDIRECTPROC)(GLenum mode, const void* indirect, GLsizei drawcount, GLsizei stride);
typedef void (APIENTRY* PFNGLMULTIDRAWELEMENTSINDIRECTPROC)(GLenum mode, GLenum type, const void* indirect, GLsizei drawcount, GLsizei stride);

// Blend functions (extended)
typedef void (APIENTRY* PFNGLBLENDEQUATIONPROC)(GLenum mode);
typedef void (APIENTRY* PFNGLBLENDEQUATIONSEPARATEPROC)(GLenum modeRGB, GLenum modeAlpha);
typedef void (APIENTRY* PFNGLBLENDFUNCSEPARATEPROC)(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
typedef void (APIENTRY* PFNGLBLENDCOLORPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (APIENTRY* PFNGLBLENDFUNCIPROC)(GLuint buf, GLenum src, GLenum dst);
typedef void (APIENTRY* PFNGLBLENDEQUATIONIPROC)(GLuint buf, GLenum mode);
typedef void (APIENTRY* PFNGLBLENDFUNCSEPARATEIPROC)(GLuint buf, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
typedef void (APIENTRY* PFNGLBLENDEQUATIONSEPARATEIPROC)(GLuint buf, GLenum modeRGB, GLenum modeAlpha);
typedef void (APIENTRY* PFNGLCOLORMASKIPROC)(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a);

// Stencil functions (extended)
typedef void (APIENTRY* PFNGLSTENCILFUNCSEPARATEPROC)(GLenum face, GLenum func, GLint ref, GLuint mask);
typedef void (APIENTRY* PFNGLSTENCILMASKSEPARATEPROC)(GLenum face, GLuint mask);
typedef void (APIENTRY* PFNGLSTENCILOPSEPARATEPROC)(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);

// Query functions
typedef void (APIENTRY* PFNGLGENQUERIESPROC)(GLsizei n, GLuint* ids);
typedef void (APIENTRY* PFNGLDELETEQUERIESPROC)(GLsizei n, const GLuint* ids);
typedef void (APIENTRY* PFNGLBEGINQUERYPROC)(GLenum target, GLuint id);
typedef void (APIENTRY* PFNGLENDQUERYPROC)(GLenum target);
typedef void (APIENTRY* PFNGLGETQUERYOBJECTIVPROC)(GLuint id, GLenum pname, GLint* params);
typedef void (APIENTRY* PFNGLGETQUERYOBJECTUIVPROC)(GLuint id, GLenum pname, GLuint* params);
typedef void (APIENTRY* PFNGLGETQUERYOBJECTI64VPROC)(GLuint id, GLenum pname, GLint64* params);
typedef void (APIENTRY* PFNGLGETQUERYOBJECTUI64VPROC)(GLuint id, GLenum pname, GLuint64* params);
typedef void (APIENTRY* PFNGLQUERYCOUNTERPROC)(GLuint id, GLenum target);

// Sync functions
typedef GLsync (APIENTRY* PFNGLFENCESYNCPROC)(GLenum condition, GLbitfield flags);
typedef void (APIENTRY* PFNGLDELETESYNCPROC)(GLsync sync);
typedef GLenum (APIENTRY* PFNGLCLIENTWAITSYNCPROC)(GLsync sync, GLbitfield flags, GLuint64 timeout);
typedef void (APIENTRY* PFNGLWAITSYNCPROC)(GLsync sync, GLbitfield flags, GLuint64 timeout);
typedef void (APIENTRY* PFNGLGETSYNCIVPROC)(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei* length, GLint* values);

// Debug functions
typedef void (APIENTRY* PFNGLDEBUGMESSAGECALLBACKPROC)(GLDEBUGPROC callback, const void* userParam);
typedef void (APIENTRY* PFNGLDEBUGMESSAGECONTROLPROC)(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint* ids, GLboolean enabled);
typedef void (APIENTRY* PFNGLDEBUGMESSAGEINSERTPROC)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* buf);
typedef void (APIENTRY* PFNGLOBJECTLABELPROC)(GLenum identifier, GLuint name, GLsizei length, const GLchar* label);
typedef void (APIENTRY* PFNGLPUSHDEBUGGROUPPROC)(GLenum source, GLuint id, GLsizei length, const GLchar* message);
typedef void (APIENTRY* PFNGLPOPDEBUGGROUPPROC)(void);

// Compute shader functions
typedef void (APIENTRY* PFNGLDISPATCHCOMPUTEPROC)(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
typedef void (APIENTRY* PFNGLDISPATCHCOMPUTEINDIRECTPROC)(GLintptr indirect);
typedef void (APIENTRY* PFNGLMEMORYBARRIERPROC)(GLbitfield barriers);

// Image functions
typedef void (APIENTRY* PFNGLBINDIMAGETEXTUREPROC)(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format);

// Get functions (extended)
typedef const GLubyte* (APIENTRY* PFNGLGETSTRINGIPROC)(GLenum name, GLuint index);
typedef void (APIENTRY* PFNGLGETINTEGER64VPROC)(GLenum pname, GLint64* data);
typedef void (APIENTRY* PFNGLGETINTEGERI_VPROC)(GLenum target, GLuint index, GLint* data);

// Clip control (GL 4.5+)
typedef void (APIENTRY* PFNGLCLIPCONTROLPROC)(GLenum origin, GLenum depth);

// ============================================================================
// OpenGL Function Pointer Declarations
// ============================================================================

// Core functions
extern PFNGLCLEARPROC glClear;
extern PFNGLCLEARCOLORPROC glClearColor;
extern PFNGLCLEARDEPTHPROC glClearDepth;
extern PFNGLCLEARSTENCILPROC glClearStencil;
extern PFNGLDEPTHFUNCPROC glDepthFunc;
extern PFNGLDEPTHMASKPROC glDepthMask;
extern PFNGLDEPTHRANGEPROC glDepthRange;
extern PFNGLENABLEPROC glEnable;
extern PFNGLDISABLEPROC glDisable;
extern PFNGLFINISHPROC glFinish;
extern PFNGLFLUSHPROC glFlush;
extern PFNGLVIEWPORTPROC glViewport;
extern PFNGLSCISSORPROC glScissor;
extern PFNGLCULLFACEPROC glCullFace;
extern PFNGLFRONTFACEPROC glFrontFace;
extern PFNGLPOLYGONMODEPROC glPolygonMode;
extern PFNGLPOLYGONOFFSETPROC glPolygonOffset;
extern PFNGLBLENDFUNCPROC glBlendFunc;
extern PFNGLCOLORMASKPROC glColorMask;
extern PFNGLSTENCILFUNCPROC glStencilFunc;
extern PFNGLSTENCILMASKPROC glStencilMask;
extern PFNGLSTENCILOPPROC glStencilOp;
extern PFNGLDRAWARRAYSPROC glDrawArrays;
extern PFNGLDRAWELEMENTSPROC glDrawElements;
extern PFNGLGETERRORPROC glGetError;
extern PFNGLGETSTRINGPROC glGetString;
extern PFNGLGETINTEGERVPROC glGetIntegerv;
extern PFNGLGETFLOATVPROC glGetFloatv;
extern PFNGLISENABLEDPROC glIsEnabled;
extern PFNGLPIXELSTOREFPROC glPixelStoref;
extern PFNGLPIXELSTOREIPROC glPixelStorei;
extern PFNGLREADPIXELSPROC glReadPixels;
extern PFNGLTEXIMAGE1DPROC glTexImage1D;
extern PFNGLTEXIMAGE2DPROC glTexImage2D;
extern PFNGLTEXPARAMETERFPROC glTexParameterf;
extern PFNGLTEXPARAMETERIPROC glTexParameteri;
extern PFNGLGENTEXTURESPROC glGenTextures;
extern PFNGLDELETETEXTURESPROC glDeleteTextures;
extern PFNGLBINDTEXTUREPROC glBindTexture;

// Buffer functions
extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLBUFFERSUBDATAPROC glBufferSubData;
extern PFNGLBUFFERSTORAGEPROC glBufferStorage;
extern PFNGLMAPBUFFERPROC glMapBuffer;
extern PFNGLMAPBUFFERRANGEPROC glMapBufferRange;
extern PFNGLUNMAPBUFFERPROC glUnmapBuffer;
extern PFNGLCOPYBUFFERSUBDATAPROC glCopyBufferSubData;
extern PFNGLBINDBUFFERBASEPROC glBindBufferBase;
extern PFNGLBINDBUFFERRANGEPROC glBindBufferRange;

// Vertex array functions
extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
extern PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
extern PFNGLVERTEXATTRIBIPOINTERPROC glVertexAttribIPointer;
extern PFNGLVERTEXATTRIBDIVISORPROC glVertexAttribDivisor;
extern PFNGLVERTEXATTRIBBINDINGPROC glVertexAttribBinding;
extern PFNGLVERTEXATTRIBFORMATPROC glVertexAttribFormat;
extern PFNGLVERTEXATTRIBIFORMATPROC glVertexAttribIFormat;
extern PFNGLBINDVERTEXBUFFERPROC glBindVertexBuffer;
extern PFNGLVERTEXBINDINGDIVISORPROC glVertexBindingDivisor;

// Texture functions
extern PFNGLACTIVETEXTUREPROC glActiveTexture;
extern PFNGLTEXIMAGE3DPROC glTexImage3D;
extern PFNGLTEXSUBIMAGE1DPROC glTexSubImage1D;
extern PFNGLTEXSUBIMAGE2DPROC glTexSubImage2D;
extern PFNGLTEXSUBIMAGE3DPROC glTexSubImage3D;
extern PFNGLTEXSTORAGE1DPROC glTexStorage1D;
extern PFNGLTEXSTORAGE2DPROC glTexStorage2D;
extern PFNGLTEXSTORAGE3DPROC glTexStorage3D;
extern PFNGLCOMPRESSEDTEXIMAGE2DPROC glCompressedTexImage2D;
extern PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC glCompressedTexSubImage2D;
extern PFNGLGENERATEMIPMAPPROC glGenerateMipmap;
extern PFNGLTEXTUREVIEWPROC glTextureView;
extern PFNGLCOPYIMAGESUBDATAPROC glCopyImageSubData;

// Sampler functions
extern PFNGLGENSAMPLERSPROC glGenSamplers;
extern PFNGLDELETESAMPLERSPROC glDeleteSamplers;
extern PFNGLBINDSAMPLERPROC glBindSampler;
extern PFNGLSAMPLERPARAMETERIPROC glSamplerParameteri;
extern PFNGLSAMPLERPARAMETERFPROC glSamplerParameterf;
extern PFNGLSAMPLERPARAMETERFVPROC glSamplerParameterfv;

// Framebuffer functions
extern PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
extern PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
extern PFNGLFRAMEBUFFERTEXTUREPROC glFramebufferTexture;
extern PFNGLFRAMEBUFFERTEXTURELAYERPROC glFramebufferTextureLayer;
extern PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers;
extern PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer;
extern PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage;
extern PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC glRenderbufferStorageMultisample;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;
extern PFNGLDRAWBUFFERSPROC glDrawBuffers;
extern PFNGLREADBUFFERPROC glReadBuffer;
extern PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer;
extern PFNGLCLEARBUFFERFVPROC glClearBufferfv;
extern PFNGLCLEARBUFFERIVPROC glClearBufferiv;
extern PFNGLCLEARBUFFERUIVPROC glClearBufferuiv;
extern PFNGLCLEARBUFFERFIPROC glClearBufferfi;

// Shader functions
extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLDELETESHADERPROC glDeleteShader;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLGETSHADERIVPROC glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
extern PFNGLSHADERBINARYPROC glShaderBinary;
extern PFNGLSPECIALIZESHADERPROC glSpecializeShader;

// Program functions
extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLDETACHSHADERPROC glDetachShader;
extern PFNGLLINKPROGRAMPROC glLinkProgram;
extern PFNGLUSEPROGRAMPROC glUseProgram;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
extern PFNGLVALIDATEPROGRAMPROC glValidateProgram;
extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLGETUNIFORMBLOCKINDEXPROC glGetUniformBlockIndex;
extern PFNGLUNIFORMBLOCKBINDINGPROC glUniformBlockBinding;
extern PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation;
extern PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
extern PFNGLBINDFRAGDATALOCATIONPROC glBindFragDataLocation;

// Uniform functions
extern PFNGLUNIFORM1IPROC glUniform1i;
extern PFNGLUNIFORM1FPROC glUniform1f;
extern PFNGLUNIFORM2FPROC glUniform2f;
extern PFNGLUNIFORM3FPROC glUniform3f;
extern PFNGLUNIFORM4FPROC glUniform4f;
extern PFNGLUNIFORM1IVPROC glUniform1iv;
extern PFNGLUNIFORM1FVPROC glUniform1fv;
extern PFNGLUNIFORM2FVPROC glUniform2fv;
extern PFNGLUNIFORM3FVPROC glUniform3fv;
extern PFNGLUNIFORM4FVPROC glUniform4fv;
extern PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv;
extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;

// Draw functions
extern PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced;
extern PFNGLDRAWELEMENTSINSTANCEDPROC glDrawElementsInstanced;
extern PFNGLDRAWELEMENTSBASEVERTEXPROC glDrawElementsBaseVertex;
extern PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXPROC glDrawElementsInstancedBaseVertex;
extern PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXBASEINSTANCEPROC glDrawElementsInstancedBaseVertexBaseInstance;
extern PFNGLDRAWARRAYSINDIRECTPROC glDrawArraysIndirect;
extern PFNGLDRAWELEMENTSINDIRECTPROC glDrawElementsIndirect;
extern PFNGLMULTIDRAWARRAYSINDIRECTPROC glMultiDrawArraysIndirect;
extern PFNGLMULTIDRAWELEMENTSINDIRECTPROC glMultiDrawElementsIndirect;

// Blend functions
extern PFNGLBLENDEQUATIONPROC glBlendEquation;
extern PFNGLBLENDEQUATIONSEPARATEPROC glBlendEquationSeparate;
extern PFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate;
extern PFNGLBLENDCOLORPROC glBlendColor;
extern PFNGLBLENDFUNCIPROC glBlendFunci;
extern PFNGLBLENDEQUATIONIPROC glBlendEquationi;
extern PFNGLBLENDFUNCSEPARATEIPROC glBlendFuncSeparatei;
extern PFNGLBLENDEQUATIONSEPARATEIPROC glBlendEquationSeparatei;
extern PFNGLCOLORMASKIPROC glColorMaski;

// Stencil functions
extern PFNGLSTENCILFUNCSEPARATEPROC glStencilFuncSeparate;
extern PFNGLSTENCILMASKSEPARATEPROC glStencilMaskSeparate;
extern PFNGLSTENCILOPSEPARATEPROC glStencilOpSeparate;

// Query functions
extern PFNGLGENQUERIESPROC glGenQueries;
extern PFNGLDELETEQUERIESPROC glDeleteQueries;
extern PFNGLBEGINQUERYPROC glBeginQuery;
extern PFNGLENDQUERYPROC glEndQuery;
extern PFNGLGETQUERYOBJECTIVPROC glGetQueryObjectiv;
extern PFNGLGETQUERYOBJECTUIVPROC glGetQueryObjectuiv;
extern PFNGLGETQUERYOBJECTI64VPROC glGetQueryObjecti64v;
extern PFNGLGETQUERYOBJECTUI64VPROC glGetQueryObjectui64v;
extern PFNGLQUERYCOUNTERPROC glQueryCounter;

// Sync functions
extern PFNGLFENCESYNCPROC glFenceSync;
extern PFNGLDELETESYNCPROC glDeleteSync;
extern PFNGLCLIENTWAITSYNCPROC glClientWaitSync;
extern PFNGLWAITSYNCPROC glWaitSync;
extern PFNGLGETSYNCIVPROC glGetSynciv;

// Debug functions
extern PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback;
extern PFNGLDEBUGMESSAGECONTROLPROC glDebugMessageControl;
extern PFNGLDEBUGMESSAGEINSERTPROC glDebugMessageInsert;
extern PFNGLOBJECTLABELPROC glObjectLabel;
extern PFNGLPUSHDEBUGGROUPPROC glPushDebugGroup;
extern PFNGLPOPDEBUGGROUPPROC glPopDebugGroup;

// Compute shader functions
extern PFNGLDISPATCHCOMPUTEPROC glDispatchCompute;
extern PFNGLDISPATCHCOMPUTEINDIRECTPROC glDispatchComputeIndirect;
extern PFNGLMEMORYBARRIERPROC glMemoryBarrier;

// Image functions
extern PFNGLBINDIMAGETEXTUREPROC glBindImageTexture;

// Get functions
extern PFNGLGETSTRINGIPROC glGetStringi;
extern PFNGLGETINTEGER64VPROC glGetInteger64v;
extern PFNGLGETINTEGERI_VPROC glGetIntegeri_v;

// Clip control
extern PFNGLCLIPCONTROLPROC glClipControl;

// ============================================================================
// Function Loader Interface
// ============================================================================

/**
 * Initialize OpenGL function pointers
 * Must be called after creating a valid OpenGL context
 * @return true if all required functions were loaded successfully
 */
bool LoadOpenGLFunctions();

/**
 * Check if OpenGL functions have been loaded
 */
bool IsOpenGLLoaded();

/**
 * Get the last OpenGL error as a string
 */
const char* GetGLErrorString(GLenum error);

/**
 * Check for OpenGL errors and log them
 * @param operation Description of the operation being checked
 * @return true if no errors occurred
 */
bool CheckGLError(const char* operation);

} // namespace MonsterEngine::OpenGL

// Convenience macro for checking OpenGL errors in debug builds
#if MR_BUILD_DEBUG
    #define GL_CHECK(operation) MonsterEngine::OpenGL::CheckGLError(operation)
#else
    #define GL_CHECK(operation) ((void)0)
#endif
