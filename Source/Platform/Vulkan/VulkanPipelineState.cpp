#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanUtils.h"
#include "Core/Log.h"

#include <mutex>
#include <functional>

namespace MonsterRender::RHI::Vulkan
{

    VulkanPipelineState::VulkanPipelineState(VulkanDevice *device, const PipelineStateDesc &desc)
        : IRHIPipelineState(desc), m_device(device), m_isValid(false), m_pipeline(VK_NULL_HANDLE), m_pipelineLayout(VK_NULL_HANDLE), m_renderPass(VK_NULL_HANDLE)
    {

        MR_LOG_INFO("Creating Vulkan pipeline state: " + desc.debugName);
    }

    VulkanPipelineState::~VulkanPipelineState()
    {
        MR_LOG_INFO("Destroying Vulkan pipeline state: " + m_debugName);
        destroyVulkanObjects();
    }

    bool VulkanPipelineState::initialize()
    {
        if (m_isValid)
        {
            return true;
        }

        MR_LOG_INFO("Initializing Vulkan pipeline state: " + m_debugName);

        // Check if we can load from cache first
        if (loadFromCache())
        {
            MR_LOG_INFO("Pipeline state loaded from cache");
            return true;
        }

        // Create shader modules
        if (!createShaderModules())
        {
            MR_LOG_ERROR("Failed to create shader modules");
            return false;
        }

        // Reflect shaders for automatic resource binding
        if (!reflectShaders())
        {
            MR_LOG_WARNING("Shader reflection failed, continuing without reflection data");
        }

        // Create pipeline layout
        if (!createPipelineLayout())
        {
            MR_LOG_ERROR("Failed to create pipeline layout");
            return false;
        }

        // Determine which render pass to use
        // Create custom render pass if:
        // 1. Depth-only pipeline (no color attachments)
        // 2. Custom sample count specified that differs from device default
        bool needsCustomRenderPass = 
            (m_desc.renderTargetFormats.empty() && m_desc.depthStencilFormat != EPixelFormat::Unknown) ||
            (m_desc.sampleCount > 0 && m_desc.sampleCount != static_cast<uint32>(m_device->getMSAASampleCount()));
        
        if (needsCustomRenderPass)
        {
            // Create custom render pass with specified sample count
            MR_LOG_INFO("Creating custom render pass for pipeline: " + m_desc.debugName + 
                       " (sampleCount=" + std::to_string(m_desc.sampleCount > 0 ? m_desc.sampleCount : 1) + ")");
            if (!createRenderPass())
            {
                MR_LOG_ERROR("Failed to create custom render pass");
                return false;
            }
        }
        else
        {
            // Use device's main render pass for swapchain rendering
            // This is the standard render pass with finalLayout=PRESENT_SRC_KHR
            m_renderPass = m_device->getRenderPass();
            if (m_renderPass == VK_NULL_HANDLE)
            {
                MR_LOG_ERROR("Device render pass is null");
                return false;
            }
            MR_LOG_DEBUG("Using device render pass for pipeline");
        }

        // Create graphics pipeline
        if (!createGraphicsPipeline())
        {
            MR_LOG_ERROR("Failed to create graphics pipeline");
            return false;
        }

        // Save to cache for future reuse
        saveToCache();

        m_isValid = true;
        MR_LOG_INFO("Vulkan pipeline state initialized successfully");
        return true;
    }

    const ShaderReflectionData *VulkanPipelineState::getShaderReflection(EShaderStage stage) const
    {
        for (const auto &reflection : m_reflectionData)
        {
            if (reflection.stage == stage)
            {
                return &reflection;
            }
        }
        return nullptr;
    }

    uint32 VulkanPipelineState::getSize() const
    {
        // Estimate pipeline size based on shader modules and state
        uint32 size = 0;
        size += static_cast<uint32>(m_shaderModules.size()) * sizeof(VkShaderModule);
        size += static_cast<uint32>(m_reflectionData.size()) * sizeof(ShaderReflectionData);
        size += sizeof(VkPipeline) + sizeof(VkPipelineLayout) + sizeof(VkRenderPass);
        return size;
    }

