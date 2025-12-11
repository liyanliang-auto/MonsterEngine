// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file LightShaderParameters.h
 * @brief Light shader parameter structures for GPU uniform buffers
 * 
 * This file defines the light data structures that are passed to shaders.
 * Following UE5's LightData.ush and LightShaderParameters.ush architecture.
 * 
 * Reference UE5 files:
 * - Engine/Shaders/Private/LightData.ush
 * - Engine/Shaders/Private/LightShaderParameters.ush
 * - Engine/Source/Runtime/Renderer/Private/LightRendering.h
 */

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "Math/Vector4.h"
#include "Math/MathFunctions.h"
#include "Core/Color.h"

namespace MonsterEngine
{

// ============================================================================
// Light Type Constants
// ============================================================================

/**
 * Light type constants matching shader defines
 * These must match the values in LightingCommon.ush
 */
namespace ELightTypeShader
{
    constexpr uint32 Directional = 0;
    constexpr uint32 Point       = 1;
    constexpr uint32 Spot        = 2;
    constexpr uint32 Rect        = 3;
    constexpr uint32 Max         = 4;
}

/** Maximum number of local lights supported in a single draw call */
constexpr int32 MAX_LOCAL_LIGHTS = 256;

/** Maximum number of lights per tile/cluster for tiled/clustered deferred */
constexpr int32 MAX_LIGHTS_PER_TILE = 32;

/** Lighting channel mask for all channels */
constexpr uint32 LIGHTING_CHANNEL_MASK = 0x7;

// ============================================================================
// FLightShaderParameters - Lightweight light parameters for shaders
// ============================================================================

/**
 * Lightweight light shader parameters
 * 
 * This structure contains the essential light parameters needed for
 * lighting calculations in shaders. It is designed to be compact
 * and GPU-friendly for use in uniform buffers.
 * 
 * Memory layout is optimized for GPU access (16-byte aligned vectors).
 * Total size: 112 bytes (7 x float4)
 * 
 * Reference UE5: FLightShaderParameters in LightShaderParameters.ush
 */
struct alignas(16) FLightShaderParameters
{
    // ========================================================================
    // Position and Radius (float4)
    // ========================================================================
    
    /** 
     * World position of the light (translated for camera-relative rendering)
     * For directional lights, this is unused (set to 0)
     */
    FVector3f TranslatedWorldPosition;
    
    /** 
     * Inverse of the attenuation radius (1.0 / Radius)
     * Used for efficient distance-based attenuation calculation
     * For directional lights, this is 0
     */
    float InvRadius;

    // ========================================================================
    // Color and Falloff (float4)
    // ========================================================================
    
    /** 
     * Light color pre-multiplied with intensity
     * Can be > 1.0 for HDR lighting
     */
    FVector3f Color;
    
    /** 
     * Falloff exponent for custom attenuation curves
     * 0 = inverse squared falloff (physically correct)
     * > 0 = pow(1 - (d/r)^4, FalloffExponent) * (1 / (d^2 + 1))
     */
    float FalloffExponent;

    // ========================================================================
    // Direction and Specular Scale (float4)
    // ========================================================================
    
    /** 
     * Normalized direction the light is pointing
     * For directional lights: direction TO the light (negated light direction)
     * For spot lights: direction the cone is pointing
     */
    FVector3f Direction;
    
    /** 
     * Scale factor for specular contribution
     * 0 = no specular, 1 = full specular
     */
    float SpecularScale;

    // ========================================================================
    // Tangent and Source Radius (float4)
    // ========================================================================
    
    /** 
     * Tangent vector for rect lights
     * Defines the orientation of the rectangular light source
     */
    FVector3f Tangent;
    
    /** 
     * Radius of the light source for area light approximation
     * Used for soft shadows and specular highlight size
     */
    float SourceRadius;

    // ========================================================================
    // Spot Angles and Source Parameters (float4)
    // ========================================================================
    
    /** 
     * Spot light cone angles:
     * X = cos(InnerConeAngle)
     * Y = 1.0 / (cos(InnerConeAngle) - cos(OuterConeAngle))
     */
    FVector2f SpotAngles;
    
    /** 
     * Soft source radius for penumbra calculation
     */
    float SoftSourceRadius;
    
    /** 
     * Length of the light source (for capsule/tube lights)
     */
    float SourceLength;

    // ========================================================================
    // Rect Light Parameters (float4)
    // ========================================================================
    
    /** Barn door cosine angle for rect lights */
    float RectLightBarnCosAngle;
    
    /** Barn door length for rect lights */
    float RectLightBarnLength;
    
    /** UV offset in the rect light texture atlas */
    FVector2f RectLightAtlasUVOffset;

    // ========================================================================
    // Additional Parameters (float4)
    // ========================================================================
    
