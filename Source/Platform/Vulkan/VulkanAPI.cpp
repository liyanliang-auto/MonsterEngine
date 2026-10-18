#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"

#if PLATFORM_WINDOWS
#include <Windows.h>
#elif PLATFORM_ANDROID || PLATFORM_LINUX
#include <dlfcn.h>
#endif

namespace MonsterRender::RHI::Vulkan {
    namespace {
        void* OpenLoader() {
#if PLATFORM_WINDOWS
            return LoadLibraryW(L"vulkan-1.dll");
#elif PLATFORM_ANDROID
            return dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#elif PLATFORM_LINUX
            void* library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
            return library ? library : dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#else
            return nullptr;
#endif
        }
        void CloseLoader(void* library) {
            if (!library) return;
#if PLATFORM_WINDOWS
            FreeLibrary(static_cast<HMODULE>(library));
#elif PLATFORM_ANDROID || PLATFORM_LINUX
            dlclose(library);
#endif
        }
        PFN_vkGetInstanceProcAddr ResolveLoader(void* library) {
#if PLATFORM_WINDOWS
            return reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(static_cast<HMODULE>(library), "vkGetInstanceProcAddr"));
#elif PLATFORM_ANDROID || PLATFORM_LINUX
            return reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
#else
            return nullptr;
#endif
        }
    }

    VulkanFunctions VulkanAPI::s_functions{};
    bool VulkanAPI::s_initialized = false;

    bool VulkanAPI::acquireGlobalDispatch(VulkanFunctions& dispatch, String& error) {
        dispatch = {};
        error.clear();
        void* library = OpenLoader();
        if (!library) { error = "Vulkan loader library unavailable"; return false; }
        // RAII closes even a partially initialized loader; copies keep probe calls valid.
        VulkanFunctions next;
        next.loaderLifetime = std::shared_ptr<void>(library, CloseLoader);
        next.vkGetInstanceProcAddr = ResolveLoader(library);
        if (!next.vkGetInstanceProcAddr) { error = "Missing vkGetInstanceProcAddr"; return false; }
#define GLOBAL(name) next.name = reinterpret_cast<PFN_##name>(next.vkGetInstanceProcAddr(VK_NULL_HANDLE, #name))
        GLOBAL(vkCreateInstance);
        GLOBAL(vkEnumerateInstanceExtensionProperties);
        GLOBAL(vkEnumerateInstanceLayerProperties);
        GLOBAL(vkEnumerateInstanceVersion);
#undef GLOBAL
        if (!next.vkCreateInstance || !next.vkEnumerateInstanceExtensionProperties || !next.vkEnumerateInstanceLayerProperties) {
            error = "Vulkan loader is missing required global commands";
            return false;
        }
        dispatch = std::move(next);
        return true;
    }

    bool VulkanAPI::initialize() {
        if (s_initialized) return true;
        String error;
        if (!acquireGlobalDispatch(s_functions, error)) { MR_LOG_ERROR(error); return false; }
        s_initialized = true;
        return true;
    }

    void VulkanAPI::shutdown() {
        // Independent probes use their own tables and loader references.
        s_functions = {};
        s_initialized = false;
    }

    VkResult VulkanAPI::enumerateInstanceVersion(const VulkanFunctions& dispatch, uint32& version) {
        version = VK_API_VERSION_1_0;
        if (!dispatch.vkGetInstanceProcAddr) return VK_ERROR_INITIALIZATION_FAILED;
        if (!dispatch.vkEnumerateInstanceVersion) return VK_SUCCESS;
        uint32 reported = VK_API_VERSION_1_0;
        const VkResult result = dispatch.vkEnumerateInstanceVersion(&reported);
        if (result == VK_SUCCESS) version = reported;
        return result;
    }

    bool VulkanAPI::loadInstanceDispatch(VkInstance instance, VulkanFunctions& dispatch, String& error, bool requireSurface) {
        error.clear();
        if (!instance || !dispatch.vkGetInstanceProcAddr || !dispatch.loaderLifetime) {
            error = "Instance dispatch requires a live loader and instance"; return false;
        }
        bool complete = true;
        const auto get = dispatch.vkGetInstanceProcAddr;
#define OPTIONAL_INSTANCE(name) dispatch.name = reinterpret_cast<PFN_##name>(get(instance, #name))
#define LOAD_INSTANCE(name) do { OPTIONAL_INSTANCE(name); if (!dispatch.name) { complete = false; error += #name ";"; } } while (false)
        LOAD_INSTANCE(vkDestroyInstance);
        LOAD_INSTANCE(vkEnumeratePhysicalDevices);
        LOAD_INSTANCE(vkGetPhysicalDeviceProperties);
        LOAD_INSTANCE(vkGetPhysicalDeviceFeatures);
        LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
        LOAD_INSTANCE(vkEnumerateDeviceExtensionProperties);
        LOAD_INSTANCE(vkCreateDevice);
        LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties);
        LOAD_INSTANCE(vkGetPhysicalDeviceFormatProperties);
        LOAD_INSTANCE(vkGetDeviceProcAddr);
        LOAD_INSTANCE(vkGetPhysicalDeviceImageFormatProperties);
        // Resolution does not imply API/extension enablement: callers gate these invocations.
        OPTIONAL_INSTANCE(vkGetPhysicalDeviceFeatures2);
        OPTIONAL_INSTANCE(vkGetPhysicalDeviceProperties2);
        OPTIONAL_INSTANCE(vkGetPhysicalDeviceMemoryProperties2);
        OPTIONAL_INSTANCE(vkDestroySurfaceKHR);
        OPTIONAL_INSTANCE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
        OPTIONAL_INSTANCE(vkGetPhysicalDeviceSurfaceFormatsKHR);
        OPTIONAL_INSTANCE(vkGetPhysicalDeviceSurfacePresentModesKHR);
        OPTIONAL_INSTANCE(vkGetPhysicalDeviceSurfaceSupportKHR);
