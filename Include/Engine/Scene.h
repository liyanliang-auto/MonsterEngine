// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Scene.h
 * @brief Renderer scene implementation
 * 
 * FScene is the renderer's private implementation of FSceneInterface.
 * It manages all rendering objects including primitives, lights, and other
 * scene elements. Following UE5's scene architecture.
 */

#include "SceneInterface.h"
#include "SceneTypes.h"
#include "SceneOctree.h"
#include "RenderCommandQueue.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/SparseArray.h"

namespace MonsterEngine
{

// Forward declarations
class FPrimitiveSceneInfo;
class FPrimitiveSceneProxy;
class FLightSceneInfo;
class FLightSceneProxy;
class FConvexVolume;
// Note: FScenePrimitiveOctree and FSceneLightOctree are type aliases defined in Octree.h

/**
 * Compact representation of a light for efficient storage and iteration
 */
struct FLightSceneInfoCompact
{
    /** The light scene info */
    FLightSceneInfo* LightSceneInfo;
    
    /** Light type for quick access */
    ELightType LightType;
    
    /** Cached light color */
    FVector Color;
    
    /** Cached light position (for point/spot lights) */
    FVector Position;
    
    /** Cached light direction (for directional/spot lights) */
    FVector Direction;
    
    /** Cached light radius (for point/spot lights) */
    float Radius;
    
    /** Whether the light casts shadows */
    uint8 bCastShadow : 1;
    
    /** Whether the light casts static shadows */
    uint8 bCastStaticShadow : 1;
    
    /** Whether the light casts dynamic shadows */
    uint8 bCastDynamicShadow : 1;

    FLightSceneInfoCompact()
        : LightSceneInfo(nullptr)
        , LightType(ELightType::Point)
        , Color(FVector::ZeroVector)
        , Position(FVector::ZeroVector)
        , Direction(FVector::ForwardVector)
        , Radius(0.0f)
        , bCastShadow(false)
        , bCastStaticShadow(false)
        , bCastDynamicShadow(false)
    {
    }

    /** Initialize from a light scene info */
    void Init(FLightSceneInfo* InLightSceneInfo);
};

/**
 * Renderer scene which is private to the renderer module
 * 
 * Ordinarily this is the renderer version of a UWorld, but an FScene can be
 * created for previewing in editors which don't have a UWorld as well.
 * The scene stores renderer state that is independent of any view or frame,
 * with the primary actions being adding and removing of primitives and lights.
 */
class FScene : public FSceneInterface
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /**
     * Constructor
     * @param InWorld Optional world associated with the scene
     * @param bInRequiresHitProxies Whether hit proxies are required
     * @param bInIsEditorScene Whether this is an editor scene
     */
    FScene(UWorld* InWorld = nullptr, bool bInRequiresHitProxies = false, bool bInIsEditorScene = false);

    /** Destructor */
    virtual ~FScene();

    // ========================================================================
    // FSceneInterface Implementation - Primitive Management
    // ========================================================================

    virtual void AddPrimitive(UPrimitiveComponent* Primitive) override;
    virtual void RemovePrimitive(UPrimitiveComponent* Primitive) override;
    virtual void ReleasePrimitive(UPrimitiveComponent* Primitive) override;
    virtual void UpdatePrimitiveTransform(UPrimitiveComponent* Primitive) override;
    virtual void UpdatePrimitiveAttachment(UPrimitiveComponent* Primitive) override;
    virtual FPrimitiveSceneInfo* GetPrimitiveSceneInfo(int32 PrimitiveIndex) override;

    // ========================================================================
    // FSceneInterface Implementation - Light Management
    // ========================================================================

    virtual void AddLight(ULightComponent* Light) override;
    virtual void RemoveLight(ULightComponent* Light) override;
    virtual void AddInvisibleLight(ULightComponent* Light) override;
    virtual void UpdateLightTransform(ULightComponent* Light) override;
    virtual void UpdateLightColorAndBrightness(ULightComponent* Light) override;
    virtual void SetSkyLight(FSkyLightSceneProxy* Light) override;
    virtual void DisableSkyLight(FSkyLightSceneProxy* Light) override;

    // ========================================================================
    // FSceneInterface Implementation - Decal Management
    // ========================================================================

