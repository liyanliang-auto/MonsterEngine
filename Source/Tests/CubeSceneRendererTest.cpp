// Copyright Monster Engine. All Rights Reserved.

/**
 * @file CubeSceneRendererTest.cpp
 * @brief Implementation of cube scene renderer integration test
 * 
 * Demonstrates integration of cube rendering with engine render pipeline.
 */

#include "Tests/CubeSceneRendererTest.h"
#include "Renderer/Scene.h"
#include "Renderer/SceneView.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/SceneVisibility.h"
#include "Renderer/MeshDrawCommand.h"
#include "Renderer/RenderQueue.h"
#include "Core/Logging/Logging.h"
#include "Core/ShaderCompiler.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"
#include "RHI/RHIResources.h"
#include "Renderer/FTextureLoader.h"
#include "Math/MonsterMath.h"
#include <cmath>
#include <chrono>
#include <cstring>
#include <fstream>

using namespace MonsterRender;

// Use RHI types from MonsterRender namespace
using RHIDevice = MonsterRender::RHI::IRHIDevice;
using RHICommandList = MonsterRender::RHI::IRHICommandList;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace MonsterEngine
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogCubeSceneTest, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

// Cube vertex structure
struct CubeVertex
{
    float Position[3];
    float TexCoord[2];
};

// Uniform buffer for MVP matrices (aligned to 16 bytes)
struct alignas(16) CubeUniformBuffer
{
    float Model[16];
    float View[16];
    float Projection[16];
};

FCubeSceneRendererTest::FCubeSceneRendererTest()
    : Device(nullptr)
    , Scene(nullptr)
    , SceneRenderer(nullptr)
    , RenderQueue(nullptr)
    , ViewFamily(nullptr)
    , RHIBackend(MonsterRender::RHI::ERHIBackend::Unknown)
    , CameraPosition(0.0, 0.0, -5.0)
    , CameraForward(0.0, 0.0, 1.0)
    , CameraUp(0.0, 1.0, 0.0)
    , CameraRight(1.0, 0.0, 0.0)
    , FieldOfView(45.0f)
    , NearClipPlane(0.1f)
    , FarClipPlane(100.0f)
    , WindowWidth(1280)
    , WindowHeight(720)
    , TotalTime(0.0f)
    , RotationAngle(0.0f)
    , CubeCount(1)
    , bTestFrustumCulling(true)
    , bTestDistanceCulling(true)
    , bTestRenderQueue(true)
    , bInitialized(false)
    , NumVisiblePrimitives(0)
    , NumDrawCalls(0)
    , NumTriangles(0)
    , VisibilityTimeMs(0.0f)
    , DrawCommandTimeMs(0.0f)
{
    MR_LOG(LogCubeSceneTest, Log, "FCubeSceneRendererTest created");
}

FCubeSceneRendererTest::~FCubeSceneRendererTest()
{
    Shutdown();
    MR_LOG(LogCubeSceneTest, Log, "FCubeSceneRendererTest destroyed");
}

// ============================================================================
// Test Lifecycle
// ============================================================================

bool FCubeSceneRendererTest::Initialize(MonsterRender::RHI::IRHIDevice* InDevice)
{
    if (this->bInitialized)
    {
        return true;
    }
    
    if (!InDevice)
    {
        MR_LOG(LogCubeSceneTest, Error, "Cannot initialize: null device");
        return false;
    }
    
    this->Device = InDevice;
    this->RHIBackend = this->Device->getRHIBackend();
    
    MR_LOG(LogCubeSceneTest, Log, "Initializing CubeSceneRendererTest...");
    MR_LOG(LogCubeSceneTest, Log, "Using RHI backend: %s", MonsterRender::RHI::GetRHIBackendName(this->RHIBackend));
    
    // Create scene
    if (!this->CreateScene())
    {
        MR_LOG(LogCubeSceneTest, Error, "Failed to create scene");
        return false;
    }
    
    // Create cube primitives
    if (!this->CreateCubePrimitives())
    {
        MR_LOG(LogCubeSceneTest, Error, "Failed to create cube primitives");
        return false;
    }
    
    // Create shared GPU resources
    if (!this->CreateSharedGPUResources())
    {
        MR_LOG(LogCubeSceneTest, Error, "Failed to create GPU resources");
        return false;
    }
    
    // Create render queue
    this->RenderQueue = new FRenderQueue();
    // Note: FRenderQueue::Initialize expects MonsterEngine::RHI::IRHIDevice*
    // but we have MonsterRender::RHI::IRHIDevice*, so we skip initialization for now
    // this->RenderQueue->Initialize(this->Device);
    
    this->bInitialized = true;
    int32 LocalCubeCount = this->CubeCount;
    bool bLocalFrustum = this->bTestFrustumCulling;
    bool bLocalDistance = this->bTestDistanceCulling;
    bool bLocalRenderQueue = this->bTestRenderQueue;
    MR_LOG(LogCubeSceneTest, Log, "CubeSceneRendererTest initialized successfully");
    MR_LOG(LogCubeSceneTest, Log, "  - Cube count: %d", LocalCubeCount);
    MR_LOG(LogCubeSceneTest, Log, "  - Frustum culling: %s", bLocalFrustum ? "enabled" : "disabled");
    MR_LOG(LogCubeSceneTest, Log, "  - Distance culling: %s", bLocalDistance ? "enabled" : "disabled");
    MR_LOG(LogCubeSceneTest, Log, "  - Render queue: %s", bLocalRenderQueue ? "enabled" : "disabled");
    
    return true;
}

