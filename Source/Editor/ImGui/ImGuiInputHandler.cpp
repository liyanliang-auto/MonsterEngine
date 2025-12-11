// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ImGuiInputHandler.cpp
 * @brief Implementation of ImGui input handler
 */

#include "Editor/ImGui/ImGuiInputHandler.h"
#include "Editor/ImGui/ImGuiContext.h"
#include "Core/Logging/LogMacros.h"

// ImGui includes
#include "imgui.h"

namespace MonsterEngine
{
namespace Editor
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogImGuiInput, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

FImGuiInputHandler::FImGuiInputHandler(FImGuiContext* InContext)
    : Context(InContext)
    , MouseX(0.0f)
    , MouseY(0.0f)
{
    MR_LOG(LogImGuiInput, Log, "FImGuiInputHandler created");
}

FImGuiInputHandler::~FImGuiInputHandler()
{
    MR_LOG(LogImGuiInput, Log, "FImGuiInputHandler destroyed");
}

// ============================================================================
// Input Event Handlers
// ============================================================================

void FImGuiInputHandler::OnKeyEvent(EKey Key, EInputAction Action)
{
    if (!Context || !Context->IsInitialized())
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    int32 imguiKey = ConvertKeyToImGui(Key);
    if (imguiKey >= 0)
    {
        io.AddKeyEvent(static_cast<ImGuiKey>(imguiKey), Action == EInputAction::Pressed || Action == EInputAction::Repeat);
    }

    // Handle modifier keys
    switch (Key)
    {
        case EKey::LeftControl:
        case EKey::RightControl:
            io.AddKeyEvent(ImGuiMod_Ctrl, Action == EInputAction::Pressed || Action == EInputAction::Repeat);
            break;
        case EKey::LeftShift:
        case EKey::RightShift:
            io.AddKeyEvent(ImGuiMod_Shift, Action == EInputAction::Pressed || Action == EInputAction::Repeat);
            break;
        case EKey::LeftAlt:
        case EKey::RightAlt:
            io.AddKeyEvent(ImGuiMod_Alt, Action == EInputAction::Pressed || Action == EInputAction::Repeat);
            break;
        case EKey::LeftSuper:
        case EKey::RightSuper:
            io.AddKeyEvent(ImGuiMod_Super, Action == EInputAction::Pressed || Action == EInputAction::Repeat);
            break;
        default:
            break;
    }
}

void FImGuiInputHandler::OnMouseButton(EKey Button, bool bPressed)
{
    if (!Context || !Context->IsInitialized())
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    int32 mouseButton = ConvertMouseButtonToImGui(Button);
    if (mouseButton >= 0 && mouseButton < 5)
    {
        io.AddMouseButtonEvent(mouseButton, bPressed);
    }
}

void FImGuiInputHandler::OnMouseMove(float X, float Y)
{
    if (!Context || !Context->IsInitialized())
    {
        return;
    }

    MouseX = X;
    MouseY = Y;

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(X, Y);
}

void FImGuiInputHandler::OnMouseScroll(float XOffset, float YOffset)
{
    if (!Context || !Context->IsInitialized())
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent(XOffset, YOffset);
}

void FImGuiInputHandler::OnCharInput(uint32 Char)
{
    if (!Context || !Context->IsInitialized())
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharacter(Char);
}

void FImGuiInputHandler::UpdateModifiers(bool bCtrl, bool bShift, bool bAlt, bool bSuper)
{
    if (!Context || !Context->IsInitialized())
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, bCtrl);
    io.AddKeyEvent(ImGuiMod_Shift, bShift);
    io.AddKeyEvent(ImGuiMod_Alt, bAlt);
    io.AddKeyEvent(ImGuiMod_Super, bSuper);
}

// ============================================================================
// Key Conversion
// ============================================================================

