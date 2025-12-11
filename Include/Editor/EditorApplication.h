// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file EditorApplication.h
 * @brief Main editor application class
 * 
 * Integrates ImGui with the engine for editor UI rendering.
 * Provides the foundation for the visual editor framework.
 * 
 * Reference: UE5 Editor architecture, Dear ImGui
 */

#include "Core/Application.h"
#include "Core/CoreMinimal.h"
#include "Core/Templates/UniquePtr.h"

namespace MonsterEngine
{
namespace Editor
{

// Forward declarations
class FImGuiContext;
class FImGuiRenderer;
class FImGuiInputHandler;

/**
 * @class FEditorApplication
 * @brief Main editor application integrating ImGui with the engine
 * 
 * This class extends the base Application to add ImGui-based editor UI.
 * It handles:
 * - ImGui initialization and shutdown
 * - ImGui frame rendering
 * - Input event forwarding to ImGui
 * - Docking space setup
 * - Demo window for testing
 */
class FEditorApplication : public MonsterRender::Application
{
public:
    /**
     * Constructor
     * @param Config Application configuration
     */
    FEditorApplication(const MonsterRender::ApplicationConfig& Config = MonsterRender::ApplicationConfig());

    /**
     * Destructor
     */
    virtual ~FEditorApplication();

protected:
    // ========================================================================
    // Application lifecycle overrides
    // ========================================================================

    /**
     * Called after base initialization
     * Initializes ImGui context and renderer
     */
    virtual void onInitialize() override;

    /**
     * Called before base shutdown
     * Shuts down ImGui renderer and context
     */
    virtual void onShutdown() override;

    /**
     * Called each frame for updates
     * @param deltaTime Time since last frame
     */
    virtual void onUpdate(float32 deltaTime) override;

    /**
     * Called each frame for rendering
     * Renders ImGui UI
     */
    virtual void onRender() override;

    /**
     * Called when window is resized
     * @param width New window width
     * @param height New window height
     */
    virtual void onWindowResize(uint32 width, uint32 height) override;

    // ========================================================================
    // Input event overrides - forward to ImGui
    // ========================================================================

    virtual void onKeyPressed(MonsterRender::EKey key) override;
    virtual void onKeyReleased(MonsterRender::EKey key) override;
    virtual void onMouseButtonPressed(MonsterRender::EKey button, const MonsterRender::MousePosition& position) override;
    virtual void onMouseButtonReleased(MonsterRender::EKey button, const MonsterRender::MousePosition& position) override;
    virtual void onMouseMoved(const MonsterRender::MousePosition& position) override;
    virtual void onMouseScrolled(float64 xOffset, float64 yOffset) override;

protected:
    // ========================================================================
    // ImGui rendering methods
    // ========================================================================

    /**
     * Initialize ImGui subsystem
     * @return True if successful
     */
    bool InitializeImGui();

    /**
     * Shutdown ImGui subsystem
     */
    void ShutdownImGui();

    /**
     * Render ImGui frame
     */
    void RenderImGui();

    /**
     * Render the main menu bar
     */
    void RenderMainMenuBar();

    /**
     * Render editor panels
     */
    void RenderEditorPanels();

    /**
     * Render the demo window (for testing)
     */
    void RenderDemoWindow();

protected:
    // ========================================================================
    // ImGui components
    // ========================================================================

    /** ImGui context manager */
    TUniquePtr<FImGuiContext> ImGuiContext;

    /** ImGui RHI renderer */
    TUniquePtr<FImGuiRenderer> ImGuiRenderer;

    /** ImGui input handler */
    TUniquePtr<FImGuiInputHandler> ImGuiInputHandler;

    // ========================================================================
    // Editor state
    // ========================================================================

    /** Show ImGui demo window */
    bool bShowDemoWindow;

    /** Show metrics window */
    bool bShowMetricsWindow;

    /** Show about window */
    bool bShowAboutWindow;

    /** ImGui initialization state */
    bool bImGuiInitialized;
};

} // namespace Editor
} // namespace MonsterEngine
