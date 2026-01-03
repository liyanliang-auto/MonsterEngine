// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file PBRMaterial.h
 * @brief PBR Material class for physically-based rendering
 * 
 * FPBRMaterial encapsulates all data needed for PBR rendering:
 * - Material parameters (metallic, roughness, etc.)
 * - Texture references
 * - Descriptor set management
 * 
 * Reference:
 * - Filament: MaterialInstance
 * - UE5: UMaterialInstanceDynamic
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Containers/Name.h"
#include "Renderer/PBR/PBRMaterialTypes.h"
#include "Renderer/PBR/PBRUniformBuffers.h"

// Forward declarations
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
    class IRHIBuffer;
    class IRHITexture;
    class IRHIDescriptorSet;
    class IRHISampler;
}}

namespace MonsterEngine
{

// Forward declarations
class FTexture2D;

namespace Renderer
{

// Forward declarations
class FPBRDescriptorSetManager;

// ============================================================================
// FPBRMaterial
// ============================================================================

/**
 * @class FPBRMaterial
 * @brief PBR material for physically-based rendering
 * 
 * Manages material parameters, textures, and GPU resources for PBR rendering.
 * Supports both Vulkan and OpenGL backends through the RHI abstraction.
 */
class FPBRMaterial
{
public:
    FPBRMaterial();
    explicit FPBRMaterial(const FName& name);
    ~FPBRMaterial();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initialize GPU resources for this material
     * @param device RHI device for resource creation
     * @param descriptorManager Descriptor set manager for layout access
     * @return true if initialization succeeded
     */
    bool initialize(MonsterRender::RHI::IRHIDevice* device,
                    FPBRDescriptorSetManager* descriptorManager);
    
    /**
     * Release GPU resources
     */
    void shutdown();
    
    /**
     * Check if material is initialized
     */
    bool isInitialized() const { return m_initialized; }
    
    // ========================================================================
    // Material Parameters
    // ========================================================================
    
    /** Get material name */
    const FName& getName() const { return m_name; }
    
    /** Set material name */
    void setName(const FName& name) { m_name = name; }
    
    /** Get material parameters (read-only, use setters to modify) */
    const FPBRMaterialParams& getParams() const { return m_params; }
    
    /** Set base color factor */
    void setBaseColor(const FVector4f& color);
    
    /** Set base color factor (RGB only, alpha = 1) */
    void setBaseColor(const FVector3f& color);
    
    /** Set metallic factor */
    void setMetallic(float metallic);
    
    /** Set roughness factor */
    void setRoughness(float roughness);
    
    /** Set reflectance factor */
    void setReflectance(float reflectance);
    
    /** Set ambient occlusion factor */
    void setAmbientOcclusion(float ao);
    
    /** Set emissive color and intensity */
    void setEmissive(const FVector3f& color, float intensity = 1.0f);
    
    /** Set clear coat parameters */
    void setClearCoat(float intensity, float roughness = 0.0f);
    
    /** Set alpha cutoff for masked materials */
    void setAlphaCutoff(float cutoff);
    
    /** Set double-sided flag */
    void setDoubleSided(bool doubleSided);
    
    // ========================================================================
    // Textures
    // ========================================================================
    
    /** Get texture references */
    const FPBRMaterialTextures& getTextures() const { return m_textures; }
    
    /** Set base color texture */
    void setBaseColorTexture(FTexture2D* texture);
    
    /** Set metallic-roughness texture */
    void setMetallicRoughnessTexture(FTexture2D* texture);
    
    /** Set normal map texture */
    void setNormalTexture(FTexture2D* texture);
    
    /** Set ambient occlusion texture */
    void setOcclusionTexture(FTexture2D* texture);
    
    /** Set emissive texture */
    void setEmissiveTexture(FTexture2D* texture);
    
    /** Set clear coat texture */
    void setClearCoatTexture(FTexture2D* texture);
    
    // ========================================================================
    // GPU Resources
    // ========================================================================
    
    /**
     * Update GPU uniform buffer with current parameters
     * Call this after modifying parameters and before rendering
     */
    void updateGPUResources();
    
    /**
     * Get the material uniform buffer
     */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> getMaterialBuffer() const { return m_materialBuffer; }
    
    /**
     * Get the material descriptor set
     */
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSet> getDescriptorSet() const { return m_descriptorSet; }
    
    /**
     * Check if material needs GPU update
     */
    bool isDirty() const { return m_dirty; }
    
    /**
     * Mark material as needing GPU update
     */
    void markDirty() { m_dirty = true; }
    
    // ========================================================================
    // Factory Methods
    // ========================================================================
    
    /**
     * Create a default PBR material (white, non-metallic, rough)
     */
    static TSharedPtr<FPBRMaterial> createDefault(
        MonsterRender::RHI::IRHIDevice* device,
        FPBRDescriptorSetManager* descriptorManager);
    
    /**
     * Create a metallic material
     */
    static TSharedPtr<FPBRMaterial> createMetallic(
        MonsterRender::RHI::IRHIDevice* device,
        FPBRDescriptorSetManager* descriptorManager,
        const FVector3f& baseColor,
        float roughness = 0.3f);
    
    /**
     * Create a dielectric (non-metallic) material
     */
    static TSharedPtr<FPBRMaterial> createDielectric(
        MonsterRender::RHI::IRHIDevice* device,
        FPBRDescriptorSetManager* descriptorManager,
        const FVector3f& baseColor,
        float roughness = 0.5f);
    
private:
    // Material identification
    FName m_name;
    
    // Material parameters
    FPBRMaterialParams m_params;
    FPBRMaterialTextures m_textures;
    
    // GPU resources
    MonsterRender::RHI::IRHIDevice* m_device = nullptr;
    FPBRDescriptorSetManager* m_descriptorManager = nullptr;
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> m_materialBuffer;
    TSharedPtr<MonsterRender::RHI::IRHIDescriptorSet> m_descriptorSet;
    
    // State
    bool m_initialized = false;
    bool m_dirty = true;
    
    /**
     * Create material uniform buffer
     */
    bool _createMaterialBuffer();
    
    /**
     * Create and update descriptor set
     */
    bool _createDescriptorSet();
    
    /**
     * Update descriptor set with current textures
     */
    void _updateDescriptorSet();
};

} // namespace Renderer
} // namespace MonsterEngine
