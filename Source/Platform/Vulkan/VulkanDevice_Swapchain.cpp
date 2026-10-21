#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Platform/Vulkan/VulkanUtils.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Core/Logging/LogMacros.h"
#include <algorithm>
#if PLATFORM_ANDROID
#include <android/native_window.h>
#endif

namespace MonsterRender::RHI::Vulkan {
    bool VulkanDevice::ConfigurePresentationVSync(bool enabled) {
        m_presentationVSync = enabled;
        return recreateSwapchain(m_swapchainExtent.width, m_swapchainExtent.height);
    }

    bool VulkanDevice::DrainCompletedDestructions() {
        if (!m_device) return false;
        const VkResult result = VulkanAPI::getFunctions().vkDeviceWaitIdle(m_device);
        if (result != VK_SUCCESS) { if (result == VK_ERROR_DEVICE_LOST) m_deviceLost = true; return false; }
        {
            std::lock_guard<std::mutex> lock(m_submissionMutex);
            m_completedSubmission = m_lastSubmission;
            m_submissions.clear();
        }
        for (uint32 i = 0; i <= DEFERRED_DESTRUCTION_FRAMES; ++i) processDeferredDestructions();
        return true;
    }

    VkResult VulkanDevice::waitPresentationCompletion(uint64 timeoutNs) {
        const auto& f = VulkanAPI::getFunctions();
        if (!m_device || m_deviceLost) return m_deviceLost ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
        if (m_presentMaintenanceEnabled) {
            for (size_t i = 0; i < m_presentFences.size(); ++i) {
                if (!m_presentFencePending[i]) continue;
                const VkResult result = [&] { MonsterEngine::Profiling::Scope scope("vulkan.retire_present_fence",m_profilingContext);const auto observed=f.vkWaitForFences(m_device, 1, &m_presentFences[i], VK_TRUE, timeoutNs);scope.SetValue(MonsterEngine::Profiling::EncodeSignedValue(observed));return observed;}();
                if (result != VK_SUCCESS) return result;
                m_presentFencePending[i] = false;
            }
            return VK_SUCCESS;
        }
        // Unextended WSI has no spec-level presentation completion primitive.
        // This is a teardown-only platform assumption, never a normal frame wait.
        MR_LOG_WARNING("Present teardown uses queue/device idle fallback; precise presentation completion unavailable");
        return m_presentQueue ? [&] { MonsterEngine::Profiling::Scope scope("vulkan.present_queue_idle",m_profilingContext);const auto observed=f.vkQueueWaitIdle(m_presentQueue);scope.SetValue(MonsterEngine::Profiling::EncodeSignedValue(observed));return observed;}() : VK_SUCCESS;
    }

    bool VulkanDevice::createImageSynchronization() {
        const auto& f = VulkanAPI::getFunctions();
        m_perImageRenderFinishedSemaphores.assign(m_swapchainImages.size(), VK_NULL_HANDLE);
        m_imagesInFlight.assign(m_swapchainImages.size(), VK_NULL_HANDLE);
        m_presentFences.assign(m_swapchainImages.size(), VK_NULL_HANDLE);
        m_presentFencePending.assign(m_swapchainImages.size(), false);
        VkSemaphoreCreateInfo semaphore{}; semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence{}; fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        for (size_t i = 0; i < m_swapchainImages.size(); ++i) {
            if (f.vkCreateSemaphore(m_device, &semaphore, nullptr, &m_perImageRenderFinishedSemaphores[i]) != VK_SUCCESS) return false;
            if (m_presentMaintenanceEnabled &&
                f.vkCreateFence(m_device, &fence, nullptr, &m_presentFences[i]) != VK_SUCCESS) return false;
        }
        return true;
    }

