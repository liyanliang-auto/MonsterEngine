// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SceneComponent.cpp
 * @brief Implementation of USceneComponent
 */

#include "Engine/Components/SceneComponent.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

DEFINE_LOG_CATEGORY_STATIC(LogSceneComponent, Log, All);

// ============================================================================
// Static Attachment Rules
// ============================================================================

FAttachmentTransformRules FAttachmentTransformRules::KeepRelativeTransform(EAttachmentRule::KeepRelative, false);
FAttachmentTransformRules FAttachmentTransformRules::KeepWorldTransform(EAttachmentRule::KeepWorld, false);
FAttachmentTransformRules FAttachmentTransformRules::SnapToTargetNotIncludingScale(
    EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);
FAttachmentTransformRules FAttachmentTransformRules::SnapToTargetIncludingScale(EAttachmentRule::SnapToTarget, false);

FDetachmentTransformRules FDetachmentTransformRules::KeepRelativeTransform(EAttachmentRule::KeepRelative, false);
FDetachmentTransformRules FDetachmentTransformRules::KeepWorldTransform(EAttachmentRule::KeepWorld, false);

// ============================================================================
// Construction / Destruction
// ============================================================================

USceneComponent::USceneComponent()
    : RelativeLocation(FVector::ZeroVector)
    , RelativeRotation(FRotator::ZeroRotator)
    , RelativeScale3D(FVector::OneVector)
    , ComponentToWorld(FTransform::Identity)
    , Bounds()
    , AttachParent(nullptr)
    , Owner(nullptr)
    , Mobility(EComponentMobility::Movable)
    , bVisible(true)
    , bHiddenInGame(false)
    , bTransformDirty(true)
    , bBoundsDirty(true)
    , bIsRegistered(false)
    , bUseAttachParentBound(false)
    , bAbsoluteLocation(false)
    , bAbsoluteRotation(false)
    , bAbsoluteScale(false)
{
}

USceneComponent::USceneComponent(AActor* InOwner)
    : USceneComponent()
{
    Owner = InOwner;
}

USceneComponent::~USceneComponent()
{
    // Detach from parent
    if (AttachParent)
    {
        DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    }

    // Detach all children
    while (AttachChildren.Num() > 0)
    {
        USceneComponent* Child = AttachChildren.Last();
        Child->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    }
}

// ============================================================================
// Component Lifecycle
// ============================================================================

void USceneComponent::OnRegister()
{
    bIsRegistered = true;
    UpdateComponentToWorld();
    UpdateBounds();
    
    MR_LOG(LogSceneComponent, Verbose, "Component registered: %ls", *ComponentName);
}

void USceneComponent::OnUnregister()
{
    bIsRegistered = false;
    MR_LOG(LogSceneComponent, Verbose, "Component unregistered: %ls", *ComponentName);
}

void USceneComponent::OnComponentCreated()
{
    MR_LOG(LogSceneComponent, Verbose, "Component created: %ls", *ComponentName);
}

void USceneComponent::OnComponentDestroyed()
{
    MR_LOG(LogSceneComponent, Verbose, "Component destroyed: %ls", *ComponentName);
}

void USceneComponent::TickComponent(float DeltaTime)
{
    // Base implementation does nothing
    // Derived classes can override for per-frame updates
}

// ============================================================================
// Transform - Relative
// ============================================================================

void USceneComponent::SetRelativeLocation(const FVector& NewLocation)
{
    if (RelativeLocation != NewLocation)
    {
        RelativeLocation = NewLocation;
        MarkTransformDirty();
    }
}

void USceneComponent::SetRelativeRotation(const FRotator& NewRotation)
{
    if (RelativeRotation != NewRotation)
    {
        RelativeRotation = NewRotation;
        MarkTransformDirty();
    }
}

void USceneComponent::SetRelativeScale3D(const FVector& NewScale)
{
    if (RelativeScale3D != NewScale)
    {
        RelativeScale3D = NewScale;
        MarkTransformDirty();
    }
}

