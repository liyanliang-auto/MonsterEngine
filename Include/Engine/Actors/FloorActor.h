// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file FloorActor.h
 * @brief Floor actor for rendering textured floor planes
 * 
 * AFloorActor is a simple actor that displays a textured floor plane.
 * It demonstrates integration with the scene system and forward rendering
 * pipeline with lighting and shadow support.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h
 */

#include "Engine/Actor.h"
#include "Math/MonsterMath.h"

namespace MonsterEngine
{

// Forward declarations
class UFloorMeshComponent;

/**
 * A floor actor for rendering textured floor planes
 * 
 * This actor contains a floor mesh component for rendering.
 * It demonstrates:
 * - Actor-Component pattern
 * - Scene integration
 * - Forward rendering with lighting and shadows
 */
class AFloorActor : public AActor
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /** Default constructor */
    AFloorActor();

    /** Virtual destructor */
    virtual ~AFloorActor();

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * Called when the actor is spawned
     */
    virtual void BeginPlay() override;

    /**
     * Called every frame to update the actor
     * @param DeltaTime - Time elapsed since last frame
     */
    virtual void Tick(float DeltaTime) override;

    // ========================================================================
    // Floor Settings
    // ========================================================================

    /**
     * Set the floor size (half-extent)
     * @param Size - Half-extent of the floor plane
     */
    void SetFloorSize(float Size);

    /**
     * Get the floor size
     * @return Half-extent of the floor plane
     */
    float GetFloorSize() const { return FloorSize; }

    /**
     * Set texture tile factor
     * @param Factor - How many times texture repeats
     */
    void SetTextureTile(float Factor);

    /**
     * Get texture tile factor
     * @return Tile factor
     */
    float GetTextureTile() const { return TextureTile; }

    // ========================================================================
    // Component Access
    // ========================================================================

    /**
     * Get the floor mesh component
     * @return Floor mesh component
     */
    UFloorMeshComponent* GetFloorMeshComponent() const { return FloorMeshComponent; }

protected:
    /** Floor mesh component */
    UFloorMeshComponent* FloorMeshComponent;

    /** Floor half-extent */
    float FloorSize;

    /** Texture tile factor */
    float TextureTile;
};

} // namespace MonsterEngine
