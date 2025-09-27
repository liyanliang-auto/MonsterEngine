#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanUtils.h"
#include "Core/Log.h"

#include <mutex>
#include <functional>

namespace MonsterRender::RHI::Vulkan {
    
    VulkanPipelineState::VulkanPipelineState(VulkanDevice* device, const PipelineStateDesc& desc)
        : IRHIPipelineState(desc)
        , m_device(device)
        , m_isValid(false)
        , m_pipeline(VK_NULL_HANDLE)
        , m_pipelineLayout(VK_NULL_HANDLE)
        , m_renderPass(VK_NULL_HANDLE) {
        
        MR_LOG_INFO("Creating Vulkan pipeline state: " + desc.debugName);
    }
    
    VulkanPipelineState::~VulkanPipelineState() {
        MR_LOG_INFO("Destroying Vulkan pipeline state: " + m_debugName);
        destroyVulkanObjects();
    }
    
    bool VulkanPipelineState::initialize() {
        if (m_isValid) {
            return true;
        }
        
        MR_LOG_INFO("Initializing Vulkan pipeline state: " + m_debugName);
        
        // Check if we can load from cache first
        if (loadFromCache()) {
            MR_LOG_INFO("Pipeline state loaded from cache");
            return true;
        }
        
        // Create shader modules
        if (!createShaderModules()) {
            MR_LOG_ERROR("Failed to create shader modules");
            return false;
        }
        
        // Reflect shaders for automatic resource binding
        if (!reflectShaders()) {
            MR_LOG_WARNING("Shader reflection failed, continuing without reflection data");
        }
        
        // Create pipeline layout
        if (!createPipelineLayout()) {
            MR_LOG_ERROR("Failed to create pipeline layout");
            return false;
        }
        
        // Create render pass
        if (!createRenderPass()) {
            MR_LOG_ERROR("Failed to create render pass");
            return false;
        }
        
        // Create graphics pipeline
        if (!createGraphicsPipeline()) {
            MR_LOG_ERROR("Failed to create graphics pipeline");
            return false;
        }
        
        // Save to cache for future reuse
        saveToCache();
        
        m_isValid = true;
        MR_LOG_INFO("Vulkan pipeline state initialized successfully");
        return true;
    }
    
    const ShaderReflectionData* VulkanPipelineState::getShaderReflection(EShaderStage stage) const {
        for (const auto& reflection : m_reflectionData) {
            if (reflection.stage == stage) {
                return &reflection;
            }
        }
        return nullptr;
    }
    
    uint32 VulkanPipelineState::getSize() const {
        // Estimate pipeline size based on shader modules and state
        uint32 size = 0;
        size += m_shaderModules.size() * sizeof(VkShaderModule);
        size += m_reflectionData.size() * sizeof(ShaderReflectionData);
        size += sizeof(VkPipeline) + sizeof(VkPipelineLayout) + sizeof(VkRenderPass);
        return size;
    }
    
    bool VulkanPipelineState::createShaderModules() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        
        m_shaderModules.clear();
        m_shaderStages.clear();
        
        // Create vertex shader module
        if (m_desc.vertexShader) {
            auto* vulkanVertexShader = static_cast<VulkanVertexShader*>(m_desc.vertexShader.get());
            VkShaderModule vertexModule = vulkanVertexShader->getShaderModule();
            
            if (vertexModule != VK_NULL_HANDLE) {
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
        if (m_desc.pixelShader) {
            auto* vulkanPixelShader = static_cast<VulkanPixelShader*>(m_desc.pixelShader.get());
            VkShaderModule pixelModule = vulkanPixelShader->getShaderModule();
            
            if (pixelModule != VK_NULL_HANDLE) {
                m_shaderModules.push_back(pixelModule);
                
                VkPipelineShaderStageCreateInfo stageInfo{};
                stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stageInfo.module = pixelModule;
                stageInfo.pName = "main";
                m_shaderStages.push_back(stageInfo);
            }
        }
        
        if (m_shaderStages.empty()) {
            MR_LOG_ERROR("No valid shader stages found");
            return false;
        }
        
        MR_LOG_DEBUG("Created " + std::to_string(m_shaderStages.size()) + " shader stages");
        return true;
    }
    
    bool VulkanPipelineState::createPipelineLayout() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        
        // For now, create a basic pipeline layout without descriptor sets
        // TODO: Implement descriptor set layout creation based on shader reflection
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 0;
        layoutInfo.pSetLayouts = nullptr;
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;
        
        VkResult result = functions.vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create pipeline layout: " + std::to_string(result));
            return false;
        }
        
