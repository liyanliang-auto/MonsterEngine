// Copyright Monster Engine. All Rights Reserved.

/**
 * @file EditorApplication.cpp
 * @brief Implementation of the main editor application
 */

#include "Editor/EditorApplication.h"
#include "Editor/ImGui/ImGuiContext.h"
#include "Editor/ImGui/ImGuiRenderer.h"
#include "Editor/ImGui/ImGuiInputHandler.h"
#include "Core/Logging/LogMacros.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"

// ImGui includes
#include "imgui.h"

namespace MonsterEngine
{
namespace Editor
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogEditor, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

FEditorApplication::FEditorApplication(const MonsterRender::ApplicationConfig& Config)
    : Application(Config)
    , bShowDemoWindow(true)
    , bShowMetricsWindow(false)
    , bShowAboutWindow(false)
    , bImGuiInitialized(false)
{
    MR_LOG(LogEditor, Log, "FEditorApplication created");
}

FEditorApplication::~FEditorApplication()
{
    MR_LOG(LogEditor, Log, "FEditorApplication destroyed");
}

// ============================================================================
// Application Lifecycle
// ============================================================================

void FEditorApplication::onInitialize()
{
    MR_LOG(LogEditor, Log, "Initializing Editor Application...");

    // Initialize ImGui
    if (!InitializeImGui())
    {
        MR_LOG(LogEditor, Error, "Failed to initialize ImGui");
        return;
    }

    MR_LOG(LogEditor, Log, "Editor Application initialized successfully");
}

void FEditorApplication::onShutdown()
{
    MR_LOG(LogEditor, Log, "Shutting down Editor Application...");

    // Shutdown ImGui
    ShutdownImGui();

    MR_LOG(LogEditor, Log, "Editor Application shutdown complete");
}

void FEditorApplication::onUpdate(float32 deltaTime)
{
    // Update editor state here
    // For now, nothing to update
}

void FEditorApplication::onRender()
{
    if (!bImGuiInitialized)
    {
        return;
    }

    // Get RHI device
    MonsterRender::RHI::IRHIDevice* device = nullptr;
    if (m_engine)
    {
        device = m_engine->getRHIDevice();
    }

    if (!device)
    {
        return;
    }

    MonsterRender::RHI::IRHICommandList* cmdList = device->getImmediateCommandList();
    if (!cmdList)
    {
        return;
    }

    // Begin command recording
    cmdList->begin();

    // Set empty render targets (will use default swapchain)
    TArray<TSharedPtr<MonsterRender::RHI::IRHITexture>> renderTargets;
    cmdList->setRenderTargets(renderTargets, nullptr);

    // Render ImGui
    RenderImGui();

    // End render pass
    cmdList->endRenderPass();

    // End command recording
    cmdList->end();

    // Present using window swap buffers
    if (m_window)
    {
        m_window->swapBuffers();
    }
}

void FEditorApplication::onWindowResize(uint32 width, uint32 height)
{
    MR_LOG(LogEditor, Log, "Window resized to %ux%u", width, height);

    if (ImGuiRenderer)
    {
        ImGuiRenderer->OnWindowResize(width, height);
    }
}

// ============================================================================
// Input Events
// ============================================================================

void FEditorApplication::onKeyPressed(MonsterRender::EKey key)
{
    if (ImGuiInputHandler)
    {
        ImGuiInputHandler->OnKeyEvent(key, MonsterRender::EInputAction::Pressed);
    }

    // Handle editor shortcuts
    if (key == MonsterRender::EKey::Escape)
    {
        requestExit();
    }
}

void FEditorApplication::onKeyReleased(MonsterRender::EKey key)
{
    if (ImGuiInputHandler)
    {
        ImGuiInputHandler->OnKeyEvent(key, MonsterRender::EInputAction::Released);
    }
}

void FEditorApplication::onMouseButtonPressed(MonsterRender::EKey button, const MonsterRender::MousePosition& position)
{
    if (ImGuiInputHandler)
    {
        ImGuiInputHandler->OnMouseButton(button, true);
    }
}

void FEditorApplication::onMouseButtonReleased(MonsterRender::EKey button, const MonsterRender::MousePosition& position)
{
    if (ImGuiInputHandler)
    {
        ImGuiInputHandler->OnMouseButton(button, false);
    }
}

void FEditorApplication::onMouseMoved(const MonsterRender::MousePosition& position)
{
    if (ImGuiInputHandler)
    {
        ImGuiInputHandler->OnMouseMove(static_cast<float>(position.x), static_cast<float>(position.y));
    }
}

void FEditorApplication::onMouseScrolled(float64 xOffset, float64 yOffset)
{
    if (ImGuiInputHandler)
    {
        ImGuiInputHandler->OnMouseScroll(static_cast<float>(xOffset), static_cast<float>(yOffset));
    }
}

// ============================================================================
// ImGui Management
// ============================================================================

bool FEditorApplication::InitializeImGui()
{
    MR_LOG(LogEditor, Log, "Initializing ImGui...");

    // Create ImGui context
    ImGuiContext = MakeUnique<FImGuiContext>();
    if (!ImGuiContext->Initialize())
    {
        MR_LOG(LogEditor, Error, "Failed to initialize ImGui context");
        return false;
    }

    // Create ImGui renderer
    MonsterRender::RHI::IRHIDevice* device = nullptr;
    if (m_engine)
    {
        device = m_engine->getRHIDevice();
    }

    if (!device)
    {
        MR_LOG(LogEditor, Error, "No RHI device available for ImGui renderer");
        return false;
    }

    ImGuiRenderer = MakeUnique<FImGuiRenderer>();
    if (!ImGuiRenderer->Initialize(device))
    {
        MR_LOG(LogEditor, Error, "Failed to initialize ImGui renderer");
        return false;
    }

    // Create input handler
    ImGuiInputHandler = MakeUnique<FImGuiInputHandler>(ImGuiContext.Get());

    // Set initial window size
    if (m_window)
    {
        ImGuiRenderer->OnWindowResize(m_window->getWidth(), m_window->getHeight());
    }

    bImGuiInitialized = true;
    MR_LOG(LogEditor, Log, "ImGui initialized successfully");

    return true;
}

void FEditorApplication::ShutdownImGui()
{
    MR_LOG(LogEditor, Log, "Shutting down ImGui...");

    bImGuiInitialized = false;

    // Shutdown in reverse order
    ImGuiInputHandler.Reset();

    if (ImGuiRenderer)
    {
        ImGuiRenderer->Shutdown();
        ImGuiRenderer.Reset();
    }

    if (ImGuiContext)
    {
        ImGuiContext->Shutdown();
        ImGuiContext.Reset();
    }

    MR_LOG(LogEditor, Log, "ImGui shutdown complete");
}

void FEditorApplication::RenderImGui()
{
    if (!bImGuiInitialized || !ImGuiContext || !ImGuiRenderer)
    {
        return;
    }

    // Get window dimensions
    uint32 width = m_window ? m_window->getWidth() : 1280;
    uint32 height = m_window ? m_window->getHeight() : 720;

    // Begin ImGui frame
    ImGuiContext->BeginFrame(m_deltaTime, width, height);

    // Setup docking space
    ImGuiContext->SetupDockSpace();

    // Render main menu bar
    RenderMainMenuBar();

    // Render editor panels
    RenderEditorPanels();

    // Render demo window if enabled
    if (bShowDemoWindow)
    {
        RenderDemoWindow();
    }

    // End ImGui frame
    ImGuiContext->EndFrame();

    // Get draw data and render
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData)
    {
        MonsterRender::RHI::IRHIDevice* device = m_engine ? m_engine->getRHIDevice() : nullptr;
        if (device)
        {
            MonsterRender::RHI::IRHICommandList* cmdList = device->getImmediateCommandList();
            if (cmdList)
            {
                ImGuiRenderer->RenderDrawData(cmdList, drawData);
            }
        }
    }
}

