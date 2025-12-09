#include "Core/Application.h"
#include "Core/Log.h"
#include "Core/HAL/FMemoryManager.h"
#include "RHI/RHI.h"

#include <chrono>
#include <thread>

namespace MonsterRender {
    
    Application::Application(const ApplicationConfig& config) 
        : m_config(config) {
        MR_LOG_INFO("Creating application: " + config.name);
    }
    
    Application::~Application() {
        shutdown();
    }
    
    bool Application::initialize() {
        if (m_initialized) {
            MR_LOG_WARNING("Application already initialized");
            return true;
        }
        
        MR_LOG_INFO("Initializing application...");
        
        // Initialize memory system FIRST - required by all other systems
        if (!FMemoryManager::Get().Initialize()) {
            MR_LOG_ERROR("Failed to initialize memory system");
            return false;
        }
        MR_LOG_INFO("Memory system initialized successfully");
        
        // Initialize window factory
        WindowFactory::initialize();
        
        // Set RHI backend in window properties for proper window creation
        m_config.windowProperties.rhiBackend = m_config.preferredBackend;
        
        // Create window
        m_window = WindowFactory::createWindow(m_config.windowProperties);
        if (!m_window) {
            MR_LOG_ERROR("Failed to create application window");
            return false;
        }
        
        // Setup window event callbacks
        m_window->onClose = [this]() { onWindowCloseInternal(); };
        m_window->onResize = [this](uint32 width, uint32 height) { onWindowResizeInternal(width, height); };
        
        // Setup input event callbacks
        if (auto* inputManager = m_window->getInputManager()) {
            inputManager->onKeyEvent = [this](const InputEvent& event) { onKeyEventInternal(event); };
            inputManager->onMouseButtonEvent = [this](EKey button, const MousePosition& pos) { onMouseButtonEventInternal(button, pos); };
            inputManager->onMouseMoveEvent = [this](const MousePosition& pos) { onMouseMoveEventInternal(pos); };
            inputManager->onMouseScrollEvent = [this](float64 x, float64 y) { onMouseScrollEventInternal(x, y); };
        }
        
        // Create engine
        m_engine = MakeUnique<Engine>();
        
        // Setup RHI
        RHI::RHICreateInfo rhiCreateInfo;
        rhiCreateInfo.preferredBackend = m_config.preferredBackend;
        rhiCreateInfo.enableValidation = m_config.enableValidation;
        rhiCreateInfo.enableDebugMarkers = m_config.enableDebugMarkers;
        rhiCreateInfo.applicationName = m_config.name;
        rhiCreateInfo.windowWidth = m_config.windowProperties.width;
        rhiCreateInfo.windowHeight = m_config.windowProperties.height;
        rhiCreateInfo.windowHandle = m_window->getNativeHandle();
        rhiCreateInfo.displayHandle = m_window->getNativeDisplayHandle();
        
        if (!m_engine->initialize(rhiCreateInfo)) {
            MR_LOG_ERROR("Failed to initialize engine");
            return false;
        }
        
        // Initialize timing
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        m_lastFrameTime = std::chrono::duration<float32>(duration).count();
        
        // Call derived class initialization
        onInitialize();
        
        m_initialized = true;
        MR_LOG_INFO("Application initialized successfully");
        return true;
    }
    
    void Application::shutdown() {
        if (!m_initialized) {
            return;
        }
        
        MR_LOG_INFO("Shutting down application...");
        
        // Call derived class shutdown
        onShutdown();
        
        // Shutdown engine
        if (m_engine) {
            m_engine->shutdown();
            m_engine.reset();
        }
        
        // Shutdown window
        if (m_window) {
            m_window->shutdown();
            m_window.reset();
        }
        
        // Shutdown window factory
        WindowFactory::shutdown();
        
        // Shutdown memory system LAST - after all other systems are destroyed
        FMemoryManager::Get().Shutdown();
        MR_LOG_INFO("Memory system shut down");
        
        m_initialized = false;
        MR_LOG_INFO("Application shut down successfully");
    }
    
