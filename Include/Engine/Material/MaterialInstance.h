#pragma once

/**
 * @file MaterialInstance.h
 * @brief Material instance class definition
 * 
 * FMaterialInstance allows overriding parameters from a parent material
 * without duplicating the entire material definition.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/Materials/MaterialInstance.h
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Core/Templates/SharedPointer.h"
#include "Engine/Material/MaterialInterface.h"
#include "Engine/Material/MaterialTypes.h"
#include "Containers/Array.h"

namespace MonsterEngine
{

// Forward declarations
class FMaterial;
class FMaterialRenderProxy;
class FTexture;

// ============================================================================
// Material Instance
// ============================================================================

/**
 * @class FMaterialInstance
 * @brief Material instance with parameter overrides
 * 
 * A material instance references a parent material (or another instance)
 * and can override specific parameter values. This allows creating
 * variations of a material without duplicating shader code.
 * 
 * Hierarchy:
 * - FMaterialInstance -> FMaterialInstance -> ... -> FMaterial
 * 
 * Parameter lookup walks up the hierarchy until a value is found.
 * 
 * Reference UE5: UMaterialInstance
 */
class FMaterialInstance : public FMaterialInterface
{
public:
    FMaterialInstance();
    explicit FMaterialInstance(FMaterialInterface* InParent);
    virtual ~FMaterialInstance();
    
    // ========================================================================
    // FMaterialInterface Implementation
    // ========================================================================
    
    virtual FMaterial* GetMaterial() override;
    virtual const FMaterial* GetMaterial() const override;
    
    virtual FMaterialInterface* GetParent() override { return m_parent.Pin().Get(); }
    virtual const FMaterialInterface* GetParent() const override { return m_parent.Pin().Get(); }
    
    virtual bool IsMaterialInstance() const override { return true; }
    
    virtual FMaterialRenderProxy* GetRenderProxy() override;
    virtual const FMaterialRenderProxy* GetRenderProxy() const override;
    
    virtual const FMaterialProperties& GetMaterialProperties() const override;
    
    virtual bool GetScalarParameterValue(const FMaterialParameterInfo& ParameterInfo, float& OutValue) const override;
    virtual bool GetVectorParameterValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const override;
    virtual bool GetTextureParameterValue(const FMaterialParameterInfo& ParameterInfo, FTexture*& OutValue) const override;
    
    virtual void GetUsedTextures(TArray<FTexture*>& OutTextures) const override;
    
    // ========================================================================
    // Parent Management
    // ========================================================================
    
    /**
     * Set the parent material interface
     * @param InParent New parent (material or instance)
     */
    void SetParent(FMaterialInterface* InParent);
    
    /**
     * Check if parent is valid
     */
    bool HasValidParent() const { return m_parent.IsValid(); }
    
    // ========================================================================
    // Parameter Overrides
    // ========================================================================
    
    /**
     * Set a scalar parameter override
     * @param ParameterName Parameter name
     * @param Value Override value
     */
    void SetScalarParameterValue(const FName& ParameterName, float Value);
    
    /**
     * Set a scalar parameter override with full info
     */
    void SetScalarParameterValue(const FMaterialParameterInfo& ParameterInfo, float Value);
    
    /**
     * Set a vector parameter override
     * @param ParameterName Parameter name
     * @param Value Override value
     */
    void SetVectorParameterValue(const FName& ParameterName, const FLinearColor& Value);
    
    /**
     * Set a vector parameter override with full info
     */
    void SetVectorParameterValue(const FMaterialParameterInfo& ParameterInfo, const FLinearColor& Value);
    
    /**
     * Set a texture parameter override
     * @param ParameterName Parameter name
     * @param Value Override texture
     */
    void SetTextureParameterValue(const FName& ParameterName, FTexture* Value);
    
    /**
     * Set a texture parameter override with full info
     */
    void SetTextureParameterValue(const FMaterialParameterInfo& ParameterInfo, FTexture* Value);
    
