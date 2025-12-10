// Copyright Monster Engine. All Rights Reserved.
// Reference UE5: Engine/Source/Runtime/Engine/Private/DynamicMeshBuilder.cpp

/**
 * @file MeshBuilder.cpp
 * @brief Implementation of FMeshBuilder and primitive mesh generators
 */

#include "Engine/Mesh/MeshBuilder.h"
#include "RHI/IRHIDevice.h"
#include "RHI/RHIResources.h"
#include "RHI/RHIDefinitions.h"
#include "Core/Logging/LogMacros.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace MonsterEngine
{

// Use MonsterRender::RHI types
using MonsterRender::RHI::FRHIVertexBufferRef;
using MonsterRender::RHI::FRHIIndexBufferRef;
using MonsterRender::RHI::IRHIDevice;

// Define log category for mesh builder operations
DEFINE_LOG_CATEGORY_STATIC(LogMeshBuilder, Log, All);

// ============================================================================
// Constants
// ============================================================================

/** Pi constant for mesh generation */
constexpr float PI = 3.14159265358979323846f;

/** Two Pi constant */
constexpr float TWO_PI = 2.0f * PI;

// ============================================================================
// FMeshBuilder Implementation
// ============================================================================

/**
 * Default constructor
 */
FMeshBuilder::FMeshBuilder()
    : bNormalsComputed(false)
    , bTangentsComputed(false)
{
    // Initialize with one default material
    MaterialNames.Add("DefaultMaterial");
}

/**
 * Constructor with settings
 * @param InSettings Builder settings
 */
FMeshBuilder::FMeshBuilder(const FMeshBuilderSettings& InSettings)
    : Settings(InSettings)
    , bNormalsComputed(false)
    , bTangentsComputed(false)
{
    MaterialNames.Add("DefaultMaterial");
}

/**
 * Destructor
 */
FMeshBuilder::~FMeshBuilder()
{
    Clear();
}

// ============================================================================
// Vertex Operations
// ============================================================================

/**
 * Add a vertex with position only
 */
int32 FMeshBuilder::AddVertex(const Math::FVector3f& Position)
{
    FStaticMeshBuildVertex Vertex(Position);
    return AddVertex(Vertex);
}

/**
 * Add a vertex with position and UV
 */
int32 FMeshBuilder::AddVertex(const Math::FVector3f& Position, const Math::FVector2f& UV)
{
    FStaticMeshBuildVertex Vertex(Position, UV);
    return AddVertex(Vertex);
}

/**
 * Add a vertex with position, UV, and normal
 */
int32 FMeshBuilder::AddVertex(
    const Math::FVector3f& Position,
    const Math::FVector2f& UV,
    const Math::FVector3f& Normal)
{
    FStaticMeshBuildVertex Vertex;
    Vertex.Position = Position;
    Vertex.UVs[0] = UV;
    Vertex.TangentZ = Normal;
    Vertex.SetTangentBasisFromNormal(Normal);
    
    return AddVertex(Vertex);
}

/**
 * Add a vertex with full data
 */
int32 FMeshBuilder::AddVertex(
    const Math::FVector3f& Position,
    const Math::FVector3f& Normal,
    const Math::FVector3f& Tangent,
    const Math::FVector2f& UV,
    const FColor& Color)
{
    FStaticMeshBuildVertex Vertex;
    Vertex.Position = Position;
    Vertex.TangentZ = Normal;
    Vertex.TangentX = Tangent;
    Vertex.ComputeBinormal();
    Vertex.UVs[0] = UV;
    Vertex.Color = Color;
    
    return AddVertex(Vertex);
}

/**
 * Add a vertex from FStaticMeshBuildVertex
 */
int32 FMeshBuilder::AddVertex(const FStaticMeshBuildVertex& Vertex)
{
    int32 Index = Vertices.Num();
    Vertices.Add(Vertex);
    
    // Reset computed flags when new vertices are added
    bNormalsComputed = false;
    bTangentsComputed = false;
    
    return Index;
}

/**
 * Add multiple vertices
 */
int32 FMeshBuilder::AddVertices(const TArray<FStaticMeshBuildVertex>& InVertices)
{
    int32 FirstIndex = Vertices.Num();
    
    for (const FStaticMeshBuildVertex& Vertex : InVertices)
    {
        Vertices.Add(Vertex);
    }
    
    bNormalsComputed = false;
    bTangentsComputed = false;
    
    return FirstIndex;
}

/**
 * Set UV for a specific vertex and channel
 */
void FMeshBuilder::SetVertexUV(int32 VertexIndex, uint32 UVChannel, const Math::FVector2f& UV)
{
    if (VertexIndex >= 0 && VertexIndex < Vertices.Num() && UVChannel < MAX_STATIC_MESH_TEXCOORDS)
    {
        Vertices[VertexIndex].UVs[UVChannel] = UV;
    }
}

/**
 * Set color for a specific vertex
 */
void FMeshBuilder::SetVertexColor(int32 VertexIndex, const FColor& Color)
{
    if (VertexIndex >= 0 && VertexIndex < Vertices.Num())
    {
        Vertices[VertexIndex].Color = Color;
    }
}

/**
 * Reserve space for vertices
 */
void FMeshBuilder::ReserveVertices(int32 NumVertices)
{
    Vertices.Reserve(Vertices.Num() + NumVertices);
}

// ============================================================================
// Index / Triangle Operations
// ============================================================================

/**
 * Add a triangle by vertex indices
 */
void FMeshBuilder::AddTriangle(int32 V0, int32 V1, int32 V2, int32 MaterialIndex)
{
    // Validate indices
    int32 NumVerts = Vertices.Num();
    if (V0 < 0 || V0 >= NumVerts ||
        V1 < 0 || V1 >= NumVerts ||
        V2 < 0 || V2 >= NumVerts)
    {
        MR_LOG(LogMeshBuilder, Warning, "AddTriangle: Invalid vertex indices (%d, %d, %d), NumVertices=%d",
               V0, V1, V2, NumVerts);
        return;
    }
    
    // Add indices
    Indices.Add(static_cast<uint32>(V0));
    Indices.Add(static_cast<uint32>(V1));
    Indices.Add(static_cast<uint32>(V2));
    
    // Track material for this triangle
    TriangleMaterials.Add(MaterialIndex);
    
    // Ensure material slot exists
    while (MaterialIndex >= MaterialNames.Num())
    {
        MaterialNames.Add("Material_" + std::to_string(MaterialNames.Num()));
    }
}

/**
 * Add multiple triangles from index array
 */
void FMeshBuilder::AddTriangles(const TArray<uint32>& InIndices, int32 MaterialIndex)
{
    // Must be multiple of 3
    if (InIndices.Num() % 3 != 0)
    {
        MR_LOG(LogMeshBuilder, Warning, "AddTriangles: Index count (%d) is not a multiple of 3",
               InIndices.Num());
        return;
    }
    
    int32 NumTriangles = InIndices.Num() / 3;
    
    for (int32 i = 0; i < InIndices.Num(); i += 3)
    {
        AddTriangle(InIndices[i], InIndices[i + 1], InIndices[i + 2], MaterialIndex);
    }
}

/**
 * Add a quad (two triangles)
 */
void FMeshBuilder::AddQuad(int32 V0, int32 V1, int32 V2, int32 V3, int32 MaterialIndex)
{
    // First triangle: V0, V1, V2
    AddTriangle(V0, V1, V2, MaterialIndex);
    
    // Second triangle: V0, V2, V3
    AddTriangle(V0, V2, V3, MaterialIndex);
}

/**
 * Reserve space for triangles
 */
void FMeshBuilder::ReserveTriangles(int32 NumTriangles)
{
    Indices.Reserve(Indices.Num() + NumTriangles * 3);
    TriangleMaterials.Reserve(TriangleMaterials.Num() + NumTriangles);
}

// ============================================================================
// Material Operations
// ============================================================================

/**
 * Set the number of material slots
 */
void FMeshBuilder::SetNumMaterials(int32 NumMaterials)
{
    MaterialNames.SetNum(NumMaterials);
    for (int32 i = 0; i < NumMaterials; ++i)
    {
        if (MaterialNames[i].empty())
        {
            MaterialNames[i] = "Material_" + std::to_string(i);
        }
    }
}

/**
 * Set material name
 */
void FMeshBuilder::SetMaterialName(int32 MaterialIndex, const String& Name)
{
    if (MaterialIndex >= 0 && MaterialIndex < MaterialNames.Num())
    {
        MaterialNames[MaterialIndex] = Name;
    }
}

// ============================================================================
// Normal and Tangent Computation
// ============================================================================

/**
 * Compute normals for all vertices
 */
void FMeshBuilder::ComputeNormals()
{
    if (Vertices.Num() == 0 || Indices.Num() == 0)
    {
        return;
    }
    
    MR_LOG(LogMeshBuilder, Verbose, "Computing normals for %d vertices, %d triangles",
           Vertices.Num(), GetNumTriangles());
    
    // Initialize all normals to zero
    for (FStaticMeshBuildVertex& Vertex : Vertices)
    {
        Vertex.TangentZ = Math::FVector3f(0.0f, 0.0f, 0.0f);
    }
    
    // Accumulate face normals at each vertex
    for (int32 i = 0; i < Indices.Num(); i += 3)
    {
        uint32 I0 = Indices[i];
        uint32 I1 = Indices[i + 1];
        uint32 I2 = Indices[i + 2];
        
        // Get vertex positions
        const Math::FVector3f& P0 = Vertices[I0].Position;
        const Math::FVector3f& P1 = Vertices[I1].Position;
        const Math::FVector3f& P2 = Vertices[I2].Position;
        
        // Calculate face normal using cross product
        // Edge vectors
        Math::FVector3f E1(P1.X - P0.X, P1.Y - P0.Y, P1.Z - P0.Z);
        Math::FVector3f E2(P2.X - P0.X, P2.Y - P0.Y, P2.Z - P0.Z);
        
        // Cross product: E1 x E2
        Math::FVector3f FaceNormal(
            E1.Y * E2.Z - E1.Z * E2.Y,
            E1.Z * E2.X - E1.X * E2.Z,
            E1.X * E2.Y - E1.Y * E2.X
        );
        
        // Accumulate to each vertex (for smooth normals)
        if (Settings.bUseSmoothNormals)
        {
            Vertices[I0].TangentZ.X += FaceNormal.X;
            Vertices[I0].TangentZ.Y += FaceNormal.Y;
            Vertices[I0].TangentZ.Z += FaceNormal.Z;
            
            Vertices[I1].TangentZ.X += FaceNormal.X;
            Vertices[I1].TangentZ.Y += FaceNormal.Y;
            Vertices[I1].TangentZ.Z += FaceNormal.Z;
            
            Vertices[I2].TangentZ.X += FaceNormal.X;
            Vertices[I2].TangentZ.Y += FaceNormal.Y;
            Vertices[I2].TangentZ.Z += FaceNormal.Z;
        }
        else
        {
            // Flat shading - normalize and assign directly
            float Length = std::sqrt(FaceNormal.X * FaceNormal.X + 
                                    FaceNormal.Y * FaceNormal.Y + 
                                    FaceNormal.Z * FaceNormal.Z);
            if (Length > 0.0001f)
            {
                FaceNormal.X /= Length;
                FaceNormal.Y /= Length;
                FaceNormal.Z /= Length;
            }
            
            Vertices[I0].TangentZ = FaceNormal;
            Vertices[I1].TangentZ = FaceNormal;
            Vertices[I2].TangentZ = FaceNormal;
        }
    }
    
    // Normalize all vertex normals (for smooth normals)
    if (Settings.bUseSmoothNormals)
    {
        for (FStaticMeshBuildVertex& Vertex : Vertices)
        {
            float Length = std::sqrt(
                Vertex.TangentZ.X * Vertex.TangentZ.X +
                Vertex.TangentZ.Y * Vertex.TangentZ.Y +
                Vertex.TangentZ.Z * Vertex.TangentZ.Z
            );
            
            if (Length > 0.0001f)
            {
                Vertex.TangentZ.X /= Length;
                Vertex.TangentZ.Y /= Length;
                Vertex.TangentZ.Z /= Length;
            }
            else
            {
                // Default to up vector if degenerate
                Vertex.TangentZ = Math::FVector3f(0.0f, 0.0f, 1.0f);
            }
        }
    }
    
    bNormalsComputed = true;
    MR_LOG(LogMeshBuilder, Verbose, "Normals computed successfully");
}

/**
 * Compute flat normals (one normal per face, not smoothed)
 */
void FMeshBuilder::ComputeFlatNormals()
{
    // Temporarily disable smooth normals
    bool bOldSmooth = Settings.bUseSmoothNormals;
    Settings.bUseSmoothNormals = false;
    
    ComputeNormals();
    
    Settings.bUseSmoothNormals = bOldSmooth;
}

/**
 * Compute tangents for all vertices
 */
void FMeshBuilder::ComputeTangents()
{
    if (Vertices.Num() == 0 || Indices.Num() == 0)
    {
        return;
    }
    
    // Ensure normals are computed first
    if (!bNormalsComputed && Settings.bComputeNormals)
    {
        ComputeNormals();
    }
    
    MR_LOG(LogMeshBuilder, Verbose, "Computing tangents for %d vertices", Vertices.Num());
    
    // Initialize tangents and binormals to zero
    TArray<Math::FVector3f> Tangents;
    TArray<Math::FVector3f> Binormals;
    Tangents.SetNum(Vertices.Num());
    Binormals.SetNum(Vertices.Num());
    
    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        Tangents[i] = Math::FVector3f(0.0f, 0.0f, 0.0f);
        Binormals[i] = Math::FVector3f(0.0f, 0.0f, 0.0f);
    }
    
    // Calculate tangent and binormal for each triangle
    for (int32 i = 0; i < Indices.Num(); i += 3)
    {
        uint32 I0 = Indices[i];
        uint32 I1 = Indices[i + 1];
        uint32 I2 = Indices[i + 2];
        
        // Get positions
        const Math::FVector3f& P0 = Vertices[I0].Position;
        const Math::FVector3f& P1 = Vertices[I1].Position;
        const Math::FVector3f& P2 = Vertices[I2].Position;
        
        // Get UVs
        const Math::FVector2f& UV0 = Vertices[I0].UVs[0];
        const Math::FVector2f& UV1 = Vertices[I1].UVs[0];
        const Math::FVector2f& UV2 = Vertices[I2].UVs[0];
        
        // Calculate edge vectors
        Math::FVector3f E1(P1.X - P0.X, P1.Y - P0.Y, P1.Z - P0.Z);
        Math::FVector3f E2(P2.X - P0.X, P2.Y - P0.Y, P2.Z - P0.Z);
        
        // Calculate UV deltas
        float DeltaU1 = UV1.X - UV0.X;
        float DeltaV1 = UV1.Y - UV0.Y;
        float DeltaU2 = UV2.X - UV0.X;
        float DeltaV2 = UV2.Y - UV0.Y;
        
        // Calculate tangent and binormal
        float Denom = DeltaU1 * DeltaV2 - DeltaU2 * DeltaV1;
        
        Math::FVector3f Tangent, Binormal;
        
        if (std::abs(Denom) > 0.0001f)
        {
            float InvDenom = 1.0f / Denom;
            
            Tangent = Math::FVector3f(
                (DeltaV2 * E1.X - DeltaV1 * E2.X) * InvDenom,
                (DeltaV2 * E1.Y - DeltaV1 * E2.Y) * InvDenom,
                (DeltaV2 * E1.Z - DeltaV1 * E2.Z) * InvDenom
            );
            
            Binormal = Math::FVector3f(
                (-DeltaU2 * E1.X + DeltaU1 * E2.X) * InvDenom,
                (-DeltaU2 * E1.Y + DeltaU1 * E2.Y) * InvDenom,
                (-DeltaU2 * E1.Z + DeltaU1 * E2.Z) * InvDenom
            );
        }
        else
        {
            // Degenerate UV mapping, use default tangent
            Tangent = Math::FVector3f(1.0f, 0.0f, 0.0f);
            Binormal = Math::FVector3f(0.0f, 1.0f, 0.0f);
        }
        
        // Accumulate to vertices
        Tangents[I0].X += Tangent.X; Tangents[I0].Y += Tangent.Y; Tangents[I0].Z += Tangent.Z;
        Tangents[I1].X += Tangent.X; Tangents[I1].Y += Tangent.Y; Tangents[I1].Z += Tangent.Z;
        Tangents[I2].X += Tangent.X; Tangents[I2].Y += Tangent.Y; Tangents[I2].Z += Tangent.Z;
        
        Binormals[I0].X += Binormal.X; Binormals[I0].Y += Binormal.Y; Binormals[I0].Z += Binormal.Z;
        Binormals[I1].X += Binormal.X; Binormals[I1].Y += Binormal.Y; Binormals[I1].Z += Binormal.Z;
        Binormals[I2].X += Binormal.X; Binormals[I2].Y += Binormal.Y; Binormals[I2].Z += Binormal.Z;
    }
    
    // Orthonormalize and assign to vertices
    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        FStaticMeshBuildVertex& Vertex = Vertices[i];
        const Math::FVector3f& N = Vertex.TangentZ;
        Math::FVector3f& T = Tangents[i];
        Math::FVector3f& B = Binormals[i];
        
        // Gram-Schmidt orthogonalization: T = T - (N . T) * N
        float NdotT = N.X * T.X + N.Y * T.Y + N.Z * T.Z;
        T.X -= NdotT * N.X;
        T.Y -= NdotT * N.Y;
        T.Z -= NdotT * N.Z;
        
        // Normalize tangent
        float TLength = std::sqrt(T.X * T.X + T.Y * T.Y + T.Z * T.Z);
        if (TLength > 0.0001f)
        {
            T.X /= TLength;
            T.Y /= TLength;
            T.Z /= TLength;
        }
        else
        {
            // Generate arbitrary tangent perpendicular to normal
            Vertex.SetTangentBasisFromNormal(N);
            continue;
        }
        
        // Calculate binormal sign
        // Cross product: N x T
        Math::FVector3f CrossNT(
            N.Y * T.Z - N.Z * T.Y,
            N.Z * T.X - N.X * T.Z,
            N.X * T.Y - N.Y * T.X
        );
        
        // Dot with accumulated binormal to determine sign
        float BSign = (CrossNT.X * B.X + CrossNT.Y * B.Y + CrossNT.Z * B.Z) < 0.0f ? -1.0f : 1.0f;
        
        // Assign to vertex
        Vertex.TangentX = T;
        Vertex.TangentY = Math::FVector3f(CrossNT.X * BSign, CrossNT.Y * BSign, CrossNT.Z * BSign);
    }
    
    bTangentsComputed = true;
    MR_LOG(LogMeshBuilder, Verbose, "Tangents computed successfully");
}

