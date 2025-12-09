/**
 * OpenGLContext.cpp
 * 
 * OpenGL 4.6 context management implementation for Windows
 * Reference: UE5 OpenGLDrv/Private/Windows/OpenGLWindows.cpp
 */

#include "Platform/OpenGL/OpenGLContext.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Core/Logging/LogMacros.h"

#if PLATFORM_WINDOWS
    #include "Windows/AllowWindowsPlatformTypes.h"
    #include <Windows.h>
    #include "Windows/HideWindowsPlatformTypes.h"
#endif

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogOpenGLContext, Log, All);

namespace MonsterEngine::OpenGL {

// ============================================================================
// WGL Function Pointers
// ============================================================================

#if PLATFORM_WINDOWS
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = nullptr;
PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = nullptr;
PFNWGLGETSWAPINTERVALEXTPROC wglGetSwapIntervalEXT = nullptr;
#endif

// ============================================================================
// Singleton Instance
// ============================================================================

FOpenGLContextManager* FOpenGLContextManager::s_instance = nullptr;

FOpenGLContextManager& FOpenGLContextManager::Get()
{
    if (!s_instance)
    {
        s_instance = new FOpenGLContextManager();
    }
    return *s_instance;
}

// ============================================================================
// OpenGL Debug Callback
// ============================================================================

#if PLATFORM_WINDOWS

static void APIENTRY OpenGLDebugCallback(
    GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar* message,
    const void* userParam)
{
    // Convert source to string
    const char* sourceStr = "Unknown";
    switch (source)
    {
        case GL_DEBUG_SOURCE_API:             sourceStr = "API"; break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   sourceStr = "Window System"; break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: sourceStr = "Shader Compiler"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     sourceStr = "Third Party"; break;
        case GL_DEBUG_SOURCE_APPLICATION:     sourceStr = "Application"; break;
        case GL_DEBUG_SOURCE_OTHER:           sourceStr = "Other"; break;
    }
    
    // Convert type to string
    const char* typeStr = "Unknown";
    switch (type)
    {
        case GL_DEBUG_TYPE_ERROR:               typeStr = "Error"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeStr = "Deprecated"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  typeStr = "Undefined Behavior"; break;
        case GL_DEBUG_TYPE_PORTABILITY:         typeStr = "Portability"; break;
        case GL_DEBUG_TYPE_PERFORMANCE:         typeStr = "Performance"; break;
        case GL_DEBUG_TYPE_MARKER:              typeStr = "Marker"; break;
        case GL_DEBUG_TYPE_PUSH_GROUP:          typeStr = "Push Group"; break;
        case GL_DEBUG_TYPE_POP_GROUP:           typeStr = "Pop Group"; break;
        case GL_DEBUG_TYPE_OTHER:               typeStr = "Other"; break;
    }
    
    // Log based on severity using OutputDebugString
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "OpenGL [%s/%s]: %s\n", sourceStr, typeStr, message);
    
    switch (severity)
    {
        case GL_DEBUG_SEVERITY_HIGH:
        case GL_DEBUG_SEVERITY_MEDIUM:
        case GL_DEBUG_SEVERITY_LOW:
        case GL_DEBUG_SEVERITY_NOTIFICATION:
            OutputDebugStringA(buffer);
            break;
    }
}

/**
 * Dummy window procedure for WGL extension loading
 */
static LRESULT CALLBACK DummyWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

#endif // PLATFORM_WINDOWS

// ============================================================================
// FOpenGLContextManager Implementation
// ============================================================================

FOpenGLContextManager::FOpenGLContextManager()
{
    s_instance = this;
}

FOpenGLContextManager::~FOpenGLContextManager()
{
    Shutdown();
    if (s_instance == this)
    {
        s_instance = nullptr;
    }
}

