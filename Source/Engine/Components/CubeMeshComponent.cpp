// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CubeMeshComponent.cpp
 * @brief Implementation of cube mesh component
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Private/Components/StaticMeshComponent.cpp
 */

#include "Engine/Components/CubeMeshComponent.h"
#include "Engine/Proxies/CubeSceneProxy.h"
#include "Core/Logging/LogMacros.h"
#include "Math/MonsterMath.h"

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogCubeMeshComponent, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

UCubeMeshComponent::UCubeMeshComponent()
    : UMeshComponent()
    , Texture1(nullptr)
    , Texture2(nullptr)
    , TextureBlendFactor(0.2f)
    , CubeSize(0.5f)
    , bNeedsProxyRecreation(false)
{
    MR_LOG(LogCubeMeshComponent, Verbose, "CubeMeshComponent created");
}

UCubeMeshComponent::UCubeMeshComponent(AActor* InOwner)
    : UMeshComponent(InOwner)
    , Texture1(nullptr)
    , Texture2(nullptr)
    , TextureBlendFactor(0.2f)
    , CubeSize(0.5f)
    , bNeedsProxyRecreation(false)
{
    MR_LOG(LogCubeMeshComponent, Verbose, "CubeMeshComponent created with owner");
}

UCubeMeshComponent::~UCubeMeshComponent()
{
    Texture1.Reset();
    Texture2.Reset();
    
    MR_LOG(LogCubeMeshComponent, Verbose, "CubeMeshComponent destroyed");
}

// ============================================================================
// UPrimitiveComponent Interface
// ============================================================================

FPrimitiveSceneProxy* UCubeMeshComponent::CreateSceneProxy()
{
    // Create a new cube scene proxy
    FCubeSceneProxy* Proxy = new FCubeSceneProxy(this);
    
    ClearProxyRecreationNeeded();
    
    MR_LOG(LogCubeMeshComponent, Verbose, "Created CubeSceneProxy");
    
    return Proxy;
}

FBox UCubeMeshComponent::GetLocalBounds() const
{
    // Cube bounds based on size
    FVector HalfExtent(CubeSize, CubeSize, CubeSize);
    return FBox(-HalfExtent, HalfExtent);
}

// ============================================================================
// Textures
// ============================================================================

void UCubeMeshComponent::SetTexture1(TSharedPtr<MonsterRender::RHI::IRHITexture> Texture)
{
    Texture1 = Texture;
    MarkProxyRecreationNeeded();
}

void UCubeMeshComponent::SetTexture2(TSharedPtr<MonsterRender::RHI::IRHITexture> Texture)
{
    Texture2 = Texture;
    MarkProxyRecreationNeeded();
}

// ============================================================================
// Rendering Settings
// ============================================================================

void UCubeMeshComponent::SetCubeSize(float Size)
{
    if (!FMath::IsNearlyEqual(CubeSize, Size))
    {
        CubeSize = Size;
        MarkProxyRecreationNeeded();
        MarkRenderStateDirty();
    }
}

// ============================================================================
// Static Mesh Data Generation
// ============================================================================

void UCubeMeshComponent::GenerateCubeVertices(TArray<FCubeLitVertex>& OutVertices, float HalfExtent)
{
    OutVertices.Empty();
    OutVertices.Reserve(36);  // 6 faces * 2 triangles * 3 vertices
    
    const float S = HalfExtent;
    
    // Helper lambda to add a vertex
    auto AddVertex = [&OutVertices](float px, float py, float pz, 
                                     float nx, float ny, float nz,
                                     float u, float v)
    {
        FCubeLitVertex Vertex;
        Vertex.Position[0] = px;
        Vertex.Position[1] = py;
        Vertex.Position[2] = pz;
        Vertex.Normal[0] = nx;
        Vertex.Normal[1] = ny;
        Vertex.Normal[2] = nz;
        Vertex.TexCoord[0] = u;
        Vertex.TexCoord[1] = v;
        OutVertices.Add(Vertex);
    };
    
    // Back face (z = -S, normal = (0, 0, -1))
    AddVertex(-S, -S, -S,  0.0f, 0.0f, -1.0f,  0.0f, 0.0f);
    AddVertex( S, -S, -S,  0.0f, 0.0f, -1.0f,  1.0f, 0.0f);
    AddVertex( S,  S, -S,  0.0f, 0.0f, -1.0f,  1.0f, 1.0f);
    AddVertex( S,  S, -S,  0.0f, 0.0f, -1.0f,  1.0f, 1.0f);
    AddVertex(-S,  S, -S,  0.0f, 0.0f, -1.0f,  0.0f, 1.0f);
    AddVertex(-S, -S, -S,  0.0f, 0.0f, -1.0f,  0.0f, 0.0f);

    // Front face (z = +S, normal = (0, 0, 1))
    AddVertex(-S, -S,  S,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f);
    AddVertex( S, -S,  S,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f);
    AddVertex( S,  S,  S,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f);
    AddVertex( S,  S,  S,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f);
    AddVertex(-S,  S,  S,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f);
    AddVertex(-S, -S,  S,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f);

    // Left face (x = -S, normal = (-1, 0, 0))
    AddVertex(-S,  S,  S,  -1.0f, 0.0f, 0.0f,  1.0f, 0.0f);
    AddVertex(-S,  S, -S,  -1.0f, 0.0f, 0.0f,  1.0f, 1.0f);
    AddVertex(-S, -S, -S,  -1.0f, 0.0f, 0.0f,  0.0f, 1.0f);
    AddVertex(-S, -S, -S,  -1.0f, 0.0f, 0.0f,  0.0f, 1.0f);
    AddVertex(-S, -S,  S,  -1.0f, 0.0f, 0.0f,  0.0f, 0.0f);
    AddVertex(-S,  S,  S,  -1.0f, 0.0f, 0.0f,  1.0f, 0.0f);

    // Right face (x = +S, normal = (1, 0, 0))
    AddVertex( S,  S,  S,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f);
    AddVertex( S,  S, -S,  1.0f, 0.0f, 0.0f,  1.0f, 1.0f);
    AddVertex( S, -S, -S,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f);
    AddVertex( S, -S, -S,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f);
    AddVertex( S, -S,  S,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f);
    AddVertex( S,  S,  S,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f);

    // Bottom face (y = -S, normal = (0, -1, 0))
    AddVertex(-S, -S, -S,  0.0f, -1.0f, 0.0f,  0.0f, 1.0f);
    AddVertex( S, -S, -S,  0.0f, -1.0f, 0.0f,  1.0f, 1.0f);
    AddVertex( S, -S,  S,  0.0f, -1.0f, 0.0f,  1.0f, 0.0f);
    AddVertex( S, -S,  S,  0.0f, -1.0f, 0.0f,  1.0f, 0.0f);
    AddVertex(-S, -S,  S,  0.0f, -1.0f, 0.0f,  0.0f, 0.0f);
    AddVertex(-S, -S, -S,  0.0f, -1.0f, 0.0f,  0.0f, 1.0f);

    // Top face (y = +S, normal = (0, 1, 0))
    AddVertex(-S,  S, -S,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f);
    AddVertex( S,  S, -S,  0.0f, 1.0f, 0.0f,  1.0f, 1.0f);
    AddVertex( S,  S,  S,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f);
    AddVertex( S,  S,  S,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f);
    AddVertex(-S,  S,  S,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f);
    AddVertex(-S,  S, -S,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f);
    
    MR_LOG(LogCubeMeshComponent, Verbose, "Generated %d cube vertices", OutVertices.Num());
}

} // namespace MonsterEngine
