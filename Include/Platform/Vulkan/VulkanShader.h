#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIResource.h"
#include "Platform/Vulkan/VulkanRHI.h"

namespace MonsterRender::RHI::Vulkan {
    
    // Forward declarations
    class VulkanDevice;
    
    /**
     * Base class for Vulkan shader implementations
     */
    class VulkanShader : public IRHIShader {
    public:
        VulkanShader(VulkanDevice* device, EShaderStage stage, TSpan<const uint8> bytecode);
        virtual ~VulkanShader();
        
        // Non-copyable, non-movable
        VulkanShader(const VulkanShader&) = delete;
        VulkanShader& operator=(const VulkanShader&) = delete;
        VulkanShader(VulkanShader&&) = delete;
        VulkanShader& operator=(VulkanShader&&) = delete;
        
        // Vulkan-specific interface
        VkShaderModule getShaderModule() const { return m_shaderModule; }
        VkPipelineShaderStageCreateInfo getPipelineStageCreateInfo() const;
        
        // Validation and debugging
        bool isValid() const { return m_shaderModule != VK_NULL_HANDLE; }
        
        // Shader reflection (future enhancement)
        const TArray<VkDescriptorSetLayoutBinding>& getDescriptorBindings() const { return m_descriptorBindings; }
        
    private:
        bool initialize(TSpan<const uint8> bytecode);
        void destroy();
        void performReflection(TSpan<const uint8> bytecode);
        
    private:
        VulkanDevice* m_device;
        VkShaderModule m_shaderModule = VK_NULL_HANDLE;
        VkPipelineShaderStageCreateInfo m_stageCreateInfo{};
        
        // Shader reflection data
        TArray<VkDescriptorSetLayoutBinding> m_descriptorBindings;
        uint32 m_pushConstantSize = 0;
    };
    
    /**
     * Vulkan vertex shader implementation
     */
    class VulkanVertexShader : public VulkanShader, public IRHIVertexShader {
    public:
        VulkanVertexShader(VulkanDevice* device, TSpan<const uint8> bytecode)
            : VulkanShader(device, EShaderStage::Vertex, bytecode) {}
        
        // IRHIResource interface
        uint32 getSize() const override { return VulkanShader::getSize(); }
        EResourceUsage getUsage() const override { return VulkanShader::getUsage(); }
    };
    
    /**
     * Vulkan pixel shader implementation  
     */
    class VulkanPixelShader : public VulkanShader, public IRHIPixelShader {
    public:
        VulkanPixelShader(VulkanDevice* device, TSpan<const uint8> bytecode)
            : VulkanShader(device, EShaderStage::Fragment, bytecode) {}
        
        // IRHIResource interface
        uint32 getSize() const override { return VulkanShader::getSize(); }
        EResourceUsage getUsage() const override { return VulkanShader::getUsage(); }
    };
}