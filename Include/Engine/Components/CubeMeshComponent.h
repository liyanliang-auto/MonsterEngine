// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CubeMeshComponent.h
 * @brief Cube mesh component for rendering textured cubes with lighting
 * 
 * UCubeMeshComponent is a specialized mesh component that renders a cube
 * with texture mapping and lighting support. It creates its own vertex
 * buffer with position, normal, and texture coordinate data.
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
class FCubeSceneProxy;

/**
 * Vertex structure for lit cube rendering
 * Contains position, normal, and texture coordinates
 */
struct FCubeLitVertex
{
    float Position[3];   // Position (x, y, z)
    float Normal[3];     // Normal (nx, ny, nz)
    float TexCoord[2];   // Texture coordinates (u, v)
};

/**
 * Cube mesh component for rendering textured cubes with lighting
 * 
 * This component:
 * - Creates a cube mesh with proper normals for lighting
 * - Supports two textures for blending
 * - Integrates with the forward rendering pipeline
 * - Uses the scene proxy pattern for thread-safe rendering
 */
class UCubeMeshComponent : public UMeshComponent
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /** Default constructor */
    UCubeMeshComponent();

    /** Constructor with owner */
    explicit UCubeMeshComponent(AActor* InOwner);

    /** Virtual destructor */
    virtual ~UCubeMeshComponent();

    // ========================================================================
    // UPrimitiveComponent Interface
    // ========================================================================

    /**
     * Create the scene proxy for rendering
     * @return New scene proxy instance
     */
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

    /**
     * Get the local bounds of the cube
     * @return Local space bounding box
     */
    FBox GetLocalBounds() const;

    // ========================================================================
    // Textures
    // ========================================================================

    /**
     * Set the first texture (base texture)
     * @param Texture - Texture to set
     */
    void SetTexture1(TSharedPtr<MonsterRender::RHI::IRHITexture> Texture);

    /**
     * Get the first texture
     * @return First texture
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> GetTexture1() const { return Texture1; }

    /**
     * Set the second texture (blend texture)
     * @param Texture - Texture to set
     */
    void SetTexture2(TSharedPtr<MonsterRender::RHI::IRHITexture> Texture);

    /**
     * Get the second texture
     * @return Second texture
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> GetTexture2() const { return Texture2; }

    /**
     * Set the texture blend factor
     * @param Factor - Blend factor (0 = texture1 only, 1 = texture2 only)
     */
    void SetTextureBlendFactor(float Factor) { TextureBlendFactor = FMath::Clamp(Factor, 0.0f, 1.0f); }

    /**
     * Get the texture blend factor
     * @return Blend factor
     */
    float GetTextureBlendFactor() const { return TextureBlendFactor; }

    // ========================================================================
    // Rendering Settings
    // ========================================================================

    /**
     * Set the cube size (half-extent)
     * @param Size - Half-extent of the cube
     */
    void SetCubeSize(float Size);

    /**
     * Get the cube size
     * @return Half-extent of the cube
     */
    float GetCubeSize() const { return CubeSize; }

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

    // ========================================================================
    // Static Mesh Data Generation
    // ========================================================================

    /**
     * Generate cube vertex data with normals
     * @param OutVertices - Array to receive vertex data
     * @param HalfExtent - Half-extent of the cube
     */
    static void GenerateCubeVertices(TArray<FCubeLitVertex>& OutVertices, float HalfExtent = 0.5f);

protected:
    /** First texture (base) */
    TSharedPtr<MonsterRender::RHI::IRHITexture> Texture1;

    /** Second texture (blend) */
    TSharedPtr<MonsterRender::RHI::IRHITexture> Texture2;

    /** Texture blend factor */
    float TextureBlendFactor;

    /** Cube half-extent */
    float CubeSize;

    /** Whether the proxy needs to be recreated */
    uint8 bNeedsProxyRecreation : 1;
};

} // namespace MonsterEngine