FTransform USceneComponent::GetRelativeTransform() const
{
    return FTransform(RelativeRotation.Quaternion(), RelativeLocation, RelativeScale3D);
}

void USceneComponent::SetRelativeTransform(const FTransform& NewTransform)
{
    RelativeLocation = NewTransform.GetTranslation();
    RelativeRotation = NewTransform.GetRotation().Rotator();
    RelativeScale3D = NewTransform.GetScale3D();
    MarkTransformDirty();
}

// ============================================================================
// Transform - World
// ============================================================================

FVector USceneComponent::GetComponentLocation() const
{
    return ComponentToWorld.GetTranslation();
}

void USceneComponent::SetWorldLocation(const FVector& NewLocation)
{
    if (AttachParent && !bAbsoluteLocation)
    {
        // Convert world location to relative
        FTransform ParentTransform = AttachParent->GetComponentTransform();
        FVector LocalLocation = ParentTransform.InverseTransformPosition(NewLocation);
        SetRelativeLocation(LocalLocation);
    }
    else
    {
        SetRelativeLocation(NewLocation);
    }
}

FRotator USceneComponent::GetComponentRotation() const
{
    return ComponentToWorld.GetRotation().Rotator();
}

void USceneComponent::SetWorldRotation(const FRotator& NewRotation)
{
    if (AttachParent && !bAbsoluteRotation)
    {
        // Convert world rotation to relative
        FTransform ParentTransform = AttachParent->GetComponentTransform();
        FQuat ParentQuat = ParentTransform.GetRotation();
        FQuat WorldQuat = NewRotation.Quaternion();
        FQuat LocalQuat = ParentQuat.Inverse() * WorldQuat;
        SetRelativeRotation(LocalQuat.Rotator());
    }
    else
    {
        SetRelativeRotation(NewRotation);
    }
}

FVector USceneComponent::GetComponentScale() const
{
    return ComponentToWorld.GetScale3D();
}

void USceneComponent::SetWorldScale3D(const FVector& NewScale)
{
    if (AttachParent && !bAbsoluteScale)
    {
        // Convert world scale to relative
        FVector ParentScale = AttachParent->GetComponentScale();
        FVector LocalScale(
            ParentScale.X != 0.0 ? NewScale.X / ParentScale.X : NewScale.X,
            ParentScale.Y != 0.0 ? NewScale.Y / ParentScale.Y : NewScale.Y,
            ParentScale.Z != 0.0 ? NewScale.Z / ParentScale.Z : NewScale.Z
        );
        SetRelativeScale3D(LocalScale);
    }
    else
    {
        SetRelativeScale3D(NewScale);
    }
}

void USceneComponent::SetWorldTransform(const FTransform& NewTransform)
{
    SetWorldLocation(NewTransform.GetTranslation());
    SetWorldRotation(NewTransform.GetRotation().Rotator());
    SetWorldScale3D(NewTransform.GetScale3D());
}

FMatrix USceneComponent::GetComponentToWorld() const
{
    return ComponentToWorld.ToMatrixWithScale();
}

// ============================================================================
// Transform - Directions
// ============================================================================

FVector USceneComponent::GetForwardVector() const
{
    return ComponentToWorld.GetRotation().GetForwardVector();
}

FVector USceneComponent::GetRightVector() const
{
    return ComponentToWorld.GetRotation().GetRightVector();
}

FVector USceneComponent::GetUpVector() const
{
    return ComponentToWorld.GetRotation().GetUpVector();
}

// ============================================================================
// Transform Updates
// ============================================================================

