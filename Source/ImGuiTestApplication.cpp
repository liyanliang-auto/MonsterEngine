// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ImGuiTestApplication.cpp
 * @brief Simple ImGui test application with Cube rendering
 * 
 * This application demonstrates ImGui integration with the engine,
 * displaying frame rate and rendering a cube in the viewport.
 */

#include "Core/Application.h"
#include "Core/Logging/Logging.h"
#include "CubeRenderer.h"
#include "imgui.h"
#include "Editor/ImGui/ImGuiContext.h"
#include "Editor/ImGui/ImGuiRenderer.h"
#include "Editor/ImGui/ImGuiInputHandler.h"
#include "Platform/OpenGL/OpenGLFunctions.h"

using namespace MonsterRender;
using namespace MonsterEngine;

// Declare log category
DECLARE_LOG_CATEGORY_EXTERN(LogImGuiTest, Log, All);
DEFINE_LOG_CATEGORY(LogImGuiTest);

/**
 * @class ImGuiTestApplication
 * @brief Test application demonstrating ImGui integration with Cube rendering
 */
class ImGuiTestApplication : public Application {
public:
    ImGuiTestApplication() 
        : Application(createConfig())
        , m_cubeRenderer(nullptr)
        , m_imguiContext(nullptr)
        , m_imguiRenderer(nullptr)
        , m_imguiInputHandler(nullptr)
        , m_frameCount(0)
        , m_fps(0.0f)
        , m_fpsUpdateTimer(0.0f)
        , m_lastFrameTime(0.0f)
        , m_showDemoWindow(false)
        , m_showStatsWindow(true)
        , m_cubeRotationSpeed(1.0f)
    {
    }
    
    ~ImGuiTestApplication() override = default;

protected:
    void onInitialize() override {
        MR_LOG(LogImGuiTest, Log, "ImGuiTestApplication initializing...");
        
        // Initialize cube renderer
        auto* device = getEngine()->getRHIDevice();
        if (device) {
            m_cubeRenderer = new CubeRenderer();
            if (!m_cubeRenderer->initialize(device)) {
                MR_LOG(LogImGuiTest, Error, "Failed to initialize CubeRenderer");
                delete m_cubeRenderer;
                m_cubeRenderer = nullptr;
            }
        }
        
        // Initialize ImGui
        initializeImGui();
        
        MR_LOG(LogImGuiTest, Log, "ImGuiTestApplication initialized successfully");
    }
    
    void onShutdown() override {
        MR_LOG(LogImGuiTest, Log, "ImGuiTestApplication shutting down...");
        
        // Shutdown ImGui
        shutdownImGui();
        
        // Cleanup cube renderer
        if (m_cubeRenderer) {
            delete m_cubeRenderer;
            m_cubeRenderer = nullptr;
        }
    }
    
    void onUpdate(float32 deltaTime) override {
        m_lastFrameTime = deltaTime;
        
        // Update FPS counter
        m_frameCount++;
        m_fpsUpdateTimer += deltaTime;
        if (m_fpsUpdateTimer >= 1.0f) {
            m_fps = static_cast<float>(m_frameCount) / m_fpsUpdateTimer;
            m_frameCount = 0;
            m_fpsUpdateTimer = 0.0f;
        }
        
        // Update cube animation
        if (m_cubeRenderer) {
            m_cubeRenderer->update(deltaTime * m_cubeRotationSpeed);
        }
    }
    
    void onRender() override {
        auto* device = getEngine()->getRHIDevice();
        if (!device) return;
        
        RHI::ERHIBackend backend = device->getBackendType();
        
        if (backend == RHI::ERHIBackend::OpenGL) {
            renderOpenGL();
        } else {
            // Vulkan path - simplified for now
            renderVulkan();
        }
    }
    
    void onWindowResize(uint32 width, uint32 height) override {
        MR_LOG(LogImGuiTest, Log, "Window resized to %ux%u", width, height);
        
        if (m_imguiRenderer) {
            m_imguiRenderer->OnWindowResize(width, height);
        }
        
        if (m_cubeRenderer) {
            m_cubeRenderer->setWindowDimensions(width, height);
        }
    }
    
