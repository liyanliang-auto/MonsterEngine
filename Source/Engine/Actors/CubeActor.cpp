// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CubeActor.cpp
 * @brief Implementation of rotating cube actor
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/Actor.cpp
 */

#include "Engine/Actors/CubeActor.h"
#include "Engine/Components/CubeMeshComponent.h"
#include "Core/Logging/LogMacros.h"
#include "Math/MonsterMath.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogCubeActor, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

ACubeActor::ACubeActor()
    : AActor()
    , CubeMeshComponent(nullptr)
    , RotationSpeed(1.0f)
    , RotationAxis(0.5f, 1.0f, 0.0f)  // Default axis like LearnOpenGL tutorial
    , CurrentAngle(0.0f)
    , bRotationEnabled(true)
{
    // Normalize the rotation axis
    RotationAxis = RotationAxis.GetSafeNormal();
    
    // Create the cube mesh component
    CubeMeshComponent = new UCubeMeshComponent(this);
    AddComponent(CubeMeshComponent);
    SetRootComponent(CubeMeshComponent);
    
    // Set actor name
    SetName(TEXT("CubeActor"));
    
    MR_LOG(LogCubeActor, Log, "CubeActor created");
}

ACubeActor::~ACubeActor()
{
    // Components are cleaned up by base class
    CubeMeshComponent = nullptr;
    
    MR_LOG(LogCubeActor, Log, "CubeActor destroyed");
}

// ============================================================================
// Lifecycle
// ============================================================================

void ACubeActor::BeginPlay()
{
    AActor::BeginPlay();
    
    MR_LOG(LogCubeActor, Log, "CubeActor BeginPlay");
}

void ACubeActor::Tick(float DeltaTime)
{
    AActor::Tick(DeltaTime);
    
    if (!bRotationEnabled)
    {
        return;
    }
    
    // Update rotation angle
    CurrentAngle += RotationSpeed * DeltaTime;
    
    // Keep angle in reasonable range
    const float TwoPI = 2.0f * 3.14159265358979323846f;
    if (CurrentAngle > TwoPI)
    {
        CurrentAngle -= TwoPI;
    }
    
    // Create rotation from axis-angle
    // Convert angle to degrees for FRotator
    float AngleDegrees = FMath::RadiansToDegrees(CurrentAngle);
    
    // Create quaternion from axis-angle rotation
    FQuat RotationQuat = FQuat(RotationAxis, CurrentAngle);
    FRotator NewRotation = RotationQuat.Rotator();
    
    // Apply rotation to the actor
    SetActorRotation(NewRotation);
}

} // namespace MonsterEngine
