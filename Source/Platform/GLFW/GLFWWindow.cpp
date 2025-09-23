#include "Platform/GLFW/GLFWWindow.h"
#include "Core/Log.h"

// GLFW includes with error handling
#define GLFW_INCLUDE_NONE

// Try different GLFW include paths
#ifdef GLFW_DIR
    // Use GLFW_DIR environment variable path
    #pragma message("Using GLFW from GLFW_DIR environment variable")
#endif

#include <GLFW/glfw3.h>

// Platform-specific native includes
#if PLATFORM_WINDOWS
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
    #include <windows.h>
#elif PLATFORM_LINUX
    #define GLFW_EXPOSE_NATIVE_X11
    #include <GLFW/glfw3native.h>
    #include <X11/Xlib.h>
#endif

namespace MonsterRender::Platform::GLFW {
    
    // Static error callback for GLFW
    static void glfwErrorCallback(int error, const char* description) {
        MR_LOG_ERROR("GLFW Error " + std::to_string(error) + ": " + description);
    }
    
    //=============================================================================
    // GLFWInputManager Implementation
    //=============================================================================
    
    GLFWInputManager::GLFWInputManager(GLFWwindow* window) 
        : m_window(window) {
        initializeKeyStates();
    }
    
    void GLFWInputManager::initializeKeyStates() {
        // Initialize arrays with reasonable sizes
        m_keyStates.resize(512, false);
        m_keyPressed.resize(512, false);
        m_keyReleased.resize(512, false);
        
        m_mouseButtonStates.resize(8, false);
        m_mouseButtonPressed.resize(8, false);
        m_mouseButtonReleased.resize(8, false);
    }
    
    bool GLFWInputManager::isKeyPressed(EKey key) const {
        int32 glfwKey = convertKeyToGLFW(key);
        return glfwKey >= 0 && glfwKey < static_cast<int32>(m_keyPressed.size()) ? m_keyPressed[glfwKey] : false;
    }
    
    bool GLFWInputManager::isKeyReleased(EKey key) const {
        int32 glfwKey = convertKeyToGLFW(key);
        return glfwKey >= 0 && glfwKey < static_cast<int32>(m_keyReleased.size()) ? m_keyReleased[glfwKey] : false;
    }
    
    bool GLFWInputManager::isKeyDown(EKey key) const {
        int32 glfwKey = convertKeyToGLFW(key);
        return glfwKey >= 0 && glfwKey < static_cast<int32>(m_keyStates.size()) ? m_keyStates[glfwKey] : false;
    }
    
    bool GLFWInputManager::isMouseButtonPressed(EKey button) const {
        int32 glfwButton = convertMouseButtonToGLFW(button);
        return glfwButton >= 0 && glfwButton < static_cast<int32>(m_mouseButtonPressed.size()) ? m_mouseButtonPressed[glfwButton] : false;
    }
    
    bool GLFWInputManager::isMouseButtonReleased(EKey button) const {
        int32 glfwButton = convertMouseButtonToGLFW(button);
        return glfwButton >= 0 && glfwButton < static_cast<int32>(m_mouseButtonReleased.size()) ? m_mouseButtonReleased[glfwButton] : false;
    }
    
    bool GLFWInputManager::isMouseButtonDown(EKey button) const {
        int32 glfwButton = convertMouseButtonToGLFW(button);
        return glfwButton >= 0 && glfwButton < static_cast<int32>(m_mouseButtonStates.size()) ? m_mouseButtonStates[glfwButton] : false;
    }
    
    MousePosition GLFWInputManager::getMousePosition() const {
        return m_mousePosition;
    }
    
    MousePosition GLFWInputManager::getMouseDelta() const {
        return m_mouseDelta;
    }
    
    void GLFWInputManager::processEvents() {
        // Events are processed in GLFW's callback system
        // This method can be used for additional per-frame processing if needed
    }
    
    void GLFWInputManager::resetFrameState() {
        // Reset frame-specific states
        std::fill(m_keyPressed.begin(), m_keyPressed.end(), false);
        std::fill(m_keyReleased.begin(), m_keyReleased.end(), false);
        std::fill(m_mouseButtonPressed.begin(), m_mouseButtonPressed.end(), false);
        std::fill(m_mouseButtonReleased.begin(), m_mouseButtonReleased.end(), false);
        
        m_mouseDelta = MousePosition(0.0, 0.0);
    }
    