/**
 * Compute tangents using MikkTSpace algorithm
 * For now, falls back to standard tangent computation
 */
void FMeshBuilder::ComputeTangentsMikkTSpace()
{
    // TODO: Implement MikkTSpace tangent calculation
    // For now, use standard method
    MR_LOG(LogMeshBuilder, Warning, "MikkTSpace not yet implemented, using standard tangent calculation");
    ComputeTangents();
}

// ============================================================================
// Bounds Computation
// ============================================================================

/**
 * Compute bounding box from vertices
 */
Math::FBox3f FMeshBuilder::ComputeBounds() const
{
    if (Vertices.Num() == 0)
    {
        return Math::FBox3f(
            Math::FVector3f(0.0f, 0.0f, 0.0f),
            Math::FVector3f(0.0f, 0.0f, 0.0f)
        );
    }
    
    // Initialize with first vertex
    Math::FVector3f Min = Vertices[0].Position;
    Math::FVector3f Max = Vertices[0].Position;
    
    // Expand to include all vertices
    for (int32 i = 1; i < Vertices.Num(); ++i)
    {
        const Math::FVector3f& P = Vertices[i].Position;
        
        Min.X = std::min(Min.X, P.X);
        Min.Y = std::min(Min.Y, P.Y);
        Min.Z = std::min(Min.Z, P.Z);
        
        Max.X = std::max(Max.X, P.X);
        Max.Y = std::max(Max.Y, P.Y);
        Max.Z = std::max(Max.Z, P.Z);
    }
    
    return Math::FBox3f(Min, Max);
}

