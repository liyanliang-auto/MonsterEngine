// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file FloorMeshComponent.h
 * @brief Floor mesh component for rendering textured floor planes with lighting
 * 
 * UFloorMeshComponent is a specialized mesh component that renders a floor plane
 * with texture mapping, tiling support, and lighting/shadow integration.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/Components/StaticMeshComponent.h
 */

#include "Engine/Components/MeshComponent.h"
#include "Core/Templates/SharedPointer.h"
#include "Math/MonsterMath.h"
#include "RHI/RHI.h"

namespace MonsterEngine
{

// Forward declarations
class FFloorSceneProxy;

/**
 * Floor mesh component for rendering textured floor planes with lighting
 * 
 * This component:
 * - Creates a floor plane mesh with proper normals for lighting
 * - Supports texture tiling for large floors
 * - Integrates with the forward rendering pipeline
 * - Uses the scene proxy pattern for thread-safe rendering
 */
class UFloorMeshComponent : public UMeshComponent
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /** Default constructor */
    UFloorMeshComponent();

    /** Constructor with owner */
    explicit UFloorMeshComponent(AActor* InOwner);

    /** Virtual destructor */
    virtual ~UFloorMeshComponent();

    // ========================================================================
    // UPrimitiveComponent Interface
    // ========================================================================

    /**
     * Create the scene proxy for rendering
     * @return New scene proxy instance
     */
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

    /**
     * Get the local bounds of the floor
     * @return Local space bounding box
     */
    FBox GetLocalBounds() const;

    // ========================================================================
    // Texture Settings
    // ========================================================================

    /**
     * Set the floor texture
     * @param Texture - Texture to set
     */
    void SetTexture(TSharedPtr<MonsterRender::RHI::IRHITexture> Texture);

    /**
     * Get the floor texture
     * @return Floor texture
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> GetTexture() const { return FloorTexture; }

    /**
     * Set the texture sampler
     * @param InSampler - Sampler to set
     */
    void SetSampler(TSharedPtr<MonsterRender::RHI::IRHISampler> InSampler);

    /**
     * Get the texture sampler
     * @return Texture sampler
     */
    TSharedPtr<MonsterRender::RHI::IRHISampler> GetSampler() const { return Sampler; }

    /**
     * Set texture tile factor (how many times texture repeats)
     * @param Factor - Tile factor
     */
    void SetTextureTile(float Factor) { TextureTile = FMath::Max(0.1f, Factor); }

    /**
     * Get texture tile factor
     * @return Tile factor
     */
    float GetTextureTile() const { return TextureTile; }

    // ========================================================================
    // Size Settings
    // ========================================================================

    /**
     * Set the floor size (half-extent)
     * @param Size - Half-extent of the floor
     */
    void SetFloorSize(float Size);

    /**
     * Get the floor size
     * @return Half-extent of the floor
     */
    float GetFloorSize() const { return FloorSize; }

    // ========================================================================
    // Proxy Access
    // ========================================================================

    /**
     * Get the floor scene proxy (for direct rendering access)
     * @return Floor scene proxy
     */
    FFloorSceneProxy* GetFloorSceneProxy() const;

    /**
     * Check if the component needs to recreate its proxy
     * @return True if proxy needs recreation
     */
    bool NeedsProxyRecreation() const { return bNeedsProxyRecreation; }

    /**
     * Mark that proxy recreation is needed
     */
    void MarkProxyRecreationNeeded() { bNeedsProxyRecreation = true; }

    /**
     * Clear the proxy recreation flag
     */
    void ClearProxyRecreationNeeded() { bNeedsProxyRecreation = false; }

protected:
    /** Floor texture */
    TSharedPtr<MonsterRender::RHI::IRHITexture> FloorTexture;

    /** Texture sampler */
    TSharedPtr<MonsterRender::RHI::IRHISampler> Sampler;

    /** Texture tile factor */
    float TextureTile;

    /** Floor half-extent */
    float FloorSize;

    /** Whether proxy needs to be recreated */
    bool bNeedsProxyRecreation;
};

} // namespace MonsterEngine
