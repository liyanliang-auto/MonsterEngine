#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHI.h"
#include "Core/RuntimeLease.h"

namespace MonsterRender {
    
    /**
     * Main engine class that manages all subsystems
     */
    class Engine {
    public:
        Engine();
        ~Engine();
        
        /**
         * Initialize the engine
         */
        bool initialize(const RHI::RHICreateInfo& rhiCreateInfo);
        
        /**
         * Shutdown the engine
         */
        void shutdown();
        
        /**
         * Run the main engine loop
         */
        void run();
        /** Return the actual backend transaction; no inferred frame/token success. */
        RHI::FFrameSubmitResult PresentFrame();
        
        /**
         * Get the RHI device
         */
        RHI::IRHIDevice* getRHIDevice() const { return m_rhiDevice.get(); }
        
        /**
         * Check if the engine is initialized
         */
        bool isInitialized() const { return m_initialized; }
        
        /**
         * Get window width
         */
        uint32 getWindowWidth() const { return m_windowWidth; }
        
        /**
         * Get window height
         */
        uint32 getWindowHeight() const { return m_windowHeight; }
        
    private:
        FProcessRuntimeLease m_runtimeLease; // outlives device and runtime allocations
        // RHI system
        TUniquePtr<RHI::IRHIDevice> m_rhiDevice;
        
        // Engine state
        bool m_initialized = false;
        bool m_shouldRun = true;
        
        // Window dimensions
        uint32 m_windowWidth = 800;
        uint32 m_windowHeight = 600;
        
        // Update the engine
        void update();
        
        // Render a frame
        void render();
    };
}
