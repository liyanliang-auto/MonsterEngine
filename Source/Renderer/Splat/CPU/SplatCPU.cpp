// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SplatCPU.cpp
 * @brief Implementation of the CPU 3DGS validation pipeline
 * 
 * References:
 *   - 3DGS.cpp/src/GSScene.cpp            : PLY parsing
 *   - preprocess.comp (3dgs-vulkan-cpp)   : Cov3D / Frustum / Jacobian / Conic / SH
 *   - render.comp    (3DGS.cpp)           : Alpha Blend
 */

#include "SplatCPU.h"

#include "Core/Logging/LogMacros.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <chrono>

namespace MonsterRender
{

// ============================================================================
// SH Constants (matches preprocess.comp L6-23)
// ============================================================================
static constexpr float32 SH_C0 = 0.28209479177387814f;
static constexpr float32 SH_C1 = 0.4886025119029199f;
static constexpr float32 SH_C2[5] = {
    1.0925484305920792f,
    -1.0925484305920792f,
    0.31539156525252005f,
    -1.0925484305920792f,
    0.5462742152960396f
};
static constexpr float32 SH_C3[7] = {
    -0.5900435899266435f,
    2.890611442640554f,
    -0.4570457994644658f,
    0.3731763325901154f,
    -0.4570457994644658f,
    1.445305721320277f,
    -0.5900435899266435f
};

// ============================================================================
// Step 1.2: .ply Parsing (matches GSScene.cpp)
// ============================================================================

bool SplatCPU::parseHeader(std::ifstream& plyFile)
{
    std::string line;
    bool headerEnd = false;

    while (std::getline(plyFile, line))
    {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "ply")
        {
            // PLY format indicator, skip
        }
        else if (token == "format")
        {
            std::string format;
            iss >> format;
            if (format != "binary_little_endian")
            {
                MR_LOG(LogTemp, Warning, "[SplatCPU] PLY format is '%s', expected 'binary_little_endian'", format.c_str());
            }
        }
        else if (token == "element")
        {
            std::string elemType;
            int32 count;
            iss >> elemType >> count;
            if (elemType == "vertex")
            {
                m_numVertices = count;
            }
        }
        else if (token == "property")
        {
            // Skip property definitions; vertex data is read with a fixed layout
        }
        else if (token == "end_header")
        {
            headerEnd = true;
            break;
        }
    }

    if (!headerEnd)
    {
        MR_LOG(LogTemp, Error, "[SplatCPU] Could not find 'end_header' in PLY file");
        return false;
    }
    if (m_numVertices <= 0)
    {
        MR_LOG(LogTemp, Error, "[SplatCPU] No vertices found in PLY file");
        return false;
    }
    return true;
}

