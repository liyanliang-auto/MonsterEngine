#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIDevice.h"

namespace MonsterRender::RHI {
    
    // ERHIBackend is defined in IRHISwapChain.h
    
    /**
     * RHI initialization parameters
     */
    struct RHICreateInfo {
        ERHIBackend preferredBackend = ERHIBackend::Vulkan;
        bool enableValidation = false;
        bool enableDebugMarkers = true;
        String applicationName = "MonsterRender Application";
        uint32 applicationVersion = 1;
        uint32 engineVersion = 1;
        uint32 windowWidth = 1920;
        uint32 windowHeight = 1080;
        void* windowHandle = nullptr; // Platform-specific window handle
        void* displayHandle = nullptr; // Platform-specific display handle (Linux only)
        
        RHICreateInfo() = default;
    };
    
    /**
     * RHI Factory for creating platform-specific implementations
     */
    class RHIFactory {
    public:
        /**
         * Create an RHI device for the specified backend
         */
        static TUniquePtr<IRHIDevice> createDevice(const RHICreateInfo& createInfo);
        
        /**
         * Get available RHI backends on current platform
         */
        static TArray<ERHIBackend> getAvailableBackends();
        
        /**
         * Check if a specific backend is available
         */
        static bool isBackendAvailable(ERHIBackend backend);
        
        /**
         * Get backend name string
         */
        static const char* getBackendName(ERHIBackend backend);
        
        /**
         * Auto-select the best available backend for current platform
         */
        static ERHIBackend selectBestBackend();
    };
}
