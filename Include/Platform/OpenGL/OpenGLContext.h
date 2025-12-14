#pragma once

/**
 * OpenGLContext.h
 * 
 * OpenGL 4.6 context management for Windows
 * Reference: UE5 OpenGLDrv/Private/Windows/OpenGLWindows.cpp
 * 
 * This file contains:
 * - OpenGL context creation and management
 * - WGL extension loading
 * - Platform-specific context handling
 */

#include "Core/CoreMinimal.h"
#include "Platform/OpenGL/OpenGLDefinitions.h"
#include "Containers/String.h"
#include "Containers/Array.h"

// Note: Windows.h is already included via OpenGLDefinitions.h

namespace MonsterEngine::OpenGL {

// Use MonsterEngine types
using MonsterEngine::FString;
using MonsterEngine::TArray;

// ============================================================================
// WGL Constants
// ============================================================================

#if PLATFORM_WINDOWS

// WGL_ARB_create_context
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_LAYER_PLANE_ARB = 0x2093;
constexpr int WGL_CONTEXT_FLAGS_ARB = 0x2094;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;

// WGL_ARB_create_context flags
constexpr int WGL_CONTEXT_DEBUG_BIT_ARB = 0x0001;
constexpr int WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB = 0x0002;

// WGL_ARB_create_context_profile
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;
constexpr int WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB = 0x00000002;

// WGL_ARB_pixel_format
constexpr int WGL_DRAW_TO_WINDOW_ARB = 0x2001;
constexpr int WGL_ACCELERATION_ARB = 0x2003;
constexpr int WGL_SUPPORT_OPENGL_ARB = 0x2010;
constexpr int WGL_DOUBLE_BUFFER_ARB = 0x2011;
constexpr int WGL_PIXEL_TYPE_ARB = 0x2013;
constexpr int WGL_COLOR_BITS_ARB = 0x2014;
constexpr int WGL_DEPTH_BITS_ARB = 0x2022;
constexpr int WGL_STENCIL_BITS_ARB = 0x2023;
constexpr int WGL_FULL_ACCELERATION_ARB = 0x2027;
constexpr int WGL_TYPE_RGBA_ARB = 0x202B;
constexpr int WGL_SAMPLE_BUFFERS_ARB = 0x2041;
constexpr int WGL_SAMPLES_ARB = 0x2042;

// WGL function pointer types
typedef HGLRC (WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int* attribList);
typedef BOOL (WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC hdc, const int* piAttribIList, const FLOAT* pfAttribFList, UINT nMaxFormats, int* piFormats, UINT* nNumFormats);
typedef BOOL (WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int interval);
typedef int (WINAPI* PFNWGLGETSWAPINTERVALEXTPROC)(void);

// WGL function pointers
extern PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;
extern PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB;
extern PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;
extern PFNWGLGETSWAPINTERVALEXTPROC wglGetSwapIntervalEXT;

#endif // PLATFORM_WINDOWS

// ============================================================================
// OpenGL Context Configuration
// ============================================================================

/**
 * OpenGL context creation parameters
 */
struct FOpenGLContextConfig
{
    int32 majorVersion = 4;
    int32 minorVersion = 6;
    bool coreProfile = true;
    bool forwardCompatible = true;
    bool debugContext = false;
    int32 colorBits = 32;
    int32 depthBits = 24;
    int32 stencilBits = 8;
    int32 samples = 0;  // MSAA samples, 0 = disabled
    bool doubleBuffer = true;
    bool sRGB = true;
    
    FOpenGLContextConfig() = default;
};

// ============================================================================
// Platform OpenGL Context
// ============================================================================

#if PLATFORM_WINDOWS

/**
 * Windows-specific OpenGL context
 * Reference: UE5 FPlatformOpenGLContext
 */
struct FPlatformOpenGLContext
{
    HWND windowHandle = nullptr;
    HDC deviceContext = nullptr;
    HGLRC openGLContext = nullptr;
    bool releaseWindowOnDestroy = false;
    int32 syncInterval = -1;
    GLuint viewportFramebuffer = 0;
    GLuint vertexArrayObject = 0;
    GLuint backBufferTexture = 0;
    GLenum backBufferTarget = 0;
    
    FPlatformOpenGLContext() = default;
};

#endif // PLATFORM_WINDOWS

// ============================================================================
// OpenGL Context Manager
// ============================================================================

/**
 * OpenGL context manager
 * Handles context creation, management, and destruction
 */
class FOpenGLContextManager
{
public:
    FOpenGLContextManager();
    ~FOpenGLContextManager();
    
