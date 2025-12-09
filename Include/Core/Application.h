#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Window.h"
#include "Engine.h"
#include "RHI/IRHISwapChain.h"  // For ERHIBackend

namespace MonsterRender {
    
    /**
     * Application configuration - similar to UE5's application settings
     */
    struct ApplicationConfig {
        String name = "MonsterRender Application";
        String version = "1.0.0";
        
        // Window settings
        WindowProperties windowProperties;
        
        // Rendering settings
        bool enableVSync = true;
        bool enableValidation = true;
        bool enableDebugMarkers = true;
        
        // RHI backend selection (None = auto-select best available)
        RHI::ERHIBackend preferredBackend = RHI::ERHIBackend::None;
        
        ApplicationConfig() {
            windowProperties.title = name;
            windowProperties.width = 1920;
            windowProperties.height = 1080;
            windowProperties.resizable = true;
            windowProperties.vsync = enableVSync;
        }
    };
    
    /**
     * Base application class - following UE5's application architecture
     */
    class Application {
    public:
        Application(const ApplicationConfig& config = ApplicationConfig());
        virtual ~Application();
        
        // Non-copyable, non-movable
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;
        
        // Application lifecycle
        virtual bool initialize();
        virtual void shutdown();
        virtual int32 run();
        
        // Application events - override these in derived classes
        virtual void onInitialize() {}
        virtual void onShutdown() {}
        virtual void onUpdate(float32 deltaTime) {}
        virtual void onRender() {}
        virtual void onWindowResize(uint32 width, uint32 height) {}
        virtual void onWindowClose() {}
        
        // Input events
        virtual void onKeyPressed(EKey key) {}
        virtual void onKeyReleased(EKey key) {}
        virtual void onMouseButtonPressed(EKey button, const MousePosition& position) {}
        virtual void onMouseButtonReleased(EKey button, const MousePosition& position) {}
        virtual void onMouseMoved(const MousePosition& position) {}
        virtual void onMouseScrolled(float64 xOffset, float64 yOffset) {}
        
        // Accessors
        IWindow* getWindow() const { return m_window.get(); }
        Engine* getEngine() const { return m_engine.get(); }
        const ApplicationConfig& getConfig() const { return m_config; }
        
        // Application control
        void requestExit() { m_shouldExit = true; }
        bool shouldExit() const { return m_shouldExit; }
        
    protected:
        // Internal methods
        virtual void tick();
        virtual void calculateDeltaTime();
        
        // Event handlers
        void onWindowCloseInternal();
        void onWindowResizeInternal(uint32 width, uint32 height);
        void onKeyEventInternal(const InputEvent& event);
        void onMouseButtonEventInternal(EKey button, const MousePosition& position);
        void onMouseMoveEventInternal(const MousePosition& position);
        void onMouseScrollEventInternal(float64 xOffset, float64 yOffset);
        
    protected:
        ApplicationConfig m_config;
        TUniquePtr<IWindow> m_window;
        TUniquePtr<Engine> m_engine;
        
        // Application state
        bool m_initialized = false;
        bool m_shouldExit = false;
        bool m_minimized = false;
        
        // Timing
        float32 m_deltaTime = 0.0f;
        float32 m_lastFrameTime = 0.0f;
        
        // Statistics
        uint64 m_frameCount = 0;
        float32 m_fpsTimer = 0.0f;
        uint32 m_fps = 0;
    };
    
    /**
     * Application creation function - must be implemented by client
     * Similar to UE5's CreateApplication pattern
     */
    TUniquePtr<Application> createApplication();
}
