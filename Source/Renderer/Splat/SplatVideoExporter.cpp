#include "Renderer/Splat/SplatVideoExporter.h"

#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanTexture.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>

#if PLATFORM_WINDOWS
#define NOMINMAX
#include <Windows.h>
#endif

namespace MonsterRender::Splat
{

namespace
{
#if PLATFORM_WINDOWS
std::wstring toWide(const String& value)
{
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        value.c_str(), -1, nullptr, 0);
    if (count <= 0) return {};
    std::wstring wide(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        value.c_str(), -1, wide.data(), count);
    wide.pop_back();
    return wide;
}

std::wstring quote(const std::wstring& value)
{
    // Windows CreateProcess quoting, including backslashes before quotes.
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value)
    {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'"')
        {
            result.append(slashes * 2 + 1, L'\\');
            result += ch;
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result += ch;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}
#endif
}

struct FSplatVideoExporter::FImplementation
{
    RHI::IRHIDevice* device = nullptr;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> readback;
    String path;
    String error;
    uint32 width = 0;
    uint32 height = 0;
    uint32 fps = 0;
    double duration = 0.0;
    uint64 frameIndex = 0;
    uint64 frameCount = 0;
    uint32 frameBytes = 0;
    bool active = false;
    bool copyPending = false;
#if PLATFORM_WINDOWS
    HANDLE pipeWrite = INVALID_HANDLE_VALUE;
    HANDLE process = INVALID_HANDLE_VALUE;
    HANDLE processThread = INVALID_HANDLE_VALUE;
#endif

    void closePipe()
    {
#if PLATFORM_WINDOWS
        if (pipeWrite != INVALID_HANDLE_VALUE)
        {
            CloseHandle(pipeWrite);
            pipeWrite = INVALID_HANDLE_VALUE;
        }
#endif
    }

    void closeProcess(bool terminate)
    {
#if PLATFORM_WINDOWS
        if (process != INVALID_HANDLE_VALUE)
        {
            if (terminate) TerminateProcess(process, 1);
            WaitForSingleObject(process, INFINITE);
            CloseHandle(process);
            process = INVALID_HANDLE_VALUE;
        }
        if (processThread != INVALID_HANDLE_VALUE)
        {
            CloseHandle(processThread);
            processThread = INVALID_HANDLE_VALUE;
        }
#endif
    }

    void removeIncomplete()
    {
        if (path.empty()) return;
        std::error_code ec;
#if PLATFORM_WINDOWS
        std::filesystem::remove(std::filesystem::path(toWide(path)), ec);
#else
        std::filesystem::remove(std::filesystem::path(path), ec);
#endif
    }
};

FSplatVideoExporter::FSplatVideoExporter()
    : m_impl(std::make_unique<FImplementation>())
{}

FSplatVideoExporter::~FSplatVideoExporter()
{
    cancel();
}