    void GLFWInputManager::onKeyCallback(int key, int scancode, int action, int mods) {
        if (key >= 0 && key < static_cast<int>(m_keyStates.size())) {
            switch (action) {
                case GLFW_PRESS:
                    m_keyStates[key] = true;
                    m_keyPressed[key] = true;
                    break;
                case GLFW_RELEASE:
                    m_keyStates[key] = false;
                    m_keyReleased[key] = true;
                    break;
                case GLFW_REPEAT:
                    // Key is held down, state remains true
                    break;
            }
            
            // Trigger callback if set
            if (onKeyEvent) {
                EKey engineKey = convertGLFWToKey(key);
                EInputAction engineAction = static_cast<EInputAction>(action);
                onKeyEvent(InputEvent(engineKey, engineAction, mods));
            }
        }
    }
    
    void GLFWInputManager::onMouseButtonCallback(int button, int action, int mods) {
        if (button >= 0 && button < static_cast<int>(m_mouseButtonStates.size())) {
            switch (action) {
                case GLFW_PRESS:
                    m_mouseButtonStates[button] = true;
                    m_mouseButtonPressed[button] = true;
                    break;
                case GLFW_RELEASE:
                    m_mouseButtonStates[button] = false;
                    m_mouseButtonReleased[button] = true;
                    break;
            }
            
            // Trigger callback if set
            if (onMouseButtonEvent) {
                EKey engineButton = convertGLFWToMouseButton(button);
                onMouseButtonEvent(engineButton, m_mousePosition);
            }
        }
    }
    
    void GLFWInputManager::onMouseMoveCallback(double xpos, double ypos) {
        m_lastMousePosition = m_mousePosition;
        m_mousePosition = MousePosition(xpos, ypos);
        m_mouseDelta = MousePosition(
            m_mousePosition.x - m_lastMousePosition.x,
            m_mousePosition.y - m_lastMousePosition.y
        );
        
        // Trigger callback if set
        if (onMouseMoveEvent) {
            onMouseMoveEvent(m_mousePosition);
        }
    }
    
    void GLFWInputManager::onMouseScrollCallback(double xoffset, double yoffset) {
        // Trigger callback if set
        if (onMouseScrollEvent) {
            onMouseScrollEvent(xoffset, yoffset);
        }
    }
    