    void VulkanDevice::destroyPresentationResources() {
        const auto& f = VulkanAPI::getFunctions();
        for (auto framebuffer : m_swapchainFramebuffers) if (framebuffer) f.vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        m_swapchainFramebuffers.clear();
        destroyDepthResources();
        destroyMSAAResources();
        for (auto view : m_swapchainImageViews) if (view) f.vkDestroyImageView(m_device, view, nullptr);
        m_swapchainImageViews.clear();
        m_swapchainImages.clear();
        for (auto semaphore : m_perImageRenderFinishedSemaphores) if (semaphore) f.vkDestroySemaphore(m_device, semaphore, nullptr);
        m_perImageRenderFinishedSemaphores.clear();
        for (auto fence : m_presentFences) if (fence) f.vkDestroyFence(m_device, fence, nullptr);
        m_presentFences.clear(); m_presentFencePending.clear(); m_imagesInFlight.clear();
        if (m_swapchain) f.vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
        if (m_renderPass) f.vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        if (m_rttRenderPass) f.vkDestroyRenderPass(m_device, m_rttRenderPass, nullptr);
        m_renderPass = m_rttRenderPass = VK_NULL_HANDLE;
        m_frameAcquired = false;
        m_swapchainExtent = {0, 0};
        m_presentationLease.reset(); m_presentationReservationEstimate = 0; m_presentationReservedImages = 0;
    }

    FFrameSubmitResult VulkanDevice::DetachExternalSurface(uint64 generation) {
        FFrameSubmitResult result;
        result.surfaceGeneration = m_surfaceGeneration;
        if (generation && generation != m_surfaceGeneration) { result.status = EFrameResult::OutOfDate; return result; }
        if (m_frameAcquired && !m_deviceLost) {
            result = cancelFrame();
            if (!result.submitted && !m_deviceLost) return result;
        }
        m_presentationStopped = true;
        if (m_device && !m_deviceLost && !DrainCompletedDestructions() && !m_deviceLost) {
            result.status = EFrameResult::Error; return result;
        }
        const VkResult completed = waitPresentationCompletion(5000000);
        if (completed != VK_SUCCESS && completed != VK_ERROR_DEVICE_LOST) {
            result.status = ClassifyFrameResult(completed);
            return result; // Still owns window; caller must retry before acknowledging detach.
        }
        if (m_device) destroyPresentationResources();
        const auto& f = VulkanAPI::getFunctions();
        if (m_surface) f.vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
#if PLATFORM_ANDROID
        if (m_externalWindow) ANativeWindow_release(static_cast<ANativeWindow*>(m_externalWindow));
#endif
        m_externalWindow = nullptr;
        result.status = m_deviceLost ? EFrameResult::DeviceLost : EFrameResult::Success;
        return result;
    }