    /** UV scale in the rect light texture atlas */
    FVector2f RectLightAtlasUVScale;
    
    /** Maximum mip level in the rect light atlas */
    float RectLightAtlasMaxLevel;
    
    /** Index into the IES profile texture atlas */
    float IESAtlasIndex;

    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor - initializes to a disabled light */
    FLightShaderParameters()
        : TranslatedWorldPosition(0.0f, 0.0f, 0.0f)
        , InvRadius(0.0f)
        , Color(0.0f, 0.0f, 0.0f)
        , FalloffExponent(0.0f)
        , Direction(0.0f, 0.0f, -1.0f)
        , SpecularScale(1.0f)
        , Tangent(1.0f, 0.0f, 0.0f)
        , SourceRadius(0.0f)
        , SpotAngles(0.0f, 1.0f)
        , SoftSourceRadius(0.0f)
        , SourceLength(0.0f)
        , RectLightBarnCosAngle(0.0f)
        , RectLightBarnLength(0.0f)
        , RectLightAtlasUVOffset(0.0f, 0.0f)
        , RectLightAtlasUVScale(1.0f, 1.0f)
        , RectLightAtlasMaxLevel(0.0f)
        , IESAtlasIndex(-1.0f)
    {
    }

    // ========================================================================
    // Helper Methods
    // ========================================================================

    /** Check if this light is enabled (has non-zero color) */
    bool IsEnabled() const
    {
        return Color.X > 0.0f || Color.Y > 0.0f || Color.Z > 0.0f;
    }

    /** Check if this is an inverse squared falloff light */
    bool IsInverseSquaredFalloff() const
    {
        return FalloffExponent == 0.0f;
    }

    /** Get the attenuation radius */
    float GetRadius() const
    {
        return InvRadius > 0.0f ? 1.0f / InvRadius : 0.0f;
    }

    /** Set the attenuation radius */
    void SetRadius(float Radius)
    {
        InvRadius = Radius > 0.0f ? 1.0f / Radius : 0.0f;
    }
};

// Verify size for GPU alignment
static_assert(sizeof(FLightShaderParameters) == 112, "FLightShaderParameters size mismatch");

// ============================================================================
// FDirectionalLightShaderParameters - Directional light specific parameters
// ============================================================================

/**
 * Shader parameters specific to directional lights
 * 
 * Directional lights have special properties like shadow cascades
 * and atmosphere interaction that require additional parameters.
 * 
 * Total size: 48 bytes (3 x float4)
 * 
 * Reference UE5: FDirectionalLightData in LightData.ush
 */
struct alignas(16) FDirectionalLightShaderParameters
{
    // ========================================================================
    // Basic Properties (float4)
    // ========================================================================
    
    /** Whether a directional light is present (1 = yes, 0 = no) */
    uint32 HasDirectionalLight;
    
    /** Shadow map channel mask for static shadows */
    uint32 DirectionalLightShadowMapChannelMask;
    
    /** Distance fade parameters: X = 1/(FadeEnd-FadeStart), Y = -FadeStart/(FadeEnd-FadeStart) */
    FVector2f DirectionalLightDistanceFadeMAD;

    // ========================================================================
    // Color and Direction (float4 + float4)
    // ========================================================================
    
    /** Light color pre-multiplied with intensity */
    FVector3f DirectionalLightColor;
    
    /** Padding for alignment */
    float Padding0;
    
    /** Normalized direction TO the light (negated light direction) */
    FVector3f DirectionalLightDirection;
    
    /** Angular radius of the light source (for soft shadows) */
    float DirectionalLightSourceRadius;

    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor */
    FDirectionalLightShaderParameters()
        : HasDirectionalLight(0)
        , DirectionalLightShadowMapChannelMask(0)
        , DirectionalLightDistanceFadeMAD(0.0f, 0.0f)
        , DirectionalLightColor(0.0f, 0.0f, 0.0f)
        , Padding0(0.0f)
        , DirectionalLightDirection(0.0f, 0.0f, -1.0f)
        , DirectionalLightSourceRadius(0.0f)
    {
    }

    /** Check if directional light is enabled */
    bool IsEnabled() const
    {
        return HasDirectionalLight != 0;
    }
};

// Verify size for GPU alignment
static_assert(sizeof(FDirectionalLightShaderParameters) == 48, "FDirectionalLightShaderParameters size mismatch");

// ============================================================================
// FDeferredLightData - Full light data for deferred lighting
// ============================================================================

/**
 * Complete light data for deferred lighting calculations
 * 
 * This structure contains all the data needed for full-featured
 * deferred lighting including shadows, contact shadows, and area lights.
 * 
 * This is the CPU-side representation; a GPU-friendly version is
 * packed into FDeferredLightUniformBuffer.
 * 
 * Reference UE5: FDeferredLightData in LightData.ush
 */
struct FDeferredLightData
{
    // ========================================================================
    // Position and Attenuation
    // ========================================================================
    
