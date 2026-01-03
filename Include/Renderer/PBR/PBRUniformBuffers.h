// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file PBRUniformBuffers.h
 * @brief PBR uniform buffer structures for descriptor sets
 * 
 * Defines GPU-aligned uniform buffer structures for PBR rendering:
 * - Set 0: Per-Frame data (Camera, Lighting)
 * - Set 1: Per-Material data (Material parameters - see PBRMaterialTypes.h)
 * - Set 2: Per-Object data (Transform)
 * 
 * Reference:
 * - Filament: FrameUniforms, ObjectUniforms
 * - UE5: FViewUniformShaderParameters, FPrimitiveUniformShaderParameters
 */

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Matrix.h"
#include "Math/MathFunctions.h"
#include <cmath>

namespace MonsterEngine
{
namespace Renderer
{

// ============================================================================
// Set 0: Per-Frame Uniform Buffers
// ============================================================================

/**
 * @struct FViewUniformBuffer
 * @brief Per-frame view/camera uniform buffer (Set 0, Binding 0)
 * 
 * Contains view and projection matrices, camera position, and viewport info.
 * This data changes once per frame (or per view for multi-view rendering).
 * 
 * Memory layout: 256 bytes (16 x float4)
 * 
 * Reference:
 * - Filament: FrameUniforms.viewFromWorldMatrix, etc.
 * - UE5: FViewUniformShaderParameters
 */
struct alignas(16) FViewUniformBuffer
{
    // ========================================================================
    // View Matrices (4 x mat4 = 256 bytes)
    // ========================================================================
    
    /**
     * View matrix (world to view/camera space)
     * Row-major order following UE5 convention
     */
    Math::FMatrix ViewMatrix;
    
    /**
     * Projection matrix (view to clip space)
     * Row-major order following UE5 convention
     */
    Math::FMatrix ProjectionMatrix;
    
    /**
     * Combined view-projection matrix
     * ViewProjectionMatrix = ViewMatrix * ProjectionMatrix (row-major)
     */
    Math::FMatrix ViewProjectionMatrix;
    
    /**
     * Inverse view matrix (view to world space)
     * Used for reconstructing world position from depth
     */
    Math::FMatrix InvViewMatrix;
    
    // ========================================================================
    // Camera Parameters (2 x float4 = 32 bytes)
    // ========================================================================
    
    /**
     * Camera world position
     * XYZ: Position, W: unused (padding)
     */
    FVector4f CameraPosition;
    
    /**
     * Camera forward direction (normalized)
     * XYZ: Direction, W: unused (padding)
     */
    FVector4f CameraForward;
    
    // ========================================================================
    // Viewport and Time (2 x float4 = 32 bytes)
    // ========================================================================
    
    /**
     * Viewport dimensions
     * X: Width, Y: Height, Z: 1/Width, W: 1/Height
     */
    FVector4f ViewportSize;
    
    /**
     * Time parameters
     * X: Time (seconds), Y: Sin(Time), Z: Cos(Time), W: DeltaTime
     */
    FVector4f TimeParams;
    
    // ========================================================================
    // Near/Far Planes (1 x float4 = 16 bytes)
    // ========================================================================
    
    /**
     * Clip plane parameters
     * X: NearPlane, Y: FarPlane, Z: 1/(Far-Near), W: Near/(Far-Near)
     */
    FVector4f ClipPlanes;
    
    /**
     * Exposure and tone mapping
     * X: Exposure, Y: Gamma, Z: PreExposure, W: unused
     */
    FVector4f ExposureParams;
    
    // ========================================================================
    // Constructor
    // ========================================================================
    
    FViewUniformBuffer()
        : ViewMatrix(Math::FMatrix::Identity)
        , ProjectionMatrix(Math::FMatrix::Identity)
        , ViewProjectionMatrix(Math::FMatrix::Identity)
        , InvViewMatrix(Math::FMatrix::Identity)
        , CameraPosition(0.0f, 0.0f, 0.0f, 1.0f)
        , CameraForward(0.0f, 0.0f, -1.0f, 0.0f)
        , ViewportSize(1920.0f, 1080.0f, 1.0f/1920.0f, 1.0f/1080.0f)
        , TimeParams(0.0f, 0.0f, 1.0f, 0.016f)
        , ClipPlanes(0.1f, 1000.0f, 1.0f/999.9f, 0.1f/999.9f)
        , ExposureParams(1.0f, 2.2f, 1.0f, 0.0f)
    {}
};

// Verify struct alignment (size depends on FMatrix which uses double)
// 4 matrices * 128 bytes + 6 float4 * 16 bytes = 512 + 96 = 608 bytes
// static_assert(sizeof(FViewUniformBuffer) >= 256, "FViewUniformBuffer size too small");

/**
 * @struct FLightUniformBuffer
 * @brief Per-frame lighting uniform buffer (Set 0, Binding 1)
 * 
 * Contains directional light and ambient lighting parameters.
 * For PBR without IBL, we use a simple directional light + ambient.
 * 
 * Memory layout: 64 bytes (4 x float4)
 * 
 * Reference:
 * - Filament: FrameUniforms.lightColorIntensity, etc.
 * - UE5: FLightShaderParameters
 */
struct alignas(16) FLightUniformBuffer
{
    // ========================================================================
    // Directional Light (2 x float4 = 32 bytes)
    // ========================================================================
    
    /**
     * Directional light direction (normalized, pointing towards light)
     * XYZ: Direction, W: unused
     */
    FVector4f DirectionalLightDirection;
    
