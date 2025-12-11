// Copyright Monster Engine. All Rights Reserved.

/**
 * @file Actor.cpp
 * @brief Implementation of AActor base class
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/Actor.cpp
 */

#include "Engine/Actor.h"
#include "Engine/Components/SceneComponent.h"
#include "Engine/Components/PrimitiveComponent.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogActor, Log, All);

// Static member initialization
uint32 AActor::NextUniqueID = 1;

// ============================================================================
// Construction / Destruction
// ============================================================================

AActor::AActor()
    : RootComponent(nullptr)
    , Scene(nullptr)
    , ActorName(TEXT("Actor"))
    , UniqueID(NextUniqueID++)
    , bTickEnabled(true)
    , bHidden(false)
    , bHasBegunPlay(false)
{
    MR_LOG(LogActor, Verbose, "Actor created with ID %u", UniqueID);
}

AActor::~AActor()
{
    // Clean up components
    for (USceneComponent* Component : Components)
    {
        if (Component)
        {
            delete Component;
        }
    }
    Components.Empty();
    RootComponent = nullptr;
    
    MR_LOG(LogActor, Verbose, "Actor %u destroyed", UniqueID);
}

// ============================================================================
// Lifecycle
// ============================================================================

void AActor::BeginPlay()
{
    if (bHasBegunPlay)
    {
        return;
    }
    
    bHasBegunPlay = true;
    
    // Notify all components
    for (USceneComponent* Component : Components)
    {
        if (Component)
        {
            Component->OnRegister();
        }
    }
    
    MR_LOG(LogActor, Verbose, "Actor %u BeginPlay", UniqueID);
}

void AActor::EndPlay()
{
    if (!bHasBegunPlay)
    {
        return;
    }
    
    // Notify all components
    for (USceneComponent* Component : Components)
    {
        if (Component)
        {
            Component->OnUnregister();
        }
    }
    
    bHasBegunPlay = false;
    
    MR_LOG(LogActor, Verbose, "Actor %u EndPlay", UniqueID);
}

void AActor::Tick(float DeltaTime)
{
    if (!bTickEnabled)
    {
        return;
    }
    
    // Tick all components
    for (USceneComponent* Component : Components)
    {
        if (Component)
        {
            Component->TickComponent(DeltaTime);
        }
    }
}

// ============================================================================
// Transform
// ============================================================================

FVector AActor::GetActorLocation() const
{
    if (RootComponent)
    {
        return RootComponent->GetComponentLocation();
    }
    return FVector::ZeroVector;
}

FRotator AActor::GetActorRotation() const
{
    if (RootComponent)
    {
        return RootComponent->GetComponentRotation();
    }
    return FRotator::ZeroRotator;
}

FVector AActor::GetActorScale() const
{
    if (RootComponent)
    {
        return RootComponent->GetComponentScale();
    }
    return FVector::OneVector;
}

FTransform AActor::GetActorTransform() const
{
    if (RootComponent)
    {
        return RootComponent->GetComponentTransform();
    }
    return FTransform::Identity;
}

bool AActor::SetActorLocation(const FVector& NewLocation, bool bSweep, bool bTeleport)
{
    if (RootComponent)
    {
        RootComponent->SetWorldLocation(NewLocation);
        return true;
    }
    return false;
}

bool AActor::SetActorRotation(const FRotator& NewRotation, bool bTeleport)
{
    if (RootComponent)
    {
        RootComponent->SetWorldRotation(NewRotation);
        return true;
    }
    return false;
}

void AActor::SetActorScale3D(const FVector& NewScale)
{
    if (RootComponent)
    {
        RootComponent->SetWorldScale3D(NewScale);
    }
}

bool AActor::SetActorTransform(const FTransform& NewTransform, bool bSweep, bool bTeleport)
{
    if (RootComponent)
    {
        RootComponent->SetWorldTransform(NewTransform);
        return true;
    }
    return false;
}

bool AActor::AddActorWorldOffset(const FVector& DeltaLocation, bool bSweep, bool bTeleport)
{
    if (RootComponent)
    {
        FVector NewLocation = RootComponent->GetComponentLocation() + DeltaLocation;
        RootComponent->SetWorldLocation(NewLocation);
        return true;
    }
    return false;
}

bool AActor::AddActorWorldRotation(const FRotator& DeltaRotation, bool bSweep, bool bTeleport)
{
    if (RootComponent)
    {
        FRotator NewRotation = RootComponent->GetComponentRotation() + DeltaRotation;
        RootComponent->SetWorldRotation(NewRotation);
        return true;
    }
    return false;
}

bool AActor::AddActorLocalRotation(const FRotator& DeltaRotation, bool bSweep, bool bTeleport)
{
    if (RootComponent)
    {
        FRotator NewRotation = RootComponent->GetRelativeRotation() + DeltaRotation;
        RootComponent->SetRelativeRotation(NewRotation);
        return true;
    }
    return false;
}

// ============================================================================
// Components
// ============================================================================

void AActor::SetRootComponent(USceneComponent* NewRootComponent)
{
    if (NewRootComponent && NewRootComponent->GetOwner() != this)
    {
        MR_LOG(LogActor, Warning, "Cannot set root component that belongs to another actor");
        return;
    }
    
    RootComponent = NewRootComponent;
}

void AActor::AddComponent(USceneComponent* Component)
{
    if (!Component)
    {
        return;
    }
    
    // Check if already added
    if (Components.Find(Component) != INDEX_NONE)
    {
        return;
    }
    
    Components.Add(Component);
    Component->SetOwner(this);
    
    // Set as root if no root exists
    if (!RootComponent)
    {
        RootComponent = Component;
    }
    
    // If already playing, notify component
    if (bHasBegunPlay)
    {
        Component->OnRegister();
    }
    
    MR_LOG(LogActor, Verbose, "Component added to actor %u", UniqueID);
}

void AActor::RemoveComponent(USceneComponent* Component)
{
    if (!Component)
    {
        return;
    }
    
    int32 Index = Components.Find(Component);
    if (Index != INDEX_NONE)
    {
        // Notify component if playing
        if (bHasBegunPlay)
        {
            Component->OnUnregister();
        }
        
        Components.RemoveAt(Index);
        Component->SetOwner(nullptr);
        
        // Clear root if this was the root
        if (RootComponent == Component)
        {
            RootComponent = Components.Num() > 0 ? Components[0] : nullptr;
        }
        
        MR_LOG(LogActor, Verbose, "Component removed from actor %u", UniqueID);
    }
}

void AActor::GetPrimitiveComponents(TArray<UPrimitiveComponent*>& OutComponents) const
{
    OutComponents.Empty();
    
    for (USceneComponent* Component : Components)
    {
        // Check if this is a primitive component
        UPrimitiveComponent* PrimitiveComp = dynamic_cast<UPrimitiveComponent*>(Component);
        if (PrimitiveComp)
        {
            OutComponents.Add(PrimitiveComp);
        }
    }
}

// ============================================================================
// Visibility
// ============================================================================

void AActor::SetHidden(bool bNewHidden)
{
    bHidden = bNewHidden;
}

void AActor::SetActorHiddenInGame(bool bNewHidden, bool bPropagateToChildren)
{
    bHidden = bNewHidden;
    
    if (bPropagateToChildren)
    {
        for (USceneComponent* Component : Components)
        {
            if (Component)
            {
                Component->SetVisibility(!bNewHidden, bPropagateToChildren);
            }
        }
    }
}

} // namespace MonsterEngine