    // ========================================================================
    // Override Queries
    // ========================================================================
    
    /**
     * Check if a scalar parameter is overridden
     */
    bool IsScalarParameterOverridden(const FName& ParameterName) const;
    
    /**
     * Check if a vector parameter is overridden
     */
    bool IsVectorParameterOverridden(const FName& ParameterName) const;
    
    /**
     * Check if a texture parameter is overridden
     */
    bool IsTextureParameterOverridden(const FName& ParameterName) const;
    
    // ========================================================================
    // Clear Overrides
    // ========================================================================
    
    /**
     * Clear a scalar parameter override
     */
    void ClearScalarParameterValue(const FName& ParameterName);
    
    /**
     * Clear a vector parameter override
     */
    void ClearVectorParameterValue(const FName& ParameterName);
    
    /**
     * Clear a texture parameter override
     */
    void ClearTextureParameterValue(const FName& ParameterName);
    
    /**
     * Clear all parameter overrides
     */
    void ClearAllParameterValues();
    
    // ========================================================================
    // Override Access
    // ========================================================================
    
    /**
     * Get all scalar parameter overrides
     */
    const TArray<FScalarParameterValue>& GetScalarParameterOverrides() const { return m_scalarOverrides; }
    
    /**
     * Get all vector parameter overrides
     */
    const TArray<FVectorParameterValue>& GetVectorParameterOverrides() const { return m_vectorOverrides; }
    
    /**
     * Get all texture parameter overrides
     */
    const TArray<FTextureParameterValue>& GetTextureParameterOverrides() const { return m_textureOverrides; }
    
    // ========================================================================
    // Property Overrides
    // ========================================================================
    
    /**
     * Override material properties
     */
    void SetPropertyOverrides(const FMaterialProperties& Properties);
    
    /**
     * Check if properties are overridden
     */
    bool HasPropertyOverrides() const { return m_hasPropertyOverrides; }
    
    /**
     * Clear property overrides
     */
    void ClearPropertyOverrides();
    
    // ========================================================================
    // Dirty State
    // ========================================================================
    
    /**
     * Mark instance as dirty (render proxy needs update)
     */
    void MarkDirty();
    
    /**
     * Check if instance is dirty
     */
    bool IsDirty() const { return m_isDirty; }
    
protected:
    /**
     * Create the render proxy
     */
    void CreateRenderProxy();
    
    /**
     * Update render proxy with current overrides
     */
    void UpdateRenderProxy();
    
    /**
     * Find override index by parameter info
     */
    int32 FindScalarOverrideIndex(const FMaterialParameterInfo& Info) const;
    int32 FindVectorOverrideIndex(const FMaterialParameterInfo& Info) const;
    int32 FindTextureOverrideIndex(const FMaterialParameterInfo& Info) const;
    
protected:
    /** Parent material interface */
    TWeakPtr<FMaterialInterface> m_parent;
    
    /** Scalar parameter overrides */
    TArray<FScalarParameterValue> m_scalarOverrides;
    
    /** Vector parameter overrides */
    TArray<FVectorParameterValue> m_vectorOverrides;
    
    /** Texture parameter overrides */
    TArray<FTextureParameterValue> m_textureOverrides;
    
    /** Property overrides */
    FMaterialProperties m_propertyOverrides;
    bool m_hasPropertyOverrides = false;
    
    /** Render proxy for this instance */
    TSharedPtr<FMaterialRenderProxy> m_renderProxy;
    
    /** Whether instance needs render proxy update */
    bool m_isDirty = true;
};

// ============================================================================
// Material Instance Ref
// ============================================================================

/** Shared pointer type for material instances */
using FMaterialInstanceRef = TSharedPtr<FMaterialInstance>;
using FMaterialInstanceWeakRef = TWeakPtr<FMaterialInstance>;

} // namespace MonsterEngine
