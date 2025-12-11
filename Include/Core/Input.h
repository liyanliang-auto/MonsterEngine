#pragma once

#include "Core/CoreMinimal.h"

namespace MonsterRender {
    
    /**
     * Key codes following UE5 style
     */
    enum class EKey : uint32 {
        Unknown = 0,
        
        // Letter keys
        A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        
        // Number keys
        Zero = 48, One, Two, Three, Four, Five, Six, Seven, Eight, Nine,
        
        // Function keys
        F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        
        // Arrow keys (GLFW key codes)
        Right = 262,
        Left = 263,
        Down = 264,
        Up = 265,
        
        // Special keys
        Escape = 256,
        Enter = 257,
        Tab = 258,
        Backspace = 259,
        Insert = 260,
        Delete = 261,
        Space = 32,
        PageUp = 266,
        PageDown = 267,
        Home = 268,
        End = 269,
        CapsLock = 280,
        ScrollLock = 281,
        NumLock = 282,
        PrintScreen = 283,
        Pause = 284,
        
        // Modifier keys
        LeftShift = 340,
        LeftControl = 341,
        LeftAlt = 342,
        LeftSuper = 343,
        RightShift = 344,
        RightControl = 345,
        RightAlt = 346,
        RightSuper = 347,
        
        // Mouse buttons
        MouseLeft = 1000,
        MouseRight = 1001,
        MouseMiddle = 1002,
        MouseButton4 = 1003,
        MouseButton5 = 1004
    };
    
    /**
     * Input action types
     */
    enum class EInputAction : uint8 {
        Released = 0,
        Pressed = 1,
        Repeat = 2
    };
    
    /**
     * Mouse position structure
     */
    struct MousePosition {
        float64 x = 0.0;
        float64 y = 0.0;
        
        MousePosition() = default;
        MousePosition(float64 inX, float64 inY) : x(inX), y(inY) {}
    };
    
    /**
     * Input event structure
     */
    struct InputEvent {
        EKey key = EKey::Unknown;
        EInputAction action = EInputAction::Released;
        int32 mods = 0; // Modifier flags
        
        InputEvent() = default;
        InputEvent(EKey inKey, EInputAction inAction, int32 inMods = 0)
            : key(inKey), action(inAction), mods(inMods) {}
    };
    
    /**
     * Input manager interface - similar to UE5's input system
     */
    class IInputManager {
    public:
        virtual ~IInputManager() = default;
        
        // Key state queries
        virtual bool isKeyPressed(EKey key) const = 0;
        virtual bool isKeyReleased(EKey key) const = 0;
        virtual bool isKeyDown(EKey key) const = 0;
        
        // Mouse state queries
        virtual bool isMouseButtonPressed(EKey button) const = 0;
        virtual bool isMouseButtonReleased(EKey button) const = 0;
        virtual bool isMouseButtonDown(EKey button) const = 0;
        virtual MousePosition getMousePosition() const = 0;
        virtual MousePosition getMouseDelta() const = 0;
        
        // Event processing
        virtual void processEvents() = 0;
        virtual void resetFrameState() = 0;
        
        // Event callbacks - similar to UE5's delegate system
        TFunction<void(const InputEvent&)> onKeyEvent;
        TFunction<void(EKey, const MousePosition&)> onMouseButtonEvent;
        TFunction<void(const MousePosition&)> onMouseMoveEvent;
        TFunction<void(float64, float64)> onMouseScrollEvent;
    };
}