bool FOpenGLContextManager::Initialize(void* windowHandle, const FOpenGLContextConfig& config)
{
    if (m_initialized)
    {
        OutputDebugStringA("OpenGL: Warning\n");
        return true;
    }
    
    m_config = config;
    
    OutputDebugStringA("OpenGL: Info\n");

#if PLATFORM_WINDOWS
    // Step 1: Create dummy window and context to load WGL extensions
    if (!InitializeWGLExtensions())
    {
        OutputDebugStringA("OpenGL: Error\n");
        return false;
    }
    
    // Step 2: Setup main context with the actual window
    m_mainContext.windowHandle = static_cast<HWND>(windowHandle);
    m_mainContext.releaseWindowOnDestroy = false;
    
    // Get device context
    m_mainContext.deviceContext = GetDC(m_mainContext.windowHandle);
    if (!m_mainContext.deviceContext)
    {
        OutputDebugStringA("OpenGL: Error\n");
        return false;
    }
    
    // Set pixel format
    if (!SetPixelFormat(m_mainContext.deviceContext, config))
    {
        OutputDebugStringA("OpenGL: Error\n");
        ReleaseDC(m_mainContext.windowHandle, m_mainContext.deviceContext);
        return false;
    }
    
    // Create OpenGL 4.6 core profile context
    if (!CreateCoreContext(config))
    {
        OutputDebugStringA("OpenGL: Error\n");
        ReleaseDC(m_mainContext.windowHandle, m_mainContext.deviceContext);
        return false;
    }
    
    // Make context current
    if (!wglMakeCurrent(m_mainContext.deviceContext, m_mainContext.openGLContext))
    {
        OutputDebugStringA("OpenGL: Error\n");
        wglDeleteContext(m_mainContext.openGLContext);
        ReleaseDC(m_mainContext.windowHandle, m_mainContext.deviceContext);
        return false;
    }
    
    // Load OpenGL functions
    if (!LoadOpenGLFunctions())
    {
        OutputDebugStringA("OpenGL: Error\n");
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m_mainContext.openGLContext);
        ReleaseDC(m_mainContext.windowHandle, m_mainContext.deviceContext);
        return false;
    }
    
    // Initialize debug output if requested
    if (config.debugContext)
    {
        InitializeDebugOutput();
    }
    
    // Query and cache capabilities
    QueryCapabilities();
    
    // Create default VAO (required for core profile)
    glGenVertexArrays(1, &m_mainContext.vertexArrayObject);
    glBindVertexArray(m_mainContext.vertexArrayObject);
    
    // Set default state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    
    // Enable seamless cubemap sampling
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    
    // Set clip control to match D3D/Vulkan conventions (optional)
    if (glClipControl)
    {
        // GL_UPPER_LEFT = 0x8CA2, GL_ZERO_TO_ONE = 0x935F
        glClipControl(0x8CA2, 0x935F);
    }
    
    // Set vsync
    SetVSync(1);
    
    m_initialized = true;
    
    OutputDebugStringA("OpenGL: Info\n");
    OutputDebugStringA("OpenGL: Info\n");
    OutputDebugStringA("OpenGL: Info\n");
    OutputDebugStringA("OpenGL: Info\n");
    OutputDebugStringA("OpenGL: Info\n");
    
    return true;
    
#else
    OutputDebugStringA("OpenGL: Error\n");
    return false;
#endif
}

void FOpenGLContextManager::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }
    
    OutputDebugStringA("OpenGL: Info\n");

#if PLATFORM_WINDOWS
    // Delete VAO
    if (m_mainContext.vertexArrayObject)
    {
        glDeleteVertexArrays(1, &m_mainContext.vertexArrayObject);
        m_mainContext.vertexArrayObject = 0;
    }
    
    // Release context
    wglMakeCurrent(nullptr, nullptr);
    
    // Delete OpenGL context
    if (m_mainContext.openGLContext)
    {
        wglDeleteContext(m_mainContext.openGLContext);
        m_mainContext.openGLContext = nullptr;
    }
    
    // Release device context
    if (m_mainContext.deviceContext && m_mainContext.windowHandle)
    {
        ReleaseDC(m_mainContext.windowHandle, m_mainContext.deviceContext);
        m_mainContext.deviceContext = nullptr;
    }
    
    // Cleanup dummy resources
    DestroyDummyWindow();
#endif
    
    m_initialized = false;
    m_extensions.Empty();
    
    OutputDebugStringA("OpenGL: Info\n");
}

bool FOpenGLContextManager::MakeCurrent()
{
#if PLATFORM_WINDOWS
    if (!m_mainContext.openGLContext || !m_mainContext.deviceContext)
    {
        return false;
    }
    return wglMakeCurrent(m_mainContext.deviceContext, m_mainContext.openGLContext) == TRUE;
#else
    return false;
#endif
}

void FOpenGLContextManager::ReleaseCurrent()
{
#if PLATFORM_WINDOWS
    wglMakeCurrent(nullptr, nullptr);
#endif
}

void FOpenGLContextManager::SwapBuffers()
{
#if PLATFORM_WINDOWS
    if (m_mainContext.deviceContext)
    {
        ::SwapBuffers(m_mainContext.deviceContext);
    }
#endif
}