    int32 Application::run() {
        if (!initialize()) {
            MR_LOG_ERROR("Failed to initialize application");
            return -1;
        }
        
        MR_LOG_INFO("Starting application main loop...");
        
        // Main application loop - similar to UE5's main loop
        while (!shouldExit() && !m_window->shouldClose()) {
            tick();
        }
        
        MR_LOG_INFO("Application main loop ended");
        return 0;
    }
    
    void Application::tick() {
        // Update timing
        calculateDeltaTime();
        
        // Process window events
        m_window->pollEvents();
        
        // Skip rendering if minimized
        if (m_minimized) {
            // Sleep a bit to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            return;
        }
        
        // Reset input frame state
        if (auto* inputManager = m_window->getInputManager()) {
            inputManager->resetFrameState();
        }
        
        // Update application
        onUpdate(m_deltaTime);
        
        // Render frame
        onRender();
        
        // Update statistics
        m_frameCount++;
        m_fpsTimer += m_deltaTime;
        if (m_fpsTimer >= 1.0f) {
            m_fps = static_cast<uint32>(m_frameCount / m_fpsTimer);
            m_frameCount = 0;
            m_fpsTimer = 0.0f;
            
            // Update window title with FPS (optional)
            if (m_config.windowProperties.title.find("FPS") == String::npos) {
                String titleWithFPS = m_config.name + " - FPS: " + std::to_string(m_fps);
                m_window->setTitle(titleWithFPS);
            }
        }
    }
    
    void Application::calculateDeltaTime() {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        float32 currentTime = std::chrono::duration<float32>(duration).count();
        
        m_deltaTime = currentTime - m_lastFrameTime;
        m_lastFrameTime = currentTime;
        
        // Clamp delta time to reasonable values
        const float32 maxDeltaTime = 1.0f / 10.0f; // 10 FPS minimum
        m_deltaTime = std::min(m_deltaTime, maxDeltaTime);
    }
    
    void Application::onWindowCloseInternal() {
        MR_LOG_INFO("Window close requested");
        onWindowClose();
        requestExit();
    }
    
    void Application::onWindowResizeInternal(uint32 width, uint32 height) {
        MR_LOG_INFO("Window resized to " + std::to_string(width) + "x" + std::to_string(height));
        
        // Check if minimized
        m_minimized = (width == 0 || height == 0);
        
        if (!m_minimized) {
            // Notify engine of resize
            if (m_engine) {
                // TODO: Implement engine resize handling
                // m_engine->onWindowResize(width, height);
            }
            
            onWindowResize(width, height);
        }
    }
    
    void Application::onKeyEventInternal(const InputEvent& event) {
        switch (event.action) {
            case EInputAction::Pressed:
                onKeyPressed(event.key);
                break;
            case EInputAction::Released:
                onKeyReleased(event.key);
                break;
            default:
                break;
        }
        
        // Handle common application keys
        if (event.action == EInputAction::Pressed) {
            switch (event.key) {
                case EKey::Escape:
                    requestExit();
                    break;
                default:
                    break;
            }
        }
    }
    
    void Application::onMouseButtonEventInternal(EKey button, const MousePosition& position) {
        // Determine if it's a press or release based on current state
        if (auto* inputManager = m_window->getInputManager()) {
            if (inputManager->isMouseButtonPressed(button)) {
                onMouseButtonPressed(button, position);
            } else if (inputManager->isMouseButtonReleased(button)) {
                onMouseButtonReleased(button, position);
            }
        }
    }
    
    void Application::onMouseMoveEventInternal(const MousePosition& position) {
        onMouseMoved(position);
    }
    
    void Application::onMouseScrollEventInternal(float64 xOffset, float64 yOffset) {
        onMouseScrolled(xOffset, yOffset);
    }
}