// ============================================================================
// Building
// ============================================================================

/**
 * Build a static mesh from the accumulated data
 */
FStaticMesh* FMeshBuilder::Build(IRHIDevice* Device, const String& MeshName)
{
    if (!IsValid())
    {
        MR_LOG(LogMeshBuilder, Error, "Cannot build mesh '%s': invalid data", MeshName.c_str());
        return nullptr;
    }
    
    // Create new mesh
    FStaticMesh* Mesh = new FStaticMesh(MeshName);
    
    // Build into the mesh
    if (!BuildInto(Device, *Mesh))
    {
        delete Mesh;
        return nullptr;
    }
    
    MR_LOG(LogMeshBuilder, Log, "Built mesh '%s': %d vertices, %d triangles, %d materials",
           MeshName.c_str(), Vertices.Num(), GetNumTriangles(), MaterialNames.Num());
    
    return Mesh;
}

/**
 * Build into an existing static mesh
 */
bool FMeshBuilder::BuildInto(IRHIDevice* Device, FStaticMesh& OutMesh)
{
    if (!IsValid())
    {
        MR_LOG(LogMeshBuilder, Error, "Cannot build mesh: invalid data");
        return false;
    }
    
    // Compute normals and tangents if needed
    if (Settings.bComputeNormals && !bNormalsComputed)
    {
        ComputeNormals();
    }
    
    if (Settings.bComputeTangents && !bTangentsComputed)
    {
        if (Settings.bUseMikkTSpace)
        {
            ComputeTangentsMikkTSpace();
        }
        else
        {
            ComputeTangents();
        }
    }
    
    // Allocate render data
    FStaticMeshRenderData& RenderData = OutMesh.AllocateRenderData();
    RenderData.AllocateLODResources(1); // Single LOD for now
    
    // Build LOD 0
    FStaticMeshLODResources& LOD = RenderData.GetLODResources(0);
    
    if (!BuildLODResources(Device, LOD))
    {
        return false;
    }
    
    // Set bounds
    RenderData.Bounds = ComputeBounds();
    Math::FVector3f Extent = RenderData.Bounds.GetExtent();
    RenderData.BoundingSphereRadius = std::sqrt(
        Extent.X * Extent.X + Extent.Y * Extent.Y + Extent.Z * Extent.Z
    );
    
    OutMesh.SetBounds(RenderData.Bounds);
    
    // Add materials
    for (const String& MatName : MaterialNames)
    {
        OutMesh.AddMaterial(FStaticMaterial(MatName));
    }
    
    return true;
}