    /** World position (translated for camera-relative rendering) */
    FVector3f TranslatedWorldPosition;
    
    /** Inverse attenuation radius */
    float InvRadius;
    
    /** Light color pre-multiplied with intensity (can be HDR) */
    FVector3f Color;
    
    /** Falloff exponent (0 = inverse squared) */
    float FalloffExponent;

    // ========================================================================
    // Direction and Orientation
    // ========================================================================
    
    /** Light direction (normalized) */
    FVector3f Direction;
    
    /** Tangent for rect lights */
    FVector3f Tangent;

    // ========================================================================
    // Area Light Parameters
    // ========================================================================
    
    /** Source radius for area light approximation */
    float SourceRadius;
    
    /** Soft source radius for penumbra */
    float SoftSourceRadius;
    
    /** Source length for capsule lights */
    float SourceLength;
    
    /** Specular contribution scale */
    float SpecularScale;

    // ========================================================================
    // Spot Light Parameters
    // ========================================================================
    
    /** 
     * Spot angles:
     * X = cos(InnerConeAngle)
     * Y = 1.0 / (cos(InnerConeAngle) - cos(OuterConeAngle))
     */
    FVector2f SpotAngles;

    // ========================================================================
    // Contact Shadow Parameters
    // ========================================================================
    
    /** Length of contact shadow ray */
    float ContactShadowLength;
    
    /** Intensity of contact shadows for shadow-casting lights */
    float ContactShadowCastingIntensity;
    
    /** Intensity of contact shadows for non-shadow-casting lights */
    float ContactShadowNonCastingIntensity;
    
    /** Whether contact shadow length is in world space (true) or screen space (false) */
    bool bContactShadowLengthInWS;

    // ========================================================================
    // Shadow Parameters
    // ========================================================================
    
    /** Distance fade parameters for shadows */
    FVector2f DistanceFadeMAD;
    
    /** Shadow map channel mask (4 channels packed) */
    FVector4f ShadowMapChannelMask;
    
    /** Shadow flags (bit 0 = has shadows) */
    uint32 ShadowedBits;

    // ========================================================================
    // Light Type Flags
    // ========================================================================
    
    /** Whether to use inverse squared falloff */
    bool bInverseSquared;
    
    /** Whether this is a radial light (point/spot) vs directional */
    bool bRadialLight;
    
    /** Whether this is a spot light */
    bool bSpotLight;
    
    /** Whether this is a rect light */
    bool bRectLight;

    // ========================================================================
    // Rect Light Data
    // ========================================================================
    
    /** Barn door cosine angle */
    float RectLightBarnCosAngle;
    
    /** Barn door length */
    float RectLightBarnLength;
    
    /** Atlas UV offset */
    FVector2f RectLightAtlasUVOffset;
    
    /** Atlas UV scale */
    FVector2f RectLightAtlasUVScale;
    
    /** Atlas max mip level */
    float RectLightAtlasMaxLevel;

    // ========================================================================
    // IES Profile
    // ========================================================================
    
    /** Index into IES texture atlas (-1 = no IES) */
    float IESAtlasIndex;

    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor */
    FDeferredLightData()
        : TranslatedWorldPosition(0.0f, 0.0f, 0.0f)
        , InvRadius(0.0f)
        , Color(0.0f, 0.0f, 0.0f)
        , FalloffExponent(0.0f)
        , Direction(0.0f, 0.0f, -1.0f)
        , Tangent(1.0f, 0.0f, 0.0f)
        , SourceRadius(0.0f)
        , SoftSourceRadius(0.0f)
        , SourceLength(0.0f)
        , SpecularScale(1.0f)
        , SpotAngles(0.0f, 1.0f)
        , ContactShadowLength(0.0f)
        , ContactShadowCastingIntensity(1.0f)
        , ContactShadowNonCastingIntensity(0.0f)
        , bContactShadowLengthInWS(false)
        , DistanceFadeMAD(0.0f, 0.0f)
        , ShadowMapChannelMask(0.0f, 0.0f, 0.0f, 0.0f)
        , ShadowedBits(0)
        , bInverseSquared(true)
        , bRadialLight(true)
        , bSpotLight(false)
        , bRectLight(false)
        , RectLightBarnCosAngle(0.0f)
        , RectLightBarnLength(0.0f)
        , RectLightAtlasUVOffset(0.0f, 0.0f)
        , RectLightAtlasUVScale(1.0f, 1.0f)
        , RectLightAtlasMaxLevel(0.0f)
        , IESAtlasIndex(-1.0f)
    {
    }

