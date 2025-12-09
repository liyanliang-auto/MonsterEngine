/**
 * OpenGLFunctions.cpp
 * 
 * OpenGL 4.6 function pointer definitions and loader implementation
 * Reference: UE5 OpenGLDrv/Private/Windows/OpenGLWindows.cpp
 */

#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Core/Logging/LogMacros.h"

#if PLATFORM_WINDOWS
    #include "Windows/AllowWindowsPlatformTypes.h"
    #include <Windows.h>
    #include "Windows/HideWindowsPlatformTypes.h"
#endif

// Define log category for OpenGL
DEFINE_LOG_CATEGORY_STATIC(LogOpenGL, Log, All);

namespace MonsterEngine::OpenGL {

// ============================================================================
// Function Pointer Definitions
// ============================================================================

// Core functions
PFNGLCLEARPROC glClear = nullptr;
PFNGLCLEARCOLORPROC glClearColor = nullptr;
PFNGLCLEARDEPTHPROC glClearDepth = nullptr;
PFNGLCLEARSTENCILPROC glClearStencil = nullptr;
PFNGLDEPTHFUNCPROC glDepthFunc = nullptr;
PFNGLDEPTHMASKPROC glDepthMask = nullptr;
PFNGLDEPTHRANGEPROC glDepthRange = nullptr;
PFNGLENABLEPROC glEnable = nullptr;
PFNGLDISABLEPROC glDisable = nullptr;
PFNGLFINISHPROC glFinish = nullptr;
PFNGLFLUSHPROC glFlush = nullptr;
PFNGLVIEWPORTPROC glViewport = nullptr;
PFNGLSCISSORPROC glScissor = nullptr;
PFNGLCULLFACEPROC glCullFace = nullptr;
PFNGLFRONTFACEPROC glFrontFace = nullptr;
PFNGLPOLYGONMODEPROC glPolygonMode = nullptr;
PFNGLPOLYGONOFFSETPROC glPolygonOffset = nullptr;
PFNGLBLENDFUNCPROC glBlendFunc = nullptr;
PFNGLCOLORMASKPROC glColorMask = nullptr;
PFNGLSTENCILFUNCPROC glStencilFunc = nullptr;
PFNGLSTENCILMASKPROC glStencilMask = nullptr;
PFNGLSTENCILOPPROC glStencilOp = nullptr;
PFNGLDRAWARRAYSPROC glDrawArrays = nullptr;
PFNGLDRAWELEMENTSPROC glDrawElements = nullptr;
PFNGLGETERRORPROC glGetError = nullptr;
PFNGLGETSTRINGPROC glGetString = nullptr;
PFNGLGETINTEGERVPROC glGetIntegerv = nullptr;
PFNGLGETFLOATVPROC glGetFloatv = nullptr;
PFNGLISENABLEDPROC glIsEnabled = nullptr;
PFNGLPIXELSTOREFPROC glPixelStoref = nullptr;
PFNGLPIXELSTOREIPROC glPixelStorei = nullptr;
PFNGLREADPIXELSPROC glReadPixels = nullptr;
PFNGLTEXIMAGE1DPROC glTexImage1D = nullptr;
PFNGLTEXIMAGE2DPROC glTexImage2D = nullptr;
PFNGLTEXPARAMETERFPROC glTexParameterf = nullptr;
PFNGLTEXPARAMETERIPROC glTexParameteri = nullptr;
PFNGLGENTEXTURESPROC glGenTextures = nullptr;
PFNGLDELETETEXTURESPROC glDeleteTextures = nullptr;
PFNGLBINDTEXTUREPROC glBindTexture = nullptr;

// Buffer functions
PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
PFNGLBUFFERSTORAGEPROC glBufferStorage = nullptr;
PFNGLMAPBUFFERPROC glMapBuffer = nullptr;
PFNGLMAPBUFFERRANGEPROC glMapBufferRange = nullptr;
PFNGLUNMAPBUFFERPROC glUnmapBuffer = nullptr;
PFNGLCOPYBUFFERSUBDATAPROC glCopyBufferSubData = nullptr;
PFNGLBINDBUFFERBASEPROC glBindBufferBase = nullptr;
PFNGLBINDBUFFERRANGEPROC glBindBufferRange = nullptr;

// Vertex array functions
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
PFNGLVERTEXATTRIBIPOINTERPROC glVertexAttribIPointer = nullptr;
PFNGLVERTEXATTRIBDIVISORPROC glVertexAttribDivisor = nullptr;
PFNGLVERTEXATTRIBBINDINGPROC glVertexAttribBinding = nullptr;
PFNGLVERTEXATTRIBFORMATPROC glVertexAttribFormat = nullptr;
PFNGLVERTEXATTRIBIFORMATPROC glVertexAttribIFormat = nullptr;
PFNGLBINDVERTEXBUFFERPROC glBindVertexBuffer = nullptr;
PFNGLVERTEXBINDINGDIVISORPROC glVertexBindingDivisor = nullptr;

// Texture functions
PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
PFNGLTEXIMAGE3DPROC glTexImage3D = nullptr;
PFNGLTEXSUBIMAGE1DPROC glTexSubImage1D = nullptr;
PFNGLTEXSUBIMAGE2DPROC glTexSubImage2D = nullptr;
PFNGLTEXSUBIMAGE3DPROC glTexSubImage3D = nullptr;
PFNGLTEXSTORAGE1DPROC glTexStorage1D = nullptr;
PFNGLTEXSTORAGE2DPROC glTexStorage2D = nullptr;
PFNGLTEXSTORAGE3DPROC glTexStorage3D = nullptr;
PFNGLCOMPRESSEDTEXIMAGE2DPROC glCompressedTexImage2D = nullptr;
PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC glCompressedTexSubImage2D = nullptr;
PFNGLGENERATEMIPMAPPROC glGenerateMipmap = nullptr;
PFNGLTEXTUREVIEWPROC glTextureView = nullptr;
PFNGLCOPYIMAGESUBDATAPROC glCopyImageSubData = nullptr;

// Sampler functions
PFNGLGENSAMPLERSPROC glGenSamplers = nullptr;
PFNGLDELETESAMPLERSPROC glDeleteSamplers = nullptr;
PFNGLBINDSAMPLERPROC glBindSampler = nullptr;
PFNGLSAMPLERPARAMETERIPROC glSamplerParameteri = nullptr;
PFNGLSAMPLERPARAMETERFPROC glSamplerParameterf = nullptr;
PFNGLSAMPLERPARAMETERFVPROC glSamplerParameterfv = nullptr;

// Framebuffer functions
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = nullptr;
PFNGLFRAMEBUFFERTEXTUREPROC glFramebufferTexture = nullptr;
PFNGLFRAMEBUFFERTEXTURELAYERPROC glFramebufferTextureLayer = nullptr;
PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers = nullptr;
PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers = nullptr;
PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage = nullptr;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC glRenderbufferStorageMultisample = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = nullptr;
PFNGLDRAWBUFFERSPROC glDrawBuffers = nullptr;
PFNGLREADBUFFERPROC glReadBuffer = nullptr;
PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer = nullptr;
PFNGLCLEARBUFFERFVPROC glClearBufferfv = nullptr;
PFNGLCLEARBUFFERIVPROC glClearBufferiv = nullptr;
PFNGLCLEARBUFFERUIVPROC glClearBufferuiv = nullptr;
PFNGLCLEARBUFFERFIPROC glClearBufferfi = nullptr;

// Shader functions
PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLSHADERBINARYPROC glShaderBinary = nullptr;
PFNGLSPECIALIZESHADERPROC glSpecializeShader = nullptr;

// Program functions
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLDETACHSHADERPROC glDetachShader = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLVALIDATEPROGRAMPROC glValidateProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLGETUNIFORMBLOCKINDEXPROC glGetUniformBlockIndex = nullptr;
PFNGLUNIFORMBLOCKBINDINGPROC glUniformBlockBinding = nullptr;
PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation = nullptr;
PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation = nullptr;
PFNGLBINDFRAGDATALOCATIONPROC glBindFragDataLocation = nullptr;

// Uniform functions
PFNGLUNIFORM1IPROC glUniform1i = nullptr;
PFNGLUNIFORM1FPROC glUniform1f = nullptr;
PFNGLUNIFORM2FPROC glUniform2f = nullptr;
PFNGLUNIFORM3FPROC glUniform3f = nullptr;
PFNGLUNIFORM4FPROC glUniform4f = nullptr;
PFNGLUNIFORM1IVPROC glUniform1iv = nullptr;
PFNGLUNIFORM1FVPROC glUniform1fv = nullptr;
PFNGLUNIFORM2FVPROC glUniform2fv = nullptr;
PFNGLUNIFORM3FVPROC glUniform3fv = nullptr;
PFNGLUNIFORM4FVPROC glUniform4fv = nullptr;
PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;

// Draw functions
PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced = nullptr;
PFNGLDRAWARRAYSINSTANCEDBASEINSTANCEPROC glDrawArraysInstancedBaseInstance = nullptr;
PFNGLDRAWELEMENTSINSTANCEDPROC glDrawElementsInstanced = nullptr;
PFNGLDRAWELEMENTSBASEVERTEXPROC glDrawElementsBaseVertex = nullptr;
PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXPROC glDrawElementsInstancedBaseVertex = nullptr;
PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXBASEINSTANCEPROC glDrawElementsInstancedBaseVertexBaseInstance = nullptr;
PFNGLDRAWARRAYSINDIRECTPROC glDrawArraysIndirect = nullptr;
PFNGLDRAWELEMENTSINDIRECTPROC glDrawElementsIndirect = nullptr;
PFNGLMULTIDRAWARRAYSINDIRECTPROC glMultiDrawArraysIndirect = nullptr;
PFNGLMULTIDRAWELEMENTSINDIRECTPROC glMultiDrawElementsIndirect = nullptr;

// Blend functions
PFNGLBLENDEQUATIONPROC glBlendEquation = nullptr;
PFNGLBLENDEQUATIONSEPARATEPROC glBlendEquationSeparate = nullptr;
PFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate = nullptr;
PFNGLBLENDCOLORPROC glBlendColor = nullptr;
PFNGLBLENDFUNCIPROC glBlendFunci = nullptr;
PFNGLBLENDEQUATIONIPROC glBlendEquationi = nullptr;
PFNGLBLENDFUNCSEPARATEIPROC glBlendFuncSeparatei = nullptr;
PFNGLBLENDEQUATIONSEPARATEIPROC glBlendEquationSeparatei = nullptr;
PFNGLCOLORMASKIPROC glColorMaski = nullptr;

// Stencil functions
PFNGLSTENCILFUNCSEPARATEPROC glStencilFuncSeparate = nullptr;
PFNGLSTENCILMASKSEPARATEPROC glStencilMaskSeparate = nullptr;
PFNGLSTENCILOPSEPARATEPROC glStencilOpSeparate = nullptr;

// Query functions
PFNGLGENQUERIESPROC glGenQueries = nullptr;
PFNGLDELETEQUERIESPROC glDeleteQueries = nullptr;
PFNGLBEGINQUERYPROC glBeginQuery = nullptr;
PFNGLENDQUERYPROC glEndQuery = nullptr;
PFNGLGETQUERYOBJECTIVPROC glGetQueryObjectiv = nullptr;
PFNGLGETQUERYOBJECTUIVPROC glGetQueryObjectuiv = nullptr;
PFNGLGETQUERYOBJECTI64VPROC glGetQueryObjecti64v = nullptr;
PFNGLGETQUERYOBJECTUI64VPROC glGetQueryObjectui64v = nullptr;
PFNGLQUERYCOUNTERPROC glQueryCounter = nullptr;

// Sync functions
PFNGLFENCESYNCPROC glFenceSync = nullptr;
PFNGLDELETESYNCPROC glDeleteSync = nullptr;
PFNGLCLIENTWAITSYNCPROC glClientWaitSync = nullptr;
PFNGLWAITSYNCPROC glWaitSync = nullptr;
PFNGLGETSYNCIVPROC glGetSynciv = nullptr;

// Debug functions
PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback = nullptr;
PFNGLDEBUGMESSAGECONTROLPROC glDebugMessageControl = nullptr;
PFNGLDEBUGMESSAGEINSERTPROC glDebugMessageInsert = nullptr;
PFNGLOBJECTLABELPROC glObjectLabel = nullptr;
PFNGLPUSHDEBUGGROUPPROC glPushDebugGroup = nullptr;
PFNGLPOPDEBUGGROUPPROC glPopDebugGroup = nullptr;

// Compute shader functions
PFNGLDISPATCHCOMPUTEPROC glDispatchCompute = nullptr;
PFNGLDISPATCHCOMPUTEINDIRECTPROC glDispatchComputeIndirect = nullptr;
PFNGLMEMORYBARRIERPROC glMemoryBarrier = nullptr;

// Image functions
PFNGLBINDIMAGETEXTUREPROC glBindImageTexture = nullptr;

// Get functions
PFNGLGETSTRINGIPROC glGetStringi = nullptr;
PFNGLGETINTEGER64VPROC glGetInteger64v = nullptr;
PFNGLGETINTEGERI_VPROC glGetIntegeri_v = nullptr;

// Clip control
PFNGLCLIPCONTROLPROC glClipControl = nullptr;

// ============================================================================
// Static Variables
// ============================================================================

static bool s_OpenGLLoaded = false;
static HMODULE s_OpenGLDLL = nullptr;

// ============================================================================
// Helper Functions
// ============================================================================

#if PLATFORM_WINDOWS

/**
 * Get function pointer from opengl32.dll
 */
static void* GetGLDLLFunction(const char* name)
{
    if (!s_OpenGLDLL)
    {
        s_OpenGLDLL = LoadLibraryA("opengl32.dll");
        if (!s_OpenGLDLL)
        {
            OutputDebugStringA("OpenGL: Failed to load opengl32.dll\n");
            return nullptr;
        }
    }
    return GetProcAddress(s_OpenGLDLL, name);
}

/**
 * Get function pointer via wglGetProcAddress
 */
static void* GetGLExtFunction(const char* name)
{
    // wglGetProcAddress is loaded from opengl32.dll
    typedef void* (WINAPI* PFNWGLGETPROCADDRESSPROC)(const char*);
    static PFNWGLGETPROCADDRESSPROC wglGetProcAddress_ptr = nullptr;
    
    if (!wglGetProcAddress_ptr)
    {
        wglGetProcAddress_ptr = (PFNWGLGETPROCADDRESSPROC)GetGLDLLFunction("wglGetProcAddress");
        if (!wglGetProcAddress_ptr)
        {
            OutputDebugStringA("OpenGL: Failed to get wglGetProcAddress\n");
            return nullptr;
        }
    }
    
    void* ptr = wglGetProcAddress_ptr(name);
    
    // wglGetProcAddress returns NULL, 1, 2, or 3 for functions in opengl32.dll
    if (ptr == nullptr || ptr == (void*)0x1 || ptr == (void*)0x2 || ptr == (void*)0x3)
    {
        // Try loading from DLL directly
        ptr = GetGLDLLFunction(name);
    }
    
    return ptr;
}

#endif // PLATFORM_WINDOWS

// ============================================================================
// Function Loader Implementation
// ============================================================================

bool LoadOpenGLFunctions()
{
    if (s_OpenGLLoaded)
    {
        return true;
    }

#if PLATFORM_WINDOWS
    OutputDebugStringA("OpenGL: Loading OpenGL 4.6 functions...\n");
    
    bool allLoaded = true;
    
    // Macro to load core functions from DLL
    #define LOAD_GL_DLL_FUNC(func) \
        func = (decltype(func))GetGLDLLFunction(#func); \
        if (!func) { allLoaded = false; }
    
    // Macro to load extension functions via wglGetProcAddress
    #define LOAD_GL_EXT_FUNC(func) \
        func = (decltype(func))GetGLExtFunction(#func); \
        if (!func) { allLoaded = false; }
    
    // Macro for optional functions
    #define LOAD_GL_OPT_FUNC(func) \
        func = (decltype(func))GetGLExtFunction(#func);
    
    // Load core functions from opengl32.dll
    LOAD_GL_DLL_FUNC(glClear);
    LOAD_GL_DLL_FUNC(glClearColor);
    LOAD_GL_DLL_FUNC(glClearDepth);
    LOAD_GL_DLL_FUNC(glClearStencil);
    LOAD_GL_DLL_FUNC(glDepthFunc);
    LOAD_GL_DLL_FUNC(glDepthMask);
    LOAD_GL_DLL_FUNC(glDepthRange);
    LOAD_GL_DLL_FUNC(glEnable);
    LOAD_GL_DLL_FUNC(glDisable);
    LOAD_GL_DLL_FUNC(glFinish);
    LOAD_GL_DLL_FUNC(glFlush);
    LOAD_GL_DLL_FUNC(glViewport);
    LOAD_GL_DLL_FUNC(glScissor);
    LOAD_GL_DLL_FUNC(glCullFace);
    LOAD_GL_DLL_FUNC(glFrontFace);
    LOAD_GL_DLL_FUNC(glPolygonMode);
    LOAD_GL_DLL_FUNC(glPolygonOffset);
    LOAD_GL_DLL_FUNC(glBlendFunc);
    LOAD_GL_DLL_FUNC(glColorMask);
    LOAD_GL_DLL_FUNC(glStencilFunc);
    LOAD_GL_DLL_FUNC(glStencilMask);
    LOAD_GL_DLL_FUNC(glStencilOp);
    LOAD_GL_DLL_FUNC(glDrawArrays);
    LOAD_GL_DLL_FUNC(glDrawElements);
    LOAD_GL_DLL_FUNC(glGetError);
    LOAD_GL_DLL_FUNC(glGetString);
    LOAD_GL_DLL_FUNC(glGetIntegerv);
    LOAD_GL_DLL_FUNC(glGetFloatv);
    LOAD_GL_DLL_FUNC(glIsEnabled);
    LOAD_GL_DLL_FUNC(glPixelStoref);
    LOAD_GL_DLL_FUNC(glPixelStorei);
    LOAD_GL_DLL_FUNC(glReadPixels);
    LOAD_GL_DLL_FUNC(glTexImage1D);
    LOAD_GL_DLL_FUNC(glTexImage2D);
    LOAD_GL_DLL_FUNC(glTexParameterf);
    LOAD_GL_DLL_FUNC(glTexParameteri);
    LOAD_GL_DLL_FUNC(glGenTextures);
    LOAD_GL_DLL_FUNC(glDeleteTextures);
    LOAD_GL_DLL_FUNC(glBindTexture);
    
    // Load extension functions via wglGetProcAddress
    // Buffer functions
    LOAD_GL_EXT_FUNC(glGenBuffers);
    LOAD_GL_EXT_FUNC(glDeleteBuffers);
    LOAD_GL_EXT_FUNC(glBindBuffer);
    LOAD_GL_EXT_FUNC(glBufferData);
    LOAD_GL_EXT_FUNC(glBufferSubData);
    LOAD_GL_EXT_FUNC(glBufferStorage);
    LOAD_GL_EXT_FUNC(glMapBuffer);
    LOAD_GL_EXT_FUNC(glMapBufferRange);
    LOAD_GL_EXT_FUNC(glUnmapBuffer);
    LOAD_GL_EXT_FUNC(glCopyBufferSubData);
    LOAD_GL_EXT_FUNC(glBindBufferBase);
    LOAD_GL_EXT_FUNC(glBindBufferRange);
    
    // Vertex array functions
    LOAD_GL_EXT_FUNC(glGenVertexArrays);
    LOAD_GL_EXT_FUNC(glDeleteVertexArrays);
    LOAD_GL_EXT_FUNC(glBindVertexArray);
    LOAD_GL_EXT_FUNC(glEnableVertexAttribArray);
    LOAD_GL_EXT_FUNC(glDisableVertexAttribArray);
    LOAD_GL_EXT_FUNC(glVertexAttribPointer);
    LOAD_GL_EXT_FUNC(glVertexAttribIPointer);
    LOAD_GL_EXT_FUNC(glVertexAttribDivisor);
    LOAD_GL_OPT_FUNC(glVertexAttribBinding);
    LOAD_GL_OPT_FUNC(glVertexAttribFormat);
    LOAD_GL_OPT_FUNC(glVertexAttribIFormat);
    LOAD_GL_OPT_FUNC(glBindVertexBuffer);
    LOAD_GL_OPT_FUNC(glVertexBindingDivisor);
    
    // Texture functions
    LOAD_GL_EXT_FUNC(glActiveTexture);
    LOAD_GL_EXT_FUNC(glTexImage3D);
    LOAD_GL_EXT_FUNC(glTexSubImage1D);
    LOAD_GL_EXT_FUNC(glTexSubImage2D);
    LOAD_GL_EXT_FUNC(glTexSubImage3D);
    LOAD_GL_EXT_FUNC(glTexStorage1D);
    LOAD_GL_EXT_FUNC(glTexStorage2D);
    LOAD_GL_EXT_FUNC(glTexStorage3D);
    LOAD_GL_EXT_FUNC(glCompressedTexImage2D);
    LOAD_GL_EXT_FUNC(glCompressedTexSubImage2D);
    LOAD_GL_EXT_FUNC(glGenerateMipmap);
    LOAD_GL_OPT_FUNC(glTextureView);
    LOAD_GL_OPT_FUNC(glCopyImageSubData);
    
    // Sampler functions
    LOAD_GL_EXT_FUNC(glGenSamplers);
    LOAD_GL_EXT_FUNC(glDeleteSamplers);
    LOAD_GL_EXT_FUNC(glBindSampler);
    LOAD_GL_EXT_FUNC(glSamplerParameteri);
    LOAD_GL_EXT_FUNC(glSamplerParameterf);
    LOAD_GL_EXT_FUNC(glSamplerParameterfv);
    
    // Framebuffer functions
    LOAD_GL_EXT_FUNC(glGenFramebuffers);
    LOAD_GL_EXT_FUNC(glDeleteFramebuffers);
    LOAD_GL_EXT_FUNC(glBindFramebuffer);
    LOAD_GL_EXT_FUNC(glCheckFramebufferStatus);
    LOAD_GL_EXT_FUNC(glFramebufferTexture2D);
    LOAD_GL_EXT_FUNC(glFramebufferTexture);
    LOAD_GL_EXT_FUNC(glFramebufferTextureLayer);
    LOAD_GL_EXT_FUNC(glGenRenderbuffers);
    LOAD_GL_EXT_FUNC(glDeleteRenderbuffers);
    LOAD_GL_EXT_FUNC(glBindRenderbuffer);
    LOAD_GL_EXT_FUNC(glRenderbufferStorage);
    LOAD_GL_EXT_FUNC(glRenderbufferStorageMultisample);
    LOAD_GL_EXT_FUNC(glFramebufferRenderbuffer);
    LOAD_GL_EXT_FUNC(glDrawBuffers);
    LOAD_GL_EXT_FUNC(glReadBuffer);
    LOAD_GL_EXT_FUNC(glBlitFramebuffer);
    LOAD_GL_EXT_FUNC(glClearBufferfv);
    LOAD_GL_EXT_FUNC(glClearBufferiv);
    LOAD_GL_EXT_FUNC(glClearBufferuiv);
    LOAD_GL_EXT_FUNC(glClearBufferfi);
    
    // Shader functions
    LOAD_GL_EXT_FUNC(glCreateShader);
    LOAD_GL_EXT_FUNC(glDeleteShader);
    LOAD_GL_EXT_FUNC(glShaderSource);
    LOAD_GL_EXT_FUNC(glCompileShader);
    LOAD_GL_EXT_FUNC(glGetShaderiv);
    LOAD_GL_EXT_FUNC(glGetShaderInfoLog);
    LOAD_GL_OPT_FUNC(glShaderBinary);
    LOAD_GL_OPT_FUNC(glSpecializeShader);
    
    // Program functions
    LOAD_GL_EXT_FUNC(glCreateProgram);
    LOAD_GL_EXT_FUNC(glDeleteProgram);
    LOAD_GL_EXT_FUNC(glAttachShader);
    LOAD_GL_EXT_FUNC(glDetachShader);
    LOAD_GL_EXT_FUNC(glLinkProgram);
    LOAD_GL_EXT_FUNC(glUseProgram);
    LOAD_GL_EXT_FUNC(glGetProgramiv);
    LOAD_GL_EXT_FUNC(glGetProgramInfoLog);
    LOAD_GL_EXT_FUNC(glValidateProgram);
    LOAD_GL_EXT_FUNC(glGetUniformLocation);
    LOAD_GL_EXT_FUNC(glGetUniformBlockIndex);
    LOAD_GL_EXT_FUNC(glUniformBlockBinding);
    LOAD_GL_EXT_FUNC(glBindAttribLocation);
    LOAD_GL_EXT_FUNC(glGetAttribLocation);
    LOAD_GL_EXT_FUNC(glBindFragDataLocation);
    
    // Uniform functions
    LOAD_GL_EXT_FUNC(glUniform1i);
    LOAD_GL_EXT_FUNC(glUniform1f);
    LOAD_GL_EXT_FUNC(glUniform2f);
    LOAD_GL_EXT_FUNC(glUniform3f);
    LOAD_GL_EXT_FUNC(glUniform4f);
    LOAD_GL_EXT_FUNC(glUniform1iv);
    LOAD_GL_EXT_FUNC(glUniform1fv);
    LOAD_GL_EXT_FUNC(glUniform2fv);
    LOAD_GL_EXT_FUNC(glUniform3fv);
    LOAD_GL_EXT_FUNC(glUniform4fv);
    LOAD_GL_EXT_FUNC(glUniformMatrix3fv);
    LOAD_GL_EXT_FUNC(glUniformMatrix4fv);
    
    // Draw functions
    LOAD_GL_EXT_FUNC(glDrawArraysInstanced);
    LOAD_GL_OPT_FUNC(glDrawArraysInstancedBaseInstance);
    LOAD_GL_EXT_FUNC(glDrawElementsInstanced);
    LOAD_GL_EXT_FUNC(glDrawElementsBaseVertex);
    LOAD_GL_EXT_FUNC(glDrawElementsInstancedBaseVertex);
    LOAD_GL_OPT_FUNC(glDrawElementsInstancedBaseVertexBaseInstance);
    LOAD_GL_OPT_FUNC(glDrawArraysIndirect);
    LOAD_GL_OPT_FUNC(glDrawElementsIndirect);
    LOAD_GL_OPT_FUNC(glMultiDrawArraysIndirect);
    LOAD_GL_OPT_FUNC(glMultiDrawElementsIndirect);
    
    // Blend functions
    LOAD_GL_EXT_FUNC(glBlendEquation);
    LOAD_GL_EXT_FUNC(glBlendEquationSeparate);
    LOAD_GL_EXT_FUNC(glBlendFuncSeparate);
    LOAD_GL_EXT_FUNC(glBlendColor);
    LOAD_GL_OPT_FUNC(glBlendFunci);
    LOAD_GL_OPT_FUNC(glBlendEquationi);
    LOAD_GL_OPT_FUNC(glBlendFuncSeparatei);
    LOAD_GL_OPT_FUNC(glBlendEquationSeparatei);
    LOAD_GL_OPT_FUNC(glColorMaski);
    
    // Stencil functions
    LOAD_GL_EXT_FUNC(glStencilFuncSeparate);
    LOAD_GL_EXT_FUNC(glStencilMaskSeparate);
    LOAD_GL_EXT_FUNC(glStencilOpSeparate);
    
    // Query functions
    LOAD_GL_EXT_FUNC(glGenQueries);
    LOAD_GL_EXT_FUNC(glDeleteQueries);
    LOAD_GL_EXT_FUNC(glBeginQuery);
    LOAD_GL_EXT_FUNC(glEndQuery);
    LOAD_GL_EXT_FUNC(glGetQueryObjectiv);
    LOAD_GL_EXT_FUNC(glGetQueryObjectuiv);
    LOAD_GL_OPT_FUNC(glGetQueryObjecti64v);
    LOAD_GL_OPT_FUNC(glGetQueryObjectui64v);
    LOAD_GL_OPT_FUNC(glQueryCounter);
    
    // Sync functions
    LOAD_GL_EXT_FUNC(glFenceSync);
    LOAD_GL_EXT_FUNC(glDeleteSync);
    LOAD_GL_EXT_FUNC(glClientWaitSync);
    LOAD_GL_EXT_FUNC(glWaitSync);
    LOAD_GL_EXT_FUNC(glGetSynciv);
    
    // Debug functions (optional)
    LOAD_GL_OPT_FUNC(glDebugMessageCallback);
    LOAD_GL_OPT_FUNC(glDebugMessageControl);
    LOAD_GL_OPT_FUNC(glDebugMessageInsert);
    LOAD_GL_OPT_FUNC(glObjectLabel);
    LOAD_GL_OPT_FUNC(glPushDebugGroup);
    LOAD_GL_OPT_FUNC(glPopDebugGroup);
    
    // Compute shader functions (optional)
    LOAD_GL_OPT_FUNC(glDispatchCompute);
    LOAD_GL_OPT_FUNC(glDispatchComputeIndirect);
    LOAD_GL_OPT_FUNC(glMemoryBarrier);
    
    // Image functions
    LOAD_GL_OPT_FUNC(glBindImageTexture);
    
    // Get functions
    LOAD_GL_EXT_FUNC(glGetStringi);
    LOAD_GL_OPT_FUNC(glGetInteger64v);
    LOAD_GL_OPT_FUNC(glGetIntegeri_v);
    
    // Clip control (GL 4.5+)
    LOAD_GL_OPT_FUNC(glClipControl);
    
    #undef LOAD_GL_DLL_FUNC
    #undef LOAD_GL_EXT_FUNC
    #undef LOAD_GL_OPT_FUNC
    
    s_OpenGLLoaded = allLoaded;
    
    if (allLoaded)
    {
        OutputDebugStringA("OpenGL: Functions loaded successfully\n");
    }
    else
    {
        OutputDebugStringA("OpenGL: Some functions failed to load\n");
    }
    
    return allLoaded;
    
#else
    OutputDebugStringA("OpenGL: Function loading not implemented for this platform\n");
    return false;
#endif
}

bool IsOpenGLLoaded()
{
    return s_OpenGLLoaded;
}

const char* GetGLErrorString(GLenum error)
{
    switch (error)
    {
        case GL_NO_ERROR:                       return "GL_NO_ERROR";
        case GL_INVALID_ENUM:                   return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:                  return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:              return "GL_INVALID_OPERATION";
        case GL_STACK_OVERFLOW:                 return "GL_STACK_OVERFLOW";
        case GL_STACK_UNDERFLOW:                return "GL_STACK_UNDERFLOW";
        case GL_OUT_OF_MEMORY:                  return "GL_OUT_OF_MEMORY";
        case GL_INVALID_FRAMEBUFFER_OPERATION:  return "GL_INVALID_FRAMEBUFFER_OPERATION";
        default:                                return "Unknown GL error";
    }
}

bool CheckGLError(const char* operation)
{
    if (!glGetError)
    {
        return true;
    }
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        OutputDebugStringA("OpenGL: Error detected\n");
        return false;
    }
    return true;
}

} // namespace MonsterEngine::OpenGL