/**
 * Build LOD resources only
 */
bool FMeshBuilder::BuildLODResources(IRHIDevice* Device, FStaticMeshLODResources& OutLOD)
{
    if (!Device)
    {
        MR_LOG(LogMeshBuilder, Error, "Cannot build LOD resources: null device");
        return false;
    }
    
    // Build sections
    BuildSections(OutLOD.Sections);
    
    // Create vertex buffers
    OutLOD.VertexBuffers.PositionVertexBuffer = CreatePositionBuffer(Device);
    OutLOD.VertexBuffers.TangentVertexBuffer = CreateTangentBuffer(Device);
    OutLOD.VertexBuffers.TexCoordVertexBuffer = CreateTexCoordBuffer(Device);
    
    if (Settings.bGenerateVertexColors)
    {
        OutLOD.VertexBuffers.ColorVertexBuffer = CreateColorBuffer(Device);
        OutLOD.VertexBuffers.bHasVertexColors = true;
    }
    
    // Set vertex buffer properties
    OutLOD.VertexBuffers.NumVertices = static_cast<uint32>(Vertices.Num());
    OutLOD.VertexBuffers.NumTexCoords = Settings.NumTexCoords;
    OutLOD.VertexBuffers.bUseHighPrecisionTangentBasis = Settings.bUseHighPrecisionTangentBasis;
    OutLOD.VertexBuffers.bUseFullPrecisionUVs = Settings.bUseFullPrecisionUVs;
    
    // Create index buffer
    OutLOD.IndexBuffer = CreateIndexBuffer(Device, OutLOD.bUse32BitIndices);
    
    MR_LOG(LogMeshBuilder, Verbose, "Built LOD resources: %d vertices, %d indices, %d sections",
           OutLOD.GetNumVertices(), OutLOD.GetNumIndices(), OutLOD.GetNumSections());
    
    return OutLOD.IsValid();
}

// ============================================================================
// Internal Methods
// ============================================================================

/**
 * Build mesh sections from triangle materials
 */
void FMeshBuilder::BuildSections(TArray<FMeshSection>& OutSections)
{
    OutSections.Empty();
    
    if (Indices.Num() == 0)
    {
        return;
    }
    
    // Group triangles by material
    // For simplicity, we'll create one section per material used
    
    // Find unique materials and their triangle ranges
    TArray<int32> MaterialTriangleCounts;
    MaterialTriangleCounts.SetNum(MaterialNames.Num());
    for (int32 i = 0; i < MaterialNames.Num(); ++i)
    {
        MaterialTriangleCounts[i] = 0;
    }
    
    // Count triangles per material
    for (int32 MatIndex : TriangleMaterials)
    {
        if (MatIndex >= 0 && MatIndex < MaterialTriangleCounts.Num())
        {
            MaterialTriangleCounts[MatIndex]++;
        }
    }
    
    // Create sections for materials that have triangles
    uint32 CurrentIndex = 0;
    for (int32 MatIndex = 0; MatIndex < MaterialNames.Num(); ++MatIndex)
    {
        int32 TriCount = MaterialTriangleCounts[MatIndex];
        if (TriCount > 0)
        {
            FMeshSection Section;
            Section.MaterialIndex = MatIndex;
            Section.FirstIndex = CurrentIndex;
            Section.NumTriangles = TriCount;
            Section.MinVertexIndex = 0;
            Section.MaxVertexIndex = static_cast<uint32>(Vertices.Num() - 1);
            
            OutSections.Add(Section);
            CurrentIndex += TriCount * 3;
        }
    }
    
    // If no sections were created, create a default one
    if (OutSections.Num() == 0 && Indices.Num() > 0)
    {
        FMeshSection Section;
        Section.MaterialIndex = 0;
        Section.FirstIndex = 0;
        Section.NumTriangles = Indices.Num() / 3;
        Section.MinVertexIndex = 0;
        Section.MaxVertexIndex = static_cast<uint32>(Vertices.Num() - 1);
        
        OutSections.Add(Section);
    }
}

/**
 * Create position vertex buffer
 * 
 * Creates a GPU buffer containing vertex positions.
 * Reference UE5: FPositionVertexBuffer::InitRHI
 */
