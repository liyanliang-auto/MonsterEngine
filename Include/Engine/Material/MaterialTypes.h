#pragma once

/**
 * @file MaterialTypes.h
 * @brief Material parameter types and structures
 * 
 * Defines all material parameter types, parameter info structures,
 * and parameter value containers used throughout the material system.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Public/MaterialTypes.h
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Color.h"

namespace MonsterEngine
{

// Forward declarations
class FTexture;
class FTexture2D;
class FTextureCube;

// ============================================================================
// Material Parameter Type Enumeration
// ============================================================================

/**
 * @enum EMaterialParameterType
 * @brief Types of material parameters
 * 
 * Reference UE5: EMaterialParameterType
 */
enum class EMaterialParameterType : uint8
{
    None = 0,
    Scalar,                 // Single float value
    Vector,                 // FLinearColor (4 floats)
    DoubleVector,           // FVector4d (4 doubles)
    Texture,                // Texture reference
    Font,                   // Font texture
    RuntimeVirtualTexture,  // Runtime virtual texture
    SparseVolumeTexture,    // Sparse volume texture
    StaticSwitch,           // Static boolean switch
    StaticComponentMask,    // Static RGBA component mask
    
    NumTypes
};

/**
 * @enum EMaterialParameterAssociation
 * @brief How a parameter is associated with material layers
 * 
 * Reference UE5: EMaterialParameterAssociation
 */
enum class EMaterialParameterAssociation : uint8
{
    GlobalParameter,    // Global parameter (not layer-specific)
    LayerParameter,     // Parameter belongs to a specific layer
    BlendParameter,     // Parameter controls layer blending
};

/**
 * @enum EMaterialDomain
 * @brief The domain in which the material is used
 * 
 * Reference UE5: EMaterialDomain
 */
enum class EMaterialDomain : uint8
{
    Surface,            // Standard surface material
    DeferredDecal,      // Deferred decal material
    LightFunction,      // Light function material
    Volume,             // Volumetric material
    PostProcess,        // Post-process material
    UI,                 // User interface material
    
    NumDomains
};

/**
 * @enum EMaterialBlendMode
 * @brief Material blend modes
 * 
 * Reference UE5: EBlendMode
 */
enum class EMaterialBlendMode : uint8
{
    Opaque,             // Fully opaque
    Masked,             // Binary alpha (alpha test)
    Translucent,        // Standard alpha blending
    Additive,           // Additive blending
    Modulate,           // Multiplicative blending
    AlphaComposite,     // Pre-multiplied alpha
    AlphaHoldout,       // Alpha holdout for compositing
    
    NumModes
};

/**
 * @enum EMaterialShadingModel
 * @brief Material shading models
 * 
 * Reference UE5: EMaterialShadingModel
 */
enum class EMaterialShadingModel : uint8
{
    Unlit,              // No lighting
    DefaultLit,         // Standard PBR lighting
    Subsurface,         // Subsurface scattering
    PreintegratedSkin,  // Pre-integrated skin shading
    ClearCoat,          // Clear coat (car paint)
    SubsurfaceProfile,  // Subsurface with profile
    TwoSidedFoliage,    // Two-sided foliage
    Hair,               // Hair/fur shading
    Cloth,              // Cloth shading
    Eye,                // Eye shading
    SingleLayerWater,   // Single layer water
    ThinTranslucent,    // Thin translucent surface
    
    NumModels
};

// ============================================================================
// Material Parameter Info
// ============================================================================

/**
 * @struct FMaterialParameterInfo
 * @brief Information identifying a material parameter
 * 
 * Contains the name and association info for a material parameter.
 * Used to look up parameter values in the material hierarchy.
 * 
 * Reference UE5: FMaterialParameterInfo
 */
struct FMaterialParameterInfo
{
    /** Parameter name */
    FName Name;
    
    /** How this parameter is associated (global, layer, blend) */
    EMaterialParameterAssociation Association = EMaterialParameterAssociation::GlobalParameter;
    
    /** Layer index for layer/blend parameters, INDEX_NONE for global */
    int32 Index = INDEX_NONE;
    
