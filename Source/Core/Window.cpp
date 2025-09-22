#include "Core/Window.h"
#include "Platform/GLFW/GLFWWindow.h"
#include "Core/Log.h"

namespace MonsterRender {
    
    // Static member definition
    bool WindowFactory::s_initialized = false;
    
    TUniquePtr<IWindow> WindowFactory::createWindow(const WindowProperties& properties) {
        if (!s_initialized) {
            MR_LOG_ERROR("WindowFactory not initialized. Call WindowFactory::initialize() first.");
            return nullptr;
        }
        
        // For now, we only support GLFW windows
        // In the future, we could add platform-specific window implementations
        auto window = MakeUnique<Platform::GLFW::GLFWWindow>();
        
        if (!window->initialize(properties)) {
            MR_LOG_ERROR("Failed to initialize GLFW window");
            return nullptr;
        }
        
        return std::move(window);
    }
    
    void WindowFactory::initialize() {
        if (s_initialized) {
            MR_LOG_WARNING("WindowFactory already initialized");
            return;
        }
        
        MR_LOG_INFO("Initializing WindowFactory...");
        s_initialized = true;
        MR_LOG_INFO("WindowFactory initialized successfully");
    }
    
    void WindowFactory::shutdown() {
        if (!s_initialized) {
            return;
        }
        
        MR_LOG_INFO("Shutting down WindowFactory...");
        s_initialized = false;
        MR_LOG_INFO("WindowFactory shut down successfully");
    }
    
    bool WindowFactory::isInitialized() {
        return s_initialized;
    }
}