void USceneComponent::UpdateComponentToWorld()
{
    if (!bTransformDirty)
    {
        return;
    }

    FTransform RelativeTransform = GetRelativeTransform();

    if (AttachParent)
    {
        // Ensure parent is up to date
        AttachParent->UpdateComponentToWorld();

        FTransform ParentTransform = AttachParent->GetComponentTransform();

        // Apply absolute flags
        FVector Location = bAbsoluteLocation ? RelativeLocation : 
            ParentTransform.TransformPosition(RelativeLocation);
        
        FQuat Rotation = bAbsoluteRotation ? RelativeRotation.Quaternion() :
            ParentTransform.GetRotation() * RelativeRotation.Quaternion();
        
        FVector Scale = bAbsoluteScale ? RelativeScale3D :
            ParentTransform.GetScale3D() * RelativeScale3D;

        ComponentToWorld = FTransform(Rotation, Location, Scale);
    }
    else
    {
        ComponentToWorld = RelativeTransform;
    }

    bTransformDirty = false;
    bBoundsDirty = true;

    OnTransformUpdated();
    PropagateTransformUpdate();
}

void USceneComponent::MarkTransformDirty()
{
    if (!bTransformDirty)
    {
        bTransformDirty = true;
        bBoundsDirty = true;

        // Propagate to children
        for (USceneComponent* Child : AttachChildren)
        {
            if (Child)
            {
                Child->MarkTransformDirty();
            }
        }
    }
}

// ============================================================================
// Attachment
// ============================================================================

bool USceneComponent::AttachToComponent(
    USceneComponent* Parent,
    const FAttachmentTransformRules& AttachmentRules,
    const FString& SocketName)
{
    if (!Parent || Parent == this)
    {
        return false;
    }

    // Check for circular attachment
    if (Parent->IsAttachedTo(this))
    {
        MR_LOG(LogSceneComponent, Warning, "Cannot attach - would create circular dependency");
        return false;
    }

    // Detach from current parent
    if (AttachParent)
    {
        DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    }

    // Store current world transform if needed
    FTransform OldWorldTransform = GetComponentTransform();

    // Set new parent
    AttachParent = Parent;
    AttachSocketName = SocketName;
    Parent->AddChild(this);

    // Apply attachment rules
    switch (AttachmentRules.LocationRule)
    {
    case EAttachmentRule::KeepRelative:
        // Keep current relative location
        break;
    case EAttachmentRule::KeepWorld:
        SetWorldLocation(OldWorldTransform.GetTranslation());
        break;
    case EAttachmentRule::SnapToTarget:
        SetRelativeLocation(FVector::ZeroVector);
        break;
    }

    switch (AttachmentRules.RotationRule)
    {
    case EAttachmentRule::KeepRelative:
        break;
    case EAttachmentRule::KeepWorld:
        SetWorldRotation(OldWorldTransform.GetRotation().Rotator());
        break;
    case EAttachmentRule::SnapToTarget:
        SetRelativeRotation(FRotator::ZeroRotator);
        break;
    }

    switch (AttachmentRules.ScaleRule)
    {
    case EAttachmentRule::KeepRelative:
        break;
    case EAttachmentRule::KeepWorld:
        SetWorldScale3D(OldWorldTransform.GetScale3D());
        break;
    case EAttachmentRule::SnapToTarget:
        SetRelativeScale3D(FVector::OneVector);
        break;
    }

    OnAttachmentChanged();
    
    MR_LOG(LogSceneComponent, Verbose, "Component attached to parent");
    return true;
}

void USceneComponent::DetachFromComponent(const FDetachmentTransformRules& DetachmentRules)
{
    if (!AttachParent)
    {
        return;
    }

    // Store current world transform if needed
    FTransform OldWorldTransform = GetComponentTransform();

    // Remove from parent
    AttachParent->RemoveChild(this);
    AttachParent = nullptr;
    AttachSocketName.Empty();

    // Apply detachment rules
    switch (DetachmentRules.LocationRule)
    {
    case EAttachmentRule::KeepRelative:
        // Keep current relative location (now becomes world location)
        break;
    case EAttachmentRule::KeepWorld:
        RelativeLocation = OldWorldTransform.GetTranslation();
        break;
    }

    switch (DetachmentRules.RotationRule)
    {
    case EAttachmentRule::KeepRelative:
        break;
    case EAttachmentRule::KeepWorld:
        RelativeRotation = OldWorldTransform.GetRotation().Rotator();
        break;
    }

    switch (DetachmentRules.ScaleRule)
    {
    case EAttachmentRule::KeepRelative:
        break;
    case EAttachmentRule::KeepWorld:
        RelativeScale3D = OldWorldTransform.GetScale3D();
        break;
    }

    MarkTransformDirty();
    OnAttachmentChanged();
    
    MR_LOG(LogSceneComponent, Verbose, "Component detached from parent");
}