void FCubeSceneRendererTest::Shutdown()
{
    if (!this->bInitialized)
    {
        return;
    }
    
    MR_LOG(LogCubeSceneTest, Log, "Shutting down CubeSceneRendererTest...");
    
    // Cleanup cube proxies
    for (FPrimitiveSceneProxy* Proxy : CubeProxies)
    {
        delete Proxy;
    }
    CubeProxies.Empty();
    
    // Cleanup scene infos
    for (FPrimitiveSceneInfo* Info : CubeSceneInfos)
    {
        delete Info;
    }
    CubeSceneInfos.Empty();
    
    // Cleanup positions and rotations
    CubePositions.Empty();
    CubeRotations.Empty();
    
    // Cleanup GPU resources
    PipelineState.Reset();
    PixelShader.Reset();
    VertexShader.Reset();
    Sampler.Reset();
    Texture2.Reset();
    Texture1.Reset();
    UniformBuffer.Reset();
    VertexBuffer.Reset();
    
    // Cleanup render queue
    if (RenderQueue)
    {
        RenderQueue->Shutdown();
        delete RenderQueue;
        RenderQueue = nullptr;
    }
    
    // Cleanup scene renderer
    if (SceneRenderer)
    {
        delete SceneRenderer;
        SceneRenderer = nullptr;
    }
    
    // Cleanup view family
    if (ViewFamily)
    {
        delete ViewFamily;
        ViewFamily = nullptr;
    }
    
    // Cleanup scene
    if (Scene)
    {
        delete Scene;
        Scene = nullptr;
    }
    
    Device = nullptr;
    bInitialized = false;
    
    MR_LOG(LogCubeSceneTest, Log, "CubeSceneRendererTest shutdown complete");
}

void FCubeSceneRendererTest::Update(float DeltaTime)
{
    if (!bInitialized)
    {
        return;
    }
    
    TotalTime += DeltaTime;
    RotationAngle = TotalTime;
    
    // Update cube rotations
    for (int32 i = 0; i < CubeRotations.Num(); ++i)
    {
        CubeRotations[i] = RotationAngle + static_cast<float>(i) * 0.5f;
    }
    
    // Update cube transforms in scene
    for (int32 i = 0; i < CubeProxies.Num(); ++i)
    {
        if (CubeProxies[i])
        {
            // Build rotation matrix
            float Angle = CubeRotations[i];
            float CosA = std::cos(Angle);
            float SinA = std::sin(Angle);
            
            // Rotation around Y axis combined with position
            Math::FMatrix Transform = Math::FMatrix::Identity;
            Transform.M[0][0] = CosA;
            Transform.M[0][2] = SinA;
            Transform.M[2][0] = -SinA;
            Transform.M[2][2] = CosA;
            
            // Set translation
            Transform.M[3][0] = CubePositions[i].X;
            Transform.M[3][1] = CubePositions[i].Y;
            Transform.M[3][2] = CubePositions[i].Z;
            
            CubeProxies[i]->SetLocalToWorld(Transform);
        }
    }
}

void FCubeSceneRendererTest::Render(MonsterRender::RHI::IRHICommandList* CmdList)
{
    if (!bInitialized || !CmdList)
    {
        return;
    }
    
    // Reset statistics
    this->NumVisiblePrimitives = 0;
    this->NumDrawCalls = 0;
    this->NumTriangles = 0;
    
    // Setup view
    this->SetupView();
    
    // Compute visibility (frustum and distance culling)
    auto VisStart = std::chrono::high_resolution_clock::now();
    this->ComputeVisibility();
    auto VisEnd = std::chrono::high_resolution_clock::now();
    this->VisibilityTimeMs = std::chrono::duration<float, std::milli>(VisEnd - VisStart).count();
    
    // Generate mesh draw commands
    auto DrawStart = std::chrono::high_resolution_clock::now();
    this->GenerateMeshDrawCommands();
    auto DrawEnd = std::chrono::high_resolution_clock::now();
    this->DrawCommandTimeMs = std::chrono::duration<float, std::milli>(DrawEnd - DrawStart).count();
    
    // Execute render queue
    if (this->bTestRenderQueue)
    {
        this->ExecuteRenderQueueInternal(CmdList);
    }
    
    // Log statistics periodically
    static float StaticLastLogTime = 0.0f;
    if (this->TotalTime - StaticLastLogTime > 5.0f)
    {
        this->LogStatistics();
        StaticLastLogTime = this->TotalTime;
    }
}

void FCubeSceneRendererTest::SetWindowDimensions(uint32 Width, uint32 Height)
{
    WindowWidth = Width;
    WindowHeight = Height;
    MR_LOG(LogCubeSceneTest, Verbose, "Window dimensions set to %dx%d", Width, Height);
}

bool FCubeSceneRendererTest::RunTest()
{
    MR_LOG(LogCubeSceneTest, Log, "Running CubeSceneRendererTest...");
    
    bool bPassed = true;
    
    // Test 1: Scene creation
    if (Scene == nullptr)
    {
        MR_LOG(LogCubeSceneTest, Error, "Test FAILED: Scene not created");
        bPassed = false;
    }
    else
    {
        MR_LOG(LogCubeSceneTest, Log, "Test PASSED: Scene created");
    }
    
    // Test 2: Primitive registration
    if (Scene && Scene->GetNumPrimitives() != CubeCount)
    {
        MR_LOG(LogCubeSceneTest, Error, "Test FAILED: Expected %d primitives, got %d",
               CubeCount, Scene->GetNumPrimitives());
        bPassed = false;
    }
    else
    {
        MR_LOG(LogCubeSceneTest, Log, "Test PASSED: %d primitives registered", CubeCount);
    }
    
    // Test 3: Render queue
    if (RenderQueue == nullptr)
    {
        MR_LOG(LogCubeSceneTest, Error, "Test FAILED: Render queue not created");
        bPassed = false;
    }
    else
    {
        MR_LOG(LogCubeSceneTest, Log, "Test PASSED: Render queue created");
    }
    
    // Test 4: Visibility computation
    ComputeVisibility();
    if (NumVisiblePrimitives < 0 || NumVisiblePrimitives > CubeCount)
    {
        MR_LOG(LogCubeSceneTest, Error, "Test FAILED: Invalid visible primitive count: %d",
               NumVisiblePrimitives);
        bPassed = false;
    }
    else
    {
        MR_LOG(LogCubeSceneTest, Log, "Test PASSED: Visibility computed, %d visible",
               NumVisiblePrimitives);
    }
    
    MR_LOG(LogCubeSceneTest, Log, "CubeSceneRendererTest %s", bPassed ? "PASSED" : "FAILED");
    
    return bPassed;
}

