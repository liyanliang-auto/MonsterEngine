// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SceneComponent.h
 * @brief Base class for scene components with transform
 * 
 * USceneComponent is the base class for all components that have a transform
 * and can be attached to other components. It provides the foundation for
 * the scene hierarchy. Following UE5's component architecture.
 */

#include "Engine/SceneTypes.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Math/Matrix.h"
#include "Containers/Array.h"
#include "Containers/String.h"

namespace MonsterEngine
{

// Forward declarations
class AActor;
class USceneComponent;

/**
 * Attachment rules for component attachment
 */
enum class EAttachmentRule : uint8
{
    /** Keep the current relative transform */
    KeepRelative,
    
    /** Keep the current world transform */
    KeepWorld,
    
    /** Snap to the target (reset relative transform) */
    SnapToTarget
};

/**
 * Attachment transform rules
 */
struct FAttachmentTransformRules
{
    /** Rule for location */
    EAttachmentRule LocationRule;
    
    /** Rule for rotation */
    EAttachmentRule RotationRule;
    
    /** Rule for scale */
    EAttachmentRule ScaleRule;
    
    /** Whether to weld simulated bodies */
    bool bWeldSimulatedBodies;

    FAttachmentTransformRules(EAttachmentRule InRule, bool bInWeldSimulatedBodies = false)
        : LocationRule(InRule)
        , RotationRule(InRule)
        , ScaleRule(InRule)
        , bWeldSimulatedBodies(bInWeldSimulatedBodies)
    {
    }

    FAttachmentTransformRules(
        EAttachmentRule InLocationRule,
        EAttachmentRule InRotationRule,
        EAttachmentRule InScaleRule,
        bool bInWeldSimulatedBodies = false)
        : LocationRule(InLocationRule)
        , RotationRule(InRotationRule)
        , ScaleRule(InScaleRule)
        , bWeldSimulatedBodies(bInWeldSimulatedBodies)
    {
    }

    /** Keep relative transform */
    static FAttachmentTransformRules KeepRelativeTransform;
    
    /** Keep world transform */
    static FAttachmentTransformRules KeepWorldTransform;
    
    /** Snap to target */
    static FAttachmentTransformRules SnapToTargetNotIncludingScale;
    
    /** Snap to target including scale */
    static FAttachmentTransformRules SnapToTargetIncludingScale;
};

/**
 * Detachment transform rules
 */
struct FDetachmentTransformRules
{
    /** Rule for location */
    EAttachmentRule LocationRule;
    
    /** Rule for rotation */
    EAttachmentRule RotationRule;
    
    /** Rule for scale */
    EAttachmentRule ScaleRule;
    
    /** Whether to call modify on the component */
    bool bCallModify;

    FDetachmentTransformRules(EAttachmentRule InRule, bool bInCallModify = false)
        : LocationRule(InRule)
        , RotationRule(InRule)
        , ScaleRule(InRule)
        , bCallModify(bInCallModify)
    {
    }

    /** Keep relative transform */
    static FDetachmentTransformRules KeepRelativeTransform;
    
    /** Keep world transform */
    static FDetachmentTransformRules KeepWorldTransform;
};

/**
 * Base class for components that have a transform
 * 
 * USceneComponent provides:
 * - Transform (location, rotation, scale)
 * - Parent-child hierarchy
 * - Attachment system
 * - Visibility flags
 * - Mobility settings
 */
class USceneComponent
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /** Default constructor */
    USceneComponent();

    /** Constructor with owner */
    explicit USceneComponent(AActor* InOwner);

    /** Virtual destructor */
    virtual ~USceneComponent();

    // ========================================================================
    // Component Lifecycle
    // ========================================================================

    /** Called when the component is registered with the world */
    virtual void OnRegister();

    /** Called when the component is unregistered from the world */
    virtual void OnUnregister();

    /** Called when the component is created */
    virtual void OnComponentCreated();

    /** Called when the component is destroyed */
    virtual void OnComponentDestroyed();

    /** Called every frame to update the component */
    virtual void TickComponent(float DeltaTime);

    // ========================================================================
    // Transform - Relative
    // ========================================================================

    /** Get the relative location */
    const FVector& GetRelativeLocation() const { return RelativeLocation; }

    /** Set the relative location */
    void SetRelativeLocation(const FVector& NewLocation);

    /** Get the relative rotation */
    const FRotator& GetRelativeRotation() const { return RelativeRotation; }

    /** Set the relative rotation */
    void SetRelativeRotation(const FRotator& NewRotation);

    /** Get the relative scale */
    const FVector& GetRelativeScale3D() const { return RelativeScale3D; }

    /** Set the relative scale */
    void SetRelativeScale3D(const FVector& NewScale);

    /** Get the relative transform */
    FTransform GetRelativeTransform() const;

    /** Set the relative transform */
    void SetRelativeTransform(const FTransform& NewTransform);

    // ========================================================================
    // Transform - World
    // ========================================================================

    /** Get the world location */
    FVector GetComponentLocation() const;

    /** Set the world location */
    void SetWorldLocation(const FVector& NewLocation);

    /** Get the world rotation */
    FRotator GetComponentRotation() const;

    /** Set the world rotation */
    void SetWorldRotation(const FRotator& NewRotation);

    /** Get the world scale */
    FVector GetComponentScale() const;

    /** Set the world scale */
    void SetWorldScale3D(const FVector& NewScale);

    /** Get the world transform */
    const FTransform& GetComponentTransform() const { return ComponentToWorld; }

    /** Set the world transform */
    void SetWorldTransform(const FTransform& NewTransform);

    /** Get the component to world matrix */
    FMatrix GetComponentToWorld() const;

