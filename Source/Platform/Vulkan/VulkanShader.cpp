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
        // Minimal SPIR-V reflection: extract descriptor bindings and push constants by scanning decorations
        m_descriptorBindings.clear();
        m_pushConstantSize = 0;

        const uint32* code = reinterpret_cast<const uint32*>(bytecode.data());
        const uint32 wordCount = static_cast<uint32>(bytecode.size() / 4);
        if (wordCount < 5 || code[0] != 0x07230203) {
            MR_LOG_WARNING("Invalid SPIR-V for reflection");
            return;
        }

        // Very small parser for OpDecorate and push constants (heuristic)
        // SPIR-V instruction stream starts after 5-word header
        uint32 i = 5;
        // Maps from (targetId) -> (binding,set)
        struct BindingInfo { uint32 set = 0; uint32 binding = 0; bool hasSet = false; bool hasBinding = false; };
        TMap<uint32, BindingInfo> idToBinding;

        while (i < wordCount) {
            uint32 word = code[i++];
            uint16 op = static_cast<uint16>(word & 0xFFFF);
            uint16 wc = static_cast<uint16>((word >> 16) & 0xFFFF);
            if (wc == 0) break;
            uint32 start = i;

            if (op == 71 /*OpDecorate*/) {
                if (i + 2 <= start + wc - 1) {
                    uint32 targetId = code[i++];
                    uint32 decoration = code[i++];
                    switch (decoration) {
                        case 33: /* Binding */
                        {
                            uint32 val = (i < start + wc) ? code[i++] : 0;
                            auto& info = idToBinding[targetId];
                            info.binding = val; info.hasBinding = true;
                            break;
                        }
                        case 34: /* DescriptorSet */
                        {
                            uint32 val = (i < start + wc) ? code[i++] : 0;
                            auto& info = idToBinding[targetId];
                            info.set = val; info.hasSet = true;
                            break;
                        }
                        default:
                            // skip extra operands
                            while (i < start + wc) ++i;
                            break;
                    }
                } else {
                    i = start + wc - 1;
                }
            } else if (op == 59 /*OpVariable*/) {
                // Storage class is at operand 2; if Uniform/UniformConstant, check previous decorations
                if (i + 2 <= start + wc - 1) {
                    uint32 resultType = code[i++];
                    uint32 resultId = code[i++];
                    uint32 storageClass = code[i++];
                    (void)resultType;
                    if (storageClass == 2 /*UniformConstant*/ || storageClass == 0 /*Uniform*/) {
                        auto it = idToBinding.find(resultId);
                        if (it != idToBinding.end()) {
                            VkDescriptorSetLayoutBinding b{};
                            b.binding = it->second.binding;
                            b.descriptorCount = 1;
                            b.stageFlags = (getStage() == EShaderStage::Vertex) ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
                            // Heuristic: UniformConstant -> sampled image/sampler; Uniform -> uniform buffer
                            b.descriptorType = (storageClass == 2) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                            m_descriptorBindings.push_back(b);
                        }
                    }
                }
                i = start + wc - 1;
            } else if (op == 71 /*OpDecorate*/ ) {
                // handled above
            } else {
                i = start + wc - 1;
            }
        }

        MR_LOG_DEBUG("Reflection: found " + std::to_string(m_descriptorBindings.size()) + " descriptor bindings");
    }
}