bool SplatCPU::loadPLY(const String& filePath)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    std::ifstream plyFile(filePath, std::ios::binary);
    if (!plyFile.is_open())
    {
        MR_LOG(LogTemp, Error, "[SplatCPU] Cannot open file: %s", filePath.c_str());
        return false;
    }

    // Parse header
    if (!parseHeader(plyFile))
    {
        return false;
    }

    // Binary data follows immediately after "end_header\n" plus one newline

    m_rawPoints.Empty();
    m_rawPoints.Reserve(m_numVertices);

    // Determine SH degree by inspecting the first vertex.
    // Each vertex = 62 floats (3 pos + 3 normal + 3 dc + 45 rest + 1 opacity + 3 scale + 4 rot)

    struct VertexStorage
    {
        float32 position[3];
        float32 normal[3];
        float32 f_dc[3];
        float32 f_rest[45];
        float32 opacity;
        float32 scale[3];
        float32 rotation[4];
    };
    static_assert(sizeof(VertexStorage) == 62 * sizeof(float32), "VertexStorage must be 62 floats");

    // Read the first vertex to detect the actual SH degree
    if (m_numVertices > 0)
    {
        VertexStorage first;
        plyFile.read(reinterpret_cast<char*>(&first), sizeof(VertexStorage));

        // Detect SH degree by counting non-zero f_rest coefficients
        int32 maxRestNeeded = 0;
        for (int32 i = 0; i < 45; i++)
        {
            if (std::abs(first.f_rest[i]) > 1e-10f)
            {
                maxRestNeeded = i + 1;
            }
        }

        // SH degree N -> (N+1)^2 coefficients per color channel:
        //   degree 0 ->  1 coeff  (dc only)
        //   degree 1 ->  4 coeffs (dc + 3)
        //   degree 2 ->  9 coeffs (dc + 3 + 5)
        //   degree 3 -> 16 coeffs (dc + 3 + 5 + 7)
        // rest[45] + dc[3] = 48 total -> 48/3 = 16 coeffs per channel -> degree 3 max

        if (maxRestNeeded <= 0)
            m_shDegree = 0;
        else if (maxRestNeeded <= 8)   // degree 1: 3 rest per channel = 9 total rest
            m_shDegree = 1;
        else if (maxRestNeeded <= 24)  // degree 2: 8 per channel = 24 total rest
            m_shDegree = 2;
        else
            m_shDegree = 3;

        // Process the first vertex
        {
            GaussianPointRaw gp;
            gp.position[0] = first.position[0];
            gp.position[1] = first.position[1];
            gp.position[2] = first.position[2];

            // Activation functions
            gp.scale[0] = std::exp(first.scale[0]);
            gp.scale[1] = std::exp(first.scale[1]);
            gp.scale[2] = std::exp(first.scale[2]);
            gp.opacity = 1.0f / (1.0f + std::exp(-first.opacity));

            // Normalize quaternion
            float32 qlen = std::sqrt(first.rotation[0] * first.rotation[0] +
                                     first.rotation[1] * first.rotation[1] +
                                     first.rotation[2] * first.rotation[2] +
                                     first.rotation[3] * first.rotation[3]);
            if (qlen > 1e-10f)
            {
                gp.rotation[0] = first.rotation[0] / qlen; // w
                gp.rotation[1] = first.rotation[1] / qlen; // x
                gp.rotation[2] = first.rotation[2] / qlen; // y
                gp.rotation[3] = first.rotation[3] / qlen; // z
            }
            else
            {
                gp.rotation[0] = 1.0f;
                gp.rotation[1] = 0.0f;
                gp.rotation[2] = 0.0f;
                gp.rotation[3] = 0.0f;
            }

            // SH coefficient reordering:
            //   PLY layout: [dc_r, dc_g, dc_b, rest_r0..rest_r14, rest_g0..rest_g14, rest_b0..rest_b14]
            //   Output layout: [dc_r, dc_g, dc_b, r0,g0,b0, r1,g1,b1, ...]
            gp.shDegree = m_shDegree;
            gp.shCoeffs[0] = first.f_dc[0];
            gp.shCoeffs[1] = first.f_dc[1];
            gp.shCoeffs[2] = first.f_dc[2];

            constexpr int32 SH_N = 16; // degree 3: 16 coeffs per channel
            for (int32 j = 1; j < SH_N; j++)
            {
                gp.shCoeffs[j * 3 + 0] = first.f_rest[(j - 1) + 3];
                gp.shCoeffs[j * 3 + 1] = first.f_rest[(j - 1) + SH_N + 2];
                gp.shCoeffs[j * 3 + 2] = first.f_rest[(j - 1) + SH_N * 2 + 1];
            }

            m_rawPoints.Add(gp);
        }

        // Read remaining vertices
        for (int32 i = 1; i < m_numVertices; i++)
        {
            VertexStorage vs;
            plyFile.read(reinterpret_cast<char*>(&vs), sizeof(VertexStorage));

            GaussianPointRaw gp;
            gp.position[0] = vs.position[0];
            gp.position[1] = vs.position[1];
            gp.position[2] = vs.position[2];

            gp.scale[0] = std::exp(vs.scale[0]);
            gp.scale[1] = std::exp(vs.scale[1]);
            gp.scale[2] = std::exp(vs.scale[2]);
            gp.opacity = 1.0f / (1.0f + std::exp(-vs.opacity));

            float32 qlen = std::sqrt(vs.rotation[0] * vs.rotation[0] +
                                     vs.rotation[1] * vs.rotation[1] +
                                     vs.rotation[2] * vs.rotation[2] +
                                     vs.rotation[3] * vs.rotation[3]);
            if (qlen > 1e-10f)
            {
                gp.rotation[0] = vs.rotation[0] / qlen;
                gp.rotation[1] = vs.rotation[1] / qlen;
                gp.rotation[2] = vs.rotation[2] / qlen;
                gp.rotation[3] = vs.rotation[3] / qlen;
            }
            else
            {
                gp.rotation[0] = 1.0f; gp.rotation[1] = 0.0f; gp.rotation[2] = 0.0f; gp.rotation[3] = 0.0f;
            }

            gp.shDegree = m_shDegree;
            gp.shCoeffs[0] = vs.f_dc[0];
            gp.shCoeffs[1] = vs.f_dc[1];
            gp.shCoeffs[2] = vs.f_dc[2];

            for (int32 j = 1; j < SH_N; j++)
            {
                gp.shCoeffs[j * 3 + 0] = vs.f_rest[(j - 1) + 3];
                gp.shCoeffs[j * 3 + 1] = vs.f_rest[(j - 1) + SH_N + 2];
                gp.shCoeffs[j * 3 + 2] = vs.f_rest[(j - 1) + SH_N * 2 + 1];
            }

            m_rawPoints.Add(gp);
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    MR_LOG(LogTemp, Log, "[SplatCPU] Loaded %d Gaussians from %s in %lldms (SH degree %d)",
           m_numVertices, filePath.c_str(), ms, m_shDegree);

    return true;
}

// ============================================================================
// Step 1.3: 3D Covariance (matches preprocess.comp::computeCov3D)
// ============================================================================

void SplatCPU::computeCov3D(const GaussianPointRaw& raw, float32 scaleModifier, float32 cov3D[6])
{
    // S = diag(scaleModifier * scale)
    float32 sx = scaleModifier * raw.scale[0];
    float32 sy = scaleModifier * raw.scale[1];
    float32 sz = scaleModifier * raw.scale[2];

    // Quaternion -> rotation matrix
    // quaternion: w=raw.rotation[0], x=raw.rotation[1], y=raw.rotation[2], z=raw.rotation[3]
    // matches preprocess.comp L291-298: r=q.x, x=q.y, y=q.z, z=q.w
    // Here q=(w,x,y,z), mapping: r=w, x=x, y=y, z=z
    float32 r = raw.rotation[0]; // w
    float32 x = raw.rotation[1]; // x
    float32 y = raw.rotation[2]; // y
    float32 z = raw.rotation[3]; // z

    // Rotation matrix (row-major)
    float32 R00 = 1.0f - 2.0f * (y * y + z * z);
    float32 R01 = 2.0f * (x * y - r * z);
    float32 R02 = 2.0f * (x * z + r * y);
    float32 R10 = 2.0f * (x * y + r * z);
    float32 R11 = 1.0f - 2.0f * (x * x + z * z);
    float32 R12 = 2.0f * (y * z - r * x);
    float32 R20 = 2.0f * (x * z - r * y);
    float32 R21 = 2.0f * (y * z + r * x);
    float32 R22 = 1.0f - 2.0f * (x * x + y * y);

    // M = S * R
    float32 M00 = sx * R00, M01 = sx * R01, M02 = sx * R02;
    float32 M10 = sy * R10, M11 = sy * R11, M12 = sy * R12;
    float32 M20 = sz * R20, M21 = sz * R21, M22 = sz * R22;

    // Sigma = M^T * M (store upper triangle only)
    float32 S00 = M00 * M00 + M10 * M10 + M20 * M20;
    float32 S01 = M00 * M01 + M10 * M11 + M20 * M21;
    float32 S02 = M00 * M02 + M10 * M12 + M20 * M22;
    float32 S11 = M01 * M01 + M11 * M11 + M21 * M21;
    float32 S12 = M01 * M02 + M11 * M12 + M21 * M22;
    float32 S22 = M02 * M02 + M12 * M12 + M22 * M22;

    cov3D[0] = S00;  // xx
    cov3D[1] = S01;  // xy
    cov3D[2] = S02;  // xz
    cov3D[3] = S11;  // yy
    cov3D[4] = S12;  // yz
    cov3D[5] = S22;  // zz
}

// ============================================================================
// Step 1.4: Frustum Culling (matches preprocess.comp::inFrustum)
// ============================================================================

bool SplatCPU::inFrustum(const GaussianPointRaw& raw, const SplatCamera& cam,
                         float32 nearPlane, float32 farPlane, float32 viewPos[3])
{
    // World -> view space
    // pView = mat3(viewMatrix) * pos + viewMatrix[3].xyz
    const float32* V = cam.viewMatrix; // column-major 4x4
    float32 px = raw.position[0];
    float32 py = raw.position[1];
    float32 pz = raw.position[2];

    viewPos[0] = V[0] * px + V[4] * py + V[8]  * pz + V[12];
    viewPos[1] = V[1] * px + V[5] * py + V[9]  * pz + V[13];
    viewPos[2] = V[2] * px + V[6] * py + V[10] * pz + V[14];

    // Near / Far culling (view-space z is negative, near plane has larger z)
    if (viewPos[2] >= -nearPlane) return false;
    if (viewPos[2] <  -farPlane)  return false;

    // Clip-space culling
    const float32* P = cam.projMatrix;
    float32 cx = P[0] * viewPos[0] + P[4] * viewPos[1] + P[8]  * viewPos[2] + P[12];
    float32 cy = P[1] * viewPos[0] + P[5] * viewPos[1] + P[9]  * viewPos[2] + P[13];
    float32 cw = P[3] * viewPos[0] + P[7] * viewPos[1] + P[11] * viewPos[2] + P[15];

    return (std::abs(cx) <= cw) && (std::abs(cy) <= cw);
}

// ============================================================================
// Step 1.5 + 1.6: Jacobian Projection + Conic + 3-sigma (matches preprocess.comp L241-382)
// ============================================================================

bool SplatCPU::computeCov2D(const float32 viewPos[3], const float32 cov3D[6], const SplatCamera& cam,
                            float32 pointImage[2], float32 conicOpacity[4], int32& radius,
                            int32 rectMin[2], int32 rectMax[2], int32& tilesTouched)
{
    float32 tx = viewPos[0];
    float32 ty = viewPos[1];
    float32 tz = viewPos[2];

    // Clamp to frustum
    float32 limx = 1.3f * cam.tanFovX;
    float32 limy = 1.3f * cam.tanFovY;
    float32 txtz = tx / tz;
    float32 tytz = ty / tz;
    tx = std::max(-limx, std::min(limx, txtz)) * tz;
    ty = std::max(-limy, std::min(limy, tytz)) * tz;

    // Jacobian matrix J = [fx/tz, 0, -fx*tx/tz^2; 0, fy/tz, -fy*ty/tz^2; 0, 0, 0]
    float32 tz2 = tz * tz;
    float32 J00 = cam.focalX / tz;
    float32 J02 = -(cam.focalX * tx) / tz2;
    float32 J11 = cam.focalY / tz;
    float32 J12 = -(cam.focalY * ty) / tz2;

    // W = transpose(mat3(viewMatrix)) -> extract rotation from viewMatrix upper-left 3x3
    const float32* V = cam.viewMatrix;
    float32 W00 = V[0], W01 = V[4], W02 = V[8];
    float32 W10 = V[1], W11 = V[5], W12 = V[9];
    float32 W20 = V[2], W21 = V[6], W22 = V[10];

    // T = W * J
    float32 T00 = W00 * J00 + W01 * 0.0f + W02 * 0.0f;
    float32 T01 = W00 * 0.0f + W01 * J11 + W02 * 0.0f;
    float32 T02 = W00 * J02 + W01 * J12 + W02 * 0.0f;
    float32 T10 = W10 * J00 + W11 * 0.0f + W12 * 0.0f;
    float32 T11 = W10 * 0.0f + W11 * J11 + W12 * 0.0f;
    float32 T12 = W10 * J02 + W11 * J12 + W12 * 0.0f;
    float32 T20 = W20 * J00 + W21 * 0.0f + W22 * 0.0f;
    float32 T21 = W20 * 0.0f + W21 * J11 + W22 * 0.0f;
    float32 T22 = W20 * J02 + W21 * J12 + W22 * 0.0f;

    // Vrk = 3x3 from cov3D upper triangle
    float32 V00 = cov3D[0], V01 = cov3D[1], V02 = cov3D[2];
    float32 V11 = cov3D[3], V12 = cov3D[4];
    float32 V22 = cov3D[5];

    // cov = T^T * Vrk * T  ->  compute Vrk * T first
    float32 VT00 = V00 * T00 + V01 * T10 + V02 * T20;
    float32 VT01 = V00 * T01 + V01 * T11 + V02 * T21;
    float32 VT02 = V00 * T02 + V01 * T12 + V02 * T22;
    float32 VT10 = V01 * T00 + V11 * T10 + V12 * T20;
    float32 VT11 = V01 * T01 + V11 * T11 + V12 * T21;
    float32 VT12 = V01 * T02 + V11 * T12 + V12 * T22;
    float32 VT20 = V02 * T00 + V12 * T10 + V22 * T20;
    float32 VT21 = V02 * T01 + V12 * T11 + V22 * T21;
    float32 VT22 = V02 * T02 + V12 * T12 + V22 * T22;

    // cov = T^T * (Vrk * T)
    float32 cov00 = T00 * VT00 + T10 * VT10 + T20 * VT20;
    float32 cov01 = T00 * VT01 + T10 * VT11 + T20 * VT21;
    float32 cov11 = T01 * VT01 + T11 * VT11 + T21 * VT21;

    // Low-pass filter (matches preprocess.comp L273-274)
    cov00 += 0.3f;
    cov11 += 0.3f;

    // 2D conic (EWA algorithm, matches preprocess.comp L353-358)
    float32 det = cov00 * cov11 - cov01 * cov01;
    if (std::abs(det) < 1e-10f) return false;

    float32 detInv = 1.0f / det;
    conicOpacity[0] = cov11 * detInv;   // conic.x
    conicOpacity[1] = -cov01 * detInv;  // conic.y
    conicOpacity[2] = cov00 * detInv;   // conic.z
    // conicOpacity[3] = opacity -> set by caller

    // 3-sigma radius (matches preprocess.comp L361-364)
    float32 mid = 0.5f * (cov00 + cov11);
    float32 lambda1 = mid + std::sqrt(std::max(0.1f, mid * mid - det));
    float32 lambda2 = mid - std::sqrt(std::max(0.1f, mid * mid - det));
    radius = static_cast<int32>(std::ceil(3.0f * std::sqrt(std::max(lambda1, lambda2))));

    // Screen-space coordinates (matches preprocess.comp L109-111, L366-368)
    // NDC -> pixels: ((ndc + 1) * size - 1) * 0.5
    const float32* P = cam.projMatrix;
    float32 pHomW = P[3] * viewPos[0] + P[7] * viewPos[1] + P[11] * viewPos[2] + P[15];
    if (std::abs(pHomW) < 1e-10f) return false;
    float32 pW = 1.0f / pHomW;
    float32 pProjX = (P[0] * viewPos[0] + P[4] * viewPos[1] + P[8]  * viewPos[2] + P[12]) * pW;
    float32 pProjY = (P[1] * viewPos[0] + P[5] * viewPos[1] + P[9]  * viewPos[2] + P[13]) * pW;

    pointImage[0] = ((pProjX + 1.0f) * cam.imageWidth  - 1.0f) * 0.5f;
    pointImage[1] = ((pProjY + 1.0f) * cam.imageHeight - 1.0f) * 0.5f;

    // Tile range (matches preprocess.comp L371-382)
    int32 BLOCK_X = 16, BLOCK_Y = 16;
    int32 gridX = (cam.imageWidth  + BLOCK_X - 1) / BLOCK_X;
    int32 gridY = (cam.imageHeight + BLOCK_Y - 1) / BLOCK_Y;

    rectMin[0] = std::min(gridX, std::max(0, static_cast<int32>((pointImage[0] - radius) / BLOCK_X)));
    rectMin[1] = std::min(gridY, std::max(0, static_cast<int32>((pointImage[1] - radius) / BLOCK_Y)));
    rectMax[0] = std::min(gridX, std::max(0, static_cast<int32>((pointImage[0] + radius + BLOCK_X - 1) / BLOCK_X)));
    rectMax[1] = std::min(gridY, std::max(0, static_cast<int32>((pointImage[1] + radius + BLOCK_Y - 1) / BLOCK_Y)));

    int32 tilesW = rectMax[0] - rectMin[0];
    int32 tilesH = rectMax[1] - rectMin[1];
    tilesTouched = tilesW * tilesH;

    return (tilesTouched > 0);
}

// ============================================================================
// Step 1.7: SH Color Evaluation (matches preprocess.comp::computeColorFromSH)
// ============================================================================

static int32 getSHCoeffCount(int32 degree)
{
    if (degree == 0) return 1;
    if (degree == 1) return 4;
    if (degree == 2) return 9;
    if (degree == 3) return 16;
    return 1;
}

void SplatCPU::computeColorFromSH(const GaussianPointRaw& raw, const SplatCamera& cam, float32 rgb[3])
{
    // Direction vector: from camera to Gaussian
    float32 dx = raw.position[0] - cam.camPos[0];
    float32 dy = raw.position[1] - cam.camPos[1];
    float32 dz = raw.position[2] - cam.camPos[2];
    float32 dirLen = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dirLen < 1e-10f)
    {
        rgb[0] = rgb[1] = rgb[2] = 0.5f;
        return;
    }
    dx /= dirLen; dy /= dirLen; dz /= dirLen;

    int32 shDegree = cam.shDegree > 0 ? cam.shDegree : raw.shDegree;
    int32 shBase = 0; // idx * coeffCount; idx=0 for single-Gaussian permutation

    // Degree 0
    rgb[0] = SH_C0 * raw.shCoeffs[shBase * 3 + 0];
    rgb[1] = SH_C0 * raw.shCoeffs[shBase * 3 + 1];
    rgb[2] = SH_C0 * raw.shCoeffs[shBase * 3 + 2];

    if (shDegree > 0)
    {
        float32 x = dx, y = dy, z = dz;

        // Degree 1: -SH_C1*y*sh[1] + SH_C1*z*sh[2] - SH_C1*x*sh[3]
        rgb[0] += -SH_C1 * y * raw.shCoeffs[(shBase + 1) * 3 + 0]
                 + SH_C1 * z * raw.shCoeffs[(shBase + 2) * 3 + 0]
                 - SH_C1 * x * raw.shCoeffs[(shBase + 3) * 3 + 0];
        rgb[1] += -SH_C1 * y * raw.shCoeffs[(shBase + 1) * 3 + 1]
                 + SH_C1 * z * raw.shCoeffs[(shBase + 2) * 3 + 1]
                 - SH_C1 * x * raw.shCoeffs[(shBase + 3) * 3 + 1];
        rgb[2] += -SH_C1 * y * raw.shCoeffs[(shBase + 1) * 3 + 2]
                 + SH_C1 * z * raw.shCoeffs[(shBase + 2) * 3 + 2]
                 - SH_C1 * x * raw.shCoeffs[(shBase + 3) * 3 + 2];

        if (shDegree > 1)
        {
            float32 xx = x * x, yy = y * y, zz = z * z;
            float32 xy = x * y, yz = y * z, xz = x * z;

            // Degree 2: sh[4]..sh[8]
            for (int32 ch = 0; ch < 3; ch++)
            {
                rgb[ch] += SH_C2[0] * xy * raw.shCoeffs[(shBase + 4) * 3 + ch]
                         + SH_C2[1] * yz * raw.shCoeffs[(shBase + 5) * 3 + ch]
                         + SH_C2[2] * (2.0f * zz - xx - yy) * raw.shCoeffs[(shBase + 6) * 3 + ch]
                         + SH_C2[3] * xz * raw.shCoeffs[(shBase + 7) * 3 + ch]
                         + SH_C2[4] * (xx - yy) * raw.shCoeffs[(shBase + 8) * 3 + ch];
            }

            if (shDegree > 2)
            {
                // Degree 3: sh[9]..sh[15]
                for (int32 ch = 0; ch < 3; ch++)
                {
                    rgb[ch] += SH_C3[0] * y * (3.0f * xx - yy) * raw.shCoeffs[(shBase + 9) * 3 + ch]
                             + SH_C3[1] * xy * z * raw.shCoeffs[(shBase + 10) * 3 + ch]
                             + SH_C3[2] * y * (4.0f * zz - xx - yy) * raw.shCoeffs[(shBase + 11) * 3 + ch]
                             + SH_C3[3] * z * (2.0f * zz - 3.0f * xx - 3.0f * yy) * raw.shCoeffs[(shBase + 12) * 3 + ch]
                             + SH_C3[4] * x * (4.0f * zz - xx - yy) * raw.shCoeffs[(shBase + 13) * 3 + ch]
                             + SH_C3[5] * z * (xx - yy) * raw.shCoeffs[(shBase + 14) * 3 + ch]
                             + SH_C3[6] * x * (xx - 3.0f * yy) * raw.shCoeffs[(shBase + 15) * 3 + ch];
                }
            }
        }
    }

    // result += 0.5; max(result, 0.0)
    rgb[0] = std::max(0.0f, rgb[0] + 0.5f);
    rgb[1] = std::max(0.0f, rgb[1] + 0.5f);
    rgb[2] = std::max(0.0f, rgb[2] + 0.5f);
}

