// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Scene.h
 * @brief Scene management class
 * 
 * This file defines FScene class for managing all scene data including
 * primitives, lights, and other scene elements.
 * Reference: UE5 Scene.h, ScenePrivate.h
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/BitArray.h"
#include "Containers/SparseArray.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Renderer/SceneTypes.h"

// Forward declarations for RHI types (in MonsterRender::RHI namespace)
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
    class IRHICommandList;
}}

namespace MonsterEngine
{

/**
 * Renderer namespace contains low-level rendering scene classes
 * that work directly with FPrimitiveSceneProxy instead of UPrimitiveComponent.
 * This is separate from the Engine-level FScene which uses UPrimitiveComponent.
 */
namespace Renderer
{

// Bring RHI types into scope
using MonsterRender::RHI::IRHIDevice;
using MonsterRender::RHI::IRHICommandList;

// Forward declarations within Renderer namespace
class FPrimitiveSceneInfo;
class FPrimitiveSceneProxy;
class FLightSceneInfo;
class FLightSceneProxy;
class FSceneRenderer;
class FViewInfo;
class FMeshElementCollector;

// ============================================================================
// FPrimitiveSceneProxy - Primitive Rendering Proxy
// ============================================================================

/**
 * @class FPrimitiveSceneProxy
 * @brief Rendering thread representation of a primitive component
 * 
 * Contains all data needed to render a primitive, separate from game thread data.
 * Reference: UE5 FPrimitiveSceneProxy
 */
class FPrimitiveSceneProxy
{
public:
    /** Constructor */
    FPrimitiveSceneProxy()
        : PrimitiveSceneInfo(nullptr)
        , bCastShadow(true)
        , bCastDynamicShadow(true)
        , bReceivesDecals(true)
        , bVisible(true)
        , bHiddenInGame(false)
        , bRenderInMainPass(true)
        , bRenderInDepthPass(true)
        , bUseAsOccluder(true)
        , bSelfShadowOnly(false)
        , bCastVolumetricTranslucentShadow(false)
        , bCastContactShadow(true)
        , bCastDeepShadow(false)
        , bCastCapsuleDirectShadow(false)
        , bCastCapsuleIndirectShadow(false)
        , bAffectDynamicIndirectLighting(true)
        , bAffectDistanceFieldLighting(true)
        , MaxDrawDistance(0.0f)
        , MinDrawDistance(0.0f)
        , VisibilityId(INDEX_NONE)
    {
    }
    
    /** Virtual destructor */
    virtual ~FPrimitiveSceneProxy() = default;
    
    /**
     * Get the bounds of this primitive
     */
    virtual FBoxSphereBounds GetBounds() const { return Bounds; }
    
    /**
     * Get the local bounds of this primitive
     */
    virtual FBoxSphereBounds GetLocalBounds() const { return LocalBounds; }
    
    /**
     * Get the view relevance for this primitive
     */
    virtual FPrimitiveViewRelevance GetViewRelevance(const FViewInfo* View) const;
    
    /**
     * Draw the primitive's dynamic elements
     */
    virtual void GetDynamicMeshElements(
        const TArray<const FViewInfo*>& Views,
        const struct FSceneViewFamily& ViewFamily,
        uint32 VisibilityMap,
        FMeshElementCollector& Collector) const {}
    
    /**
     * Whether this primitive is a detail mesh (foliage, etc.)
     */
    virtual bool IsDetailMesh() const { return false; }
    
    /**
     * Whether this primitive uses distance cull fade
     */
    virtual bool IsUsingDistanceCullFade() const { return false; }
    
    /**
     * Get the visibility ID for precomputed visibility
     */
    int32 GetVisibilityId() const { return VisibilityId; }
    
    /**
     * Set the visibility ID
     */
    void SetVisibilityId(int32 InVisibilityId) { VisibilityId = InVisibilityId; }
    
    /**
     * Get the primitive scene info
     */
    FPrimitiveSceneInfo* GetPrimitiveSceneInfo() const { return PrimitiveSceneInfo; }
    
    /**
     * Set the primitive scene info
     */
    void SetPrimitiveSceneInfo(FPrimitiveSceneInfo* InInfo) { PrimitiveSceneInfo = InInfo; }
    
    /**
     * Get the local to world transform
     */
    const Math::FMatrix& GetLocalToWorld() const { return LocalToWorld; }
    
    /**
     * Set the local to world transform
     */
    void SetLocalToWorld(const Math::FMatrix& InLocalToWorld)
    {
        LocalToWorld = InLocalToWorld;
        // Update bounds
        UpdateBounds();
    }
    