void FEditorApplication::RenderMainMenuBar()
{
    if (ImGui::BeginMainMenuBar())
    {
        // File menu
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene", "Ctrl+N"))
            {
                // TODO: Create new scene
            }
            if (ImGui::MenuItem("Open Scene", "Ctrl+O"))
            {
                // TODO: Open scene
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            {
                // TODO: Save scene
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4"))
            {
                requestExit();
            }
            ImGui::EndMenu();
        }

        // Edit menu
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z"))
            {
                // TODO: Undo
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y"))
            {
                // TODO: Redo
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X"))
            {
                // TODO: Cut
            }
            if (ImGui::MenuItem("Copy", "Ctrl+C"))
            {
                // TODO: Copy
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V"))
            {
                // TODO: Paste
            }
            ImGui::EndMenu();
        }

        // View menu
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Demo Window", nullptr, &bShowDemoWindow);
            ImGui::MenuItem("Metrics", nullptr, &bShowMetricsWindow);
            ImGui::EndMenu();
        }

        // Help menu
        if (ImGui::BeginMenu("Help"))
        {
            ImGui::MenuItem("About", nullptr, &bShowAboutWindow);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void FEditorApplication::RenderEditorPanels()
{
    // Render metrics window if enabled
    if (bShowMetricsWindow)
    {
        ImGui::ShowMetricsWindow(&bShowMetricsWindow);
    }

    // Render about window if enabled
    if (bShowAboutWindow)
    {
        ImGui::Begin("About MonsterEngine Editor", &bShowAboutWindow, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("MonsterEngine Editor");
        ImGui::Separator();
        ImGui::Text("A modern rendering engine inspired by UE5");
        ImGui::Text("Built with Dear ImGui %s", IMGUI_VERSION);
        ImGui::Separator();
        ImGui::Text("Copyright Monster Engine. All Rights Reserved.");
        ImGui::End();
    }

    // Placeholder panels - will be replaced with actual editor panels
    
    // Hierarchy panel placeholder
    ImGui::Begin("Hierarchy");
    ImGui::Text("Scene objects will appear here");
    ImGui::End();

    // Properties panel placeholder
    ImGui::Begin("Properties");
    ImGui::Text("Object properties will appear here");
    ImGui::End();

    // Viewport panel placeholder
    ImGui::Begin("Viewport");
    ImGui::Text("3D viewport will render here");
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    ImGui::Text("Viewport size: %.0f x %.0f", viewportSize.x, viewportSize.y);
    ImGui::End();

    // Console panel placeholder
    ImGui::Begin("Console");
    ImGui::Text("Console output will appear here");
    ImGui::End();

    // Asset browser placeholder
    ImGui::Begin("Asset Browser");
    ImGui::Text("Assets will appear here");
    ImGui::End();
}

void FEditorApplication::RenderDemoWindow()
{
    ImGui::ShowDemoWindow(&bShowDemoWindow);
}

} // namespace Editor
} // namespace MonsterEngine
