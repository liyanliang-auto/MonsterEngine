// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SplatSortPass.cpp
 * @brief GPU sort pipeline: Prefix Sum → Assign Keys → Radix Sort → Tile Boundaries.
 * 
 * Each pass is encapsulated as a self-contained class that manages its own
 * descriptor sets, pipeline, and buffers.
 */

#include "Renderer/Splat/SplatSortPass.h"
#include "Renderer/Splat/SplatSortTypes.h"
#include "Renderer/Splat/SplatTypes.h"

#include "Core/Logging/LogMacros.h"
#include "Core/ShaderCompiler.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDescriptorSet.h"
#include "RHI/RHIDefinitions.h"

#include <cmath>

namespace MonsterRender {
namespace Splat {

// ============================================================================
// Utility: Load SPIR-V and create compute pipeline for sort passes
// ============================================================================

namespace {

bool createSortPipeline(RHI::IRHIDevice* device,
                        const String& spvPath,
                        MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> setLayout,
                        const RHI::FPushConstantRange& pushRange,
                        MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout>& outLayout,
                        MonsterEngine::TSharedPtr<RHI::IRHIPipelineState>& outPipeline)
{
    using namespace RHI;

    // Pipeline layout
    {
        FPipelineLayoutDesc layoutDesc(spvPath);
        layoutDesc.setLayouts.Add(setLayout);
        if (pushRange.size > 0)
            layoutDesc.pushConstantRanges.Add(pushRange);

        outLayout = device->createPipelineLayout(layoutDesc);
        if (!outLayout)
        {
            MR_LOG(LogTemp, Error, "[SortPass] Failed to create pipeline layout: %s", spvPath.c_str());
            return false;
        }
    }

    // Load SPIR-V
    auto bytecodeVec = MonsterRender::ShaderCompiler::readFileBytes(spvPath);
    if (bytecodeVec.empty())
    {
        MR_LOG(LogTemp, Error, "[SortPass] Failed to load SPIR-V: %s", spvPath.c_str());
        return false;
    }

    TSpan<const uint8> bytecode(bytecodeVec.data(), bytecodeVec.size());
    auto shader = device->createComputeShader(bytecode);
    if (!shader)
    {
        MR_LOG(LogTemp, Error, "[SortPass] Failed to create compute shader: %s", spvPath.c_str());
        return false;
    }

    // Compute pipeline
    ComputePipelineStateDesc pipeDesc;
    pipeDesc.computeShader = shader;
    pipeDesc.pipelineLayout = outLayout;
    pipeDesc.debugName = spvPath;

    outPipeline = device->createComputePipelineState(pipeDesc);
    if (!outPipeline)
    {
        MR_LOG(LogTemp, Error, "[SortPass] Failed to create compute pipeline: %s", spvPath.c_str());
        return false;
    }

    return true;
}

MonsterEngine::TSharedPtr<RHI::IRHIBuffer> createStorageBuffer(
    RHI::IRHIDevice* device, uint32 size, const char* name)
{
    RHI::BufferDesc desc;
    desc.size = size;
    desc.usage = RHI::EResourceUsage::StorageBuffer | RHI::EResourceUsage::TransferDst | RHI::EResourceUsage::TransferSrc;
    desc.memoryUsage = RHI::EMemoryUsage::Default;
    desc.debugName = name;
    return device->createBuffer(desc);
}

} // anonymous namespace

// ============================================================================
// FSplatPrefixSumPass
// ============================================================================

FSplatPrefixSumPass::FSplatPrefixSumPass() = default;

FSplatPrefixSumPass::~FSplatPrefixSumPass()
{
    m_bufferA.Reset();
    m_bufferB.Reset();
    m_totalSumHost.Reset();
    m_pipelineState.Reset();
    m_pipelineLayout.Reset();
    m_descriptorSet.Reset();
    m_setLayout.Reset();
}

bool FSplatPrefixSumPass::initialize(RHI::IRHIDevice* device, uint32 maxElements)
{
    using namespace RHI;

    if (!device || maxElements == 0)
    {
        MR_LOG(LogTemp, Error, "[PrefixSum] Invalid device or maxElements=0");
        return false;
    }

    m_numElements = maxElements;

    // Descriptor set layout: 2 storage buffers
    {
        FDescriptorSetLayoutDesc desc(0, "PrefixSum_Set");
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EPrefixSumBinding::BufferA, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EPrefixSumBinding::BufferB, EDescriptorType::StorageBuffer, EShaderStage::Compute));