#if PLATFORM_ANDROID
        OPTIONAL_INSTANCE(vkCreateAndroidSurfaceKHR);
        const bool platformSurface = dispatch.vkCreateAndroidSurfaceKHR != nullptr;
#elif PLATFORM_WINDOWS
        OPTIONAL_INSTANCE(vkCreateWin32SurfaceKHR);
        const bool platformSurface = dispatch.vkCreateWin32SurfaceKHR != nullptr;
#elif PLATFORM_LINUX
        OPTIONAL_INSTANCE(vkCreateXlibSurfaceKHR);
        const bool platformSurface = dispatch.vkCreateXlibSurfaceKHR != nullptr;
#else
        const bool platformSurface = false;
#endif
        if (requireSurface && (!platformSurface ||
            !dispatch.vkDestroySurfaceKHR ||
            !dispatch.vkGetPhysicalDeviceSurfaceCapabilitiesKHR ||
            !dispatch.vkGetPhysicalDeviceSurfaceFormatsKHR ||
            !dispatch.vkGetPhysicalDeviceSurfacePresentModesKHR ||
            !dispatch.vkGetPhysicalDeviceSurfaceSupportKHR)) {
            complete = false; error += "Required platform/surface commands unavailable;";
        }
        OPTIONAL_INSTANCE(vkCreateDebugUtilsMessengerEXT);
        OPTIONAL_INSTANCE(vkDestroyDebugUtilsMessengerEXT);