    /** Default constructor */
    FMaterialParameterInfo() = default;
    
    /** Constructor with name only (global parameter) */
    explicit FMaterialParameterInfo(const FName& InName)
        : Name(InName)
        , Association(EMaterialParameterAssociation::GlobalParameter)
        , Index(INDEX_NONE)
    {}
    
    /** Constructor with name and string (global parameter) */
    explicit FMaterialParameterInfo(const TCHAR* InName)
        : Name(InName)
        , Association(EMaterialParameterAssociation::GlobalParameter)
        , Index(INDEX_NONE)
    {}
    
    /** Full constructor */
    FMaterialParameterInfo(const FName& InName, EMaterialParameterAssociation InAssociation, int32 InIndex = INDEX_NONE)
        : Name(InName)
        , Association(InAssociation)
        , Index(InIndex)
    {}
    
    /** Equality comparison */
    bool operator==(const FMaterialParameterInfo& Other) const
    {
        return Name == Other.Name && 
               Association == Other.Association && 
               Index == Other.Index;
    }
    
    bool operator!=(const FMaterialParameterInfo& Other) const
    {
        return !(*this == Other);
    }
    
    /** Get hash for use in containers */
    friend uint32 GetTypeHash(const FMaterialParameterInfo& Info)
    {
        return HashCombine(
            GetTypeHash(Info.Name),
            HashCombine(static_cast<uint32>(Info.Association), static_cast<uint32>(Info.Index))
        );
    }
    
    /** Check if this is a global parameter */
    bool IsGlobal() const
    {
        return Association == EMaterialParameterAssociation::GlobalParameter;
    }
};

// ============================================================================
// Material Parameter Values
// ============================================================================

/**
 * @struct FScalarParameterValue
 * @brief Scalar (float) parameter value
 * 
 * Reference UE5: FScalarParameterValue
 */
struct FScalarParameterValue
{
    /** Parameter identification */
    FMaterialParameterInfo ParameterInfo;
    
    /** The scalar value */
    float ParameterValue = 0.0f;
    
    /** Default constructor */
    FScalarParameterValue() = default;
    
    /** Constructor with info and value */
    FScalarParameterValue(const FMaterialParameterInfo& InInfo, float InValue)
        : ParameterInfo(InInfo)
        , ParameterValue(InValue)
    {}
    
    /** Constructor with name and value */
    FScalarParameterValue(const FName& InName, float InValue)
        : ParameterInfo(InName)
        , ParameterValue(InValue)
    {}
    
    bool operator==(const FScalarParameterValue& Other) const
    {
        return ParameterInfo == Other.ParameterInfo && 
               ParameterValue == Other.ParameterValue;
    }
};

/**
 * @struct FVectorParameterValue
 * @brief Vector (color) parameter value
 * 
 * Reference UE5: FVectorParameterValue
 */
struct FVectorParameterValue
{
    /** Parameter identification */
    FMaterialParameterInfo ParameterInfo;
    
    /** The vector/color value */
    FLinearColor ParameterValue = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    /** Default constructor */
    FVectorParameterValue() = default;
    
    /** Constructor with info and value */
    FVectorParameterValue(const FMaterialParameterInfo& InInfo, const FLinearColor& InValue)
        : ParameterInfo(InInfo)
        , ParameterValue(InValue)
    {}
    
    /** Constructor with name and value */
    FVectorParameterValue(const FName& InName, const FLinearColor& InValue)
        : ParameterInfo(InName)
        , ParameterValue(InValue)
    {}
    
    bool operator==(const FVectorParameterValue& Other) const
    {
        return ParameterInfo == Other.ParameterInfo && 
               ParameterValue == Other.ParameterValue;
    }
};

/**
 * @struct FTextureParameterValue
 * @brief Texture parameter value
 * 
 * Reference UE5: FTextureParameterValue
 */
struct FTextureParameterValue
{
    /** Parameter identification */
    FMaterialParameterInfo ParameterInfo;
    
    /** The texture reference */
    FTexture* ParameterValue = nullptr;
    