    bool VulkanDevice::AttachExternalSurface(void* window, uint64 generation, uint32 width, uint32 height, String& error) {
        error.clear();
        if (!window || !generation || !width || !height || !m_device || m_deviceLost) {
            error = "External Surface requires a live device, nonzero generation and extent"; return false;
        }
        if (m_surface && m_externalWindow == window && m_surfaceGeneration == generation)
            return recreateSwapchain(width, height);
        if (m_surface) {
            const auto detached = DetachExternalSurface(m_surfaceGeneration);
            if (detached.status != EFrameResult::Success) { error = "Previous Surface is still retiring"; return false; }
        }
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const auto& f = VulkanAPI::getFunctions();
        VkResult created = VulkanUtils::createSurface(m_instance, window, &surface);
        if (created != VK_SUCCESS) { error = "External Surface creation failed: " + std::to_string(created); return false; }
        VkBool32 present = VK_FALSE;
        created = f.vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_presentQueueFamily.familyIndex, surface, &present);
        if (created != VK_SUCCESS || !present) {
            f.vkDestroySurfaceKHR(m_instance, surface, nullptr);
            error = "New Surface is unsupported by the enabled presentation queue"; return false;
        }
#if PLATFORM_ANDROID
        ANativeWindow_acquire(static_cast<ANativeWindow*>(window));
#endif
        m_surface = surface; m_externalWindow = window; m_surfaceGeneration = generation;
        m_presentationStopped = false;
        if (!recreateSwapchain(width, height)) { error = "External Surface swapchain creation failed"; return false; }
        return true;
    }

    bool VulkanDevice::recreateSwapchain(uint32 width, uint32 height) {
        MonsterEngine::Profiling::Scope scope("vulkan.recreate_swapchain",m_profilingContext);
        if (!width || !height || !m_device || !m_surface || m_deviceLost) return false;
        std::shared_ptr<void> nextLease;
        uint64 envelope = 0;
        uint32 reservedImages = 0;
        if (m_presentationBudget) {
            VkSurfaceCapabilitiesKHR caps{};
            if (VulkanAPI::getFunctions().vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice,m_surface,&caps) != VK_SUCCESS) return false;
            const uint64 w = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent.width : std::clamp(width,caps.minImageExtent.width,caps.maxImageExtent.width);
            const uint64 h = caps.currentExtent.height != UINT32_MAX ? caps.currentExtent.height : std::clamp(height,caps.minImageExtent.height,caps.maxImageExtent.height);
            uint64 images = uint64(caps.minImageCount) + 1;
            if (caps.maxImageCount) images = std::min(images,uint64(caps.maxImageCount));
            // WSI can return more than the requested minimum. Admit two spare
            // images; reject a larger actual count before views/attachments.
            images += 2;
            if (caps.maxImageCount) images = std::min(images,uint64(caps.maxImageCount));
            if(images>64)return false;
            reservedImages=uint32(images);
            uint32 formatCount=0;const auto& f=VulkanAPI::getFunctions();
            if(f.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice,m_surface,&formatCount,nullptr)!=VK_SUCCESS||!formatCount||formatCount>4096)return false;
            std::vector<VkSurfaceFormatKHR> formats(formatCount);
            if(f.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice,m_surface,&formatCount,formats.data())!=VK_SUCCESS)return false;
            auto format=formats.front().format;
            for(const auto& candidate:formats)if(candidate.format==VK_FORMAT_B8G8R8A8_SRGB&&candidate.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){format=candidate.format;break;}
            uint64 colorBytes=16; // Unknown formats retain a conservative 128-bit upper envelope.
            switch(format){
            case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_R8G8B8A8_SRGB:case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_B8G8R8A8_SRGB:
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32:case VK_FORMAT_A2R10G10B10_UNORM_PACK32:colorBytes=4;break;
            case VK_FORMAT_R16G16B16A16_SFLOAT:case VK_FORMAT_R16G16B16A16_UNORM:colorBytes=8;break;
            }
            const uint64 samples=uint64(m_msaaSampleCount);
            const uint64 depthBytes=m_depthFormat==VK_FORMAT_D32_SFLOAT_S8_UINT?8:4;
            // Count each WSI image and depth/MSAA attachment once. Double their
            // byte upper bound for opaque driver/alignment overhead, plus 16 MiB
            // metadata. This remains a policy estimate, never measured WSI backing.
            const uint64 bytesPerPixel=2*(colorBytes*images+depthBytes*samples+(samples>1?colorBytes*samples:0));
            if (!w || !h || images > 1024 || !bytesPerPixel || w > UINT64_MAX / h || w*h > (UINT64_MAX-(16ull<<20))/bytesPerPixel) return false;
            envelope = w*h*bytesPerPixel+(16ull<<20);
            nextLease = m_presentationBudget(envelope);
            if (!nextLease) { MR_LOG_ERROR("Presentation budget admission rejected estimate bytes="+std::to_string(envelope)); return false; } // old+new overlap is admitted first
        }
        if (m_frameAcquired && !cancelFrame().submitted) return false;
        m_presentationStopped = true;
        if (!DrainCompletedDestructions() || waitPresentationCompletion(5000000) != VK_SUCCESS) return false;
        destroyPresentationResources();
        m_presentationLease = std::move(nextLease); m_presentationReservationEstimate = envelope; m_presentationReservedImages = reservedImages;
        RHICreateInfo info;
        info.windowWidth = width; info.windowHeight = height;
        if (!createSwapchain(info) || !createDepthResources() || !createMSAAResources() ||
            !createRenderPass() || !createFramebuffers() || !createImageSynchronization()) return false;
        m_presentationStopped = false;
        return true;
    }
}