    // Non-copyable
    FOpenGLContextManager(const FOpenGLContextManager&) = delete;
    FOpenGLContextManager& operator=(const FOpenGLContextManager&) = delete;
    
    /**
     * Initialize OpenGL and create the main context
     * @param windowHandle Native window handle (HWND on Windows)
     * @param config Context configuration
     * @return true if initialization succeeded
     */
    bool Initialize(void* windowHandle, const FOpenGLContextConfig& config = FOpenGLContextConfig());
    
    /**
     * Initialize from an existing OpenGL context (e.g., created by GLFW)
     * @param existingContext Existing OpenGL context handle (HGLRC on Windows)
     * @param existingDC Existing device context (HDC on Windows)
     * @param windowHandle Native window handle (HWND on Windows)
     * @return true if initialization succeeded
     */
    bool InitializeFromExistingContext(void* existingContext, void* existingDC, void* windowHandle);
    
    /**
     * Shutdown OpenGL and destroy all contexts
     */
    void Shutdown();
    
    /**
     * Check if OpenGL is initialized
     */
    bool IsInitialized() const { return m_initialized; }
    
    /**
     * Make the main context current on this thread
     */
    bool MakeCurrent();
    
    /**
     * Release the current context from this thread
     */
    void ReleaseCurrent();
    
    /**
     * Swap buffers (present)
     */
    void SwapBuffers();
    
    /**
     * Set vertical sync interval
     * @param interval 0 = disabled, 1 = vsync, 2 = half refresh rate, etc.
     */
    void SetVSync(int32 interval);
    
    /**
     * Get the current vsync interval
     */
    int32 GetVSync() const;
    
    /**
     * Get OpenGL version string
     */
    FString GetVersionString() const;
    
    /**
     * Get OpenGL vendor string
     */
    FString GetVendorString() const;
    
    /**
     * Get OpenGL renderer string
     */
    FString GetRendererString() const;
    
    /**
     * Get GLSL version string
     */
    FString GetGLSLVersionString() const;
    
    /**
     * Check if an extension is supported
     */
    bool IsExtensionSupported(const char* extensionName) const;
    
    /**
     * Get the main context
     */
#if PLATFORM_WINDOWS
    FPlatformOpenGLContext* GetMainContext() { return &m_mainContext; }
    const FPlatformOpenGLContext* GetMainContext() const { return &m_mainContext; }
#endif
    
    /**
     * Get singleton instance
     */
    static FOpenGLContextManager& Get();
    
private:
    /**
     * Initialize WGL extensions
     */
    bool InitializeWGLExtensions();
    
    /**
     * Create a dummy window for WGL extension loading
     */
    bool CreateDummyWindow();
    
    /**
     * Destroy the dummy window
     */
    void DestroyDummyWindow();
    
    /**
     * Set pixel format for the device context
     */
    bool SetPixelFormat(HDC dc, const FOpenGLContextConfig& config);
    
    /**
     * Create OpenGL 4.6 core profile context
     */
    bool CreateCoreContext(const FOpenGLContextConfig& config);
    
    /**
     * Initialize debug output if available
     */
    void InitializeDebugOutput();
    
    /**
     * Query device capabilities
     */
    void QueryCapabilities();
    
private:
    bool m_initialized = false;
    FOpenGLContextConfig m_config;
    
#if PLATFORM_WINDOWS
    FPlatformOpenGLContext m_mainContext;
    HWND m_dummyWindow = nullptr;
    HDC m_dummyDC = nullptr;
    HGLRC m_dummyContext = nullptr;
#endif
    
    // Cached info
    FString m_versionString;
    FString m_vendorString;
    FString m_rendererString;
    FString m_glslVersionString;
    TArray<FString> m_extensions;
    
    // Singleton
    static FOpenGLContextManager* s_instance;
};

// ============================================================================
// Scoped Context Helper
// ============================================================================

/**
 * RAII helper for making a context current
 */
class FScopedGLContext
{
public:
    FScopedGLContext(FOpenGLContextManager& manager)
        : m_manager(manager)
    {
        m_manager.MakeCurrent();
    }
    
    ~FScopedGLContext()
    {
        m_manager.ReleaseCurrent();
    }
    
private:
    FOpenGLContextManager& m_manager;
};

} // namespace MonsterEngine::OpenGL
