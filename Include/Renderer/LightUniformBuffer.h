// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file LightUniformBuffer.h
 * @brief Light uniform buffer structures for GPU
 * 
 * This file defines the uniform buffer structures used to pass light data
 * to the GPU. These structures are designed to be directly mapped to
 * shader uniform buffers with proper alignment.
 * 
 * Reference UE5 files:
 * - Engine/Source/Runtime/Renderer/Private/LightRendering.h (FDeferredLightUniformStruct)
 * - Engine/Shaders/Private/LightData.ush (FLocalLightData)
 */

#include "LightShaderParameters.h"
#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"

namespace MonsterEngine
{

// ============================================================================
// Forward Declarations
// ============================================================================

class FLightSceneProxy;

// ============================================================================
// FLocalLightData - Compact light data for light grid/clustering
// ============================================================================

/**
 * Compact light data used in light grid for clustered/tiled deferred
 * 
 * This structure is optimized for GPU access and packs light data
 * into 6 float4 values for efficient memory bandwidth.
 * 
 * Total size: 96 bytes (6 x float4)
 * 
 * Reference UE5: FLocalLightData in LightData.ush
 */
struct alignas(16) FLocalLightData
{
    // ========================================================================
    // Packed Data (6 x float4 = 96 bytes)
    // ========================================================================
    
    /** 
     * float4[0]: Position and inverse radius
     * xyz = World position
     * w = 1.0 / AttenuationRadius
     */
    FVector4f LightPositionAndInvRadius;
    
    /** 
     * float4[1]: Color and falloff
     * xyz = Light color (pre-multiplied with intensity)
     * w = Falloff exponent (0 = inverse squared)
     */
    FVector4f LightColorAndFalloffExponent;
    
    /** 
     * float4[2]: Direction and shadow mask
     * xyz = Light direction (normalized)
     * w = Packed shadow map channel mask and light type
     *     bits [3:0] = ShadowMapChannelMask
     *     bits [7:4] = PreviewShadowMapChannel
     *     bits [10:8] = LightingChannelMask
     *     bits [17:16] = LightType
     *     bit 18 = bCastShadow
     */
    FVector4f LightDirectionAndShadowMask;
    
    /** 
     * float4[3]: Spot angles, light ID, and source radius
     * x = Packed spot angles (half2: cos(inner), 1/(cos(inner)-cos(outer)))
     * y = Light scene ID
     * z = Packed source radius and soft source radius (half2)
     * w = Packed source length and volumetric scattering intensity (half2)
     */
    FVector4f SpotAnglesAndIdAndSourceRadiusPacked;
    
    /** 
     * float4[4]: Tangent, IES data, and specular scale
     * xyz = Tangent vector for rect lights
     * w = Packed IES atlas index and specular scale (half2)
     */
    FVector4f LightTangentAndIESDataAndSpecularScale;
    
    /** 
     * float4[5]: Rect light data and virtual shadow map ID
     * x = Packed rect light atlas UV offset (half2)
     * y = Packed rect light atlas UV scale (half2)
     * z = Virtual shadow map ID
     * w = Packed barn length, barn cos angle, and atlas max level
     */
    FVector4f RectDataAndVirtualShadowMapId;

    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor */
    FLocalLightData()
    {
        LightPositionAndInvRadius = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        LightColorAndFalloffExponent = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        LightDirectionAndShadowMask = FVector4f(0.0f, 0.0f, -1.0f, 0.0f);
        SpotAnglesAndIdAndSourceRadiusPacked = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        LightTangentAndIESDataAndSpecularScale = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
        RectDataAndVirtualShadowMapId = FVector4f(0.0f, 0.0f, -1.0f, 0.0f);
    }

    // ========================================================================
    // Packing/Unpacking Helpers
    // ========================================================================

