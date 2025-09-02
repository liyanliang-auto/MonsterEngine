#include "Core/CoreMinimal.h"
#include "RHI/RHI.h"

// Platform-specific includes
#if PLATFORM_WINDOWS
    #include "Platform/Vulkan/VulkanDevice.h"
#endif

namespace MonsterRender::RHI {
    
    TUniquePtr<IRHIDevice> RHIFactory::createDevice(const RHICreateInfo& createInfo) {
        MR_LOG_INFO("Creating RHI device...");
        
        ERHIBackend backend = createInfo.preferredBackend;
        
        // Auto-select if None specified
        if (backend == ERHIBackend::None) {
            backend = selectBestBackend();
            MR_LOG_INFO("Auto-selected RHI backend: " + String(getBackendName(backend)));
        }
        
        // Try to create device for the specified backend
        switch (backend) {
            case ERHIBackend::Vulkan:
                if (isBackendAvailable(ERHIBackend::Vulkan)) {
                    auto vulkanDevice = MakeUnique<Vulkan::VulkanDevice>();
                    if (vulkanDevice->initialize(createInfo)) {
                        MR_LOG_INFO("Successfully created Vulkan RHI device");
                        return std::move(vulkanDevice);
                    } else {
                        MR_LOG_ERROR("Failed to initialize Vulkan device");
                    }
                }
                break;
                
            case ERHIBackend::D3D12:
                MR_LOG_WARNING("D3D12 backend not yet implemented");
                break;
                
            case ERHIBackend::D3D11:
                MR_LOG_WARNING("D3D11 backend not yet implemented");
                break;
                
            case ERHIBackend::OpenGL:
                MR_LOG_WARNING("OpenGL backend not yet implemented");
                break;
                
            case ERHIBackend::Metal:
                MR_LOG_WARNING("Metal backend not yet implemented");
                break;
                
            default:
                MR_LOG_ERROR("Unknown RHI backend specified");
                break;
        }
        
        MR_LOG_ERROR("Failed to create RHI device for any available backend");
        return nullptr;
    }
    
    TArray<ERHIBackend> RHIFactory::getAvailableBackends() {
        TArray<ERHIBackend> availableBackends;
        
        // Check each backend availability
        if (isBackendAvailable(ERHIBackend::Vulkan)) {
            availableBackends.push_back(ERHIBackend::Vulkan);
        }
        
        #if PLATFORM_WINDOWS
        if (isBackendAvailable(ERHIBackend::D3D12)) {
            availableBackends.push_back(ERHIBackend::D3D12);
        }
        
        if (isBackendAvailable(ERHIBackend::D3D11)) {
            availableBackends.push_back(ERHIBackend::D3D11);
        }
        #endif
        
        if (isBackendAvailable(ERHIBackend::OpenGL)) {
            availableBackends.push_back(ERHIBackend::OpenGL);
        }
        
        #if defined(__APPLE__)
        if (isBackendAvailable(ERHIBackend::Metal)) {
            availableBackends.push_back(ERHIBackend::Metal);
        }
        #endif
        
        return availableBackends;
    }
    
    bool RHIFactory::isBackendAvailable(ERHIBackend backend) {
        switch (backend) {
            case ERHIBackend::Vulkan:
                return Vulkan::VulkanAPI::isAvailable();
                
            case ERHIBackend::D3D12:
                #if PLATFORM_WINDOWS
                // TODO: Check D3D12 availability
                return false;
                #else
                return false;
                #endif
                
            case ERHIBackend::D3D11:
                #if PLATFORM_WINDOWS
                // TODO: Check D3D11 availability
                return false;
                #else
                return false;
                #endif
                
            case ERHIBackend::OpenGL:
                // TODO: Check OpenGL availability
                return false;
                
            case ERHIBackend::Metal:
                #if defined(__APPLE__)
                // TODO: Check Metal availability
                return false;
                #else
                return false;
                #endif
                
            default:
                return false;
        }
    }
    
    const char* RHIFactory::getBackendName(ERHIBackend backend) {
        switch (backend) {
            case ERHIBackend::None: return "None";
            case ERHIBackend::Vulkan: return "Vulkan";
            case ERHIBackend::D3D12: return "Direct3D 12";
            case ERHIBackend::D3D11: return "Direct3D 11";
            case ERHIBackend::OpenGL: return "OpenGL";
            case ERHIBackend::Metal: return "Metal";
            default: return "Unknown";
        }
    }
    
    ERHIBackend RHIFactory::selectBestBackend() {
        auto availableBackends = getAvailableBackends();
        
        if (availableBackends.empty()) {
            MR_LOG_ERROR("No RHI backends available!");
            return ERHIBackend::None;
        }
        
        // Priority order for backend selection
        #if PLATFORM_WINDOWS
        // On Windows, prefer D3D12 > Vulkan > D3D11 > OpenGL
        TArray<ERHIBackend> preferredOrder = {
            ERHIBackend::D3D12,
            ERHIBackend::Vulkan,
            ERHIBackend::D3D11,
            ERHIBackend::OpenGL
        };
        #elif PLATFORM_LINUX
        // On Linux, prefer Vulkan > OpenGL
        TArray<ERHIBackend> preferredOrder = {
            ERHIBackend::Vulkan,
            ERHIBackend::OpenGL
        };
        #elif defined(__APPLE__)
        // On macOS, prefer Metal > Vulkan (via MoltenVK) > OpenGL
        TArray<ERHIBackend> preferredOrder = {
            ERHIBackend::Metal,
            ERHIBackend::Vulkan,
            ERHIBackend::OpenGL
        };
        #else
        // Default fallback
        TArray<ERHIBackend> preferredOrder = {
            ERHIBackend::Vulkan,
            ERHIBackend::OpenGL
        };
        #endif
        
        // Select the first available backend from the preferred order
        for (ERHIBackend preferred : preferredOrder) {
            for (ERHIBackend available : availableBackends) {
                if (preferred == available) {
                    return preferred;
                }
            }
        }
        
        // If no preferred backend is available, return the first available one
        return availableBackends[0];
    }
}
