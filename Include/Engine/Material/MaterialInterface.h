#pragma once

/**
 * @file MaterialInterface.h
 * @brief Abstract base class for all material types
 * 
 * FMaterialInterface is the base class for FMaterial and FMaterialInstance.
 * It provides the common interface for accessing material properties and parameters.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/Materials/MaterialInterface.h
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Core/Templates/SharedPointer.h"
#include "Engine/Material/MaterialTypes.h"
#include "Containers/Array.h"

namespace MonsterEngine
{

// Forward declarations
class FMaterial;
class FMaterialInstance;
class FMaterialRenderProxy;
class FTexture;

namespace RHI = MonsterRender::RHI;

// ============================================================================
// Material Interface
// ============================================================================

/**
 * @class FMaterialInterface
 * @brief Abstract base class for materials and material instances
 * 
 * Provides the common interface for:
 * - Accessing the base material
 * - Getting/setting material parameters
 * - Creating render proxies
 * - Querying material properties
 * 
 * Reference UE5: UMaterialInterface
 */
class FMaterialInterface
{
public:
    FMaterialInterface();
    virtual ~FMaterialInterface();
    
    // Non-copyable
    FMaterialInterface(const FMaterialInterface&) = delete;
    FMaterialInterface& operator=(const FMaterialInterface&) = delete;
    
    // ========================================================================
    // Material Hierarchy
    // ========================================================================
    
    /**
     * Get the base material at the root of the hierarchy
     * @return The root FMaterial, never null for valid materials
     */
    virtual FMaterial* GetMaterial() = 0;
    virtual const FMaterial* GetMaterial() const = 0;
    
    /**
     * Get the parent material interface
     * For FMaterial, returns itself
     * For FMaterialInstance, returns the parent
     */
    virtual FMaterialInterface* GetParent() { return nullptr; }
    virtual const FMaterialInterface* GetParent() const { return nullptr; }
    
    /**
     * Check if this is a material instance
     */
    virtual bool IsMaterialInstance() const { return false; }
    
    // ========================================================================
    // Render Proxy
    // ========================================================================
    
    /**
     * Get the render proxy for this material
     * The render proxy is used by the renderer to access material data
     * @return The material render proxy
     */
    virtual FMaterialRenderProxy* GetRenderProxy() = 0;
    virtual const FMaterialRenderProxy* GetRenderProxy() const = 0;
    
    // ========================================================================
    // Material Properties
    // ========================================================================
    
    /**
     * Get material properties
     */
    virtual const FMaterialProperties& GetMaterialProperties() const = 0;
    
    /**
     * Get the material domain
     */
    virtual EMaterialDomain GetMaterialDomain() const;
    
    /**
     * Get the blend mode
     */
    virtual EMaterialBlendMode GetBlendMode() const;
    
    /**
     * Get the shading model
     */
    virtual EMaterialShadingModel GetShadingModel() const;
    
    /**
     * Check if material is two-sided
     */
    virtual bool IsTwoSided() const;
    
    /**
     * Check if material is masked (uses alpha test)
     */
    virtual bool IsMasked() const;
    
    /**
     * Check if material is translucent
     */
    virtual bool IsTranslucent() const;
    
    /**
     * Get opacity mask clip value
     */
    virtual float GetOpacityMaskClipValue() const;
    
    // ========================================================================
    // Parameter Access
    // ========================================================================
    
    /**
     * Get a scalar parameter value
     * @param ParameterInfo Parameter identification
     * @param OutValue Output value if found
     * @return true if parameter was found
     */
    virtual bool GetScalarParameterValue(const FMaterialParameterInfo& ParameterInfo, float& OutValue) const = 0;
    
    /**
     * Get a vector parameter value
     * @param ParameterInfo Parameter identification
     * @param OutValue Output value if found
     * @return true if parameter was found
     */
    virtual bool GetVectorParameterValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const = 0;
    
    /**
     * Get a texture parameter value
     * @param ParameterInfo Parameter identification
     * @param OutValue Output value if found
     * @return true if parameter was found
     */
    virtual bool GetTextureParameterValue(const FMaterialParameterInfo& ParameterInfo, FTexture*& OutValue) const = 0;
    
    /**
     * Get parameter metadata
     * @param Type Parameter type to query
     * @param ParameterInfo Parameter identification
     * @param OutMetadata Output metadata if found
     * @return true if parameter was found
     */
    virtual bool GetParameterValue(EMaterialParameterType Type, 
                                   const FMaterialParameterInfo& ParameterInfo,
                                   FMaterialParameterMetadata& OutMetadata) const;
    
    // ========================================================================
    // Convenience Parameter Access (by name)
    // ========================================================================
    
    /**
     * Get scalar parameter by name
     */
    bool GetScalarParameterValue(const FName& ParameterName, float& OutValue) const
    {
        return GetScalarParameterValue(FMaterialParameterInfo(ParameterName), OutValue);
    }
    
    /**
     * Get vector parameter by name
     */
    bool GetVectorParameterValue(const FName& ParameterName, FLinearColor& OutValue) const
    {
        return GetVectorParameterValue(FMaterialParameterInfo(ParameterName), OutValue);
    }
    
    /**
     * Get texture parameter by name
     */
    bool GetTextureParameterValue(const FName& ParameterName, FTexture*& OutValue) const
    {
        return GetTextureParameterValue(FMaterialParameterInfo(ParameterName), OutValue);
    }
    
    // ========================================================================
    // Textures
    // ========================================================================
    
    /**
     * Get all textures used by this material
     * @param OutTextures Array to fill with used textures
     */
    virtual void GetUsedTextures(TArray<FTexture*>& OutTextures) const = 0;
    
    // ========================================================================
    // Identification
    // ========================================================================
    
    /**
     * Get the material name
     */
    const FName& GetMaterialName() const { return m_materialName; }
    
    /**
     * Set the material name
     */
    void SetMaterialName(const FName& Name) { m_materialName = Name; }
    
    /**
     * Get debug name for logging
     */
    virtual String GetDebugName() const;
    
protected:
    /** Material name for identification */
    FName m_materialName;
    
    /** Cached material properties */
    mutable bool m_propertiesCached = false;
};

// ============================================================================
// Material Interface Ref
// ============================================================================

/** Shared pointer type for material interfaces */
using FMaterialInterfaceRef = TSharedPtr<FMaterialInterface>;
using FMaterialInterfaceWeakRef = TWeakPtr<FMaterialInterface>;

} // namespace MonsterEngine