    /** Pack two half floats into a uint32 */
    static uint32 PackHalf2(float A, float B)
    {
        // Simple half-float packing (IEEE 754 half precision)
        auto FloatToHalf = [](float Value) -> uint16
        {
            // Simplified conversion - for production use proper half-float conversion
            if (Value == 0.0f) return 0;
            
            uint32 Bits = *reinterpret_cast<uint32*>(&Value);
            uint32 Sign = (Bits >> 16) & 0x8000;
            int32 Exponent = ((Bits >> 23) & 0xFF) - 127 + 15;
            uint32 Mantissa = (Bits >> 13) & 0x3FF;
            
            if (Exponent <= 0)
            {
                return static_cast<uint16>(Sign);
            }
            else if (Exponent >= 31)
            {
                return static_cast<uint16>(Sign | 0x7C00);
            }
            
            return static_cast<uint16>(Sign | (Exponent << 10) | Mantissa);
        };
        
        return static_cast<uint32>(FloatToHalf(A)) | 
               (static_cast<uint32>(FloatToHalf(B)) << 16);
    }

    /** Unpack uint32 to two half floats */
    static void UnpackHalf2(uint32 Packed, float& OutA, float& OutB)
    {
        auto HalfToFloat = [](uint16 Value) -> float
        {
            if (Value == 0) return 0.0f;
            
            uint32 Sign = (Value & 0x8000) << 16;
            int32 Exponent = ((Value >> 10) & 0x1F) - 15 + 127;
            uint32 Mantissa = (Value & 0x3FF) << 13;
            
            if (Exponent <= 0)
            {
                return 0.0f;
            }
            else if (Exponent >= 255)
            {
                uint32 Bits = Sign | 0x7F800000;
                return *reinterpret_cast<float*>(&Bits);
            }
            
            uint32 Bits = Sign | (Exponent << 23) | Mantissa;
            return *reinterpret_cast<float*>(&Bits);
        };
        
        OutA = HalfToFloat(static_cast<uint16>(Packed & 0xFFFF));
        OutB = HalfToFloat(static_cast<uint16>(Packed >> 16));
    }

    /** Pack light type and shadow info into uint32 */
    static uint32 PackLightTypeAndShadowMask(
        uint32 ShadowMapChannelMask,
        uint32 PreviewShadowMapChannel,
        uint32 LightingChannelMask,
        uint32 LightType,
        bool bCastShadow)
    {
        return (ShadowMapChannelMask & 0xF) |
               ((PreviewShadowMapChannel & 0xF) << 4) |
               ((LightingChannelMask & 0x7) << 8) |
               ((LightType & 0x3) << 16) |
               (bCastShadow ? (1 << 18) : 0);
    }

    /** Unpack light type from packed value */
    static uint32 UnpackLightType(uint32 Packed)
    {
        return (Packed >> 16) & 0x3;
    }

    /** Unpack cast shadow flag from packed value */
    static bool UnpackCastShadow(uint32 Packed)
    {
        return ((Packed >> 18) & 0x1) != 0;
    }

    /** Unpack lighting channel mask from packed value */
    static uint32 UnpackLightingChannelMask(uint32 Packed)
    {
        return (Packed >> 8) & 0x7;
    }
};

// Verify size for GPU alignment
static_assert(sizeof(FLocalLightData) == 96, "FLocalLightData size mismatch");

// ============================================================================
// FDeferredLightUniformBuffer - Single light uniform buffer
// ============================================================================

/**
 * Uniform buffer structure for a single deferred light
 * 
 * This structure is used for the per-light uniform buffer in
 * deferred lighting passes. It contains all parameters needed
 * for full-featured lighting calculations.
 * 
 * Total size: 192 bytes (12 x float4)
 * 
 * Reference UE5: FDeferredLightUniformStruct in LightRendering.h
 */
struct alignas(16) FDeferredLightUniformBuffer
{
    // ========================================================================
    // Position and Radius (float4)
    // ========================================================================
    
    /** xyz = Position, w = InvRadius */
    FVector4f LightPositionAndInvRadius;

    // ========================================================================
    // Color and Falloff (float4)
    // ========================================================================
    
    /** xyz = Color, w = FalloffExponent */
    FVector4f LightColorAndFalloffExponent;

    // ========================================================================
    // Direction and Specular (float4)
    // ========================================================================
    
    /** xyz = Direction, w = SpecularScale */
    FVector4f LightDirectionAndSpecularScale;

    // ========================================================================
    // Tangent and Source Radius (float4)
    // ========================================================================
    
    /** xyz = Tangent, w = SourceRadius */
    FVector4f LightTangentAndSourceRadius;

    // ========================================================================
    // Spot Angles and Source Parameters (float4)
    // ========================================================================
    