    virtual void AddDecal(UDecalComponent* Component) override;
    virtual void RemoveDecal(UDecalComponent* Component) override;
    virtual void UpdateDecalTransform(UDecalComponent* Component) override;

    // ========================================================================
    // FSceneInterface Implementation - Scene Queries
    // ========================================================================

    virtual void GetRelevantLights(UPrimitiveComponent* Primitive,
                                   TArray<const ULightComponent*>* RelevantLights) const override;
    virtual bool RequiresHitProxies() const override { return bRequiresHitProxies; }
    virtual UWorld* GetWorld() const override { return World; }
    virtual FScene* GetRenderScene() override { return this; }

    // ========================================================================
    // FSceneInterface Implementation - Scene State
    // ========================================================================

    virtual bool HasAnyLights() const override;
    virtual bool IsEditorScene() const override { return bIsEditorScene; }
    virtual void Release() override;

    // ========================================================================
    // FSceneInterface Implementation - Fog Management
    // ========================================================================

    virtual void AddExponentialHeightFog(class UExponentialHeightFogComponent* FogComponent) override;
    virtual void RemoveExponentialHeightFog(class UExponentialHeightFogComponent* FogComponent) override;
    virtual bool HasAnyExponentialHeightFog() const override;

    // ========================================================================
    // FSceneInterface Implementation - Frame Management
    // ========================================================================

    virtual void StartFrame() override;
    virtual uint32 GetFrameNumber() const override { return FrameNumber; }
    virtual void IncrementFrameNumber() override { ++FrameNumber; }

    // ========================================================================
    // FSceneInterface Implementation - World Offset
    // ========================================================================

    virtual void ApplyWorldOffset(const FVector& InOffset) override;

    // ========================================================================
    // Scene Data Access
    // ========================================================================

    /** Get the number of primitives in the scene */
    int32 GetNumPrimitives() const { return Primitives.Num(); }

    /** Get primitive at index */
    FPrimitiveSceneInfo* GetPrimitive(int32 Index) const;

    /** Get the number of lights in the scene */
    int32 GetNumLights() const { return Lights.Num(); }

    /** Get all primitives */
    const TArray<FPrimitiveSceneInfo*>& GetPrimitives() const { return Primitives; }

    /** Get all primitive transforms */
    const TArray<FMatrix>& GetPrimitiveTransforms() const { return PrimitiveTransforms; }

    /** Get all primitive bounds */
    const TArray<FPrimitiveBounds>& GetPrimitiveBounds() const { return PrimitiveBounds; }

    /** Get all primitive proxies */
    const TArray<FPrimitiveSceneProxy*>& GetPrimitiveSceneProxies() const { return PrimitiveSceneProxies; }

    /** Get all lights (sparse array) */
    const TSparseArray<FLightSceneInfoCompact>& GetLights() const { return Lights; }

    /** Get directional lights */
    const TArray<FLightSceneInfo*>& GetDirectionalLights() const { return DirectionalLights; }

protected:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    /**
     * Adds a primitive to the scene on the render thread
     * @param PrimitiveSceneInfo The primitive scene info to add
     */
    void AddPrimitiveSceneInfo_RenderThread(FPrimitiveSceneInfo* PrimitiveSceneInfo);

    /**
     * Removes a primitive from the scene on the render thread
     * @param PrimitiveSceneInfo The primitive scene info to remove
     */
    void RemovePrimitiveSceneInfo_RenderThread(FPrimitiveSceneInfo* PrimitiveSceneInfo);

    /**
     * Adds a light to the scene on the render thread
     * @param LightSceneInfo The light scene info to add
     */
    void AddLightSceneInfo_RenderThread(FLightSceneInfo* LightSceneInfo);

    /**
     * Removes a light from the scene on the render thread
     * @param LightSceneInfo The light scene info to remove
     */
    void RemoveLightSceneInfo_RenderThread(FLightSceneInfo* LightSceneInfo);

public:
    // ========================================================================
    // Scene Data
    // ========================================================================

    /** An optional world associated with the scene */
    UWorld* World;

    /**
     * The following arrays are densely packed primitive data needed by various
     * rendering passes. PrimitiveSceneInfo->PackedIndex maintains the index
     * where data is stored in these arrays for a given primitive.
     */

