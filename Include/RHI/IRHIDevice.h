#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/IRHIResource.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHISwapChain.h"

namespace MonsterRender::RHI {
    
    /**
     * RHI device capabilities
     */
    struct RHIDeviceCapabilities {
        String deviceName;
        String vendorName;
        uint64 dedicatedVideoMemory = 0;
        uint64 dedicatedSystemMemory = 0;
        uint64 sharedSystemMemory = 0;
        
        bool supportsGeometryShader = false;
        bool supportsTessellation = false;
        bool supportsComputeShader = false;
        bool supportsMultiDrawIndirect = false;
        bool supportsTimestampQuery = false;
        
        uint32 maxTexture1DSize = 0;
        uint32 maxTexture2DSize = 0;
        uint32 maxTexture3DSize = 0;
        uint32 maxTextureCubeSize = 0;
        uint32 maxTextureArrayLayers = 0;
        uint32 maxRenderTargets = 8;
        uint32 maxVertexInputBindings = 16;
        uint32 maxVertexInputAttributes = 16;
    };
    
    /**
     * RHI Device interface
     * Factory for creating RHI resources and command lists
     */
    class IRHIDevice {
    public:
        IRHIDevice() = default;
        virtual ~IRHIDevice() = default;
        
        // Non-copyable, non-movable
        IRHIDevice(const IRHIDevice&) = delete;
        IRHIDevice& operator=(const IRHIDevice&) = delete;
        IRHIDevice(IRHIDevice&&) = delete;
        IRHIDevice& operator=(IRHIDevice&&) = delete;
        
        /**
         * Get device capabilities
         */
        virtual const RHIDeviceCapabilities& getCapabilities() const = 0;
        
        /**
         * Get the RHI backend type (Vulkan, OpenGL, etc.)
         */
        virtual ERHIBackend getBackendType() const = 0;
        
        // Resource creation
        /**
         * Create a buffer
         */
        virtual TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc) = 0;
        
        /**
         * Create a texture
         */
        virtual TSharedPtr<IRHITexture> createTexture(const TextureDesc& desc) = 0;
        
        /**
         * Create a vertex shader from bytecode
         */
        virtual TSharedPtr<IRHIVertexShader> createVertexShader(TSpan<const uint8> bytecode) = 0;
        
        /**
         * Create a pixel shader from bytecode
         */
        virtual TSharedPtr<IRHIPixelShader> createPixelShader(TSpan<const uint8> bytecode) = 0;
        
        /**
         * Create a pipeline state object
         */
        virtual TSharedPtr<IRHIPipelineState> createPipelineState(const PipelineStateDesc& desc) = 0;
        
        /**
         * Create a sampler state
         * Reference UE5: RHICreateSamplerState
         */
        virtual TSharedPtr<IRHISampler> createSampler(const SamplerDesc& desc) = 0;
        
        // Command list management
        /**
         * Create a command list
         */
        virtual TSharedPtr<IRHICommandList> createCommandList() = 0;
        
        /**
         * Execute command lists
         */
        virtual void executeCommandLists(TSpan<TSharedPtr<IRHICommandList>> commandLists) = 0;
        
        /**
         * Get the immediate command list (for direct execution)
         */
        virtual IRHICommandList* getImmediateCommandList() = 0;
        
        // Synchronization
        /**
         * Wait for all GPU work to complete
         */
        virtual void waitForIdle() = 0;
        
        /**
         * Present the current frame
         */
        virtual void present() = 0;
        
        // Memory management
        /**
         * Get memory usage statistics
         */
        virtual void getMemoryStats(uint64& usedBytes, uint64& availableBytes) = 0;
        
        /**
         * Force garbage collection of unused resources
         */
        virtual void collectGarbage() = 0;
        
        // Debug and validation
        /**
         * Set debug name for the device
         */
        virtual void setDebugName(const String& name) = 0;
        
        /**
         * Enable/disable GPU validation layer
         */
        virtual void setValidationEnabled(bool enabled) = 0;
        
        // SwapChain management
        /**
         * Create a swap chain for the given window
         * Reference: UE5 RHICreateViewport
         */
        virtual TSharedPtr<IRHISwapChain> createSwapChain(const SwapChainDesc& desc) = 0;
        
        /**
         * Get the RHI backend type
         */
        virtual ERHIBackend getRHIBackend() const = 0;
        
        /**
         * Get the render target format for the current swapchain
         */
        virtual EPixelFormat getSwapChainFormat() const = 0;
        
        /**
         * Get the depth format
         */
        virtual EPixelFormat getDepthFormat() const = 0;
        
    protected:
        RHIDeviceCapabilities m_capabilities;
    };
}