        m_setLayout = device->createDescriptorSetLayout(desc);
        if (!m_setLayout)
        {
            MR_LOG(LogTemp, Error, "[PrefixSum] Failed to create descriptor set layout");
            return false;
        }
    }

    // Pipeline
    {
        FRadixSortPushConstants dummy; (void)dummy;
        FPushConstantRange pushRange(EShaderStage::Compute, 0, sizeof(FPrefixSumPushConstants));
        if (!createSortPipeline(device, "Shaders/Splat/Sort/compiled/splat_prefix_sum.spv",
                                m_setLayout, pushRange, m_pipelineLayout, m_pipelineState))
            return false;
    }

    // Allocate ping-pong buffers (size rounded up to 256)
    uint32 paddedSize = ((maxElements + 255u) / 256u) * 256u;
    m_bufferA = createStorageBuffer(device, paddedSize * sizeof(uint32), "PrefixSum_BufA");
    m_bufferB = createStorageBuffer(device, paddedSize * sizeof(uint32), "PrefixSum_BufB");
    if (!m_bufferA || !m_bufferB)
    {
        MR_LOG(LogTemp, Error, "[PrefixSum] Failed to create ping-pong buffers");
        return false;
    }

    // Allocate descriptor set and bind buffers
    m_descriptorSet = device->allocateDescriptorSet(m_setLayout);
    if (!m_descriptorSet)
    {
        MR_LOG(LogTemp, Error, "[PrefixSum] Failed to allocate descriptor set");
        return false;
    }
    m_descriptorSet->updateStorageBuffer(EPrefixSumBinding::BufferA, m_bufferA, 0, 0);
    m_descriptorSet->updateStorageBuffer(EPrefixSumBinding::BufferB, m_bufferB, 0, 0);

    m_bInitialized = true;
    MR_LOG(LogTemp, Log, "[PrefixSum] Initialized for %u elements (padded: %u)", maxElements, paddedSize);
    return true;
}

uint32 FSplatPrefixSumPass::execute(RHI::IRHICommandList* cmdList, uint32 actualCount)
{
    if (!m_bInitialized || !cmdList || actualCount == 0)
        return 0;

    if (actualCount > m_numElements)
    {
        MR_LOG(LogTemp, Error, "[PrefixSum] actualCount (%u) exceeds maxElements (%u)",
               actualCount, m_numElements);
        return 0;
    }

    // Blelloch scan: ceil(log2(N)) iterations
    uint32 numSteps = 0;
    {
        uint32 n = actualCount;
        while (n > 1)
        {
            n = (n + 1) / 2;
            numSteps++;
        }
    }

    FPrefixSumPushConstants pc;
    pc.numElements = actualCount;

    for (uint32 step = 0; step < numSteps; step++)
    {
        pc.step = step;
        pc.readFromA = (step % 2 == 0) ? 1u : 0u;

        uint32 numGroups = (actualCount + 255u) / 256u;

        cmdList->setPipelineState(m_pipelineState);
        cmdList->bindDescriptorSet(m_pipelineLayout, 0, m_descriptorSet);
        cmdList->pushConstants(m_pipelineLayout, RHI::EShaderStage::Compute, 0,
                               sizeof(FPrefixSumPushConstants), &pc);
        cmdList->dispatch(numGroups, 1, 1);

        // Barrier: ensure previous dispatch writes are visible to next dispatch
        cmdList->resourceBarrier(); // Full pipeline barrier
    }

    // If numSteps is odd, final result is in bufferB; otherwise in bufferA
    m_resultIsInB = (numSteps % 2 == 1);

    MR_LOG(LogTemp, Log, "[PrefixSum] Completed %u steps for %u elements", numSteps, actualCount);
    return actualCount;
}