#undef LOAD_INSTANCE
#undef OPTIONAL_INSTANCE
        return complete;
    }

    bool VulkanAPI::loadDeviceDispatch(VkDevice device, VulkanFunctions& dispatch, String& error, bool requireSwapchain) {
        error.clear();
        if (!device || !dispatch.vkGetDeviceProcAddr || !dispatch.loaderLifetime) {
            error = "Device dispatch requires a live instance dispatch and device"; return false;
        }
        bool complete = true;
        const auto get = dispatch.vkGetDeviceProcAddr;
#define OPTIONAL_DEVICE(name) dispatch.name = reinterpret_cast<PFN_##name>(get(device, #name))
#define LOAD_DEVICE(name) do { OPTIONAL_DEVICE(name); if (!dispatch.name) { complete = false; error += #name ";"; } } while (false)
        LOAD_DEVICE(vkDestroyDevice);
        LOAD_DEVICE(vkGetDeviceQueue);
        LOAD_DEVICE(vkCreateCommandPool);
        LOAD_DEVICE(vkDestroyCommandPool);
        LOAD_DEVICE(vkResetCommandPool);
        LOAD_DEVICE(vkAllocateCommandBuffers);
        LOAD_DEVICE(vkFreeCommandBuffers);
        LOAD_DEVICE(vkBeginCommandBuffer);
        LOAD_DEVICE(vkEndCommandBuffer);
        LOAD_DEVICE(vkResetCommandBuffer);
        LOAD_DEVICE(vkQueueSubmit);
        LOAD_DEVICE(vkQueueWaitIdle);
        LOAD_DEVICE(vkDeviceWaitIdle);
        LOAD_DEVICE(vkCreateSemaphore);
        LOAD_DEVICE(vkDestroySemaphore);
        LOAD_DEVICE(vkCreateFence);
        LOAD_DEVICE(vkDestroyFence);
        LOAD_DEVICE(vkWaitForFences);
        LOAD_DEVICE(vkResetFences);
        LOAD_DEVICE(vkGetFenceStatus);
        LOAD_DEVICE(vkAllocateMemory);
        LOAD_DEVICE(vkFreeMemory);
        LOAD_DEVICE(vkMapMemory);
        LOAD_DEVICE(vkUnmapMemory);
        LOAD_DEVICE(vkCreateBuffer);
        LOAD_DEVICE(vkDestroyBuffer);
        LOAD_DEVICE(vkGetBufferMemoryRequirements);
        LOAD_DEVICE(vkBindBufferMemory);
        LOAD_DEVICE(vkCreateImage);
        LOAD_DEVICE(vkDestroyImage);
        LOAD_DEVICE(vkGetImageMemoryRequirements);
        LOAD_DEVICE(vkBindImageMemory);
        LOAD_DEVICE(vkCreateImageView);
        LOAD_DEVICE(vkDestroyImageView);
        LOAD_DEVICE(vkCreateSampler);
        LOAD_DEVICE(vkDestroySampler);
        LOAD_DEVICE(vkCreateShaderModule);
        LOAD_DEVICE(vkDestroyShaderModule);
        LOAD_DEVICE(vkCreateRenderPass);
        LOAD_DEVICE(vkDestroyRenderPass);
        LOAD_DEVICE(vkCmdBeginRenderPass);
        LOAD_DEVICE(vkCmdEndRenderPass);
        LOAD_DEVICE(vkCreateFramebuffer);
        LOAD_DEVICE(vkDestroyFramebuffer);
        LOAD_DEVICE(vkCreateGraphicsPipelines);
        LOAD_DEVICE(vkCreateComputePipelines);
        LOAD_DEVICE(vkDestroyPipeline);
        LOAD_DEVICE(vkCreatePipelineLayout);
        LOAD_DEVICE(vkDestroyPipelineLayout);
        LOAD_DEVICE(vkCreatePipelineCache);
        LOAD_DEVICE(vkDestroyPipelineCache);
        LOAD_DEVICE(vkCmdDraw);
        LOAD_DEVICE(vkCmdDrawIndexed);
        LOAD_DEVICE(vkCmdBindVertexBuffers);
        LOAD_DEVICE(vkCmdBindIndexBuffer);
        LOAD_DEVICE(vkCmdBindPipeline);
        LOAD_DEVICE(vkCmdSetViewport);
        LOAD_DEVICE(vkCmdSetScissor);
        LOAD_DEVICE(vkCmdPushConstants);
        LOAD_DEVICE(vkCmdDispatch);
        LOAD_DEVICE(vkCmdCopyBuffer);
        LOAD_DEVICE(vkCmdCopyBufferToImage);
        LOAD_DEVICE(vkCmdCopyImage);
        LOAD_DEVICE(vkCmdBlitImage);
        LOAD_DEVICE(vkCmdPipelineBarrier);
        LOAD_DEVICE(vkCmdExecuteCommands);
        LOAD_DEVICE(vkCreateDescriptorSetLayout);
        LOAD_DEVICE(vkDestroyDescriptorSetLayout);
        LOAD_DEVICE(vkCreateDescriptorPool);
        LOAD_DEVICE(vkDestroyDescriptorPool);
        LOAD_DEVICE(vkResetDescriptorPool);
        LOAD_DEVICE(vkAllocateDescriptorSets);
        LOAD_DEVICE(vkFreeDescriptorSets);
        LOAD_DEVICE(vkUpdateDescriptorSets);
        LOAD_DEVICE(vkCmdBindDescriptorSets);
        LOAD_DEVICE(vkFlushMappedMemoryRanges);
        LOAD_DEVICE(vkInvalidateMappedMemoryRanges);
        LOAD_DEVICE(vkCmdDispatchIndirect);
        LOAD_DEVICE(vkCreateQueryPool);
        LOAD_DEVICE(vkDestroyQueryPool);
        LOAD_DEVICE(vkGetQueryPoolResults);
        LOAD_DEVICE(vkCmdResetQueryPool);
        LOAD_DEVICE(vkCmdWriteTimestamp);
        LOAD_DEVICE(vkCmdCopyImageToBuffer);
        LOAD_DEVICE(vkCmdClearColorImage);
        LOAD_DEVICE(vkCmdClearDepthStencilImage);
        OPTIONAL_DEVICE(vkCreateSwapchainKHR);
        OPTIONAL_DEVICE(vkDestroySwapchainKHR);
        OPTIONAL_DEVICE(vkGetSwapchainImagesKHR);
        OPTIONAL_DEVICE(vkAcquireNextImageKHR);
        OPTIONAL_DEVICE(vkQueuePresentKHR);
        if (requireSwapchain && (
            !dispatch.vkCreateSwapchainKHR ||
            !dispatch.vkDestroySwapchainKHR ||
            !dispatch.vkGetSwapchainImagesKHR ||
            !dispatch.vkAcquireNextImageKHR ||
            !dispatch.vkQueuePresentKHR)) {
            complete = false; error += "Required swapchain commands unavailable;";
        }
        OPTIONAL_DEVICE(vkCmdBeginDebugUtilsLabelEXT);
        OPTIONAL_DEVICE(vkCmdEndDebugUtilsLabelEXT);
        OPTIONAL_DEVICE(vkCmdInsertDebugUtilsLabelEXT);
#ifdef VK_EXT_swapchain_maintenance1
        OPTIONAL_DEVICE(vkReleaseSwapchainImagesEXT);
#endif
#undef LOAD_DEVICE
#undef OPTIONAL_DEVICE
        return complete;
    }

    bool VulkanAPI::loadInstanceFunctions(VkInstance instance) {
        String error;
        const bool result = loadInstanceDispatch(instance, s_functions, error, true);
        if (!result) MR_LOG_ERROR("Incomplete Vulkan instance dispatch: " + error);
        return result;
    }

    bool VulkanAPI::loadDeviceFunctions(VkDevice device) {
        String error;
        const bool result = loadDeviceDispatch(device, s_functions, error, true);
        if (!result) MR_LOG_ERROR("Incomplete Vulkan device dispatch: " + error);
        return result;
    }

    bool VulkanAPI::isAvailable() {
        // This checks loader commands, not physical-device render support.
        VulkanFunctions probe;
        String error;
        return acquireGlobalDispatch(probe, error);
    }
}
