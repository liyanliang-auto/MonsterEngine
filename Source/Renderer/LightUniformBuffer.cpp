// Copyright Monster Engine. All Rights Reserved.

/**
 * @file LightUniformBuffer.cpp
 * @brief Implementation of light uniform buffer manager
 */

#include "Renderer/LightUniformBuffer.h"
#include "Engine/LightSceneProxy.h"
#include "Math/MathFunctions.h"
#include "Math/Vector2D.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Define log category for lighting
DECLARE_LOG_CATEGORY_EXTERN(LogLighting, Log, All)
DEFINE_LOG_CATEGORY(LogLighting)

// ============================================================================
// FLightUniformBufferManager Implementation
// ============================================================================

FDeferredLightData FLightUniformBufferManager::CreateDeferredLightData(
    const FLightSceneProxy* Proxy,
    const FVector3f& CameraPosition)
{
    FDeferredLightData LightData;
    
    if (!Proxy)
    {
        MR_LOG(LogLighting, Warning, "CreateDeferredLightData called with null proxy");
        return LightData;
    }

    // Get light type
    const ELightType LightType = Proxy->GetLightType();
    
    // Position (camera-relative)
    const FVector& WorldPos = Proxy->GetPosition();
    LightData.TranslatedWorldPosition = FVector3f(
        static_cast<float>(WorldPos.X) - CameraPosition.X,
        static_cast<float>(WorldPos.Y) - CameraPosition.Y,
        static_cast<float>(WorldPos.Z) - CameraPosition.Z
    );
    
    // Radius and inverse radius
    const float Radius = Proxy->GetRadius();
    LightData.InvRadius = Radius > 0.0f ? 1.0f / Radius : 0.0f;
    
    // Color (pre-multiplied with intensity)
    const FLinearColor& Color = Proxy->GetColor();
    const float Intensity = Proxy->GetIntensity();
    LightData.Color = FVector3f(
        Color.R * Intensity,
        Color.G * Intensity,
        Color.B * Intensity
    );
    
    // Direction
    const FVector& Dir = Proxy->GetDirection();
    LightData.Direction = FVector3f(
        static_cast<float>(Dir.X),
        static_cast<float>(Dir.Y),
        static_cast<float>(Dir.Z)
    );
    
    // Area light parameters
    LightData.SourceRadius = Proxy->GetSourceRadius();
    LightData.SoftSourceRadius = Proxy->GetSoftSourceRadius();
    LightData.SourceLength = Proxy->GetSourceLength();
    
    // Spot light parameters
    if (LightType == ELightType::Spot)
    {
        const float CosInner = Proxy->GetCosInnerConeAngle();
        const float CosOuter = Proxy->GetCosOuterConeAngle();
        const float InvAngleRange = 1.0f / FMath::Max(0.001f, CosInner - CosOuter);
        LightData.SpotAngles = FVector2f(CosInner, InvAngleRange);
    }
    
    // Light type flags
    LightData.bRadialLight = (LightType != ELightType::Directional);
    LightData.bSpotLight = (LightType == ELightType::Spot);
    LightData.bRectLight = (LightType == ELightType::Rect);
    LightData.bInverseSquared = true; // Default to physically correct falloff
    
    // Falloff exponent (0 = inverse squared)
    LightData.FalloffExponent = 0.0f;
    
    // Specular scale
    LightData.SpecularScale = 1.0f;
    
    // Shadow settings
    if (Proxy->CastsShadow())
    {
        LightData.ShadowedBits = 1;
    }
    
    // Contact shadow defaults
    LightData.ContactShadowLength = 0.0f;
    LightData.ContactShadowCastingIntensity = 1.0f;
    LightData.ContactShadowNonCastingIntensity = 0.0f;
    LightData.bContactShadowLengthInWS = false;
    
    return LightData;
}

