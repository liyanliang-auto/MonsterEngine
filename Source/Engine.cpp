#include "Core/CoreMinimal.h"
#include "Engine.h"

namespace MonsterRender {
    
    Engine::Engine() {
        MR_LOG_INFO("MonsterRender Engine created");
    }
    
    Engine::~Engine() {
        shutdown();
        MR_LOG_INFO("MonsterRender Engine destroyed");
    }
    
    bool Engine::initialize(const RHI::RHICreateInfo& rhiCreateInfo) {
        if (m_initialized) {
            MR_LOG_WARNING("Engine already initialized");
            return true;
        }
        
        MR_LOG_INFO("Initializing MonsterRender Engine...");
        // Initialize memory system first
        MemorySystem::get().initialize();
        
        // Initialize RHI system
        MR_LOG_INFO("Available RHI backends:");
        auto availableBackends = RHI::RHIFactory::getAvailableBackends();
        for (auto backend : availableBackends) {
            MR_LOG_INFO("  - " + String(RHI::RHIFactory::getBackendName(backend)));
        }
        
        m_rhiDevice = RHI::RHIFactory::createDevice(rhiCreateInfo);
        if (!m_rhiDevice) {
            MR_LOG_ERROR("Failed to create RHI device");
            return false;
        }
        
        // Print device capabilities
        const auto& caps = m_rhiDevice->getCapabilities();
        MR_LOG_INFO("RHI Device initialized:");
        MR_LOG_INFO("  Device: " + caps.deviceName);
        MR_LOG_INFO("  Vendor: " + caps.vendorName);
        MR_LOG_INFO("  Video Memory: " + std::to_string(caps.dedicatedVideoMemory / 1024 / 1024) + " MB");
        
        m_initialized = true;
        MR_LOG_INFO("MonsterRender Engine initialized successfully");
        return true;
    }
    
    void Engine::shutdown() {
        if (!m_initialized) {
            return;
        }
        
        MR_LOG_INFO("Shutting down MonsterRender Engine...");
        
        // Wait for all GPU work to complete
        if (m_rhiDevice) {
            m_rhiDevice->waitForIdle();
        }
        
        // Shutdown RHI
        m_rhiDevice.reset();
        
        m_initialized = false;
        MR_LOG_INFO("MonsterRender Engine shutdown complete");

        // Shutdown memory system last
        MemorySystem::get().shutdown();
    }
    
    void Engine::run() {
        if (!m_initialized) {
            MR_LOG_ERROR("Engine not initialized - cannot run");
            return;
        }
        
        MR_LOG_INFO("Starting engine main loop...");
        
        while (m_shouldRun) {
            update();
            render();
            
            // For now, just run one frame and exit
            // TODO: Implement proper windowing and event handling
            m_shouldRun = false;
        }
        
        MR_LOG_INFO("Engine main loop ended");
    }
    
    void Engine::update() {
        // TODO: Implement engine update logic
        // - Process input
        // - Update game logic
        // - Update render objects
    }
    
    void Engine::render() {
        if (!m_rhiDevice) {
            return;
        }
        
        // TODO: Implement basic rendering
        // For now, just clear and present
        
        // Get immediate command list
        auto* cmdList = m_rhiDevice->getImmediateCommandList();
        if (!cmdList) {
            MR_LOG_WARNING("No immediate command list available");
            return;
        }
        
        // Basic render operations
        MR_LOG_DEBUG("Rendering frame...");
        
        // Present the frame
        m_rhiDevice->present();
    }
}