    /** x = SpotAngles.x, y = SpotAngles.y, z = SoftSourceRadius, w = SourceLength */
    FVector4f SpotAnglesAndSourceLength;

    // ========================================================================
    // Shadow Parameters (float4)
    // ========================================================================
    
    /** Shadow map channel mask (4 channels) */
    FVector4f ShadowMapChannelMask;

    // ========================================================================
    // Distance Fade and Flags (float4)
    // ========================================================================
    
    /** x = DistanceFadeMAD.x, y = DistanceFadeMAD.y, z = ContactShadowLength, w = Flags */
    FVector4f DistanceFadeAndFlags;

    // ========================================================================
    // Contact Shadow Parameters (float4)
    // ========================================================================
    
    /** x = ContactShadowCastingIntensity, y = ContactShadowNonCastingIntensity, z = unused, w = unused */
    FVector4f ContactShadowParams;

    // ========================================================================
    // Rect Light Parameters (float4 x 2)
    // ========================================================================
    
    /** x = BarnCosAngle, y = BarnLength, z = AtlasUVOffset.x, w = AtlasUVOffset.y */
    FVector4f RectLightParams0;
    
    /** x = AtlasUVScale.x, y = AtlasUVScale.y, z = AtlasMaxLevel, w = IESAtlasIndex */
    FVector4f RectLightParams1;

    // ========================================================================
    // Light Type and Additional Flags (float4)
    // ========================================================================
    
    /** 
     * x = LightType (as float)
     * y = bInverseSquared
     * z = bRadialLight
     * w = bSpotLight
     */
    FVector4f LightTypeFlags0;
    
    /** 
     * x = bRectLight
     * y = ShadowedBits
     * z = bContactShadowLengthInWS
     * w = unused
     */
    FVector4f LightTypeFlags1;

    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor */
    FDeferredLightUniformBuffer()
    {
        LightPositionAndInvRadius = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        LightColorAndFalloffExponent = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        LightDirectionAndSpecularScale = FVector4f(0.0f, 0.0f, -1.0f, 1.0f);
        LightTangentAndSourceRadius = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
        SpotAnglesAndSourceLength = FVector4f(0.0f, 1.0f, 0.0f, 0.0f);
        ShadowMapChannelMask = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        DistanceFadeAndFlags = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        ContactShadowParams = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
        RectLightParams0 = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        RectLightParams1 = FVector4f(1.0f, 1.0f, 0.0f, -1.0f);
        LightTypeFlags0 = FVector4f(1.0f, 1.0f, 1.0f, 0.0f);
        LightTypeFlags1 = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // ========================================================================
    // Initialization from FDeferredLightData
    // ========================================================================

    /** Initialize from FDeferredLightData */
    void SetFromDeferredLightData(const FDeferredLightData& LightData)
    {
        LightPositionAndInvRadius = FVector4f(
            LightData.TranslatedWorldPosition.X,
            LightData.TranslatedWorldPosition.Y,
            LightData.TranslatedWorldPosition.Z,
            LightData.InvRadius
        );
        
        LightColorAndFalloffExponent = FVector4f(
            LightData.Color.X,
            LightData.Color.Y,
            LightData.Color.Z,
            LightData.FalloffExponent
        );
        
        LightDirectionAndSpecularScale = FVector4f(
            LightData.Direction.X,
            LightData.Direction.Y,
            LightData.Direction.Z,
            LightData.SpecularScale
        );
        
        LightTangentAndSourceRadius = FVector4f(
            LightData.Tangent.X,
            LightData.Tangent.Y,
            LightData.Tangent.Z,
            LightData.SourceRadius
        );
        
        SpotAnglesAndSourceLength = FVector4f(
            LightData.SpotAngles.X,
            LightData.SpotAngles.Y,
            LightData.SoftSourceRadius,
            LightData.SourceLength
        );
        
        ShadowMapChannelMask = LightData.ShadowMapChannelMask;
        
        DistanceFadeAndFlags = FVector4f(
            LightData.DistanceFadeMAD.X,
            LightData.DistanceFadeMAD.Y,
            LightData.ContactShadowLength,
            0.0f
        );
        
        ContactShadowParams = FVector4f(
            LightData.ContactShadowCastingIntensity,
            LightData.ContactShadowNonCastingIntensity,
            0.0f,
            0.0f
        );
        
        RectLightParams0 = FVector4f(
            LightData.RectLightBarnCosAngle,
            LightData.RectLightBarnLength,
            LightData.RectLightAtlasUVOffset.X,
            LightData.RectLightAtlasUVOffset.Y
        );
        
        RectLightParams1 = FVector4f(
            LightData.RectLightAtlasUVScale.X,
            LightData.RectLightAtlasUVScale.Y,
            LightData.RectLightAtlasMaxLevel,
            LightData.IESAtlasIndex
        );
        
        LightTypeFlags0 = FVector4f(
            static_cast<float>(LightData.GetLightType()),
            LightData.bInverseSquared ? 1.0f : 0.0f,
            LightData.bRadialLight ? 1.0f : 0.0f,
            LightData.bSpotLight ? 1.0f : 0.0f
        );
        
        LightTypeFlags1 = FVector4f(
            LightData.bRectLight ? 1.0f : 0.0f,
            static_cast<float>(LightData.ShadowedBits),
            LightData.bContactShadowLengthInWS ? 1.0f : 0.0f,
            0.0f
        );
    }
};

// Verify size for GPU alignment
static_assert(sizeof(FDeferredLightUniformBuffer) == 192, "FDeferredLightUniformBuffer size mismatch");

// ============================================================================
// FForwardLightData - Forward rendering light buffer
// ============================================================================

/**
 * Light data buffer for forward rendering
 * 
 * Contains the directional light and an array of local lights
 * for forward rendering passes.
 * 
 * Reference UE5: ForwardLightData uniform buffer
 */
struct FForwardLightData
{
    /** Directional light parameters */
    FDirectionalLightShaderParameters DirectionalLight;
    
