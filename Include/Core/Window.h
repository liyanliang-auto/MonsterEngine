#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Input.h"

namespace MonsterRender {
    
    /**
     * Window properties structure - similar to UE5's window descriptors
     */
    struct WindowProperties {
        String title = "MonsterRender Engine";
        uint32 width = 1920;
        uint32 height = 1080;
        bool fullscreen = false;
        bool resizable = true;
        bool vsync = true;
        bool decorated = true; // Window decorations (title bar, etc)
        
        WindowProperties() = default;
        WindowProperties(const String& inTitle, uint32 inWidth, uint32 inHeight)
            : title(inTitle), width(inWidth), height(inHeight) {}
    };
    
    /**
     * Window events - following UE5's event pattern
     */
    struct WindowEvent {
        enum Type {
            Closed,
            Resized,
            Minimized,
            Maximized,
            Restored,
            FocusGained,
            FocusLost,
            Moved
        } type;
        
        uint32 width = 0;
        uint32 height = 0;
        int32 x = 0;
        int32 y = 0;
    };
    
    /**
     * Abstract window interface - following UE5's GenericWindow pattern
     */
    class IWindow {
    public:
        virtual ~IWindow() = default;
        
        // Window lifecycle
        virtual bool initialize(const WindowProperties& properties) = 0;
        virtual void shutdown() = 0;
        virtual bool shouldClose() const = 0;
        
        // Window operations
        virtual void pollEvents() = 0;
        virtual void swapBuffers() = 0;
        virtual void setTitle(const String& title) = 0;
        virtual void setSize(uint32 width, uint32 height) = 0;
        virtual void setPosition(int32 x, int32 y) = 0;
        virtual void setFullscreen(bool fullscreen) = 0;
        virtual void setVSync(bool enabled) = 0;
        
        // Window properties
        virtual WindowProperties getProperties() const = 0;
        virtual uint32 getWidth() const = 0;
        virtual uint32 getHeight() const = 0;
        virtual String getTitle() const = 0;
        virtual bool isFullscreen() const = 0;
        virtual bool isMinimized() const = 0;
        virtual bool isMaximized() const = 0;
        virtual bool hasFocus() const = 0;
        
        // Platform-specific handle for graphics API integration
        virtual void* getNativeHandle() const = 0;
        virtual void* getNativeDisplayHandle() const = 0;
        
        // Input manager access
        virtual IInputManager* getInputManager() const = 0;
        
        // Event callbacks - similar to UE5's delegate system
        TFunction<void(const WindowEvent&)> onWindowEvent;
        TFunction<void()> onClose;
        TFunction<void(uint32, uint32)> onResize;
        TFunction<void(bool)> onFocusChange;
    };
    
    /**
     * Window factory - following UE5's factory pattern
     */
    class WindowFactory {
    public:
        static TUniquePtr<IWindow> createWindow(const WindowProperties& properties = WindowProperties());
        static void initialize();
        static void shutdown();
        static bool isInitialized();
        
    private:
        static bool s_initialized;
    };
}