        MR_LOG_DEBUG("Pipeline layout created successfully");
        return true;
    }
    
    bool VulkanPipelineState::createRenderPass() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        
        // Create render pass based on render target formats
        TArray<VkAttachmentDescription> attachments;
        TArray<VkAttachmentReference> colorReferences;
        VkAttachmentReference depthReference{};
        bool hasDepth = false;
        
        // Color attachments
        for (uint32 i = 0; i < m_desc.renderTargetFormats.size(); ++i) {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format = VulkanUtils::getRHIFormatToVulkan(m_desc.renderTargetFormats[i]);
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
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
        if (m_desc.depthStencilFormat != EPixelFormat::Unknown) {
            VkAttachmentDescription depthAttachment{};
            depthAttachment.format = VulkanUtils::getRHIFormatToVulkan(m_desc.depthStencilFormat);
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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
        if (hasDepth) {
            subpass.pDepthStencilAttachment = &depthReference;
        }
        
        // Subpass dependencies
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        
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
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create render pass: " + std::to_string(result));
            return false;
        }
        
        MR_LOG_DEBUG("Render pass created with " + std::to_string(attachments.size()) + " attachments");
        return true;
    }
    
    bool VulkanPipelineState::createGraphicsPipeline() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        
        // Vertex input state
        VkVertexInputBindingDescription bindingDescription = createVertexInputBinding();
        TArray<VkVertexInputAttributeDescription> attributeDescriptions = createVertexInputAttributes();
        
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
        viewport.width = 800.0f;  // TODO: Get from render target
        viewport.height = 600.0f;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {800, 600};  // TODO: Get from render target
        
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;
        
        // Rasterization state
        VkPipelineRasterizationStateCreateInfo rasterizer = createRasterizationState();
        
        // Multisample state
        VkPipelineMultisampleStateCreateInfo multisampling = createMultisampleState();
        
        // Color blend state
        TArray<VkPipelineColorBlendAttachmentState> colorBlendAttachments = createColorBlendAttachments();
        VkPipelineColorBlendStateCreateInfo colorBlending = createColorBlendState();
        colorBlending.attachmentCount = static_cast<uint32>(colorBlendAttachments.size());
        colorBlending.pAttachments = colorBlendAttachments.data();
        
        // Depth stencil state
        VkPipelineDepthStencilStateCreateInfo depthStencil = createDepthStencilState();
        
        // Dynamic state
        TArray<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        
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
        
        VkResult result = functions.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create graphics pipeline: " + std::to_string(result));
            return false;
        }
        
        MR_LOG_DEBUG("Graphics pipeline created successfully");
        return true;
    }
    
    void VulkanPipelineState::destroyVulkanObjects() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device ? m_device->getLogicalDevice() : VK_NULL_HANDLE;
        
        if (device != VK_NULL_HANDLE) {
            if (m_pipeline != VK_NULL_HANDLE) {
                functions.vkDestroyPipeline(device, m_pipeline, nullptr);
                m_pipeline = VK_NULL_HANDLE;
            }
            
            if (m_pipelineLayout != VK_NULL_HANDLE) {
                functions.vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
                m_pipelineLayout = VK_NULL_HANDLE;
            }
            
            if (m_renderPass != VK_NULL_HANDLE) {
                functions.vkDestroyRenderPass(device, m_renderPass, nullptr);
                m_renderPass = VK_NULL_HANDLE;
            }
        }
        
        m_isValid = false;
    }
    
    bool VulkanPipelineState::reflectShaders() {
        m_reflectionData.clear();
        
        // Reflect vertex shader
        if (m_desc.vertexShader) {
            auto* vulkanVertexShader = static_cast<VulkanVertexShader*>(m_desc.vertexShader.get());
            ShaderReflectionData vertexReflection = reflectShader(vulkanVertexShader);
            m_reflectionData.push_back(vertexReflection);
        }
        
        // Reflect pixel shader
        if (m_desc.pixelShader) {
            auto* vulkanPixelShader = static_cast<VulkanPixelShader*>(m_desc.pixelShader.get());
            ShaderReflectionData pixelReflection = reflectShader(vulkanPixelShader);
            m_reflectionData.push_back(pixelReflection);
        }
        
        MR_LOG_DEBUG("Reflected " + std::to_string(m_reflectionData.size()) + " shaders");
        return true;
    }
    
    ShaderReflectionData VulkanPipelineState::reflectShader(const VulkanShader* shader) {
        ShaderReflectionData reflection;
        reflection.stage = shader->getStage();
        reflection.entryPoint = "main";  // Default entry point
        
        // TODO: Implement actual shader reflection using SPIR-V reflection
        // For now, provide basic reflection data
        if (reflection.stage == EShaderStage::Vertex) {
            reflection.vertexAttributes = {"Position", "Color", "TexCoord"};
            reflection.outputVariables = {"gl_Position", "outColor", "outTexCoord"};
        } else if (reflection.stage == EShaderStage::Fragment) {
            reflection.inputVariables = {"inColor", "inTexCoord"};
            reflection.outputVariables = {"outColor"};
        }
        
        return reflection;
    }
    
    uint64 VulkanPipelineState::calculatePipelineHash() const {
        // Simple hash based on pipeline state description
        // TODO: Implement more sophisticated hashing
        uint64 hash = 0;
        
        // Hash shader pointers
        if (m_desc.vertexShader) {
            hash ^= std::hash<void*>{}(m_desc.vertexShader.get());
        }
        if (m_desc.pixelShader) {
            hash ^= std::hash<void*>{}(m_desc.pixelShader.get());
        }
        
        // Hash primitive topology
        hash ^= std::hash<uint32>{}(static_cast<uint32>(m_desc.primitiveTopology));
        
        // Hash render target formats
        for (auto format : m_desc.renderTargetFormats) {
            hash ^= std::hash<uint32>{}(static_cast<uint32>(format));
        }
        
        return hash;
    }
    
    bool VulkanPipelineState::loadFromCache() {
        // TODO: Implement pipeline cache loading
        // For now, always create new pipeline
        return false;
    }
    
    void VulkanPipelineState::saveToCache() {
        // TODO: Implement pipeline cache saving
        // Update cache entry
        m_cacheEntry = PipelineCacheEntry(m_pipeline, m_pipelineLayout, m_renderPass, calculatePipelineHash());
    }
    
    // Helper methods for creating Vulkan pipeline states
    VkVertexInputBindingDescription VulkanPipelineState::createVertexInputBinding() const {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(float) * 6;  // Position (3) + Color (3)
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }
    
    TArray<VkVertexInputAttributeDescription> VulkanPipelineState::createVertexInputAttributes() const {
        TArray<VkVertexInputAttributeDescription> attributes;
        
        // Position attribute
        VkVertexInputAttributeDescription posAttribute{};
        posAttribute.binding = 0;
        posAttribute.location = 0;
        posAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
        posAttribute.offset = 0;
        attributes.push_back(posAttribute);
        
        // Color attribute
        VkVertexInputAttributeDescription colorAttribute{};
        colorAttribute.binding = 0;
        colorAttribute.location = 1;
        colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
        colorAttribute.offset = sizeof(float) * 3;
        attributes.push_back(colorAttribute);
        
        return attributes;
    }
    
    VkPipelineInputAssemblyStateCreateInfo VulkanPipelineState::createInputAssemblyState() const {
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VulkanUtils::getPrimitiveTopology(m_desc.primitiveTopology);
        inputAssembly.primitiveRestartEnable = VK_FALSE;
        return inputAssembly;
    }
    
    VkPipelineRasterizationStateCreateInfo VulkanPipelineState::createRasterizationState() const {
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = m_desc.rasterizerState.fillMode == EFillMode::Wireframe ? 
                                VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = m_desc.rasterizerState.cullMode == ECullMode::None ? 
                             VK_CULL_MODE_NONE : 
                             (m_desc.rasterizerState.cullMode == ECullMode::Front ? 
                              VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
        rasterizer.frontFace = m_desc.rasterizerState.frontCounterClockwise ? 
                              VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        return rasterizer;
    }
    
    VkPipelineColorBlendStateCreateInfo VulkanPipelineState::createColorBlendState() const {
        // Note: This function should be called within createGraphicsPipeline where the attachments array is managed
        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 0; // Will be set by caller
        colorBlending.pAttachments = nullptr; // Will be set by caller
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;
        return colorBlending;
    }
    
    VkPipelineDepthStencilStateCreateInfo VulkanPipelineState::createDepthStencilState() const {
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = m_desc.depthStencilState.depthEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = m_desc.depthStencilState.depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;  // TODO: Convert from EComparisonFunc
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.0f;
        depthStencil.maxDepthBounds = 1.0f;
        depthStencil.stencilTestEnable = m_desc.depthStencilState.stencilEnable ? VK_TRUE : VK_FALSE;
        depthStencil.front = {};
        depthStencil.back = {};
        return depthStencil;
    }
    
    VkPipelineMultisampleStateCreateInfo VulkanPipelineState::createMultisampleState() const {
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f;
        multisampling.pSampleMask = nullptr;
        multisampling.alphaToCoverageEnable = VK_FALSE;
        multisampling.alphaToOneEnable = VK_FALSE;
        return multisampling;
    }
    
    TArray<VkPipelineColorBlendAttachmentState> VulkanPipelineState::createColorBlendAttachments() const {
        TArray<VkPipelineColorBlendAttachmentState> attachments;
        
        for (uint32 i = 0; i < m_desc.renderTargetFormats.size(); ++i) {
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            attachment.blendEnable = m_desc.blendState.blendEnable ? VK_TRUE : VK_FALSE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // TODO: Convert from EBlendFactor
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            attachment.colorBlendOp = VK_BLEND_OP_ADD;  // TODO: Convert from EBlendOp
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            attachments.push_back(attachment);
        }
        
        return attachments;
    }
    
    // VulkanPipelineCache implementation
    VulkanPipelineCache::VulkanPipelineCache(VulkanDevice* device)
        : m_device(device) {
        MR_LOG_INFO("Creating Vulkan pipeline cache");
    }
    
    VulkanPipelineCache::~VulkanPipelineCache() {
        MR_LOG_INFO("Destroying Vulkan pipeline cache");
        clear();
    }
    
    TSharedPtr<VulkanPipelineState> VulkanPipelineCache::getOrCreatePipelineState(const PipelineStateDesc& desc) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        
        uint64 hash = calculateDescHash(desc);
        
        // Check if pipeline already exists in cache
        auto it = m_pipelineCache.find(hash);
        if (it != m_pipelineCache.end()) {
            updateStats(true);
            MR_LOG_DEBUG("Pipeline cache hit for: " + desc.debugName);
            return it->second;
        }
        
        // Create new pipeline state
        auto pipelineState = MakeShared<VulkanPipelineState>(m_device, desc);
        if (!pipelineState->initialize()) {
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
    
    void VulkanPipelineCache::clear() {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        
        m_pipelineCache.clear();
        m_stats = CacheStats{};
        
        MR_LOG_INFO("Pipeline cache cleared");
    }
    
    uint64 VulkanPipelineCache::calculateDescHash(const PipelineStateDesc& desc) const {
        // Simple hash implementation
        // TODO: Implement more sophisticated hashing
        uint64 hash = 0;
        
        if (desc.vertexShader) {
            hash ^= std::hash<void*>{}(desc.vertexShader.get());
        }
        if (desc.pixelShader) {
            hash ^= std::hash<void*>{}(desc.pixelShader.get());
        }
        
        hash ^= std::hash<uint32>{}(static_cast<uint32>(desc.primitiveTopology));
        hash ^= std::hash<String>{}(desc.debugName);
        
        return hash;
    }
    
    void VulkanPipelineCache::updateStats(bool hit) const {
        if (hit) {
            m_stats.cacheHits++;
        } else {
            m_stats.cacheMisses++;
        }
        m_stats.totalPipelines = static_cast<uint32>(m_pipelineCache.size());
    }
}
