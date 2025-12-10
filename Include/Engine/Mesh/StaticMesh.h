// Copyright Monster Engine. All Rights Reserved.
// Reference UE5: Engine/Source/Runtime/Engine/Classes/Engine/StaticMesh.h
//               Engine/Source/Runtime/Engine/Public/StaticMeshResources.h

#pragma once

/**
 * @file StaticMesh.h
 * @brief Static mesh resource and LOD classes
 * 
 * This file defines the core static mesh classes following UE5's architecture:
 * - FMeshSection: A section of a mesh using a single material
 * - FStaticMeshLODResources: Per-LOD rendering resources
 * - FStaticMeshRenderData: Complete render data for all LODs
 * - FStaticMesh: The main static mesh resource class
 * 
 * The mesh data is organized hierarchically:
 * FStaticMesh
 *   └── FStaticMeshRenderData
 *         └── FStaticMeshLODResources[] (one per LOD)
 *               └── FMeshSection[] (one per material)
 *               └── Vertex Buffers (position, tangent, UV, color)
 *               └── Index Buffer
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "Math/MathFwd.h"
#include "Math/Box.h"
#include "Engine/Mesh/StaticMeshVertex.h"
#include "Engine/Mesh/VertexFactory.h"
#include "RHI/RHIResources.h"

// Forward declaration in MonsterRender namespace
namespace MonsterRender::RHI { class IRHIDevice; }

namespace MonsterEngine
{

// Use MonsterRender::RHI buffer types
using MonsterRender::RHI::FRHIVertexBufferRef;
using MonsterRender::RHI::FRHIIndexBufferRef;

// Forward declarations
class FStaticMesh;
class FStaticMeshRenderData;
struct FStaticMeshLODResources;

// ============================================================================
// Mesh Section
// ============================================================================

/**
 * @struct FMeshSection
 * @brief A section of a mesh that uses a single material
 * 
 * Each mesh section represents a contiguous range of triangles that share
 * the same material. This allows efficient batching during rendering.
 * 
 * Reference UE5: FStaticMeshSection
 */
struct FMeshSection
{
    /** Index of the material used by this section */
    int32 MaterialIndex;
    
    /** First index in the index buffer for this section */
    uint32 FirstIndex;
    
    /** Number of triangles in this section */
    uint32 NumTriangles;
    
    /** Minimum vertex index used by this section */
    uint32 MinVertexIndex;
    
    /** Maximum vertex index used by this section */
    uint32 MaxVertexIndex;
    
    /** Whether this section is enabled for rendering */
    bool bEnableCollision;
    
    /** Whether this section casts shadows */
    bool bCastShadow;
    
    /** Whether this section is visible */
    bool bForceOpaque;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    /**
     * Default constructor
     */
    FORCEINLINE FMeshSection()
        : MaterialIndex(0)
        , FirstIndex(0)
        , NumTriangles(0)
        , MinVertexIndex(0)
        , MaxVertexIndex(0)
        , bEnableCollision(true)
        , bCastShadow(true)
        , bForceOpaque(false)
    {
    }
    
    /**
     * Constructor with parameters
     * @param InMaterialIndex Material index
     * @param InFirstIndex First index in index buffer
     * @param InNumTriangles Number of triangles
     * @param InMinVertex Minimum vertex index
     * @param InMaxVertex Maximum vertex index
     */
    FORCEINLINE FMeshSection(
        int32 InMaterialIndex,
        uint32 InFirstIndex,
        uint32 InNumTriangles,
        uint32 InMinVertex,
        uint32 InMaxVertex)
        : MaterialIndex(InMaterialIndex)
        , FirstIndex(InFirstIndex)
        , NumTriangles(InNumTriangles)
        , MinVertexIndex(InMinVertex)
        , MaxVertexIndex(InMaxVertex)
        , bEnableCollision(true)
        , bCastShadow(true)
        , bForceOpaque(false)
    {
    }
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    /**
     * Get the number of indices in this section
     * @return Number of indices (NumTriangles * 3)
     */
    FORCEINLINE uint32 GetNumIndices() const
    {
        return NumTriangles * 3;
    }
    
