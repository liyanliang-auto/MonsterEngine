#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/IRHIResource.h"

namespace MonsterRender::RHI {
    
    // Forward declarations
    class IRHIBuffer;
    class IRHITexture;
    class IRHISampler;
    
    /**
     * Descriptor type enumeration
     * Defines the type of resource that can be bound to a descriptor
     * Reference: UE5 RHIDefinitions.h - Descriptor types
     */
    enum class EDescriptorType : uint8 {
        UniformBuffer,           // Constant buffer / Uniform buffer
        StorageBuffer,           // Read-write structured buffer
        Texture,                 // Shader resource view / Sampled image
        StorageTexture,          // Unordered access view / Storage image
        Sampler,                 // Sampler state
        CombinedTextureSampler,  // Combined image sampler (Vulkan)
        InputAttachment          // Input attachment (Vulkan)
    };
    
    /**
     * Descriptor set layout binding
     * Describes a single binding point in a descriptor set
     * Reference: UE5 RHIDefinitions.h - FRHIDescriptorSetLayoutBinding
     */
    struct FDescriptorSetLayoutBinding {
        uint32 binding = 0;                           // Binding slot number
        EDescriptorType descriptorType = EDescriptorType::UniformBuffer;
        uint32 descriptorCount = 1;                   // Array size (1 for single descriptor)
        EShaderStage shaderStages = EShaderStage::Vertex | EShaderStage::Fragment;
        
        FDescriptorSetLayoutBinding() = default;
        
        FDescriptorSetLayoutBinding(uint32 inBinding, EDescriptorType inType, 
                                   EShaderStage inStages, uint32 inCount = 1)
            : binding(inBinding)
            , descriptorType(inType)
            , descriptorCount(inCount)
            , shaderStages(inStages)
        {}
    };
    
    /**
     * Descriptor set layout description
     * Defines the layout of a descriptor set (Set 0, Set 1, etc.)
     * Reference: UE5 RHIResources.h - FRHIDescriptorSetLayout
     */
    struct FDescriptorSetLayoutDesc {
        TArray<FDescriptorSetLayoutBinding> bindings;
        uint32 setIndex = 0;                          // Set number (0, 1, 2, ...)
        String debugName;
        
        FDescriptorSetLayoutDesc() = default;
        
        explicit FDescriptorSetLayoutDesc(uint32 inSetIndex, const String& inDebugName = "")
            : setIndex(inSetIndex)
            , debugName(inDebugName)
        {}
    };
    
    /**
     * Push constant range
     * Defines a range of push constants for fast uniform updates
     * Reference: UE5 RHIDefinitions.h - Push constants
     */
    struct FPushConstantRange {
        EShaderStage shaderStages = EShaderStage::Vertex;
        uint32 offset = 0;
        uint32 size = 0;
        
        FPushConstantRange() = default;
        
        FPushConstantRange(EShaderStage inStages, uint32 inOffset, uint32 inSize)
            : shaderStages(inStages)
            , offset(inOffset)
            , size(inSize)
        {}
    };
    
    /**
     * Pipeline layout description
     * Defines the complete layout of all descriptor sets and push constants
     * Reference: UE5 RHIResources.h - FRHIPipelineLayout
     */
    struct FPipelineLayoutDesc {
        TArray<TSharedPtr<class IRHIDescriptorSetLayout>> setLayouts;
        TArray<FPushConstantRange> pushConstantRanges;
        String debugName;
        
        FPipelineLayoutDesc() = default;
        
        explicit FPipelineLayoutDesc(const String& inDebugName)
            : debugName(inDebugName)
        {}
    };
    
    /**
     * Descriptor set layout interface
     * Represents the layout/schema of a descriptor set
     * Reference: UE5 RHIResources.h - FRHIDescriptorSetLayout
     */
    class IRHIDescriptorSetLayout : public IRHIResource {
    public:
        IRHIDescriptorSetLayout() = default;
        virtual ~IRHIDescriptorSetLayout() = default;
        
        /**
         * Get the set index (0, 1, 2, ...)
         */
        virtual uint32 getSetIndex() const = 0;
        
        /**
         * Get the bindings in this layout
         */
        virtual const TArray<FDescriptorSetLayoutBinding>& getBindings() const = 0;
        
        /**
         * Get resource size
         */
        uint32 getSize() const override { return 0; }
        
        /**
         * Get resource usage
         */
        EResourceUsage getUsage() const override { return EResourceUsage::None; }
    };
    
    /**
     * Pipeline layout interface
     * Represents the complete layout of all descriptor sets
     * Reference: UE5 RHIResources.h - FRHIPipelineLayout
     */
    class IRHIPipelineLayout : public IRHIResource {
    public:
        IRHIPipelineLayout() = default;
        virtual ~IRHIPipelineLayout() = default;
        
        /**
         * Get descriptor set layouts
         */
        virtual const TArray<TSharedPtr<IRHIDescriptorSetLayout>>& getSetLayouts() const = 0;
        
        /**
         * Get push constant ranges
         */
        virtual const TArray<FPushConstantRange>& getPushConstantRanges() const = 0;
        
        /**
         * Get resource size
         */
        uint32 getSize() const override { return 0; }
        
        /**
         * Get resource usage
         */
        EResourceUsage getUsage() const override { return EResourceUsage::None; }
    };
    
    /**
     * Descriptor set interface
     * Represents an allocated descriptor set that can be bound to a pipeline
     * Reference: UE5 RHIResources.h - FRHIDescriptorSet
     */
    class IRHIDescriptorSet : public IRHIResource {
    public:
        IRHIDescriptorSet() = default;
        virtual ~IRHIDescriptorSet() = default;
        
        /**
         * Update uniform buffer binding
         * @param binding Binding slot number
         * @param buffer Uniform buffer to bind
         * @param offset Offset in bytes into the buffer
         * @param range Size in bytes (0 = whole buffer)
         */
        virtual void updateUniformBuffer(uint32 binding, TSharedPtr<IRHIBuffer> buffer, 
                                        uint32 offset = 0, uint32 range = 0) = 0;
        
        /**
         * Update texture binding
         * @param binding Binding slot number
         * @param texture Texture to bind
         */
        virtual void updateTexture(uint32 binding, TSharedPtr<IRHITexture> texture) = 0;
        
        /**
         * Update sampler binding
         * @param binding Binding slot number
         * @param sampler Sampler to bind
         */
        virtual void updateSampler(uint32 binding, TSharedPtr<IRHISampler> sampler) = 0;
        
        /**
         * Update combined texture and sampler binding (Vulkan-style)
         * @param binding Binding slot number
         * @param texture Texture to bind
         * @param sampler Sampler to bind
         */
        virtual void updateCombinedTextureSampler(uint32 binding, TSharedPtr<IRHITexture> texture,
                                                 TSharedPtr<IRHISampler> sampler) = 0;
        
        /**
         * Get the layout this descriptor set was created from
         */
        virtual TSharedPtr<IRHIDescriptorSetLayout> getLayout() const = 0;
        
        /**
         * Get resource size
         */
        uint32 getSize() const override { return 0; }
        
        /**
         * Get resource usage
         */
        EResourceUsage getUsage() const override { return EResourceUsage::None; }
    };
}