void FOpenGLContextManager::SetVSync(int32 interval)
{
#if PLATFORM_WINDOWS
    if (wglSwapIntervalEXT)
    {
        wglSwapIntervalEXT(interval);
        m_mainContext.syncInterval = interval;
    }
#endif
}

int32 FOpenGLContextManager::GetVSync() const
{
#if PLATFORM_WINDOWS
    if (wglGetSwapIntervalEXT)
    {
        return wglGetSwapIntervalEXT();
    }
#endif
    return m_mainContext.syncInterval;
}

FString FOpenGLContextManager::GetVersionString() const
{
    return m_versionString;
}

FString FOpenGLContextManager::GetVendorString() const
{
    return m_vendorString;
}

FString FOpenGLContextManager::GetRendererString() const
{
    return m_rendererString;
}

FString FOpenGLContextManager::GetGLSLVersionString() const
{
    return m_glslVersionString;
}

bool FOpenGLContextManager::IsExtensionSupported(const char* extensionName) const
{
    FString extName(extensionName);
    for (const auto& ext : m_extensions)
    {
        if (ext == extName)
        {
            return true;
        }
    }
    return false;
}

#if PLATFORM_WINDOWS

bool FOpenGLContextManager::InitializeWGLExtensions()
{
    // Create dummy window
    if (!CreateDummyWindow())
    {
        return false;
    }
    
    // Set basic pixel format
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    
    int pixelFormat = ChoosePixelFormat(m_dummyDC, &pfd);
    if (!pixelFormat)
    {
        OutputDebugStringA("OpenGL: Error\n");
        DestroyDummyWindow();
        return false;
    }
    
    if (!::SetPixelFormat(m_dummyDC, pixelFormat, &pfd))
    {
        OutputDebugStringA("OpenGL: Error\n");
        DestroyDummyWindow();
        return false;
    }
    
    // Create legacy OpenGL context
    m_dummyContext = wglCreateContext(m_dummyDC);
    if (!m_dummyContext)
    {
        OutputDebugStringA("OpenGL: Error\n");
        DestroyDummyWindow();
        return false;
    }
    
    // Make it current
    if (!wglMakeCurrent(m_dummyDC, m_dummyContext))
    {
        OutputDebugStringA("OpenGL: Error\n");
        wglDeleteContext(m_dummyContext);
        DestroyDummyWindow();
        return false;
    }
    
    // Load WGL extensions
    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
    wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    wglGetSwapIntervalEXT = (PFNWGLGETSWAPINTERVALEXTPROC)wglGetProcAddress("wglGetSwapIntervalEXT");
    
    if (!wglCreateContextAttribsARB)
    {
        OutputDebugStringA("OpenGL: Error\n");
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m_dummyContext);
        DestroyDummyWindow();
        return false;
    }
    
    // Release dummy context (keep window for now)
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(m_dummyContext);
    m_dummyContext = nullptr;
    
    OutputDebugStringA("OpenGL: Debug\n");
    return true;
}

bool FOpenGLContextManager::CreateDummyWindow()
{
    // Register dummy window class
    static const wchar_t* className = L"MonsterEngineOpenGLDummy";
    static bool classRegistered = false;
    
    if (!classRegistered)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = DummyWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = className;
        
        if (!RegisterClassExW(&wc))
        {
            OutputDebugStringA("OpenGL: Error\n");
            return false;
        }
        classRegistered = true;
    }
    
    // Create dummy window
    m_dummyWindow = CreateWindowExW(
        0,
        className,
        L"OpenGL Dummy",
        WS_OVERLAPPEDWINDOW,
        0, 0, 1, 1,
        nullptr, nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    
    if (!m_dummyWindow)
    {
        OutputDebugStringA("OpenGL: Error\n");
        return false;
    }
    
    m_dummyDC = GetDC(m_dummyWindow);
    if (!m_dummyDC)
    {
        OutputDebugStringA("OpenGL: Error\n");
        DestroyWindow(m_dummyWindow);
        m_dummyWindow = nullptr;
        return false;
    }
    
    return true;
}

void FOpenGLContextManager::DestroyDummyWindow()
{
    if (m_dummyDC && m_dummyWindow)
    {
        ReleaseDC(m_dummyWindow, m_dummyDC);
        m_dummyDC = nullptr;
    }
    
    if (m_dummyWindow)
    {
        DestroyWindow(m_dummyWindow);
        m_dummyWindow = nullptr;
    }
}

