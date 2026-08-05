/**
 * @file CopyBufferTest.cpp
 * @brief Integration test for GPU copyBuffer + staging readback.
 * 
 * Validates the full path:
 *   CPU-mapped source → copyBuffer → GPU destination → copyBuffer → staging → readback
 */

#include "Renderer/Splat/CopyBufferTest.h"
#include "Core/Logging/LogMacros.h"
#include "RHI/RHIDefinitions.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"

namespace MonsterRender::Splat
{

    bool FSplatCopyBufferTest::Run(RHI::IRHIDevice* device, uint32 testCount)
    {
        using namespace RHI;

        MR_LOG(LogTemp, Log, "=================================================");
        MR_LOG(LogTemp, Log, "[CopyBufTest] Starting copyBuffer + readback test (count=%u)", testCount);

        // ------------------------------------------------------------------
        // Validate prerequisites
        // ------------------------------------------------------------------
        if (!device || device->getRHIBackend() != ERHIBackend::Vulkan)
        {
            MR_LOG(LogTemp, Warning, "[CopyBufTest] Skipped — device is not Vulkan");
            return true;
        }

        Vulkan::VulkanDevice* vulkanDevice = static_cast<Vulkan::VulkanDevice*>(device);
        auto* context = vulkanDevice->getCommandListContext();
        if (!context)
        {
            MR_LOG(LogTemp, Error, "[CopyBufTest] No command list context available");
            return false;
        }

        uint32 bufferSizeBytes = testCount * sizeof(uint32);

        // ------------------------------------------------------------------
        // Phase 1: Create source buffer (CPU → GPU: Upload memory)
        // ------------------------------------------------------------------
        TSharedPtr<IRHIBuffer> srcBuffer;
        {
            BufferDesc desc;
            desc.size         = bufferSizeBytes;
            desc.usage        = EResourceUsage::TransferSrc;
            desc.memoryUsage  = EMemoryUsage::Upload;
            desc.cpuAccessible = true;
            desc.debugName    = "CopyBufTest_Src";

            srcBuffer = device->createBuffer(desc);
            if (!srcBuffer)
            {
                MR_LOG(LogTemp, Error, "[CopyBufTest] Failed to create source buffer");
                return false;
            }

            // Fill with pattern [0, 1, 2, ..., testCount-1]
            void* mapped = srcBuffer->map();
            if (!mapped)
            {
                MR_LOG(LogTemp, Error, "[CopyBufTest] Failed to map source buffer");
                return false;
            }

            uint32* data = static_cast<uint32*>(mapped);
            for (uint32 i = 0; i < testCount; ++i)
                data[i] = i;

            srcBuffer->unmap();
            MR_LOG(LogTemp, Log, "[CopyBufTest] Source buffer created: %u bytes, pattern [0..%u]",
                   bufferSizeBytes, testCount - 1);
        }

        // ------------------------------------------------------------------
        // Phase 2: Create destination buffer (GPU-only)
        // ------------------------------------------------------------------
        TSharedPtr<IRHIBuffer> dstBuffer;
        {
            BufferDesc desc;
            desc.size      = bufferSizeBytes;
            desc.usage     = EResourceUsage::TransferDst | EResourceUsage::TransferSrc;
            desc.memoryUsage = EMemoryUsage::Default;
            desc.debugName = "CopyBufTest_Dst";

            dstBuffer = device->createBuffer(desc);
            if (!dstBuffer)
            {
                MR_LOG(LogTemp, Error, "[CopyBufTest] Failed to create destination buffer");
                return false;
            }
            MR_LOG(LogTemp, Log, "[CopyBufTest] GPU destination buffer created");
        }

        // ------------------------------------------------------------------
        // Phase 3: Create staging buffer (GPU → CPU: Readback memory)
        // ------------------------------------------------------------------
        TSharedPtr<IRHIBuffer> stagingBuffer;
        {
            BufferDesc desc;
            desc.size         = bufferSizeBytes;
            desc.usage        = EResourceUsage::TransferDst;
            desc.memoryUsage  = EMemoryUsage::Readback;
            desc.cpuAccessible = true;
            desc.debugName    = "CopyBufTest_Staging";

            stagingBuffer = device->createBuffer(desc);
            if (!stagingBuffer)
            {
                MR_LOG(LogTemp, Error, "[CopyBufTest] Failed to create staging buffer");
                return false;
            }
            MR_LOG(LogTemp, Log, "[CopyBufTest] Readback staging buffer created");
        }

        // ------------------------------------------------------------------
        // Phase 4: Record copy commands via FVulkanRHICommandListImmediate
        // ------------------------------------------------------------------
        {
            MR_LOG(LogTemp, Log, "[CopyBufTest] Recording copyBuffer commands...");

            auto cmdList = MonsterEngine::MakeShared<Vulkan::FVulkanRHICommandListImmediate>(vulkanDevice);
            cmdList->begin();

            // Step 4a: copyBuffer(src → dst), full source range
            cmdList->copyBuffer(dstBuffer, srcBuffer, bufferSizeBytes);
            cmdList->resourceBarrier();

            // Step 4b: copyBuffer(dst → staging), for readback verification
            cmdList->copyBuffer(stagingBuffer, dstBuffer, bufferSizeBytes);

            cmdList->end();

            MR_LOG(LogTemp, Log, "[CopyBufTest] Recorded: src(%uB) → dst → staging(%uB)",
                   bufferSizeBytes, bufferSizeBytes);
        }

        // ------------------------------------------------------------------
        // Phase 5: Submit to GPU and wait for completion
        // ------------------------------------------------------------------
        {
            MR_LOG(LogTemp, Log, "[CopyBufTest] Submitting commands to GPU...");

            // Submit the recorded commands
            context->submitCommands(nullptr, 0, nullptr, 0);

            // Wait for GPU to complete all work
            vulkanDevice->waitForIdle();

            MR_LOG(LogTemp, Log, "[CopyBufTest] GPU work complete");
        }

        // ------------------------------------------------------------------
        // Phase 6: Readback and verify
        // ------------------------------------------------------------------
        {
            MR_LOG(LogTemp, Log, "[CopyBufTest] Reading back staging buffer...");

            void* mapped = stagingBuffer->map();
            if (!mapped)
            {
                MR_LOG(LogTemp, Error, "[CopyBufTest] Failed to map staging buffer for readback");
                return false;
            }

            const uint32* readback = static_cast<const uint32*>(mapped);

            // Verify: all elements should match the original pattern
            bool allMatch = true;
            uint32 errorCount = 0;

            for (uint32 i = 0; i < testCount; ++i)
            {
                if (readback[i] != i)
                {
                    if (errorCount < 5)  // Log first 5 errors only
                    {
                        MR_LOG(LogTemp, Error, "[CopyBufTest] Mismatch at index %u: expected=%u, got=%u",
                               i, i, readback[i]);
                    }
                    allMatch = false;
                    ++errorCount;
                }
            }

            stagingBuffer->unmap();

            if (allMatch)
            {
                MR_LOG(LogTemp, Log, "[CopyBufTest] PASS — All %u elements match expected pattern", testCount);
            }
            else
            {
                MR_LOG(LogTemp, Error, "[CopyBufTest] FAIL — %u/%u elements mismatched",
                       errorCount, testCount);
            }

            MR_LOG(LogTemp, Log, "=================================================");
            return allMatch;
        }
    }

} // namespace MonsterRender::Splat
