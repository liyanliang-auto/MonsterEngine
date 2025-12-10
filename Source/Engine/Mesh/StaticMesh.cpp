// Copyright Monster Engine. All Rights Reserved.
// Reference UE5: Engine/Source/Runtime/Engine/Private/StaticMesh.cpp

/**
 * @file StaticMesh.cpp
 * @brief Implementation of FStaticMesh and related classes
 */

#include "Engine/Mesh/StaticMesh.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Define log category for mesh operations
DEFINE_LOG_CATEGORY_STATIC(LogStaticMesh, Log, All);

// ============================================================================
// FStaticMesh Implementation
// ============================================================================

/**
 * Default constructor
 * Initializes mesh with default values
 */
FStaticMesh::FStaticMesh()
    : Name("Unnamed")
    , MinLOD(0)
    , LightMapResolution(64)
    , LightMapCoordinateIndex(1)
{
    MR_LOG(LogStaticMesh, Verbose, "FStaticMesh default constructor called");
}

/**
 * Constructor with name
 * @param InName Mesh name for identification
 */
FStaticMesh::FStaticMesh(const String& InName)
    : Name(InName)
    , MinLOD(0)
    , LightMapResolution(64)
    , LightMapCoordinateIndex(1)
{
    MR_LOG(LogStaticMesh, Verbose, "FStaticMesh created with name: %s", InName.c_str());
}

/**
 * Destructor
 * Releases all GPU resources
 */
FStaticMesh::~FStaticMesh()
{
    MR_LOG(LogStaticMesh, Verbose, "FStaticMesh destructor called for: %s", Name.c_str());
    
    // Release render data if it exists
    ReleaseRenderData();
}

/**
 * Move constructor
 * @param Other Mesh to move from
 */
FStaticMesh::FStaticMesh(FStaticMesh&& Other) noexcept
    : Name(std::move(Other.Name))
    , SourceFilePath(std::move(Other.SourceFilePath))
    , RenderData(std::move(Other.RenderData))
    , StaticMaterials(std::move(Other.StaticMaterials))
    , Bounds(Other.Bounds)
    , MinLOD(Other.MinLOD)
    , LightMapResolution(Other.LightMapResolution)
    , LightMapCoordinateIndex(Other.LightMapCoordinateIndex)
{
    MR_LOG(LogStaticMesh, Verbose, "FStaticMesh move constructor called");
}

/**
 * Move assignment operator
 * @param Other Mesh to move from
 * @return Reference to this mesh
 */
FStaticMesh& FStaticMesh::operator=(FStaticMesh&& Other) noexcept
{
    if (this != &Other)
    {
        // Release existing resources
        ReleaseRenderData();
        
        // Move data from other
        Name = std::move(Other.Name);
        SourceFilePath = std::move(Other.SourceFilePath);
        RenderData = std::move(Other.RenderData);
        StaticMaterials = std::move(Other.StaticMaterials);
        Bounds = Other.Bounds;
        MinLOD = Other.MinLOD;
        LightMapResolution = Other.LightMapResolution;
        LightMapCoordinateIndex = Other.LightMapCoordinateIndex;
        
        MR_LOG(LogStaticMesh, Verbose, "FStaticMesh move assignment called");
    }
    return *this;
}

/**
 * Allocate render data for this mesh
 * Creates a new FStaticMeshRenderData if one doesn't exist
 * @return Reference to the render data
 */
FStaticMeshRenderData& FStaticMesh::AllocateRenderData()
{
    // Create new render data if needed
    if (!RenderData)
    {
        RenderData = MakeUnique<FStaticMeshRenderData>();
        MR_LOG(LogStaticMesh, Verbose, "Allocated render data for mesh: %s", Name.c_str());
    }
    
    return *RenderData;
}

/**
 * Release render data and free GPU resources
 */
void FStaticMesh::ReleaseRenderData()
{
    if (RenderData)
    {
        // Release all GPU resources
        RenderData->ReleaseResources();
        RenderData.Reset();
        
        MR_LOG(LogStaticMesh, Verbose, "Released render data for mesh: %s", Name.c_str());
    }
}

/**
 * Add a material slot to the mesh
 * @param Material Material to add
 * @return Index of the added material
 */
int32 FStaticMesh::AddMaterial(const FStaticMaterial& Material)
{
    int32 Index = StaticMaterials.Num();
    StaticMaterials.Add(Material);
    
    MR_LOG(LogStaticMesh, Verbose, "Added material '%s' at index %d to mesh: %s",
           Material.MaterialSlotName.c_str(), Index, Name.c_str());
    
    return Index;
}

/**
 * Calculate bounding box from render data
 * Iterates through all vertices in LOD 0 to compute bounds
 */
void FStaticMesh::CalculateBounds()
{
    if (!RenderData || RenderData->GetNumLODs() == 0)
    {
        // No render data, set default bounds
        Bounds = Math::FBox3f(
            Math::FVector3f(-1.0f, -1.0f, -1.0f),
            Math::FVector3f(1.0f, 1.0f, 1.0f)
        );
        return;
    }
    
    // Use bounds from render data
    Bounds = RenderData->Bounds;
    
    MR_LOG(LogStaticMesh, Verbose, "Calculated bounds for mesh: %s - Min(%.2f, %.2f, %.2f) Max(%.2f, %.2f, %.2f)",
           Name.c_str(),
           Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z,
           Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z);
}

/**
 * Get the number of LOD levels
 * @return LOD count, or 0 if no render data
 */
int32 FStaticMesh::GetNumLODs() const
{
    if (RenderData)
    {
        return RenderData->GetNumLODs();
    }
    return 0;
}

/**
 * Check if the mesh is valid for rendering
 * @return true if mesh has valid render data
 */
bool FStaticMesh::IsValid() const
{
    // Must have render data with at least one valid LOD
    if (!RenderData || !RenderData->IsValid())
    {
        return false;
    }
    
    // Must have at least one material
    if (StaticMaterials.Num() == 0)
    {
        return false;
    }
    
    return true;
}

/**
 * Get statistics about the mesh
 * @param OutVertexCount Output total vertex count
 * @param OutTriangleCount Output total triangle count
 * @param OutLODCount Output LOD count
 */
void FStaticMesh::GetStatistics(uint32& OutVertexCount, uint32& OutTriangleCount, int32& OutLODCount) const
{
    OutVertexCount = 0;
    OutTriangleCount = 0;
    OutLODCount = 0;
    
    if (RenderData)
    {
        OutVertexCount = RenderData->GetTotalVertexCount();
        OutTriangleCount = RenderData->GetTotalTriangleCount();
        OutLODCount = RenderData->GetNumLODs();
    }
}

} // namespace MonsterEngine