bool FSplatVideoExporter::start(RHI::IRHIDevice* device, const String& outputPath,
                                uint32 width, uint32 height, uint32 fps,
                                double durationSeconds, const String& ffmpegExecutable)
{
    cancel();
    auto& state = *m_impl;
    state.error.clear();

#if !PLATFORM_WINDOWS
    state.error = "Video export currently requires Windows";
    return false;
#else
    if (!device || device->getBackendType() != RHI::ERHIBackend::Vulkan ||
        width == 0 || height == 0 ||
        fps == 0 || fps > 240 ||
        !std::isfinite(durationSeconds) || durationSeconds <= 0.0 ||
        outputPath.empty() || ffmpegExecutable.empty())
    {
        state.error = "Invalid video export settings";
        return false;
    }

    const uint64 byteCount = uint64(width) * uint64(height) * 4u;
    const double proposedCount = std::ceil(durationSeconds * fps) + 1.0;
    if (byteCount > std::numeric_limits<uint32>::max() ||
        !std::isfinite(proposedCount) || proposedCount > 1000000.0)
    {
        state.error = "Video export dimensions or duration are too large";
        return false;
    }

    const auto widePath = toWide(outputPath);
    const auto wideExe = toWide(ffmpegExecutable);
    if (widePath.empty() || wideExe.empty())
    {
        state.error = "Video path is not valid UTF-8";
        return false;
    }

    state.path = outputPath;
    state.device = device;
    state.width = width;
    state.height = height;
    state.fps = fps;
    state.duration = durationSeconds;
    state.frameBytes = static_cast<uint32>(byteCount);
    state.frameCount = static_cast<uint64>(proposedCount);

    RHI::BufferDesc bufferDesc{};
    bufferDesc.size = state.frameBytes;
    bufferDesc.usage = RHI::EResourceUsage::TransferDst;
    bufferDesc.memoryUsage = RHI::EMemoryUsage::Readback;
    bufferDesc.cpuAccessible = true;
    bufferDesc.debugName = "3DGS video readback";
    state.readback = device->createBuffer(bufferDesc);
    if (!state.readback)
    {
        state.error = "Could not allocate GPU readback buffer";
        state.device = nullptr;
        return false;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE pipeRead = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&pipeRead, &state.pipeWrite, &security, 0) ||
        !SetHandleInformation(state.pipeWrite, HANDLE_FLAG_INHERIT, 0))
    {
        if (pipeRead != INVALID_HANDLE_VALUE) CloseHandle(pipeRead);
        state.closePipe();
        state.readback.Reset();
        state.error = "Could not create ffmpeg input pipe";
        return false;
    }

    HANDLE nullOutput = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
                                    &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullOutput == INVALID_HANDLE_VALUE)
    {
        CloseHandle(pipeRead);
        state.closePipe();
        state.readback.Reset();
        state.error = "Could not open ffmpeg log sink";
        return false;
    }

    std::wstring command = quote(wideExe) + L" -hide_banner -loglevel error -nostdin -y -f rawvideo -pixel_format rgba -video_size "
        + std::to_wstring(width) + L"x" + std::to_wstring(height)
        + L" -framerate " + std::to_wstring(fps)
        + L" -i pipe:0 -an -vf vflip,pad=ceil(iw/2)*2:ceil(ih/2)*2 -c:v libx264 -pix_fmt yuv420p -movflags +faststart -f mp4 "
        + quote(widePath);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = pipeRead;
    startup.hStdOutput = nullOutput;
    startup.hStdError = nullOutput;
    PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr,
                                         TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                         &startup, &process);
    CloseHandle(pipeRead);
    CloseHandle(nullOutput);
    if (!launched)
    {
        state.closePipe();
        state.readback.Reset();
        state.error = "Could not start ffmpeg; check its executable path";
        return false;
    }
    state.process = process.hProcess;
    state.processThread = process.hThread;
    state.active = true;
    return true;
#endif
}

bool FSplatVideoExporter::captureFrame(RHI::IRHICommandList* cmdList,
    MonsterEngine::TSharedPtr<RHI::IRHITexture> outputTexture)
{
    auto& state = *m_impl;
    if (!state.active || state.copyPending || state.frameIndex >= state.frameCount ||
        !cmdList || !outputTexture || !state.readback ||
        outputTexture->getWidth() != state.width ||
        outputTexture->getHeight() != state.height ||
        outputTexture->getDesc().format != RHI::EPixelFormat::R8G8B8A8_UNORM ||
        !RHI::hasResourceUsage(outputTexture->getDesc().usage, RHI::EResourceUsage::TransferSrc))
    {
        state.error = "Output texture is not a readable RGBA8 export frame";
        return false;
    }
    auto* vulkanCommand = dynamic_cast<RHI::Vulkan::FVulkanRHICommandListImmediate*>(cmdList);
    auto* vulkanTexture = dynamic_cast<RHI::Vulkan::VulkanTexture*>(outputTexture.Get());
    auto* vulkanBuffer = dynamic_cast<RHI::Vulkan::VulkanBuffer*>(state.readback.Get());
    if (!vulkanCommand || !vulkanTexture || !vulkanBuffer ||
        !vulkanCommand->GetVulkanCommandBuffer())
    {
        state.error = "Video export requires a Vulkan command buffer";
        return false;
    }
    const VkCommandBuffer command = vulkanCommand->GetVulkanCommandBuffer()->getHandle();
    if (command == VK_NULL_HANDLE || vulkanTexture->getImage() == VK_NULL_HANDLE ||
        vulkanBuffer->getBuffer() == VK_NULL_HANDLE)
    {
        state.error = "Invalid Vulkan video export resources";
        return false;
    }

    VkImageMemoryBarrier before{};
    before.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    before.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    before.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.image = vulkanTexture->getImage();
    before.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &before);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0; // tightly packed RGBA rows
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {state.width, state.height, 1};
    vkCmdCopyImageToBuffer(command, vulkanTexture->getImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           vulkanBuffer->getBuffer(), 1, &region);

    VkImageMemoryBarrier after = before;
    after.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    after.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    after.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &after);
    state.copyPending = true;
    return true;
}