    // ========================================================================
    // Conversion Methods
    // ========================================================================

    /** Convert to lightweight FLightShaderParameters */
    FLightShaderParameters ToLightShaderParameters() const
    {
        FLightShaderParameters Params;
        Params.TranslatedWorldPosition = TranslatedWorldPosition;
        Params.InvRadius = InvRadius;
        Params.Color = Color;
        Params.FalloffExponent = FalloffExponent;
        Params.Direction = Direction;
        Params.SpecularScale = SpecularScale;
        Params.Tangent = Tangent;
        Params.SourceRadius = SourceRadius;
        Params.SpotAngles = SpotAngles;
        Params.SoftSourceRadius = SoftSourceRadius;
        Params.SourceLength = SourceLength;
        Params.RectLightBarnCosAngle = RectLightBarnCosAngle;
        Params.RectLightBarnLength = RectLightBarnLength;
        Params.RectLightAtlasUVOffset = RectLightAtlasUVOffset;
        Params.RectLightAtlasUVScale = RectLightAtlasUVScale;
        Params.RectLightAtlasMaxLevel = RectLightAtlasMaxLevel;
        Params.IESAtlasIndex = IESAtlasIndex;
        return Params;
    }

    /** Check if light has shadows enabled */
    bool HasShadows() const
    {
        return (ShadowedBits & 0x1) != 0;
    }

    /** Get the light type as shader constant */
    uint32 GetLightType() const
    {
        if (!bRadialLight) return ELightTypeShader::Directional;
        if (bRectLight) return ELightTypeShader::Rect;
        if (bSpotLight) return ELightTypeShader::Spot;
        return ELightTypeShader::Point;
    }
};

// ============================================================================
// FSimpleLightData - Simplified light data for particles/simple shading
// ============================================================================

/**
 * Simplified light data for simple shading models
 * 
 * Used for particle lighting and other cases where full
 * deferred lighting features are not needed.
 * 
 * Reference UE5: FSimpleDeferredLightData in LightData.ush
 */
struct alignas(16) FSimpleLightData
{
    /** World position */
    FVector3f TranslatedWorldPosition;
    
    /** Inverse radius */
    float InvRadius;
    
    /** Light color */
    FVector3f Color;
    
    /** Falloff exponent */
    float FalloffExponent;
    
    /** Whether to use inverse squared falloff */
    bool bInverseSquared;
    
    /** Padding for alignment */
    uint8 Padding[3];

    /** Default constructor */
    FSimpleLightData()
        : TranslatedWorldPosition(0.0f, 0.0f, 0.0f)
        , InvRadius(0.0f)
        , Color(0.0f, 0.0f, 0.0f)
        , FalloffExponent(0.0f)
        , bInverseSquared(true)
    {
        Padding[0] = Padding[1] = Padding[2] = 0;
    }
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Pack shadow map channel mask into a single uint32
 * @param Channel0 First shadow map channel
 * @param Channel1 Second shadow map channel
 * @param Channel2 Third shadow map channel
 * @param Channel3 Fourth shadow map channel
 * @return Packed channel mask
 */
inline uint32 PackShadowMapChannelMask(bool Channel0, bool Channel1, bool Channel2, bool Channel3)
{
    return (Channel0 ? 0x1 : 0) | 
           (Channel1 ? 0x2 : 0) | 
           (Channel2 ? 0x4 : 0) | 
           (Channel3 ? 0x8 : 0);
}

/**
 * Unpack shadow map channel mask from uint32
 * @param Packed Packed channel mask
 * @return FVector4f with each component being 0 or 1
 */
inline FVector4f UnpackShadowMapChannelMask(uint32 Packed)
{
    return FVector4f(
        (Packed & 0x1) ? 1.0f : 0.0f,
        (Packed & 0x2) ? 1.0f : 0.0f,
        (Packed & 0x4) ? 1.0f : 0.0f,
        (Packed & 0x8) ? 1.0f : 0.0f
    );
}

/**
 * Calculate spot light attenuation angles from cone angles
 * @param InnerConeAngleRadians Inner cone angle in radians
 * @param OuterConeAngleRadians Outer cone angle in radians
 * @return FVector2f with X = cos(inner), Y = 1/(cos(inner) - cos(outer))
 */
inline FVector2f CalculateSpotAngles(float InnerConeAngleRadians, float OuterConeAngleRadians)
{
    float CosInner = std::cos(InnerConeAngleRadians);
    float CosOuter = std::cos(OuterConeAngleRadians);
    float InvAngleRange = 1.0f / FMath::Max(0.001f, CosInner - CosOuter);
    return FVector2f(CosInner, InvAngleRange);
}

} // namespace MonsterEngine