    /**
     * Get the number of vertices used by this section
     * @return Vertex count
     */
    FORCEINLINE uint32 GetNumVertices() const
    {
        return MaxVertexIndex - MinVertexIndex + 1;
    }
    
    /**
     * Check if this section is valid
     * @return true if section has triangles
     */
    FORCEINLINE bool IsValid() const
    {
        return NumTriangles > 0;
    }
};

// ============================================================================
// Static Mesh Vertex Buffers
// ============================================================================

/**
 * @struct FStaticMeshVertexBuffers
 * @brief Collection of vertex buffers for a static mesh LOD
 * 
 * Organizes vertex data into separate buffers for optimal GPU access:
 * - Position buffer: Vertex positions only (for depth-only passes)
 * - Tangent buffer: Normal and tangent vectors
 * - TexCoord buffer: UV coordinates
 * - Color buffer: Vertex colors (optional)
 * 
 * Reference UE5: FStaticMeshVertexBuffers
 */
struct FStaticMeshVertexBuffers
{
    /** Position vertex buffer */
    FRHIVertexBufferRef PositionVertexBuffer;
    
    /** Tangent (normal + tangent) vertex buffer */
    FRHIVertexBufferRef TangentVertexBuffer;
    
    /** Texture coordinate vertex buffer */
    FRHIVertexBufferRef TexCoordVertexBuffer;
    
    /** Color vertex buffer (may be null if no vertex colors) */
    FRHIVertexBufferRef ColorVertexBuffer;
    
    /** Number of vertices */
    uint32 NumVertices;
    
    /** Number of texture coordinate sets */
    uint32 NumTexCoords;
    
    /** Whether using high precision tangent basis */
    bool bUseHighPrecisionTangentBasis;
    
    /** Whether using full precision UVs */
    bool bUseFullPrecisionUVs;
    
    /** Whether vertex colors are present */
    bool bHasVertexColors;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    /**
     * Default constructor
     */
    FORCEINLINE FStaticMeshVertexBuffers()
        : NumVertices(0)
        , NumTexCoords(1)
        , bUseHighPrecisionTangentBasis(false)
        , bUseFullPrecisionUVs(false)
        , bHasVertexColors(false)
    {
    }
    
    // ========================================================================
    // Buffer Size Calculations
    // ========================================================================
    
    /**
     * Get the stride of the position buffer
     * @return Stride in bytes (always 12 for FVector3f)
     */
    FORCEINLINE uint32 GetPositionStride() const
    {
        return sizeof(Math::FVector3f);
    }
    
    /**
     * Get the stride of the tangent buffer
     * @return Stride in bytes (8 for default, 16 for high precision)
     */
    FORCEINLINE uint32 GetTangentStride() const
    {
        return bUseHighPrecisionTangentBasis 
            ? sizeof(FStaticMeshVertexTangentHighPrecision)
            : sizeof(FStaticMeshVertexTangent);
    }
    
    /**
     * Get the stride of the texcoord buffer (per vertex)
     * @return Stride in bytes
     */
    FORCEINLINE uint32 GetTexCoordStride() const
    {
        uint32 UVSize = bUseFullPrecisionUVs 
            ? sizeof(FStaticMeshVertexUVHighPrecision)
            : sizeof(FStaticMeshVertexUV);
        return UVSize * NumTexCoords;
    }
    
    /**
     * Get the stride of the color buffer
     * @return Stride in bytes (4 for FColor)
     */
    FORCEINLINE uint32 GetColorStride() const
    {
        return sizeof(FColor);
    }
    
    /**
     * Check if all required buffers are valid
     * @return true if position and tangent buffers exist
     */
    FORCEINLINE bool IsValid() const
    {
        return PositionVertexBuffer.IsValid() && NumVertices > 0;
    }
    
    /**
     * Release all GPU resources
     */
    void ReleaseResources()
    {
        PositionVertexBuffer.SafeRelease();
        TangentVertexBuffer.SafeRelease();
        TexCoordVertexBuffer.SafeRelease();
        ColorVertexBuffer.SafeRelease();
        NumVertices = 0;
    }
};

// ============================================================================
// Static Mesh LOD Resources
// ============================================================================