MonsterEngine::TSharedPtr<RHI::IRHIBuffer> FSplatPrefixSumPass::getResultBuffer() const
{
    return m_resultIsInB ? m_bufferB : m_bufferA;
}

// ============================================================================
// FSplatAssignKeysPass
// ============================================================================

FSplatAssignKeysPass::~FSplatAssignKeysPass()
{
    m_sortKeys.Reset();
    m_sortValues.Reset();
    m_pipelineState.Reset();
    m_pipelineLayout.Reset();
    for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
        m_descriptorSets[i].Reset();
    m_setLayout.Reset();
}

bool FSplatAssignKeysPass::initialize(RHI::IRHIDevice* device, uint32 totalSortElements, uint32 gaussianCount)
{
    using namespace RHI;

    if (!device || totalSortElements == 0 || gaussianCount == 0)
    {
        MR_LOG(LogTemp, Error, "[AssignKeys] Invalid parameters");
        return false;
    }

    m_sortElementCount = totalSortElements;
    m_gaussianCount = gaussianCount;

    // Descriptor set layout: 6 bindings (3 input + 2 output, binding 3 reserved)
    {
        FDescriptorSetLayoutDesc desc(0, "AssignKeys_Set");
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EAssignKeysBinding::PrefixSum, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EAssignKeysBinding::BBox, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EAssignKeysBinding::Depth, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EAssignKeysBinding::SortKeys, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            EAssignKeysBinding::SortValues, EDescriptorType::StorageBuffer, EShaderStage::Compute));

        m_setLayout = device->createDescriptorSetLayout(desc);
        if (!m_setLayout)
        {
            MR_LOG(LogTemp, Error, "[AssignKeys] Failed to create descriptor set layout");
            return false;
        }
    }

    // Pipeline
    {
        FPushConstantRange pushRange(EShaderStage::Compute, 0, sizeof(FAssignKeysPushConstants));
        if (!createSortPipeline(device, "Shaders/Splat/Sort/compiled/splat_assign_keys.spv",
                                m_setLayout, pushRange, m_pipelineLayout, m_pipelineState))
            return false;
    }

    // Allocate output buffers (key = 8 bytes, value = 4 bytes per entry)
    m_sortKeys = createStorageBuffer(device, totalSortElements * sizeof(uint64), "AssignKeys_Keys");
    m_sortValues = createStorageBuffer(device, totalSortElements * sizeof(uint32), "AssignKeys_Values");
    if (!m_sortKeys || !m_sortValues)
    {
        MR_LOG(LogTemp, Error, "[AssignKeys] Failed to create output buffers");
        return false;
    }

    // Allocate per-frame descriptor sets (double-buffered)
    for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
    {
        m_descriptorSets[i] = device->allocateDescriptorSet(m_setLayout);
        if (!m_descriptorSets[i])
        {
            MR_LOG(LogTemp, Error, "[AssignKeys] Failed to allocate descriptor set %u", i);
            return false;
        }

        // Pre-bind output buffers (unchanged across frames)
        m_descriptorSets[i]->updateStorageBuffer(EAssignKeysBinding::SortKeys, m_sortKeys, 0, 0);
        m_descriptorSets[i]->updateStorageBuffer(EAssignKeysBinding::SortValues, m_sortValues, 0, 0);
    }

    m_bInitialized = true;
    MR_LOG(LogTemp, Log, "[AssignKeys] Initialized: %u Gaussians → %u sort entries",
           gaussianCount, totalSortElements);
    return true;
}

