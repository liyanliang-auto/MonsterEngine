#pragma once

#include "Core/Window.h"
#include "Core/Input.h"

#include <vector>

// Forward declare GLFW types to avoid including GLFW headers in public headers
struct GLFWwindow;

namespace MonsterRender::Platform::GLFW {
    
    /**
     * GLFW-specific input manager - following UE5's platform input pattern
     */
    class GLFWInputManager : public IInputManager {
    public:
        explicit GLFWInputManager(GLFWwindow* window);
        virtual ~GLFWInputManager() = default;
        
        // IInputManager interface
        bool isKeyPressed(EKey key) const override;
        bool isKeyReleased(EKey key) const override;
        bool isKeyDown(EKey key) const override;
        
        bool isMouseButtonPressed(EKey button) const override;
        bool isMouseButtonReleased(EKey button) const override;
        bool isMouseButtonDown(EKey button) const override;
        MousePosition getMousePosition() const override;
        MousePosition getMouseDelta() const override;
        
        void processEvents() override;
        void resetFrameState() override;
        
        // Internal event callbacks from GLFW
        void onKeyCallback(int key, int scancode, int action, int mods);
        void onMouseButtonCallback(int button, int action, int mods);
        void onMouseMoveCallback(double xpos, double ypos);
        void onMouseScrollCallback(double xoffset, double yoffset);
        
    private:
        GLFWwindow* m_window = nullptr;
        
        // Current frame input state - use std::vector to avoid TArray memory issues
        std::vector<bool> m_keyStates;
        std::vector<bool> m_keyPressed;
        std::vector<bool> m_keyReleased;
        
        std::vector<bool> m_mouseButtonStates;
        std::vector<bool> m_mouseButtonPressed;
        std::vector<bool> m_mouseButtonReleased;
        
        MousePosition m_mousePosition;
        MousePosition m_lastMousePosition;
        MousePosition m_mouseDelta;
        
        // Helper methods
        int32 convertKeyToGLFW(EKey key) const;
        EKey convertGLFWToKey(int32 glfwKey) const;
        int32 convertMouseButtonToGLFW(EKey button) const;
        EKey convertGLFWToMouseButton(int32 glfwButton) const;
        
        void initializeKeyStates();
    };
    
    /**
     * GLFW window implementation - following UE5's FGenericPlatformWindow pattern
     */
    class GLFWWindow : public IWindow {
    public:
        GLFWWindow();
        virtual ~GLFWWindow();
        
        // Non-copyable, non-movable
        GLFWWindow(const GLFWWindow&) = delete;
        GLFWWindow& operator=(const GLFWWindow&) = delete;
        GLFWWindow(GLFWWindow&&) = delete;
        GLFWWindow& operator=(GLFWWindow&&) = delete;
        
        // IWindow interface
        bool initialize(const WindowProperties& properties) override;
        void shutdown() override;
        bool shouldClose() const override;
        
        void pollEvents() override;
        void swapBuffers() override;
        void setTitle(const String& title) override;
        void setSize(uint32 width, uint32 height) override;
        void setPosition(int32 x, int32 y) override;
        void setFullscreen(bool fullscreen) override;
        void setVSync(bool enabled) override;
        
        WindowProperties getProperties() const override;
        uint32 getWidth() const override;
        uint32 getHeight() const override;
        String getTitle() const override;
        bool isFullscreen() const override;
        bool isMinimized() const override;
        bool isMaximized() const override;
        bool hasFocus() const override;
        
        void* getNativeHandle() const override;
        void* getNativeDisplayHandle() const override;
        
        IInputManager* getInputManager() const override;
        
        // GLFW-specific methods
        GLFWwindow* getGLFWWindow() const { return m_window; }
        
        // Static GLFW callbacks - following UE5's callback pattern
        static void onWindowCloseCallback(GLFWwindow* window);
        static void onWindowSizeCallback(GLFWwindow* window, int width, int height);
        static void onWindowPosCallback(GLFWwindow* window, int xpos, int ypos);
        static void onWindowFocusCallback(GLFWwindow* window, int focused);
        static void onWindowIconifyCallback(GLFWwindow* window, int iconified);
        static void onWindowMaximizeCallback(GLFWwindow* window, int maximized);
        static void onFramebufferSizeCallback(GLFWwindow* window, int width, int height);
        
        // Input callbacks
        static void onKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void onMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void onCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
        static void onScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
        
    private:
        GLFWwindow* m_window = nullptr;
        WindowProperties m_properties;
        TUniquePtr<GLFWInputManager> m_inputManager;
        
        // Window state
        bool m_initialized = false;
        bool m_minimized = false;
        bool m_maximized = false;
        bool m_focused = true;
        
        // Helper methods
        void setupCallbacks();
        void updateWindowProperties();
        static GLFWWindow* getWindowFromGLFW(GLFWwindow* window);
    };
}