FRHIVertexBufferRef FMeshBuilder::CreatePositionBuffer(IRHIDevice* Device)
{
    if (!Device) {
        MR_LOG(LogMeshBuilder, Error, "CreatePositionBuffer: Device is null");
        return nullptr;
    }
    
    uint32 NumVerts = static_cast<uint32>(Vertices.Num());
    if (NumVerts == 0) {
        MR_LOG(LogMeshBuilder, Warning, "CreatePositionBuffer: No vertices to upload");
        return nullptr;
    }
    
    // Calculate buffer size
    uint32 Stride = sizeof(Math::FVector3f);
    uint32 Size = NumVerts * Stride;
    
    // Prepare position data
    TArray<Math::FVector3f> PositionData;
    PositionData.Reserve(NumVerts);
    for (uint32 i = 0; i < NumVerts; ++i) {
        PositionData.Add(Vertices[i].Position);
    }
    
    // Create resource info with initial data
    MonsterRender::RHI::FRHIResourceCreateInfo CreateInfo(
        "PositionVertexBuffer",
        PositionData.GetData(),
        Size
    );
    
    // Create vertex buffer using new RHI API
    auto Buffer = Device->CreateVertexBuffer(
        Size,
        MonsterRender::RHI::EBufferUsageFlags::Static | MonsterRender::RHI::EBufferUsageFlags::VertexBuffer,
        CreateInfo
    );
    
    if (!Buffer) {
        MR_LOG(LogMeshBuilder, Error, "Failed to create position vertex buffer");
        return nullptr;
    }
    
    MR_LOG(LogMeshBuilder, Verbose, "Created position buffer: %u vertices, %u bytes", NumVerts, Size);
    
    // Convert TSharedPtr to TRefCountPtr
    // Note: This requires the buffer to use reference counting
    return FRHIVertexBufferRef(Buffer.Get());
}

/**
 * Create tangent vertex buffer
 * 
 * Creates a GPU buffer containing tangent basis (normal + tangent).
 * Reference UE5: FStaticMeshVertexBuffer::InitRHI
 */
FRHIVertexBufferRef FMeshBuilder::CreateTangentBuffer(IRHIDevice* Device)
{
    if (!Device) {
        MR_LOG(LogMeshBuilder, Error, "CreateTangentBuffer: Device is null");
        return nullptr;
    }
    
    uint32 NumVerts = static_cast<uint32>(Vertices.Num());
    if (NumVerts == 0) {
        MR_LOG(LogMeshBuilder, Warning, "CreateTangentBuffer: No vertices to upload");
        return nullptr;
    }
    
    // Calculate buffer size based on precision setting
    uint32 Stride = Settings.bUseHighPrecisionTangentBasis 
        ? sizeof(FStaticMeshVertexTangentHighPrecision)
        : sizeof(FStaticMeshVertexTangent);
    uint32 Size = NumVerts * Stride;
    
    // Prepare tangent data
    TArray<uint8> TangentData;
    TangentData.SetNum(Size);
    
    if (Settings.bUseHighPrecisionTangentBasis) {
        FStaticMeshVertexTangentHighPrecision* Tangents = 
            reinterpret_cast<FStaticMeshVertexTangentHighPrecision*>(TangentData.GetData());
        for (uint32 i = 0; i < NumVerts; ++i) {
            const FStaticMeshBuildVertex& V = Vertices[i];
            Tangents[i].SetTangents(V.TangentX, V.TangentY, V.TangentZ);
        }
    } else {
        FStaticMeshVertexTangent* Tangents = 
            reinterpret_cast<FStaticMeshVertexTangent*>(TangentData.GetData());
        for (uint32 i = 0; i < NumVerts; ++i) {
            const FStaticMeshBuildVertex& V = Vertices[i];
            Tangents[i].SetTangents(V.TangentX, V.TangentY, V.TangentZ);
        }
    }
    
    // Create resource info with initial data
    MonsterRender::RHI::FRHIResourceCreateInfo CreateInfo(
        "TangentVertexBuffer",
        TangentData.GetData(),
        Size
    );
    
    // Create vertex buffer
    auto Buffer = Device->CreateVertexBuffer(
        Size,
        MonsterRender::RHI::EBufferUsageFlags::Static | MonsterRender::RHI::EBufferUsageFlags::VertexBuffer,
        CreateInfo
    );
    
    if (!Buffer) {
        MR_LOG(LogMeshBuilder, Error, "Failed to create tangent vertex buffer");
        return nullptr;
    }
    
    MR_LOG(LogMeshBuilder, Verbose, "Created tangent buffer: %u vertices, %u bytes, %s precision",
           NumVerts, Size, Settings.bUseHighPrecisionTangentBasis ? "high" : "normal");
    
    return FRHIVertexBufferRef(Buffer.Get());
}

/**
 * Create texcoord vertex buffer
 * 
 * Creates a GPU buffer containing texture coordinates.
 * Reference UE5: FStaticMeshVertexBuffer::InitRHI
 */
FRHIVertexBufferRef FMeshBuilder::CreateTexCoordBuffer(IRHIDevice* Device)
{
    if (!Device) {
        MR_LOG(LogMeshBuilder, Error, "CreateTexCoordBuffer: Device is null");
        return nullptr;
    }
    
    uint32 NumVerts = static_cast<uint32>(Vertices.Num());
    if (NumVerts == 0) {
        MR_LOG(LogMeshBuilder, Warning, "CreateTexCoordBuffer: No vertices to upload");
        return nullptr;
    }
    
    // Calculate buffer size based on precision and number of UV sets
    uint32 UVSize = Settings.bUseFullPrecisionUVs 
        ? sizeof(FStaticMeshVertexUVHighPrecision)
        : sizeof(FStaticMeshVertexUV);
    uint32 Stride = UVSize * Settings.NumTexCoords;
    uint32 Size = NumVerts * Stride;
    
    // Prepare UV data
    TArray<uint8> UVData;
    UVData.SetNum(Size);
    uint8* ByteData = UVData.GetData();
    
    for (uint32 i = 0; i < NumVerts; ++i) {
        const FStaticMeshBuildVertex& V = Vertices[i];
        
        for (uint32 UVIndex = 0; UVIndex < Settings.NumTexCoords; ++UVIndex) {
            if (Settings.bUseFullPrecisionUVs) {
                FStaticMeshVertexUVHighPrecision* UV = 
                    reinterpret_cast<FStaticMeshVertexUVHighPrecision*>(
                        ByteData + i * Stride + UVIndex * sizeof(FStaticMeshVertexUVHighPrecision));
                UV->SetUV(V.UVs[UVIndex]);
            } else {
                FStaticMeshVertexUV* UV = 
                    reinterpret_cast<FStaticMeshVertexUV*>(
                        ByteData + i * Stride + UVIndex * sizeof(FStaticMeshVertexUV));
                UV->SetUV(V.UVs[UVIndex]);
            }
        }
    }
    
    // Create resource info with initial data
    MonsterRender::RHI::FRHIResourceCreateInfo CreateInfo(
        "TexCoordVertexBuffer",
        UVData.GetData(),
        Size
    );
    
    // Create vertex buffer
    auto Buffer = Device->CreateVertexBuffer(
        Size,
        MonsterRender::RHI::EBufferUsageFlags::Static | MonsterRender::RHI::EBufferUsageFlags::VertexBuffer,
        CreateInfo
    );
    
    if (!Buffer) {
        MR_LOG(LogMeshBuilder, Error, "Failed to create texcoord vertex buffer");
        return nullptr;
    }
    
    MR_LOG(LogMeshBuilder, Verbose, "Created texcoord buffer: %u vertices, %u UV sets, %u bytes",
           NumVerts, Settings.NumTexCoords, Size);
    
    return FRHIVertexBufferRef(Buffer.Get());
}

/**
 * Create color vertex buffer
 * 
 * Creates a GPU buffer containing vertex colors.
 * Reference UE5: FColorVertexBuffer::InitRHI
 */
