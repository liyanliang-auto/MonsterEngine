// Copyright Monster Engine. All Rights Reserved.

#include "Renderer/Splat/SplatPLYLoader.h"

#include "Core/Logging/LogMacros.h"
#include "RHI/RHIDefinitions.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <vector>

namespace MonsterRender {
namespace Splat {

// ============================================================================
// Parse PLY header: extract vertex count, format, and property name order.
//
// Compared with:
//  - 3dgs-vulkan-cpp (PLYLoader.cpp): hardcoded binary-read order, no name match.
//  - vk3dGaussianSplatting (ResourceManager.cpp): happly lib, matches by name.
// MonsterEngine now does dynamic name-based offset mapping (same robustness as happly,
// no third-party dependency).
// ============================================================================
bool FSplatPLYLoader::parseHeader(std::ifstream& plyFile, int32& outVertexCount,
                                  std::vector<String>& outPropertyNames)
{
    std::string line;
    bool headerEnd = false;
    bool inVertexElement = false;

    while (std::getline(plyFile, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "format")
        {
            std::string fmt;
            iss >> fmt;
            if (fmt != "binary_little_endian")
            {
                MR_LOG(LogTemp, Warning, "[SplatPLYLoader] PLY format is '%s', expected 'binary_little_endian'",
                       fmt.c_str());
            }
        }
        else if (token == "element")
        {
            std::string elemType;
            int32 count;
            iss >> elemType >> count;
            if (elemType == "vertex")
            {
                outVertexCount = count;
                inVertexElement  = true;
            }
            else
            {
                inVertexElement = false;
            }
        }
        else if (token == "property" && inVertexElement)
        {
            // Collect property name in declaration order.
            // The binary vertex record is a flat concatenation of property values
            // in this exact order — each is a 4-byte float for standard 3DGS PLY.
            std::string propType, propName;
            iss >> propType;  // "float" (always for 3DGS)
            iss >> propName;  // e.g. "x", "scale_0", "rot_3"
            outPropertyNames.push_back(String(propName.c_str()));
        }
        else if (token == "end_header")
        {
            headerEnd = true;
            break;
        }
    }

    if (!headerEnd)
    {
        MR_LOG(LogTemp, Error, "[SplatPLYLoader] Could not find 'end_header'");
        return false;
    }
    if (outVertexCount <= 0)
    {
        MR_LOG(LogTemp, Error, "[SplatPLYLoader] No vertices in PLY file");
        return false;
    }

    const int32 propertyCount = static_cast<int32>(outPropertyNames.size());
    MR_LOG(LogTemp, Log, "[SplatPLYLoader] Header: %d vertices, %d properties", outVertexCount, propertyCount);
    for (int32 i = 0; i < std::min(propertyCount, 10); ++i)
    {
        MR_LOG(LogTemp, Log, "  property[%d] = '%s'", i, outPropertyNames[i].c_str());
    }
    if (propertyCount > 10)
    {
        MR_LOG(LogTemp, Log, "  ... (%d total properties)", propertyCount);
    }

    return true;
}

// ============================================================================
// Helper: find the index of a property name in the header list.
// Returns -1 if not found.
// ============================================================================
static int32 findPropertyIndex(const std::vector<String>& propNames,
                               const char* name)
{
    for (size_t i = 0; i < propNames.size(); ++i)
    {
        if (propNames[i] == name)
            return static_cast<int32>(i);
    }
    return -1;
}

// ============================================================================
// Main entry: load PLY into CPU arrays, then upload to GPU buffers.
//
// Comparison with reference implementations:
//   - 3dgs-vulkan-cpp (PLYLoader.cpp):   hardcoded binary-read order, same
//     activation functions (exp for scales, sigmoid for opacity, normalize
//     quaternion). No property-name matching.
//   - vk3dGaussianSplatting (ResourceManager.cpp): uses happly library for
//     name-based matching, additionally flips X/Y coords and reorders
//     quaternion as (-rot_2, -rot_3, rot_0, -rot_1).
//
// MonsterEngine follows the 3dgs-vulkan-cpp convention (primary reference,
// star repo) for activation and quaternion layout. The quaternion is NOT
// reordered; the shader expects (r, x, y, z) = (rot_0, rot_1, rot_2, rot_3).
// ============================================================================
bool FSplatPLYLoader::loadAndUpload(RHI::IRHIDevice* device, const String& filePath, FSplatGPUData& outData)
{
    if (!device)
    {
        MR_LOG(LogTemp, Error, "[SplatPLYLoader] Null RHI device");
        return false;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    std::ifstream plyFile(filePath, std::ios::binary);
    if (!plyFile.is_open())
    {
        MR_LOG(LogTemp, Error, "[SplatPLYLoader] Cannot open file: %s", filePath.c_str());
        return false;
    }

    // ---- Parse header: vertex count + property name order ----
    int32 vertexCount = 0;
    std::vector<String> propNames;
    if (!parseHeader(plyFile, vertexCount, propNames))
        return false;

    const int32 numProps = static_cast<int32>(propNames.size());

    // Verify standard 3DGS PLY by checking for mandatory property names.
    // Standard INRIA 3DGS .ply has 62 float properties:
    //   x,y,z(3) nx,ny,nz(3) f_dc_0..2(3) f_rest_0..44(45)
    //   opacity(1) scale_0..2(3) rot_0..3(4)  = 62
    {
        const bool hasX = findPropertyIndex(propNames, "x") >= 0;
        const bool hasScale0 = findPropertyIndex(propNames, "scale_0") >= 0;
        const bool hasRot0 = findPropertyIndex(propNames, "rot_0") >= 0;
        MR_LOG(LogTemp, Log, "[SplatPLYLoader] Property validation: has x=%d has scale_0=%d has rot_0=%d",
               hasX ? 1 : 0, hasScale0 ? 1 : 0, hasRot0 ? 1 : 0);
    }

    // Build name → index lookup for key properties
    const int32 idxX  = findPropertyIndex(propNames, "x");
    const int32 idxY  = findPropertyIndex(propNames, "y");
    const int32 idxZ  = findPropertyIndex(propNames, "z");
    const int32 idxDC0 = findPropertyIndex(propNames, "f_dc_0");
    const int32 idxDC1 = findPropertyIndex(propNames, "f_dc_1");
    const int32 idxDC2 = findPropertyIndex(propNames, "f_dc_2");
    const int32 idxOpa = findPropertyIndex(propNames, "opacity");
    const int32 idxS0  = findPropertyIndex(propNames, "scale_0");
    const int32 idxS1  = findPropertyIndex(propNames, "scale_1");
    const int32 idxS2  = findPropertyIndex(propNames, "scale_2");
    const int32 idxR0  = findPropertyIndex(propNames, "rot_0");
    const int32 idxR1  = findPropertyIndex(propNames, "rot_1");
    const int32 idxR2  = findPropertyIndex(propNames, "rot_2");
    const int32 idxR3  = findPropertyIndex(propNames, "rot_3");

    // f_rest_0 .. f_rest_{maxIndex} indices — scan only the actual range from header
    int32 maxRestPropIndex = -1;
    for (const auto& name : propNames)
    {
        if (name.find("f_rest_") == 0)
        {
            int32 idx = std::stoi(name.substr(7));
            maxRestPropIndex = std::max(maxRestPropIndex, idx);
        }
    }
    const int32 numRestProps = maxRestPropIndex >= 0 ? (maxRestPropIndex + 1) : 0;
    std::vector<int32> restIndices(numRestProps, -1);
    for (int32 i = 0; i < numRestProps; ++i)
    {
        restIndices[i] = findPropertyIndex(propNames,
            (String("f_rest_") + std::to_string(i)).c_str());
    }
    MR_LOG(LogTemp, Log, "[SplatPLYLoader] f_rest properties: %d total (max index %d)", numRestProps, maxRestPropIndex);

    // ---- SH degree detection: read first vertex ----
    int32 shDegree = 0;
    {
        std::vector<float> firstVertex(numProps);
        plyFile.read(reinterpret_cast<char*>(firstVertex.data()),
                     numProps * sizeof(float32));
        plyFile.seekg(-static_cast<std::ifstream::off_type>(numProps * sizeof(float32)),
                      std::ios::cur);  // rewind

        int32 maxRestNeeded = 0;
        for (int32 i = 0; i < numRestProps; ++i)
        {
            const int32 ri = restIndices[i];
            if (ri >= 0 && std::abs(firstVertex[ri]) > 1e-10f)
                maxRestNeeded = i + 1;
        }

        if (maxRestNeeded <= 0)       shDegree = 0;
        else if (maxRestNeeded <= 8)  shDegree = 1;
        else if (maxRestNeeded <= 24) shDegree = 2;
        else                          shDegree = 3;

        MR_LOG(LogTemp, Log, "[SplatPLYLoader] Detected SH degree=%d (max non-zero rest index=%d)",
               shDegree, maxRestNeeded);
    }

    const int32 shCoeffCount = (shDegree == 0 ? 1 : (shDegree == 1 ? 4 : (shDegree == 2 ? 9 : 16)));
    const int32 numRestPerChannel = shCoeffCount - 1;  // rest coefficients per RGB channel

    // ---- Pre-allocate CPU arrays ----
    MonsterEngine::TArray<float32> posData;
    MonsterEngine::TArray<float32> scaleData;
    MonsterEngine::TArray<float32> rotData;
    MonsterEngine::TArray<float32> opacityData;
    MonsterEngine::TArray<float32> shData;

    posData.Reserve(static_cast<int64>(vertexCount) * 4);
    scaleData.Reserve(static_cast<int64>(vertexCount) * 4);
    rotData.Reserve(static_cast<int64>(vertexCount) * 4);
    opacityData.Reserve(vertexCount);
    shData.Reserve(static_cast<int64>(vertexCount) * shCoeffCount * 3);

    // ---- Bbox tracking ----
    float32 bboxMin[3] = { 0.0f, 0.0f, 0.0f };
    float32 bboxMax[3] = { 0.0f, 0.0f, 0.0f };
    bool bBboxInit = false;

    // ---- Diagnostic min/max for scales & rotations ----
    float32 scaleMinRaw[3] = { 1e30f, 1e30f, 1e30f };
    float32 scaleMaxRaw[3] = { -1e30f, -1e30f, -1e30f };
    float32 scaleMinAct[3] = { 1e30f, 1e30f, 1e30f };
    float32 scaleMaxAct[3] = { -1e30f, -1e30f, -1e30f };
    float32 rotMin[4] = { 1e30f, 1e30f, 1e30f, 1e30f };
    float32 rotMax[4] = { -1e30f, -1e30f, -1e30f, -1e30f };
    float32 opaMinRaw = 1e30f, opaMaxRaw = -1e30f;
    float32 opaMinAct = 1e30f, opaMaxAct = -1e30f;

    // ---- Read all vertices ----
    std::vector<float32> rawVertex(numProps);
    for (int32 i = 0; i < vertexCount; ++i)
    {
        plyFile.read(reinterpret_cast<char*>(rawVertex.data()),
                     numProps * sizeof(float32));

        // --- Bbox ---
        const float32 px = (idxX >= 0) ? rawVertex[idxX] : 0.0f;
        const float32 py = (idxY >= 0) ? rawVertex[idxY] : 0.0f;
        const float32 pz = (idxZ >= 0) ? rawVertex[idxZ] : 0.0f;

        if (!bBboxInit)
        {
            bboxMin[0] = bboxMax[0] = px;
            bboxMin[1] = bboxMax[1] = py;
            bboxMin[2] = bboxMax[2] = pz;
            bBboxInit = true;
        }
        else
        {
            bboxMin[0] = std::min(bboxMin[0], px);
            bboxMax[0] = std::max(bboxMax[0], px);
            bboxMin[1] = std::min(bboxMin[1], py);
            bboxMax[1] = std::max(bboxMax[1], py);
            bboxMin[2] = std::min(bboxMin[2], pz);
            bboxMax[2] = std::max(bboxMax[2], pz);
        }

        // Position: vec4 (w = 1.0)
        posData.Add(px);
        posData.Add(py);
        posData.Add(pz);
        posData.Add(1.0f);

        // --- Scales: exp activate ---
        const float32 rs0 = (idxS0 >= 0) ? rawVertex[idxS0] : 0.0f;
        const float32 rs1 = (idxS1 >= 0) ? rawVertex[idxS1] : 0.0f;
        const float32 rs2 = (idxS2 >= 0) ? rawVertex[idxS2] : 0.0f;

        scaleMinRaw[0] = std::min(scaleMinRaw[0], rs0);
        scaleMinRaw[1] = std::min(scaleMinRaw[1], rs1);
        scaleMinRaw[2] = std::min(scaleMinRaw[2], rs2);
        scaleMaxRaw[0] = std::max(scaleMaxRaw[0], rs0);
        scaleMaxRaw[1] = std::max(scaleMaxRaw[1], rs1);
        scaleMaxRaw[2] = std::max(scaleMaxRaw[2], rs2);

        const float32 as0 = std::exp(rs0);
        const float32 as1 = std::exp(rs1);
        const float32 as2 = std::exp(rs2);

        scaleMinAct[0] = std::min(scaleMinAct[0], as0);
        scaleMinAct[1] = std::min(scaleMinAct[1], as1);
        scaleMinAct[2] = std::min(scaleMinAct[2], as2);
        scaleMaxAct[0] = std::max(scaleMaxAct[0], as0);
        scaleMaxAct[1] = std::max(scaleMaxAct[1], as1);
        scaleMaxAct[2] = std::max(scaleMaxAct[2], as2);

        scaleData.Add(as0);
        scaleData.Add(as1);
        scaleData.Add(as2);
        scaleData.Add(0.0f);  // w unused

        // --- Rotation: normalize quaternion ---
        const float32 rr0 = (idxR0 >= 0) ? rawVertex[idxR0] : 0.0f;
        const float32 rr1 = (idxR1 >= 0) ? rawVertex[idxR1] : 0.0f;
        const float32 rr2 = (idxR2 >= 0) ? rawVertex[idxR2] : 0.0f;
        const float32 rr3 = (idxR3 >= 0) ? rawVertex[idxR3] : 0.0f;

        rotMin[0] = std::min(rotMin[0], rr0);
        rotMin[1] = std::min(rotMin[1], rr1);
        rotMin[2] = std::min(rotMin[2], rr2);
        rotMin[3] = std::min(rotMin[3], rr3);
        rotMax[0] = std::max(rotMax[0], rr0);
        rotMax[1] = std::max(rotMax[1], rr1);
        rotMax[2] = std::max(rotMax[2], rr2);
        rotMax[3] = std::max(rotMax[3], rr3);

        float32 qlen = std::sqrt(rr0 * rr0 + rr1 * rr1 + rr2 * rr2 + rr3 * rr3);
        if (qlen > 1e-10f)
        {
            rotData.Add(rr0 / qlen);
            rotData.Add(rr1 / qlen);
            rotData.Add(rr2 / qlen);
            rotData.Add(rr3 / qlen);
        }
        else
        {
            rotData.Add(1.0f); rotData.Add(0.0f); rotData.Add(0.0f); rotData.Add(0.0f);
        }

        // --- Opacity: sigmoid activate ---
        const float32 rawOpa = (idxOpa >= 0) ? rawVertex[idxOpa] : 0.0f;
        opaMinRaw = std::min(opaMinRaw, rawOpa);
        opaMaxRaw = std::max(opaMaxRaw, rawOpa);
        const float32 actOpa = 1.0f / (1.0f + std::exp(-rawOpa));
        opaMinAct = std::min(opaMinAct, actOpa);
        opaMaxAct = std::max(opaMaxAct, actOpa);
        opacityData.Add(actOpa);

        // --- SH coefficients: reorder from PLY layout to interleaved (r,g,b) per coeff ---
        // PLY layout: [dc_r, dc_g, dc_b, rest_r[0..N], rest_g[0..N], rest_b[0..N]]
        //   where N = numRestPerChannel - 1 (e.g. 14 for degree 3, 7 for degree 2)
        // Output layout: [dc_r, dc_g, dc_b, coeff1_r, coeff1_g, coeff1_b, ...]
        shData.Add((idxDC0 >= 0) ? rawVertex[idxDC0] : 0.0f);
        shData.Add((idxDC1 >= 0) ? rawVertex[idxDC1] : 0.0f);
        shData.Add((idxDC2 >= 0) ? rawVertex[idxDC2] : 0.0f);

        for (int32 j = 1; j < shCoeffCount; ++j)
        {
            // INRIA 3DGS PLY layout: f_rest = [r_0..r_N, g_0..g_N, b_0..b_N]
            // Reorder to interleaved (r, g, b) per coefficient index.
            const int32 k  = j - 1;                        // zero-based rest index
            const int32 riR = restIndices[k];                          // R channel: f_rest_{k}
            const int32 riG = restIndices[k + numRestPerChannel];      // G channel: f_rest_{k+N}
            const int32 riB = restIndices[k + numRestPerChannel * 2];  // B channel: f_rest_{k+2N}
            shData.Add((riR >= 0) ? rawVertex[riR] : 0.0f);
            shData.Add((riG >= 0) ? rawVertex[riG] : 0.0f);
            shData.Add((riB >= 0) ? rawVertex[riB] : 0.0f);
        }
    }

    // ---- Diagnostic logs ----
    MR_LOG(LogTemp, Log, "[SplatPLYLoader] Model bbox min=(%.3f, %.3f, %.3f) max=(%.3f, %.3f, %.3f) "
           "center=(%.3f, %.3f, %.3f) extent=(%.3f, %.3f, %.3f)",
           bboxMin[0], bboxMin[1], bboxMin[2],
           bboxMax[0], bboxMax[1], bboxMax[2],
           (bboxMin[0] + bboxMax[0]) * 0.5f, (bboxMin[1] + bboxMax[1]) * 0.5f, (bboxMin[2] + bboxMax[2]) * 0.5f,
           bboxMax[0] - bboxMin[0], bboxMax[1] - bboxMin[1], bboxMax[2] - bboxMin[2]);

    // SH buffer layout diagnostic
    MR_LOG(LogTemp, Log, "[SplatPLYLoader] SH layout: degree=%d, coeffsPerChannel=%d, numRestPerChannel=%d, "
           "total shData floats=%lld (expected %lld)",
           shDegree, shCoeffCount, numRestPerChannel,
           static_cast<int64>(shData.Num()),
           static_cast<int64>(vertexCount) * shCoeffCount * 3);

    // Scale ranges: raw (log-scale) and activated (exp)
    MR_LOG(LogTemp, Log, "[SplatPLYLoader] Scale (raw / log): min=(%.4f, %.4f, %.4f) max=(%.4f, %.4f, %.4f)",
           scaleMinRaw[0], scaleMinRaw[1], scaleMinRaw[2],
           scaleMaxRaw[0], scaleMaxRaw[1], scaleMaxRaw[2]);
    MR_LOG(LogTemp, Log, "[SplatPLYLoader] Scale (activated / exp): min=(%.6f, %.6f, %.6f) max=(%.6f, %.6f, %.6f)",
           scaleMinAct[0], scaleMinAct[1], scaleMinAct[2],
           scaleMaxAct[0], scaleMaxAct[1], scaleMaxAct[2]);

    // Rotation ranges: raw quaternion (before normalize)
    MR_LOG(LogTemp, Log, "[SplatPLYLoader] Rotation (raw quaternion): "
           "min=(%.4f, %.4f, %.4f, %.4f) max=(%.4f, %.4f, %.4f, %.4f)",
           rotMin[0], rotMin[1], rotMin[2], rotMin[3],
           rotMax[0], rotMax[1], rotMax[2], rotMax[3]);

    // Opacity ranges
    MR_LOG(LogTemp, Log, "[SplatPLYLoader] Opacity (raw): min=%.4f max=%.4f  (activated / sigmoid): min=%.4f max=%.4f",
           opaMinRaw, opaMaxRaw, opaMinAct, opaMaxAct);

    // ---- Upload to GPU buffers ----
    if (!uploadBuffers(device, posData, scaleData, rotData, opacityData, shData,
                       static_cast<uint32>(vertexCount), outData))
    {
        return false;
    }

    outData.gaussianCount = static_cast<uint32>(vertexCount);
    outData.shDegree = shDegree;

    auto endTime = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    MR_LOG(LogTemp, Log, "[SplatPLYLoader] Loaded %d Gaussians (SH degree %d) in %lldms",
           vertexCount, shDegree, ms);

    return true;
}

// ============================================================================
// uploadBuffers — unchanged from original (storage buffer creation + upload)
// ============================================================================
bool FSplatPLYLoader::uploadBuffers(RHI::IRHIDevice* device,
                                     const MonsterEngine::TArray<float32>& posData,
                                     const MonsterEngine::TArray<float32>& scaleData,
                                     const MonsterEngine::TArray<float32>& rotData,
                                     const MonsterEngine::TArray<float32>& opacityData,
                                     const MonsterEngine::TArray<float32>& shData,
                                     uint32 count,
                                     FSplatGPUData& outData)
{
    using namespace RHI;

    auto createStorageBuf = [device](const MonsterEngine::TArray<float32>& data,
                                     uint32 size, const char* debugName) -> MonsterEngine::TSharedPtr<IRHIBuffer>
    {
        BufferDesc desc;
        desc.size = size;
        desc.usage = EResourceUsage::StorageBuffer | EResourceUsage::TransferDst;
        desc.memoryUsage = EMemoryUsage::Default;
        desc.initialData = data.GetData();
        desc.initialDataSize = size;
        desc.debugName = debugName;
        return device->createBuffer(desc);
    };

    uint32 posSize = count * 4 * sizeof(float32);
    outData.positions = createStorageBuf(posData, posSize, "Splat_Positions");

    uint32 scaleSize = count * 4 * sizeof(float32);
    outData.scales = createStorageBuf(scaleData, scaleSize, "Splat_Scales");

    uint32 rotSize = count * 4 * sizeof(float32);
    outData.rotations = createStorageBuf(rotData, rotSize, "Splat_Rotations");

    uint32 opacitySize = count * sizeof(float32);
    outData.opacities = createStorageBuf(opacityData, opacitySize, "Splat_Opacities");

    uint32 shSize = static_cast<uint32>(shData.Num() * sizeof(float32));
    outData.shCoefficients = createStorageBuf(shData, shSize, "Splat_SH");

    if (!outData.positions || !outData.scales || !outData.rotations ||
        !outData.opacities || !outData.shCoefficients)
    {
        MR_LOG(LogTemp, Error, "[SplatPLYLoader] Failed to create one or more GPU buffers");
        return false;
    }

    return true;
}

} // namespace Splat
} // namespace MonsterRender
