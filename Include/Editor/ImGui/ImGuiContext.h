// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ImGuiContext.h
 * @brief ImGui context management for the editor
 * 
 * Manages ImGui initialization, configuration, and frame lifecycle.
 * Supports docking and custom styling.
 * 
 * Reference: UE5 ImGui plugin, Dear ImGui docking branch
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"

// Forward declarations for ImGui types
struct ImGuiContext;
struct ImGuiIO;
struct ImFont;

namespace MonsterEngine
{
namespace Editor
{

/**
 * @class FImGuiContext
 * @brief Manages ImGui context and configuration
 * 
 * This class handles:
 * - ImGui initialization and shutdown
 * - Docking space setup
 * - Custom editor styling
 * - Frame begin/end lifecycle
 * - Font loading and management
 */
class FImGuiContext
{
public:
    FImGuiContext();
    ~FImGuiContext();

    // Non-copyable
    FImGuiContext(const FImGuiContext&) = delete;
    FImGuiContext& operator=(const FImGuiContext&) = delete;

    /**
     * Initialize ImGui context
     * @return True if initialization successful
     */
    bool Initialize();

    /**
     * Shutdown ImGui context and release resources
     */
    void Shutdown();

    /**
     * Begin a new ImGui frame
     * Must be called before any ImGui rendering
     * @param DeltaTime Time since last frame in seconds
     * @param WindowWidth Current window width in pixels
     * @param WindowHeight Current window height in pixels
     */
    void BeginFrame(float DeltaTime, uint32 WindowWidth, uint32 WindowHeight);

    /**
     * End the current ImGui frame
     * Must be called after all ImGui rendering
     */
    void EndFrame();

    /**
     * Setup the main docking space
     * Creates a full-window docking space for editor panels
     */
    void SetupDockSpace();

    /**
     * Apply custom editor style
     * Sets colors, sizes, and other visual properties
     */
    void ApplyEditorStyle();

    /**
     * Get ImGui IO reference for configuration
     * @return Reference to ImGuiIO
     */
    ImGuiIO& GetIO();

    /**
     * Check if ImGui wants to capture keyboard input
     * @return True if ImGui is handling keyboard
     */
    bool WantsCaptureKeyboard() const;

    /**
     * Check if ImGui wants to capture mouse input
     * @return True if ImGui is handling mouse
     */
    bool WantsCaptureMouse() const;

    /**
     * Check if context is initialized
     * @return True if initialized
     */
    bool IsInitialized() const { return bInitialized; }

    /**
     * Get the default font
     * @return Pointer to default font, or nullptr if not loaded
     */
    ImFont* GetDefaultFont() const { return DefaultFont; }

    /**
     * Get the icon font
     * @return Pointer to icon font, or nullptr if not loaded
     */
    ImFont* GetIconFont() const { return IconFont; }

private:
    /**
     * Load custom fonts for the editor
     * Loads default font and icon font
     */
    void LoadFonts();

    /**
     * Setup default docking layout
     * Creates initial panel arrangement
     */
    void SetupDefaultLayout();

    /**
     * Configure ImGui settings
     * Sets up IO flags, key mappings, etc.
     */
    void ConfigureImGui();

private:
    /** ImGui context handle */
    ImGuiContext* Context;

    /** Default editor font */
    ImFont* DefaultFont;

    /** Icon font for toolbar/buttons */
    ImFont* IconFont;

    /** Initialization state */
    bool bInitialized;

    /** First frame flag for layout setup */
    bool bFirstFrame;

    /** Docking space ID */
    uint32 DockSpaceId;
};

} // namespace Editor
} // namespace MonsterEngine