// ============================================================================
// Steps 1.2-1.8: Full Pipeline
// ============================================================================

bool SplatCPU::runPipeline(const SplatCamera& camera)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    m_width = camera.imageWidth;
    m_height = camera.imageHeight;

    m_processed.Empty();
    m_processed.Reserve(static_cast<int64>(m_rawPoints.Num()));

    // Steps 1.3-1.7: Process each Gaussian
    int32 culledCount = 0;
    int32 zeroRadiusCount = 0;

    for (int64 i = 0; i < m_rawPoints.Num(); i++)
    {
        const auto& raw = m_rawPoints[i];

        // Step 1.3: 3D covariance
        float32 cov3D[6];
        computeCov3D(raw, 1.0f, cov3D);

        // Step 1.4: Frustum culling
        float32 viewPos[3];
        if (!inFrustum(raw, camera, m_nearPlane, m_farPlane, viewPos))
        {
            culledCount++;
            continue;
        }

        // Steps 1.5-1.6: Jacobian + Conic + 3-sigma
        GaussianPointProcessed gp;
        if (!computeCov2D(viewPos, cov3D, camera,
                          gp.pointImage, gp.conicOpacity, gp.radius,
                          gp.rectMin, gp.rectMax, gp.tilesTouched))
        {
            zeroRadiusCount++;
            continue;
        }

        // Step 1.7: SH color
        computeColorFromSH(raw, camera, gp.rgb);

        // Set opacity
        gp.conicOpacity[3] = raw.opacity;

        // Depth (positive = further away)
        gp.depth = -viewPos[2];

        m_processed.Add(gp);
    }

    MR_LOG(LogTemp, Log, "[SplatCPU] Culled: %d, zero-radius: %d, remaining: %lld",
           culledCount, zeroRadiusCount, m_processed.Num());

    // Step 1.8: Depth sorting (far to near)
    MonsterEngine::TArray<int32> sortedIndices;
    sortedIndices.Reserve(m_processed.Num());
    for (int64 i = 0; i < m_processed.Num(); i++)
        sortedIndices.Add(static_cast<int32>(i));

    std::sort(sortedIndices.begin(), sortedIndices.end(),
              [this](int32 a, int32 b) {
                  return m_processed[a].depth > m_processed[b].depth; // far to near
              });

    // Step 1.8: Alpha Blend
    // imageData: RGBA, A channel stores transmittance T (1 = fully transparent)
    int64 pixelCount = static_cast<int64>(m_width) * static_cast<int64>(m_height);
    m_imageData.SetNum(pixelCount * 4);
    // Initialize transmittance to 1
    for (int64 i = 0; i < pixelCount; i++)
        m_imageData[i * 4 + 3] = 1.0f;

    for (int32 idx : sortedIndices)
    {
        const auto& gp = m_processed[idx];

        if (gp.radius <= 0)
            continue;

        int32 pxMin = std::max(0, static_cast<int32>(gp.pointImage[0] - gp.radius));
        int32 pxMax = std::min(m_width,  static_cast<int32>(gp.pointImage[0] + gp.radius + 1));
        int32 pyMin = std::max(0, static_cast<int32>(gp.pointImage[1] - gp.radius));
        int32 pyMax = std::min(m_height, static_cast<int32>(gp.pointImage[1] + gp.radius + 1));

        for (int32 py = pyMin; py < pyMax; py++)
        {
            for (int32 px = pxMin; px < pxMax; px++)
            {
                float32 dx = static_cast<float32>(px) - gp.pointImage[0];
                float32 dy = static_cast<float32>(py) - gp.pointImage[1];

                float32 power = -0.5f * (gp.conicOpacity[0] * dx * dx + gp.conicOpacity[2] * dy * dy)
                                - gp.conicOpacity[1] * dx * dy;

                if (power > 0.0f)
                    continue;

                float32 alpha = std::min(0.99f, gp.conicOpacity[3] * std::exp(power));
                if (alpha < 1.0f / 255.0f)
                    continue;

                int64 pixelIdx = (static_cast<int64>(py) * m_width + px) * 4;
                float32& T = m_imageData[pixelIdx + 3];

                float32 test_T = T * (1.0f - alpha);
                if (test_T < 0.0001f)
                    continue;

                m_imageData[pixelIdx + 0] += gp.rgb[0] * alpha * T;
                m_imageData[pixelIdx + 1] += gp.rgb[1] * alpha * T;
                m_imageData[pixelIdx + 2] += gp.rgb[2] * alpha * T;
                T = test_T;
            }
        }
    }

    // Clamp RGB to [0, 1]
    for (int64 i = 0; i < pixelCount; i++)
    {
        m_imageData[i * 4 + 0] = std::min(1.0f, std::max(0.0f, m_imageData[i * 4 + 0]));
        m_imageData[i * 4 + 1] = std::min(1.0f, std::max(0.0f, m_imageData[i * 4 + 1]));
        m_imageData[i * 4 + 2] = std::min(1.0f, std::max(0.0f, m_imageData[i * 4 + 2]));
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    MR_LOG(LogTemp, Log, "[SplatCPU] Pipeline complete in %lldms (%dx%d, %lld Gaussians rendered)",
           ms, m_width, m_height, m_processed.Num());

    // Print depth distribution
    if (m_processed.Num() > 0)
    {
        float32 minDepth = 1e10f, maxDepth = -1e10f;
        float32 sumRadius = 0;
        int32 sumTiles = 0;
        for (const auto& gp : m_processed)
        {
            if (gp.depth < minDepth) minDepth = gp.depth;
            if (gp.depth > maxDepth) maxDepth = gp.depth;
            sumRadius += gp.radius;
            sumTiles += gp.tilesTouched;
        }
        int64 num = m_processed.Num();
        MR_LOG(LogTemp, Log, "[SplatCPU] Depth range: [%f, %f], avg radius: %f, avg tiles: %f",
               minDepth, maxDepth, sumRadius / static_cast<float32>(num),
               static_cast<float32>(sumTiles) / static_cast<float32>(num));
    }

    return true;
}

