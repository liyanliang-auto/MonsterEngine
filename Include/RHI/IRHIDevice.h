#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/RHIResources.h"
#include "RHI/IRHIResource.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHISwapChain.h"
#include "RHI/IRHIDescriptorSet.h"

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
    
	//IRHIDevice（设备接口）
	//	├── 资源创建：createBuffer / createTexture / createPipelineState / createSampler
	//	├── Shader：createVertexShader / createPixelShader
	//	├── 描述符：createDescriptorSetLayout / createPipelineLayout / allocateDescriptorSet
	//	├── 命令：getImmediateCommandList
	//	├── 同步：waitForIdle / present / collectGarbage
	//	└── 查询：getBackendType / getCapabilities / getSwapChainFormat

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
        
        // ========================================================================
        // Resource creation
        // ========================================================================
        
        /**
         * Create a generic buffer
         * @param desc Buffer description
         * @return Created buffer, or nullptr on failure
         */
        virtual TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc) = 0;
        
        /**
         * Create a vertex buffer (UE5-style convenience method)
         * 
         * Creates a GPU buffer specifically for vertex data. The buffer can optionally
         * be initialized with data at creation time.
         * 
         * @param Size Size of the buffer in bytes
         * @param Usage Buffer usage flags (Static, Dynamic, etc.)
         * @param CreateInfo Resource creation info with optional initial data
         * @return Created vertex buffer, or nullptr on failure
         * 
         * Reference UE5: RHICreateVertexBuffer
         */
        virtual TSharedPtr<FRHIVertexBuffer> CreateVertexBuffer(
            uint32 Size,
            EBufferUsageFlags Usage,
            FRHIResourceCreateInfo& CreateInfo) = 0;
        
        /**
         * Create an index buffer (UE5-style convenience method)
         * 
         * Creates a GPU buffer specifically for index data. Supports both 16-bit
         * and 32-bit indices.
         * 
         * @param Stride Index stride (2 for 16-bit, 4 for 32-bit)
         * @param Size Size of the buffer in bytes
         * @param Usage Buffer usage flags (Static, Dynamic, etc.)
         * @param CreateInfo Resource creation info with optional initial data
         * @return Created index buffer, or nullptr on failure
         * 
         * Reference UE5: RHICreateIndexBuffer
         */
        virtual TSharedPtr<FRHIIndexBuffer> CreateIndexBuffer(
            uint32 Stride,
            uint32 Size,
            EBufferUsageFlags Usage,
            FRHIResourceCreateInfo& CreateInfo) = 0;
        
        /**
         * Create a texture
         * @param desc Texture description
         * @return Created texture, or nullptr on failure
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
        
        // ========================================================================
        // Descriptor set management (Multi-descriptor set support)
        // ========================================================================
        
        /**
         * Create a descriptor set layout
         * Defines the layout/schema for a descriptor set (Set 0, Set 1, etc.)
         * Reference: UE5 RHICreateDescriptorSetLayout
         * 
         * @param desc Descriptor set layout description
         * @return Created descriptor set layout, or nullptr on failure
         */
        virtual TSharedPtr<IRHIDescriptorSetLayout> createDescriptorSetLayout(
            const FDescriptorSetLayoutDesc& desc) = 0;
        
        /**
         * Create a pipeline layout
         * Defines the complete layout of all descriptor sets and push constants
         * Reference: UE5 RHICreatePipelineLayout
         * 
         * @param desc Pipeline layout description
         * @return Created pipeline layout, or nullptr on failure
         */
        virtual TSharedPtr<IRHIPipelineLayout> createPipelineLayout(
            const FPipelineLayoutDesc& desc) = 0;
        
        /**
         * Allocate a descriptor set from a layout
         * Allocates a descriptor set that can be updated and bound to a pipeline
         * Reference: UE5 RHIAllocateDescriptorSet
         * 
         * @param layout Descriptor set layout to allocate from
         * @return Allocated descriptor set, or nullptr on failure
         */
        virtual TSharedPtr<IRHIDescriptorSet> allocateDescriptorSet(
            TSharedPtr<IRHIDescriptorSetLayout> layout) = 0;
        
        // Command list management
        /**
         * Get the immediate command list (for direct execution)
         * 
         * Reference UE5: RHIGetDefaultContext()
         * Use this method to get the immediate command list for rendering.
         * The immediate command list is managed per-frame and automatically submitted.
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