// ============================================================================
// Internal Methods
// ============================================================================

bool FCubeSceneRendererTest::CreateScene()
{
    MR_LOG(LogCubeSceneTest, Log, "Creating scene...");
    
    // Create scene
    Scene = new FScene();
    
    // Create view family
    ViewFamily = new FSceneViewFamily();
    ViewFamily->Scene = Scene;
    
    MR_LOG(LogCubeSceneTest, Log, "Scene created");
    return true;
}

bool FCubeSceneRendererTest::CreateCubePrimitives()
{
    MR_LOG(LogCubeSceneTest, Log, "Creating %d cube primitives...", CubeCount);
    
    // Reserve space
    CubeProxies.Reserve(CubeCount);
    CubeSceneInfos.Reserve(CubeCount);
    CubePositions.Reserve(CubeCount);
    CubeRotations.Reserve(CubeCount);
    
    // Create cubes in a grid pattern
    int32 GridSize = static_cast<int32>(std::ceil(std::sqrt(static_cast<float>(CubeCount))));
    float Spacing = 2.5f;
    float StartOffset = -static_cast<float>(GridSize - 1) * Spacing * 0.5f;
    
    for (int32 i = 0; i < CubeCount; ++i)
    {
        int32 GridX = i % GridSize;
        int32 GridY = i / GridSize;
        
        // Calculate position
        Math::FVector Position(
            StartOffset + static_cast<float>(GridX) * Spacing,
            StartOffset + static_cast<float>(GridY) * Spacing,
            0.0
        );
        CubePositions.Add(Position);
        
        // Initial rotation
        CubeRotations.Add(static_cast<float>(i) * 0.3f);
        
        // Create primitive proxy
        FPrimitiveSceneProxy* Proxy = new FPrimitiveSceneProxy();
        
        // Set initial transform
        Math::FMatrix Transform = Math::FMatrix::Identity;
        Transform.M[3][0] = Position.X;
        Transform.M[3][1] = Position.Y;
        Transform.M[3][2] = Position.Z;
        Proxy->SetLocalToWorld(Transform);
        
        // Set bounds (cube is 1x1x1)
        Proxy->LocalBounds.Origin = Math::FVector(0.0, 0.0, 0.0);
        Proxy->LocalBounds.BoxExtent = Math::FVector(0.5, 0.5, 0.5);
        Proxy->LocalBounds.SphereRadius = 0.866f; // sqrt(3) / 2
        Proxy->Bounds = Proxy->LocalBounds;
        Proxy->Bounds.Origin = Position;
        
        // Set culling distances
        Proxy->MinDrawDistance = 0.0f;
        Proxy->MaxDrawDistance = 50.0f;
        
        CubeProxies.Add(Proxy);
        
        // Create scene info and add to scene
        FPrimitiveSceneInfo* SceneInfo = new FPrimitiveSceneInfo(Proxy, Scene);
        CubeSceneInfos.Add(SceneInfo);
        
        // Add to scene
        if (Scene)
        {
            Scene->AddPrimitive(Proxy);
        }
        
        // GPU resources are shared, created in CreateSharedGPUResources()
    }
    
    MR_LOG(LogCubeSceneTest, Log, "Created %d cube primitives", CubeProxies.Num());
    return CubeProxies.Num() > 0;
}

void FCubeSceneRendererTest::SetupView()
{
    if (!ViewFamily)
    {
        return;
    }
    
    // Update view matrices
    // View family already has scene reference
}