    bool VulkanPipelineState::createShaderModules()
    {
        const auto &functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();

        m_shaderModules.clear();
        m_shaderStages.clear();

        // Create vertex shader module
        if (m_desc.vertexShader)
        {
            auto *vulkanVertexShader = static_cast<VulkanVertexShader *>(m_desc.vertexShader.get());
            VkShaderModule vertexModule = vulkanVertexShader->getShaderModule();

            if (vertexModule != VK_NULL_HANDLE)
            {
                m_shaderModules.push_back(vertexModule);

                VkPipelineShaderStageCreateInfo stageInfo{};
                stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
                stageInfo.module = vertexModule;
                stageInfo.pName = "main";
                m_shaderStages.push_back(stageInfo);
            }
        }

        // Create pixel shader module
        if (m_desc.pixelShader)
        {
            auto *vulkanPixelShader = static_cast<VulkanPixelShader *>(m_desc.pixelShader.get());
            VkShaderModule pixelModule = vulkanPixelShader->getShaderModule();

            if (pixelModule != VK_NULL_HANDLE)
            {
                m_shaderModules.push_back(pixelModule);

                VkPipelineShaderStageCreateInfo stageInfo{};
                stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stageInfo.module = pixelModule;
                stageInfo.pName = "main";
                m_shaderStages.push_back(stageInfo);
            }
        }

        if (m_shaderStages.empty())
        {
            MR_LOG_ERROR("No valid shader stages found");
            return false;
        }

        MR_LOG_INFO("Created " + std::to_string(m_shaderStages.size()) + " shader stages:");
        for (size_t i = 0; i < m_shaderStages.size(); ++i) {
            MR_LOG_INFO("  Stage[" + std::to_string(i) + "]: type=" + 
                       std::to_string(m_shaderStages[i].stage) + ", module=" +
                       std::to_string(reinterpret_cast<uint64>(m_shaderStages[i].module)));
        }
        return true;
    }