    // ========================================================================
    // Transform - Directions
    // ========================================================================

    /** Get the forward vector in world space */
    FVector GetForwardVector() const;

    /** Get the right vector in world space */
    FVector GetRightVector() const;

    /** Get the up vector in world space */
    FVector GetUpVector() const;

    // ========================================================================
    // Transform Updates
    // ========================================================================

    /** Update the component to world transform */
    virtual void UpdateComponentToWorld();

    /** Mark the transform as dirty */
    void MarkTransformDirty();

    /** Check if the transform is dirty */
    bool IsTransformDirty() const { return bTransformDirty; }

    // ========================================================================
    // Attachment
    // ========================================================================

    /** Get the parent component */
    USceneComponent* GetAttachParent() const { return AttachParent; }

    /** Get the attached children */
    const TArray<USceneComponent*>& GetAttachChildren() const { return AttachChildren; }

    /**
     * Attach this component to another component
     * @param Parent The component to attach to
     * @param AttachmentRules The attachment rules to use
     * @param SocketName Optional socket name to attach to
     * @return True if attachment was successful
     */
    bool AttachToComponent(
        USceneComponent* Parent,
        const FAttachmentTransformRules& AttachmentRules,
        const FString& SocketName = FString());

    /**
     * Detach this component from its parent
     * @param DetachmentRules The detachment rules to use
     */
    void DetachFromComponent(const FDetachmentTransformRules& DetachmentRules);

    /** Check if this component is attached to another */
    bool IsAttachedTo(const USceneComponent* TestComp) const;

    /** Get the root component of the attachment hierarchy */
    USceneComponent* GetAttachmentRoot() const;

    // ========================================================================
    // Visibility
    // ========================================================================

    /** Check if the component is visible */
    bool IsVisible() const { return bVisible; }

    /** Set visibility */
    void SetVisibility(bool bNewVisibility, bool bPropagateToChildren = false);

    /** Check if the component is hidden in game */
    bool IsHiddenInGame() const { return bHiddenInGame; }

    /** Set hidden in game */
    void SetHiddenInGame(bool bNewHidden, bool bPropagateToChildren = false);

    // ========================================================================
    // Mobility
    // ========================================================================

    /** Get the mobility */
    EComponentMobility GetMobility() const { return Mobility; }

    /** Set the mobility */
    void SetMobility(EComponentMobility NewMobility);

    /** Check if the component is static */
    bool IsStatic() const { return Mobility == EComponentMobility::Static; }

    /** Check if the component is movable */
    bool IsMovable() const { return Mobility == EComponentMobility::Movable; }

    // ========================================================================
    // Bounds
    // ========================================================================

    /** Get the bounds of this component */
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const;

    /** Get the local bounds */
    virtual FBox CalcLocalBounds() const;

    /** Get the cached bounds */
    const FBoxSphereBounds& GetBounds() const { return Bounds; }

    /** Update the bounds */
    virtual void UpdateBounds();

    // ========================================================================
    // Owner
    // ========================================================================

    /** Get the owning actor */
    AActor* GetOwner() const { return Owner; }

    /** Set the owning actor */
    void SetOwner(AActor* NewOwner) { Owner = NewOwner; }

    // ========================================================================
    // Component Name
    // ========================================================================

    /** Get the component name */
    const FString& GetComponentName() const { return ComponentName; }

    /** Set the component name */
    void SetComponentName(const FString& NewName) { ComponentName = NewName; }

protected:
    // ========================================================================
    // Protected Methods
    // ========================================================================

    /** Called when the transform is updated */
    virtual void OnTransformUpdated();

    /** Called when attached to a parent */
    virtual void OnAttachmentChanged();

    /** Propagate transform update to children */
    void PropagateTransformUpdate();

    /** Add a child component */
    void AddChild(USceneComponent* Child);

    /** Remove a child component */
    void RemoveChild(USceneComponent* Child);

protected:
    // ========================================================================
    // Transform Data
    // ========================================================================

    /** Location relative to parent */
    FVector RelativeLocation;

    /** Rotation relative to parent */
    FRotator RelativeRotation;

    /** Scale relative to parent */
    FVector RelativeScale3D;

    /** Cached component to world transform */
    FTransform ComponentToWorld;

    /** Cached bounds */
    FBoxSphereBounds Bounds;

    // ========================================================================
    // Hierarchy
    // ========================================================================

    /** Parent component */
    USceneComponent* AttachParent;

    /** Child components */
    TArray<USceneComponent*> AttachChildren;

    /** Socket name for attachment */
    FString AttachSocketName;

    /** Owning actor */
    AActor* Owner;

    /** Component name */
    FString ComponentName;

    // ========================================================================
    // Mobility
    // ========================================================================

    /** How the component can move */
    EComponentMobility Mobility;

    // ========================================================================
    // Flags
    // ========================================================================

    /** Whether the component is visible */
    uint8 bVisible : 1;

    /** Whether the component is hidden in game */
    uint8 bHiddenInGame : 1;

    /** Whether the transform is dirty */
    uint8 bTransformDirty : 1;

    /** Whether the bounds are dirty */
    uint8 bBoundsDirty : 1;

    /** Whether the component is registered */
    uint8 bIsRegistered : 1;

    /** Whether to use attach parent bounds */
    uint8 bUseAttachParentBound : 1;

    /** Whether absolute location is used */
    uint8 bAbsoluteLocation : 1;

    /** Whether absolute rotation is used */
    uint8 bAbsoluteRotation : 1;

    /** Whether absolute scale is used */
    uint8 bAbsoluteScale : 1;
};

} // namespace MonsterEngine