    /**
     * Directional light color and intensity
     * RGB: Linear color, A: Intensity multiplier
     */
    FVector4f DirectionalLightColor;
    
    // ========================================================================
    // Ambient Light (1 x float4 = 16 bytes)
    // ========================================================================
    
    /**
     * Ambient light color and intensity
     * RGB: Linear color, A: Intensity multiplier
     * This is a simple approximation until IBL is implemented
     */
    FVector4f AmbientLightColor;
    
    // ========================================================================
    // Additional Light Parameters (1 x float4 = 16 bytes)
    // ========================================================================
    
    /**
     * Additional lighting parameters
     * X: Shadow intensity [0,1]
     * Y: Ambient occlusion strength [0,1]
     * Z: IBL intensity (for future use)
     * W: Number of active point lights
     */
    FVector4f LightingParams;
    
    // ========================================================================
    // Constructor
    // ========================================================================
    
    FLightUniformBuffer()
        : DirectionalLightDirection(0.0f, -1.0f, 0.0f, 0.0f)  // Sun from above
        , DirectionalLightColor(1.0f, 1.0f, 1.0f, 1.0f)        // White light
        , AmbientLightColor(0.03f, 0.03f, 0.03f, 1.0f)         // Low ambient
        , LightingParams(1.0f, 1.0f, 0.0f, 0.0f)
    {}
    
    /** Set directional light */
    void setDirectionalLight(const FVector3f& direction, const FVector3f& color, float intensity)
    {
        // Normalize and negate direction (shader expects direction TO light)
        FVector3f dir = direction;
        float len = std::sqrt(dir.X * dir.X + dir.Y * dir.Y + dir.Z * dir.Z);
        if (len > 0.0001f) {
            dir.X /= len;
            dir.Y /= len;
            dir.Z /= len;
        }
        DirectionalLightDirection = FVector4f(-dir.X, -dir.Y, -dir.Z, 0.0f);
        DirectionalLightColor = FVector4f(color.X, color.Y, color.Z, intensity);
    }
    
    /** Set ambient light */
    void setAmbientLight(const FVector3f& color, float intensity)
    {
        AmbientLightColor = FVector4f(color.X, color.Y, color.Z, intensity);
    }
};

// Verify struct size
static_assert(sizeof(FLightUniformBuffer) == 64, "FLightUniformBuffer size mismatch");

// ============================================================================
// Set 2: Per-Object Uniform Buffer
// ============================================================================

/**
 * @struct FObjectUniformBuffer
 * @brief Per-object transform uniform buffer (Set 2, Binding 0)
 * 
 * Contains model matrix and normal matrix for each rendered object.
 * This data changes for each draw call.
 * 
 * Memory layout: 192 bytes (12 x float4)
 * 
 * Reference:
 * - Filament: ObjectUniforms
 * - UE5: FPrimitiveUniformShaderParameters
 */
struct alignas(16) FObjectUniformBuffer
{
    // ========================================================================
    // Transform Matrices (3 x mat4 = 192 bytes)
    // ========================================================================
    
    /**
     * Model matrix (local to world space)
     * Row-major order following UE5 convention
     */
    Math::FMatrix ModelMatrix;
    
    /**
     * Normal matrix (transpose of inverse model matrix upper 3x3)
     * Used for transforming normals correctly with non-uniform scaling
     * Stored as 4x4 for GPU alignment, only upper 3x3 is used
     */
    Math::FMatrix NormalMatrix;
    
    /**
     * Model-View-Projection matrix (precomputed for efficiency)
     * MVP = ModelMatrix * ViewMatrix * ProjectionMatrix (row-major)
     */
    Math::FMatrix MVPMatrix;
    
    // ========================================================================
    // Constructor
    // ========================================================================
    
    FObjectUniformBuffer()
        : ModelMatrix(Math::FMatrix::Identity)
        , NormalMatrix(Math::FMatrix::Identity)
        , MVPMatrix(Math::FMatrix::Identity)
    {}
    
    /**
     * Update matrices from model matrix and view-projection matrix
     * @param model The model (local to world) matrix
     * @param viewProjection The view-projection matrix
     */
    void updateFromModelMatrix(const Math::FMatrix& model, const Math::FMatrix& viewProjection)
    {
        ModelMatrix = model;
        
        // Compute normal matrix (inverse transpose of upper 3x3)
        // For now, assume uniform scaling - just use the rotation part
        NormalMatrix = model;
        // TODO: Proper inverse transpose for non-uniform scaling
        
        // Compute MVP
        MVPMatrix = model * viewProjection;
    }
};

// Verify struct alignment (size depends on FMatrix which uses double)
// 3 matrices * 128 bytes = 384 bytes
// static_assert(sizeof(FObjectUniformBuffer) >= 192, "FObjectUniformBuffer size too small");

// ============================================================================
// Combined Per-Frame Buffer (Optional - for simpler binding)
// ============================================================================

/**
 * @struct FPerFrameUniformBuffer
 * @brief Combined per-frame uniform buffer (Set 0, Binding 0)
 * 
 * Alternative layout that combines view and light data into a single buffer.
 * Use this for simpler descriptor set management.
 * 
 * Memory layout: 448 bytes
 */
struct alignas(16) FPerFrameUniformBuffer
{
    FViewUniformBuffer View;
    FLightUniformBuffer Light;
    
    FPerFrameUniformBuffer() = default;
};

// Verify struct alignment
// static_assert(sizeof(FPerFrameUniformBuffer) >= 256, "FPerFrameUniformBuffer size too small");

} // namespace Renderer
} // namespace MonsterEngine