    bool VulkanPipelineState::createPipelineLayout()
    {
        const auto &functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();

        // Aggregate descriptor bindings from all shader stages
        // Maps (set, binding) -> VkDescriptorSetLayoutBinding
        // Use std::map and std::vector for Vulkan API compatibility
        std::map<uint32, std::vector<VkDescriptorSetLayoutBinding>> setBindings;

        // Collect descriptor bindings from shaders using extended binding info
        // Use std::map to organize bindings by set index
        if (m_desc.vertexShader)
        {
            auto *vulkanVS = static_cast<VulkanVertexShader *>(m_desc.vertexShader.get());
            const auto &extBindings = vulkanVS->getExtendedDescriptorBindings();
            MR_LOG(LogRHI, Log, "createPipelineLayout: Vertex shader has %d descriptor bindings", static_cast<int32>(extBindings.size()));
            for (const auto &extBinding : extBindings)
            {
                uint32 setIndex = extBinding.set;
                const auto &binding = extBinding.layoutBinding;
                MR_LOG_DEBUG("  - VS set=" + std::to_string(setIndex) + 
                             " binding=" + std::to_string(binding.binding) +
                             " type=" + std::to_string(binding.descriptorType));
                
                // Merge with existing bindings in the same set, combining stage flags if binding already exists
                bool found = false;
                for (auto &existing : setBindings[setIndex])
                {
                    if (existing.binding == binding.binding)
                    {
                        existing.stageFlags |= binding.stageFlags;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    setBindings[setIndex].push_back(binding);
                }
            }
        }

        // Merge pixel shader bindings
        if (m_desc.pixelShader)
        {
            auto *vulkanPS = static_cast<VulkanPixelShader *>(m_desc.pixelShader.get());
            const auto &extBindings = vulkanPS->getExtendedDescriptorBindings();
            MR_LOG_DEBUG("createPipelineLayout: Pixel shader has " + std::to_string(extBindings.size()) + " descriptor bindings");
            for (const auto &extBinding : extBindings)
            {
                uint32 setIndex = extBinding.set;
                const auto &binding = extBinding.layoutBinding;
                MR_LOG_DEBUG("  - PS set=" + std::to_string(setIndex) + 
                             " binding=" + std::to_string(binding.binding) +
                             " type=" + std::to_string(binding.descriptorType));
                
                // Merge with existing bindings in the same set, combining stage flags if binding already exists
                bool found = false;
                for (auto &existing : setBindings[setIndex])
                {
                    if (existing.binding == binding.binding)
                    {
                        existing.stageFlags |= binding.stageFlags;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    setBindings[setIndex].push_back(binding);
                }
            }
        }

        // Fallback: If no bindings were found from reflection, create default layout
        // This handles cases where SPIR-V reflection fails or shaders don't have explicit bindings
        if (setBindings.empty() || setBindings[0].empty())
        {
            MR_LOG_WARNING("createPipelineLayout: No descriptor bindings from reflection, creating default layout");

            // Create default bindings for typical rendering setup:
            // binding 0: Uniform buffer (MVP matrices) - Vertex stage
            // binding 1: Combined image sampler (texture1) - Fragment stage
            // binding 2: Combined image sampler (texture2) - Fragment stage

            VkDescriptorSetLayoutBinding uboBinding{};
            uboBinding.binding = 0;
            uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboBinding.descriptorCount = 1;
            uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            uboBinding.pImmutableSamplers = nullptr;
            setBindings[0].push_back(uboBinding);

            VkDescriptorSetLayoutBinding sampler1Binding{};
            sampler1Binding.binding = 1;
            sampler1Binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sampler1Binding.descriptorCount = 1;
            sampler1Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            sampler1Binding.pImmutableSamplers = nullptr;
            setBindings[0].push_back(sampler1Binding);

            VkDescriptorSetLayoutBinding sampler2Binding{};
            sampler2Binding.binding = 2;
            sampler2Binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sampler2Binding.descriptorCount = 1;
            sampler2Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            sampler2Binding.pImmutableSamplers = nullptr;
            setBindings[0].push_back(sampler2Binding);

            MR_LOG_DEBUG("createPipelineLayout: Created default layout with 3 bindings (UBO + 2 samplers)");
        }

        // Create VkDescriptorSetLayouts for each set
        // Use std::vector for Vulkan API compatibility
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        for (const auto &[setIndex, bindings] : setBindings)
        {
            if (bindings.empty())
                continue;

            MR_LOG_DEBUG("createPipelineLayout: Creating descriptor set layout for set " + 
                       std::to_string(setIndex) + " with " + std::to_string(bindings.size()) + " bindings");

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
            VkResult result = functions.vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout);
            if (result != VK_SUCCESS)
            {
                MR_LOG_ERROR("Failed to create descriptor set layout for set " + std::to_string(setIndex) + ": " + std::to_string(result));
                // Clean up previously created layouts
                for (auto layout : descriptorSetLayouts)
                {
                    functions.vkDestroyDescriptorSetLayout(device, layout, nullptr);
                }
                return false;
            }

            descriptorSetLayouts.push_back(setLayout);
            MR_LOG_DEBUG("Created descriptor set layout for set " + std::to_string(setIndex) + " with " + std::to_string(bindings.size()) + " bindings");
        }

        // Store descriptor set layouts for later cleanup
        m_descriptorSetLayouts = descriptorSetLayouts;

        // Create pipeline layout with descriptor set layouts
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32>(descriptorSetLayouts.size());
        layoutInfo.pSetLayouts = descriptorSetLayouts.empty() ? nullptr : descriptorSetLayouts.data();
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        VkResult result = functions.vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout);
        if (result != VK_SUCCESS)
        {
            MR_LOG_ERROR("Failed to create pipeline layout: " + std::to_string(result));
            // Clean up descriptor set layouts
            for (auto layout : m_descriptorSetLayouts)
            {
                functions.vkDestroyDescriptorSetLayout(device, layout, nullptr);
            }
            m_descriptorSetLayouts.clear();
            return false;
        }

        MR_LOG_DEBUG("Pipeline layout created successfully with " + std::to_string(descriptorSetLayouts.size()) + " descriptor sets");
        return true;
    }