void FCubeSceneRendererTest::ComputeVisibility()
{
    if (!Scene)
    {
        return;
    }
    
    NumVisiblePrimitives = 0;
    
    // Build view frustum
    Math::FMatrix ViewMatrix;
    Math::FMatrix ProjectionMatrix;
    BuildViewMatrix(ViewMatrix);
    BuildProjectionMatrix(ProjectionMatrix);
    
    Math::FMatrix ViewProjection = ViewMatrix * ProjectionMatrix;
    
    // Build frustum planes from view-projection matrix
    FConvexVolume ViewFrustum;
    
    // Extract frustum planes from view-projection matrix
    // Left plane
    Math::FPlane LeftPlane;
    LeftPlane.X = ViewProjection.M[0][3] + ViewProjection.M[0][0];
    LeftPlane.Y = ViewProjection.M[1][3] + ViewProjection.M[1][0];
    LeftPlane.Z = ViewProjection.M[2][3] + ViewProjection.M[2][0];
    LeftPlane.W = ViewProjection.M[3][3] + ViewProjection.M[3][0];
    ViewFrustum.Planes.Add(LeftPlane);
    
    // Right plane
    Math::FPlane RightPlane;
    RightPlane.X = ViewProjection.M[0][3] - ViewProjection.M[0][0];
    RightPlane.Y = ViewProjection.M[1][3] - ViewProjection.M[1][0];
    RightPlane.Z = ViewProjection.M[2][3] - ViewProjection.M[2][0];
    RightPlane.W = ViewProjection.M[3][3] - ViewProjection.M[3][0];
    ViewFrustum.Planes.Add(RightPlane);
    
    // Bottom plane
    Math::FPlane BottomPlane;
    BottomPlane.X = ViewProjection.M[0][3] + ViewProjection.M[0][1];
    BottomPlane.Y = ViewProjection.M[1][3] + ViewProjection.M[1][1];
    BottomPlane.Z = ViewProjection.M[2][3] + ViewProjection.M[2][1];
    BottomPlane.W = ViewProjection.M[3][3] + ViewProjection.M[3][1];
    ViewFrustum.Planes.Add(BottomPlane);
    
    // Top plane
    Math::FPlane TopPlane;
    TopPlane.X = ViewProjection.M[0][3] - ViewProjection.M[0][1];
    TopPlane.Y = ViewProjection.M[1][3] - ViewProjection.M[1][1];
    TopPlane.Z = ViewProjection.M[2][3] - ViewProjection.M[2][1];
    TopPlane.W = ViewProjection.M[3][3] - ViewProjection.M[3][1];
    ViewFrustum.Planes.Add(TopPlane);
    
    // Near plane
    Math::FPlane NearPlane;
    NearPlane.X = ViewProjection.M[0][2];
    NearPlane.Y = ViewProjection.M[1][2];
    NearPlane.Z = ViewProjection.M[2][2];
    NearPlane.W = ViewProjection.M[3][2];
    ViewFrustum.Planes.Add(NearPlane);
    
    // Far plane
    Math::FPlane FarPlane;
    FarPlane.X = ViewProjection.M[0][3] - ViewProjection.M[0][2];
    FarPlane.Y = ViewProjection.M[1][3] - ViewProjection.M[1][2];
    FarPlane.Z = ViewProjection.M[2][3] - ViewProjection.M[2][2];
    FarPlane.W = ViewProjection.M[3][3] - ViewProjection.M[3][2];
    ViewFrustum.Planes.Add(FarPlane);
    
    // Test each primitive against frustum
    for (int32 i = 0; i < CubeProxies.Num(); ++i)
    {
        FPrimitiveSceneProxy* Proxy = CubeProxies[i];
        if (!Proxy)
        {
            continue;
        }
        
        const FBoxSphereBounds& Bounds = Proxy->GetBounds();
        bool bVisible = true;
        
        // Frustum culling
        if (bTestFrustumCulling)
        {
            bVisible = ViewFrustum.IntersectSphere(Bounds.Origin, Bounds.SphereRadius);
        }
        
        // Distance culling
        if (bVisible && bTestDistanceCulling)
        {
            Math::FVector ToBounds = Bounds.Origin - CameraPosition;
            float DistanceSquared = ToBounds.X * ToBounds.X + ToBounds.Y * ToBounds.Y + ToBounds.Z * ToBounds.Z;
            float MaxDist = Proxy->MaxDrawDistance;
            
            if (MaxDist > 0.0f && DistanceSquared > MaxDist * MaxDist)
            {
                bVisible = false;
            }
        }
        
        if (bVisible)
        {
            NumVisiblePrimitives++;
        }
    }
}

void FCubeSceneRendererTest::GenerateMeshDrawCommands()
{
    if (!RenderQueue)
    {
        return;
    }
    
    // Clear previous frame's commands
    RenderQueue->BeginFrame();
    
    // Generate draw commands for visible primitives
    for (int32 i = 0; i < CubeProxies.Num(); ++i)
    {
        FPrimitiveSceneProxy* Proxy = CubeProxies[i];
        if (!Proxy)
        {
            continue;
        }
        
        // Create mesh batch for this cube
        FMeshBatch MeshBatch;
        MeshBatch.NumVertices = 36;  // 6 faces * 2 triangles * 3 vertices
        MeshBatch.NumInstances = 1;
        MeshBatch.PrimitiveType = 0;  // Triangle list
        MeshBatch.bCastShadow = true;
        
        // Note: In a full implementation, we would set VertexBuffer, IndexBuffer, PipelineState
        // from the actual GPU resources created for this cube
        
        // Calculate distance from camera for sorting
        const FBoxSphereBounds& Bounds = Proxy->GetBounds();
        Math::FVector ToBounds = Bounds.Origin - CameraPosition;
        float Distance = std::sqrt(ToBounds.X * ToBounds.X + ToBounds.Y * ToBounds.Y + ToBounds.Z * ToBounds.Z);
        
        // Add to render queue (base pass)
        RenderQueue->AddMeshBatch(
            EMeshPass::BasePass,
            MeshBatch,
            ERenderQueuePriority::Opaque,
            Distance,
            CubeSceneInfos[i]
        );
        
        // Also add to depth pass
        RenderQueue->AddMeshBatch(
            EMeshPass::DepthPass,
            MeshBatch,
            ERenderQueuePriority::Opaque,
            Distance,
            CubeSceneInfos[i]
        );
    }
    
    // Sort and optimize render queue
    RenderQueue->Optimize();
    
    NumDrawCalls = RenderQueue->GetTotalNumItems();
    NumTriangles = NumVisiblePrimitives * 12;  // 12 triangles per cube
}