bool FOpenGLContextManager::SetPixelFormat(HDC dc, const FOpenGLContextConfig& config)
{
    if (wglChoosePixelFormatARB)
    {
        // Use modern pixel format selection
        int attribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
            WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
            WGL_DOUBLE_BUFFER_ARB, config.doubleBuffer ? GL_TRUE : GL_FALSE,
            WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
            WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
            WGL_COLOR_BITS_ARB, config.colorBits,
            WGL_DEPTH_BITS_ARB, config.depthBits,
            WGL_STENCIL_BITS_ARB, config.stencilBits,
            WGL_SAMPLE_BUFFERS_ARB, config.samples > 0 ? GL_TRUE : GL_FALSE,
            WGL_SAMPLES_ARB, config.samples,
            0
        };
        
        int pixelFormat;
        UINT numFormats;
        
        if (wglChoosePixelFormatARB(dc, attribs, nullptr, 1, &pixelFormat, &numFormats) && numFormats > 0)
        {
            PIXELFORMATDESCRIPTOR pfd;
            DescribePixelFormat(dc, pixelFormat, sizeof(pfd), &pfd);
            return ::SetPixelFormat(dc, pixelFormat, &pfd) == TRUE;
        }
    }
    
    // Fallback to legacy pixel format selection
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    if (config.doubleBuffer) pfd.dwFlags |= PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = static_cast<BYTE>(config.colorBits);
    pfd.cDepthBits = static_cast<BYTE>(config.depthBits);
    pfd.cStencilBits = static_cast<BYTE>(config.stencilBits);
    pfd.iLayerType = PFD_MAIN_PLANE;
    
    int pixelFormat = ChoosePixelFormat(dc, &pfd);
    if (!pixelFormat)
    {
        return false;
    }
    
    return ::SetPixelFormat(dc, pixelFormat, &pfd) == TRUE;
}

bool FOpenGLContextManager::CreateCoreContext(const FOpenGLContextConfig& config)
{
    if (!wglCreateContextAttribsARB)
    {
        OutputDebugStringA("OpenGL: Error\n");
        return false;
    }
    
    int contextFlags = WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
    if (config.debugContext)
    {
        contextFlags |= WGL_CONTEXT_DEBUG_BIT_ARB;
    }
    
    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, config.majorVersion,
        WGL_CONTEXT_MINOR_VERSION_ARB, config.minorVersion,
        WGL_CONTEXT_FLAGS_ARB, contextFlags,
        WGL_CONTEXT_PROFILE_MASK_ARB, config.coreProfile ? 
            WGL_CONTEXT_CORE_PROFILE_BIT_ARB : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
        0
    };
    
    m_mainContext.openGLContext = wglCreateContextAttribsARB(
        m_mainContext.deviceContext, 
        nullptr, 
        attribs
    );
    
    if (!m_mainContext.openGLContext)
    {
        OutputDebugStringA("OpenGL: Failed to create context\n");
        return false;
    }
    
    OutputDebugStringA("OpenGL: Debug\n");
    
    return true;
}

#endif // PLATFORM_WINDOWS

void FOpenGLContextManager::InitializeDebugOutput()
{
    if (glDebugMessageCallback)
    {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(OpenGLDebugCallback, nullptr);
        
        // Enable all messages
        if (glDebugMessageControl)
        {
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            
            // Optionally disable notification-level messages
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        }
        
        OutputDebugStringA("OpenGL: Info\n");
    }
    else
    {
        OutputDebugStringA("OpenGL: Warning\n");
    }
}

void FOpenGLContextManager::QueryCapabilities()
{
    // Get version strings
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);
    
    m_versionString = version ? FString(reinterpret_cast<const char*>(version)) : FString("Unknown");
    m_vendorString = vendor ? FString(reinterpret_cast<const char*>(vendor)) : FString("Unknown");
    m_rendererString = renderer ? FString(reinterpret_cast<const char*>(renderer)) : FString("Unknown");
    m_glslVersionString = glslVersion ? FString(reinterpret_cast<const char*>(glslVersion)) : FString("Unknown");
    
    // Get extensions
    m_extensions.Empty();
    
    if (glGetStringi)
    {
        GLint numExtensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
        
        for (GLint i = 0; i < numExtensions; ++i)
        {
            const GLubyte* ext = glGetStringi(GL_EXTENSIONS, i);
            if (ext)
            {
                m_extensions.Add(FString(reinterpret_cast<const char*>(ext)));
            }
        }
    }
    
    OutputDebugStringA("OpenGL: Extensions loaded\n");
}

} // namespace MonsterEngine::OpenGL