    /** Default constructor */
    FTextureParameterValue() = default;
    
    /** Constructor with info and value */
    FTextureParameterValue(const FMaterialParameterInfo& InInfo, FTexture* InValue)
        : ParameterInfo(InInfo)
        , ParameterValue(InValue)
    {}
    
    /** Constructor with name and value */
    FTextureParameterValue(const FName& InName, FTexture* InValue)
        : ParameterInfo(InName)
        , ParameterValue(InValue)
    {}
    
    bool operator==(const FTextureParameterValue& Other) const
    {
        return ParameterInfo == Other.ParameterInfo && 
               ParameterValue == Other.ParameterValue;
    }
};

/**
 * @struct FDoubleVectorParameterValue
 * @brief Double precision vector parameter value
 * 
 * Reference UE5: FDoubleVectorParameterValue
 */
struct FDoubleVectorParameterValue
{
    /** Parameter identification */
    FMaterialParameterInfo ParameterInfo;
    
    /** The double vector value */
    FVector4d ParameterValue = FVector4d(0.0, 0.0, 0.0, 1.0);
    
    /** Default constructor */
    FDoubleVectorParameterValue() = default;
    
    /** Constructor with info and value */
    FDoubleVectorParameterValue(const FMaterialParameterInfo& InInfo, const FVector4d& InValue)
        : ParameterInfo(InInfo)
        , ParameterValue(InValue)
    {}
    
    bool operator==(const FDoubleVectorParameterValue& Other) const
    {
        return ParameterInfo == Other.ParameterInfo && 
               ParameterValue == Other.ParameterValue;
    }
};

// ============================================================================
// Material Parameter Metadata
// ============================================================================

/**
 * @struct FMaterialParameterMetadata
 * @brief Extended metadata for a material parameter
 * 
 * Contains the value and additional metadata about a parameter.
 * 
 * Reference UE5: FMaterialParameterMetadata
 */
struct FMaterialParameterMetadata
{
    /** Parameter type */
    EMaterialParameterType Type = EMaterialParameterType::None;
    
    /** Parameter value (union-like storage) */
    union
    {
        float ScalarValue;
        struct { float R, G, B, A; } VectorValue;
        FTexture* TextureValue;
        bool BoolValue;
    };
    
    /** Whether this parameter is overridden from parent */
    bool bOverride = false;
    
    /** Group name for editor organization */
    FName Group;
    
    /** Sort priority within group */
    int32 SortPriority = 0;
    
    /** Default constructor */
    FMaterialParameterMetadata()
        : Type(EMaterialParameterType::None)
        , ScalarValue(0.0f)
        , bOverride(false)
        , SortPriority(0)
    {}
    
    /** Set scalar value */
    void SetScalar(float Value)
    {
        Type = EMaterialParameterType::Scalar;
        ScalarValue = Value;
    }
    
    /** Set vector value */
    void SetVector(const FLinearColor& Value)
    {
        Type = EMaterialParameterType::Vector;
        VectorValue.R = Value.R;
        VectorValue.G = Value.G;
        VectorValue.B = Value.B;
        VectorValue.A = Value.A;
    }
    
    /** Set texture value */
    void SetTexture(FTexture* Value)
    {
        Type = EMaterialParameterType::Texture;
        TextureValue = Value;
    }
    
    /** Get as scalar */
    float GetScalar() const { return ScalarValue; }
    
    /** Get as vector */
    FLinearColor GetVector() const 
    { 
        return FLinearColor(VectorValue.R, VectorValue.G, VectorValue.B, VectorValue.A); 
    }
    
    /** Get as texture */
    FTexture* GetTexture() const { return TextureValue; }
};

// ============================================================================
// Material Properties
// ============================================================================

/**
 * @struct FMaterialProperties
 * @brief Collection of material rendering properties
 * 
 * Contains all the rendering-related properties of a material.
 */
struct FMaterialProperties
{
    /** Material domain */
    EMaterialDomain Domain = EMaterialDomain::Surface;
    
