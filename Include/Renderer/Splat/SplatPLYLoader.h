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
#include "Renderer/Splat/SplatSHStorage.h"
#include <functional>
#include "Renderer/Splat/LOD/SplatLODTypes.h"

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
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> shCoefficients;  // FP32 words or packed binary16 pairs, Gaussian-major RGB scalars.

    uint32 shStorageMode = ESplatSHStorage::FP32; ///< Effective GPU representation.
    uint64_t shLogicalBytes = 0; ///< Word-aligned SH descriptor bytes including tail padding.
    uint64_t sourceLogicalBytes = 0; ///< Sum of all five source descriptor sizes.
    uint64_t uploadedBytes = 0; ///< Initial GPU copy payload including half tail padding.
    uint64_t stagingAlignmentBytes = 0; ///< Queried upload allocation alignment.
    uint64_t stagingLogicalBytes = 0; ///< Upload staging descriptor bytes.
    double shConversionMilliseconds = 0; ///< CPU validation and packing, excluding parsing.
    double uploadMilliseconds = 0; ///< Buffer creation, staging, copies and waits including packing.
    uint32 gaussianCount = 0;
    int32 shDegree = 0;
    int32 sourceSHDegree = 0;

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
        shStorageMode = ESplatSHStorage::FP32;
        shLogicalBytes = sourceLogicalBytes = uploadedBytes = stagingLogicalBytes = stagingAlignmentBytes = 0;
        shConversionMilliseconds = uploadMilliseconds = 0;
    }
};

/**
 * PLY loader for 3DGS Gaussian data.
 * Parses .ply files and uploads the result to GPU storage buffers.
 */
class FSplatPLYLoader
{
public:
    /** CPU-only strict PLY ingestion. Outputs are published only on success. */
    static bool LoadCPU(const std::string& path, std::vector<FSplatGaussian>& out,
                        uint32_t& shDegree, std::string& error);
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
    bool loadAndUpload(RHI::IRHIDevice* device, const String& filePath, FSplatGPUData& outData,
                       uint64_t sourceBudgetBytes = UINT64_MAX, uint64_t stagingBudgetBytes = 32ull << 20, uint32_t shStorageMode = ESplatSHStorage::FP32, int32_t requestedSHDegree = -1);
    uint64_t GetStagingBackingBytes() const { return m_stagingBackingBytes; }

private:
    static bool LoadCPUWithAdmission(const std::string&, std::vector<FSplatGaussian>&,
        uint32_t&, std::string&, const std::function<bool(uint64_t,uint32_t,std::string&)>&);
    bool uploadBuffers(RHI::IRHIDevice*, const std::vector<FSplatGaussian>&,
                       uint32_t degree, uint32_t stagingBytes, uint32_t shStorageMode, FSplatGPUData&);
    uint64_t m_stagingBackingBytes = 0;
};

} // namespace Splat
} // namespace MonsterRender