    /**
     * Update the world bounds from local bounds and transform
     */
    virtual void UpdateBounds()
    {
        Bounds = LocalBounds.TransformBy(LocalToWorld);
    }
    
public:
    /** World space bounds */
    FBoxSphereBounds Bounds;
    
    /** Local space bounds */
    FBoxSphereBounds LocalBounds;
    
    /** Local to world transform */
    Math::FMatrix LocalToWorld;
    
    /** Pointer to the scene info */
    FPrimitiveSceneInfo* PrimitiveSceneInfo;
    
    /** Shadow casting flags */
    uint32 bCastShadow : 1;
    uint32 bCastDynamicShadow : 1;
    uint32 bReceivesDecals : 1;
    uint32 bVisible : 1;
    uint32 bHiddenInGame : 1;
    uint32 bRenderInMainPass : 1;
    uint32 bRenderInDepthPass : 1;
    uint32 bUseAsOccluder : 1;
    uint32 bSelfShadowOnly : 1;
    uint32 bCastVolumetricTranslucentShadow : 1;
    uint32 bCastContactShadow : 1;
    uint32 bCastDeepShadow : 1;
    uint32 bCastCapsuleDirectShadow : 1;
    uint32 bCastCapsuleIndirectShadow : 1;
    uint32 bAffectDynamicIndirectLighting : 1;
    uint32 bAffectDistanceFieldLighting : 1;
    
    /** Draw distance settings */
    float MaxDrawDistance;
    float MinDrawDistance;
    
    /** Visibility ID for precomputed visibility */
    int32 VisibilityId;
};

// ============================================================================
// FPrimitiveSceneInfo - Primitive Scene Information
// ============================================================================

/**
 * @class FPrimitiveSceneInfo
 * @brief Scene-level information about a primitive
 * 
 * Manages the primitive's presence in the scene, including its index,
 * proxy, and various scene-related data.
 * Reference: UE5 FPrimitiveSceneInfo
 */
class FPrimitiveSceneInfo
{
public:
    /** Constructor */
    FPrimitiveSceneInfo(FPrimitiveSceneProxy* InProxy, FScene* InScene)
        : Proxy(InProxy)
        , Scene(InScene)
        , PackedIndex(INDEX_NONE)
        , ComponentId(0)
        , bNeedsUniformBufferUpdate(true)
        , bNeedsCachedReflectionCaptureUpdate(true)
        , bPendingAddToScene(false)
        , bPendingRemoveFromScene(false)
    {
        if (Proxy)
        {
            Proxy->SetPrimitiveSceneInfo(this);
        }
    }
    
    /** Destructor */
    ~FPrimitiveSceneInfo()
    {
        if (Proxy)
        {
            Proxy->SetPrimitiveSceneInfo(nullptr);
        }
    }
    
    /**
     * Get the primitive index in the scene
     */
    int32 GetIndex() const { return PackedIndex; }
    
    /**
     * Set the primitive index
     */
    void SetIndex(int32 InIndex) { PackedIndex = InIndex; }
    
    /**
     * Get the proxy
     */
    FPrimitiveSceneProxy* GetProxy() const { return Proxy; }
    
    /**
     * Get the scene
     */
    FScene* GetScene() const { return Scene; }
    
    /**
     * Get the component ID
     */
    uint32 GetComponentId() const { return ComponentId; }
    
    /**
     * Set the component ID
     */
    void SetComponentId(uint32 InId) { ComponentId = InId; }
    
public:
    /** The primitive's proxy */
    FPrimitiveSceneProxy* Proxy;
    
    /** The scene this primitive belongs to */
    FScene* Scene;
    
    /** Index in the scene's primitive arrays */
    int32 PackedIndex;
    
    /** Unique component identifier */
    uint32 ComponentId;
    
    /** Flags */
    uint32 bNeedsUniformBufferUpdate : 1;
    uint32 bNeedsCachedReflectionCaptureUpdate : 1;
    uint32 bPendingAddToScene : 1;
    uint32 bPendingRemoveFromScene : 1;
};

// ============================================================================
// FLightSceneProxy - Light Rendering Proxy
// ============================================================================

/**
 * @class FLightSceneProxy
 * @brief Rendering thread representation of a light component
 * 
 * Reference: UE5 FLightSceneProxy
 */
class FLightSceneProxy
{
public:
    /** Light types */
    enum class ELightType : uint8
    {
        Directional,
        Point,
        Spot,
        Rect
    };
    