bool FSplatVideoExporter::finishFrame()
{
    auto& state = *m_impl;
    if (!state.active || !state.copyPending || !state.device)
    {
        state.error = "No video frame is pending";
        return false;
    }
    state.device->waitForIdle();
    auto* data = static_cast<const uint8*>(state.readback->map());
    if (!data)
    {
        state.error = "Could not map video readback buffer";
        cancel();
        return false;
    }
#if PLATFORM_WINDOWS
    size_t written = 0;
    while (written < state.frameBytes)
    {
        DWORD chunkWritten = 0;
        const DWORD chunkSize = static_cast<DWORD>(std::min<size_t>(
            state.frameBytes - written, 1u << 20));
        if (!WriteFile(state.pipeWrite, data + written, chunkSize, &chunkWritten, nullptr) ||
            chunkWritten == 0)
        {
            state.readback->unmap();
            state.error = "ffmpeg stopped accepting video frames";
            cancel();
            return false;
        }
        written += chunkWritten;
    }
#endif
    state.readback->unmap();
    state.copyPending = false;
    ++state.frameIndex;
    return true;
}

bool FSplatVideoExporter::finish()
{
    auto& state = *m_impl;
    if (!state.active || state.copyPending || state.frameIndex != state.frameCount)
    {
        state.error = "Video export has incomplete frames";
        return false;
    }
    state.closePipe();
#if PLATFORM_WINDOWS
    WaitForSingleObject(state.process, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(state.process, &exitCode);
    state.closeProcess(false);
    if (exitCode != 0)
    {
        state.error = "ffmpeg failed while encoding MP4 (exit code " +
                      std::to_string(exitCode) + ")";
        state.removeIncomplete();
        state.active = false;
        state.readback.Reset();
        return false;
    }
#endif
    std::error_code ec;
#if PLATFORM_WINDOWS
    const auto outputFile = std::filesystem::path(toWide(state.path));
#else
    const auto outputFile = std::filesystem::path(state.path);
#endif
    const auto fileSize = std::filesystem::file_size(outputFile, ec);
    if (ec || fileSize == 0)
    {
        state.error = "ffmpeg did not produce a valid MP4 file";
        state.removeIncomplete();
        state.active = false;
        state.readback.Reset();
        return false;
    }
    state.active = false;
    state.readback.Reset();
    state.device = nullptr;
    return true;
}

void FSplatVideoExporter::cancel()
{
    auto& state = *m_impl;
    if (state.copyPending && state.device) state.device->waitForIdle();
    state.closePipe();
    state.closeProcess(true);
    if (state.active) state.removeIncomplete();
    state.readback.Reset();
    state.device = nullptr;
    state.active = false;
    state.copyPending = false;
    state.frameIndex = 0;
    state.frameCount = 0;
}

bool FSplatVideoExporter::isActive() const { return m_impl->active; }
uint64 FSplatVideoExporter::getFrameIndex() const { return m_impl->frameIndex; }
uint64 FSplatVideoExporter::getFrameCount() const { return m_impl->frameCount; }
double FSplatVideoExporter::getSampleTime() const
{
    if (!m_impl->fps || !m_impl->frameCount) return 0.0;
    if (m_impl->frameIndex + 1 >= m_impl->frameCount) return m_impl->duration;
    return std::min(m_impl->duration,
                    static_cast<double>(m_impl->frameIndex) / m_impl->fps);
}
const String& FSplatVideoExporter::getLastError() const { return m_impl->error; }

} // namespace MonsterRender::Splat