/**
 * @struct FStaticMeshLODResources
 * @brief Per-LOD rendering resources for a static mesh
 * 
 * Contains all GPU resources needed to render a single LOD level:
 * - Vertex buffers (position, tangent, UV, color)
 * - Index buffer
 * - Mesh sections (material assignments)
 * 
 * Reference UE5: FStaticMeshLODResources
 */
struct FStaticMeshLODResources
{
    /** Vertex buffers */
    FStaticMeshVertexBuffers VertexBuffers;
    
    /** Index buffer */
    FRHIIndexBufferRef IndexBuffer;
    
    /** Mesh sections (one per material) */
    TArray<FMeshSection> Sections;
    
    /** Whether using 32-bit indices */
    bool bUse32BitIndices;
    
    /** Maximum number of bones influencing a vertex (for skeletal meshes, 0 for static) */
    uint32 MaxBoneInfluences;
    
    /** LOD index */
    int32 LODIndex;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    /**
     * Default constructor
     */
    FORCEINLINE FStaticMeshLODResources()
        : bUse32BitIndices(false)
        , MaxBoneInfluences(0)
        , LODIndex(0)
    {
    }
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    /**
     * Get the number of vertices in this LOD
     * @return Vertex count
     */
    FORCEINLINE uint32 GetNumVertices() const
    {
        return VertexBuffers.NumVertices;
    }
    
    /**
     * Get the number of triangles in this LOD
     * @return Triangle count (sum of all sections)
     */
    uint32 GetNumTriangles() const
    {
        uint32 Total = 0;
        for (const FMeshSection& Section : Sections)
        {
            Total += Section.NumTriangles;
        }
        return Total;
    }
    
    /**
     * Get the number of indices in this LOD
     * @return Index count
     */
    FORCEINLINE uint32 GetNumIndices() const
    {
        return GetNumTriangles() * 3;
    }
    
    /**
     * Get the number of sections in this LOD
     * @return Section count
     */
    FORCEINLINE int32 GetNumSections() const
    {
        return Sections.Num();
    }
    
    /**
     * Get the index buffer stride
     * @return 4 for 32-bit, 2 for 16-bit
     */
    FORCEINLINE uint32 GetIndexStride() const
    {
        return bUse32BitIndices ? 4 : 2;
    }
    
    /**
     * Check if this LOD has valid resources
     * @return true if vertex and index buffers exist
     */
    FORCEINLINE bool IsValid() const
    {
        return VertexBuffers.IsValid() && IndexBuffer.IsValid() && Sections.Num() > 0;
    }
    
    /**
     * Release all GPU resources
     */
    void ReleaseResources()
    {
        VertexBuffers.ReleaseResources();
        IndexBuffer.SafeRelease();
        Sections.Empty();
    }
};

// ============================================================================
// Static Mesh Render Data
// ============================================================================

/**
 * @class FStaticMeshRenderData
 * @brief Complete render data for a static mesh (all LODs)
 * 
 * Contains all LOD resources and metadata for rendering a static mesh.
 * This is the main data structure passed to the renderer.
 * 
 * Reference UE5: FStaticMeshRenderData
 */
class FStaticMeshRenderData
{
public:
    /** Per-LOD resources */
    TArray<FStaticMeshLODResources> LODResources;
    
    /** Screen size thresholds for LOD transitions */
    TArray<float> ScreenSize;
    
    /** Bounding box of the mesh */
    Math::FBox3f Bounds;
    
    /** Bounding sphere radius */
    float BoundingSphereRadius;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    /**
     * Default constructor
     */
    FStaticMeshRenderData()
        : BoundingSphereRadius(0.0f)
    {
    }
    
    // ========================================================================
    // LOD Management
    // ========================================================================
    
    /**
     * Get the number of LOD levels
     * @return LOD count
     */
    FORCEINLINE int32 GetNumLODs() const
    {
        return LODResources.Num();
    }
    
    /**
     * Get resources for a specific LOD
     * @param LODIndex LOD level index
     * @return Reference to LOD resources
     */
    FORCEINLINE FStaticMeshLODResources& GetLODResources(int32 LODIndex)
    {
        return LODResources[LODIndex];
    }
    
    /**
     * Get resources for a specific LOD (const)
     * @param LODIndex LOD level index
     * @return Const reference to LOD resources
     */
    FORCEINLINE const FStaticMeshLODResources& GetLODResources(int32 LODIndex) const
    {
        return LODResources[LODIndex];
    }
    
