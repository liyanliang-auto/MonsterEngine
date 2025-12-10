// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SceneVisibility.cpp
 * @brief Implementation of scene visibility and culling system
 */

#include "Engine/SceneVisibility.h"
#include "Engine/Scene.h"
#include "Engine/SceneView.h"
#include "Engine/PrimitiveSceneInfo.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

DEFINE_LOG_CATEGORY_STATIC(LogSceneVisibility, Log, All);

// ============================================================================
// FOcclusionQueryManager Implementation
// ============================================================================

void FOcclusionQueryManager::BeginFrame(uint32 FrameNumber)
{
    CurrentFrameNumber = FrameNumber;
    
    // Clear pending queries from previous frame
    PendingQueries.Empty();
}

void FOcclusionQueryManager::EndFrame()
{
    // In a real implementation, this would read back query results from GPU
    // For now, we just mark all pending queries as complete and visible
    for (int32 PrimitiveIndex : PendingQueries)
    {
        FOcclusionQueryResult& Result = QueryResults.FindOrAdd(PrimitiveIndex);
        Result.PrimitiveIndex = PrimitiveIndex;
        Result.NumVisibleSamples = 1; // Assume visible
        Result.bQueryComplete = true;
    }
    
    PendingQueries.Empty();
}

bool FOcclusionQueryManager::RequestQuery(int32 PrimitiveIndex, const FBoxSphereBounds& Bounds)
{
    if (PendingQueries.Num() >= MaxPendingQueries)
    {
        return false;
    }
    
    PendingQueries.Add(PrimitiveIndex);
    return true;
}

bool FOcclusionQueryManager::GetQueryResult(int32 PrimitiveIndex, FOcclusionQueryResult& OutResult) const
{
    const FOcclusionQueryResult* Result = QueryResults.Find(PrimitiveIndex);
    if (Result && Result->bQueryComplete)
    {
        OutResult = *Result;
        return true;
    }
    
    return false;
}

bool FOcclusionQueryManager::WasVisibleLastFrame(int32 PrimitiveIndex) const
{
    const FOcclusionQueryResult* Result = QueryResults.Find(PrimitiveIndex);
    if (Result && Result->bQueryComplete)
    {
        return Result->IsVisible();
    }
    
    // If no query exists, assume visible
    return true;
}

// ============================================================================
// FSceneVisibilityManager Implementation
// ============================================================================

void FSceneVisibilityManager::ComputeVisibility(const FScene& Scene, const FSceneView& View, FViewVisibilityResult& OutResult)
{
    // Get number of primitives
    int32 NumPrimitives = Scene.GetNumPrimitives();
    
    // Initialize result
    OutResult.Init(NumPrimitives);
    
    // Ensure visibility states array is large enough
    if (VisibilityStates.Num() < NumPrimitives)
    {
        VisibilityStates.SetNum(NumPrimitives);
    }
    
    // Perform frustum culling first
    FrustumCull(Scene, View, OutResult);
    
    // Then distance culling
    DistanceCull(Scene, View, OutResult);
    
    // Finally occlusion culling (if enabled)
    OcclusionCull(Scene, View, OutResult);
    
    MR_LOG(LogSceneVisibility, Verbose, 
           "Visibility computed: %d visible, %d frustum culled, %d distance culled, %d occlusion culled",
           OutResult.NumVisiblePrimitives, OutResult.NumFrustumCulled, 
           OutResult.NumDistanceCulled, OutResult.NumOcclusionCulled);
}

void FSceneVisibilityManager::FrustumCull(const FScene& Scene, const FSceneView& View, FViewVisibilityResult& OutResult)
{
    FFrustumCuller FrustumCuller(View.ViewFrustum);
    
    const TArray<FPrimitiveSceneInfo*>& Primitives = Scene.GetPrimitives();
    
    for (int32 PrimitiveIndex = 0; PrimitiveIndex < Primitives.Num(); ++PrimitiveIndex)
    {
        FPrimitiveSceneInfo* PrimitiveInfo = Primitives[PrimitiveIndex];
        if (!PrimitiveInfo)
        {
            continue;
        }
        
        FPrimitiveSceneProxy* Proxy = PrimitiveInfo->GetProxy();
        if (!Proxy)
        {
            continue;
        }
        
        // Check if primitive is hidden
        if (View.HiddenPrimitives.Contains(PrimitiveInfo->GetPrimitiveComponentId()))
        {
            continue;
        }
        
        // Get bounds
        const FBoxSphereBounds& Bounds = Proxy->GetBounds();
        
        // Perform frustum test
        if (FrustumCuller.IsVisible(Bounds))
        {
            OutResult.SetVisible(PrimitiveIndex);
        }
        else
        {
            ++OutResult.NumFrustumCulled;
        }
    }
}

