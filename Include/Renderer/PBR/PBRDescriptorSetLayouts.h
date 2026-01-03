// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file PBRDescriptorSetLayouts.h
 * @brief PBR descriptor set layout definitions and factory
 * 
 * Defines the descriptor set layouts for PBR rendering:
 * - Set 0: Per-Frame data (View UBO, Light UBO)
 * - Set 1: Per-Material data (Material UBO, Textures)
 * - Set 2: Per-Object data (Object UBO)
 * 
 * Reference:
 * - Filament: DescriptorSetLayout definitions
 * - UE5: FShaderParameterBindings
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "RHI/IRHIDescriptorSet.h"
#include "Renderer/PBR/PBRMaterialTypes.h"

// Forward declarations
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
    class IRHIDescriptorSetLayout;
    class IRHIPipelineLayout;
    class IRHIDescriptorSet;
    class IRHISampler;
}}

namespace MonsterEngine
{
namespace Renderer
{

// ============================================================================
// PBR Descriptor Set Indices
// ============================================================================

/**
 * @enum EPBRDescriptorSet
 * @brief Descriptor set indices for PBR rendering
 */
enum class EPBRDescriptorSet : uint32
{
    PerFrame = 0,       // Set 0: View + Lighting data
    PerMaterial = 1,    // Set 1: Material parameters + Textures
    PerObject = 2,      // Set 2: Object transform
    
    Count = 3
};

/**
 * @enum EPBRPerFrameBinding
 * @brief Binding indices for Set 0 (Per-Frame)
 */
enum class EPBRPerFrameBinding : uint32
{
    ViewUBO = 0,        // View/Camera uniform buffer
    LightUBO = 1,       // Lighting uniform buffer
    
    Count = 2
};

/**
 * @enum EPBRPerMaterialBinding
 * @brief Binding indices for Set 1 (Per-Material)
 */
enum class EPBRPerMaterialBinding : uint32
{
    MaterialUBO = 0,            // Material parameters uniform buffer
    BaseColorTexture = 1,       // Base color texture + sampler
    MetallicRoughnessTexture = 2, // Metallic-roughness texture + sampler
    NormalTexture = 3,          // Normal map texture + sampler
    OcclusionTexture = 4,       // Ambient occlusion texture + sampler
    EmissiveTexture = 5,        // Emissive texture + sampler
    
    Count = 6
};

/**
 * @enum EPBRPerObjectBinding
 * @brief Binding indices for Set 2 (Per-Object)
 */
enum class EPBRPerObjectBinding : uint32
{
    ObjectUBO = 0,      // Object transform uniform buffer
    
    Count = 1
};

// ============================================================================
// FPBRDescriptorSetLayoutFactory
// ============================================================================

/**
 * @class FPBRDescriptorSetLayoutFactory
 * @brief Factory for creating PBR descriptor set layouts
 * 
 * Creates and caches descriptor set layouts for PBR rendering.
 * Supports both Vulkan and OpenGL backends.
 */
class FPBRDescriptorSetLayoutFactory
{
public:
    /**
     * Create Per-Frame descriptor set layout (Set 0)
     * Contains: ViewUBO, LightUBO
     */
    static TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> createPerFrameLayout(
        MonsterRender::RHI::IRHIDevice* device);
    
    /**
     * Create Per-Material descriptor set layout (Set 1)
     * Contains: MaterialUBO, BaseColor, MetallicRoughness, Normal, Occlusion, Emissive textures
     */
    static TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> createPerMaterialLayout(
        MonsterRender::RHI::IRHIDevice* device);
    
    /**
     * Create Per-Object descriptor set layout (Set 2)
     * Contains: ObjectUBO
     */
    static TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> createPerObjectLayout(
        MonsterRender::RHI::IRHIDevice* device);
    
    /**
     * Create complete PBR pipeline layout with all three descriptor sets
     */
    static TSharedPtr<MonsterRender::RHI::IRHIPipelineLayout> createPBRPipelineLayout(
        MonsterRender::RHI::IRHIDevice* device,
        TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> perFrameLayout,
        TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> perMaterialLayout,
        TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> perObjectLayout);
};

// ============================================================================
// FPBRDescriptorSetManager
// ============================================================================

/**
 * @class FPBRDescriptorSetManager
 * @brief Manages descriptor sets for PBR rendering
 * 
 * Handles allocation, updating, and binding of descriptor sets.
 * Provides caching and pooling for efficient descriptor set reuse.
 */
class FPBRDescriptorSetManager
{
public:
    FPBRDescriptorSetManager();
    ~FPBRDescriptorSetManager();
    
    /**
     * Initialize the manager with device and layouts
     */
    bool initialize(MonsterRender::RHI::IRHIDevice* device);
    
    /**
     * Shutdown and release resources
     */
    void shutdown();
    
    /**
     * Get or create Per-Frame descriptor set
     */
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSet> getPerFrameDescriptorSet();
    
    /**
     * Get or create Per-Material descriptor set for a material
     */
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSet> getPerMaterialDescriptorSet(
        const FPBRMaterialParams& params,
        const FPBRMaterialTextures& textures);
    
    /**
     * Get or create Per-Object descriptor set
     */
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSet> getPerObjectDescriptorSet();
    
    /**
     * Get the pipeline layout
     */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineLayout> getPipelineLayout() const { return m_pipelineLayout; }
    
    /**
     * Get descriptor set layouts
     */
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> getPerFrameLayout() const { return m_perFrameLayout; }
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> getPerMaterialLayout() const { return m_perMaterialLayout; }
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> getPerObjectLayout() const { return m_perObjectLayout; }
    
    /**
     * Get default sampler for PBR textures
     */
    TSharedPtr<MonsterRender::RHI::IRHISampler> getDefaultSampler() const { return m_defaultSampler; }
    
    /**
     * Begin new frame - reset per-frame allocations
     */
    void beginFrame(uint32 frameIndex);
    
private:
    MonsterRender::RHI::IRHIDevice* m_device = nullptr;
    
    // Descriptor set layouts
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> m_perFrameLayout;
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> m_perMaterialLayout;
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> m_perObjectLayout;
    
    // Pipeline layout
    TSharedPtr<MonsterRender::RHI::IRHIPipelineLayout> m_pipelineLayout;
    
    // Default sampler for PBR textures
    TSharedPtr<MonsterRender::RHI::IRHISampler> m_defaultSampler;
    
    // Current frame index
    uint32 m_currentFrame = 0;
    
    /**
     * Create default sampler for PBR textures
     */
    bool _createDefaultSampler();
};

} // namespace Renderer
} // namespace MonsterEngine
