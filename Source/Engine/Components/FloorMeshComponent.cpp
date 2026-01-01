// Copyright Monster Engine. All Rights Reserved.

/**
 * @file FloorMeshComponent.cpp
 * @brief Implementation of floor mesh component
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/Components/StaticMeshComponent.cpp
 */

#include "Engine/Components/FloorMeshComponent.h"
#include "Engine/Proxies/FloorSceneProxy.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogFloorMeshComponent, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

UFloorMeshComponent::UFloorMeshComponent()
    : UMeshComponent()
    , TextureTile(25.0f)
    , FloorSize(25.0f)
    , bNeedsProxyRecreation(false)
{
    MR_LOG(LogFloorMeshComponent, Verbose, "FloorMeshComponent created (default constructor)");
}

UFloorMeshComponent::UFloorMeshComponent(AActor* InOwner)
    : UMeshComponent(InOwner)
    , TextureTile(25.0f)
    , FloorSize(25.0f)
    , bNeedsProxyRecreation(false)
{
    MR_LOG(LogFloorMeshComponent, Verbose, "FloorMeshComponent created with owner");
}

UFloorMeshComponent::~UFloorMeshComponent()
{
    FloorTexture.Reset();
    Sampler.Reset();
    
    MR_LOG(LogFloorMeshComponent, Verbose, "FloorMeshComponent destroyed");
}

// ============================================================================
// UPrimitiveComponent Interface
// ============================================================================

FPrimitiveSceneProxy* UFloorMeshComponent::CreateSceneProxy()
{
    MR_LOG(LogFloorMeshComponent, Log, "Creating FloorSceneProxy");
    
    FFloorSceneProxy* Proxy = new FFloorSceneProxy(this);
    
    // Store the proxy reference
    SceneProxy = Proxy;
    
    return Proxy;
}

FBox UFloorMeshComponent::GetLocalBounds() const
{
    // Floor is a plane at y = -0.5 with extent FloorSize
    FVector Min(-FloorSize, -0.5f, -FloorSize);
    FVector Max(FloorSize, -0.5f, FloorSize);
    return FBox(Min, Max);
}

// ============================================================================
// Texture Settings
// ============================================================================

void UFloorMeshComponent::SetTexture(TSharedPtr<MonsterRender::RHI::IRHITexture> Texture)
{
    FloorTexture = Texture;
    
    // Update proxy if it exists
    if (SceneProxy)
    {
        FFloorSceneProxy* FloorProxy = static_cast<FFloorSceneProxy*>(SceneProxy);
        if (FloorProxy)
        {
            FloorProxy->SetTexture(Texture);
        }
    }
}

void UFloorMeshComponent::SetSampler(TSharedPtr<MonsterRender::RHI::IRHISampler> InSampler)
{
    Sampler = InSampler;
    
    // Update proxy if it exists
    if (SceneProxy)
    {
        FFloorSceneProxy* FloorProxy = static_cast<FFloorSceneProxy*>(SceneProxy);
        if (FloorProxy)
        {
            FloorProxy->SetSampler(InSampler);
        }
    }
}

// ============================================================================
// Size Settings
// ============================================================================

void UFloorMeshComponent::SetFloorSize(float Size)
{
    FloorSize = FMath::Max(0.1f, Size);
    MarkProxyRecreationNeeded();
}

// ============================================================================
// Proxy Access
// ============================================================================

FFloorSceneProxy* UFloorMeshComponent::GetFloorSceneProxy() const
{
    return static_cast<FFloorSceneProxy*>(SceneProxy);
}

} // namespace MonsterEngine
