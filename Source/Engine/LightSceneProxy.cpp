// Copyright Monster Engine. All Rights Reserved.

/**
 * @file LightSceneProxy.cpp
 * @brief Implementation of FLightSceneProxy
 */

#include "Engine/LightSceneProxy.h"
#include "Engine/LightSceneInfo.h"
#include "Engine/Components/LightComponent.h"
#include "Core/Logging/LogMacros.h"
#include <cmath>

namespace MonsterEngine
{

DEFINE_LOG_CATEGORY_STATIC(LogLightSceneProxy, Log, All);

// ============================================================================
// FLightSceneProxy Implementation
// ============================================================================

FLightSceneProxy::FLightSceneProxy(const ULightComponent* InComponent)
    : LightComponent(InComponent)
    , LightSceneInfo(nullptr)
    , LightType(ELightType::Point)
    , Position(FVector::ZeroVector)
    , Direction(FVector::ForwardVector)
    , LocalToWorld(FMatrix::Identity)
    , Color(FLinearColor::White)
    , Intensity(1.0f)
    , Radius(1000.0f)
    , SourceRadius(0.0f)
    , SourceLength(0.0f)
    , SoftSourceRadius(0.0f)
    , InnerConeAngle(0.0f)
    , OuterConeAngle(0.7853981f) // 45 degrees
    , CosInnerConeAngle(1.0f)
    , CosOuterConeAngle(0.7071068f) // cos(45)
    , ShadowBias(0.5f)
    , ShadowSlopeBias(0.5f)
    , ShadowResolutionScale(1.0f)
    , LightingChannelMask(0x01)
    , bCastShadow(true)
    , bCastStaticShadow(true)
    , bCastDynamicShadow(true)
    , bAffectsWorld(true)
    , bVisible(true)
    , bUseInverseSquaredFalloff(true)
    , bAffectTranslucentLighting(true)
    , bCastVolumetricShadow(false)
{
    if (InComponent)
    {
        // Copy properties from component
        LightType = InComponent->GetLightType();
        LocalToWorld = InComponent->GetComponentToWorld();
        Position = InComponent->GetComponentLocation();
        Direction = InComponent->GetForwardVector();
        Color = InComponent->GetLightColor();
        Intensity = InComponent->GetIntensity();
        
        bCastShadow = InComponent->CastsShadow();
        bCastStaticShadow = InComponent->CastsStaticShadow();
        bCastDynamicShadow = InComponent->CastsDynamicShadow();
        bAffectsWorld = InComponent->AffectsWorld();
        bVisible = InComponent->IsVisible();
        
        ShadowBias = InComponent->GetShadowBias();
        ShadowSlopeBias = InComponent->GetShadowSlopeBias();
        ShadowResolutionScale = InComponent->GetShadowResolutionScale();
        LightingChannelMask = InComponent->GetLightingChannelMask();
    }
}

FLightSceneProxy::~FLightSceneProxy()
{
    MR_LOG(LogLightSceneProxy, Verbose, "LightSceneProxy destroyed");
}

// ============================================================================
// Transform
// ============================================================================

void FLightSceneProxy::SetTransform(const FMatrix& InLocalToWorld)
{
    LocalToWorld = InLocalToWorld;
    Position = LocalToWorld.GetOrigin();
    
    // Extract direction from transform (forward vector)
    Direction = FVector(LocalToWorld.M[0][0], LocalToWorld.M[1][0], LocalToWorld.M[2][0]);
    Direction.Normalize();
}

// ============================================================================
// Lighting Calculations
// ============================================================================

FBoxSphereBounds FLightSceneProxy::GetBounds() const
{
    switch (LightType)
    {
    case ELightType::Directional:
        // Directional lights affect the entire scene
        return FBoxSphereBounds(FVector::ZeroVector, FVector(1e10, 1e10, 1e10), 1e10);
        
    case ELightType::Point:
    case ELightType::Spot:
        return FBoxSphereBounds(Position, FVector(Radius, Radius, Radius), Radius);
        
    case ELightType::Rect:
        return FBoxSphereBounds(Position, FVector(Radius, Radius, Radius), Radius);
        
    default:
        return FBoxSphereBounds(Position, FVector(Radius, Radius, Radius), Radius);
    }
}

bool FLightSceneProxy::AffectsBounds(const FBoxSphereBounds& Bounds) const
{
    if (!bAffectsWorld || !bVisible)
    {
        return false;
    }

    switch (LightType)
    {
    case ELightType::Directional:
        // Directional lights affect everything
        return true;
        
    case ELightType::Point:
    {
        // Check sphere-sphere intersection
        FVector Delta = Bounds.Origin - Position;
        double DistanceSquared = Delta.SizeSquared();
        double CombinedRadius = Radius + Bounds.SphereRadius;
        return DistanceSquared <= (CombinedRadius * CombinedRadius);
    }
    
    case ELightType::Spot:
    {
        // First check sphere intersection
        FVector Delta = Bounds.Origin - Position;
        double DistanceSquared = Delta.SizeSquared();
        double CombinedRadius = Radius + Bounds.SphereRadius;
        if (DistanceSquared > (CombinedRadius * CombinedRadius))
        {
            return false;
        }
        
        // Then check cone intersection (simplified)
        double Distance = std::sqrt(DistanceSquared);
        if (Distance > 0.0)
        {
            FVector ToTarget = Delta / Distance;
            double CosAngle = ToTarget | Direction;
            
            // Account for bounds sphere radius
            double SinAngle = std::sqrt(1.0 - CosAngle * CosAngle);
            double EffectiveCosAngle = CosAngle - (Bounds.SphereRadius / Distance) * SinAngle;
            
            return EffectiveCosAngle >= CosOuterConeAngle;
        }
        return true;
    }
    
    case ELightType::Rect:
        // Similar to point light for now
        {
            FVector Delta = Bounds.Origin - Position;
            double DistanceSquared = Delta.SizeSquared();
            double CombinedRadius = Radius + Bounds.SphereRadius;
            return DistanceSquared <= (CombinedRadius * CombinedRadius);
        }
        
    default:
        return true;
    }
}

float FLightSceneProxy::GetAttenuation(float Distance) const
{
    if (Distance <= 0.0f)
    {
        return 1.0f;
    }

    if (LightType == ELightType::Directional)
    {
        return 1.0f; // No attenuation for directional lights
    }

    if (Distance >= Radius)
    {
        return 0.0f;
    }

    if (bUseInverseSquaredFalloff)
    {
        // Inverse squared falloff with smooth falloff at radius
        float NormalizedDistance = Distance / Radius;
        float DistanceSquared = Distance * Distance;
        float InvSquared = 1.0f / (DistanceSquared + 1.0f);
        
        // Smooth falloff near radius
        float Falloff = 1.0f - NormalizedDistance;
        Falloff = Falloff * Falloff;
        
        return InvSquared * Falloff;
    }
    else
    {
        // Linear falloff
        return 1.0f - (Distance / Radius);
    }
}

FLinearColor FLightSceneProxy::GetLightContribution(const FVector& WorldPosition, const FVector& WorldNormal) const
{
    if (!bAffectsWorld || !bVisible)
    {
        return FLinearColor::Black;
    }

    FVector LightDirection;
    float Attenuation = 1.0f;

    switch (LightType)
    {
    case ELightType::Directional:
        LightDirection = -Direction;
        break;
        
    case ELightType::Point:
    {
        FVector ToLight = Position - WorldPosition;
        float Distance = static_cast<float>(ToLight.Size());
        if (Distance > 0.0f)
        {
            LightDirection = ToLight / Distance;
            Attenuation = GetAttenuation(Distance);
        }
        else
        {
            return FLinearColor::Black;
        }
        break;
    }
    
    case ELightType::Spot:
    {
        FVector ToLight = Position - WorldPosition;
        float Distance = static_cast<float>(ToLight.Size());
        if (Distance > 0.0f)
        {
            LightDirection = ToLight / Distance;
            Attenuation = GetAttenuation(Distance);
            
            // Apply spot cone attenuation
            float CosAngle = static_cast<float>(LightDirection | (-Direction));
            if (CosAngle < CosOuterConeAngle)
            {
                return FLinearColor::Black;
            }
            
            // Smooth falloff between inner and outer cone
            if (CosAngle < CosInnerConeAngle)
            {
                float ConeAttenuation = (CosAngle - CosOuterConeAngle) / (CosInnerConeAngle - CosOuterConeAngle);
                Attenuation *= ConeAttenuation * ConeAttenuation;
            }
        }
        else
        {
            return FLinearColor::Black;
        }
        break;
    }
    
    default:
        LightDirection = -Direction;
        break;
    }

    // Calculate N dot L
    float NdotL = static_cast<float>(WorldNormal | LightDirection);
    NdotL = std::max(0.0f, NdotL);

    // Calculate final contribution
    FLinearColor Result = Color;
    Result.R *= Intensity * Attenuation * NdotL;
    Result.G *= Intensity * Attenuation * NdotL;
    Result.B *= Intensity * Attenuation * NdotL;
    
    return Result;
}

// ============================================================================
// FSkyLightSceneProxy Implementation
// ============================================================================

FSkyLightSceneProxy::FSkyLightSceneProxy(const ULightComponent* InComponent)
    : FLightSceneProxy(InComponent)
    , SkyColor(FLinearColor::White)
    , LowerHemisphereColor(FLinearColor::Black)
    , OcclusionMaxDistance(1000.0f)
    , OcclusionContrast(0.0f)
    , bRealTimeCapture(false)
{
    LightType = ELightType::Sky;
}

} // namespace MonsterEngine
