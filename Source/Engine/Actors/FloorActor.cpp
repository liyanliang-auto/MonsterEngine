// Copyright Monster Engine. All Rights Reserved.

/**
 * @file FloorActor.cpp
 * @brief Implementation of floor actor
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/Actor.cpp
 */

#include "Engine/Actors/FloorActor.h"
#include "Engine/Components/FloorMeshComponent.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogFloorActor, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

AFloorActor::AFloorActor()
    : AActor()
    , FloorMeshComponent(nullptr)
    , FloorSize(25.0f)
    , TextureTile(25.0f)
{
    // Create the floor mesh component
    FloorMeshComponent = new UFloorMeshComponent(this);
    AddComponent(FloorMeshComponent);
    SetRootComponent(FloorMeshComponent);
    
    // Set default properties
    FloorMeshComponent->SetFloorSize(FloorSize);
    FloorMeshComponent->SetTextureTile(TextureTile);
    
    // Set actor name
    SetName(TEXT("FloorActor"));
    
    MR_LOG(LogFloorActor, Log, "FloorActor created");
}

AFloorActor::~AFloorActor()
{
    // Components are cleaned up by base class
    FloorMeshComponent = nullptr;
    
    MR_LOG(LogFloorActor, Log, "FloorActor destroyed");
}

// ============================================================================
// Lifecycle
// ============================================================================

void AFloorActor::BeginPlay()
{
    AActor::BeginPlay();
    
    MR_LOG(LogFloorActor, Log, "FloorActor BeginPlay");
}

void AFloorActor::Tick(float DeltaTime)
{
    AActor::Tick(DeltaTime);
    
    // Floor doesn't need any special tick behavior
}

// ============================================================================
// Floor Settings
// ============================================================================

void AFloorActor::SetFloorSize(float Size)
{
    FloorSize = FMath::Max(0.1f, Size);
    
    if (FloorMeshComponent)
    {
        FloorMeshComponent->SetFloorSize(FloorSize);
    }
}

void AFloorActor::SetTextureTile(float Factor)
{
    TextureTile = FMath::Max(0.1f, Factor);
    
    if (FloorMeshComponent)
    {
        FloorMeshComponent->SetTextureTile(TextureTile);
    }
}

} // namespace MonsterEngine
