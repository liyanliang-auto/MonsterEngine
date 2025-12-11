// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Actor.h
 * @brief Base class for all actors in the engine
 * 
 * AActor is the base class for all objects that can be placed in a level.
 * Actors can contain components and have a transform in the world.
 * Following UE5's actor architecture.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h
 */

#include "Core/CoreMinimal.h"
#include "Math/MonsterMath.h"
#include "Containers/Array.h"
#include "Containers/String.h"

namespace MonsterEngine
{

// Forward declarations
class USceneComponent;
class UPrimitiveComponent;
class FScene;

/**
 * Base class for all actors
 * 
 * An Actor is an object that can be placed or spawned in the world.
 * Actors may contain a collection of ActorComponents, which can be used
 * to control how actors move, how they are rendered, etc.
 * 
 * Reference UE5: AActor
 */
class AActor
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /** Default constructor */
    AActor();

    /** Virtual destructor */
    virtual ~AActor();

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * Called when the actor is spawned or placed in the world
     */
    virtual void BeginPlay();

    /**
     * Called when the actor is being destroyed
     */
    virtual void EndPlay();

    /**
     * Called every frame to update the actor
     * @param DeltaTime - Time elapsed since last frame in seconds
     */
    virtual void Tick(float DeltaTime);

    /**
     * Check if this actor should tick
     * @return True if the actor should tick
     */
    bool IsTickEnabled() const { return bTickEnabled; }

    /**
     * Enable or disable ticking for this actor
     * @param bEnabled - True to enable ticking
     */
    void SetTickEnabled(bool bEnabled) { bTickEnabled = bEnabled; }

    // ========================================================================
    // Transform
    // ========================================================================

    /**
     * Get the actor's world location
     * @return World location
     */
    FVector GetActorLocation() const;

    /**
     * Get the actor's world rotation
     * @return World rotation
     */
    FRotator GetActorRotation() const;

    /**
     * Get the actor's world scale
     * @return World scale
     */
    FVector GetActorScale() const;

    /**
     * Get the actor's world transform
     * @return World transform
     */
    FTransform GetActorTransform() const;

    /**
     * Set the actor's world location
     * @param NewLocation - New world location
     * @param bSweep - Whether to sweep for collision
     * @param bTeleport - Whether this is a teleport (affects physics)
     * @return True if the location was set successfully
     */
    bool SetActorLocation(const FVector& NewLocation, bool bSweep = false, bool bTeleport = false);

    /**
     * Set the actor's world rotation
     * @param NewRotation - New world rotation
     * @param bTeleport - Whether this is a teleport
     * @return True if the rotation was set successfully
     */
    bool SetActorRotation(const FRotator& NewRotation, bool bTeleport = false);

    /**
     * Set the actor's world scale
     * @param NewScale - New world scale
     */
    void SetActorScale3D(const FVector& NewScale);

    /**
     * Set the actor's world transform
     * @param NewTransform - New world transform
     * @param bSweep - Whether to sweep for collision
     * @param bTeleport - Whether this is a teleport
     * @return True if the transform was set successfully
     */
    bool SetActorTransform(const FTransform& NewTransform, bool bSweep = false, bool bTeleport = false);

    /**
     * Add delta to the actor's location
     * @param DeltaLocation - Delta to add
     * @param bSweep - Whether to sweep for collision
     * @param bTeleport - Whether this is a teleport
     * @return True if successful
     */
    bool AddActorWorldOffset(const FVector& DeltaLocation, bool bSweep = false, bool bTeleport = false);

    /**
     * Add delta to the actor's rotation
     * @param DeltaRotation - Delta to add
     * @param bSweep - Whether to sweep for collision
     * @param bTeleport - Whether this is a teleport
     * @return True if successful
     */
    bool AddActorWorldRotation(const FRotator& DeltaRotation, bool bSweep = false, bool bTeleport = false);

    /**
     * Add delta to the actor's local rotation
     * @param DeltaRotation - Delta to add
     * @param bSweep - Whether to sweep for collision
     * @param bTeleport - Whether this is a teleport
     * @return True if successful
     */
    bool AddActorLocalRotation(const FRotator& DeltaRotation, bool bSweep = false, bool bTeleport = false);

    // ========================================================================
    // Components
    // ========================================================================

    /**
     * Get the root component
     * @return Root component, or nullptr if none
     */
    USceneComponent* GetRootComponent() const { return RootComponent; }

    /**
     * Set the root component
     * @param NewRootComponent - New root component
     */
    void SetRootComponent(USceneComponent* NewRootComponent);

    /**
     * Add a component to this actor
     * @param Component - Component to add
     */
    void AddComponent(USceneComponent* Component);

    /**
     * Remove a component from this actor
     * @param Component - Component to remove
     */
    void RemoveComponent(USceneComponent* Component);

    /**
     * Get all components of this actor
     * @return Array of components
     */
    const TArray<USceneComponent*>& GetComponents() const { return Components; }

    /**
     * Get all primitive components (for rendering)
     * @param OutComponents - Array to receive primitive components
     */
    void GetPrimitiveComponents(TArray<UPrimitiveComponent*>& OutComponents) const;

    // ========================================================================
    // Scene
    // ========================================================================

    /**
     * Get the scene this actor belongs to
     * @return Scene, or nullptr if not in a scene
     */
    FScene* GetScene() const { return Scene; }

    /**
     * Set the scene (called when added to scene)
     * @param InScene - Scene to set
     */
    void SetScene(FScene* InScene) { Scene = InScene; }

    /**
     * Check if this actor is in a scene
     * @return True if in a scene
     */
    bool IsInScene() const { return Scene != nullptr; }

    // ========================================================================
    // Identification
    // ========================================================================

    /**
     * Get the actor's name
     * @return Actor name
     */
    const FString& GetName() const { return ActorName; }

    /**
     * Set the actor's name
     * @param NewName - New name
     */
    void SetName(const FString& NewName) { ActorName = NewName; }

    /**
     * Get the actor's unique ID
     * @return Unique ID
     */
    uint32 GetUniqueID() const { return UniqueID; }

    // ========================================================================
    // Visibility
    // ========================================================================

    /**
     * Check if the actor is hidden
     * @return True if hidden
     */
    bool IsHidden() const { return bHidden; }

    /**
     * Set the actor's hidden state
     * @param bNewHidden - True to hide
     */
    void SetHidden(bool bNewHidden);

    /**
     * Set actor and components visibility
     * @param bNewVisibility - True for visible
     * @param bPropagateToChildren - Whether to propagate to children
     */
    void SetActorHiddenInGame(bool bNewHidden, bool bPropagateToChildren = true);

protected:
    /** Root component of this actor */
    USceneComponent* RootComponent;

    /** All components owned by this actor */
    TArray<USceneComponent*> Components;

    /** Scene this actor belongs to */
    FScene* Scene;

    /** Actor name for debugging */
    FString ActorName;

    /** Unique ID for this actor */
    uint32 UniqueID;

    /** Whether ticking is enabled */
    uint8 bTickEnabled : 1;

    /** Whether the actor is hidden */
    uint8 bHidden : 1;

    /** Whether BeginPlay has been called */
    uint8 bHasBegunPlay : 1;

private:
    /** Static counter for generating unique IDs */
    static uint32 NextUniqueID;
};

} // namespace MonsterEngine
