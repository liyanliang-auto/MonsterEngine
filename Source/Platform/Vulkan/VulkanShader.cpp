#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {
    
    VulkanShader::VulkanShader(VulkanDevice* device, EShaderStage stage, TSpan<const uint8> bytecode)
        : IRHIShader(stage), m_device(device) {
        
        MR_ASSERT(m_device != nullptr);
        MR_ASSERT(!bytecode.empty());
        
        MR_LOG_DEBUG("Creating Vulkan shader for stage: " + std::to_string(static_cast<uint32>(stage)));
        
        if (!initialize(bytecode)) {
            MR_LOG_ERROR("Failed to create Vulkan shader");
        }
    }
    
    VulkanShader::~VulkanShader() {
        destroy();
    }
    
    bool VulkanShader::initialize(TSpan<const uint8> bytecode) {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getDevice();
        
        // Validate SPIR-V bytecode size (must be multiple of 4)
        if (bytecode.size() % 4 != 0) {
            MR_LOG_ERROR("Invalid SPIR-V bytecode size: " + std::to_string(bytecode.size()));
            return false;
        }
        
        // Create shader module
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = bytecode.size();
        createInfo.pCode = reinterpret_cast<const uint32*>(bytecode.data());
        
        VkResult result = functions.vkCreateShaderModule(device, &createInfo, nullptr, &m_shaderModule);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create Vulkan shader module: " + std::to_string(result));
            return false;
        }
        
        // Setup pipeline stage create info
        m_stageCreateInfo = {};
        m_stageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        m_stageCreateInfo.module = m_shaderModule;
        m_stageCreateInfo.pName = "main"; // Entry point
        
        // Convert stage enum to Vulkan stage flag
        switch (getStage()) {
            case EShaderStage::Vertex:
                m_stageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case EShaderStage::Fragment:
                m_stageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case EShaderStage::Compute:
                m_stageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                break;
            case EShaderStage::Geometry:
                m_stageCreateInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
                break;
            case EShaderStage::TessellationControl:
                m_stageCreateInfo.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                break;
            case EShaderStage::TessellationEvaluation:
                m_stageCreateInfo.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                break;
            default:
                MR_LOG_ERROR("Unsupported shader stage");
                return false;
        }
        
        // Perform shader reflection (basic implementation)
        performReflection(bytecode);
        
        MR_LOG_DEBUG("Successfully created Vulkan shader module");
        return true;
    }
    
    void VulkanShader::destroy() {
        if (m_device && m_shaderModule != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice device = m_device->getDevice();
            
            functions.vkDestroyShaderModule(device, m_shaderModule, nullptr);
            m_shaderModule = VK_NULL_HANDLE;
        }
    }
    
    VkPipelineShaderStageCreateInfo VulkanShader::getPipelineStageCreateInfo() const {
        return m_stageCreateInfo;
    }
    
    void VulkanShader::performReflection(TSpan<const uint8> bytecode) {
        // Basic SPIR-V reflection implementation
        // For a production engine, you would use a library like SPIRV-Reflect
        
        MR_LOG_DEBUG("Performing basic shader reflection...");
        
        // Clear existing reflection data
        m_descriptorBindings.clear();
        m_pushConstantSize = 0;
        
        // For now, we'll just log that reflection was attempted
        // In a real implementation, you would:
        // 1. Parse SPIR-V instructions to find OpDecorate instructions
        // 2. Extract binding points, descriptor sets, push constants
        // 3. Determine input/output variables for vertex shaders
        // 4. Build descriptor set layout information
        
        const uint32* spirvData = reinterpret_cast<const uint32*>(bytecode.data());
        uint32 spirvSize = static_cast<uint32>(bytecode.size() / 4);
        
        if (spirvSize < 5) {
            MR_LOG_WARNING("SPIR-V bytecode too small for valid shader");
            return;
        }
        
        // Check SPIR-V magic number
        if (spirvData[0] != 0x07230203) {
            MR_LOG_WARNING("Invalid SPIR-V magic number");
            return;
        }
        
        MR_LOG_DEBUG("SPIR-V validation passed, shader reflection completed");
        
        // TODO: Implement full SPIR-V reflection
        // - Parse OpDecorate instructions for bindings
        // - Extract uniform buffer bindings  
        // - Extract texture/sampler bindings
        // - Extract push constant ranges
        // - Build descriptor set layout bindings
    }
}