    /** Constructor */
    FLightSceneProxy()
        : LightSceneInfo(nullptr)
        , LightType(ELightType::Point)
        , Color(1.0f, 1.0f, 1.0f)
        , Intensity(1.0f)
        , AttenuationRadius(1000.0f)
        , InnerConeAngle(0.0f)
        , OuterConeAngle(44.0f)
        , bCastShadows(true)
        , bCastStaticShadows(true)
        , bCastDynamicShadows(true)
        , bAffectsWorld(true)
        , bVisible(true)
    {
    }
    
    /** Virtual destructor */
    virtual ~FLightSceneProxy() = default;
    
    /**
     * Get the light position
     */
    const Math::FVector& GetPosition() const { return Position; }
    
    /**
     * Get the light direction
     */
    const Math::FVector& GetDirection() const { return Direction; }
    
    /**
     * Get the light color
     */
    const Math::FVector& GetColor() const { return Color; }
    
    /**
     * Get the light intensity
     */
    float GetIntensity() const { return Intensity; }
    
    /**
     * Get the light type
     */
    ELightType GetLightType() const { return LightType; }
    
    /**
     * Check if this is a directional light
     */
    bool IsDirectionalLight() const { return LightType == ELightType::Directional; }
    
    /**
     * Check if this is a point light
     */
    bool IsPointLight() const { return LightType == ELightType::Point; }
    
    /**
     * Check if this is a spot light
     */
    bool IsSpotLight() const { return LightType == ELightType::Spot; }
    
    /**
     * Get the bounding sphere for this light
     */
    Math::FSphere GetBoundingSphere() const
    {
        if (IsDirectionalLight())
        {
            // Directional lights have infinite range
            return Math::FSphere(Math::FVector::ZeroVector, FLT_MAX);
        }
        return Math::FSphere(Position, AttenuationRadius);
    }
    
public:
    /** Light scene info */
    FLightSceneInfo* LightSceneInfo;
    
    /** Light type */
    ELightType LightType;
    
    /** World position */
    Math::FVector Position;
    
    /** Light direction (for directional and spot lights) */
    Math::FVector Direction;
    
    /** Light color */
    Math::FVector Color;
    
    /** Light intensity */
    float Intensity;
    
    /** Attenuation radius (for point and spot lights) */
    float AttenuationRadius;
    
    /** Inner cone angle in degrees (for spot lights) */
    float InnerConeAngle;
    
    /** Outer cone angle in degrees (for spot lights) */
    float OuterConeAngle;
    
    /** Shadow flags */
    uint32 bCastShadows : 1;
    uint32 bCastStaticShadows : 1;
    uint32 bCastDynamicShadows : 1;
    uint32 bAffectsWorld : 1;
    uint32 bVisible : 1;
};

// ============================================================================
// FLightSceneInfo - Light Scene Information
// ============================================================================

/**
 * @class FLightSceneInfo
 * @brief Scene-level information about a light
 * 
 * Reference: UE5 FLightSceneInfo
 */
class FLightSceneInfo
{
public:
    /** Constructor */
    FLightSceneInfo(FLightSceneProxy* InProxy, FScene* InScene)
        : Proxy(InProxy)
        , Scene(InScene)
        , Id(INDEX_NONE)
        , bVisible(true)
    {
        if (Proxy)
        {
            Proxy->LightSceneInfo = this;
        }
    }
    
    /** Destructor */
    ~FLightSceneInfo()
    {
        if (Proxy)
        {
            Proxy->LightSceneInfo = nullptr;
        }
    }
    
    /**
     * Get the light index in the scene
     */
    int32 GetId() const { return Id; }
    
    /**
     * Set the light index
     */
    void SetId(int32 InId) { Id = InId; }
    
    /**
     * Get the proxy
     */
    FLightSceneProxy* GetProxy() const { return Proxy; }
    
public:
    /** The light's proxy */
    FLightSceneProxy* Proxy;
    
    /** The scene this light belongs to */
    FScene* Scene;
    
    /** Index in the scene's light arrays */
    int32 Id;
    
    /** Whether the light is visible */
    bool bVisible;
};

// ============================================================================
// FScene - Scene Manager
// ============================================================================

/**
 * @class FScene
 * @brief Main scene management class
 * 
 * Manages all scene data including primitives, lights, and other elements.
 * Provides methods for adding/removing scene elements and querying scene state.
 * Reference: UE5 FScene
 */
class FScene
{
public:
    /** Constructor */
    FScene();
    
    /** Destructor */
    ~FScene();
    
    // ========================================================================
    // Primitive Management
    // ========================================================================
    