FRHIVertexBufferRef FMeshBuilder::CreateColorBuffer(IRHIDevice* Device)
{
    if (!Device) {
        MR_LOG(LogMeshBuilder, Error, "CreateColorBuffer: Device is null");
        return nullptr;
    }
    
    uint32 NumVerts = static_cast<uint32>(Vertices.Num());
    if (NumVerts == 0) {
        MR_LOG(LogMeshBuilder, Warning, "CreateColorBuffer: No vertices to upload");
        return nullptr;
    }
    
    // Check if any vertex has non-white color
    bool bHasColors = false;
    for (uint32 i = 0; i < NumVerts && !bHasColors; ++i) {
        const FColor& C = Vertices[i].Color;
        if (C.R != 255 || C.G != 255 || C.B != 255 || C.A != 255) {
            bHasColors = true;
        }
    }
    
    if (!bHasColors) {
        MR_LOG(LogMeshBuilder, Verbose, "CreateColorBuffer: All vertices are white, skipping color buffer");
        return nullptr;
    }
    
    // Calculate buffer size
    uint32 Stride = sizeof(FColor);
    uint32 Size = NumVerts * Stride;
    
    // Prepare color data
    TArray<FColor> ColorData;
    ColorData.Reserve(NumVerts);
    for (uint32 i = 0; i < NumVerts; ++i) {
        ColorData.Add(Vertices[i].Color);
    }
    
    // Create resource info with initial data
    MonsterRender::RHI::FRHIResourceCreateInfo CreateInfo(
        "ColorVertexBuffer",
        ColorData.GetData(),
        Size
    );
    
    // Create vertex buffer
    auto Buffer = Device->CreateVertexBuffer(
        Size,
        MonsterRender::RHI::EBufferUsageFlags::Static | MonsterRender::RHI::EBufferUsageFlags::VertexBuffer,
        CreateInfo
    );
    
    if (!Buffer) {
        MR_LOG(LogMeshBuilder, Error, "Failed to create color vertex buffer");
        return nullptr;
    }
    
    MR_LOG(LogMeshBuilder, Verbose, "Created color buffer: %u vertices, %u bytes", NumVerts, Size);
    
    return FRHIVertexBufferRef(Buffer.Get());
}

/**
 * Create index buffer
 * 
 * Creates a GPU buffer containing triangle indices.
 * Automatically selects 16-bit or 32-bit indices based on vertex count.
 * Reference UE5: FRawStaticIndexBuffer::InitRHI
 */
FRHIIndexBufferRef FMeshBuilder::CreateIndexBuffer(IRHIDevice* Device, bool& bUse32Bit)
{
    if (!Device) {
        MR_LOG(LogMeshBuilder, Error, "CreateIndexBuffer: Device is null");
        bUse32Bit = false;
        return nullptr;
    }
    
    uint32 NumIndices = static_cast<uint32>(Indices.Num());
    if (NumIndices == 0) {
        MR_LOG(LogMeshBuilder, Warning, "CreateIndexBuffer: No indices to upload");
        bUse32Bit = false;
        return nullptr;
    }
    
    // Determine if we need 32-bit indices
    bUse32Bit = (Vertices.Num() > 65535);
    
    uint32 IndexStride = bUse32Bit ? 4 : 2;
    uint32 Size = NumIndices * IndexStride;
    
    // Prepare index data
    TArray<uint8> IndexData;
    IndexData.SetNum(Size);
    
    if (bUse32Bit) {
        // Copy 32-bit indices directly
        uint32* Dest = reinterpret_cast<uint32*>(IndexData.GetData());
        for (uint32 i = 0; i < NumIndices; ++i) {
            Dest[i] = Indices[i];
        }
    } else {
        // Convert to 16-bit indices
        uint16* Dest = reinterpret_cast<uint16*>(IndexData.GetData());
        for (uint32 i = 0; i < NumIndices; ++i) {
            Dest[i] = static_cast<uint16>(Indices[i]);
        }
    }
    
    // Create resource info with initial data
    MonsterRender::RHI::FRHIResourceCreateInfo CreateInfo(
        "IndexBuffer",
        IndexData.GetData(),
        Size
    );
    
    // Create index buffer
    auto Buffer = Device->CreateIndexBuffer(
        IndexStride,
        Size,
        MonsterRender::RHI::EBufferUsageFlags::Static | MonsterRender::RHI::EBufferUsageFlags::IndexBuffer,
        CreateInfo
    );
    
    if (!Buffer) {
        MR_LOG(LogMeshBuilder, Error, "Failed to create index buffer");
        return nullptr;
    }
    
    MR_LOG(LogMeshBuilder, Verbose, "Created index buffer: %u indices, %s, %u bytes",
           NumIndices, bUse32Bit ? "32-bit" : "16-bit", Size);
    
    return FRHIIndexBufferRef(Buffer.Get());
}

// ============================================================================
// Utility
// ============================================================================

/**
 * Clear all data
 */
void FMeshBuilder::Clear()
{
    Vertices.Empty();
    Indices.Empty();
    TriangleMaterials.Empty();
    MaterialNames.Empty();
    MaterialNames.Add("DefaultMaterial");
    
    bNormalsComputed = false;
    bTangentsComputed = false;
}

/**
 * Check if the builder has valid data
 */
bool FMeshBuilder::IsValid() const
{
    return Vertices.Num() > 0 && Indices.Num() >= 3;
}

// ============================================================================
// Primitive Mesh Generators
// ============================================================================

namespace MeshPrimitives
{

/**
 * Create a box mesh
 */
void CreateBox(
    FMeshBuilder& Builder,
    const Math::FVector3f& HalfExtent,
    int32 MaterialIndex)
{
    // 8 corners of the box
    Math::FVector3f Corners[8] = {
        Math::FVector3f(-HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z), // 0: ---
        Math::FVector3f( HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z), // 1: +--
        Math::FVector3f( HalfExtent.X,  HalfExtent.Y, -HalfExtent.Z), // 2: ++-
        Math::FVector3f(-HalfExtent.X,  HalfExtent.Y, -HalfExtent.Z), // 3: -+-
        Math::FVector3f(-HalfExtent.X, -HalfExtent.Y,  HalfExtent.Z), // 4: --+
        Math::FVector3f( HalfExtent.X, -HalfExtent.Y,  HalfExtent.Z), // 5: +-+
        Math::FVector3f( HalfExtent.X,  HalfExtent.Y,  HalfExtent.Z), // 6: +++
        Math::FVector3f(-HalfExtent.X,  HalfExtent.Y,  HalfExtent.Z)  // 7: -++
    };
    
    // Face normals
    Math::FVector3f Normals[6] = {
        Math::FVector3f( 0,  0, -1), // Front  (Z-)
        Math::FVector3f( 0,  0,  1), // Back   (Z+)
        Math::FVector3f(-1,  0,  0), // Left   (X-)
        Math::FVector3f( 1,  0,  0), // Right  (X+)
        Math::FVector3f( 0, -1,  0), // Bottom (Y-)
        Math::FVector3f( 0,  1,  0)  // Top    (Y+)
    };
    
    // UV coordinates for each corner of a face
    Math::FVector2f UVs[4] = {
        Math::FVector2f(0, 1),
        Math::FVector2f(1, 1),
        Math::FVector2f(1, 0),
        Math::FVector2f(0, 0)
    };
    
    // Face definitions: 4 corner indices per face
    int32 Faces[6][4] = {
        {0, 1, 2, 3}, // Front
        {5, 4, 7, 6}, // Back
        {4, 0, 3, 7}, // Left
        {1, 5, 6, 2}, // Right
        {4, 5, 1, 0}, // Bottom
        {3, 2, 6, 7}  // Top
    };
    
    // Create vertices and triangles for each face
    for (int32 Face = 0; Face < 6; ++Face)
    {
        int32 BaseVertex = Builder.GetNumVertices();
        
        // Add 4 vertices for this face
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            Builder.AddVertex(
                Corners[Faces[Face][Corner]],
                UVs[Corner],
                Normals[Face]
            );
        }
        
        // Add 2 triangles for this face
        Builder.AddTriangle(BaseVertex + 0, BaseVertex + 1, BaseVertex + 2, MaterialIndex);
        Builder.AddTriangle(BaseVertex + 0, BaseVertex + 2, BaseVertex + 3, MaterialIndex);
    }
}

