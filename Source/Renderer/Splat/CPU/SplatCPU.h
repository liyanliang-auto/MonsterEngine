// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SplatCPU.h
 * @brief CPU validation baseline for the full 3DGS math pipeline
 * 
 * References: preprocess.comp + render.comp
 * Pipeline: PLY parse -> 3D covariance -> frustum cull -> Jacobian projection ->
 *           2D conic -> 3-sigma radius -> SH color -> depth sort -> Alpha Blend -> output
 */

#include "Core/CoreTypes.h"
#include "Containers/Array.h"

#include <cstdint>
#include <cmath>

namespace MonsterRender
{

// ============================================================================
// Step 1.1: Data Structures
// ============================================================================

/** Raw Gaussian data parsed from .ply file */
struct GaussianPointRaw
{
    float32 position[3];   // x, y, z
    float32 scale[3];      // activated via exp()
    float32 rotation[4];   // quaternion (w, x, y, z), normalized
    float32 opacity;       // activated via sigmoid
    float32 shCoeffs[48];  // SH coefficients: 3*16 = 48, reordered as (dc_r,dc_g,dc_b, r0,g0,b0, r1,g1,b1, ...)
    int32 shDegree;        // SH degree (0-3)
};

/** Processed Gaussian data (matches preprocess.comp output buffers) */
struct GaussianPointProcessed
{
    float32 pointImage[2];     // screen-space coordinates (x, y)
    float32 conicOpacity[4];   // conic[3] + opacity
    float32 rgb[3];            // SH-computed color
    float32 depth;             // view-space depth (for sorting)
    int32 radius;              // 3-sigma radius
    int32 rectMin[2];          // tile range min
    int32 rectMax[2];          // tile range max
    int32 tilesTouched;        // number of tiles covered
};

/** Camera parameters (matches preprocess.comp CameraUniforms) */
struct SplatCamera
{
    float32 viewMatrix[16];      // 4x4 view matrix (column-major, GLM style)
    float32 projMatrix[16];      // 4x4 projection matrix (column-major)
    float32 camPos[3];           // camera world-space position
    float32 focalX;              // focal length X
    float32 focalY;              // focal length Y
    float32 tanFovX;             // tan(fovX/2)
    float32 tanFovY;             // tan(fovY/2)
    int32 imageWidth;            // output image width
    int32 imageHeight;           // output image height
    int32 shDegree;              // SH degree
};

/** CPU validation pipeline */
class SplatCPU
{
public:
    SplatCPU() = default;
    ~SplatCPU() = default;

    /** Load Gaussian data from a .ply file (binary, little-endian) */
    bool loadPLY(const String& filePath);

    /** Run the full preprocessing + rendering pipeline on CPU */
    bool runPipeline(const SplatCamera& camera);

    /** Save the rendered image to a PPM file */
    bool saveImage(const String& filePath) const;

    // ---- Accessors ----
    const MonsterEngine::TArray<GaussianPointRaw>& getRawPoints() const { return m_rawPoints; }
    const MonsterEngine::TArray<GaussianPointProcessed>& getProcessedPoints() const { return m_processed; }
    const MonsterEngine::TArray<float32>& getImageData() const { return m_imageData; }
    int32 getWidth() const { return m_width; }
    int32 getHeight() const { return m_height; }
    int64 getNumPoints() const { return m_rawPoints.Num(); }
    int32 getSHDegree() const { return m_shDegree; }

private:
    /**
     * Compute the 3D covariance matrix for a single Gaussian.
     * Sigma = (R * S)^T * (R * S), stored as upper triangle [xx, xy, xz, yy, yz, zz]
     */
    static void computeCov3D(const GaussianPointRaw& raw, float32 scaleModifier, float32 cov3D[6]);

    /**
     * Frustum culling: world -> view -> clip space.
     * @param raw The raw Gaussian point data
     * @param cam Camera parameters
     * @param nearPlane Near clip distance
     * @param farPlane Far clip distance
     * @param viewPos Output: view-space position of the Gaussian
     * @return true if the point is inside the frustum
     */
    static bool inFrustum(const GaussianPointRaw& raw, const SplatCamera& cam, float32 nearPlane, float32 farPlane, float32 viewPos[3]);

    /**
     * Jacobian projection + 2D conic + 3-sigma radius.
     * Combines Steps 1.5 and 1.6 of the 3DGS pipeline.
     */
    static bool computeCov2D(const float32 viewPos[3], const float32 cov3D[6], const SplatCamera& cam,
                             float32 pointImage[2], float32 conicOpacity[4], int32& radius,
                             int32 rectMin[2], int32 rectMax[2], int32& tilesTouched);

    /** Evaluate color from spherical harmonics (degree 0-3) */
    static void computeColorFromSH(const GaussianPointRaw& raw, const SplatCamera& cam, float32 rgb[3]);

    /** Parse the PLY file header to extract vertex count */
    bool parseHeader(std::ifstream& file);

    // ---- Data ----
    MonsterEngine::TArray<GaussianPointRaw> m_rawPoints;
    MonsterEngine::TArray<GaussianPointProcessed> m_processed;
    MonsterEngine::TArray<float32> m_imageData;  // RGBA pixel data (width x height x 4)
    int32 m_width = 0;
    int32 m_height = 0;

    int32 m_numVertices = 0;
    int32 m_shDegree = 0;
    float32 m_nearPlane = 0.01f;
    float32 m_farPlane = 1000.0f;
    int32 BLOCK_X = 16;
    int32 BLOCK_Y = 16;
};

} // namespace MonsterRender