void FSceneVisibilityManager::DistanceCull(const FScene& Scene, const FSceneView& View, FViewVisibilityResult& OutResult)
{
    FDistanceCuller DistanceCuller(View.ViewLocation);
    
    const TArray<FPrimitiveSceneInfo*>& Primitives = Scene.GetPrimitives();
    
    for (int32 PrimitiveIndex = 0; PrimitiveIndex < Primitives.Num(); ++PrimitiveIndex)
    {
        // Skip if already culled
        if (!OutResult.IsVisible(PrimitiveIndex))
        {
            continue;
        }
        
        FPrimitiveSceneInfo* PrimitiveInfo = Primitives[PrimitiveIndex];
        if (!PrimitiveInfo)
        {
            continue;
        }
        
        FPrimitiveSceneProxy* Proxy = PrimitiveInfo->GetProxy();
        if (!Proxy)
        {
            continue;
        }
        
        // Get bounds and draw distances
        const FBoxSphereBounds& Bounds = Proxy->GetBounds();
        float MinDrawDistance = Proxy->GetMinDrawDistance();
        float MaxDrawDistance = Proxy->GetMaxDrawDistance();
        
        // Perform distance test
        if (DistanceCuller.ShouldCull(Bounds, MinDrawDistance, MaxDrawDistance))
        {
            OutResult.SetNotVisible(PrimitiveIndex);
            ++OutResult.NumDistanceCulled;
        }
    }
}

void FSceneVisibilityManager::OcclusionCull(const FScene& Scene, const FSceneView& View, FViewVisibilityResult& OutResult)
{
    // Occlusion culling is optional and requires GPU queries
    // For now, this is a placeholder that uses temporal coherence
    
    const TArray<FPrimitiveSceneInfo*>& Primitives = Scene.GetPrimitives();
    
    for (int32 PrimitiveIndex = 0; PrimitiveIndex < Primitives.Num(); ++PrimitiveIndex)
    {
        // Skip if already culled
        if (!OutResult.IsVisible(PrimitiveIndex))
        {
            continue;
        }
        
        FPrimitiveSceneInfo* PrimitiveInfo = Primitives[PrimitiveIndex];
        if (!PrimitiveInfo)
        {
            continue;
        }
        
        // Check if primitive supports occlusion culling
        FPrimitiveSceneProxy* Proxy = PrimitiveInfo->GetProxy();
        if (!Proxy)
        {
            continue;
        }
        
        // Update visibility state
        FPrimitiveVisibilityState& VisState = VisibilityStates[PrimitiveIndex];
        VisState.LastVisibilityFrame = CurrentFrameNumber;
        
        // Check previous frame's occlusion query result
        if (!OcclusionQueryManager.WasVisibleLastFrame(PrimitiveIndex))
        {
            // Primitive was occluded last frame
            ++VisState.OccludedFrameCount;
            
            // If occluded for multiple frames, cull it
            // But periodically re-test to handle moving objects
            if (VisState.OccludedFrameCount > 2 && (CurrentFrameNumber % 8) != (uint32)(PrimitiveIndex % 8))
            {
                OutResult.SetNotVisible(PrimitiveIndex);
                ++OutResult.NumOcclusionCulled;
                continue;
            }
        }
        else
        {
            VisState.OccludedFrameCount = 0;
            VisState.LastVisibleFrame = CurrentFrameNumber;
        }
        
        // Request occlusion query for next frame
        const FBoxSphereBounds& Bounds = Proxy->GetBounds();
        OcclusionQueryManager.RequestQuery(PrimitiveIndex, Bounds);
        
        VisState.bWasVisible = true;
    }
}

FPrimitiveVisibilityState& FSceneVisibilityManager::GetVisibilityState(int32 PrimitiveIndex)
{
    if (PrimitiveIndex >= VisibilityStates.Num())
    {
        VisibilityStates.SetNum(PrimitiveIndex + 1);
    }
    return VisibilityStates[PrimitiveIndex];
}

FPrimitiveFadingState& FSceneVisibilityManager::GetFadingState(FPrimitiveComponentId PrimitiveId)
{
    return FadingStates.FindOrAdd(PrimitiveId);
}

// ============================================================================
// Utility Functions
// ============================================================================

float ComputeBoundsScreenSize(
    const FVector& Origin,
    float SphereRadius,
    const FVector& ViewOrigin,
    const FMatrix& ProjMatrix,
    const FIntRect& ViewRect)
{
    // Calculate distance from view to sphere center
    float Distance = (Origin - ViewOrigin).Size();
    
    // Avoid division by zero
    if (Distance < MR_SMALL_NUMBER)
    {
        return ViewRect.Width(); // Full screen
    }
    
    // Calculate projected size
    // Using the projection matrix to get the screen space size
    float ProjectedRadius = SphereRadius * FMath::Max(ProjMatrix.M[0][0], ProjMatrix.M[1][1]) / Distance;
    
    // Convert to pixels
    float ScreenRadius = ProjectedRadius * FMath::Max(ViewRect.Width(), ViewRect.Height()) * 0.5f;
    
    return ScreenRadius;
}

float ComputeBoundsScreenSizeRatio(
    const FVector& Origin,
    float SphereRadius,
    const FVector& ViewOrigin,
    const FMatrix& ProjMatrix)
{
    // Calculate distance from view to sphere center
    float Distance = (Origin - ViewOrigin).Size();
    
    // Avoid division by zero
    if (Distance < MR_SMALL_NUMBER)
    {
        return 1.0f; // Full screen
    }
    
    // Calculate projected size ratio
    float ProjectedRadius = SphereRadius * FMath::Max(ProjMatrix.M[0][0], ProjMatrix.M[1][1]) / Distance;
    
    // Clamp to valid range
    return FMath::Clamp(ProjectedRadius, 0.0f, 1.0f);
}

} // namespace MonsterEngine