void FSplatAssignKeysPass::setInputBuffers(
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> prefixSum,
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> bbox,
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> depth)
{
    m_prefixSum = prefixSum;
    m_bbox = bbox;
    m_depth = depth;

    // Only update the descriptor set that will be used by the next execute() call.
    // Updating ALL per-frame sets would touch the set still in use by the previous
    // frame's pending command buffer (VUID-03047).
    uint32 nextIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
    auto& ds = m_descriptorSets[nextIndex];
    if (ds)
    {
        if (m_prefixSum) ds->updateStorageBuffer(EAssignKeysBinding::PrefixSum, m_prefixSum, 0, 0);
        if (m_bbox)      ds->updateStorageBuffer(EAssignKeysBinding::BBox,      m_bbox,      0, 0);
        if (m_depth)     ds->updateStorageBuffer(EAssignKeysBinding::Depth,     m_depth,     0, 0);
    }
}

void FSplatAssignKeysPass::execute(RHI::IRHICommandList* cmdList, uint32 gaussianCount,
                                    uint32 gridX, uint32 gridY)
{
    if (!m_bInitialized || !cmdList)
        return;

    // Toggle per-frame descriptor set index
    m_currentDsIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
    auto& ds = m_descriptorSets[m_currentDsIndex];

    FAssignKeysPushConstants pc;
    pc.gaussianCount = gaussianCount;
    pc.gridX = gridX;
    pc.gridY = gridY;
    pc.maxDepth = 0.0f; // Unused; shader uses floatBitsToUint

    uint32 numGroups = (gaussianCount + 255u) / 256u;

    cmdList->setPipelineState(m_pipelineState);
    cmdList->bindDescriptorSet(m_pipelineLayout, 0, ds);
    cmdList->pushConstants(m_pipelineLayout, RHI::EShaderStage::Compute, 0,
                           sizeof(FAssignKeysPushConstants), &pc);
    cmdList->dispatch(numGroups, 1, 1);
}

// ============================================================================
// FSplatRadixSortPass
// ============================================================================

FSplatRadixSortPass::~FSplatRadixSortPass()
{
    m_keysEven.Reset();
    m_keysOdd.Reset();
    m_valuesEven.Reset();
    m_valuesOdd.Reset();
    m_histogram.Reset();
    m_histPipeline.Reset();
    m_histPipelineLayout.Reset();
    m_histDescriptorSetEven.Reset();
    m_histDescriptorSetOdd.Reset();
    m_histSetLayout.Reset();
    m_scatterPipeline.Reset();
    m_scatterPipelineLayout.Reset();
    m_scatterDescriptorSetEven.Reset();
    m_scatterDescriptorSetOdd.Reset();
    m_scatterSetLayout.Reset();
}

bool FSplatRadixSortPass::createHistogramPipeline(RHI::IRHIDevice* device)
{
    using namespace RHI;

    // Descriptor layout: 2 bindings (keys_in, histogram_out)
    {
        FDescriptorSetLayoutDesc desc(0, "RadixHistogram_Set");
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            ERadixHistogramBinding::SortKeys, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            ERadixHistogramBinding::Histogram, EDescriptorType::StorageBuffer, EShaderStage::Compute));

        m_histSetLayout = device->createDescriptorSetLayout(desc);
        if (!m_histSetLayout)
        {
            MR_LOG(LogTemp, Error, "[RadixSort] Failed to create histogram descriptor layout");
            return false;
        }
    }

    FPushConstantRange pushRange(EShaderStage::Compute, 0, sizeof(FRadixSortPushConstants));
    if (!createSortPipeline(device, "Shaders/Splat/Sort/compiled/splat_radix_histogram.spv",
                            m_histSetLayout, pushRange, m_histPipelineLayout, m_histPipeline))
        return false;

    return true;
}