    /** Number of local lights in the buffer */
    uint32 NumLocalLights;
    
    /** Padding for alignment */
    uint32 Padding[3];
    
    /** Array of local light data (up to MAX_LOCAL_LIGHTS) */
    FLocalLightData LocalLights[MAX_LOCAL_LIGHTS];

    /** Default constructor */
    FForwardLightData()
        : NumLocalLights(0)
    {
        Padding[0] = Padding[1] = Padding[2] = 0;
    }

    /** Add a local light to the buffer */
    bool AddLocalLight(const FLocalLightData& Light)
    {
        if (NumLocalLights >= MAX_LOCAL_LIGHTS)
        {
            return false;
        }
        LocalLights[NumLocalLights++] = Light;
        return true;
    }

    /** Clear all local lights */
    void ClearLocalLights()
    {
        NumLocalLights = 0;
    }
};

// ============================================================================
// FLightUniformBufferManager - Manager for light uniform buffers
// ============================================================================

/**
 * Manager class for creating and updating light uniform buffers
 * 
 * This class handles the creation and management of GPU uniform buffers
 * for lighting data. It provides methods to update buffers from
 * light scene proxies.
 */
class FLightUniformBufferManager
{
public:
    /**
     * Create FDeferredLightData from a light scene proxy
     * @param Proxy The light scene proxy
     * @param CameraPosition Camera position for camera-relative rendering
     * @return FDeferredLightData populated from the proxy
     */
    static FDeferredLightData CreateDeferredLightData(
        const FLightSceneProxy* Proxy,
        const FVector3f& CameraPosition);

    /**
     * Create FLocalLightData from a light scene proxy
     * @param Proxy The light scene proxy
     * @param CameraPosition Camera position for camera-relative rendering
     * @param LightSceneId Scene ID for the light
     * @return FLocalLightData populated from the proxy
     */
    static FLocalLightData CreateLocalLightData(
        const FLightSceneProxy* Proxy,
        const FVector3f& CameraPosition,
        int32 LightSceneId);

    /**
     * Create FLightShaderParameters from a light scene proxy
     * @param Proxy The light scene proxy
     * @param CameraPosition Camera position for camera-relative rendering
     * @return FLightShaderParameters populated from the proxy
     */
    static FLightShaderParameters CreateLightShaderParameters(
        const FLightSceneProxy* Proxy,
        const FVector3f& CameraPosition);
};

} // namespace MonsterEngine