    /**
     * Add a primitive to the scene
     * @param Proxy The primitive proxy to add
     * @return The created primitive scene info
     */
    FPrimitiveSceneInfo* AddPrimitive(FPrimitiveSceneProxy* Proxy);
    
    /**
     * Remove a primitive from the scene
     * @param PrimitiveSceneInfo The primitive to remove
     */
    void RemovePrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo);
    
    /**
     * Update a primitive's transform
     * @param PrimitiveSceneInfo The primitive to update
     * @param NewTransform The new transform
     */
    void UpdatePrimitiveTransform(FPrimitiveSceneInfo* PrimitiveSceneInfo, 
                                  const Math::FMatrix& NewTransform);
    
    /**
     * Get the number of primitives in the scene
     */
    int32 GetNumPrimitives() const { return Primitives.Num(); }
    
    /**
     * Get a primitive by index
     */
    FPrimitiveSceneInfo* GetPrimitive(int32 Index) const
    {
        if (Index >= 0 && Index < Primitives.Num())
        {
            return Primitives[Index];
        }
        return nullptr;
    }
    
    // ========================================================================
    // Light Management
    // ========================================================================
    
    /**
     * Add a light to the scene
     * @param Proxy The light proxy to add
     * @return The created light scene info
     */
    FLightSceneInfo* AddLight(FLightSceneProxy* Proxy);
    
    /**
     * Remove a light from the scene
     * @param LightSceneInfo The light to remove
     */
    void RemoveLight(FLightSceneInfo* LightSceneInfo);
    
    /**
     * Get the number of lights in the scene
     */
    int32 GetNumLights() const { return Lights.Num(); }
    
    /**
     * Get a light by index
     */
    FLightSceneInfo* GetLight(int32 Index) const
    {
        if (Index >= 0 && Index < Lights.Num())
        {
            return Lights[Index];
        }
        return nullptr;
    }
    
    // ========================================================================
    // Scene Data Access
    // ========================================================================
    
    /**
     * Get the primitive bounds array
     */
    const TArray<FPrimitiveBounds>& GetPrimitiveBounds() const { return PrimitiveBounds; }
    
    /**
     * Get the primitive occlusion flags array
     */
    const TArray<uint8>& GetPrimitiveOcclusionFlags() const { return PrimitiveOcclusionFlags; }
    
    /**
     * Get the primitive component IDs array
     */
    const TArray<uint32>& GetPrimitiveComponentIds() const { return PrimitiveComponentIds; }
    
    // ========================================================================
    // Frame Management
    // ========================================================================
    
    /**
     * Called at the start of a new frame
     */
    void BeginFrame();
    
    /**
     * Called at the end of a frame
     */
    void EndFrame();
    
    /**
     * Get the current frame number
     */
    uint32 GetFrameNumber() const { return FrameNumber; }
    
    // ========================================================================
    // RHI Device Access
    // ========================================================================
    
    /**
     * Set the RHI device for this scene
     * @param InDevice RHI device pointer
     */
    void setRHIDevice(MonsterRender::RHI::IRHIDevice* InDevice) { m_rhiDevice = InDevice; }
    
    /**
     * Get the RHI device for this scene
     * @return RHI device pointer (may be nullptr)
     */
    MonsterRender::RHI::IRHIDevice* getRHIDevice() const { return m_rhiDevice; }
    
public:
    // ========================================================================
    // Scene Data Arrays (parallel arrays indexed by primitive index)
    // ========================================================================
    
    /** All primitives in the scene */
    TArray<FPrimitiveSceneInfo*> Primitives;
    
    /** Primitive bounds for culling */
    TArray<FPrimitiveBounds> PrimitiveBounds;
    
    /** Primitive occlusion flags */
    TArray<uint8> PrimitiveOcclusionFlags;
    
    /** Primitive component IDs */
    TArray<uint32> PrimitiveComponentIds;
    
    /** All lights in the scene */
    TArray<FLightSceneInfo*> Lights;
    
    /** Current frame number */
    uint32 FrameNumber;
    
    /** Next available component ID */
    uint32 NextComponentId;
    
private:
    /**
     * Add primitive to internal arrays
     */
    void AddPrimitiveToArrays(FPrimitiveSceneInfo* PrimitiveSceneInfo);
    
    /**
     * Remove primitive from internal arrays
     */
    void RemovePrimitiveFromArrays(FPrimitiveSceneInfo* PrimitiveSceneInfo);
    
    /** RHI device for resource creation */
    MonsterRender::RHI::IRHIDevice* m_rhiDevice = nullptr;
};

} // namespace Renderer
} // namespace MonsterEngine


