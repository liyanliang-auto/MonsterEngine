// Copyright Monster Engine. All Rights Reserved.
// Reference UE5: Engine/Source/Runtime/Engine/Public/DynamicMeshBuilder.h
//               Engine/Source/Runtime/MeshDescription/Public/MeshDescription.h

#pragma once

/**
 * @file MeshBuilder.h
 * @brief Mesh building utilities for constructing static meshes
 * 
 * This file provides the FMeshBuilder class for constructing static meshes
 * from raw vertex and index data. It supports:
 * - Adding vertices with full attribute data
 * - Adding triangles/indices
 * - Computing normals and tangents
 * - Building GPU-ready vertex and index buffers
 * - Creating FStaticMesh resources
 * 
 * The builder follows a typical workflow:
 * 1. Create builder
 * 2. Add vertices and triangles
 * 3. Compute normals/tangents if needed
 * 4. Build to FStaticMesh
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "Math/MathFwd.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "Engine/Mesh/StaticMeshVertex.h"
#include "Engine/Mesh/StaticMesh.h"
#include "RHI/RHIResources.h"

// Forward declaration in MonsterRender namespace
namespace MonsterRender { namespace RHI { class IRHIDevice; } }

namespace MonsterEngine
{

// Use MonsterRender::RHI buffer types
using MonsterRender::RHI::FRHIVertexBufferRef;
using MonsterRender::RHI::FRHIIndexBufferRef;
using MonsterRender::RHI::IRHIDevice;

/**
 * @struct FMeshBuilderSettings
 * @brief Settings for mesh building operations
 */
struct FMeshBuilderSettings
{
    /** Whether to use high precision tangent basis (16-bit) */
    bool bUseHighPrecisionTangentBasis;
    
    /** Whether to use full precision UVs (32-bit float) */
    bool bUseFullPrecisionUVs;
    
    /** Whether to generate vertex colors */
    bool bGenerateVertexColors;
    
    /** Whether to compute normals if not provided */
    bool bComputeNormals;
    
    /** Whether to compute tangents if not provided */
    bool bComputeTangents;
    
    /** Whether to use smooth normals (average across shared vertices) */
    bool bUseSmoothNormals;
    
    /** Whether to use mikktspace for tangent calculation */
    bool bUseMikkTSpace;
    
    /** Number of texture coordinate sets */
    uint32 NumTexCoords;
    
    /**
     * Default constructor
     */
    FORCEINLINE FMeshBuilderSettings()
        : bUseHighPrecisionTangentBasis(false)
        , bUseFullPrecisionUVs(false)
        , bGenerateVertexColors(true)
        , bComputeNormals(true)
        , bComputeTangents(true)
        , bUseSmoothNormals(true)
        , bUseMikkTSpace(true)
        , NumTexCoords(1)
    {
    }
};

/**
 * @class FMeshBuilder
 * @brief Utility class for building static meshes from raw data
 * 
 * FMeshBuilder provides a convenient interface for constructing meshes
 * programmatically or from imported data. It handles:
 * - Vertex data accumulation
 * - Index buffer construction
 * - Normal and tangent computation
 * - GPU buffer creation
 * 
 * Example usage:
 * @code
 * FMeshBuilder Builder;
 * 
 * // Add a triangle
 * int32 V0 = Builder.AddVertex(FVector3f(0, 0, 0), FVector2f(0, 0));
 * int32 V1 = Builder.AddVertex(FVector3f(1, 0, 0), FVector2f(1, 0));
 * int32 V2 = Builder.AddVertex(FVector3f(0, 1, 0), FVector2f(0, 1));
 * Builder.AddTriangle(V0, V1, V2);
 * 
 * // Build the mesh
 * Builder.ComputeNormals();
 * Builder.ComputeTangents();
 * FStaticMesh* Mesh = Builder.Build(Device, "MyMesh");
 * @endcode
 * 
 * Reference UE5: FDynamicMeshBuilder, FMeshDescription
 */
class FMeshBuilder
{
public:
    // ========================================================================
    // Constructors / Destructor
    // ========================================================================
    
    /**
     * Default constructor
     */
    FMeshBuilder();
    
