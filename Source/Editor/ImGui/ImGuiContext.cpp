// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ImGuiContext.cpp
 * @brief Implementation of ImGui context management
 */

#include "Editor/ImGui/ImGuiContext.h"
#include "Core/Logging/LogMacros.h"

// ImGui includes
#include "imgui.h"

namespace MonsterEngine
{
namespace Editor
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogImGui, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

FImGuiContext::FImGuiContext()
    : Context(nullptr)
    , DefaultFont(nullptr)
    , IconFont(nullptr)
    , bInitialized(false)
    , bFirstFrame(true)
    , DockSpaceId(0)
{
    MR_LOG(LogImGui, Log, "FImGuiContext created");
}

FImGuiContext::~FImGuiContext()
{
    Shutdown();
    MR_LOG(LogImGui, Log, "FImGuiContext destroyed");
}

// ============================================================================
// Initialization / Shutdown
// ============================================================================

bool FImGuiContext::Initialize()
{
    if (bInitialized)
    {
        MR_LOG(LogImGui, Warning, "ImGui context already initialized");
        return true;
    }

    MR_LOG(LogImGui, Log, "Initializing ImGui context...");

    // Check ImGui version
    IMGUI_CHECKVERSION();

    // Create ImGui context
    Context = ImGui::CreateContext();
    if (!Context)
    {
        MR_LOG(LogImGui, Error, "Failed to create ImGui context");
        return false;
    }

    // Configure ImGui
    ConfigureImGui();

    // Load fonts
    LoadFonts();

    // Apply editor style
    ApplyEditorStyle();

    bInitialized = true;
    bFirstFrame = true;

    MR_LOG(LogImGui, Log, "ImGui context initialized successfully");
    MR_LOG(LogImGui, Log, "  ImGui version: %s", IMGUI_VERSION);

    return true;
}

void FImGuiContext::Shutdown()
{
    if (!bInitialized)
    {
        return;
    }

    MR_LOG(LogImGui, Log, "Shutting down ImGui context...");

    if (Context)
    {
        ImGui::DestroyContext(Context);
        Context = nullptr;
    }

    DefaultFont = nullptr;
    IconFont = nullptr;
    bInitialized = false;

    MR_LOG(LogImGui, Log, "ImGui context shutdown complete");
}

// ============================================================================
// Frame Lifecycle
// ============================================================================

void FImGuiContext::BeginFrame(float DeltaTime, uint32 WindowWidth, uint32 WindowHeight)
{
    if (!bInitialized)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Update display size
    io.DisplaySize = ImVec2(static_cast<float>(WindowWidth), static_cast<float>(WindowHeight));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    // Update delta time
    io.DeltaTime = DeltaTime > 0.0f ? DeltaTime : (1.0f / 60.0f);

    // Start new frame
    ImGui::NewFrame();

    // Setup default layout on first frame
    if (bFirstFrame)
    {
        SetupDefaultLayout();
        bFirstFrame = false;
    }
}

void FImGuiContext::EndFrame()
{
    if (!bInitialized)
    {
        return;
    }

    // Render ImGui
    ImGui::Render();
}

// ============================================================================
// Docking
// ============================================================================

void FImGuiContext::SetupDockSpace()
{
    if (!bInitialized)
    {
        return;
    }

    // Get the main viewport
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Set next window to cover the entire viewport
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // Window flags for dockspace host window
    ImGuiWindowFlags windowFlags = 
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    // Remove padding for dockspace
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    // Begin dockspace host window
    ImGui::Begin("DockSpaceWindow", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    // Create dockspace
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        DockSpaceId = ImGui::GetID("EditorDockSpace");
        ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    }

    ImGui::End();
}

// ============================================================================
// Styling
// ============================================================================

void FImGuiContext::ApplyEditorStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounding
    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 4.0f;

    // Padding and spacing
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(4.0f, 3.0f);
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;

    // Borders
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    // Colors - Dark theme inspired by UE5
    ImVec4* colors = style.Colors;

    // Background colors
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.12f, 0.94f);

    // Border colors
    colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.30f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame colors
    colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);

    // Title bar colors
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.08f, 0.08f, 0.75f);

    // Menu bar
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    // Check mark
    colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

    // Slider
    colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

    // Button
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

    // Header (collapsing headers, tree nodes, selectable)
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

    // Separator
    colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.30f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

    // Resize grip
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

    // Tab
    colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);

    // Docking
    colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

    // Plot
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);

    // Table
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);

    // Text
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);

    // Drag drop
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.26f, 0.59f, 0.98f, 0.90f);

    // Nav
    colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);

    // Modal
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);

    MR_LOG(LogImGui, Log, "Applied editor style");
}

// ============================================================================
// IO Access
// ============================================================================

ImGuiIO& FImGuiContext::GetIO()
{
    return ImGui::GetIO();
}

bool FImGuiContext::WantsCaptureKeyboard() const
{
    if (!bInitialized)
    {
        return false;
    }
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool FImGuiContext::WantsCaptureMouse() const
{
    if (!bInitialized)
    {
        return false;
    }
    return ImGui::GetIO().WantCaptureMouse;
}

// ============================================================================
// Private Methods
// ============================================================================

void FImGuiContext::LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();

    // Add default font with larger size for better readability
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    fontConfig.PixelSnapH = true;

    // Try to load custom font, fall back to default
    DefaultFont = io.Fonts->AddFontDefault(&fontConfig);

    // Build font atlas
    io.Fonts->Build();

    MR_LOG(LogImGui, Log, "Fonts loaded");
}

void FImGuiContext::SetupDefaultLayout()
{
    // Default layout will be set up when docking is first used
    // This is called on the first frame after initialization
    MR_LOG(LogImGui, Log, "Default layout initialized");
}

void FImGuiContext::ConfigureImGui()
{
    ImGuiIO& io = ImGui::GetIO();

    // Enable docking
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Enable keyboard navigation
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Disable saving ini file for now (we'll implement custom save/load)
    io.IniFilename = nullptr;

    // Configure docking
    io.ConfigDockingWithShift = false;  // Dock without holding shift
    io.ConfigDockingAlwaysTabBar = true;  // Always show tab bar

    // Configure windows
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    MR_LOG(LogImGui, Log, "ImGui configured with docking enabled");
}

} // namespace Editor
} // namespace MonsterEngine