// ============================================================================
// Step 1.8: PPM Image Output
// ============================================================================

bool SplatCPU::saveImage(const String& filePath) const
{
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        MR_LOG(LogTemp, Error, "[SplatCPU] Cannot write file: %s", filePath.c_str());
        return false;
    }

    // PPM P6 binary format
    file << "P6\n" << m_width << " " << m_height << "\n255\n";

    int64 pixelCount = static_cast<int64>(m_width) * static_cast<int64>(m_height);
    for (int64 i = 0; i < pixelCount; i++)
    {
        uint8 r = static_cast<uint8>(std::min(255.0f, std::max(0.0f, m_imageData[i * 4 + 0] * 255.0f)));
        uint8 g = static_cast<uint8>(std::min(255.0f, std::max(0.0f, m_imageData[i * 4 + 1] * 255.0f)));
        uint8 b = static_cast<uint8>(std::min(255.0f, std::max(0.0f, m_imageData[i * 4 + 2] * 255.0f)));
        file.write(reinterpret_cast<const char*>(&r), 1);
        file.write(reinterpret_cast<const char*>(&g), 1);
        file.write(reinterpret_cast<const char*>(&b), 1);
    }

    file.close();
    MR_LOG(LogTemp, Log, "[SplatCPU] Image saved to %s (%dx%d PPM)", filePath.c_str(), m_width, m_height);
    return true;
}

} // namespace MonsterRender
