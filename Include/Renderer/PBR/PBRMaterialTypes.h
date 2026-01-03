// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file PBRMaterialTypes.h
 * @brief PBR material data structures for GPU uniform buffers
 * 
 * This file defines the PBR material parameter structures that are passed to shaders.
 * Following Google Filament's MaterialInputs and UE5's material parameter architecture.
 * 
 * Reference:
 * - Filament: shaders/src/material_inputs.fs
 * - UE5: Engine/Source/Runtime/Engine/Public/MaterialShared.h
 * 
 * Descriptor Set Layout:
 * - Set 0: Per-Frame data (Camera, Lighting)
 * - Set 1: Per-Material data (Textures, Material parameters)
 * - Set 2: Per-Object data (Transform)
 */

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Matrix.h"

namespace MonsterEngine
{

// Forward declarations
class FTexture;
class FTexture2D;

namespace Renderer
{

// ============================================================================
// PBR Material Constants
// ============================================================================

/**
 * PBR material constants matching shader defines
 * Reference: Filament common_material.fs
 */
namespace PBRConstants
{
    /** Minimum perceptual roughness to avoid division by zero in BRDF */
    constexpr float MIN_PERCEPTUAL_ROUGHNESS = 0.045f;
    
    /** Minimum roughness (MIN_PERCEPTUAL_ROUGHNESS^2) */
    constexpr float MIN_ROUGHNESS = 0.002025f;
    
    /** Minimum N dot V to avoid artifacts at grazing angles */
    constexpr float MIN_N_DOT_V = 1e-4f;
    
    /** Default reflectance for dielectric materials (4% at normal incidence) */
    constexpr float DEFAULT_REFLECTANCE = 0.5f;
    
    /** Default IOR for dielectric materials (1.5 = glass) */
    constexpr float DEFAULT_IOR = 1.5f;
    
    /** Maximum number of PBR textures per material */
    constexpr uint32 MAX_PBR_TEXTURES = 8;
}

// ============================================================================
// EPBRTextureSlot - PBR Texture Slot Enumeration
// ============================================================================

/**
 * @enum EPBRTextureSlot
 * @brief Texture slots for PBR materials
 * 
 * Defines the binding order for PBR textures in descriptor set 1.
 * Reference: glTF 2.0 PBR material model
 */
enum class EPBRTextureSlot : uint8
{
    BaseColor = 0,          // RGB: Base color, A: Alpha
    MetallicRoughness = 1,  // R: unused, G: Roughness, B: Metallic
    Normal = 2,             // RGB: Tangent-space normal map
    Occlusion = 3,          // R: Ambient occlusion
    Emissive = 4,           // RGB: Emissive color
    
    // Extended slots for advanced materials
    ClearCoat = 5,          // R: Clear coat intensity
    ClearCoatRoughness = 6, // R: Clear coat roughness
    Anisotropy = 7,         // RG: Anisotropy direction, B: Anisotropy strength
    
    Count
};

// ============================================================================
// FPBRMaterialParams - GPU Uniform Buffer Structure
// ============================================================================

/**
 * @struct FPBRMaterialParams
 * @brief PBR material parameters for GPU uniform buffer
 * 
 * This structure is designed to be uploaded directly to GPU as a uniform buffer.
 * Memory layout is optimized for GPU access (16-byte aligned).
 * Total size: 80 bytes (5 x float4)
 * 
 * Reference:
 * - Filament: MaterialInputs struct
 * - UE5: FMaterialUniformExpressionSet
 */
struct alignas(16) FPBRMaterialParams
{
    // ========================================================================
    // Base Color and Alpha (float4) - 16 bytes
    // ========================================================================
    
    /**
     * Base color (albedo) of the material
     * RGB: Linear color, A: Alpha/Opacity
     * Default: (1.0, 1.0, 1.0, 1.0) = white, fully opaque
     */
    FVector4f BaseColorFactor;
    
    // ========================================================================
    // Metallic, Roughness, Reflectance, AO (float4) - 16 bytes
    // ========================================================================
    
    /**
     * Metallic factor [0, 1]
     * 0 = dielectric (non-metal), 1 = metal
     * Default: 0.0
     */
    float MetallicFactor;
    