void FCubeSceneRendererTest::ExecuteRenderQueueInternal(MonsterRender::RHI::IRHICommandList* CmdList)
{
    if (!CmdList || !PipelineState)
    {
        return;
    }
    
    // Update uniform buffer with current MVP matrices
    UpdateUniformBuffer();
    
    // Set pipeline state
    CmdList->setPipelineState(PipelineState);
    
    // Bind uniform buffer at slot 0
    CmdList->setConstantBuffer(0, UniformBuffer);
    
    // Bind textures at slots 1 and 2
    if (Texture1)
    {
        CmdList->setShaderResource(1, Texture1);
    }
    if (Texture2)
    {
        CmdList->setShaderResource(2, Texture2);
    }
    
    // Bind vertex buffer
    TArray<TSharedPtr<MonsterRender::RHI::IRHIBuffer>> VertexBuffers;
    VertexBuffers.Add(VertexBuffer);
    CmdList->setVertexBuffers(0, TSpan<TSharedPtr<MonsterRender::RHI::IRHIBuffer>>(VertexBuffers));
    
    // Set viewport
    MonsterRender::RHI::Viewport Viewport(static_cast<float>(WindowWidth), static_cast<float>(WindowHeight));
    CmdList->setViewport(Viewport);
    
    // Set scissor rect
    MonsterRender::RHI::ScissorRect Scissor;
    Scissor.left = 0;
    Scissor.top = 0;
    Scissor.right = WindowWidth;
    Scissor.bottom = WindowHeight;
    CmdList->setScissorRect(Scissor);
    
    // Draw cube (36 vertices)
    CmdList->draw(36, 0);
    
    NumDrawCalls = 1;
    NumTriangles = 12;  // 12 triangles per cube
    
    // End frame for render queue
    if (RenderQueue)
    {
        RenderQueue->EndFrame();
    }
    
    MR_LOG(LogCubeSceneTest, Verbose, "Cube rendered at angle: %.2f", RotationAngle);
}

// ============================================================================
// GPU Resource Creation
// ============================================================================

bool FCubeSceneRendererTest::CreateSharedGPUResources()
{
    MR_LOG(LogCubeSceneTest, Log, "Creating shared GPU resources...");
    
    if (!CreateVertexBuffer())
    {
        MR_LOG(LogCubeSceneTest, Error, "Failed to create vertex buffer");
        return false;
    }
    
    if (!CreateUniformBuffer())
    {
        MR_LOG(LogCubeSceneTest, Error, "Failed to create uniform buffer");
        return false;
    }
    
    if (!LoadTextures())
    {
        MR_LOG(LogCubeSceneTest, Error, "Failed to load textures");
        return false;
    }
    
    // Create sampler
    MonsterRender::RHI::SamplerDesc SamplerDesc;
    SamplerDesc.filter = MonsterRender::RHI::ESamplerFilter::Trilinear;
    SamplerDesc.addressU = MonsterRender::RHI::ESamplerAddressMode::Wrap;
    SamplerDesc.addressV = MonsterRender::RHI::ESamplerAddressMode::Wrap;
    SamplerDesc.addressW = MonsterRender::RHI::ESamplerAddressMode::Wrap;
    SamplerDesc.maxAnisotropy = 16;
    SamplerDesc.debugName = "CubeSceneTest Sampler";
    Sampler = Device->createSampler(SamplerDesc);
    if (!Sampler)
    {
        MR_LOG(LogCubeSceneTest, Error, "Failed to create sampler");
        return false;
    }
    
    if (!CreateShaders())
    {
        MR_LOG(LogCubeSceneTest, Error, "Failed to create shaders");
        return false;
    }
    
    if (!CreatePipelineState())
    {
        MR_LOG(LogCubeSceneTest, Error, "Failed to create pipeline state");
        return false;
    }
    
    MR_LOG(LogCubeSceneTest, Log, "Shared GPU resources created successfully");
    return true;
}

bool FCubeSceneRendererTest::CreateVertexBuffer()
{
    MR_LOG(LogCubeSceneTest, Log, "Creating cube vertex buffer...");
    
    // Cube vertices with position and texture coordinates (36 vertices for 6 faces)
    TArray<CubeVertex> Vertices = {
        // Back face (z = -0.5)
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},

        // Front face (z = 0.5)
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},

        // Left face (x = -0.5)
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},

        // Right face (x = 0.5)
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},

        // Bottom face (y = -0.5)
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},

        // Top face (y = 0.5)
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}}
    };
    
    MonsterRender::RHI::BufferDesc VBDesc;
    VBDesc.size = static_cast<uint32>(sizeof(CubeVertex) * Vertices.Num());
    VBDesc.usage = MonsterRender::RHI::EResourceUsage::VertexBuffer;
    VBDesc.cpuAccessible = true;
    VBDesc.debugName = "CubeSceneTest Vertex Buffer";
    
    VertexBuffer = Device->createBuffer(VBDesc);
    if (!VertexBuffer)
    {
        return false;
    }
    
    // Upload vertex data
    void* MappedData = VertexBuffer->map();
    if (!MappedData)
    {
        return false;
    }
    std::memcpy(MappedData, Vertices.GetData(), VBDesc.size);
    VertexBuffer->unmap();
    
    MR_LOG(LogCubeSceneTest, Log, "Vertex buffer created (36 vertices, %u bytes)", VBDesc.size);
    return true;
}

bool FCubeSceneRendererTest::CreateUniformBuffer()
{
    MR_LOG(LogCubeSceneTest, Log, "Creating uniform buffer...");
    
    MonsterRender::RHI::BufferDesc UBDesc;
    UBDesc.size = sizeof(CubeUniformBuffer);
    UBDesc.usage = MonsterRender::RHI::EResourceUsage::UniformBuffer;
    UBDesc.cpuAccessible = true;
    UBDesc.debugName = "CubeSceneTest Uniform Buffer";
    
    UniformBuffer = Device->createBuffer(UBDesc);
    if (!UniformBuffer)
    {
        return false;
    }
    
    MR_LOG(LogCubeSceneTest, Log, "Uniform buffer created (%u bytes)", UBDesc.size);
    return true;
}

