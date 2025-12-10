// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file LightSceneProxy.h
 * @brief Light scene proxy for rendering
 * 
 * FLightSceneProxy is the rendering thread's representation of a ULightComponent.
 * It encapsulates all the data needed to render the light and provides methods
 * for lighting calculations. Following UE5's light proxy architecture.
 */

#include "SceneTypes.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Core/Color.h"

namespace MonsterEngine
{

// Forward declarations
class ULightComponent;
class FLightSceneInfo;
class FScene;

/**
 * Rendering thread representation of a light component
 * 
 * FLightSceneProxy is created by ULightComponent and is owned by the rendering thread.
 * It contains all the data needed to render the light without accessing the game
 * thread's ULightComponent.
 */
class FLightSceneProxy
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /**
     * Constructor
     * @param InComponent The light component this proxy represents
     */
    FLightSceneProxy(const ULightComponent* InComponent);

    /** Virtual destructor */
    virtual ~FLightSceneProxy();

    // ========================================================================
    // Light Type
    // ========================================================================

    /** Get the light type */
    ELightType GetLightType() const { return LightType; }

    /** Check if this is a directional light */
    bool IsDirectionalLight() const { return LightType == ELightType::Directional; }

    /** Check if this is a point light */
    bool IsPointLight() const { return LightType == ELightType::Point; }

    /** Check if this is a spot light */
    bool IsSpotLight() const { return LightType == ELightType::Spot; }

    /** Check if this is a rect light */
    bool IsRectLight() const { return LightType == ELightType::Rect; }

    // ========================================================================
    // Transform
    // ========================================================================

    /** Get the world position of the light */
    const FVector& GetPosition() const { return Position; }

    /** Get the direction the light is pointing */
    const FVector& GetDirection() const { return Direction; }

    /** Get the local to world transform */
    const FMatrix& GetLocalToWorld() const { return LocalToWorld; }

    /** Set the transform */
    void SetTransform(const FMatrix& InLocalToWorld);

    // ========================================================================
    // Light Properties
    // ========================================================================

    /** Get the light color */
    const FLinearColor& GetColor() const { return Color; }

    /** Set the light color */
    void SetColor(const FLinearColor& InColor) { Color = InColor; }

    /** Get the light intensity */
    float GetIntensity() const { return Intensity; }

    /** Set the light intensity */
    void SetIntensity(float InIntensity) { Intensity = InIntensity; }

    /** Get the attenuation radius (for point/spot lights) */
    float GetRadius() const { return Radius; }

    /** Get the source radius (for area lights) */
    float GetSourceRadius() const { return SourceRadius; }

    /** Get the source length (for area lights) */
    float GetSourceLength() const { return SourceLength; }

    /** Get the soft source radius */
    float GetSoftSourceRadius() const { return SoftSourceRadius; }

    // ========================================================================
    // Spot Light Properties
    // ========================================================================

    /** Get the inner cone angle in radians */
    float GetInnerConeAngle() const { return InnerConeAngle; }

    /** Get the outer cone angle in radians */
    float GetOuterConeAngle() const { return OuterConeAngle; }

    /** Get the cosine of the inner cone angle */
    float GetCosInnerConeAngle() const { return CosInnerConeAngle; }

    /** Get the cosine of the outer cone angle */
    float GetCosOuterConeAngle() const { return CosOuterConeAngle; }

    // ========================================================================
    // Shadow Properties
    // ========================================================================

    /** Check if the light casts shadows */
    bool CastsShadow() const { return bCastShadow; }

    /** Check if the light casts static shadows */
    bool CastsStaticShadow() const { return bCastStaticShadow; }

    /** Check if the light casts dynamic shadows */
    bool CastsDynamicShadow() const { return bCastDynamicShadow; }

    /** Get the shadow bias */
    float GetShadowBias() const { return ShadowBias; }

    /** Get the shadow slope bias */
    float GetShadowSlopeBias() const { return ShadowSlopeBias; }

    /** Get the shadow resolution scale */
    float GetShadowResolutionScale() const { return ShadowResolutionScale; }

    // ========================================================================
    // Visibility
    // ========================================================================

    /** Check if the light affects the world */
    bool AffectsWorld() const { return bAffectsWorld; }

    /** Check if the light is visible */
    bool IsVisible() const { return bVisible; }

    /** Get the lighting channels */
    uint8 GetLightingChannelMask() const { return LightingChannelMask; }

    // ========================================================================
    // Scene Info
    // ========================================================================

    /** Get the light scene info */
    FLightSceneInfo* GetLightSceneInfo() const { return LightSceneInfo; }