    /**
     * Constructor with settings
     * @param InSettings Builder settings
     */
    explicit FMeshBuilder(const FMeshBuilderSettings& InSettings);
    
    /**
     * Destructor
     */
    ~FMeshBuilder();
    
    // ========================================================================
    // Vertex Operations
    // ========================================================================
    
    /**
     * Add a vertex with position only
     * @param Position Vertex position
     * @return Index of the added vertex
     */
    int32 AddVertex(const Math::FVector3f& Position);
    
    /**
     * Add a vertex with position and UV
     * @param Position Vertex position
     * @param UV Texture coordinate
     * @return Index of the added vertex
     */
    int32 AddVertex(const Math::FVector3f& Position, const Math::FVector2f& UV);
    
    /**
     * Add a vertex with position, UV, and normal
     * @param Position Vertex position
     * @param UV Texture coordinate
     * @param Normal Vertex normal
     * @return Index of the added vertex
     */
    int32 AddVertex(
        const Math::FVector3f& Position,
        const Math::FVector2f& UV,
        const Math::FVector3f& Normal);
    
    /**
     * Add a vertex with full data
     * @param Position Vertex position
     * @param Normal Vertex normal
     * @param Tangent Vertex tangent
     * @param UV Texture coordinate
     * @param Color Vertex color
     * @return Index of the added vertex
     */
    int32 AddVertex(
        const Math::FVector3f& Position,
        const Math::FVector3f& Normal,
        const Math::FVector3f& Tangent,
        const Math::FVector2f& UV,
        const FColor& Color = FColor::White);
    
    /**
     * Add a vertex from FStaticMeshBuildVertex
     * @param Vertex Complete vertex data
     * @return Index of the added vertex
     */
    int32 AddVertex(const FStaticMeshBuildVertex& Vertex);
    
    /**
     * Add multiple vertices
     * @param Vertices Array of vertices to add
     * @return Index of the first added vertex
     */
    int32 AddVertices(const TArray<FStaticMeshBuildVertex>& Vertices);
    
    /**
     * Set UV for a specific vertex and channel
     * @param VertexIndex Vertex index
     * @param UVChannel UV channel (0-7)
     * @param UV Texture coordinate
     */
    void SetVertexUV(int32 VertexIndex, uint32 UVChannel, const Math::FVector2f& UV);
    
    /**
     * Set color for a specific vertex
     * @param VertexIndex Vertex index
     * @param Color Vertex color
     */
    void SetVertexColor(int32 VertexIndex, const FColor& Color);
    
    /**
     * Reserve space for vertices
     * @param NumVertices Number of vertices to reserve
     */
    void ReserveVertices(int32 NumVertices);
    
    /**
     * Get the number of vertices
     * @return Vertex count
     */
    int32 GetNumVertices() const { return Vertices.Num(); }
    
    /**
     * Get a vertex by index
     * @param Index Vertex index
     * @return Reference to vertex data
     */
    FStaticMeshBuildVertex& GetVertex(int32 Index) { return Vertices[Index]; }
    
    /**
     * Get a vertex by index (const)
     * @param Index Vertex index
     * @return Const reference to vertex data
     */
    const FStaticMeshBuildVertex& GetVertex(int32 Index) const { return Vertices[Index]; }
    
    // ========================================================================
    // Index / Triangle Operations
    // ========================================================================
    
    /**
     * Add a triangle by vertex indices
     * @param V0 First vertex index
     * @param V1 Second vertex index
     * @param V2 Third vertex index
     * @param MaterialIndex Material index for this triangle (default 0)
     */
    void AddTriangle(int32 V0, int32 V1, int32 V2, int32 MaterialIndex = 0);
    
    /**
     * Add multiple triangles from index array
     * @param Indices Array of indices (must be multiple of 3)
     * @param MaterialIndex Material index for all triangles
     */
    void AddTriangles(const TArray<uint32>& Indices, int32 MaterialIndex = 0);
    
    /**
     * Add a quad (two triangles)
     * @param V0 First vertex index
     * @param V1 Second vertex index
     * @param V2 Third vertex index
     * @param V3 Fourth vertex index
     * @param MaterialIndex Material index
     */
    void AddQuad(int32 V0, int32 V1, int32 V2, int32 V3, int32 MaterialIndex = 0);
    
