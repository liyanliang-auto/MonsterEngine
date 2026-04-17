#pragma once

#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "RHI/RHI.h"
#include <vector>
#include <map>
#include <array>

namespace MonsterRender::RHI::Vulkan {
    // Forward declarations
    class VulkanDevice;
    class VulkanPipelineState;
    class FVulkanCmdBuffer;
    struct FVulkanDescriptorSetKey;

    // Uniform buffer binding info
    struct UniformBufferBinding {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize range = 0;
    };

    // Texture binding info
    struct TextureBinding {
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32 mipLevels = 1;
        uint32 arrayLayers = 1;
    };

    /**
     * FDescriptorSetState - State for a single descriptor set
     * Tracks bindings and dirty flag for one descriptor set (Set 0, 1, 2, or 3)
     */
    struct FDescriptorSetState {
        // Buffer bindings: binding index -> buffer info
        std::map<uint32, UniformBufferBinding> buffers;
        
        // Texture bindings: binding index -> texture info
        std::map<uint32, TextureBinding> textures;
        
        // Dirty flag: needs update
        bool dirty = true;
        
        // Cached descriptor set handle
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        
        /**
         * Clear all bindings and reset state
         */
        void clear() {
            buffers.clear();
            textures.clear();
            dirty = true;
            descriptorSet = VK_NULL_HANDLE;
        }
        
        /**
         * Check if this set has any bindings
         */
        bool hasBindings() const {
            return !buffers.empty() || !textures.empty();
        }
    };

    /**
     * FVulkanPendingState - Manages pending state for command buffer (UE5 pattern)
     * Tracks dynamic states that need to be set before draw calls
     * Each command buffer has its own pending state to avoid conflicts
     *
     * Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanPendingState.h
     */

    class FVulkanPendingState {

    public:
        FVulkanPendingState(VulkanDevice* device, FVulkanCmdBuffer* cmdBuffer);
        ~FVulkanPendingState();
        // Non-copyable
        FVulkanPendingState(const FVulkanPendingState&) = delete;
        FVulkanPendingState& operator=(const FVulkanPendingState&) = delete;

        /**
         * Reset pending state for new frame (UE5: Reset())
         * Clears all cached state
         */

        void reset();

        /**
         * Update command buffer reference (when switching buffers in ring buffer)
         */

        void updateCommandBuffer(FVulkanCmdBuffer* cmdBuffer);

        /**
         * Set graphics pipeline state (UE5: SetGraphicsPipeline())
         */

        void setGraphicsPipeline(VulkanPipelineState* pipeline);

        /**
         * Set viewport (UE5: SetViewport())
         */

        void setViewport(const Viewport& viewport);

        /**
         * Set scissor rect (UE5: SetScissor())
         */

        void setScissor(const ScissorRect& scissor);

        /**
         * Set vertex buffers (UE5: SetStreamSource())
         */

        void setVertexBuffer(uint32 binding, VkBuffer buffer, VkDeviceSize offset);

        /**
         * Set index buffer (UE5: SetIndexBuffer())
         */

        void setIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);

        /**
         * Set uniform buffer at (set, binding)
         * @param set Descriptor set index (0-3)
         * @param binding Binding index within set (0-7)
         */

        void setUniformBuffer(uint32 set, uint32 binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);

        /**
         * Bind a texture to (set, binding)
         * @param set Descriptor set index (0-3)
         * @param binding Binding index within set (0-7)
         * @param imageView Vulkan image view
         * @param sampler Vulkan sampler
         * @param image Vulkan image (for layout transitions)
         * @param format Vulkan format (for aspect mask determination)
         * @param mipLevels Number of mip levels for barrier
         * @param arrayLayers Number of array layers for barrier
         */

        void setTexture(uint32 set, uint32 binding, VkImageView imageView, VkSampler sampler,
                       VkImage image = VK_NULL_HANDLE, VkFormat format = VK_FORMAT_UNDEFINED,
                       uint32 mipLevels = 1, uint32 arrayLayers = 1);

        /**
         * Update sampler for texture at (set, binding)
         * @param set Descriptor set index (0-3)
         * @param binding Binding index within set (0-7)
         * @param sampler Vulkan sampler handle
         */

        void setSampler(uint32 set, uint32 binding, VkSampler sampler);

        /**
         * Prepare for draw call - ensure all pending state is applied (UE5: PrepareForDraw())
         * @return true if ready to draw, false if critical state is missing
         */

        bool prepareForDraw();

        /**
         * Execute texture layout transitions before render pass begins
         * Must be called BEFORE vkCmdBeginRenderPass
         */

        void transitionTexturesForShaderRead();