    /** Packed array of primitives in the scene */
    TArray<FPrimitiveSceneInfo*> Primitives;

    /** Packed array of all transforms in the scene */
    TArray<FMatrix> PrimitiveTransforms;

    /** Packed array of primitive scene proxies in the scene */
    TArray<FPrimitiveSceneProxy*> PrimitiveSceneProxies;

    /** Packed array of primitive bounds */
    TArray<FPrimitiveBounds> PrimitiveBounds;

    /** Packed array of primitive flags */
    TArray<FPrimitiveFlagsCompact> PrimitiveFlagsCompact;

    /** Packed array of precomputed primitive visibility IDs */
    TArray<FPrimitiveVisibilityId> PrimitiveVisibilityIds;

    /** Packed array of primitive occlusion flags */
    TArray<uint8> PrimitiveOcclusionFlags;

    /** Packed array of primitive occlusion bounds */
    TArray<FBoxSphereBounds> PrimitiveOcclusionBounds;

    /** Packed array of primitive component IDs */
    TArray<FPrimitiveComponentId> PrimitiveComponentIds;

    /** The lights in the scene */
    TSparseArray<FLightSceneInfoCompact> Lights;

    /**
     * Lights in the scene which are invisible, but still needed by the editor for previewing.
     * Lights in this array cannot be in the Lights array.
     */
    TSparseArray<FLightSceneInfoCompact> InvisibleLights;

    /** The directional light to use for simple dynamic lighting, if any */
    FLightSceneInfo* SimpleDirectionalLight;

    /** The scene's sky light, if any */
    FSkyLightSceneProxy* SkyLight;

    /** Used to track the order that skylights were enabled in */
    TArray<FSkyLightSceneProxy*> SkyLightStack;

    /** Directional lights in the scene */
    TArray<FLightSceneInfo*> DirectionalLights;

    /** The attachment groups in the scene. The map key is the attachment group's root primitive */
    TMap<FPrimitiveComponentId, FAttachmentGroupSceneInfo> AttachmentGroups;

    // ========================================================================
    // Spatial Acceleration Structures
    // ========================================================================

    /**
     * Octree containing the primitives in the scene
     * Used for efficient spatial queries during visibility culling
     */
    FScenePrimitiveOctree PrimitiveOctree;

    /**
     * Octree containing shadow-casting local lights in the scene
     * Used for finding lights that affect primitives
     */
    FSceneLightOctree LocalShadowCastingLightOctree;

    // ========================================================================
    // Scene State Flags
    // ========================================================================

    /** Indicates whether this scene requires hit proxy rendering */
    bool bRequiresHitProxies;

    /** Whether this is an editor scene */
    bool bIsEditorScene;

    /** Current frame number */
    uint32 FrameNumber;

    /** Number of uncached static lighting interactions */
    mutable int32 NumUncachedStaticLightingInteractions;

    // ========================================================================
    // Visibility Culling Methods
    // ========================================================================

public:
    /**
     * Find all primitives visible in the given frustum
     * 
     * This method uses the primitive octree for efficient frustum culling.
     * It returns all primitives whose bounds intersect the view frustum.
     * 
     * @param Frustum The view frustum to cull against
     * @param OutVisiblePrimitives Output array of visible primitives
     */
    void FindVisiblePrimitives(const FConvexVolume& Frustum, 
                                TArray<FPrimitiveSceneInfo*>& OutVisiblePrimitives) const;

    /**
     * Find all lights affecting a primitive
     * 
     * @param PrimitiveSceneInfo The primitive to find lights for
     * @param OutAffectingLights Output array of affecting lights
     */
    void FindLightsAffectingPrimitive(const FPrimitiveSceneInfo* PrimitiveSceneInfo,
                                       TArray<FLightSceneInfo*>& OutAffectingLights) const;

    /**
     * Get the primitive octree for direct access
     * @return Reference to the primitive octree
     */
    const FScenePrimitiveOctree& GetPrimitiveOctree() const { return PrimitiveOctree; }

    /**
     * Get the light octree for direct access
     * @return Reference to the light octree
     */
    const FSceneLightOctree& GetLightOctree() const { return LocalShadowCastingLightOctree; }

private:
    /** Next available primitive component ID */
    uint32 NextPrimitiveComponentId;
};

} // namespace MonsterEngine