    /**
     * Allocate LOD resources
     * @param NumLODs Number of LOD levels to allocate
     */
    void AllocateLODResources(int32 NumLODs)
    {
        LODResources.SetNum(NumLODs);
        ScreenSize.SetNum(NumLODs);
        
        // Initialize LOD indices and default screen sizes
        for (int32 i = 0; i < NumLODs; ++i)
        {
            LODResources[i].LODIndex = i;
            // Default screen size thresholds (can be customized)
            ScreenSize[i] = 1.0f / (1 << i); // 1.0, 0.5, 0.25, 0.125, ...
        }
    }
    
    /**
     * Get the appropriate LOD index for a given screen size
     * @param InScreenSize Screen size of the mesh
     * @return LOD index to use
     */
    int32 GetLODForScreenSize(float InScreenSize) const
    {
        for (int32 i = 0; i < ScreenSize.Num(); ++i)
        {
            if (InScreenSize >= ScreenSize[i])
            {
                return i;
            }
        }
        return ScreenSize.Num() - 1;
    }
    
    /**
     * Check if render data is valid
     * @return true if at least one LOD exists and is valid
     */
    FORCEINLINE bool IsValid() const
    {
        return LODResources.Num() > 0 && LODResources[0].IsValid();
    }
    
    /**
     * Release all GPU resources
     */
    void ReleaseResources()
    {
        for (FStaticMeshLODResources& LOD : LODResources)
        {
            LOD.ReleaseResources();
        }
        LODResources.Empty();
        ScreenSize.Empty();
    }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    /**
     * Get total vertex count across all LODs
     * @return Total vertex count
     */
    uint32 GetTotalVertexCount() const
    {
        uint32 Total = 0;
        for (const FStaticMeshLODResources& LOD : LODResources)
        {
            Total += LOD.GetNumVertices();
        }
        return Total;
    }
    
    /**
     * Get total triangle count across all LODs
     * @return Total triangle count
     */
    uint32 GetTotalTriangleCount() const
    {
        uint32 Total = 0;
        for (const FStaticMeshLODResources& LOD : LODResources)
        {
            Total += LOD.GetNumTriangles();
        }
        return Total;
    }
};

// ============================================================================
// Material Slot
// ============================================================================

/**
 * @struct FStaticMaterial
 * @brief Material slot information for a static mesh
 * 
 * Associates a material with a name for identification in the editor
 * and at runtime.
 */
struct FStaticMaterial
{
    /** Material slot name (for identification) */
    String MaterialSlotName;
    
    /** Import name from the source file */
    String ImportedMaterialSlotName;
    
    /** UV channel index used for this material */
    int32 UVChannelIndex;
    
    /**
     * Default constructor
     */
    FORCEINLINE FStaticMaterial()
        : UVChannelIndex(0)
    {
    }
    
    /**
     * Constructor with name
     * @param InName Material slot name
     */
    explicit FORCEINLINE FStaticMaterial(const String& InName)
        : MaterialSlotName(InName)
        , ImportedMaterialSlotName(InName)
        , UVChannelIndex(0)
    {
    }
};

// ============================================================================
// Static Mesh
// ============================================================================

/**
 * @class FStaticMesh
 * @brief Main static mesh resource class
 * 
 * Represents a complete static mesh asset with all LODs, materials,
 * and metadata. This is the primary class for working with static meshes.
 * 
 * Reference UE5: UStaticMesh
 */
class FStaticMesh
{
public:
    // ========================================================================
    // Constructors / Destructor
    // ========================================================================
    
    /**
     * Default constructor
     */
    FStaticMesh();
    
    /**
     * Constructor with name
     * @param InName Mesh name
     */
    explicit FStaticMesh(const String& InName);
    
    /**
     * Destructor
     */
    ~FStaticMesh();
    
    // Prevent copying
    FStaticMesh(const FStaticMesh&) = delete;
    FStaticMesh& operator=(const FStaticMesh&) = delete;
    
    // Allow moving
    FStaticMesh(FStaticMesh&& Other) noexcept;
    FStaticMesh& operator=(FStaticMesh&& Other) noexcept;
    