/**
 * Create a sphere mesh
 */
void CreateSphere(
    FMeshBuilder& Builder,
    float Radius,
    int32 Segments,
    int32 Rings,
    int32 MaterialIndex)
{
    // Clamp parameters
    Segments = std::max(3, Segments);
    Rings = std::max(2, Rings);
    
    int32 BaseVertex = Builder.GetNumVertices();
    
    // Generate vertices
    for (int32 Ring = 0; Ring <= Rings; ++Ring)
    {
        float Phi = PI * static_cast<float>(Ring) / static_cast<float>(Rings);
        float SinPhi = std::sin(Phi);
        float CosPhi = std::cos(Phi);
        
        for (int32 Seg = 0; Seg <= Segments; ++Seg)
        {
            float Theta = TWO_PI * static_cast<float>(Seg) / static_cast<float>(Segments);
            float SinTheta = std::sin(Theta);
            float CosTheta = std::cos(Theta);
            
            // Position
            Math::FVector3f Position(
                Radius * SinPhi * CosTheta,
                Radius * SinPhi * SinTheta,
                Radius * CosPhi
            );
            
            // Normal (same as position normalized, which is just position/radius)
            Math::FVector3f Normal(
                SinPhi * CosTheta,
                SinPhi * SinTheta,
                CosPhi
            );
            
            // UV
            Math::FVector2f UV(
                static_cast<float>(Seg) / static_cast<float>(Segments),
                static_cast<float>(Ring) / static_cast<float>(Rings)
            );
            
            Builder.AddVertex(Position, UV, Normal);
        }
    }
    
    // Generate triangles
    int32 VertsPerRing = Segments + 1;
    
    for (int32 Ring = 0; Ring < Rings; ++Ring)
    {
        for (int32 Seg = 0; Seg < Segments; ++Seg)
        {
            int32 Current = BaseVertex + Ring * VertsPerRing + Seg;
            int32 Next = Current + 1;
            int32 CurrentBelow = Current + VertsPerRing;
            int32 NextBelow = Next + VertsPerRing;
            
            // Two triangles per quad
            Builder.AddTriangle(Current, CurrentBelow, Next, MaterialIndex);
            Builder.AddTriangle(Next, CurrentBelow, NextBelow, MaterialIndex);
        }
    }
}

/**
 * Create a cylinder mesh
 */
void CreateCylinder(
    FMeshBuilder& Builder,
    float Radius,
    float Height,
    int32 Segments,
    int32 MaterialIndex)
{
    Segments = std::max(3, Segments);
    
    float HalfHeight = Height * 0.5f;
    int32 BaseVertex = Builder.GetNumVertices();
    
    // Generate side vertices
    for (int32 i = 0; i <= Segments; ++i)
    {
        float Angle = TWO_PI * static_cast<float>(i) / static_cast<float>(Segments);
        float SinA = std::sin(Angle);
        float CosA = std::cos(Angle);
        
        float U = static_cast<float>(i) / static_cast<float>(Segments);
        
        // Bottom vertex
        Builder.AddVertex(
            Math::FVector3f(Radius * CosA, Radius * SinA, -HalfHeight),
            Math::FVector2f(U, 1.0f),
            Math::FVector3f(CosA, SinA, 0.0f)
        );
        
        // Top vertex
        Builder.AddVertex(
            Math::FVector3f(Radius * CosA, Radius * SinA, HalfHeight),
            Math::FVector2f(U, 0.0f),
            Math::FVector3f(CosA, SinA, 0.0f)
        );
    }
    
    // Side triangles
    for (int32 i = 0; i < Segments; ++i)
    {
        int32 Bottom1 = BaseVertex + i * 2;
        int32 Top1 = Bottom1 + 1;
        int32 Bottom2 = Bottom1 + 2;
        int32 Top2 = Top1 + 2;
        
        Builder.AddTriangle(Bottom1, Top1, Bottom2, MaterialIndex);
        Builder.AddTriangle(Bottom2, Top1, Top2, MaterialIndex);
    }
    
    // Top cap
    int32 TopCenterIndex = Builder.GetNumVertices();
    Builder.AddVertex(
        Math::FVector3f(0, 0, HalfHeight),
        Math::FVector2f(0.5f, 0.5f),
        Math::FVector3f(0, 0, 1)
    );
    
    for (int32 i = 0; i <= Segments; ++i)
    {
        float Angle = TWO_PI * static_cast<float>(i) / static_cast<float>(Segments);
        float SinA = std::sin(Angle);
        float CosA = std::cos(Angle);
        
        Builder.AddVertex(
            Math::FVector3f(Radius * CosA, Radius * SinA, HalfHeight),
            Math::FVector2f(0.5f + 0.5f * CosA, 0.5f + 0.5f * SinA),
            Math::FVector3f(0, 0, 1)
        );
    }
    
    for (int32 i = 0; i < Segments; ++i)
    {
        Builder.AddTriangle(TopCenterIndex, TopCenterIndex + 1 + i, TopCenterIndex + 2 + i, MaterialIndex);
    }
    
    // Bottom cap
    int32 BottomCenterIndex = Builder.GetNumVertices();
    Builder.AddVertex(
        Math::FVector3f(0, 0, -HalfHeight),
        Math::FVector2f(0.5f, 0.5f),
        Math::FVector3f(0, 0, -1)
    );
    
    for (int32 i = 0; i <= Segments; ++i)
    {
        float Angle = TWO_PI * static_cast<float>(i) / static_cast<float>(Segments);
        float SinA = std::sin(Angle);
        float CosA = std::cos(Angle);
        
        Builder.AddVertex(
            Math::FVector3f(Radius * CosA, Radius * SinA, -HalfHeight),
            Math::FVector2f(0.5f + 0.5f * CosA, 0.5f - 0.5f * SinA),
            Math::FVector3f(0, 0, -1)
        );
    }
    
    for (int32 i = 0; i < Segments; ++i)
    {
        Builder.AddTriangle(BottomCenterIndex, BottomCenterIndex + 2 + i, BottomCenterIndex + 1 + i, MaterialIndex);
    }
}

