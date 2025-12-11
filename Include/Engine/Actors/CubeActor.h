// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CubeActor.h
 * @brief Rotating cube actor for demonstration
 * 
 * ACubeActor is a simple actor that displays a rotating textured cube.
 * It demonstrates integration with the scene system, camera system,
 * and forward rendering pipeline with lighting support.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h
 */

#include "Engine/Actor.h"
#include "Math/MonsterMath.h"

namespace MonsterEngine
{

// Forward declarations
class UCubeMeshComponent;

/**
 * A rotating cube actor for demonstration purposes
 * 
 * This actor contains a cube mesh component and rotates it over time.
 * It demonstrates:
 * - Actor-Component pattern
 * - Scene integration
 * - Camera system usage
 * - Forward rendering with lighting
 */
class ACubeActor : public AActor
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /** Default constructor */
    ACubeActor();

    /** Virtual destructor */
    virtual ~ACubeActor();

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
    // Rotation Settings
    // ========================================================================

    /**
     * Set the rotation speed
     * @param Speed - Rotation speed in radians per second
     */
    void SetRotationSpeed(float Speed) { RotationSpeed = Speed; }

    /**
     * Get the rotation speed
     * @return Rotation speed in radians per second
     */
    float GetRotationSpeed() const { return RotationSpeed; }

    /**
     * Set the rotation axis
     * @param Axis - Normalized rotation axis
     */
    void SetRotationAxis(const FVector& Axis) { RotationAxis = Axis.GetSafeNormal(); }

    /**
     * Get the rotation axis
     * @return Rotation axis
     */
    const FVector& GetRotationAxis() const { return RotationAxis; }

    /**
     * Enable or disable rotation
     * @param bEnable - True to enable rotation
     */
    void SetRotationEnabled(bool bEnable) { bRotationEnabled = bEnable; }

    /**
     * Check if rotation is enabled
     * @return True if rotation is enabled
     */
    bool IsRotationEnabled() const { return bRotationEnabled; }

    /**
     * Get the current rotation angle
     * @return Current rotation angle in radians
     */
    float GetCurrentAngle() const { return CurrentAngle; }

    // ========================================================================
    // Components
    // ========================================================================

    /**
     * Get the cube mesh component
     * @return Cube mesh component
     */
    UCubeMeshComponent* GetCubeMeshComponent() const { return CubeMeshComponent; }

protected:
    /** Cube mesh component */
    UCubeMeshComponent* CubeMeshComponent;

    /** Rotation speed in radians per second */
    float RotationSpeed;

    /** Rotation axis (normalized) */
    FVector RotationAxis;

    /** Current rotation angle in radians */
    float CurrentAngle;

    /** Whether rotation is enabled */
    uint8 bRotationEnabled : 1;
};

} // namespace MonsterEngine