    // ========================================================================
    // Properties
    // ========================================================================
    
    /**
     * Get the mesh name
     * @return Mesh name
     */
    const String& GetName() const { return Name; }
    
    /**
     * Set the mesh name
     * @param InName New name
     */
    void SetName(const String& InName) { Name = InName; }
    
    /**
     * Get the source file path
     * @return Source file path
     */
    const String& GetSourceFilePath() const { return SourceFilePath; }
    
    /**
     * Set the source file path
     * @param InPath Source file path
     */
    void SetSourceFilePath(const String& InPath) { SourceFilePath = InPath; }
    
    // ========================================================================
    // Render Data
    // ========================================================================
    
    /**
     * Get the render data
     * @return Pointer to render data (may be null)
     */
    FStaticMeshRenderData* GetRenderData() { return RenderData.Get(); }
    
    /**
     * Get the render data (const)
     * @return Const pointer to render data (may be null)
     */
    const FStaticMeshRenderData* GetRenderData() const { return RenderData.Get(); }
    
    /**
     * Check if render data exists
     * @return true if render data is available
     */
    bool HasRenderData() const { return RenderData != nullptr && RenderData->IsValid(); }
    
    /**
     * Allocate render data
     * @return Reference to the allocated render data
     */
    FStaticMeshRenderData& AllocateRenderData();
    
    /**
     * Release render data
     */
    void ReleaseRenderData();
    
    // ========================================================================
    // Materials
    // ========================================================================
    
    /**
     * Get the material slots
     * @return Reference to material array
     */
    TArray<FStaticMaterial>& GetStaticMaterials() { return StaticMaterials; }
    
    /**
     * Get the material slots (const)
     * @return Const reference to material array
     */
    const TArray<FStaticMaterial>& GetStaticMaterials() const { return StaticMaterials; }
    
    /**
     * Get the number of material slots
     * @return Material count
     */
    int32 GetNumMaterials() const { return StaticMaterials.Num(); }
    
    /**
     * Add a material slot
     * @param Material Material to add
     * @return Index of the added material
     */
    int32 AddMaterial(const FStaticMaterial& Material);
    
    // ========================================================================
    // Bounds
    // ========================================================================
    
    /**
     * Get the bounding box
     * @return Bounding box
     */
    const Math::FBox3f& GetBounds() const { return Bounds; }
    
    /**
     * Set the bounding box
     * @param InBounds New bounding box
     */
    void SetBounds(const Math::FBox3f& InBounds) { Bounds = InBounds; }
    
    /**
     * Calculate bounds from render data
     */
    void CalculateBounds();
    
    // ========================================================================
    // LOD
    // ========================================================================
    
    /**
     * Get the number of LOD levels
     * @return LOD count
     */
    int32 GetNumLODs() const;
    
    /**
     * Get the minimum LOD level
     * @return Minimum LOD index
     */
    int32 GetMinLOD() const { return MinLOD; }
    
    /**
     * Set the minimum LOD level
     * @param InMinLOD Minimum LOD index
     */
    void SetMinLOD(int32 InMinLOD) { MinLOD = InMinLOD; }
    
    // ========================================================================
    // Validation
    // ========================================================================
    
    /**
     * Check if the mesh is valid for rendering
     * @return true if mesh can be rendered
     */
    bool IsValid() const;
    
    /**
     * Get statistics about the mesh
     * @param OutVertexCount Output vertex count
     * @param OutTriangleCount Output triangle count
     * @param OutLODCount Output LOD count
     */
    void GetStatistics(uint32& OutVertexCount, uint32& OutTriangleCount, int32& OutLODCount) const;

private:
    /** Mesh name */
    String Name;
    
    /** Source file path */
    String SourceFilePath;
    
    /** Render data (all LODs) */
    TUniquePtr<FStaticMeshRenderData> RenderData;
    
    /** Material slots */
    TArray<FStaticMaterial> StaticMaterials;
    
    /** Bounding box */
    Math::FBox3f Bounds;
    
    /** Minimum LOD level to use */
    int32 MinLOD;
    
    /** Lightmap resolution */
    int32 LightMapResolution;
    
    /** Lightmap coordinate index */
    int32 LightMapCoordinateIndex;
};

} // namespace MonsterEngine
