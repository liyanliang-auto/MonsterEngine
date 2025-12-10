#pragma once

/**
 * @file MaterialInstanceDynamic.h
 * @brief Dynamic material instance for runtime parameter modification
 * 
 * FMaterialInstanceDynamic allows modifying material parameters at runtime.
 * It provides optimized methods for frequently changing parameters.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/Materials/MaterialInstanceDynamic.h
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Core/Templates/SharedPointer.h"
#include "Engine/Material/MaterialInstance.h"
#include "Engine/Material/MaterialTypes.h"

namespace MonsterEngine
{

// Forward declarations
class FMaterialInterface;
class FTexture;

// ============================================================================
// Material Instance Dynamic
// ============================================================================

/**
 * @class FMaterialInstanceDynamic
 * @brief Dynamic material instance for runtime modifications
 * 
 * This class extends FMaterialInstance with:
 * - Optimized parameter setting for runtime use
 * - Index-based parameter access for performance
 * - Parameter interpolation between instances
 * - Copy operations for parameter values
 * 
 * Use this class when you need to modify material parameters at runtime,
 * such as changing colors, textures, or other values during gameplay.
 * 
 * Reference UE5: UMaterialInstanceDynamic
 */
class FMaterialInstanceDynamic : public FMaterialInstance
{
public:
    FMaterialInstanceDynamic();
    explicit FMaterialInstanceDynamic(FMaterialInterface* InParent);
    virtual ~FMaterialInstanceDynamic();
    
    // ========================================================================
    // Static Creation
    // ========================================================================
    
    /**
     * Create a dynamic material instance from a parent material
     * @param ParentMaterial Parent material or instance
     * @return New dynamic material instance
     */
    static TSharedPtr<FMaterialInstanceDynamic> Create(FMaterialInterface* ParentMaterial);
    
    /**
     * Create a dynamic material instance with a name
     * @param ParentMaterial Parent material or instance
     * @param Name Instance name
     * @return New dynamic material instance
     */
    static TSharedPtr<FMaterialInstanceDynamic> Create(FMaterialInterface* ParentMaterial, const FName& Name);
    
    // ========================================================================
    // Scalar Parameters
    // ========================================================================
    
    /**
     * Set a scalar parameter value
     * @param ParameterName Parameter name
     * @param Value New value
     */
    void SetScalarParameterValue(const FName& ParameterName, float Value);
    
    /**
     * Set a scalar parameter value by info
     * @param ParameterInfo Parameter info
     * @param Value New value
     */
    void SetScalarParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, float Value);
    
    /**
     * Get a scalar parameter value
     * @param ParameterName Parameter name
     * @return Parameter value (0 if not found)
     */
    float GetScalarParameterValue(const FName& ParameterName) const;
    
    /**
     * Initialize a scalar parameter and get its index for fast access
     * @param ParameterName Parameter name
     * @param Value Initial value
     * @param OutParameterIndex Output index for SetScalarParameterByIndex
     * @return true if successful
     */
    bool InitializeScalarParameterAndGetIndex(const FName& ParameterName, float Value, int32& OutParameterIndex);
    
    /**
     * Set a scalar parameter by cached index (fast path)
     * @param ParameterIndex Index from InitializeScalarParameterAndGetIndex
     * @param Value New value
     * @return true if successful
     */
    bool SetScalarParameterByIndex(int32 ParameterIndex, float Value);
    
    // ========================================================================
    // Vector Parameters
    // ========================================================================
    
    /**
     * Set a vector parameter value
     * @param ParameterName Parameter name
     * @param Value New value
     */
    void SetVectorParameterValue(const FName& ParameterName, const FLinearColor& Value);
    
    /**
     * Set a vector parameter value (FVector overload)
     */
    void SetVectorParameterValue(const FName& ParameterName, const FVector& Value)
    {
        SetVectorParameterValue(ParameterName, FLinearColor(Value.X, Value.Y, Value.Z, 1.0f));
    }
    
    /**
     * Set a vector parameter value by info
     * @param ParameterInfo Parameter info
     * @param Value New value
     */
    void SetVectorParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, const FLinearColor& Value);
    
    /**
     * Get a vector parameter value
     * @param ParameterName Parameter name
     * @return Parameter value (black if not found)
     */
    FLinearColor GetVectorParameterValue(const FName& ParameterName) const;
    
    /**
     * Initialize a vector parameter and get its index for fast access
     * @param ParameterName Parameter name
     * @param Value Initial value
     * @param OutParameterIndex Output index for SetVectorParameterByIndex
     * @return true if successful
     */
    bool InitializeVectorParameterAndGetIndex(const FName& ParameterName, const FLinearColor& Value, int32& OutParameterIndex);
    
    /**
     * Set a vector parameter by cached index (fast path)
     * @param ParameterIndex Index from InitializeVectorParameterAndGetIndex
     * @param Value New value
     * @return true if successful
     */
    bool SetVectorParameterByIndex(int32 ParameterIndex, const FLinearColor& Value);
    
    // ========================================================================
    // Texture Parameters
    // ========================================================================
    
    /**
     * Set a texture parameter value
     * @param ParameterName Parameter name
     * @param Value New texture
     */
    void SetTextureParameterValue(const FName& ParameterName, FTexture* Value);
    
    /**
     * Set a texture parameter value by info
     * @param ParameterInfo Parameter info
     * @param Value New texture
     */
    void SetTextureParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, FTexture* Value);
    
    /**
     * Get a texture parameter value
     * @param ParameterName Parameter name
     * @return Parameter texture (nullptr if not found)
     */
    FTexture* GetTextureParameterValue(const FName& ParameterName) const;
    
    // ========================================================================
    // Parameter Interpolation
    // ========================================================================
    
    /**
     * Interpolate parameters between two material instances
     * @param SourceA First source (used when Alpha = 0)
     * @param SourceB Second source (used when Alpha = 1)
     * @param Alpha Blend factor (0-1)
     */
    void InterpolateParameters(FMaterialInstance* SourceA, FMaterialInstance* SourceB, float Alpha);
    
    // ========================================================================
    // Copy Operations
    // ========================================================================
    
    /**
     * Copy all parameter values from another instance
     * @param Source Source material instance
     */
    void CopyParameterOverrides(FMaterialInstance* Source);
    
    /**
     * Copy only scalar and vector parameters (faster)
     * @param Source Source material interface
     */
    void CopyScalarAndVectorParameters(FMaterialInterface* Source);
    
    /**
     * Clear all parameter values
     */
    void ClearParameterValues();
    
    // ========================================================================
    // Update Management
    // ========================================================================
    
    /**
     * Force update of render proxy
     * Call this after batch parameter changes
     */
    void UpdateRenderProxy();
    
    /**
     * Begin batch parameter update (defers render proxy update)
     */
    void BeginBatchUpdate();
    
    /**
     * End batch parameter update (updates render proxy)
     */
    void EndBatchUpdate();
    
    /**
     * Check if in batch update mode
     */
    bool IsInBatchUpdate() const { return m_batchUpdateDepth > 0; }
    
protected:
    /**
     * Internal parameter update (respects batch mode)
     */
    void OnParameterChanged();
    
protected:
    /** Batch update depth counter */
    int32 m_batchUpdateDepth = 0;
};

// ============================================================================
// Material Instance Dynamic Ref
// ============================================================================

/** Shared pointer type for dynamic material instances */
using FMaterialInstanceDynamicRef = TSharedPtr<FMaterialInstanceDynamic>;
using FMaterialInstanceDynamicWeakRef = TWeakPtr<FMaterialInstanceDynamic>;

} // namespace MonsterEngine
