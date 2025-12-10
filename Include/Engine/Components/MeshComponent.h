// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file MeshComponent.h
 * @brief Base class for mesh components
 * 
 * UMeshComponent is the base class for components that render mesh geometry.
 * It provides material support and mesh-specific rendering features.
 * Following UE5's mesh component architecture.
 */

#include "PrimitiveComponent.h"
#include "Containers/Array.h"

namespace MonsterEngine
{

// Forward declarations
class UMaterialInterface;

/**
 * Base class for mesh components
 * 
 * UMeshComponent provides:
 * - Material slots and overrides
 * - Mesh-specific rendering settings
 * - LOD support
 */
class UMeshComponent : public UPrimitiveComponent
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /** Default constructor */
    UMeshComponent();

    /** Constructor with owner */
    explicit UMeshComponent(AActor* InOwner);

    /** Virtual destructor */
    virtual ~UMeshComponent();

    // ========================================================================
    // Materials
    // ========================================================================

    /**
     * Get the number of materials
     * @return The number of material slots
     */
    virtual int32 GetNumMaterials() const { return Materials.Num(); }

    /**
     * Get a material by index
     * @param MaterialIndex The material slot index
     * @return The material, or nullptr if invalid index
     */
    virtual UMaterialInterface* GetMaterial(int32 MaterialIndex) const;

    /**
     * Set a material by index
     * @param MaterialIndex The material slot index
     * @param Material The material to set
     */
    virtual void SetMaterial(int32 MaterialIndex, UMaterialInterface* Material);

    /**
     * Get all materials
     * @return Array of materials
     */
    const TArray<UMaterialInterface*>& GetMaterials() const { return Materials; }

    /**
     * Set all materials
     * @param NewMaterials Array of materials to set
     */
    void SetMaterials(const TArray<UMaterialInterface*>& NewMaterials);

    // ========================================================================
    // Material Overrides
    // ========================================================================

    /**
     * Get an override material by index
     * @param MaterialIndex The material slot index
     * @return The override material, or nullptr if not overridden
     */
    UMaterialInterface* GetOverrideMaterial(int32 MaterialIndex) const;

    /**
     * Set an override material by index
     * @param MaterialIndex The material slot index
     * @param Material The override material (nullptr to clear)
     */
    void SetOverrideMaterial(int32 MaterialIndex, UMaterialInterface* Material);

    /**
     * Clear all material overrides
     */
    void ClearOverrideMaterials();

    // ========================================================================
    // Rendering Settings
    // ========================================================================

    /** Check if the mesh uses vertex color */
    bool UsesVertexColor() const { return bUseVertexColor; }

    /** Set whether to use vertex color */
    void SetUseVertexColor(bool bNewUseVertexColor);

    /** Check if the mesh overrides the lightmap resolution */
    bool OverridesLightmapRes() const { return bOverrideLightmapRes; }

    /** Get the overridden lightmap resolution */
    int32 GetOverriddenLightmapRes() const { return OverriddenLightmapRes; }

    /** Set the overridden lightmap resolution */
    void SetOverriddenLightmapRes(int32 NewRes);

protected:
    // ========================================================================
    // Protected Data
    // ========================================================================

    /** Materials for each slot */
    TArray<UMaterialInterface*> Materials;

    /** Override materials (sparse - only set indices have values) */
    TArray<UMaterialInterface*> OverrideMaterials;

    /** Overridden lightmap resolution */
    int32 OverriddenLightmapRes;

    // ========================================================================
    // Flags
    // ========================================================================

    /** Whether to use vertex color */
    uint8 bUseVertexColor : 1;

    /** Whether to override lightmap resolution */
    uint8 bOverrideLightmapRes : 1;
};

/**
 * Static mesh component
 * Renders a static mesh asset
 */
class UStaticMeshComponent : public UMeshComponent
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /** Default constructor */
    UStaticMeshComponent();

    /** Constructor with owner */
    explicit UStaticMeshComponent(AActor* InOwner);

    /** Virtual destructor */
    virtual ~UStaticMeshComponent();

    // ========================================================================
    // Static Mesh
    // ========================================================================

    // Note: StaticMesh asset type would be defined elsewhere
    // For now, we use a placeholder

    /** Get the static mesh */
    // UStaticMesh* GetStaticMesh() const { return StaticMesh; }

    /** Set the static mesh */
    // void SetStaticMesh(UStaticMesh* NewMesh);

    // ========================================================================
    // Scene Proxy
    // ========================================================================

    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

    // ========================================================================
    // LOD
    // ========================================================================

    /** Get the forced LOD level (-1 = auto) */
    int32 GetForcedLodModel() const { return ForcedLodModel; }

    /** Set the forced LOD level */
    void SetForcedLodModel(int32 NewForcedLod);

    /** Get the minimum LOD level */
    int32 GetMinLOD() const { return MinLOD; }

    /** Set the minimum LOD level */
    void SetMinLOD(int32 NewMinLOD);

    // ========================================================================
    // Streaming
    // ========================================================================

    /** Get the streaming distance multiplier */
    float GetStreamingDistanceMultiplier() const { return StreamingDistanceMultiplier; }

    /** Set the streaming distance multiplier */
    void SetStreamingDistanceMultiplier(float NewMultiplier);

protected:
    // ========================================================================
    // Protected Data
    // ========================================================================

    // UStaticMesh* StaticMesh;

    /** Forced LOD level (-1 = auto) */
    int32 ForcedLodModel;

    /** Minimum LOD level */
    int32 MinLOD;

    /** Streaming distance multiplier */
    float StreamingDistanceMultiplier;

    // ========================================================================
    // Flags
    // ========================================================================

    /** Whether to use default collision */
    uint8 bUseDefaultCollision : 1;

    /** Whether to generate overlap events */
    uint8 bGenerateOverlapEvents : 1;
};

} // namespace MonsterEngine