    /** Set the light scene info */
    void SetLightSceneInfo(FLightSceneInfo* InLightSceneInfo) { LightSceneInfo = InLightSceneInfo; }

    // ========================================================================
    // Lighting Calculations
    // ========================================================================

    /**
     * Get the light's influence bounds
     * @return The bounding sphere of the light's influence
     */
    virtual FBoxSphereBounds GetBounds() const;

    /**
     * Check if a point is within the light's influence
     * @param Point The point to check
     * @return True if the point is within the light's influence
     */
    virtual bool AffectsBounds(const FBoxSphereBounds& Bounds) const;

    /**
     * Get the attenuation at a given distance
     * @param Distance The distance from the light
     * @return The attenuation factor (0-1)
     */
    virtual float GetAttenuation(float Distance) const;

    /**
     * Get the light's contribution at a point
     * @param WorldPosition The world position to evaluate
     * @param WorldNormal The surface normal at the position
     * @return The light contribution
     */
    virtual FLinearColor GetLightContribution(const FVector& WorldPosition, const FVector& WorldNormal) const;

protected:
    // ========================================================================
    // Protected Data
    // ========================================================================

    /** The light component this proxy represents */
    const ULightComponent* LightComponent;

    /** The light scene info */
    FLightSceneInfo* LightSceneInfo;

    /** Light type */
    ELightType LightType;

    /** World position */
    FVector Position;

    /** Light direction (normalized) */
    FVector Direction;

    /** Local to world transform */
    FMatrix LocalToWorld;

    /** Light color */
    FLinearColor Color;

    /** Light intensity */
    float Intensity;

    /** Attenuation radius */
    float Radius;

    /** Source radius for area lights */
    float SourceRadius;

    /** Source length for area lights */
    float SourceLength;

    /** Soft source radius */
    float SoftSourceRadius;

    /** Inner cone angle for spot lights (radians) */
    float InnerConeAngle;

    /** Outer cone angle for spot lights (radians) */
    float OuterConeAngle;

    /** Cosine of inner cone angle */
    float CosInnerConeAngle;

    /** Cosine of outer cone angle */
    float CosOuterConeAngle;

    /** Shadow bias */
    float ShadowBias;

    /** Shadow slope bias */
    float ShadowSlopeBias;

    /** Shadow resolution scale */
    float ShadowResolutionScale;

    /** Lighting channel mask */
    uint8 LightingChannelMask;

    // ========================================================================
    // Flags
    // ========================================================================

    /** Whether the light casts shadows */
    uint8 bCastShadow : 1;

    /** Whether the light casts static shadows */
    uint8 bCastStaticShadow : 1;

    /** Whether the light casts dynamic shadows */
    uint8 bCastDynamicShadow : 1;

    /** Whether the light affects the world */
    uint8 bAffectsWorld : 1;

    /** Whether the light is visible */
    uint8 bVisible : 1;

    /** Whether the light uses inverse squared falloff */
    uint8 bUseInverseSquaredFalloff : 1;

    /** Whether the light affects translucency */
    uint8 bAffectTranslucentLighting : 1;

    /** Whether the light casts volumetric shadows */
    uint8 bCastVolumetricShadow : 1;
};

/**
 * Sky light scene proxy
 * 
 * Specialized proxy for sky lights that capture and apply ambient lighting
 */
class FSkyLightSceneProxy : public FLightSceneProxy
{
public:
    /**
     * Constructor
     * @param InComponent The sky light component this proxy represents
     */
    FSkyLightSceneProxy(const ULightComponent* InComponent);

    /** Get the sky light color */
    const FLinearColor& GetSkyColor() const { return SkyColor; }

    /** Get the lower hemisphere color */
    const FLinearColor& GetLowerHemisphereColor() const { return LowerHemisphereColor; }

    /** Get the occlusion max distance */
    float GetOcclusionMaxDistance() const { return OcclusionMaxDistance; }

    /** Get the occlusion contrast */
    float GetOcclusionContrast() const { return OcclusionContrast; }

    /** Check if real-time capture is enabled */
    bool IsRealTimeCaptureEnabled() const { return bRealTimeCapture; }

protected:
    /** Sky color */
    FLinearColor SkyColor;

    /** Lower hemisphere color */
    FLinearColor LowerHemisphereColor;

    /** Occlusion max distance */
    float OcclusionMaxDistance;

    /** Occlusion contrast */
    float OcclusionContrast;

    /** Whether real-time capture is enabled */
    uint8 bRealTimeCapture : 1;
};

} // namespace MonsterEngine