bool USceneComponent::IsAttachedTo(const USceneComponent* TestComp) const
{
    if (!TestComp)
    {
        return false;
    }

    const USceneComponent* Current = AttachParent;
    while (Current)
    {
        if (Current == TestComp)
        {
            return true;
        }
        Current = Current->AttachParent;
    }
    return false;
}

USceneComponent* USceneComponent::GetAttachmentRoot() const
{
    const USceneComponent* Current = this;
    while (Current->AttachParent)
    {
        Current = Current->AttachParent;
    }
    return const_cast<USceneComponent*>(Current);
}

// ============================================================================
// Visibility
// ============================================================================

void USceneComponent::SetVisibility(bool bNewVisibility, bool bPropagateToChildren)
{
    bVisible = bNewVisibility;

    if (bPropagateToChildren)
    {
        for (USceneComponent* Child : AttachChildren)
        {
            if (Child)
            {
                Child->SetVisibility(bNewVisibility, true);
            }
        }
    }
}

void USceneComponent::SetHiddenInGame(bool bNewHidden, bool bPropagateToChildren)
{
    bHiddenInGame = bNewHidden;

    if (bPropagateToChildren)
    {
        for (USceneComponent* Child : AttachChildren)
        {
            if (Child)
            {
                Child->SetHiddenInGame(bNewHidden, true);
            }
        }
    }
}

// ============================================================================
// Mobility
// ============================================================================

void USceneComponent::SetMobility(EComponentMobility NewMobility)
{
    if (Mobility != NewMobility)
    {
        Mobility = NewMobility;
        // Mobility changes may require render state updates
    }
}

// ============================================================================
// Bounds
// ============================================================================

FBoxSphereBounds USceneComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    // Default implementation returns a small bounds at the component location
    return FBoxSphereBounds(LocalToWorld.GetTranslation(), FVector(1.0, 1.0, 1.0), 1.0);
}

FBox USceneComponent::CalcLocalBounds() const
{
    return FBox(FVector(-1.0, -1.0, -1.0), FVector(1.0, 1.0, 1.0));
}

void USceneComponent::UpdateBounds()
{
    if (!bBoundsDirty)
    {
        return;
    }

    if (bUseAttachParentBound && AttachParent)
    {
        Bounds = AttachParent->GetBounds();
    }
    else
    {
        Bounds = CalcBounds(ComponentToWorld);
    }

    bBoundsDirty = false;
}

// ============================================================================
// Protected Methods
// ============================================================================

void USceneComponent::OnTransformUpdated()
{
    // Override in derived classes for transform change notifications
}

void USceneComponent::OnAttachmentChanged()
{
    // Override in derived classes for attachment change notifications
}

void USceneComponent::PropagateTransformUpdate()
{
    for (USceneComponent* Child : AttachChildren)
    {
        if (Child)
        {
            Child->MarkTransformDirty();
            Child->UpdateComponentToWorld();
        }
    }
}

void USceneComponent::AddChild(USceneComponent* Child)
{
    if (Child && !AttachChildren.Contains(Child))
    {
        AttachChildren.Add(Child);
    }
}

void USceneComponent::RemoveChild(USceneComponent* Child)
{
    if (Child)
    {
        AttachChildren.Remove(Child);
    }
}

} // namespace MonsterEngine