    /**
     * Reserve space for triangles
     * @param NumTriangles Number of triangles to reserve
     */
    void ReserveTriangles(int32 NumTriangles);
    
    /**
     * Get the number of triangles
     * @return Triangle count
     */
    int32 GetNumTriangles() const { return Indices.Num() / 3; }
    
    /**
     * Get the number of indices
     * @return Index count
     */
    int32 GetNumIndices() const { return Indices.Num(); }
    
    // ========================================================================
    // Material Operations
    // ========================================================================
    
    /**
     * Set the number of material slots
     * @param NumMaterials Number of materials
     */
    void SetNumMaterials(int32 NumMaterials);
    
    /**
     * Get the number of material slots
     * @return Material count
     */
    int32 GetNumMaterials() const { return MaterialNames.Num(); }
    
    /**
     * Set material name
     * @param MaterialIndex Material index
     * @param Name Material name
     */
    void SetMaterialName(int32 MaterialIndex, const String& Name);
    
    // ========================================================================
    // Normal and Tangent Computation
    // ========================================================================
    
    /**
     * Compute normals for all vertices
     * Uses face normals averaged at shared vertices if bUseSmoothNormals is true
     */
    void ComputeNormals();
    
    /**
     * Compute flat normals (one normal per face, not smoothed)
     * This will duplicate vertices as needed
     */
    void ComputeFlatNormals();
    
    /**
     * Compute tangents for all vertices
     * Requires normals and UVs to be set
     */
    void ComputeTangents();
    
    /**
     * Compute tangents using MikkTSpace algorithm
     * Provides high-quality tangent space for normal mapping
     */
    void ComputeTangentsMikkTSpace();
    
    // ========================================================================
    // Bounds Computation
    // ========================================================================
    
    /**
     * Compute bounding box from vertices
     * @return Bounding box
     */
    Math::FBox3f ComputeBounds() const;
    
    // ========================================================================
    // Building
    // ========================================================================
    
    /**
     * Build a static mesh from the accumulated data
     * @param Device RHI device for GPU buffer creation
     * @param MeshName Name for the created mesh
     * @return Newly created static mesh (caller owns)
     */
    FStaticMesh* Build(IRHIDevice* Device, const String& MeshName);
    
    /**
     * Build into an existing static mesh
     * @param Device RHI device for GPU buffer creation
     * @param OutMesh Mesh to build into
     * @return true if successful
     */
    bool BuildInto(IRHIDevice* Device, FStaticMesh& OutMesh);
    
    /**
     * Build LOD resources only (no FStaticMesh wrapper)
     * @param Device RHI device for GPU buffer creation
     * @param OutLOD LOD resources to fill
     * @return true if successful
     */
    bool BuildLODResources(IRHIDevice* Device, FStaticMeshLODResources& OutLOD);
    
    // ========================================================================
    // Utility
    // ========================================================================
    
    /**
     * Clear all data
     */
    void Clear();
    
    /**
     * Check if the builder has valid data
     * @return true if vertices and indices exist
     */
    bool IsValid() const;
    
    /**
     * Get the builder settings
     * @return Reference to settings
     */
    FMeshBuilderSettings& GetSettings() { return Settings; }
    
    /**
     * Get the builder settings (const)
     * @return Const reference to settings
     */
    const FMeshBuilderSettings& GetSettings() const { return Settings; }

private:
    // ========================================================================
    // Internal Data
    // ========================================================================
    
    /** Builder settings */
    FMeshBuilderSettings Settings;
    
    /** Accumulated vertices */
    TArray<FStaticMeshBuildVertex> Vertices;
    
    /** Accumulated indices */
    TArray<uint32> Indices;
    
    /** Material index per triangle */
    TArray<int32> TriangleMaterials;
    
    /** Material names */
    TArray<String> MaterialNames;
    
    /** Whether normals have been computed */
    bool bNormalsComputed;
    
    /** Whether tangents have been computed */
    bool bTangentsComputed;
    
    // ========================================================================
    // Internal Methods
    // ========================================================================
    