    // Input event handlers for ImGui
    void onKeyPressed(EKey key) override {
        if (m_imguiInputHandler) {
            m_imguiInputHandler->OnKeyEvent(key, MonsterRender::EInputAction::Pressed);
        }
    }
    
    void onKeyReleased(EKey key) override {
        if (m_imguiInputHandler) {
            m_imguiInputHandler->OnKeyEvent(key, MonsterRender::EInputAction::Released);
        }
    }
    
    void onMouseButtonPressed(EKey button, const MousePosition& position) override {
        if (m_imguiInputHandler) {
            m_imguiInputHandler->OnMouseButton(button, true);
        }
    }
    
    void onMouseButtonReleased(EKey button, const MousePosition& position) override {
        if (m_imguiInputHandler) {
            m_imguiInputHandler->OnMouseButton(button, false);
        }
    }
    
    void onMouseMoved(const MousePosition& position) override {
        if (m_imguiInputHandler) {
            m_imguiInputHandler->OnMouseMove(static_cast<float>(position.x), 
                                             static_cast<float>(position.y));
        }
    }
    
    void onMouseScrolled(float64 xOffset, float64 yOffset) override {
        if (m_imguiInputHandler) {
            m_imguiInputHandler->OnMouseScroll(static_cast<float>(xOffset), 
                                               static_cast<float>(yOffset));
        }
    }

private:
    static ApplicationConfig createConfig() {
        ApplicationConfig config;
        config.name = "ImGui Test Application";
        config.windowProperties.title = "MonsterEngine - ImGui Test";
        config.windowProperties.width = 1280;
        config.windowProperties.height = 720;
        config.preferredBackend = RHI::ERHIBackend::OpenGL;
        return config;
    }
    
    void initializeImGui() {
        auto* device = getEngine()->getRHIDevice();
        if (!device) {
            MR_LOG(LogImGuiTest, Error, "Cannot initialize ImGui: no RHI device");
            return;
        }
        
        // Create ImGui context
        m_imguiContext = new Editor::FImGuiContext();
        if (!m_imguiContext->Initialize()) {
            MR_LOG(LogImGuiTest, Error, "Failed to initialize ImGui context");
            delete m_imguiContext;
            m_imguiContext = nullptr;
            return;
        }
        
        // Create ImGui renderer
        m_imguiRenderer = new Editor::FImGuiRenderer();
        if (!m_imguiRenderer->Initialize(device)) {
            MR_LOG(LogImGuiTest, Error, "Failed to initialize ImGui renderer");
            delete m_imguiRenderer;
            m_imguiRenderer = nullptr;
            return;
        }
        
        // Create ImGui input handler
        m_imguiInputHandler = new Editor::FImGuiInputHandler(m_imguiContext);
        
        MR_LOG(LogImGuiTest, Log, "ImGui initialized successfully");
    }
    
    void shutdownImGui() {
        if (m_imguiInputHandler) {
            delete m_imguiInputHandler;
            m_imguiInputHandler = nullptr;
        }
        
        if (m_imguiRenderer) {
            m_imguiRenderer->Shutdown();
            delete m_imguiRenderer;
            m_imguiRenderer = nullptr;
        }
        
        if (m_imguiContext) {
            m_imguiContext->Shutdown();
            delete m_imguiContext;
            m_imguiContext = nullptr;
        }
    }
    
    void renderOpenGL() {
        using namespace MonsterEngine::OpenGL;
        
        // Bind default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        // Set viewport
        int width = getWindow()->getWidth();
        int height = getWindow()->getHeight();
        glViewport(0, 0, width, height);
        
        // Clear screen
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Enable depth testing for 3D rendering
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        
        // Render cube
        auto* device = getEngine()->getRHIDevice();
        auto* cmdList = device->getImmediateCommandList();
        if (cmdList && m_cubeRenderer) {
            m_cubeRenderer->render(cmdList, m_lastFrameTime);
        }
        
        // Disable depth test for ImGui
        glDisable(GL_DEPTH_TEST);
        
        // Render ImGui
        renderImGui();
        
        // Swap buffers
        getWindow()->swapBuffers();
    }
    