    /**
     * Perceptual roughness factor [0, 1]
     * 0 = smooth/mirror, 1 = rough/diffuse
     * Note: Actual roughness = perceptualRoughness^2
     * Default: 1.0
     */
    float RoughnessFactor;
    
    /**
     * Reflectance at normal incidence for dielectrics [0, 1]
     * Maps to F0 = 0.16 * reflectance^2
     * Default: 0.5 (4% reflectance, typical for plastics)
     */
    float Reflectance;
    
    /**
     * Ambient occlusion factor [0, 1]
     * 0 = fully occluded, 1 = no occlusion
     * Default: 1.0
     */
    float AmbientOcclusion;
    
    // ========================================================================
    // Emissive Color and Intensity (float4) - 16 bytes
    // ========================================================================
    
    /**
     * Emissive color (HDR)
     * RGB: Linear emissive color, can be > 1.0 for HDR
     * Default: (0.0, 0.0, 0.0)
     */
    FVector3f EmissiveFactor;
    
    /**
     * Emissive intensity multiplier
     * Allows for HDR emissive without changing color
     * Default: 1.0
     */
    float EmissiveIntensity;
    
    // ========================================================================
    // Clear Coat and Flags (float4) - 16 bytes
    // ========================================================================
    
    /**
     * Clear coat intensity [0, 1]
     * 0 = no clear coat, 1 = full clear coat
     * Default: 0.0
     */
    float ClearCoat;
    
    /**
     * Clear coat roughness [0, 1]
     * Default: 0.0 (smooth clear coat)
     */
    float ClearCoatRoughness;
    
    /**
     * Alpha cutoff for masked materials [0, 1]
     * Pixels with alpha < cutoff are discarded
     * Default: 0.5
     */
    float AlphaCutoff;
    
    /**
     * Material flags (packed as float for GPU compatibility)
     * Bit 0: HasBaseColorTexture
     * Bit 1: HasMetallicRoughnessTexture
     * Bit 2: HasNormalTexture
     * Bit 3: HasOcclusionTexture
     * Bit 4: HasEmissiveTexture
     * Bit 5: UseAlphaMask
     * Bit 6: DoubleSided
     * Bit 7: HasClearCoat
     */
    float MaterialFlags;
    
    // ========================================================================
    // Extended Parameters (float4) - 16 bytes
    // ========================================================================
    
    /**
     * Normal map scale factor [0, 2]
     * Controls the strength of normal map effect
     * Default: 1.0
     */
    float NormalScale;
    
    /**
     * Occlusion texture strength [0, 1]
     * Controls how much the occlusion texture affects the final result
     * Default: 1.0
     */
    float OcclusionStrength;
    
    /**
     * Index of refraction for dielectric materials
     * Default: 1.5 (glass)
     */
    float IOR;
    
    /**
     * Padding for 16-byte alignment
     */
    float _Padding0;
    
    // ========================================================================
    // Constructors and Helpers
    // ========================================================================
    
    /** Default constructor with PBR defaults */
    FPBRMaterialParams()
        : BaseColorFactor(1.0f, 1.0f, 1.0f, 1.0f)
        , MetallicFactor(0.0f)
        , RoughnessFactor(1.0f)
        , Reflectance(PBRConstants::DEFAULT_REFLECTANCE)
        , AmbientOcclusion(1.0f)
        , EmissiveFactor(0.0f, 0.0f, 0.0f)
        , EmissiveIntensity(1.0f)
        , ClearCoat(0.0f)
        , ClearCoatRoughness(0.0f)
        , AlphaCutoff(0.5f)
        , MaterialFlags(0.0f)
        , NormalScale(1.0f)
        , OcclusionStrength(1.0f)
        , IOR(PBRConstants::DEFAULT_IOR)
        , _Padding0(0.0f)
    {}
    
    /** Set material flag bit */
    void setFlag(uint32 bit, bool value)
    {
        uint32 flags = static_cast<uint32>(MaterialFlags);
        if (value) {
            flags |= (1u << bit);
        } else {
            flags &= ~(1u << bit);
        }
        MaterialFlags = static_cast<float>(flags);
    }
    