bool FCubeSceneRendererTest::LoadTextures()
{
    MR_LOG(LogCubeSceneTest, Log, "Loading textures...");
    
    // Load texture 1 (container)
    FTextureLoadInfo LoadInfo1;
    LoadInfo1.FilePath = "resources/textures/container.jpg";
    LoadInfo1.bGenerateMips = true;
    LoadInfo1.bSRGB = true;
    LoadInfo1.bFlipVertical = true;
    LoadInfo1.DesiredChannels = 4;
    
    Texture1 = FTextureLoader::LoadFromFile(Device, LoadInfo1);
    if (!Texture1)
    {
        MR_LOG(LogCubeSceneTest, Warning, "Failed to load container texture, creating placeholder");
        
        // Create placeholder checkerboard texture
        const uint32 TexSize = 256;
        TArray<uint8> CheckerData;
        CheckerData.SetNum(TexSize * TexSize * 4);
        
        for (uint32 y = 0; y < TexSize; ++y)
        {
            for (uint32 x = 0; x < TexSize; ++x)
            {
                uint32 Idx = (y * TexSize + x) * 4;
                bool bWhite = ((x / 32) + (y / 32)) % 2 == 0;
                uint8 Color = bWhite ? 255 : 100;
                CheckerData[Idx + 0] = Color;
                CheckerData[Idx + 1] = Color;
                CheckerData[Idx + 2] = Color;
                CheckerData[Idx + 3] = 255;
            }
        }
        
        MonsterRender::RHI::TextureDesc TexDesc;
        TexDesc.width = TexSize;
        TexDesc.height = TexSize;
        TexDesc.depth = 1;
        TexDesc.mipLevels = 1;
        TexDesc.arraySize = 1;
        TexDesc.format = MonsterRender::RHI::EPixelFormat::R8G8B8A8_SRGB;
        TexDesc.usage = MonsterRender::RHI::EResourceUsage::ShaderResource;
        TexDesc.debugName = "CubeSceneTest Texture1 (Placeholder)";
        
        Texture1 = Device->createTexture(TexDesc);
        if (Texture1)
        {
            FTextureData PlaceholderData;
            PlaceholderData.Width = TexSize;
            PlaceholderData.Height = TexSize;
            PlaceholderData.Channels = 4;
            PlaceholderData.MipLevels = 1;
            PlaceholderData.Pixels = CheckerData.GetData();
            PlaceholderData.MipData.push_back(CheckerData.GetData());
            PlaceholderData.MipSizes.push_back(TexSize * TexSize * 4);
            
            MonsterRender::RHI::IRHICommandList* ImmCmdList = Device->getImmediateCommandList();
            if (ImmCmdList)
            {
                FTextureLoader::UploadTextureData(Device, ImmCmdList, Texture1, PlaceholderData);
            }
            PlaceholderData.Pixels = nullptr;
            PlaceholderData.MipData.clear();
        }
    }
    
    // Load texture 2 (awesomeface)
    FTextureLoadInfo LoadInfo2;
    LoadInfo2.FilePath = "resources/textures/awesomeface.png";
    LoadInfo2.bGenerateMips = true;
    LoadInfo2.bSRGB = true;
    LoadInfo2.bFlipVertical = true;
    LoadInfo2.DesiredChannels = 4;
    
    Texture2 = FTextureLoader::LoadFromFile(Device, LoadInfo2);
    if (!Texture2)
    {
        MR_LOG(LogCubeSceneTest, Warning, "Failed to load awesomeface texture, creating placeholder");
        
        // Create placeholder gradient texture
        const uint32 TexSize = 256;
        TArray<uint8> GradientData;
        GradientData.SetNum(TexSize * TexSize * 4);
        
        for (uint32 y = 0; y < TexSize; ++y)
        {
            for (uint32 x = 0; x < TexSize; ++x)
            {
                uint32 Idx = (y * TexSize + x) * 4;
                GradientData[Idx + 0] = static_cast<uint8>((x * 255) / TexSize);
                GradientData[Idx + 1] = static_cast<uint8>((y * 255) / TexSize);
                GradientData[Idx + 2] = 128;
                GradientData[Idx + 3] = 255;
            }
        }
        
        MonsterRender::RHI::TextureDesc Tex2Desc;
        Tex2Desc.width = TexSize;
        Tex2Desc.height = TexSize;
        Tex2Desc.depth = 1;
        Tex2Desc.mipLevels = 1;
        Tex2Desc.arraySize = 1;
        Tex2Desc.format = MonsterRender::RHI::EPixelFormat::R8G8B8A8_SRGB;
        Tex2Desc.usage = MonsterRender::RHI::EResourceUsage::ShaderResource;
        Tex2Desc.debugName = "CubeSceneTest Texture2 (Placeholder)";
        
        Texture2 = Device->createTexture(Tex2Desc);
        if (Texture2)
        {
            FTextureData PlaceholderData;
            PlaceholderData.Width = TexSize;
            PlaceholderData.Height = TexSize;
            PlaceholderData.Channels = 4;
            PlaceholderData.MipLevels = 1;
            PlaceholderData.Pixels = GradientData.GetData();
            PlaceholderData.MipData.push_back(GradientData.GetData());
            PlaceholderData.MipSizes.push_back(TexSize * TexSize * 4);
            
            MonsterRender::RHI::IRHICommandList* ImmCmdList = Device->getImmediateCommandList();
            if (ImmCmdList)
            {
                FTextureLoader::UploadTextureData(Device, ImmCmdList, Texture2, PlaceholderData);
            }
            PlaceholderData.Pixels = nullptr;
            PlaceholderData.MipData.clear();
        }
    }
    
    MR_LOG(LogCubeSceneTest, Log, "Textures loaded");
    return Texture1 != nullptr && Texture2 != nullptr;
}