    bool VulkanPipelineState::createRenderPass()
    {
        const auto &functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();

        // Create render pass based on render target formats
        // Use std::vector for Vulkan API compatibility
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> colorReferences;
        VkAttachmentReference depthReference{};
        bool hasDepth = false;

        // Get sample count: use desc sampleCount if specified, otherwise device default
        VkSampleCountFlagBits sampleCount = (m_desc.sampleCount > 0) 
            ? static_cast<VkSampleCountFlagBits>(m_desc.sampleCount)
            : m_device->getMSAASampleCount();
        
        // Color attachments
        for (uint32 i = 0; i < m_desc.renderTargetFormats.size(); ++i)
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format = VulkanUtils::getRHIFormatToVulkan(m_desc.renderTargetFormats[i]);
            colorAttachment.samples = sampleCount;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            attachments.push_back(colorAttachment);

            VkAttachmentReference colorRef{};
            colorRef.attachment = i;
            colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorReferences.push_back(colorRef);
        }

        // Depth attachment
        if (m_desc.depthStencilFormat != EPixelFormat::Unknown)
        {
            VkAttachmentDescription depthAttachment{};
            depthAttachment.format = VulkanUtils::getRHIFormatToVulkan(m_desc.depthStencilFormat);
            depthAttachment.samples = sampleCount;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            // Store depth for shadow mapping (depth-only passes need to preserve depth)
            depthAttachment.storeOp = (m_desc.renderTargetFormats.empty()) ?
                VK_ATTACHMENT_STORE_OP_STORE :       // Depth-only pass (e.g. shadow map)
                VK_ATTACHMENT_STORE_OP_DONT_CARE;    // Regular depth buffer
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            attachments.push_back(depthAttachment);

            depthReference.attachment = static_cast<uint32>(attachments.size() - 1);
            depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            hasDepth = true;
        }

        // Subpass
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32>(colorReferences.size());
        subpass.pColorAttachments = colorReferences.data();
        if (hasDepth)
        {
            subpass.pDepthStencilAttachment = &depthReference;
        }

        // Subpass dependencies - configure based on attachments
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        
        // Set stage masks based on what attachments we have
        bool hasColor = !colorReferences.empty();
        if (hasColor && hasDepth) {
            // Both color and depth
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | 
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        } else if (hasDepth) {
            // Depth only (shadow map)
            dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | 
                                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | 
                                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        } else {
            // Color only
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }

        // Create render pass
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        VkResult result = functions.vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_renderPass);
        if (result != VK_SUCCESS)
        {
            MR_LOG_ERROR("Failed to create render pass: " + std::to_string(result));
            return false;
        }

        MR_LOG_DEBUG("Render pass created with " + std::to_string(attachments.size()) + " attachments");
        return true;
    }

    bool VulkanPipelineState::createGraphicsPipeline()
    {
        const auto &functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();

        MR_LOG_DEBUG("Creating graphics pipeline: " + m_debugName);

        // Validate prerequisites
        if (m_shaderStages.empty())
        {
            MR_LOG_ERROR("No shader stages available for pipeline creation");
            return false;
        }

        if (m_pipelineLayout == VK_NULL_HANDLE)
        {
            MR_LOG_ERROR("Pipeline layout is null - cannot create pipeline");
            return false;
        }

        if (m_renderPass == VK_NULL_HANDLE)
        {
            MR_LOG_ERROR("Render pass is null - cannot create pipeline");
            return false;
        }

        MR_LOG_DEBUG("Pipeline has " + std::to_string(m_shaderStages.size()) + " shader stage(s)");

        // Vertex input state - use std::vector for Vulkan API compatibility
        VkVertexInputBindingDescription bindingDescription = createVertexInputBinding();
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions = createVertexInputAttributes();

        MR_LOG_INFO("Vertex input: " + std::to_string(attributeDescriptions.size()) +
                     " attribute(s), stride = " + std::to_string(bindingDescription.stride));
        for (const auto& attr : attributeDescriptions) {
            MR_LOG_DEBUG("  Attr loc=" + std::to_string(attr.location) + 
                       ", format=" + std::to_string(attr.format) +
                       ", offset=" + std::to_string(attr.offset));
        }

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        // Input assembly state
        VkPipelineInputAssemblyStateCreateInfo inputAssembly = createInputAssemblyState();

        // Viewport state
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = 800.0f; // TODO: Get from render target
        viewport.height = 600.0f;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {800, 600}; // TODO: Get from render target

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        // Rasterization state
        VkPipelineRasterizationStateCreateInfo rasterizer = createRasterizationState();
        MR_LOG_INFO("Rasterizer: cullMode=" + std::to_string(rasterizer.cullMode) +
                   ", frontFace=" + std::to_string(rasterizer.frontFace) +
                   ", polygonMode=" + std::to_string(rasterizer.polygonMode));

        // Multisample state
        VkPipelineMultisampleStateCreateInfo multisampling = createMultisampleState();

        // Color blend state - use std::vector for Vulkan API compatibility
        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments = createColorBlendAttachments();
        VkPipelineColorBlendStateCreateInfo colorBlending = createColorBlendState();
        colorBlending.attachmentCount = static_cast<uint32>(colorBlendAttachments.size());
        colorBlending.pAttachments = colorBlendAttachments.data();

        MR_LOG_DEBUG("Color blend: " + std::to_string(colorBlendAttachments.size()) + " attachment(s)");

        // Depth stencil state
        VkPipelineDepthStencilStateCreateInfo depthStencil = createDepthStencilState();
        MR_LOG_INFO("DepthStencil: depthTestEnable=" + std::to_string(depthStencil.depthTestEnable) +
                   ", depthWriteEnable=" + std::to_string(depthStencil.depthWriteEnable) +
                   ", depthCompareOp=" + std::to_string(depthStencil.depthCompareOp));

        // Dynamic state - use std::vector for Vulkan API compatibility
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // Create graphics pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32>(m_shaderStages.size());
        pipelineInfo.pStages = m_shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = m_renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;

        MR_LOG_INFO("Creating pipeline with renderPass=" + std::to_string(reinterpret_cast<uint64>(m_renderPass)) +
                   ", pipelineLayout=" + std::to_string(reinterpret_cast<uint64>(m_pipelineLayout)));
        VkResult result = functions.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
        if (result != VK_SUCCESS)
        {
            MR_LOG_ERROR("Failed to create graphics pipeline: VkResult = " + std::to_string(result));
            MR_LOG_ERROR("Pipeline name: " + m_debugName);
            return false;
        }

        if (m_pipeline == VK_NULL_HANDLE)
        {
            MR_LOG_ERROR("vkCreateGraphicsPipelines succeeded but returned null handle");
            return false;
        }

        MR_LOG_INFO("Graphics pipeline created successfully: " + m_debugName +
                    " (handle: " + std::to_string(reinterpret_cast<uint64>(m_pipeline)) + ")");
        return true;
    }

    void VulkanPipelineState::destroyVulkanObjects()
    {
        const auto &functions = VulkanAPI::getFunctions();
        VkDevice device = m_device ? m_device->getLogicalDevice() : VK_NULL_HANDLE;

        if (device != VK_NULL_HANDLE)
        {
            if (m_pipeline != VK_NULL_HANDLE)
            {
                functions.vkDestroyPipeline(device, m_pipeline, nullptr);
                m_pipeline = VK_NULL_HANDLE;
            }

            if (m_pipelineLayout != VK_NULL_HANDLE)
            {
                functions.vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
                m_pipelineLayout = VK_NULL_HANDLE;
            }

            // Cleanup descriptor set layouts
            for (auto layout : m_descriptorSetLayouts)
            {
                if (layout != VK_NULL_HANDLE)
                {
                    functions.vkDestroyDescriptorSetLayout(device, layout, nullptr);
                }
            }
            m_descriptorSetLayouts.clear();

            // Note: Do NOT destroy render pass - it's owned by VulkanDevice
            // Pipeline only holds a reference to device's render pass
            m_renderPass = VK_NULL_HANDLE;
        }

        m_isValid = false;
    }

    bool VulkanPipelineState::reflectShaders()
    {
        m_reflectionData.clear();

        // Reflect vertex shader
        if (m_desc.vertexShader)
        {
            auto *vulkanVertexShader = static_cast<VulkanVertexShader *>(m_desc.vertexShader.get());
            ShaderReflectionData vertexReflection = reflectShader(vulkanVertexShader);
            m_reflectionData.push_back(vertexReflection);
        }

        // Reflect pixel shader
        if (m_desc.pixelShader)
        {
            auto *vulkanPixelShader = static_cast<VulkanPixelShader *>(m_desc.pixelShader.get());
            ShaderReflectionData pixelReflection = reflectShader(vulkanPixelShader);
            m_reflectionData.push_back(pixelReflection);
        }

        MR_LOG_DEBUG("Reflected " + std::to_string(m_reflectionData.size()) + " shaders");
        return true;
    }

    ShaderReflectionData VulkanPipelineState::reflectShader(const VulkanShader *shader)
    {
        ShaderReflectionData reflection;
        reflection.stage = shader->getStage();
        reflection.entryPoint = "main"; // Default entry point

        // TODO: Implement actual shader reflection using SPIR-V reflection
        // For now, provide basic reflection data
        if (reflection.stage == EShaderStage::Vertex)
        {
            reflection.vertexAttributes = {"Position", "Color", "TexCoord"};
            reflection.outputVariables = {"gl_Position", "outColor", "outTexCoord"};
        }
        else if (reflection.stage == EShaderStage::Fragment)
        {
            reflection.inputVariables = {"inColor", "inTexCoord"};
            reflection.outputVariables = {"outColor"};
        }

        return reflection;
    }

    uint64 VulkanPipelineState::calculatePipelineHash() const
    {
        // Simple hash based on pipeline state description
        // TODO: Implement more sophisticated hashing
        uint64 hash = 0;

        // Hash shader pointers
        if (m_desc.vertexShader)
        {
            hash ^= std::hash<void *>{}(m_desc.vertexShader.get());
        }
        if (m_desc.pixelShader)
        {
            hash ^= std::hash<void *>{}(m_desc.pixelShader.get());
        }

        // Hash primitive topology
        hash ^= std::hash<uint32>{}(static_cast<uint32>(m_desc.primitiveTopology));

        // Hash render target formats
        for (auto format : m_desc.renderTargetFormats)
        {
            hash ^= std::hash<uint32>{}(static_cast<uint32>(format));
        }

        return hash;
    }

    bool VulkanPipelineState::loadFromCache()
    {
        // TODO: Implement pipeline cache loading
        // For now, always create new pipeline
        return false;
    }

    void VulkanPipelineState::saveToCache()
    {
        // TODO: Implement pipeline cache saving
        // Update cache entry
        m_cacheEntry = PipelineCacheEntry(m_pipeline, m_pipelineLayout, m_renderPass, calculatePipelineHash());
    }

    // Helper to convert RHI vertex format to Vulkan format
    static VkFormat getVulkanVertexFormat(EVertexFormat format)
    {
        switch (format)
        {
        case EVertexFormat::Float1:
            return VK_FORMAT_R32_SFLOAT;
        case EVertexFormat::Float2:
            return VK_FORMAT_R32G32_SFLOAT;
        case EVertexFormat::Float3:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case EVertexFormat::Float4:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case EVertexFormat::Int1:
            return VK_FORMAT_R32_SINT;
        case EVertexFormat::Int2:
            return VK_FORMAT_R32G32_SINT;
        case EVertexFormat::Int3:
            return VK_FORMAT_R32G32B32_SINT;
        case EVertexFormat::Int4:
            return VK_FORMAT_R32G32B32A32_SINT;
        case EVertexFormat::UInt1:
            return VK_FORMAT_R32_UINT;
        case EVertexFormat::UInt2:
            return VK_FORMAT_R32G32_UINT;
        case EVertexFormat::UInt3:
            return VK_FORMAT_R32G32B32_UINT;
        case EVertexFormat::UInt4:
            return VK_FORMAT_R32G32B32A32_UINT;
        default:
            return VK_FORMAT_R32G32B32_SFLOAT;
        }
    }

    // Helper methods for creating Vulkan pipeline states
    VkVertexInputBindingDescription VulkanPipelineState::createVertexInputBinding() const
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // Use custom vertex layout if provided, otherwise use default (Position + Color)
        if (m_desc.vertexLayout.stride > 0)
        {
            bindingDescription.stride = m_desc.vertexLayout.stride;
        }
        else if (!m_desc.vertexLayout.attributes.empty())
        {
            bindingDescription.stride = VertexInputLayout::calculateStride(m_desc.vertexLayout.attributes);
        }
        else
        {
            // Default: Position (vec3) + Color (vec3) = 24 bytes (legacy triangle demo)
            bindingDescription.stride = sizeof(float) * 6;
        }

        return bindingDescription;
    }

    std::vector<VkVertexInputAttributeDescription> VulkanPipelineState::createVertexInputAttributes() const
    {
        std::vector<VkVertexInputAttributeDescription> attributes;

        // Use custom vertex layout if provided
        if (!m_desc.vertexLayout.attributes.empty())
        {
            MR_LOG_INFO("createVertexInputAttributes: Using custom layout with " + 
                       std::to_string(m_desc.vertexLayout.attributes.size()) + " attributes, stride=" +
                       std::to_string(m_desc.vertexLayout.stride));
            for (const auto &attr : m_desc.vertexLayout.attributes)
            {
                VkVertexInputAttributeDescription vkAttr{};
                vkAttr.binding = 0;
                vkAttr.location = attr.location;
                vkAttr.format = getVulkanVertexFormat(attr.format);
                vkAttr.offset = attr.offset;
                attributes.push_back(vkAttr);
                MR_LOG_INFO("  Attr[" + std::to_string(attr.location) + "]: format=" + 
                           std::to_string(vkAttr.format) + ", offset=" + std::to_string(attr.offset));
            }
            return attributes;
        }

        // Default: Position (vec3) + Color (vec3) for legacy triangle demo
        VkVertexInputAttributeDescription posAttribute{};
        posAttribute.binding = 0;
        posAttribute.location = 0;
        posAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
        posAttribute.offset = 0;
        attributes.push_back(posAttribute);

        VkVertexInputAttributeDescription colorAttribute{};
        colorAttribute.binding = 0;
        colorAttribute.location = 1;
        colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
        colorAttribute.offset = sizeof(float) * 3;
        attributes.push_back(colorAttribute);

        return attributes;
    }

    VkPipelineInputAssemblyStateCreateInfo VulkanPipelineState::createInputAssemblyState() const
    {
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VulkanUtils::getPrimitiveTopology(m_desc.primitiveTopology);
        inputAssembly.primitiveRestartEnable = VK_FALSE;
        return inputAssembly;
    }

    VkPipelineRasterizationStateCreateInfo VulkanPipelineState::createRasterizationState() const
    {
        // UE5 Pattern: Vulkan rasterizer state with unified front-face convention
        // Reference: UE5 VulkanState.h - FVulkanRasterizerState::ResetCreateInfo()
        // 
        // With Y-flip applied in viewport (negative height), the winding order
        // appears reversed. UE5 uses VK_FRONT_FACE_CLOCKWISE as default.
        // When frontCounterClockwise=false (engine default), use CLOCKWISE.
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = m_desc.rasterizerState.fillMode == EFillMode::Wireframe 
                                 ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        
        // Cull mode conversion
        rasterizer.cullMode = m_desc.rasterizerState.cullMode == ECullMode::None 
                              ? VK_CULL_MODE_NONE 
                              : (m_desc.rasterizerState.cullMode == ECullMode::Front 
                                 ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
        
        // Front face: UE5 default is CLOCKWISE (with Y-flip in viewport)
        rasterizer.frontFace = m_desc.rasterizerState.frontCounterClockwise 
                               ? VK_FRONT_FACE_COUNTER_CLOCKWISE 
                               : VK_FRONT_FACE_CLOCKWISE;
        
        rasterizer.depthBiasEnable = VK_FALSE;
        return rasterizer;
    }

    VkPipelineColorBlendStateCreateInfo VulkanPipelineState::createColorBlendState() const
    {
        // Note: This function should be called within createGraphicsPipeline where the attachments array is managed
        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 0;    // Will be set by caller
        colorBlending.pAttachments = nullptr; // Will be set by caller
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;
        return colorBlending;
    }

    VkPipelineDepthStencilStateCreateInfo VulkanPipelineState::createDepthStencilState() const
    {
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = m_desc.depthStencilState.depthEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = m_desc.depthStencilState.depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS; // TODO: Convert from EComparisonFunc
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.0f;
        depthStencil.maxDepthBounds = 1.0f;
        depthStencil.stencilTestEnable = m_desc.depthStencilState.stencilEnable ? VK_TRUE : VK_FALSE;
        depthStencil.front = {};
        depthStencil.back = {};
        return depthStencil;
    }

    VkPipelineMultisampleStateCreateInfo VulkanPipelineState::createMultisampleState() const
    {
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        // Use desc sampleCount if specified (> 0), otherwise use device default
        if (m_desc.sampleCount > 0) {
            multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(m_desc.sampleCount);
        } else {
            multisampling.rasterizationSamples = m_device->getMSAASampleCount();
        }
        multisampling.minSampleShading = 1.0f;
        multisampling.pSampleMask = nullptr;
        multisampling.alphaToCoverageEnable = VK_FALSE;
        multisampling.alphaToOneEnable = VK_FALSE;
        return multisampling;
    }

    std::vector<VkPipelineColorBlendAttachmentState> VulkanPipelineState::createColorBlendAttachments() const
    {
        std::vector<VkPipelineColorBlendAttachmentState> attachments;

        // Color blend attachments must match the number of color attachments in RenderPass
        // For depth-only pipelines, renderTargetFormats will be empty, so no color blend attachments needed
        uint32 numAttachments = m_desc.renderTargetFormats.size();
        
        if (numAttachments == 0) {
            // Depth-only pipeline, no color attachments
            return attachments;
        }

        for (uint32 i = 0; i < numAttachments; ++i)
        {
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            attachment.blendEnable = m_desc.blendState.blendEnable ? VK_TRUE : VK_FALSE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            attachments.push_back(attachment);
            
            MR_LOG_INFO("  Attachment[" + std::to_string(i) + "]: colorWriteMask=0x" + 
                       std::to_string(attachment.colorWriteMask) + ", blendEnable=" + 
                       std::to_string(attachment.blendEnable));
        }

        return attachments;
    }

    // VulkanPipelineCache implementation
    VulkanPipelineCache::VulkanPipelineCache(VulkanDevice *device)
        : m_device(device)
    {
        MR_LOG_INFO("Creating Vulkan pipeline cache");
    }

    VulkanPipelineCache::~VulkanPipelineCache()
    {
        MR_LOG_INFO("Destroying Vulkan pipeline cache");
        clear();
    }

    TSharedPtr<VulkanPipelineState> VulkanPipelineCache::getOrCreatePipelineState(const PipelineStateDesc &desc)
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);

        uint64 hash = calculateDescHash(desc);

        // Check if pipeline already exists in cache
        TSharedPtr<VulkanPipelineState>* existingPipeline = m_pipelineCache.Find(hash);
        if (existingPipeline)
        {
            updateStats(true);
            MR_LOG_DEBUG("Pipeline cache hit for: " + desc.debugName);
            return *existingPipeline;
        }

        // Create new pipeline state
        auto pipelineState = MakeShared<VulkanPipelineState>(m_device, desc);
        if (!pipelineState->initialize())
        {
            MR_LOG_ERROR("Failed to initialize pipeline state: " + desc.debugName);
            updateStats(false);
            return nullptr;
        }

        // Add to cache
        m_pipelineCache[hash] = pipelineState;
        updateStats(false);

        MR_LOG_DEBUG("Pipeline cache miss, created new pipeline: " + desc.debugName);
        return pipelineState;
    }

    void VulkanPipelineCache::clear()
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);

        m_pipelineCache.clear();
        m_stats = CacheStats{};

        MR_LOG_INFO("Pipeline cache cleared");
    }

    uint64 VulkanPipelineCache::calculateDescHash(const PipelineStateDesc &desc) const
    {
        // Simple hash implementation
        // TODO: Implement more sophisticated hashing
        uint64 hash = 0;

        if (desc.vertexShader)
        {
            hash ^= std::hash<void *>{}(desc.vertexShader.get());
        }
        if (desc.pixelShader)
        {
            hash ^= std::hash<void *>{}(desc.pixelShader.get());
        }

        hash ^= std::hash<uint32>{}(static_cast<uint32>(desc.primitiveTopology));
        hash ^= std::hash<String>{}(desc.debugName);

        return hash;
    }

    void VulkanPipelineCache::updateStats(bool hit) const
    {
        if (hit)
        {
            m_stats.cacheHits++;
        }
        else
        {
            m_stats.cacheMisses++;
        }
        m_stats.totalPipelines = static_cast<uint32>(m_pipelineCache.size());
    }
}