    /** Get material flag bit */
    bool getFlag(uint32 bit) const
    {
        uint32 flags = static_cast<uint32>(MaterialFlags);
        return (flags & (1u << bit)) != 0;
    }
    
    // Flag accessors
    void setHasBaseColorTexture(bool v)           { setFlag(0, v); }
    void setHasMetallicRoughnessTexture(bool v)   { setFlag(1, v); }
    void setHasNormalTexture(bool v)              { setFlag(2, v); }
    void setHasOcclusionTexture(bool v)           { setFlag(3, v); }
    void setHasEmissiveTexture(bool v)            { setFlag(4, v); }
    void setUseAlphaMask(bool v)                  { setFlag(5, v); }
    void setDoubleSided(bool v)                   { setFlag(6, v); }
    void setHasClearCoat(bool v)                  { setFlag(7, v); }
    
    bool hasBaseColorTexture() const              { return getFlag(0); }
    bool hasMetallicRoughnessTexture() const      { return getFlag(1); }
    bool hasNormalTexture() const                 { return getFlag(2); }
    bool hasOcclusionTexture() const              { return getFlag(3); }
    bool hasEmissiveTexture() const               { return getFlag(4); }
    bool useAlphaMask() const                     { return getFlag(5); }
    bool isDoubleSided() const                    { return getFlag(6); }
    bool hasClearCoat() const                     { return getFlag(7); }
};

// Verify struct size for GPU alignment
static_assert(sizeof(FPBRMaterialParams) == 80, "FPBRMaterialParams must be 80 bytes");
static_assert(alignof(FPBRMaterialParams) == 16, "FPBRMaterialParams must be 16-byte aligned");

// ============================================================================
// FPBRMaterialTextures - Material Texture References
// ============================================================================

/**
 * @struct FPBRMaterialTextures
 * @brief CPU-side texture references for PBR materials
 * 
 * This structure holds texture pointers for binding to descriptor sets.
 * Not uploaded to GPU directly - used for descriptor set updates.
 */
struct FPBRMaterialTextures
{
    /** Base color texture (sRGB) */
    FTexture2D* BaseColorTexture = nullptr;
    
    /** Metallic-Roughness texture (Linear) */
    FTexture2D* MetallicRoughnessTexture = nullptr;
    
    /** Normal map texture (Linear) */
    FTexture2D* NormalTexture = nullptr;
    
    /** Ambient occlusion texture (Linear) */
    FTexture2D* OcclusionTexture = nullptr;
    
    /** Emissive texture (sRGB) */
    FTexture2D* EmissiveTexture = nullptr;
    
    /** Clear coat texture (Linear) */
    FTexture2D* ClearCoatTexture = nullptr;
    
    /** Clear coat roughness texture (Linear) */
    FTexture2D* ClearCoatRoughnessTexture = nullptr;
    
    /** Default constructor */
    FPBRMaterialTextures() = default;
    
    /** Check if any texture is set */
    bool hasAnyTexture() const
    {
        return BaseColorTexture != nullptr ||
               MetallicRoughnessTexture != nullptr ||
               NormalTexture != nullptr ||
               OcclusionTexture != nullptr ||
               EmissiveTexture != nullptr ||
               ClearCoatTexture != nullptr ||
               ClearCoatRoughnessTexture != nullptr;
    }
    
    /** Get texture by slot */
    FTexture2D* getTextureBySlot(EPBRTextureSlot slot) const
    {
        switch (slot) {
            case EPBRTextureSlot::BaseColor:           return BaseColorTexture;
            case EPBRTextureSlot::MetallicRoughness:   return MetallicRoughnessTexture;
            case EPBRTextureSlot::Normal:              return NormalTexture;
            case EPBRTextureSlot::Occlusion:           return OcclusionTexture;
            case EPBRTextureSlot::Emissive:            return EmissiveTexture;
            case EPBRTextureSlot::ClearCoat:           return ClearCoatTexture;
            case EPBRTextureSlot::ClearCoatRoughness:  return ClearCoatRoughnessTexture;
            default:                                    return nullptr;
        }
    }
};

} // namespace Renderer
} // namespace MonsterEngine
