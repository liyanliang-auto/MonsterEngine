// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file PBRDefaultTextures.h
 * @brief Default texture manager for PBR rendering
 * 
 * Provides fallback textures when materials don't have specific textures assigned.
 * These textures ensure PBR shaders always have valid texture bindings.
 * 
 * Reference:
 * - UE5: Engine/Source/Runtime/Engine/Private/Materials/MaterialTextures.cpp
 * - Filament: TextureManager.h
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"

// Forward declarations
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
    class IRHITexture;
    class IRHISampler;
}}

namespace MonsterEngine
{

// Forward declarations
class FTexture2D;

namespace Renderer
{

/**
 * @class FPBRDefaultTextures
 * @brief Singleton manager for PBR default/fallback textures
 * 
 * Provides default textures for PBR materials:
 * - White texture (1,1,1,1) - default base color
 * - Black texture (0,0,0,1) - default emissive, metallic
 * - Normal texture (0.5,0.5,1,1) - flat normal map
 * - Default AO (1,1,1,1) - no occlusion
 * 
 * Thread-safe singleton pattern for global access.
 */
class FPBRDefaultTextures
{
public:
    /**
     * Get singleton instance
     * @return Reference to the singleton instance
     */
    static FPBRDefaultTextures& get();
    
    /**
     * Initialize default textures
     * Must be called once before using any default textures
     * @param device RHI device for texture creation
     * @return true if initialization succeeded
     */
    bool initialize(MonsterRender::RHI::IRHIDevice* device);
    
    /**
     * Shutdown and release all default textures
     */
    void shutdown();
    
    /**
     * Check if default textures are initialized
     */
    bool isInitialized() const { return m_initialized; }
    
    // ========================================================================
    // Default Texture Access
    // ========================================================================
    
    /**
     * Get white texture (1,1,1,1)
     * Used as default for: BaseColor
     */
    TSharedPtr<FTexture2D> getWhiteTexture() const { return m_whiteTexture; }
    
    /**
     * Get black texture (0,0,0,1)
     * Used as default for: Emissive, Metallic
     */
    TSharedPtr<FTexture2D> getBlackTexture() const { return m_blackTexture; }
    
    /**
     * Get default normal map (0.5,0.5,1,1)
     * Used as default for: Normal maps
     */
    TSharedPtr<FTexture2D> getNormalTexture() const { return m_normalTexture; }
    
    /**
     * Get default metallic-roughness texture
     * R=0 (non-metallic), G=0.5 (medium roughness)
     */
    TSharedPtr<FTexture2D> getMetallicRoughnessTexture() const { return m_metallicRoughnessTexture; }
    
    /**
     * Get default occlusion texture (1,1,1,1)
     * Used as default for: Ambient Occlusion
     */
    TSharedPtr<FTexture2D> getOcclusionTexture() const { return m_occlusionTexture; }
    
    /**
     * Get default sampler for PBR textures
     */
    TSharedPtr<MonsterRender::RHI::IRHISampler> getDefaultSampler() const { return m_defaultSampler; }
    
    // ========================================================================
    // RHI Resource Access (for direct binding)
    // ========================================================================
    
    /**
     * Get RHI texture for white texture
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> getWhiteRHITexture() const;
    
    /**
     * Get RHI texture for black texture
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> getBlackRHITexture() const;
    
    /**
     * Get RHI texture for normal texture
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> getNormalRHITexture() const;
    
    /**
     * Get RHI texture for metallic-roughness texture
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> getMetallicRoughnessRHITexture() const;
    
    /**
     * Get RHI texture for occlusion texture
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> getOcclusionRHITexture() const;
    
private:
    // Private constructor for singleton
    FPBRDefaultTextures();
    ~FPBRDefaultTextures();
    
    // Non-copyable
    FPBRDefaultTextures(const FPBRDefaultTextures&) = delete;
    FPBRDefaultTextures& operator=(const FPBRDefaultTextures&) = delete;
    
    // Default textures
    TSharedPtr<FTexture2D> m_whiteTexture;
    TSharedPtr<FTexture2D> m_blackTexture;
    TSharedPtr<FTexture2D> m_normalTexture;
    TSharedPtr<FTexture2D> m_metallicRoughnessTexture;
    TSharedPtr<FTexture2D> m_occlusionTexture;
    
    // Default sampler
    TSharedPtr<MonsterRender::RHI::IRHISampler> m_defaultSampler;
    
    // State
    MonsterRender::RHI::IRHIDevice* m_device = nullptr;
    bool m_initialized = false;
    
    /**
     * Create default sampler
     */
    bool _createDefaultSampler();
    
    /**
     * Create metallic-roughness default texture
     * Special texture with R=0 (non-metallic), G=0.5 (medium roughness)
     */
    bool _createMetallicRoughnessTexture();
};

} // namespace Renderer
} // namespace MonsterEngine