    int32 GLFWInputManager::convertKeyToGLFW(EKey key) const {
        // Convert engine key codes to GLFW key codes
        switch (key) {
            case EKey::A: return GLFW_KEY_A;
            case EKey::B: return GLFW_KEY_B;
            case EKey::C: return GLFW_KEY_C;
            case EKey::D: return GLFW_KEY_D;
            case EKey::E: return GLFW_KEY_E;
            case EKey::F: return GLFW_KEY_F;
            case EKey::G: return GLFW_KEY_G;
            case EKey::H: return GLFW_KEY_H;
            case EKey::I: return GLFW_KEY_I;
            case EKey::J: return GLFW_KEY_J;
            case EKey::K: return GLFW_KEY_K;
            case EKey::L: return GLFW_KEY_L;
            case EKey::M: return GLFW_KEY_M;
            case EKey::N: return GLFW_KEY_N;
            case EKey::O: return GLFW_KEY_O;
            case EKey::P: return GLFW_KEY_P;
            case EKey::Q: return GLFW_KEY_Q;
            case EKey::R: return GLFW_KEY_R;
            case EKey::S: return GLFW_KEY_S;
            case EKey::T: return GLFW_KEY_T;
            case EKey::U: return GLFW_KEY_U;
            case EKey::V: return GLFW_KEY_V;
            case EKey::W: return GLFW_KEY_W;
            case EKey::X: return GLFW_KEY_X;
            case EKey::Y: return GLFW_KEY_Y;
            case EKey::Z: return GLFW_KEY_Z;
            
            case EKey::Zero: return GLFW_KEY_0;
            case EKey::One: return GLFW_KEY_1;
            case EKey::Two: return GLFW_KEY_2;
            case EKey::Three: return GLFW_KEY_3;
            case EKey::Four: return GLFW_KEY_4;
            case EKey::Five: return GLFW_KEY_5;
            case EKey::Six: return GLFW_KEY_6;
            case EKey::Seven: return GLFW_KEY_7;
            case EKey::Eight: return GLFW_KEY_8;
            case EKey::Nine: return GLFW_KEY_9;
            
            case EKey::Escape: return GLFW_KEY_ESCAPE;
            case EKey::Enter: return GLFW_KEY_ENTER;
            case EKey::Tab: return GLFW_KEY_TAB;
            case EKey::Backspace: return GLFW_KEY_BACKSPACE;
            case EKey::Space: return GLFW_KEY_SPACE;
            
            case EKey::Left: return GLFW_KEY_LEFT;
            case EKey::Right: return GLFW_KEY_RIGHT;
            case EKey::Up: return GLFW_KEY_UP;
            case EKey::Down: return GLFW_KEY_DOWN;
            
            case EKey::F1: return GLFW_KEY_F1;
            case EKey::F2: return GLFW_KEY_F2;
            case EKey::F3: return GLFW_KEY_F3;
            case EKey::F4: return GLFW_KEY_F4;
            case EKey::F5: return GLFW_KEY_F5;
            case EKey::F6: return GLFW_KEY_F6;
            case EKey::F7: return GLFW_KEY_F7;
            case EKey::F8: return GLFW_KEY_F8;
            case EKey::F9: return GLFW_KEY_F9;
            case EKey::F10: return GLFW_KEY_F10;
            case EKey::F11: return GLFW_KEY_F11;
            case EKey::F12: return GLFW_KEY_F12;
            
            default: return -1;
        }
    }
    
    EKey GLFWInputManager::convertGLFWToKey(int32 glfwKey) const {
        // Convert GLFW key codes to engine key codes
        switch (glfwKey) {
            case GLFW_KEY_A: return EKey::A;
            case GLFW_KEY_B: return EKey::B;
            case GLFW_KEY_C: return EKey::C;
            case GLFW_KEY_D: return EKey::D;
            case GLFW_KEY_E: return EKey::E;
            case GLFW_KEY_F: return EKey::F;
            case GLFW_KEY_G: return EKey::G;
            case GLFW_KEY_H: return EKey::H;
            case GLFW_KEY_I: return EKey::I;
            case GLFW_KEY_J: return EKey::J;
            case GLFW_KEY_K: return EKey::K;
            case GLFW_KEY_L: return EKey::L;
            case GLFW_KEY_M: return EKey::M;
            case GLFW_KEY_N: return EKey::N;
            case GLFW_KEY_O: return EKey::O;
            case GLFW_KEY_P: return EKey::P;
            case GLFW_KEY_Q: return EKey::Q;
            case GLFW_KEY_R: return EKey::R;
            case GLFW_KEY_S: return EKey::S;
            case GLFW_KEY_T: return EKey::T;
            case GLFW_KEY_U: return EKey::U;
            case GLFW_KEY_V: return EKey::V;
            case GLFW_KEY_W: return EKey::W;
            case GLFW_KEY_X: return EKey::X;
            case GLFW_KEY_Y: return EKey::Y;
            case GLFW_KEY_Z: return EKey::Z;
            
            case GLFW_KEY_0: return EKey::Zero;
            case GLFW_KEY_1: return EKey::One;
            case GLFW_KEY_2: return EKey::Two;
            case GLFW_KEY_3: return EKey::Three;
            case GLFW_KEY_4: return EKey::Four;
            case GLFW_KEY_5: return EKey::Five;
            case GLFW_KEY_6: return EKey::Six;
            case GLFW_KEY_7: return EKey::Seven;
            case GLFW_KEY_8: return EKey::Eight;
            case GLFW_KEY_9: return EKey::Nine;
            
            case GLFW_KEY_ESCAPE: return EKey::Escape;
            case GLFW_KEY_ENTER: return EKey::Enter;
            case GLFW_KEY_TAB: return EKey::Tab;
            case GLFW_KEY_BACKSPACE: return EKey::Backspace;
            
            case GLFW_KEY_LEFT: return EKey::Left;
            case GLFW_KEY_RIGHT: return EKey::Right;
            case GLFW_KEY_UP: return EKey::Up;
            case GLFW_KEY_DOWN: return EKey::Down;
            
            default: return EKey::Unknown;
        }
    }
    