FLocalLightData FLightUniformBufferManager::CreateLocalLightData(
    const FLightSceneProxy* Proxy,
    const FVector3f& CameraPosition,
    int32 LightSceneId)
{
    FLocalLightData LightData;
    
    if (!Proxy)
    {
        MR_LOG(LogLighting, Warning, "CreateLocalLightData called with null proxy");
        return LightData;
    }

    // Get light type
    const ELightType LightType = Proxy->GetLightType();
    
    // Position and inverse radius
    const FVector& WorldPos = Proxy->GetPosition();
    const float Radius = Proxy->GetRadius();
    const float InvRadius = Radius > 0.0f ? 1.0f / Radius : 0.0f;
    
    LightData.LightPositionAndInvRadius = FVector4f(
        static_cast<float>(WorldPos.X) - CameraPosition.X,
        static_cast<float>(WorldPos.Y) - CameraPosition.Y,
        static_cast<float>(WorldPos.Z) - CameraPosition.Z,
        InvRadius
    );
    
    // Color and falloff
    const FLinearColor& Color = Proxy->GetColor();
    const float Intensity = Proxy->GetIntensity();
    const float FalloffExponent = 0.0f; // Inverse squared
    
    LightData.LightColorAndFalloffExponent = FVector4f(
        Color.R * Intensity,
        Color.G * Intensity,
        Color.B * Intensity,
        FalloffExponent
    );
    
    // Direction and shadow mask
    const FVector& Dir = Proxy->GetDirection();
    uint32 LightTypeShader = ELightTypeShader::Point;
    if (LightType == ELightType::Spot) LightTypeShader = ELightTypeShader::Spot;
    else if (LightType == ELightType::Rect) LightTypeShader = ELightTypeShader::Rect;
    
    const uint32 PackedShadowMask = FLocalLightData::PackLightTypeAndShadowMask(
        0, // ShadowMapChannelMask
        0, // PreviewShadowMapChannel
        LIGHTING_CHANNEL_MASK, // LightingChannelMask
        LightTypeShader,
        Proxy->CastsShadow()
    );
    
    LightData.LightDirectionAndShadowMask = FVector4f(
        static_cast<float>(Dir.X),
        static_cast<float>(Dir.Y),
        static_cast<float>(Dir.Z),
        *reinterpret_cast<const float*>(&PackedShadowMask)
    );
    
    // Spot angles and source radius
    float CosInner = 0.0f;
    float InvAngleRange = 1.0f;
    if (LightType == ELightType::Spot)
    {
        CosInner = Proxy->GetCosInnerConeAngle();
        const float CosOuter = Proxy->GetCosOuterConeAngle();
        InvAngleRange = 1.0f / FMath::Max(0.001f, CosInner - CosOuter);
    }
    
    const uint32 PackedSpotAngles = FLocalLightData::PackHalf2(CosInner, InvAngleRange);
    const uint32 PackedSourceRadius = FLocalLightData::PackHalf2(
        Proxy->GetSourceRadius(),
        Proxy->GetSoftSourceRadius()
    );
    const uint32 PackedSourceLength = FLocalLightData::PackHalf2(
        Proxy->GetSourceLength(),
        1.0f // Volumetric scattering intensity
    );
    
    LightData.SpotAnglesAndIdAndSourceRadiusPacked = FVector4f(
        *reinterpret_cast<const float*>(&PackedSpotAngles),
        static_cast<float>(LightSceneId),
        *reinterpret_cast<const float*>(&PackedSourceRadius),
        *reinterpret_cast<const float*>(&PackedSourceLength)
    );
    
    // Tangent and specular scale
    const uint32 PackedSpecularScale = FLocalLightData::PackHalf2(-1.0f, 1.0f); // IES index, specular scale
    LightData.LightTangentAndIESDataAndSpecularScale = FVector4f(
        1.0f, 0.0f, 0.0f, // Tangent
        *reinterpret_cast<const float*>(&PackedSpecularScale)
    );
    
    // Rect light data (unused for point/spot)
    LightData.RectDataAndVirtualShadowMapId = FVector4f(0.0f, 0.0f, -1.0f, 0.0f);
    
    return LightData;
}

FLightShaderParameters FLightUniformBufferManager::CreateLightShaderParameters(
    const FLightSceneProxy* Proxy,
    const FVector3f& CameraPosition)
{
    FLightShaderParameters Params;
    
    if (!Proxy)
    {
        MR_LOG(LogLighting, Warning, "CreateLightShaderParameters called with null proxy");
        return Params;
    }

    // Get light type
    const ELightType LightType = Proxy->GetLightType();
    
    // Position (camera-relative)
    const FVector& WorldPos = Proxy->GetPosition();
    Params.TranslatedWorldPosition = FVector3f(
        static_cast<float>(WorldPos.X) - CameraPosition.X,
        static_cast<float>(WorldPos.Y) - CameraPosition.Y,
        static_cast<float>(WorldPos.Z) - CameraPosition.Z
    );
    
    // Radius
    const float Radius = Proxy->GetRadius();
    Params.InvRadius = Radius > 0.0f ? 1.0f / Radius : 0.0f;
    
    // Color (pre-multiplied with intensity)
    const FLinearColor& Color = Proxy->GetColor();
    const float Intensity = Proxy->GetIntensity();
    Params.Color = FVector3f(
        Color.R * Intensity,
        Color.G * Intensity,
        Color.B * Intensity
    );
    
    // Falloff exponent (0 = inverse squared)
    Params.FalloffExponent = 0.0f;
    
    // Direction
    const FVector& Dir = Proxy->GetDirection();
    Params.Direction = FVector3f(
        static_cast<float>(Dir.X),
        static_cast<float>(Dir.Y),
        static_cast<float>(Dir.Z)
    );
    
    // Specular scale
    Params.SpecularScale = 1.0f;
    
    // Area light parameters
    Params.SourceRadius = Proxy->GetSourceRadius();
    Params.SoftSourceRadius = Proxy->GetSoftSourceRadius();
    Params.SourceLength = Proxy->GetSourceLength();
    
    // Spot light parameters
    if (LightType == ELightType::Spot)
    {
        const float CosInner = Proxy->GetCosInnerConeAngle();
        const float CosOuter = Proxy->GetCosOuterConeAngle();
        const float InvAngleRange = 1.0f / FMath::Max(0.001f, CosInner - CosOuter);
        Params.SpotAngles = FVector2f(CosInner, InvAngleRange);
    }
    
    // Tangent (for rect lights)
    Params.Tangent = FVector3f(1.0f, 0.0f, 0.0f);
    
    // Rect light parameters (unused for point/spot)
    Params.RectLightBarnCosAngle = 0.0f;
    Params.RectLightBarnLength = 0.0f;
    Params.RectLightAtlasUVOffset = FVector2f(0.0f, 0.0f);
    Params.RectLightAtlasUVScale = FVector2f(1.0f, 1.0f);
    Params.RectLightAtlasMaxLevel = 0.0f;
    
    // IES profile (unused)
    Params.IESAtlasIndex = -1.0f;
    
    return Params;
}

} // namespace MonsterEngine