        /**
         * Get current pipeline state
         */

        VulkanPipelineState* getCurrentPipeline() const { return m_currentPipeline; }

        /**
         * Check if currently inside render pass
         */

        bool isInsideRenderPass() const { return m_insideRenderPass; }

        /**
         * Mark render pass state
         */

        void setInsideRenderPass(bool inside) { m_insideRenderPass = inside; }

        /**
         * Get descriptor set state for testing/debugging
         * @param set Descriptor set index (0-3)
         */

        const FDescriptorSetState& getDescriptorSetState(uint32 set) const {
            return m_descriptorSets[set];
        }

        /**
         * Disable descriptor set cache (for PBR rendering with pre-updated descriptor sets)
         */

        void setDescriptorSetCacheEnabled(bool enabled) { m_descriptorSetCacheEnabled = enabled; }

    private:

        /**
         * Update and bind descriptor sets using cache
         * Reference: UE5 FVulkanPendingGfxState descriptor set handling
         * @param CmdBuffer Command buffer to bind descriptors to
         * @param Functions Vulkan function pointers
         * @return Bound descriptor set handle
         */

        VkDescriptorSet updateAndBindDescriptorSets(VkCommandBuffer CmdBuffer,
                                                     const VulkanFunctions& Functions);

        /**
         * Build descriptor set key from current bindings
         * Used for cache lookup
         */

        FVulkanDescriptorSetKey buildDescriptorSetKey() const;

        /**
         * Update descriptor set for specific set index
         * @param set Descriptor set index (0-3)
         * @return Updated descriptor set handle
         */

        VkDescriptorSet updateDescriptorSet(uint32 set);

        /**
         * Bind all dirty descriptor sets
         * @param cmdBuffer Command buffer to bind to
         */

        void bindDescriptorSets(VkCommandBuffer cmdBuffer);

    private:
        VulkanDevice* m_device;
        FVulkanCmdBuffer* m_cmdBuffer;
        // Cached pipeline state
        VulkanPipelineState* m_currentPipeline;
        VulkanPipelineState* m_pendingPipeline;
        // Dynamic states
        bool m_viewportDirty;
        Viewport m_pendingViewport;
        bool m_scissorDirty;
        ScissorRect m_pendingScissor;
        // Vertex/Index buffers
        struct VertexBufferBinding {
            VkBuffer buffer;
            VkDeviceSize offset;
        };

        std::vector<VertexBufferBinding> m_vertexBuffers;
        bool m_vertexBuffersDirty;
        VkBuffer m_indexBuffer;
        VkDeviceSize m_indexBufferOffset;
        VkIndexType m_indexType;
        bool m_indexBufferDirty;
        // Resource bindings for descriptor sets
        // 4 independent descriptor set states (Set 0-3)
        std::array<FDescriptorSetState, RHI::RHILimits::MAX_DESCRIPTOR_SETS> m_descriptorSets;
        bool m_descriptorsDirty = true;
        VkDescriptorSet m_currentDescriptorSet = VK_NULL_HANDLE;
        // Descriptor set cache control
        bool m_descriptorSetCacheEnabled = true;
        // Render pass state
        bool m_insideRenderPass;
    };

    /**
     * FVulkanDescriptorPoolSetContainer - Per-frame descriptor pool (UE5 pattern)
     * Manages descriptor sets allocation for a single frame
     * Resets when frame completes to avoid fragmentation
     *
     * Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanDescriptorSets.h
     */

    class FVulkanDescriptorPoolSetContainer {

    public:
        FVulkanDescriptorPoolSetContainer(VulkanDevice* device);
        ~FVulkanDescriptorPoolSetContainer();
        // Non-copyable
        FVulkanDescriptorPoolSetContainer(const FVulkanDescriptorPoolSetContainer&) = delete;
        FVulkanDescriptorPoolSetContainer& operator=(const FVulkanDescriptorPoolSetContainer&) = delete;

        /**
         * Initialize the descriptor pool
         */

        bool initialize();

        /**
         * Allocate a descriptor set (UE5: AllocateDescriptorSets())
         */

        VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout layout);

        /**
         * Reset the pool for new frame (UE5: Reset())
         * Returns all descriptor sets to the pool
         */

        void reset();

    private:

        /**
         * Create descriptor pool
         */

        bool createDescriptorPool();

    private:
        VulkanDevice* m_device;
        VkDescriptorPool m_descriptorPool;
        // Pool sizes configuration
        struct PoolSizeInfo {
            VkDescriptorType type;
            uint32 count;
        };

        TArray<PoolSizeInfo> m_poolSizes;
        uint32 m_maxSets;
        uint32 m_allocatedSets;
    };

}