    int32 GLFWInputManager::convertMouseButtonToGLFW(EKey button) const {
        switch (button) {
            case EKey::MouseLeft: return GLFW_MOUSE_BUTTON_LEFT;
            case EKey::MouseRight: return GLFW_MOUSE_BUTTON_RIGHT;
            case EKey::MouseMiddle: return GLFW_MOUSE_BUTTON_MIDDLE;
            case EKey::MouseButton4: return GLFW_MOUSE_BUTTON_4;
            case EKey::MouseButton5: return GLFW_MOUSE_BUTTON_5;
            default: return -1;
        }
    }
    
    EKey GLFWInputManager::convertGLFWToMouseButton(int32 glfwButton) const {
        switch (glfwButton) {
            case GLFW_MOUSE_BUTTON_LEFT: return EKey::MouseLeft;
            case GLFW_MOUSE_BUTTON_RIGHT: return EKey::MouseRight;
            case GLFW_MOUSE_BUTTON_MIDDLE: return EKey::MouseMiddle;
            case GLFW_MOUSE_BUTTON_4: return EKey::MouseButton4;
            case GLFW_MOUSE_BUTTON_5: return EKey::MouseButton5;
            default: return EKey::Unknown;
        }
    }
    
    //=============================================================================
    // GLFWWindow Implementation
    //=============================================================================
    
    GLFWWindow::GLFWWindow() {
        MR_LOG_INFO("Creating GLFW window...");
    }
    
    GLFWWindow::~GLFWWindow() {
        shutdown();
    }
    
