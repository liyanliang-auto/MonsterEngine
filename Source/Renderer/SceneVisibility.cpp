// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SceneVisibility.cpp
 * @brief Scene visibility system implementation
 * 
 * Implements frustum culling, distance culling, and occlusion culling.
 * Reference: UE5 SceneVisibility.cpp, SceneOcclusion.cpp
 */

#include "Renderer/SceneVisibility.h"
#include "Renderer/Scene.h"
#include "Renderer/SceneView.h"
#include "Core/Logging/Logging.h"
#include "Math/MathUtility.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDevice.h"

using namespace MonsterRender;

namespace MonsterEngine
{

// ============================================================================
// Static Member Initialization
// ============================================================================

float FDistanceCuller::ViewDistanceScale = 1.0f;
float FDistanceCuller::FadeRadius = 1000.0f;
bool FDistanceCuller::bDisableLODFade = false;

// ============================================================================
// FFrustumCuller Implementation
// ============================================================================

FFrustumCuller::FFrustumCuller()
{
}

FFrustumCuller::~FFrustumCuller()
{
}

int32 FFrustumCuller::CullPrimitives(const FScene* Scene, FViewInfo& View, 
                                     const FPrimitiveCullingFlags& Flags)
{
    if (!Scene || !Flags.bShouldVisibilityCull)
    {
        return 0;
    }
    
    int32 NumPrimitives = Scene->GetNumPrimitives();
    if (NumPrimitives == 0)
    {
        return 0;
    }
    
    int32 TotalCulled = 0;
    
    // Process primitives in parallel-friendly chunks
    const int32 NumTasks = (NumPrimitives + PrimitivesPerTask - 1) / PrimitivesPerTask;
    
    for (int32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
    {
        int32 StartIndex = TaskIndex * PrimitivesPerTask;
        int32 EndIndex = Math::Min(StartIndex + PrimitivesPerTask, NumPrimitives);
        
        int32 NumCulled = 0;
        CullPrimitiveRange(Scene, View, Flags, StartIndex, EndIndex, NumCulled);
        TotalCulled += NumCulled;
    }
    
    return TotalCulled;
}

void FFrustumCuller::CullPrimitiveRange(const FScene* Scene, FViewInfo& View,
                                        const FPrimitiveCullingFlags& Flags,
                                        int32 StartIndex, int32 EndIndex, int32& OutNumCulled)
{
    OutNumCulled = 0;
    
    const TArray<FPrimitiveBounds>& PrimitiveBounds = Scene->GetPrimitiveBounds();
    const Math::FPlane* PermutedPlanes = View.ViewFrustum.PermutedPlanes.GetData();
    const int32 NumPermutedPlanes = View.ViewFrustum.PermutedPlanes.Num();
    
    for (int32 PrimitiveIndex = StartIndex; PrimitiveIndex < EndIndex; ++PrimitiveIndex)
    {
        const FPrimitiveBounds& Bounds = PrimitiveBounds[PrimitiveIndex];
        
        bool bVisible = IsPrimitiveVisible(View, Bounds, Flags);
        
        if (bVisible)
        {
            View.SetPrimitiveVisibility(PrimitiveIndex, true);
        }
        else
        {
            View.SetPrimitiveVisibility(PrimitiveIndex, false);
            OutNumCulled++;
        }
    }
}

bool FFrustumCuller::IsPrimitiveVisible(const FViewInfo& View, const FPrimitiveBounds& Bounds,
                                        const FPrimitiveCullingFlags& Flags) const
{
    const FBoxSphereBounds& BoxSphereBounds = Bounds.BoxSphereBounds;
    
    // Optional sphere test first (faster rejection)
    if (Flags.bAlsoUseSphereTest)
    {
        if (!View.ViewFrustum.IntersectSphere(BoxSphereBounds.Origin, BoxSphereBounds.SphereRadius))
        {
            return false;
        }
    }
    
    // Box-frustum intersection test
    if (Flags.bUseFastIntersect && View.ViewFrustum.PermutedPlanes.Num() == 8)
    {
        return IntersectBox8Plane(BoxSphereBounds.Origin, BoxSphereBounds.BoxExtent,
                                  View.ViewFrustum.PermutedPlanes.GetData());
    }
    else
    {
        return View.ViewFrustum.IntersectBox(BoxSphereBounds.Origin, BoxSphereBounds.BoxExtent);
    }
}

bool FFrustumCuller::IntersectBox8Plane(const Math::FVector& Origin, const Math::FVector& Extent,
                                        const Math::FPlane* PermutedPlanes)
{
    // SIMD-optimized 8-plane intersection test
    // Tests box against 8 planes (6 frustum planes + 2 padding)
    
    // For each plane, compute: distance = dot(normal, origin) - w
    // Box is outside if: distance > dot(abs(normal), extent)
    
    for (int32 PlaneIndex = 0; PlaneIndex < 8; ++PlaneIndex)
    {
        const Math::FPlane& Plane = PermutedPlanes[PlaneIndex];
        
        // Calculate signed distance from box center to plane
        float Distance = Plane.X * Origin.X + Plane.Y * Origin.Y + Plane.Z * Origin.Z - Plane.W;
        
        // Calculate the effective radius (push-out distance)
        float EffectiveRadius = 
            Math::Abs(Plane.X) * Extent.X +
            Math::Abs(Plane.Y) * Extent.Y +
            Math::Abs(Plane.Z) * Extent.Z;
        
        // If the box is completely outside this plane, it's not visible
        if (Distance > EffectiveRadius)
        {
            return false;
        }
    }
    
    return true;
}

bool FFrustumCuller::IntersectBoxPlanes(const Math::FVector& Origin, const Math::FVector& Extent,
                                        const Math::FPlane* Planes, int32 NumPlanes)
{
    for (int32 PlaneIndex = 0; PlaneIndex < NumPlanes; ++PlaneIndex)
    {
        const Math::FPlane& Plane = Planes[PlaneIndex];
        
        float Distance = Plane.X * Origin.X + Plane.Y * Origin.Y + Plane.Z * Origin.Z - Plane.W;
        float EffectiveRadius = 
            Math::Abs(Plane.X) * Extent.X +
            Math::Abs(Plane.Y) * Extent.Y +
            Math::Abs(Plane.Z) * Extent.Z;
        
        if (Distance > EffectiveRadius)
        {
            return false;
        }
    }
    
    return true;
}

bool FFrustumCuller::IntersectSpherePlanes(const Math::FVector& Center, float Radius,
                                           const Math::FPlane* Planes, int32 NumPlanes)
{
    for (int32 PlaneIndex = 0; PlaneIndex < NumPlanes; ++PlaneIndex)
    {
        const Math::FPlane& Plane = Planes[PlaneIndex];
        
        float Distance = Plane.X * Center.X + Plane.Y * Center.Y + Plane.Z * Center.Z - Plane.W;
        
        if (Distance > Radius)
        {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// FDistanceCuller Implementation
// ============================================================================

FDistanceCuller::FDistanceCuller()
{
}

FDistanceCuller::~FDistanceCuller()
{
}

int32 FDistanceCuller::CullPrimitives(const FScene* Scene, FViewInfo& View)
{
    if (!Scene)
    {
        return 0;
    }
    
    int32 NumCulled = 0;
    const TArray<FPrimitiveBounds>& PrimitiveBounds = Scene->GetPrimitiveBounds();
    const Math::FVector& ViewOrigin = View.GetViewOrigin();
    
    for (int32 PrimitiveIndex = 0; PrimitiveIndex < PrimitiveBounds.Num(); ++PrimitiveIndex)
    {
        // Skip already culled primitives
        if (!View.IsPrimitiveVisible(PrimitiveIndex))
        {
            continue;
        }
        
        const FPrimitiveBounds& Bounds = PrimitiveBounds[PrimitiveIndex];
        
        // Calculate distance squared from view to primitive center
        float DistanceSquared = (Bounds.BoxSphereBounds.Origin - ViewOrigin).SizeSquared();
        
        bool bMayBeFading = false;
        bool bFadingIn = false;
        
        if (IsDistanceCulled(DistanceSquared, Bounds.MinDrawDistance, Bounds.MaxCullDistance,
                            ViewDistanceScale, bMayBeFading, bFadingIn))
        {
            View.SetPrimitiveVisibility(PrimitiveIndex, false);
            NumCulled++;
        }
        else if (bMayBeFading)
        {
            // Mark as potentially fading for LOD transitions
            View.PotentiallyFadingPrimitiveMap[PrimitiveIndex] = true;
        }
    }
    
    return NumCulled;
}

bool FDistanceCuller::IsDistanceCulled(float DistanceSquared, float MinDrawDistance, 
                                       float MaxDrawDistance, float MaxDrawDistanceScale,
                                       bool& bOutMayBeFading, bool& bOutFadingIn)
{
    bOutMayBeFading = false;
    bOutFadingIn = false;
    
    // Check minimum draw distance
    if (MinDrawDistance > 0.0f)
    {
        float MinDistSq = MinDrawDistance * MinDrawDistance;
        if (DistanceSquared < MinDistSq)
        {
            return true;
        }
    }
    
    // Check maximum draw distance
    if (MaxDrawDistance < FLT_MAX)
    {
        float ScaledMaxDistance = MaxDrawDistance * MaxDrawDistanceScale;
        float MaxDistSq = ScaledMaxDistance * ScaledMaxDistance;
        
        if (DistanceSquared > MaxDistSq)
        {
            return true;
        }
        
        // Check for fading region
        if (!bDisableLODFade && FadeRadius > 0.0f)
        {
            float FadeStartDist = ScaledMaxDistance - FadeRadius;
            float FadeStartDistSq = FadeStartDist * FadeStartDist;
            
            if (DistanceSquared > FadeStartDistSq)
            {
                bOutMayBeFading = true;
                bOutFadingIn = false; // Fading out
            }
        }
    }
    
    return false;
}

float FDistanceCuller::GetViewDistanceScale()
{
    return ViewDistanceScale;
}

void FDistanceCuller::SetViewDistanceScale(float Scale)
{
    ViewDistanceScale = Math::Max(0.0f, Scale);
}

// ============================================================================
// FOcclusionQueryPool Implementation
// ============================================================================

FOcclusionQueryPool::FOcclusionQueryPool()
    : Device(nullptr)
    , NumAllocatedQueries(0)
    , PoolSize(0)
{
}

FOcclusionQueryPool::~FOcclusionQueryPool()
{
    Shutdown();
}

void FOcclusionQueryPool::Initialize(RHI::IRHIDevice* InDevice, int32 InitialSize)
{
    Device = InDevice;
    
    if (!Device)
    {
        MR_LOG_WARNING(LogRenderer, "FOcclusionQueryPool::Initialize: No device");
        return;
    }
    
    // Pre-allocate query pool
    QueryPool.SetNum(InitialSize);
    FreeList.Reserve(InitialSize);
    
    for (int32 i = 0; i < InitialSize; ++i)
    {
        QueryPool[i] = nullptr; // Platform-specific query creation would go here
        FreeList.Add(i);
    }
    
    PoolSize = InitialSize;
    NumAllocatedQueries = 0;
    
    MR_LOG_DEBUG(LogRenderer, "FOcclusionQueryPool initialized with %d queries", InitialSize);
}

void FOcclusionQueryPool::Shutdown()
{
    // Release all query objects
    for (void* Query : QueryPool)
    {
        if (Query)
        {
            // Platform-specific query destruction would go here
        }
    }
    
    QueryPool.Empty();
    FreeList.Empty();
    NumAllocatedQueries = 0;
    PoolSize = 0;
    Device = nullptr;
}

int32 FOcclusionQueryPool::AllocateQuery()
{
    if (FreeList.Num() == 0)
    {
        // Grow the pool
        int32 NewIndex = QueryPool.Num();
        QueryPool.Add(nullptr); // Platform-specific query creation
        PoolSize++;
        NumAllocatedQueries++;
        return NewIndex;
    }
    
    int32 Index = FreeList.Pop();
    NumAllocatedQueries++;
    return Index;
}

void FOcclusionQueryPool::ReleaseQuery(int32 QueryIndex)
{
    if (QueryIndex >= 0 && QueryIndex < QueryPool.Num())
    {
        FreeList.Add(QueryIndex);
        NumAllocatedQueries--;
    }
}

// ============================================================================
// FOcclusionCuller Implementation
// ============================================================================

FOcclusionCuller::FOcclusionCuller()
    : Device(nullptr)
    , HZBTexture(nullptr)
    , HZBMipLevels(0)
    , OcclusionMethod(EOcclusionMethod::None)
    , CurrentFrame(0)
{
}

FOcclusionCuller::~FOcclusionCuller()
{
    Shutdown();
}

void FOcclusionCuller::Initialize(RHI::IRHIDevice* InDevice, EOcclusionMethod Method)
{
    Device = InDevice;
    OcclusionMethod = Method;
    
    if (Method == EOcclusionMethod::HardwareQueries || Method == EOcclusionMethod::Combined)
    {
        QueryPool.Initialize(Device, 1024);
    }
    
    MR_LOG_INFO(LogRenderer, "FOcclusionCuller initialized with method: %d", static_cast<int>(Method));
}

void FOcclusionCuller::Shutdown()
{
    QueryPool.Shutdown();
    OcclusionHistory.Empty();
    PendingQueries.Empty();
    
    if (HZBTexture)
    {
        // Release HZB texture
        HZBTexture = nullptr;
    }
    
    Device = nullptr;
}

void FOcclusionCuller::BeginOcclusionCulling(RHI::IRHICommandList& RHICmdList, FViewInfo& View)
{
    CurrentFrame = View.Family ? View.Family->FrameNumber : 0;
    PendingQueries.Empty();
}

int32 FOcclusionCuller::CullPrimitives(const FScene* Scene, FViewInfo& View, 
                                       RHI::IRHICommandList& RHICmdList)
{
    if (!Scene || OcclusionMethod == EOcclusionMethod::None)
    {
        return 0;
    }
    
    int32 NumCulled = 0;
    const TArray<FPrimitiveBounds>& PrimitiveBounds = Scene->GetPrimitiveBounds();
    const TArray<uint8>& OcclusionFlags = Scene->GetPrimitiveOcclusionFlags();
    
    // Ensure history array is properly sized
    if (OcclusionHistory.Num() != PrimitiveBounds.Num())
    {
        OcclusionHistory.SetNum(PrimitiveBounds.Num());
    }
    
    for (int32 PrimitiveIndex = 0; PrimitiveIndex < PrimitiveBounds.Num(); ++PrimitiveIndex)
    {
        // Skip already culled primitives
        if (!View.IsPrimitiveVisible(PrimitiveIndex))
        {
            continue;
        }
        
        // Check if primitive can be occluded
        if (!(OcclusionFlags[PrimitiveIndex] & EOcclusionFlags::CanBeOccluded))
        {
            continue;
        }
        
        // Check occlusion based on history
        if (IsPrimitiveOccluded(PrimitiveIndex, CurrentFrame))
        {
            View.SetPrimitiveVisibility(PrimitiveIndex, false);
            NumCulled++;
        }
        else
        {
            // Submit new occlusion query if needed
            FPrimitiveOcclusionHistory& History = OcclusionHistory[PrimitiveIndex];
            
            if (!History.bQueryPending && 
                (CurrentFrame - History.LastQuerySubmitFrame) > OcclusionFrameThreshold)
            {
                SubmitOcclusionQuery(RHICmdList, PrimitiveIndex, PrimitiveBounds[PrimitiveIndex].BoxSphereBounds);
            }
        }
    }
    
    return NumCulled;
}

void FOcclusionCuller::EndOcclusionCulling(RHI::IRHICommandList& RHICmdList)
{
    // Read back query results
    ReadbackOcclusionResults(RHICmdList);
}

void FOcclusionCuller::SubmitOcclusionQuery(RHI::IRHICommandList& RHICmdList, int32 PrimitiveIndex,
                                            const FBoxSphereBounds& Bounds)
{
    if (PrimitiveIndex < 0 || PrimitiveIndex >= OcclusionHistory.Num())
    {
        return;
    }
    
    FPrimitiveOcclusionHistory& History = OcclusionHistory[PrimitiveIndex];
    
    // Allocate a query
    int32 QueryIndex = QueryPool.AllocateQuery();
    
    // Submit the query (platform-specific implementation)
    // This would render a bounding box and test against the depth buffer
    
    History.bQueryPending = true;
    History.LastQuerySubmitFrame = CurrentFrame;
    
    PendingQueries.Add(PrimitiveIndex);
}

void FOcclusionCuller::ReadbackOcclusionResults(RHI::IRHICommandList& RHICmdList)
{
    // Read back results from pending queries
    for (int32 PrimitiveIndex : PendingQueries)
    {
        if (PrimitiveIndex < 0 || PrimitiveIndex >= OcclusionHistory.Num())
        {
            continue;
        }
        
        FPrimitiveOcclusionHistory& History = OcclusionHistory[PrimitiveIndex];
        
        // Get query result (platform-specific)
        // For now, assume visible
        bool bIsOccluded = false;
        
        History.UpdateHistory(CurrentFrame, bIsOccluded);
    }
    
    PendingQueries.Empty();
}

bool FOcclusionCuller::IsPrimitiveOccluded(int32 PrimitiveIndex, uint32 InCurrentFrame) const
{
    if (PrimitiveIndex < 0 || PrimitiveIndex >= OcclusionHistory.Num())
    {
        return false;
    }
    
    const FPrimitiveOcclusionHistory& History = OcclusionHistory[PrimitiveIndex];
    
    // If we have recent occlusion data and the primitive was occluded
    if (History.bWasOccluded && 
        History.ConsecutiveOccludedFrames >= OcclusionFrameThreshold)
    {
        return true;
    }
    
    return false;
}

void FOcclusionCuller::BuildHZB(RHI::IRHICommandList& RHICmdList, RHI::IRHITexture* DepthTexture)
{
    if (!DepthTexture)
    {
        return;
    }
    
    // Build hierarchical Z-buffer from depth texture
    // This involves downsampling the depth buffer and taking the max depth at each level
    
    MR_LOG_DEBUG(LogRenderer, "BuildHZB");
}

bool FOcclusionCuller::TestHZB(const FBoxSphereBounds& Bounds, const Math::FMatrix& ViewProjectionMatrix) const
{
    if (!HZBTexture)
    {
        return true; // Assume visible if no HZB
    }
    
    // Project bounds to screen space and test against HZB
    // This is a software occlusion test
    
    return true; // Placeholder - assume visible
}

// ============================================================================
// FSceneVisibility Implementation
// ============================================================================

FSceneVisibility::FSceneVisibility()
    : bFrustumCullingEnabled(true)
    , bDistanceCullingEnabled(true)
    , bOcclusionCullingEnabled(false)
{
}

FSceneVisibility::~FSceneVisibility()
{
    Shutdown();
}

void FSceneVisibility::Initialize(RHI::IRHIDevice* Device)
{
    // Initialize occlusion culler if enabled
    if (bOcclusionCullingEnabled)
    {
        OcclusionCuller.Initialize(Device, FOcclusionCuller::EOcclusionMethod::HardwareQueries);
    }
    
    MR_LOG_INFO(LogRenderer, "FSceneVisibility initialized (Frustum: %d, Distance: %d, Occlusion: %d)",
                bFrustumCullingEnabled, bDistanceCullingEnabled, bOcclusionCullingEnabled);
}

void FSceneVisibility::Shutdown()
{
    OcclusionCuller.Shutdown();
}

void FSceneVisibility::ComputeViewVisibility(const FScene* Scene, FViewInfo& View, 
                                             RHI::IRHICommandList& RHICmdList)
{
    if (!Scene)
    {
        return;
    }
    
    int32 NumPrimitives = Scene->GetNumPrimitives();
    if (NumPrimitives == 0)
    {
        return;
    }
    
    // Initialize visibility to all visible
    View.InitVisibilityArrays(NumPrimitives);
    for (int32 i = 0; i < NumPrimitives; ++i)
    {
        View.SetPrimitiveVisibility(i, true);
    }
    
    int32 TotalCulled = 0;
    
    // Step 1: Frustum culling
    if (bFrustumCullingEnabled)
    {
        FPrimitiveCullingFlags Flags;
        Flags.bShouldVisibilityCull = true;
        Flags.bUseFastIntersect = true;
        Flags.bAlsoUseSphereTest = true;
        
        int32 NumFrustumCulled = FrustumCuller.CullPrimitives(Scene, View, Flags);
        TotalCulled += NumFrustumCulled;
        
        MR_LOG_DEBUG(LogRenderer, "Frustum culling: %d primitives culled", NumFrustumCulled);
    }
    
    // Step 2: Distance culling
    if (bDistanceCullingEnabled)
    {
        int32 NumDistanceCulled = DistanceCuller.CullPrimitives(Scene, View);
        TotalCulled += NumDistanceCulled;
        
        MR_LOG_DEBUG(LogRenderer, "Distance culling: %d primitives culled", NumDistanceCulled);
    }
    
    // Step 3: Occlusion culling
    if (bOcclusionCullingEnabled && OcclusionCuller.IsEnabled())
    {
        OcclusionCuller.BeginOcclusionCulling(RHICmdList, View);
        int32 NumOcclusionCulled = OcclusionCuller.CullPrimitives(Scene, View, RHICmdList);
        OcclusionCuller.EndOcclusionCulling(RHICmdList);
        TotalCulled += NumOcclusionCulled;
        
        MR_LOG_DEBUG(LogRenderer, "Occlusion culling: %d primitives culled", NumOcclusionCulled);
    }
    
    // Mark visibility as computed
    View.bVisibilityComputed = true;
    
    int32 NumVisible = NumPrimitives - TotalCulled;
    MR_LOG_DEBUG(LogRenderer, "Visibility complete: %d visible, %d culled out of %d total",
                 NumVisible, TotalCulled, NumPrimitives);
}

} // namespace MonsterEngine
