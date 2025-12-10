#pragma once

/**
 * @file MaterialRenderProxy.h
 * @brief Material render proxy for renderer access
 * 
 * FMaterialRenderProxy provides a thread-safe interface for the renderer
 * to access material data. It caches parameter values and manages
 * uniform buffer updates.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Public/MaterialShared.h (FMaterialRenderProxy)
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Core/Templates/SharedPointer.h"
#include "Engine/Material/MaterialTypes.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

// Forward declare RHI types
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
    class IRHIBuffer;
    class IRHITexture;
    class IRHIPipelineState;
}}

namespace MonsterEngine
{

// Forward declarations
class FMaterialInterface;
class FMaterial;
class FTexture;

// ============================================================================
// Material Render Proxy
// ============================================================================

/**
 * @class FMaterialRenderProxy
 * @brief Render-thread representation of a material
 * 
 * The render proxy:
 * - Caches material parameter values for render thread access
 * - Manages uniform buffer creation and updates
 * - Provides thread-safe access to material data
 * - Handles parameter override resolution
 * 
 * Reference UE5: FMaterialRenderProxy
 */
class FMaterialRenderProxy
{
public:
    FMaterialRenderProxy();
    explicit FMaterialRenderProxy(FMaterialInterface* InMaterial);
    virtual ~FMaterialRenderProxy();
    
    // Non-copyable
    FMaterialRenderProxy(const FMaterialRenderProxy&) = delete;
    FMaterialRenderProxy& operator=(const FMaterialRenderProxy&) = delete;
    
    // ========================================================================
    // Material Access
    // ========================================================================
    
    /**
     * Get the owning material interface
     */
    FMaterialInterface* GetMaterialInterface() const { return m_material; }
    
    /**
     * Get the base material
     */
    FMaterial* GetMaterial() const;
    
    /**
     * Check if proxy is valid
     */
    bool IsValid() const { return m_material != nullptr; }
    
    // ========================================================================
    // Parameter Access (Render Thread)
    // ========================================================================
    
    /**
     * Get a scalar parameter value
     * @param ParameterInfo Parameter identification
     * @param OutValue Output value
     * @return true if parameter was found
     */
    bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float& OutValue) const;
    
    /**
     * Get a vector parameter value
     * @param ParameterInfo Parameter identification
     * @param OutValue Output value
     * @return true if parameter was found
     */
    bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const;
    
    /**
     * Get a texture parameter value
     * @param ParameterInfo Parameter identification
     * @param OutValue Output texture
     * @return true if parameter was found
     */
    bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo, FTexture*& OutValue) const;
    
    // ========================================================================
    // Cached Parameter Values
    // ========================================================================
    
    /**
     * Cache a scalar parameter value
     */
    void SetCachedScalar(const FMaterialParameterInfo& ParameterInfo, float Value);
    
    /**
     * Cache a vector parameter value
     */
    void SetCachedVector(const FMaterialParameterInfo& ParameterInfo, const FLinearColor& Value);
    
    /**
     * Cache a texture parameter value
     */
    void SetCachedTexture(const FMaterialParameterInfo& ParameterInfo, FTexture* Value);
    
    /**
     * Clear all cached values
     */
    void ClearCachedValues();
    
    // ========================================================================
    // Uniform Buffer
    // ========================================================================
    
    /**
     * Get or create the material uniform buffer
     * @param Device RHI device
     * @return Uniform buffer, or nullptr on failure
     */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> GetUniformBuffer(MonsterRender::RHI::IRHIDevice* Device);
    
    /**
     * Update the uniform buffer with current parameter values
     * @param Device RHI device
     */
    void UpdateUniformBuffer(MonsterRender::RHI::IRHIDevice* Device);
    
    /**
     * Invalidate the uniform buffer (force recreation)
     */
    void InvalidateUniformBuffer();
    
    /**
     * Check if uniform buffer needs update
     */
    bool IsUniformBufferDirty() const { return m_uniformBufferDirty; }
    
    // ========================================================================
    // Texture Bindings
    // ========================================================================
    
    /**
     * Get all texture bindings for this material
     * @param OutTextures Output array of (slot, texture) pairs
     */
    void GetTextureBindings(TArray<TPair<int32, FTexture*>>& OutTextures) const;
    
    /**
     * Get texture at specific slot
     * @param Slot Texture slot index
     * @return Texture at slot, or nullptr
     */
    FTexture* GetTextureAtSlot(int32 Slot) const;
    
    // ========================================================================
    // Pipeline State
    // ========================================================================
    
    /**
     * Get the pipeline state for this material
     */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> GetPipelineState() const;
    
    // ========================================================================
    // Dirty State
    // ========================================================================
    
    /**
     * Mark proxy as dirty (needs update)
     */
    void MarkDirty();
    
    /**
     * Check if proxy is dirty
     */
    bool IsDirty() const { return m_isDirty; }
    
    /**
     * Clear dirty flag
     */
    void ClearDirty() { m_isDirty = false; }
    
    // ========================================================================
    // Debug
    // ========================================================================
    
    /**
     * Get debug name
     */
    String GetDebugName() const;
    
protected:
    /**
     * Create the uniform buffer
     */
    bool CreateUniformBuffer(MonsterRender::RHI::IRHIDevice* Device);
    
    /**
     * Calculate uniform buffer size
     */
    uint32 CalculateUniformBufferSize() const;
    
    /**
     * Fill uniform buffer data
     */
    void FillUniformBufferData(uint8* Data, uint32 Size) const;
    
protected:
    /** Owning material interface */
    FMaterialInterface* m_material = nullptr;
    
    /** Cached scalar parameters */
    TArray<FScalarParameterValue> m_cachedScalars;
    
    /** Cached vector parameters */
    TArray<FVectorParameterValue> m_cachedVectors;
    
    /** Cached texture parameters */
    TArray<FTextureParameterValue> m_cachedTextures;
    
    /** Material uniform buffer */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> m_uniformBuffer;
    
    /** Whether uniform buffer needs update */
    bool m_uniformBufferDirty = true;
    
    /** Whether proxy is dirty */
    bool m_isDirty = true;
};

// ============================================================================
// Material Render Proxy Ref
// ============================================================================

/** Shared pointer type for material render proxies */
using FMaterialRenderProxyRef = TSharedPtr<FMaterialRenderProxy>;
using FMaterialRenderProxyWeakRef = TWeakPtr<FMaterialRenderProxy>;

} // namespace MonsterEngine