    bool GLFWWindow::initialize(const WindowProperties& properties) {
        if (m_initialized) {
            MR_LOG_WARNING("GLFWWindow is already initialized");
            return true;
        }
        
        m_properties = properties;
        
        MR_LOG_INFO("Initializing GLFW window: " + properties.title + 
                   " (" + std::to_string(properties.width) + "x" + std::to_string(properties.height) + ")");
        
        // Set error callback
        glfwSetErrorCallback(glfwErrorCallback);
        
        // Initialize GLFW
        if (!glfwInit()) {
            MR_LOG_ERROR("Failed to initialize GLFW");
            return false;
        }
        
        // Configure GLFW window hints
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // We're using Vulkan, not OpenGL
        glfwWindowHint(GLFW_RESIZABLE, properties.resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_DECORATED, properties.decorated ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Start hidden, show after setup
        
        // Create window
        GLFWmonitor* monitor = properties.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
        m_window = glfwCreateWindow(
            static_cast<int>(properties.width),
            static_cast<int>(properties.height),
            properties.title.c_str(),
            monitor,
            nullptr
        );
        
        if (!m_window) {
            MR_LOG_ERROR("Failed to create GLFW window");
            glfwTerminate();
            return false;
        }
        
        // Store this instance in GLFW window user pointer
        glfwSetWindowUserPointer(m_window, this);
        
        // Setup callbacks
        setupCallbacks();
        
        // Create input manager
        m_inputManager = MakeUnique<GLFWInputManager>(m_window);
        
        // Show window
        glfwShowWindow(m_window);
        
        // Update properties from actual window
        updateWindowProperties();
        
        m_initialized = true;
        
        MR_LOG_INFO("GLFW window initialized successfully");
        return true;
    }
    
    void GLFWWindow::shutdown() {
        if (!m_initialized) {
            return;
        }
        
        MR_LOG_INFO("Shutting down GLFW window...");
        
        if (m_window) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
        
        m_inputManager.reset();
        
        glfwTerminate();
        m_initialized = false;
        
        MR_LOG_INFO("GLFW window shut down successfully");
    }
    
    bool GLFWWindow::shouldClose() const {
        return m_window ? glfwWindowShouldClose(m_window) : true;
    }
    
    void GLFWWindow::pollEvents() {
        glfwPollEvents();
        
        if (m_inputManager) {
            m_inputManager->processEvents();
        }
    }
    
    void GLFWWindow::swapBuffers() {
        // For Vulkan, we don't use GLFW's buffer swapping
        // This will be handled by the Vulkan swap chain
        // Keep this method for interface compatibility
    }
    
    void GLFWWindow::setTitle(const String& title) {
        if (m_window) {
            glfwSetWindowTitle(m_window, title.c_str());
            m_properties.title = title;
        }
    }
    
    void GLFWWindow::setSize(uint32 width, uint32 height) {
        if (m_window) {
            glfwSetWindowSize(m_window, static_cast<int>(width), static_cast<int>(height));
        }
    }
    
    void GLFWWindow::setPosition(int32 x, int32 y) {
        if (m_window) {
            glfwSetWindowPos(m_window, x, y);
        }
    }
    
    void GLFWWindow::setFullscreen(bool fullscreen) {
        if (m_window && fullscreen != m_properties.fullscreen) {
            if (fullscreen) {
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            } else {
                glfwSetWindowMonitor(m_window, nullptr, 100, 100, 
                                   static_cast<int>(m_properties.width), 
                                   static_cast<int>(m_properties.height), 0);
            }
            m_properties.fullscreen = fullscreen;
        }
    }
    
    void GLFWWindow::setVSync(bool enabled) {
        // VSync is handled by Vulkan swap chain, not GLFW
        m_properties.vsync = enabled;
    }
    
    WindowProperties GLFWWindow::getProperties() const {
        return m_properties;
    }
    
    uint32 GLFWWindow::getWidth() const {
        return m_properties.width;
    }
    
    uint32 GLFWWindow::getHeight() const {
        return m_properties.height;
    }
    
    String GLFWWindow::getTitle() const {
        return m_properties.title;
    }
    
    bool GLFWWindow::isFullscreen() const {
        return m_properties.fullscreen;
    }
    
    bool GLFWWindow::isMinimized() const {
        return m_minimized;
    }
    
    bool GLFWWindow::isMaximized() const {
        return m_maximized;
    }
    
    bool GLFWWindow::hasFocus() const {
        return m_focused;
    }
    
    void* GLFWWindow::getNativeHandle() const {
#if PLATFORM_WINDOWS
        return glfwGetWin32Window(m_window);
#elif PLATFORM_LINUX
        return reinterpret_cast<void*>(glfwGetX11Window(m_window));
#else
        return nullptr;
#endif
    }
    
    void* GLFWWindow::getNativeDisplayHandle() const {
#if PLATFORM_LINUX
        return glfwGetX11Display();
#else
        return nullptr;
#endif
    }
    
    IInputManager* GLFWWindow::getInputManager() const {
        return m_inputManager.get();
    }
    
    void GLFWWindow::setupCallbacks() {
        if (!m_window) return;
        
        // Window callbacks
        glfwSetWindowCloseCallback(m_window, onWindowCloseCallback);
        glfwSetWindowSizeCallback(m_window, onWindowSizeCallback);
        glfwSetWindowPosCallback(m_window, onWindowPosCallback);
        glfwSetWindowFocusCallback(m_window, onWindowFocusCallback);
        glfwSetWindowIconifyCallback(m_window, onWindowIconifyCallback);
        glfwSetWindowMaximizeCallback(m_window, onWindowMaximizeCallback);
        glfwSetFramebufferSizeCallback(m_window, onFramebufferSizeCallback);
        
        // Input callbacks
        glfwSetKeyCallback(m_window, onKeyCallback);
        glfwSetMouseButtonCallback(m_window, onMouseButtonCallback);
        glfwSetCursorPosCallback(m_window, onCursorPosCallback);
        glfwSetScrollCallback(m_window, onScrollCallback);
    }
    
    void GLFWWindow::updateWindowProperties() {
        if (!m_window) return;
        
        int width, height;
        glfwGetWindowSize(m_window, &width, &height);
        m_properties.width = static_cast<uint32>(width);
        m_properties.height = static_cast<uint32>(height);
        
        m_properties.fullscreen = glfwGetWindowMonitor(m_window) != nullptr;
    }
    
    GLFWWindow* GLFWWindow::getWindowFromGLFW(GLFWwindow* window) {
        return static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
    }
    
    //=============================================================================
    // GLFW Static Callbacks
    //=============================================================================
    
    void GLFWWindow::onWindowCloseCallback(GLFWwindow* window) {
        GLFWWindow* engineWindow = getWindowFromGLFW(window);
        if (engineWindow && engineWindow->onClose) {
            engineWindow->onClose();
        }
    }
    
    void GLFWWindow::onWindowSizeCallback(GLFWwindow* window, int width, int height) {
        GLFWWindow* engineWindow = getWindowFromGLFW(window);
        if (engineWindow) {
            engineWindow->m_properties.width = static_cast<uint32>(width);
            engineWindow->m_properties.height = static_cast<uint32>(height);
            
            if (engineWindow->onResize) {
                engineWindow->onResize(static_cast<uint32>(width), static_cast<uint32>(height));
            }
        }
    }
    
    void GLFWWindow::onWindowPosCallback(GLFWwindow* window, int xpos, int ypos) {
        GLFWWindow* engineWindow = getWindowFromGLFW(window);
        if (engineWindow && engineWindow->onWindowEvent) {
            WindowEvent event;
            event.type = WindowEvent::Moved;
            event.x = xpos;
            event.y = ypos;
            engineWindow->onWindowEvent(event);
        }
    }
    
    void GLFWWindow::onWindowFocusCallback(GLFWwindow* window, int focused) {
        GLFWWindow* engineWindow = getWindowFromGLFW(window);
        if (engineWindow) {
            engineWindow->m_focused = (focused == GLFW_TRUE);
            if (engineWindow->onFocusChange) {
                engineWindow->onFocusChange(engineWindow->m_focused);
            }
        }
    }
    
    void GLFWWindow::onWindowIconifyCallback(GLFWwindow* window, int iconified) {
        GLFWWindow* engineWindow = getWindowFromGLFW(window);
        if (engineWindow) {
            engineWindow->m_minimized = (iconified == GLFW_TRUE);
            if (engineWindow->onWindowEvent) {
                WindowEvent event;
                event.type = iconified ? WindowEvent::Minimized : WindowEvent::Restored;
                engineWindow->onWindowEvent(event);
            }
        }
    }
    
    void GLFWWindow::onWindowMaximizeCallback(GLFWwindow* window, int maximized) {
        GLFWWindow* engineWindow = getWindowFromGLFW(window);
        if (engineWindow) {
            engineWindow->m_maximized = (maximized == GLFW_TRUE);
            if (engineWindow->onWindowEvent) {
                WindowEvent event;
                event.type = maximized ? WindowEvent::Maximized : WindowEvent::Restored;
                engineWindow->onWindowEvent(event);
            }
        }
    }
    
    void GLFWWindow::onFramebufferSizeCallback(GLFWwindow* window, int width, int height) {
        GLFWWindow* engineWindow = getWindowFromGLFW(window);
        if (engineWindow && engineWindow->onResize) {
            engineWindow->onResize(static_cast<uint32>(width), static_cast<uint32>(height));
        }
    }
    
    void GLFWWindow::onKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        GLFWWindow* engineWindow = getWindowFromGLFW(window);
        if (engineWindow && engineWindow->m_inputManager) {
            engineWindow->m_inputManager->onKeyCallback(key, scancode, action, mods);
        }
    }
    
    void GLFWWindow::onMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
        GLFWWindow* engineWindow = getWindowFromGLFW(window);
        if (engineWindow && engineWindow->m_inputManager) {
            engineWindow->m_inputManager->onMouseButtonCallback(button, action, mods);
        }
    }
    
    void GLFWWindow::onCursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
        GLFWWindow* engineWindow = getWindowFromGLFW(window);
        if (engineWindow && engineWindow->m_inputManager) {
            engineWindow->m_inputManager->onMouseMoveCallback(xpos, ypos);
        }
    }
    
    void GLFWWindow::onScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
        GLFWWindow* engineWindow = getWindowFromGLFW(window);
        if (engineWindow && engineWindow->m_inputManager) {
            engineWindow->m_inputManager->onMouseScrollCallback(xoffset, yoffset);
        }
    }
}