bool FCubeSceneRendererTest::CreateShaders()
{
    MR_LOG(LogCubeSceneTest, Log, "Creating shaders for %s...", MonsterRender::RHI::GetRHIBackendName(RHIBackend));
    
    if (RHIBackend == MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        // Load SPIR-V shaders
        std::vector<uint8> VsSpv = ShaderCompiler::readFileBytes("Shaders/Cube.vert.spv");
        std::vector<uint8> PsSpv = ShaderCompiler::readFileBytes("Shaders/Cube.frag.spv");
        
        if (VsSpv.empty() || PsSpv.empty())
        {
            MR_LOG(LogCubeSceneTest, Error, "Failed to load SPIR-V shaders");
            return false;
        }
        
        VertexShader = Device->createVertexShader(TSpan<const uint8>(VsSpv.data(), VsSpv.size()));
        PixelShader = Device->createPixelShader(TSpan<const uint8>(PsSpv.data(), PsSpv.size()));
    }
    else if (RHIBackend == MonsterRender::RHI::ERHIBackend::OpenGL)
    {
        // Load GLSL shaders
        std::ifstream VsFile("Shaders/Cube_GL.vert", std::ios::binary);
        std::ifstream PsFile("Shaders/Cube_GL.frag", std::ios::binary);
        
        if (!VsFile.is_open() || !PsFile.is_open())
        {
            MR_LOG(LogCubeSceneTest, Error, "Failed to open GLSL shader files");
            return false;
        }
        
        std::string VsSource((std::istreambuf_iterator<char>(VsFile)), std::istreambuf_iterator<char>());
        std::string PsSource((std::istreambuf_iterator<char>(PsFile)), std::istreambuf_iterator<char>());
        
        std::vector<uint8> VsData(VsSource.begin(), VsSource.end());
        VsData.push_back(0);
        std::vector<uint8> PsData(PsSource.begin(), PsSource.end());
        PsData.push_back(0);
        
        VertexShader = Device->createVertexShader(TSpan<const uint8>(VsData.data(), VsData.size()));
        PixelShader = Device->createPixelShader(TSpan<const uint8>(PsData.data(), PsData.size()));
    }
    else
    {
        MR_LOG(LogCubeSceneTest, Error, "Unsupported RHI backend");
        return false;
    }
    
    if (!VertexShader || !PixelShader)
    {
        MR_LOG(LogCubeSceneTest, Error, "Failed to create shaders");
        return false;
    }
    
    MR_LOG(LogCubeSceneTest, Log, "Shaders created successfully");
    return true;
}

bool FCubeSceneRendererTest::CreatePipelineState()
{
    MR_LOG(LogCubeSceneTest, Log, "Creating pipeline state...");
    
    if (!VertexShader || !PixelShader)
    {
        return false;
    }
    
    MonsterRender::RHI::EPixelFormat RenderTargetFormat = Device->getSwapChainFormat();
    MonsterRender::RHI::EPixelFormat DepthFormat = Device->getDepthFormat();
    
    MonsterRender::RHI::PipelineStateDesc PipelineDesc;
    PipelineDesc.vertexShader = VertexShader;
    PipelineDesc.pixelShader = PixelShader;
    PipelineDesc.primitiveTopology = MonsterRender::RHI::EPrimitiveTopology::TriangleList;
    
    // Vertex layout: position (vec3) + texcoord (vec2)
    MonsterRender::RHI::VertexAttribute PosAttr;
    PosAttr.location = 0;
    PosAttr.format = MonsterRender::RHI::EVertexFormat::Float3;
    PosAttr.offset = 0;
    PosAttr.semanticName = "POSITION";
    
    MonsterRender::RHI::VertexAttribute TexCoordAttr;
    TexCoordAttr.location = 1;
    TexCoordAttr.format = MonsterRender::RHI::EVertexFormat::Float2;
    TexCoordAttr.offset = sizeof(float) * 3;
    TexCoordAttr.semanticName = "TEXCOORD";
    
    PipelineDesc.vertexLayout.attributes.push_back(PosAttr);
    PipelineDesc.vertexLayout.attributes.push_back(TexCoordAttr);
    PipelineDesc.vertexLayout.stride = sizeof(CubeVertex);
    
    // Rasterizer state
    PipelineDesc.rasterizerState.fillMode = MonsterRender::RHI::EFillMode::Solid;
    PipelineDesc.rasterizerState.cullMode = MonsterRender::RHI::ECullMode::None;
    PipelineDesc.rasterizerState.frontCounterClockwise = false;
    
    // Depth stencil state
    PipelineDesc.depthStencilState.depthEnable = true;
    PipelineDesc.depthStencilState.depthWriteEnable = true;
    PipelineDesc.depthStencilState.depthCompareOp = MonsterRender::RHI::ECompareOp::Less;
    
    // Blend state
    PipelineDesc.blendState.blendEnable = false;
    
    // Render target format
    PipelineDesc.renderTargetFormats.push_back(RenderTargetFormat);
    PipelineDesc.depthStencilFormat = DepthFormat;
    PipelineDesc.debugName = "CubeSceneTest Pipeline";
    
    PipelineState = Device->createPipelineState(PipelineDesc);
    if (!PipelineState)
    {
        MR_LOG(LogCubeSceneTest, Error, "Failed to create pipeline state");
        return false;
    }
    
    MR_LOG(LogCubeSceneTest, Log, "Pipeline state created successfully");
    return true;
}