bool FSplatRadixSortPass::createScatterPipeline(RHI::IRHIDevice* device)
{
    using namespace RHI;

    // Descriptor layout: 5 bindings (keys_in, keys_out, values_in, values_out, histogram)
    {
        FDescriptorSetLayoutDesc desc(0, "RadixScatter_Set");
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            ERadixScatterBinding::SortKeysIn, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            ERadixScatterBinding::SortKeysOut, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            ERadixScatterBinding::SortValuesIn, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            ERadixScatterBinding::SortValuesOut, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            ERadixScatterBinding::Histogram, EDescriptorType::StorageBuffer, EShaderStage::Compute));

        m_scatterSetLayout = device->createDescriptorSetLayout(desc);
        if (!m_scatterSetLayout)
        {
            MR_LOG(LogTemp, Error, "[RadixSort] Failed to create scatter descriptor layout");
            return false;
        }
    }

    FPushConstantRange pushRange(EShaderStage::Compute, 0, sizeof(FRadixSortPushConstants));
    if (!createSortPipeline(device, "Shaders/Splat/Sort/compiled/splat_radix_scatter.spv",
                            m_scatterSetLayout, pushRange, m_scatterPipelineLayout, m_scatterPipeline))
        return false;

    return true;
}

bool FSplatRadixSortPass::initialize(RHI::IRHIDevice* device, uint32 maxSortElements)
{
    using namespace RHI;

    if (!device || maxSortElements == 0)
    {
        MR_LOG(LogTemp, Error, "[RadixSort] Invalid parameters");
        return false;
    }

    m_maxElements = maxSortElements;

    // Compute max workgroups
    uint32 maxWorkgroups = (maxSortElements + RADIX_SORT_WORKGROUP * RADIX_SORT_BLOCKS_PER_WG - 1u)
                         / (RADIX_SORT_WORKGROUP * RADIX_SORT_BLOCKS_PER_WG);
    if (maxWorkgroups == 0) maxWorkgroups = 1;

    // Create pipelines
    if (!createHistogramPipeline(device) || !createScatterPipeline(device))
        return false;

    // Allocate ping-pong buffers
    uint32 keySize   = maxSortElements * sizeof(uint64);
    uint32 valueSize = maxSortElements * sizeof(uint32);

    m_keysEven   = createStorageBuffer(device, keySize,   "RadixSort_KeysEven");
    m_keysOdd    = createStorageBuffer(device, keySize,   "RadixSort_KeysOdd");
    m_valuesEven = createStorageBuffer(device, valueSize, "RadixSort_ValuesEven");
    m_valuesOdd  = createStorageBuffer(device, valueSize, "RadixSort_ValuesOdd");
    m_histogram  = createStorageBuffer(device, RADIX_SORT_BINS * maxWorkgroups * sizeof(uint32),
                                       "RadixSort_Histogram");

    if (!m_keysEven || !m_keysOdd || !m_valuesEven || !m_valuesOdd || !m_histogram)
    {
        MR_LOG(LogTemp, Error, "[RadixSort] Failed to create buffers");
        return false;
    }

    // Allocate descriptor sets (even/odd pairs to avoid mid-recording updates)
    m_histDescriptorSetEven = device->allocateDescriptorSet(m_histSetLayout);
    m_histDescriptorSetOdd  = device->allocateDescriptorSet(m_histSetLayout);
    m_scatterDescriptorSetEven = device->allocateDescriptorSet(m_scatterSetLayout);
    m_scatterDescriptorSetOdd  = device->allocateDescriptorSet(m_scatterSetLayout);
    if (!m_histDescriptorSetEven || !m_histDescriptorSetOdd ||
        !m_scatterDescriptorSetEven || !m_scatterDescriptorSetOdd)
    {
        MR_LOG(LogTemp, Error, "[RadixSort] Failed to allocate descriptor sets");
        return false;
    }

    // Pre-bind all buffer references (never updated during recording)
    m_histDescriptorSetEven->updateStorageBuffer(ERadixHistogramBinding::SortKeys,  m_keysEven, 0, 0);
    m_histDescriptorSetEven->updateStorageBuffer(ERadixHistogramBinding::Histogram, m_histogram, 0, 0);
    m_histDescriptorSetOdd->updateStorageBuffer(ERadixHistogramBinding::SortKeys,   m_keysOdd, 0, 0);
    m_histDescriptorSetOdd->updateStorageBuffer(ERadixHistogramBinding::Histogram,  m_histogram, 0, 0);

    // Scatter: even-pass reads even→writes odd
    m_scatterDescriptorSetEven->updateStorageBuffer(ERadixScatterBinding::SortKeysIn,   m_keysEven,   0, 0);
    m_scatterDescriptorSetEven->updateStorageBuffer(ERadixScatterBinding::SortKeysOut,  m_keysOdd,    0, 0);
    m_scatterDescriptorSetEven->updateStorageBuffer(ERadixScatterBinding::SortValuesIn, m_valuesEven,  0, 0);
    m_scatterDescriptorSetEven->updateStorageBuffer(ERadixScatterBinding::SortValuesOut, m_valuesOdd,  0, 0);
    m_scatterDescriptorSetEven->updateStorageBuffer(ERadixScatterBinding::Histogram,     m_histogram,   0, 0);

    // Scatter: odd-pass reads odd→writes even
    m_scatterDescriptorSetOdd->updateStorageBuffer(ERadixScatterBinding::SortKeysIn,   m_keysOdd,    0, 0);
    m_scatterDescriptorSetOdd->updateStorageBuffer(ERadixScatterBinding::SortKeysOut,  m_keysEven,   0, 0);
    m_scatterDescriptorSetOdd->updateStorageBuffer(ERadixScatterBinding::SortValuesIn, m_valuesOdd,  0, 0);
    m_scatterDescriptorSetOdd->updateStorageBuffer(ERadixScatterBinding::SortValuesOut, m_valuesEven, 0, 0);
    m_scatterDescriptorSetOdd->updateStorageBuffer(ERadixScatterBinding::Histogram,     m_histogram,   0, 0);

    m_bInitialized = true;
    m_finalInEven = true;
    MR_LOG(LogTemp, Log, "[RadixSort] Initialized for %u elements (max %u WGs)",
           maxSortElements, maxWorkgroups);
    return true;
}

