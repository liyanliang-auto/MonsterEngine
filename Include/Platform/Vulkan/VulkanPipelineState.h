#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIResource.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Containers/Map.h"

#include <mutex>
#include <vector>
#include <map>

namespace MonsterRender::RHI::Vulkan {

// Use MonsterEngine containers
using MonsterEngine::TArray;
using MonsterEngine::TMap;
    
    // Forward declarations
    class VulkanDevice;
    class VulkanShader;
    
    /**
     * Shader reflection data for automatic resource binding
     * Based on UE5's shader reflection system
     */
    struct ShaderReflectionData {
        // Shader input/output variables
        TArray<String> inputVariables;
        TArray<String> outputVariables;
        
        // Resource bindings (uniform buffers, textures, samplers)
        TArray<String> uniformBuffers;
        TArray<String> textures;
        TArray<String> samplers;
        
        // Vertex input attributes
        TArray<String> vertexAttributes;
        
        // Shader entry point
        String entryPoint;
        
        // Shader stage
        EShaderStage stage;
    };
    
    /**
     * Pipeline cache entry for efficient pipeline state reuse
     * Following UE5's pipeline state caching pattern
     */
    struct PipelineCacheEntry {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        uint64 hash = 0;
        bool isValid = false;
        
        PipelineCacheEntry() = default;
        PipelineCacheEntry(VkPipeline p, VkPipelineLayout l, VkRenderPass rp, uint64 h)
            : pipeline(p), layout(l), renderPass(rp), hash(h), isValid(true) {}
    };
    
    /**
     * Vulkan graphics pipeline state implementation
     * Provides comprehensive pipeline state management with caching and reflection
     * Based on UE5's RHI pipeline state architecture
     */
    class VulkanPipelineState : public IRHIPipelineState {
    public:
        VulkanPipelineState(VulkanDevice* device, const PipelineStateDesc& desc);
        virtual ~VulkanPipelineState();
        
        // Non-copyable, movable
        VulkanPipelineState(const VulkanPipelineState&) = delete;
        VulkanPipelineState& operator=(const VulkanPipelineState&) = delete;
        VulkanPipelineState(VulkanPipelineState&&) = default;
        VulkanPipelineState& operator=(VulkanPipelineState&&) = default;
        
        /**
         * Initialize the pipeline state
         * Creates Vulkan pipeline objects and sets up reflection data
         */
        bool initialize();
        
        /**
         * Get the Vulkan pipeline handle
         */
        VkPipeline getPipeline() const { return m_pipeline; }
        
        /**
         * Get the Vulkan pipeline layout
         */
        VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }
        
        /**
         * Get the Vulkan render pass
         */
        VkRenderPass getRenderPass() const { return m_renderPass; }
        
        /**
         * Get shader reflection data for a specific stage
         */
        const ShaderReflectionData* getShaderReflection(EShaderStage stage) const;
        
        /**
         * Check if the pipeline state is valid and ready for use
         */
        bool isValid() const { return m_isValid; }
        
        /**
         * Get pipeline cache entry for reuse
         */
        const PipelineCacheEntry& getCacheEntry() const { return m_cacheEntry; }
        
        /**
         * Get the size in bytes
         */
        uint32 getSize() const override;
        
        /**
         * Get the resource usage
         */
        EResourceUsage getUsage() const override { return EResourceUsage::None; }
        
        /**
         * Get descriptor set layouts
         */
        const std::vector<VkDescriptorSetLayout>& getDescriptorSetLayouts() const { return m_descriptorSetLayouts; }
        
    private:
        VulkanDevice* m_device;
        bool m_isValid;
        
        // Vulkan objects - use std::vector for Vulkan API compatibility
        VkPipeline m_pipeline;
        VkPipelineLayout m_pipelineLayout;
        VkRenderPass m_renderPass;
        std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
        
        // Shader modules - use std::vector for Vulkan API compatibility
        std::vector<VkShaderModule> m_shaderModules;
        std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;
        
        // Reflection data
        TArray<ShaderReflectionData> m_reflectionData;
        
        // Cache entry for reuse
        PipelineCacheEntry m_cacheEntry;
        
        // Internal methods
        bool createShaderModules();
        bool createPipelineLayout();
        bool createRenderPass();
        bool createGraphicsPipeline();
        void destroyVulkanObjects();
        
        // Shader reflection
        bool reflectShaders();
        ShaderReflectionData reflectShader(const VulkanShader* shader);
        
        // Pipeline cache
        uint64 calculatePipelineHash() const;
        bool loadFromCache();
        void saveToCache();
        
        // Helper methods - use std::vector for Vulkan API compatibility
        VkVertexInputBindingDescription createVertexInputBinding() const;
        std::vector<VkVertexInputAttributeDescription> createVertexInputAttributes() const;
        VkPipelineInputAssemblyStateCreateInfo createInputAssemblyState() const;
        VkPipelineRasterizationStateCreateInfo createRasterizationState() const;
        VkPipelineColorBlendStateCreateInfo createColorBlendState() const;
        VkPipelineDepthStencilStateCreateInfo createDepthStencilState() const;
        VkPipelineMultisampleStateCreateInfo createMultisampleState() const;
        std::vector<VkPipelineColorBlendAttachmentState> createColorBlendAttachments() const;
    };
    
    /**
     * Pipeline state cache manager
     * Manages pipeline state objects for efficient reuse
     * Based on UE5's pipeline state caching system
     */
    class VulkanPipelineCache {
    public:
        VulkanPipelineCache(VulkanDevice* device);
        ~VulkanPipelineCache();
        
        /**
         * Get or create a pipeline state
         */
        TSharedPtr<VulkanPipelineState> getOrCreatePipelineState(const PipelineStateDesc& desc);
        
        /**
         * Clear the cache
         */
        void clear();
        
        /**
         * Get cache statistics
         */
        struct CacheStats {
            uint32 totalPipelines;
            uint32 cacheHits;
            uint32 cacheMisses;
            uint64 totalMemoryUsage;
        };
        CacheStats getStats() const { return m_stats; }
        
    private:
        VulkanDevice* m_device;
        
        // Cache storage
        TMap<uint64, TSharedPtr<VulkanPipelineState>> m_pipelineCache;
        mutable std::mutex m_cacheMutex;
        
        // Statistics
        mutable CacheStats m_stats;
        
        // Helper methods
        uint64 calculateDescHash(const PipelineStateDesc& desc) const;
        void updateStats(bool hit) const;
    };
}
