/**
 * @file CopyBufferTest.h
 * @brief Integration test for RHI copyBuffer and staging readback.
 * 
 * Usage (from CubeSceneApplication::OnPostInit or similar):
 * @code
 *   #include "Renderer/Splat/CopyBufferTest.h"
 *   if (m_device->getRHIBackend() == RHI::ERHIBackend::Vulkan) {
 *       FSplatCopyBufferTest::Run(m_device);
 *   }
 * @endcode
 * 
 * Test steps:
 *   1. Create source buffer with known pattern [0, 1, 2, ..., N-1]
 *   2. Create destination buffer (empty, GPU-only)
 *   3. Create readback staging buffer (host-visible, 4 bytes)
 *   4. Record: copyBuffer(src → dst, first element only)
 *   5. Record: copyBuffer(dst → staging, 4 bytes)
 *   6. Submit to GPU via FVulkanRHICommandListImmediate
 *   7. waitForIdle, map staging buffer
 *   8. Verify: staging[0] == 0 (the first value copied)
 */

#pragma once

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "RHI/IRHIDevice.h"

namespace MonsterRender::Splat
{
    /**
     * FSplatCopyBufferTest — Validates GPU copyBuffer + staging readback.
     * 
     * Creates a known source pattern, copies via copyBuffer to a GPU-only
     * destination, then copies to a host-visible staging buffer and verifies
     * the readback data matches.
     */
    class FSplatCopyBufferTest
    {
    public:
        /**
         * Run the copyBuffer + readback integration test.
         * 
         * @param device    RHI device (must be Vulkan backend)
         * @param testCount Number of uint32 elements to copy (default 64)
         * @return true if all verifications pass
         */
        static bool Run(RHI::IRHIDevice* device, uint32 testCount = 64);
    };

} // namespace MonsterRender::Splat