bool FSplatRadixSortPass::execute(RHI::IRHICommandList* cmdList, uint32 numElements)
{
    using namespace RHI;

    if (!m_bInitialized || !cmdList || numElements == 0 || numElements > m_maxElements)
        return false;

    // Compute workgroups for this element count
    uint32 numWGs = (numElements + RADIX_SORT_WORKGROUP * RADIX_SORT_BLOCKS_PER_WG - 1u)
                  / (RADIX_SORT_WORKGROUP * RADIX_SORT_BLOCKS_PER_WG);
    if (numWGs == 0) numWGs = 1;

    uint32 numHistWGs = numWGs;

    FRadixSortPushConstants pc;
    pc.numElements = numElements;
    pc.numWorkgroups = numWGs;
    pc.numBlocksPerWorkgroup = RADIX_SORT_BLOCKS_PER_WG;

    // 8 passes for 64-bit keys, each processing 8 bits
    for (uint32 pass = 0; pass < 8; pass++)
    {
        pc.shift = pass * 8U;

        bool isEvenPass = (pass % 2 == 0);

        // Source buffers for this pass
        auto& srcKeys   = isEvenPass ? m_keysEven   : m_keysOdd;
        auto& srcValues = isEvenPass ? m_valuesEven : m_valuesOdd;
        auto& dstKeys   = isEvenPass ? m_keysOdd    : m_keysEven;
        auto& dstValues = isEvenPass ? m_valuesOdd  : m_valuesEven;

        // ---- Histogram Dispatch ----
        // Select even or odd descriptor set (pre-baked, no update needed)
        auto histSet = isEvenPass ? m_histDescriptorSetEven : m_histDescriptorSetOdd;
        auto scatterSet = isEvenPass ? m_scatterDescriptorSetEven : m_scatterDescriptorSetOdd;

        cmdList->setPipelineState(m_histPipeline);
        cmdList->bindDescriptorSet(m_histPipelineLayout, 0, histSet);
        cmdList->pushConstants(m_histPipelineLayout, EShaderStage::Compute, 0,
                               sizeof(FRadixSortPushConstants), &pc);
        cmdList->dispatch(numHistWGs, 1, 1);

        // Barrier: histogram writes must be visible to scatter
        cmdList->resourceBarrier();

        // ---- Scatter Dispatch ----
        cmdList->setPipelineState(m_scatterPipeline);
        cmdList->bindDescriptorSet(m_scatterPipelineLayout, 0, scatterSet);
        cmdList->pushConstants(m_scatterPipelineLayout, EShaderStage::Compute, 0,
                               sizeof(FRadixSortPushConstants), &pc);
        cmdList->dispatch(numWGs, 1, 1);

        // Barrier: scatter writes must be visible to next histogram
        cmdList->resourceBarrier();
    }

    // After 4 even/odd passes, final result is in even buffers
    m_finalInEven = true;
    MR_LOG(LogTemp, Log, "[RadixSort] Completed 4-pass sort on %u elements", numElements);
    return true;
}