    /** Blend mode */
    EMaterialBlendMode BlendMode = EMaterialBlendMode::Opaque;
    
    /** Shading model */
    EMaterialShadingModel ShadingModel = EMaterialShadingModel::DefaultLit;
    
    /** Two-sided rendering */
    bool bTwoSided = false;
    
    /** Wireframe rendering */
    bool bWireframe = false;
    
    /** Cast shadows */
    bool bCastShadow = true;
    
    /** Receive shadows */
    bool bReceiveShadow = true;
    
    /** Opacity mask clip value (for masked blend mode) */
    float OpacityMaskClipValue = 0.333f;
    
    /** Whether to use dithered LOD transition */
    bool bDitheredLODTransition = false;
    
    /** Whether this material writes to velocity buffer */
    bool bOutputVelocity = false;
    
    /** Default constructor */
    FMaterialProperties() = default;
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Get the name of a material parameter type
 */
inline const TCHAR* GetMaterialParameterTypeName(EMaterialParameterType Type)
{
    switch (Type)
    {
        case EMaterialParameterType::None:                  return TEXT("None");
        case EMaterialParameterType::Scalar:                return TEXT("Scalar");
        case EMaterialParameterType::Vector:                return TEXT("Vector");
        case EMaterialParameterType::DoubleVector:          return TEXT("DoubleVector");
        case EMaterialParameterType::Texture:               return TEXT("Texture");
        case EMaterialParameterType::Font:                  return TEXT("Font");
        case EMaterialParameterType::RuntimeVirtualTexture: return TEXT("RuntimeVirtualTexture");
        case EMaterialParameterType::SparseVolumeTexture:   return TEXT("SparseVolumeTexture");
        case EMaterialParameterType::StaticSwitch:          return TEXT("StaticSwitch");
        case EMaterialParameterType::StaticComponentMask:   return TEXT("StaticComponentMask");
        default:                                            return TEXT("Unknown");
    }
}

/**
 * Get the name of a material blend mode
 */
inline const TCHAR* GetMaterialBlendModeName(EMaterialBlendMode Mode)
{
    switch (Mode)
    {
        case EMaterialBlendMode::Opaque:         return TEXT("Opaque");
        case EMaterialBlendMode::Masked:         return TEXT("Masked");
        case EMaterialBlendMode::Translucent:    return TEXT("Translucent");
        case EMaterialBlendMode::Additive:       return TEXT("Additive");
        case EMaterialBlendMode::Modulate:       return TEXT("Modulate");
        case EMaterialBlendMode::AlphaComposite: return TEXT("AlphaComposite");
        case EMaterialBlendMode::AlphaHoldout:   return TEXT("AlphaHoldout");
        default:                                 return TEXT("Unknown");
    }
}

/**
 * Get the name of a material shading model
 */
inline const TCHAR* GetMaterialShadingModelName(EMaterialShadingModel Model)
{
    switch (Model)
    {
        case EMaterialShadingModel::Unlit:              return TEXT("Unlit");
        case EMaterialShadingModel::DefaultLit:         return TEXT("DefaultLit");
        case EMaterialShadingModel::Subsurface:         return TEXT("Subsurface");
        case EMaterialShadingModel::PreintegratedSkin:  return TEXT("PreintegratedSkin");
        case EMaterialShadingModel::ClearCoat:          return TEXT("ClearCoat");
        case EMaterialShadingModel::SubsurfaceProfile:  return TEXT("SubsurfaceProfile");
        case EMaterialShadingModel::TwoSidedFoliage:    return TEXT("TwoSidedFoliage");
        case EMaterialShadingModel::Hair:               return TEXT("Hair");
        case EMaterialShadingModel::Cloth:              return TEXT("Cloth");
        case EMaterialShadingModel::Eye:                return TEXT("Eye");
        case EMaterialShadingModel::SingleLayerWater:   return TEXT("SingleLayerWater");
        case EMaterialShadingModel::ThinTranslucent:    return TEXT("ThinTranslucent");
        default:                                        return TEXT("Unknown");
    }
}

} // namespace MonsterEngine