    /**
     * Build mesh sections from triangle materials
     * @param OutSections Output sections array
     */
    void BuildSections(TArray<FMeshSection>& OutSections);
    
    /**
     * Create position vertex buffer
     * @param Device RHI device
     * @return Created buffer
     */
    FRHIVertexBufferRef CreatePositionBuffer(IRHIDevice* Device);
    
    /**
     * Create tangent vertex buffer
     * @param Device RHI device
     * @return Created buffer
     */
    FRHIVertexBufferRef CreateTangentBuffer(IRHIDevice* Device);
    
    /**
     * Create texcoord vertex buffer
     * @param Device RHI device
     * @return Created buffer
     */
    FRHIVertexBufferRef CreateTexCoordBuffer(IRHIDevice* Device);
    
    /**
     * Create color vertex buffer
     * @param Device RHI device
     * @return Created buffer
     */
    FRHIVertexBufferRef CreateColorBuffer(IRHIDevice* Device);
    
    /**
     * Create index buffer
     * @param Device RHI device
     * @param bUse32Bit Output whether 32-bit indices are used
     * @return Created buffer
     */
    FRHIIndexBufferRef CreateIndexBuffer(IRHIDevice* Device, bool& bUse32Bit);
};

// ============================================================================
// Primitive Mesh Generators
// ============================================================================

/**
 * @namespace MeshPrimitives
 * @brief Factory functions for creating common primitive meshes
 */
namespace MeshPrimitives
{
    /**
     * Create a box mesh
     * @param Builder Mesh builder to add geometry to
     * @param HalfExtent Half-size of the box
     * @param MaterialIndex Material index
     */
    void CreateBox(
        FMeshBuilder& Builder,
        const Math::FVector3f& HalfExtent,
        int32 MaterialIndex = 0);
    
    /**
     * Create a sphere mesh
     * @param Builder Mesh builder to add geometry to
     * @param Radius Sphere radius
     * @param Segments Number of horizontal segments
     * @param Rings Number of vertical rings
     * @param MaterialIndex Material index
     */
    void CreateSphere(
        FMeshBuilder& Builder,
        float Radius,
        int32 Segments = 32,
        int32 Rings = 16,
        int32 MaterialIndex = 0);
    
    /**
     * Create a cylinder mesh
     * @param Builder Mesh builder to add geometry to
     * @param Radius Cylinder radius
     * @param Height Cylinder height
     * @param Segments Number of radial segments
     * @param MaterialIndex Material index
     */
    void CreateCylinder(
        FMeshBuilder& Builder,
        float Radius,
        float Height,
        int32 Segments = 32,
        int32 MaterialIndex = 0);
    
    /**
     * Create a cone mesh
     * @param Builder Mesh builder to add geometry to
     * @param Radius Base radius
     * @param Height Cone height
     * @param Segments Number of radial segments
     * @param MaterialIndex Material index
     */
    void CreateCone(
        FMeshBuilder& Builder,
        float Radius,
        float Height,
        int32 Segments = 32,
        int32 MaterialIndex = 0);
    
    /**
     * Create a plane mesh
     * @param Builder Mesh builder to add geometry to
     * @param Width Plane width
     * @param Height Plane height
     * @param WidthSegments Number of width segments
     * @param HeightSegments Number of height segments
     * @param MaterialIndex Material index
     */
    void CreatePlane(
        FMeshBuilder& Builder,
        float Width,
        float Height,
        int32 WidthSegments = 1,
        int32 HeightSegments = 1,
        int32 MaterialIndex = 0);
    
    /**
     * Create a torus mesh
     * @param Builder Mesh builder to add geometry to
     * @param OuterRadius Outer radius (center to tube center)
     * @param InnerRadius Inner radius (tube radius)
     * @param Segments Number of radial segments
     * @param Sides Number of tube sides
     * @param MaterialIndex Material index
     */
    void CreateTorus(
        FMeshBuilder& Builder,
        float OuterRadius,
        float InnerRadius,
        int32 Segments = 32,
        int32 Sides = 16,
        int32 MaterialIndex = 0);
}

} // namespace MonsterEngine