MonsterEngine::TSharedPtr<RHI::IRHIBuffer> FSplatRadixSortPass::getSortedKeys() const
{
    return m_finalInEven ? m_keysEven : m_keysOdd;
}

MonsterEngine::TSharedPtr<RHI::IRHIBuffer> FSplatRadixSortPass::getSortedValues() const
{
    return m_finalInEven ? m_valuesEven : m_valuesOdd;
}

// ============================================================================
// FSplatTileBoundariesPass
// ============================================================================

FSplatTileBoundariesPass::~FSplatTileBoundariesPass()
{
    m_tileRanges.Reset();
    m_pipelineState.Reset();
    m_pipelineLayout.Reset();
    for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
        m_descriptorSets[i].Reset();
    m_setLayout.Reset();
}

bool FSplatTileBoundariesPass::initialize(RHI::IRHIDevice* device, uint32 numTiles, uint32 maxSortElements)
{
    using namespace RHI;

    if (!device || numTiles == 0 || maxSortElements == 0)
    {
        MR_LOG(LogTemp, Error, "[TileBounds] Invalid parameters");
        return false;
    }

    m_numTiles = numTiles;

    // Descriptor set layout: 2 bindings (keys_in, ranges_out)
    {
        FDescriptorSetLayoutDesc desc(0, "TileBoundaries_Set");
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            ETileBoundariesBinding::SortKeys, EDescriptorType::StorageBuffer, EShaderStage::Compute));
        desc.bindings.Add(FDescriptorSetLayoutBinding(
            ETileBoundariesBinding::TileRanges, EDescriptorType::StorageBuffer, EShaderStage::Compute));

        m_setLayout = device->createDescriptorSetLayout(desc);
        if (!m_setLayout)
        {
            MR_LOG(LogTemp, Error, "[TileBounds] Failed to create descriptor set layout");
            return false;
        }
    }

    // Pipeline
    {
        FPushConstantRange pushRange(EShaderStage::Compute, 0, sizeof(FTileBoundariesPushConstants));
        if (!createSortPipeline(device, "Shaders/Splat/Sort/compiled/splat_tile_boundaries.spv",
                                m_setLayout, pushRange, m_pipelineLayout, m_pipelineState))
            return false;
    }

    // Allocate tile ranges buffer (pre-cleared to zero)
    // Tiles with no entries remain as (0,0)
    uint32 rangesSize = numTiles * 2 * sizeof(uint32); // uvec2 per tile
    {
        // Create zero-filled initial data
        TArray<uint32> zeros;
        zeros.SetNum(numTiles * 2);
        memset(zeros.GetData(), 0, rangesSize);

        BufferDesc desc;
        desc.size = rangesSize;
        desc.usage = EResourceUsage::StorageBuffer | EResourceUsage::TransferDst;
        desc.memoryUsage = EMemoryUsage::Default;
        desc.initialData = zeros.GetData();
        desc.initialDataSize = rangesSize;
        desc.debugName = "TileBoundaries_Ranges";

        m_tileRanges = device->createBuffer(desc);
        if (!m_tileRanges)
        {
            MR_LOG(LogTemp, Error, "[TileBounds] Failed to create tile ranges buffer");
            return false;
        }
    }

    // Allocate per-frame descriptor sets
    for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
    {
        m_descriptorSets[i] = device->allocateDescriptorSet(m_setLayout);
        if (!m_descriptorSets[i])
        {
            MR_LOG(LogTemp, Error, "[TileBounds] Failed to allocate descriptor set %u", i);
            return false;
        }

        // Pre-bind output ranges buffer (unchanged across frames)
        m_descriptorSets[i]->updateStorageBuffer(
            ETileBoundariesBinding::TileRanges, m_tileRanges, 0, 0);
    }

    m_bInitialized = true;
    MR_LOG(LogTemp, Log, "[TileBounds] Initialized: %u tiles", numTiles);
    return true;
}