    void renderVulkan() {
        // Simplified Vulkan rendering - just render ImGui for now
        auto* device = getEngine()->getRHIDevice();
        auto* cmdList = device->getImmediateCommandList();
        
        if (cmdList) {
            cmdList->begin();
            
            // Render ImGui
            renderImGui();
            
            cmdList->end();
        }
        
        getWindow()->swapBuffers();
    }
    
    void renderImGui() {
        if (!m_imguiContext || !m_imguiRenderer) {
            return;
        }
        
        auto* device = getEngine()->getRHIDevice();
        auto* cmdList = device->getImmediateCommandList();
        
        // Get window dimensions
        int width = getWindow()->getWidth();
        int height = getWindow()->getHeight();
        
        // Begin ImGui frame
        m_imguiContext->BeginFrame(m_lastFrameTime, static_cast<uint32>(width), static_cast<uint32>(height));
        
        // Render ImGui UI
        renderImGuiUI();
        
        // End ImGui frame
        m_imguiContext->EndFrame();
        
        // Render ImGui draw data
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData && cmdList) {
            m_imguiRenderer->RenderDrawData(cmdList, drawData);
        }
    }
    
    void renderImGuiUI() {
        // Main menu bar
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    requestExit();
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Stats Window", nullptr, &m_showStatsWindow);
                ImGui::MenuItem("ImGui Demo", nullptr, &m_showDemoWindow);
                ImGui::EndMenu();
            }
            
            // Show FPS in menu bar
            ImGui::Separator();
            ImGui::Text("FPS: %.1f", m_fps);
            
            ImGui::EndMainMenuBar();
        }
        
        // Stats window
        if (m_showStatsWindow) {
            ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
            
            if (ImGui::Begin("Statistics", &m_showStatsWindow)) {
                ImGui::Text("Frame Statistics");
                ImGui::Separator();
                
                ImGui::Text("FPS: %.1f", m_fps);
                ImGui::Text("Frame Time: %.3f ms", m_lastFrameTime * 1000.0f);
                
                ImGui::Separator();
                ImGui::Text("Cube Settings");
                
                ImGui::SliderFloat("Rotation Speed", &m_cubeRotationSpeed, 0.0f, 5.0f);
                
                if (ImGui::Button("Reset Speed")) {
                    m_cubeRotationSpeed = 1.0f;
                }
                
                ImGui::Separator();
                ImGui::Text("Renderer Info");
                
                auto* device = getEngine()->getRHIDevice();
                if (device) {
                    RHI::ERHIBackend backend = device->getBackendType();
                    const char* backendName = (backend == RHI::ERHIBackend::OpenGL) ? "OpenGL" : "Vulkan";
                    ImGui::Text("Backend: %s", backendName);
                }
                
                int width = getWindow()->getWidth();
                int height = getWindow()->getHeight();
                ImGui::Text("Resolution: %dx%d", width, height);
            }
            ImGui::End();
        }
        
        // Demo window
        if (m_showDemoWindow) {
            ImGui::ShowDemoWindow(&m_showDemoWindow);
        }
    }

private:
    // Cube renderer
    CubeRenderer* m_cubeRenderer;
    
    // ImGui components
    Editor::FImGuiContext* m_imguiContext;
    Editor::FImGuiRenderer* m_imguiRenderer;
    Editor::FImGuiInputHandler* m_imguiInputHandler;
    
    // Frame statistics
    int m_frameCount;
    float m_fps;
    float m_fpsUpdateTimer;
    float m_lastFrameTime;
    
    // UI state
    bool m_showDemoWindow;
    bool m_showStatsWindow;
    
    // Cube settings
    float m_cubeRotationSpeed;
};

// Factory function to create the ImGui test application
MonsterEngine::TUniquePtr<Application> createImGuiTestApplication() {
    return MonsterEngine::MakeUnique<ImGuiTestApplication>();
}
