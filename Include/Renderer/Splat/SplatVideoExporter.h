#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"

#include <memory>

namespace MonsterRender::Splat
{

/** Fixed-rate, UI-free export of the 3DGS RGBA8 output texture to H.264 MP4. */
class FSplatVideoExporter
{
public:
    FSplatVideoExporter();
    ~FSplatVideoExporter();

    FSplatVideoExporter(const FSplatVideoExporter&) = delete;
    FSplatVideoExporter& operator=(const FSplatVideoExporter&) = delete;

    bool start(RHI::IRHIDevice* device, const String& outputPath,
               uint32 width, uint32 height, uint32 fps, double durationSeconds,
               const String& ffmpegExecutable = "ffmpeg");

    /** Record a copy while the 3DGS output is in GENERAL layout, before present. */
    bool captureFrame(RHI::IRHICommandList* cmdList,
                      MonsterEngine::TSharedPtr<RHI::IRHITexture> outputTexture);

    /** Call after the frame has been submitted/presented. Waits for its GPU copy. */
    bool finishFrame();

    /** Close the input pipe, wait for ffmpeg, and verify its exit status. */
    bool finish();

    /** Stop the encoder and remove an incomplete output file. */
    void cancel();

    bool isActive() const;
    uint64 getFrameIndex() const;
    uint64 getFrameCount() const;
    double getSampleTime() const;
    const String& getLastError() const;

private:
    struct FImplementation;
    std::unique_ptr<FImplementation> m_impl;
};

} // namespace MonsterRender::Splat