void FSplatTileBoundariesPass::execute(RHI::IRHICommandList* cmdList, uint32 numSortElements)
{
    if (!m_bInitialized || !cmdList || numSortElements == 0)
        return;

    // Toggle per-frame descriptor set index
    m_currentDsIndex = (m_currentDsIndex + 1) % kMaxFramesInFlight;
    auto& ds = m_descriptorSets[m_currentDsIndex];

    // ---- Per-frame reset of tile ranges (clear mode) mode=1 ----
    // The boundary-detection pass below only writes tiles that are actually
    // touched by gaussians. Without a reset, tiles that lose their gaussians
    // between frames keep stale (start,end) ranges and re-draw ghost gaussians.
    {
        FTileBoundariesPushConstants clearPc;
        clearPc.numSortElements = m_numTiles;   // guard: only idx < numTiles run
        clearPc.numTiles        = m_numTiles;
        clearPc.mode            = 1u;            // clear mode
        uint32 clearGroups = (m_numTiles + 255u) / 256u;

        cmdList->setPipelineState(m_pipelineState);
        cmdList->bindDescriptorSet(m_pipelineLayout, 0, ds);
        cmdList->pushConstants(m_pipelineLayout, RHI::EShaderStage::Compute, 0,
                               sizeof(FTileBoundariesPushConstants), &clearPc);
        cmdList->dispatch(clearGroups, 1, 1);
        // Ensure the clear's writes are visible before boundary detection reads/writes.
        cmdList->resourceBarrier();
    }

    // ---- Boundary detection (mode 0) ----
    FTileBoundariesPushConstants pc;
    pc.numSortElements = numSortElements;
    pc.numTiles = m_numTiles;
    pc.mode = 0u;

    uint32 numGroups = (numSortElements + 255u) / 256u;

    cmdList->setPipelineState(m_pipelineState);
    cmdList->bindDescriptorSet(m_pipelineLayout, 0, ds);
    cmdList->pushConstants(m_pipelineLayout, RHI::EShaderStage::Compute, 0,
                           sizeof(FTileBoundariesPushConstants), &pc);
    cmdList->dispatch(numGroups, 1, 1);

    MR_LOG(LogTemp, Log, "[TileBounds] Dispatched clear(%u groups) + boundary(%u groups) for %u sort entries",
           (m_numTiles + 255u) / 256u, numGroups, numSortElements);
}

} // namespace Splat
} // namespace MonsterRender