/**
 * Create a cone mesh
 */
void CreateCone(
    FMeshBuilder& Builder,
    float Radius,
    float Height,
    int32 Segments,
    int32 MaterialIndex)
{
    Segments = std::max(3, Segments);
    
    int32 BaseVertex = Builder.GetNumVertices();
    
    // Apex vertex
    int32 ApexIndex = Builder.GetNumVertices();
    Builder.AddVertex(
        Math::FVector3f(0, 0, Height),
        Math::FVector2f(0.5f, 0.0f),
        Math::FVector3f(0, 0, 1)
    );
    
    // Side vertices
    float SlopeAngle = std::atan2(Radius, Height);
    float CosSlope = std::cos(SlopeAngle);
    float SinSlope = std::sin(SlopeAngle);
    
    for (int32 i = 0; i <= Segments; ++i)
    {
        float Angle = TWO_PI * static_cast<float>(i) / static_cast<float>(Segments);
        float SinA = std::sin(Angle);
        float CosA = std::cos(Angle);
        
        // Normal for cone side
        Math::FVector3f Normal(CosSlope * CosA, CosSlope * SinA, SinSlope);
        
        Builder.AddVertex(
            Math::FVector3f(Radius * CosA, Radius * SinA, 0),
            Math::FVector2f(static_cast<float>(i) / static_cast<float>(Segments), 1.0f),
            Normal
        );
    }
    
    // Side triangles
    for (int32 i = 0; i < Segments; ++i)
    {
        Builder.AddTriangle(ApexIndex, BaseVertex + 1 + i, BaseVertex + 2 + i, MaterialIndex);
    }
    
    // Bottom cap
    int32 BottomCenterIndex = Builder.GetNumVertices();
    Builder.AddVertex(
        Math::FVector3f(0, 0, 0),
        Math::FVector2f(0.5f, 0.5f),
        Math::FVector3f(0, 0, -1)
    );
    
    for (int32 i = 0; i <= Segments; ++i)
    {
        float Angle = TWO_PI * static_cast<float>(i) / static_cast<float>(Segments);
        float SinA = std::sin(Angle);
        float CosA = std::cos(Angle);
        
        Builder.AddVertex(
            Math::FVector3f(Radius * CosA, Radius * SinA, 0),
            Math::FVector2f(0.5f + 0.5f * CosA, 0.5f - 0.5f * SinA),
            Math::FVector3f(0, 0, -1)
        );
    }
    
    for (int32 i = 0; i < Segments; ++i)
    {
        Builder.AddTriangle(BottomCenterIndex, BottomCenterIndex + 2 + i, BottomCenterIndex + 1 + i, MaterialIndex);
    }
}

/**
 * Create a plane mesh
 */
void CreatePlane(
    FMeshBuilder& Builder,
    float Width,
    float Height,
    int32 WidthSegments,
    int32 HeightSegments,
    int32 MaterialIndex)
{
    WidthSegments = std::max(1, WidthSegments);
    HeightSegments = std::max(1, HeightSegments);
    
    float HalfWidth = Width * 0.5f;
    float HalfHeight = Height * 0.5f;
    
    int32 BaseVertex = Builder.GetNumVertices();
    
    // Generate vertices
    for (int32 y = 0; y <= HeightSegments; ++y)
    {
        float V = static_cast<float>(y) / static_cast<float>(HeightSegments);
        float PosY = -HalfHeight + Height * V;
        
        for (int32 x = 0; x <= WidthSegments; ++x)
        {
            float U = static_cast<float>(x) / static_cast<float>(WidthSegments);
            float PosX = -HalfWidth + Width * U;
            
            Builder.AddVertex(
                Math::FVector3f(PosX, PosY, 0),
                Math::FVector2f(U, V),
                Math::FVector3f(0, 0, 1)
            );
        }
    }
    
    // Generate triangles
    int32 VertsPerRow = WidthSegments + 1;
    
    for (int32 y = 0; y < HeightSegments; ++y)
    {
        for (int32 x = 0; x < WidthSegments; ++x)
        {
            int32 Current = BaseVertex + y * VertsPerRow + x;
            int32 Next = Current + 1;
            int32 CurrentAbove = Current + VertsPerRow;
            int32 NextAbove = Next + VertsPerRow;
            
            Builder.AddTriangle(Current, CurrentAbove, Next, MaterialIndex);
            Builder.AddTriangle(Next, CurrentAbove, NextAbove, MaterialIndex);
        }
    }
}

/**
 * Create a torus mesh
 */
void CreateTorus(
    FMeshBuilder& Builder,
    float OuterRadius,
    float InnerRadius,
    int32 Segments,
    int32 Sides,
    int32 MaterialIndex)
{
    Segments = std::max(3, Segments);
    Sides = std::max(3, Sides);
    
    int32 BaseVertex = Builder.GetNumVertices();
    
    // Generate vertices
    for (int32 Seg = 0; Seg <= Segments; ++Seg)
    {
        float Theta = TWO_PI * static_cast<float>(Seg) / static_cast<float>(Segments);
        float CosTheta = std::cos(Theta);
        float SinTheta = std::sin(Theta);
        
        for (int32 Side = 0; Side <= Sides; ++Side)
        {
            float Phi = TWO_PI * static_cast<float>(Side) / static_cast<float>(Sides);
            float CosPhi = std::cos(Phi);
            float SinPhi = std::sin(Phi);
            
            // Position
            float R = OuterRadius + InnerRadius * CosPhi;
            Math::FVector3f Position(
                R * CosTheta,
                R * SinTheta,
                InnerRadius * SinPhi
            );
            
            // Normal
            Math::FVector3f Normal(
                CosPhi * CosTheta,
                CosPhi * SinTheta,
                SinPhi
            );
            
            // UV
            Math::FVector2f UV(
                static_cast<float>(Seg) / static_cast<float>(Segments),
                static_cast<float>(Side) / static_cast<float>(Sides)
            );
            
            Builder.AddVertex(Position, UV, Normal);
        }
    }
    
    // Generate triangles
    int32 VertsPerSegment = Sides + 1;
    
    for (int32 Seg = 0; Seg < Segments; ++Seg)
    {
        for (int32 Side = 0; Side < Sides; ++Side)
        {
            int32 Current = BaseVertex + Seg * VertsPerSegment + Side;
            int32 Next = Current + 1;
            int32 CurrentNext = Current + VertsPerSegment;
            int32 NextNext = Next + VertsPerSegment;
            
            Builder.AddTriangle(Current, CurrentNext, Next, MaterialIndex);
            Builder.AddTriangle(Next, CurrentNext, NextNext, MaterialIndex);
        }
    }
}

} // namespace MeshPrimitives

} // namespace MonsterEngine