int32 FImGuiInputHandler::ConvertKeyToImGui(EKey Key)
{
    // Map engine keys to ImGui keys
    // Only map keys that are defined in Input.h
    switch (Key)
    {
        // Letters (A-Z are sequential starting at 65)
        case EKey::A: return ImGuiKey_A;
        case EKey::B: return ImGuiKey_B;
        case EKey::C: return ImGuiKey_C;
        case EKey::D: return ImGuiKey_D;
        case EKey::E: return ImGuiKey_E;
        case EKey::F: return ImGuiKey_F;
        case EKey::G: return ImGuiKey_G;
        case EKey::H: return ImGuiKey_H;
        case EKey::I: return ImGuiKey_I;
        case EKey::J: return ImGuiKey_J;
        case EKey::K: return ImGuiKey_K;
        case EKey::L: return ImGuiKey_L;
        case EKey::M: return ImGuiKey_M;
        case EKey::N: return ImGuiKey_N;
        case EKey::O: return ImGuiKey_O;
        case EKey::P: return ImGuiKey_P;
        case EKey::Q: return ImGuiKey_Q;
        case EKey::R: return ImGuiKey_R;
        case EKey::S: return ImGuiKey_S;
        case EKey::T: return ImGuiKey_T;
        case EKey::U: return ImGuiKey_U;
        case EKey::V: return ImGuiKey_V;
        case EKey::W: return ImGuiKey_W;
        case EKey::X: return ImGuiKey_X;
        case EKey::Y: return ImGuiKey_Y;
        case EKey::Z: return ImGuiKey_Z;

        // Numbers (Zero-Nine are sequential starting at 48)
        case EKey::Zero: return ImGuiKey_0;
        case EKey::One: return ImGuiKey_1;
        case EKey::Two: return ImGuiKey_2;
        case EKey::Three: return ImGuiKey_3;
        case EKey::Four: return ImGuiKey_4;
        case EKey::Five: return ImGuiKey_5;
        case EKey::Six: return ImGuiKey_6;
        case EKey::Seven: return ImGuiKey_7;
        case EKey::Eight: return ImGuiKey_8;
        case EKey::Nine: return ImGuiKey_9;

        // Function keys
        case EKey::F1: return ImGuiKey_F1;
        case EKey::F2: return ImGuiKey_F2;
        case EKey::F3: return ImGuiKey_F3;
        case EKey::F4: return ImGuiKey_F4;
        case EKey::F5: return ImGuiKey_F5;
        case EKey::F6: return ImGuiKey_F6;
        case EKey::F7: return ImGuiKey_F7;
        case EKey::F8: return ImGuiKey_F8;
        case EKey::F9: return ImGuiKey_F9;
        case EKey::F10: return ImGuiKey_F10;
        case EKey::F11: return ImGuiKey_F11;
        case EKey::F12: return ImGuiKey_F12;

        // Navigation / Arrow keys
        case EKey::Up: return ImGuiKey_UpArrow;
        case EKey::Down: return ImGuiKey_DownArrow;
        case EKey::Left: return ImGuiKey_LeftArrow;
        case EKey::Right: return ImGuiKey_RightArrow;
        case EKey::Home: return ImGuiKey_Home;
        case EKey::End: return ImGuiKey_End;
        case EKey::PageUp: return ImGuiKey_PageUp;
        case EKey::PageDown: return ImGuiKey_PageDown;
        case EKey::Insert: return ImGuiKey_Insert;
        case EKey::Delete: return ImGuiKey_Delete;

        // Editing keys
        case EKey::Backspace: return ImGuiKey_Backspace;
        case EKey::Tab: return ImGuiKey_Tab;
        case EKey::Enter: return ImGuiKey_Enter;
        case EKey::Escape: return ImGuiKey_Escape;
        case EKey::Space: return ImGuiKey_Space;

        // Modifier keys
        case EKey::LeftShift: return ImGuiKey_LeftShift;
        case EKey::RightShift: return ImGuiKey_RightShift;
        case EKey::LeftControl: return ImGuiKey_LeftCtrl;
        case EKey::RightControl: return ImGuiKey_RightCtrl;
        case EKey::LeftAlt: return ImGuiKey_LeftAlt;
        case EKey::RightAlt: return ImGuiKey_RightAlt;
        case EKey::LeftSuper: return ImGuiKey_LeftSuper;
        case EKey::RightSuper: return ImGuiKey_RightSuper;

        // Other special keys
        case EKey::CapsLock: return ImGuiKey_CapsLock;
        case EKey::ScrollLock: return ImGuiKey_ScrollLock;
        case EKey::NumLock: return ImGuiKey_NumLock;
        case EKey::PrintScreen: return ImGuiKey_PrintScreen;
        case EKey::Pause: return ImGuiKey_Pause;

        default:
            return -1;
    }
}

int32 FImGuiInputHandler::ConvertMouseButtonToImGui(EKey Button)
{
    switch (Button)
    {
        case EKey::MouseLeft: return 0;
        case EKey::MouseRight: return 1;
        case EKey::MouseMiddle: return 2;
        case EKey::MouseButton4: return 3;
        case EKey::MouseButton5: return 4;
        default: return -1;
    }
}

} // namespace Editor
} // namespace MonsterEngine