void FCubeSceneRendererTest::UpdateUniformBuffer()
{
    if (!UniformBuffer)
    {
        return;
    }
    
    CubeUniformBuffer Ubo;
    
    // Build model matrix (rotation)
    BuildModelMatrix(0, Ubo.Model);
    
    // Build view matrix
    std::memset(Ubo.View, 0, sizeof(Ubo.View));
    Ubo.View[0] = 1.0f;
    Ubo.View[5] = 1.0f;
    Ubo.View[10] = 1.0f;
    Ubo.View[15] = 1.0f;
    Ubo.View[14] = -3.0f;  // Camera at z = -3
    
    // Build projection matrix
    std::memset(Ubo.Projection, 0, sizeof(Ubo.Projection));
    float Fov = static_cast<float>(FieldOfView * M_PI / 180.0);
    float TanHalfFov = std::tan(Fov / 2.0f);
    float Aspect = static_cast<float>(WindowWidth) / static_cast<float>(WindowHeight);
    
    Ubo.Projection[0] = 1.0f / (Aspect * TanHalfFov);
    Ubo.Projection[5] = (RHIBackend == MonsterRender::RHI::ERHIBackend::Vulkan) ? -1.0f / TanHalfFov : 1.0f / TanHalfFov;
    Ubo.Projection[10] = FarClipPlane / (NearClipPlane - FarClipPlane);
    Ubo.Projection[11] = -1.0f;
    Ubo.Projection[14] = -(FarClipPlane * NearClipPlane) / (FarClipPlane - NearClipPlane);
    
    // Upload to GPU
    void* MappedData = UniformBuffer->map();
    if (MappedData)
    {
        std::memcpy(MappedData, &Ubo, sizeof(CubeUniformBuffer));
        UniformBuffer->unmap();
    }
}

void FCubeSceneRendererTest::BuildModelMatrix(int32 CubeIndex, float* OutMatrix)
{
    // Identity matrix
    std::memset(OutMatrix, 0, 16 * sizeof(float));
    OutMatrix[0] = OutMatrix[5] = OutMatrix[10] = OutMatrix[15] = 1.0f;
    
    // Apply rotation around axis (0.5, 1.0, 0.0)
    float Angle = RotationAngle;
    float X = 0.5f, Y = 1.0f, Z = 0.0f;
    
    // Normalize axis
    float Length = std::sqrt(X * X + Y * Y + Z * Z);
    if (Length > 0.0001f)
    {
        X /= Length;
        Y /= Length;
        Z /= Length;
    }
    
    float C = std::cos(Angle);
    float S = std::sin(Angle);
    float T = 1.0f - C;
    
    OutMatrix[0] = T * X * X + C;
    OutMatrix[1] = T * X * Y + S * Z;
    OutMatrix[2] = T * X * Z - S * Y;
    
    OutMatrix[4] = T * X * Y - S * Z;
    OutMatrix[5] = T * Y * Y + C;
    OutMatrix[6] = T * Y * Z + S * X;
    
    OutMatrix[8] = T * X * Z + S * Y;
    OutMatrix[9] = T * Y * Z - S * X;
    OutMatrix[10] = T * Z * Z + C;
}

void FCubeSceneRendererTest::BuildViewMatrix(Math::FMatrix& OutMatrix)
{
    // Build view matrix from camera position and orientation
    OutMatrix = Math::FMatrix::Identity;
    
    // View matrix = inverse of camera transform
    // For a simple look-at camera:
    OutMatrix.M[0][0] = CameraRight.X;
    OutMatrix.M[0][1] = CameraUp.X;
    OutMatrix.M[0][2] = CameraForward.X;
    
    OutMatrix.M[1][0] = CameraRight.Y;
    OutMatrix.M[1][1] = CameraUp.Y;
    OutMatrix.M[1][2] = CameraForward.Y;
    
    OutMatrix.M[2][0] = CameraRight.Z;
    OutMatrix.M[2][1] = CameraUp.Z;
    OutMatrix.M[2][2] = CameraForward.Z;
    
    // Translation (dot products)
    OutMatrix.M[3][0] = -(CameraRight.X * CameraPosition.X + CameraRight.Y * CameraPosition.Y + CameraRight.Z * CameraPosition.Z);
    OutMatrix.M[3][1] = -(CameraUp.X * CameraPosition.X + CameraUp.Y * CameraPosition.Y + CameraUp.Z * CameraPosition.Z);
    OutMatrix.M[3][2] = -(CameraForward.X * CameraPosition.X + CameraForward.Y * CameraPosition.Y + CameraForward.Z * CameraPosition.Z);
}

void FCubeSceneRendererTest::BuildProjectionMatrix(Math::FMatrix& OutMatrix)
{
    // Build perspective projection matrix
    OutMatrix = Math::FMatrix(EForceInit::ForceInit);
    
    float FOVRad = FieldOfView * 3.14159265359f / 180.0f;
    float TanHalfFOV = std::tan(FOVRad * 0.5f);
    float AspectRatio = static_cast<float>(WindowWidth) / static_cast<float>(WindowHeight);
    
    OutMatrix.M[0][0] = 1.0f / (AspectRatio * TanHalfFOV);
    OutMatrix.M[1][1] = 1.0f / TanHalfFOV;
    OutMatrix.M[2][2] = FarClipPlane / (FarClipPlane - NearClipPlane);
    OutMatrix.M[2][3] = 1.0f;
    OutMatrix.M[3][2] = -(FarClipPlane * NearClipPlane) / (FarClipPlane - NearClipPlane);
}

void FCubeSceneRendererTest::LogStatistics()
{
    MR_LOG(LogCubeSceneTest, Log, "=== CubeSceneRendererTest Statistics ===");
    MR_LOG(LogCubeSceneTest, Log, "  Total cubes: %d", CubeCount);
    MR_LOG(LogCubeSceneTest, Log, "  Visible primitives: %d", NumVisiblePrimitives);
    MR_LOG(LogCubeSceneTest, Log, "  Draw calls: %d", NumDrawCalls);
    MR_LOG(LogCubeSceneTest, Log, "  Triangles: %d", NumTriangles);
    MR_LOG(LogCubeSceneTest, Log, "  Visibility time: %.3f ms", VisibilityTimeMs);
    MR_LOG(LogCubeSceneTest, Log, "  Draw command time: %.3f ms", DrawCommandTimeMs);
    MR_LOG(LogCubeSceneTest, Log, "=========================================");
}

} // namespace MonsterEngine
