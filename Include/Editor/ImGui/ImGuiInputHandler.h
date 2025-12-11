// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ImGuiInputHandler.h
 * @brief Input event handler for ImGui integration
 * 
 * Converts engine input events to ImGui input format.
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Core/Input.h"

namespace MonsterEngine
{
namespace Editor
{

// Bring input types into scope
using ::MonsterRender::EKey;
using ::MonsterRender::EInputAction;

// Forward declaration
class FImGuiContext;

/**
 * @class FImGuiInputHandler
 * @brief Handles input events and forwards them to ImGui
 * 
 * This class converts engine input events (keyboard, mouse)
 * to ImGui's input format and updates ImGui's IO state.
 */
class FImGuiInputHandler
{
public:
    /**
     * Constructor
     * @param InContext ImGui context to forward input to
     */
    explicit FImGuiInputHandler(FImGuiContext* InContext);

    ~FImGuiInputHandler();

    // Non-copyable
    FImGuiInputHandler(const FImGuiInputHandler&) = delete;
    FImGuiInputHandler& operator=(const FImGuiInputHandler&) = delete;

    /**
     * Process key event
     * @param Key The key that was pressed/released
     * @param Action Press, release, or repeat
     */
    void OnKeyEvent(EKey Key, EInputAction Action);

    /**
     * Process mouse button event
     * @param Button The mouse button
     * @param bPressed True if pressed, false if released
     */
    void OnMouseButton(EKey Button, bool bPressed);

    /**
     * Process mouse move event
     * @param X Mouse X position
     * @param Y Mouse Y position
     */
    void OnMouseMove(float X, float Y);

    /**
     * Process mouse scroll event
     * @param XOffset Horizontal scroll offset
     * @param YOffset Vertical scroll offset
     */
    void OnMouseScroll(float XOffset, float YOffset);

    /**
     * Process character input event
     * @param Char Unicode character code
     */
    void OnCharInput(uint32 Char);

    /**
     * Update modifier key states
     * @param bCtrl Control key state
     * @param bShift Shift key state
     * @param bAlt Alt key state
     * @param bSuper Super/Windows key state
     */
    void UpdateModifiers(bool bCtrl, bool bShift, bool bAlt, bool bSuper);

private:
    /**
     * Convert engine key code to ImGui key code
     * @param Key Engine key code
     * @return ImGui key code, or -1 if not mapped
     */
    int32 ConvertKeyToImGui(EKey Key);

    /**
     * Convert engine mouse button to ImGui mouse button index
     * @param Button Engine mouse button
     * @return ImGui mouse button index (0-4), or -1 if not mapped
     */
    int32 ConvertMouseButtonToImGui(EKey Button);

private:
    /** ImGui context reference */
    FImGuiContext* Context;

    /** Current mouse position */
    float MouseX;
    float MouseY;
};

} // namespace Editor
} // namespace MonsterEngine
