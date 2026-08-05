// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SplatPLYLoader.h
 * @brief PLY file parser that uploads Gaussian data to GPU storage buffers.
 * 
 * Parses .ply files (binary, little-endian), applies activation functions
 * (exp for scales, sigmoid for opacity), reorders SH coefficients, and 
 * uploads the flattened arrays as Vulkan storage buffers.
 */

#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHIResource.h"

#include <vector>

namespace MonsterRender {
namespace Splat {

/**
 * Result of loading a .ply file and uploading to GPU buffers
 */
struct FSplatGPUData
{
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> positions;      // vec4[], size = count * 4 * sizeof(float32)
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> scales;         // vec4[], size = count * 4 * sizeof(float32)
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> rotations;      // vec4[], size = count * 4 * sizeof(float32)
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> opacities;      // float[], size = count * sizeof(float32)
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> shCoefficients;  // float[], size = count * shCoeffCount * 3 * sizeof(float32)

    uint32 gaussianCount = 0;
    int32 shDegree = 0;

    bool isValid() const
    {
        return gaussianCount > 0 && 
               positions.IsValid() && scales.IsValid() && rotations.IsValid() &&
               opacities.IsValid() && shCoefficients.IsValid();
    }

    void release()
    {
        positions.Reset();
        scales.Reset();
        rotations.Reset();
        opacities.Reset();
        shCoefficients.Reset();
        gaussianCount = 0;
    }
};

/**
 * PLY loader for 3DGS Gaussian data.
 * Parses .ply files and uploads the result to GPU storage buffers.
 */
class FSplatPLYLoader
{
public:
    FSplatPLYLoader() = default;
    ~FSplatPLYLoader() = default;

    /**
     * Load a .ply file and upload all Gaussian data to GPU storage buffers.
     * 
     * @param device RHI device for buffer creation
     * @param filePath Path to the .ply file (binary, little-endian)
     * @param outData Output: populated GPU buffer data
     * @return true on success
     */
    bool loadAndUpload(RHI::IRHIDevice* device, const String& filePath, FSplatGPUData& outData);

private:
    bool parseHeader(std::ifstream& file, int32& outVertexCount,
                     std::vector<String>& outPropertyNames);
    bool uploadBuffers(RHI::IRHIDevice* device, 
                       const MonsterEngine::TArray<float32>& posData,
                       const MonsterEngine::TArray<float32>& scaleData,
                       const MonsterEngine::TArray<float32>& rotData,
                       const MonsterEngine::TArray<float32>& opacityData,
                       const MonsterEngine::TArray<float32>& shData,
                       uint32 count,
                       FSplatGPUData& outData);
};

} // namespace Splat
} // namespace MonsterRender
