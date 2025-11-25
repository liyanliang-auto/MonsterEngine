#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHIDefinitions.h"

namespace MonsterRender::RHI {
    
    /**
     * Base class for all RHI resources
     * Provides common functionality for resource management and lifetime tracking
     */
    class IRHIResource {
    public:
        IRHIResource() = default;
        virtual ~IRHIResource() = default;
        
        // Non-copyable, movable
        IRHIResource(const IRHIResource&) = delete;
        IRHIResource& operator=(const IRHIResource&) = delete;
        IRHIResource(IRHIResource&&) = default;
        IRHIResource& operator=(IRHIResource&&) = default;
        
        /**
         * Get the debug name of this resource
         */
        const String& getDebugName() const { return m_debugName; }
        
        /**
         * Set the debug name for this resource
         */
        void setDebugName(const String& name) { m_debugName = name; }
        
        /**
         * Get the size in bytes of this resource
         */
        virtual uint32 getSize() const = 0;
        
        /**
         * Get the resource usage flags
         */
        virtual EResourceUsage getUsage() const = 0;
        
    protected:
        String m_debugName;
    };
    
    /**
     * Base class for GPU buffers
     */
    class IRHIBuffer : public IRHIResource {
    public:
        IRHIBuffer(const BufferDesc& desc) : m_desc(desc) {
            m_debugName = desc.debugName;
        }
        
        virtual ~IRHIBuffer() = default;
        
        /**
         * Map the buffer for CPU access
         * Returns nullptr if the buffer is not CPU accessible
         */
        virtual void* map() = 0;
        
        /**
         * Unmap the buffer
         */
        virtual void unmap() = 0;
        
        /**
         * Get buffer description
         */
        const BufferDesc& getDesc() const { return m_desc; }
        
        /**
         * Get the size in bytes
         */
        uint32 getSize() const override { return m_desc.size; }
        
        /**
         * Get the resource usage
         */
        EResourceUsage getUsage() const override { return m_desc.usage; }
        
    protected:
        BufferDesc m_desc;
    };
    
    /**
     * Base class for GPU textures
     */
    class IRHITexture : public IRHIResource {
    public:
        IRHITexture(const TextureDesc& desc) : m_desc(desc) {
            m_debugName = desc.debugName;
        }
        
        virtual ~IRHITexture() = default;
        
        /**
         * Get texture description
         */
        const TextureDesc& getDesc() const { return m_desc; }
        
        /**
         * Get the size in bytes (estimated)
         */
        uint32 getSize() const override;
        
        /**
         * Get the resource usage
         */
        EResourceUsage getUsage() const override { return m_desc.usage; }
        
        /**
         * Get the width of the texture
         */
        uint32 getWidth() const { return m_desc.width; }
        
        /**
         * Get the height of the texture
         */
        uint32 getHeight() const { return m_desc.height; }
        
        /**
         * Get the depth of the texture
         */
        uint32 getDepth() const { return m_desc.depth; }
        
        /**
         * Get the pixel format
         */
        EPixelFormat getFormat() const { return m_desc.format; }
        
    protected:
        TextureDesc m_desc;
    };
    
    /**
     * Base class for shaders
     */
    class IRHIShader : public IRHIResource {
    public:
        IRHIShader(EShaderStage stage) : m_stage(stage) {}
        virtual ~IRHIShader() = default;
        
        /**
         * Get the shader stage
         */
        EShaderStage getStage() const { return m_stage; }
        
        /**
         * Get the size in bytes
         */
        uint32 getSize() const override { return 0; } // Shader size is implementation-dependent
        
        /**
         * Get the resource usage
         */
        EResourceUsage getUsage() const override { return EResourceUsage::None; }
        
    protected:
        EShaderStage m_stage;
    };
    
    /**
     * Vertex shader interface
     */
    class IRHIVertexShader : public IRHIShader {
    public:
        IRHIVertexShader() : IRHIShader(EShaderStage::Vertex) {}
        virtual ~IRHIVertexShader() = default;
    };
    
    /**
     * Pixel/Fragment shader interface
     */
    class IRHIPixelShader : public IRHIShader {
    public:
        IRHIPixelShader() : IRHIShader(EShaderStage::Fragment) {}
        virtual ~IRHIPixelShader() = default;
    };
    
    /**
     * Pipeline state object interface
     */
    class IRHIPipelineState : public IRHIResource {
    public:
        IRHIPipelineState(const PipelineStateDesc& desc) : m_desc(desc) {
            m_debugName = desc.debugName;
        }
        
        virtual ~IRHIPipelineState() = default;
        
        /**
         * Get pipeline state description
         */
        const PipelineStateDesc& getDesc() const { return m_desc; }
        
        /**
         * Get the size in bytes
         */
        uint32 getSize() const override { return 0; } // Pipeline size is implementation-dependent
        
        /**
         * Get the resource usage
         */
        EResourceUsage getUsage() const override { return EResourceUsage::None; }
        
    protected:
        PipelineStateDesc m_desc;
    };
    
    // Forward declare sampler enums for SamplerDesc
    enum class ESamplerFilter : uint8;
    enum class ESamplerAddressMode : uint8;
    
    /**
     * Sampler descriptor (forward declaration - full definition in RHIResources.h)
     */
    struct SamplerDesc;
    
    /**
     * Sampler state interface
     * Reference UE5: FRHISamplerState
     */
    class IRHISampler : public IRHIResource {
    public:
        IRHISampler() = default;
        virtual ~IRHISampler() = default;
        
        /**
         * Get the size in bytes
         */
        uint32 getSize() const override { return 0; } // Sampler has no GPU memory footprint
        
        /**
         * Get the resource usage
         */
        EResourceUsage getUsage() const override { return EResourceUsage::None; }
    };
}
