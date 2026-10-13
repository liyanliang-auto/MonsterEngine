// Copyright Monster Engine. All Rights Reserved.
#pragma once

/**
 * @file SplatSceneApplication.h
 * @brief 3DGS Splat render demo — loads a .ply file and renders via SplatPipeline.
 *
 * Minimal application: PLY load → SplatPipeline (6 compute passes) → fullscreen
 * present pass → swapchain. Uses FPS camera (WASD + right-click mouse look).
 */

#include "Core/Application.h"
#include "Core/Templates/SharedPointer.h"
#include "RHI/RHI.h"
#include "Math/MonsterMath.h"
#include "Renderer/Splat/SplatPipeline.h"
#include "Renderer/Splat/SplatPLYLoader.h"
#include "Renderer/Splat/SplatTypes.h"
#include "Renderer/Splat/SplatControlState.h"
#include "Renderer/Splat/SplatCameraSequence.h"
#include "Renderer/Splat/SplatVideoExporter.h"
#include "Renderer/Splat/LOD/SplatLODSelector.h"
#include "Renderer/Splat/LOD/SplatLODBenchmark.h"
#include <chrono>
#include <set>

// Forward declarations
namespace MonsterEngine
{
    class FFPSCameraController;
    class FCameraManager;
    namespace Editor { class FImGuiContext; class FImGuiRenderer; class FImGuiInputHandler; }
}

namespace MonsterRender::Splat
{
    struct FSplatGPUData;
    class FSplatPipeline;
}

namespace MonsterRender
{

/**
 * SplatSceneApplication — loads a 3DGS .ply file and renders it interactively.
 *
 * Usage: MonsterEngine.exe --splat [path/to/model.ply]
 * Default model: Bicycle PLY (bounded LOD showcase config in main.cpp).
 *
 * Controls:
 *   WASD      — move camera
 *   Right-click + drag — look around
 *   Scroll    — zoom (FOV)
 *   ESC       — exit
 */
class SplatSceneApplication : public Application
{
public:
    struct FStartupExportOptions
    {
        String sequencePath;
        String videoPath;
        String ffmpegPath = "ffmpeg";
        uint32 fps = 30;
    };

    SplatSceneApplication(const String& modelPath = "",
                          const FStartupExportOptions& exportOptions = {},
                          const Splat::FSplatLODRuntimeOptions& runtime = {});
    virtual ~SplatSceneApplication();

    // Application interface
    void onInitialize() override;
    void onUpdate(float32 deltaTime) override;
    void onRender() override;
    void onShutdown() override;
    void onWindowResize(uint32 width, uint32 height) override;

    // Input events
    void onKeyPressed(EKey key) override;
    void onKeyReleased(EKey key) override;
    void onMouseButtonPressed(EKey button, const MousePosition& position) override;
    void onMouseButtonReleased(EKey button, const MousePosition& position) override;
    void onMouseMoved(const MousePosition& position) override;
    void onMouseScrolled(float64 xOffset, float64 yOffset) override;
    bool HasStartupExportFailed() const { return m_startupExportFailed; }
    /** Startup, lifecycle and evidence failures propagate through main's exit code. */
    bool HasRunFailed() const { return m_runFailed || m_startupExportFailed; }
    /** Scene replacement is deferred to a safe frame boundary. */
    bool QueueSceneSwitch(const String& manifestPath, const String& sourcePLY, char mode);

    // The 3DGS scene keeps rendering while the control overlay is hidden.
    // Enabled by default. F1 calls this setter at runtime.
    void SetControlUIVisible(bool visible);
    bool IsControlUIVisible() const { return m_controlUIVisible; }

private:
    // ================================================================
    // Initialization helpers
    // ================================================================
    bool loadModel();
    bool initSplatPipeline();
    bool initCamera();
    bool initPresentPass();
    bool initImGui();
    void drawControlPanels();
    void applyControlState();
    bool initializeBenchmark();
    void updateSceneStreaming(RHI::IRHICommandList*, const Splat::FCameraUniforms&);
    void releaseCPUTransfers();
    void destroyScene();
    void failRun(const std::string&);
    void drawLODOverlay();

    // ================================================================
    // Camera helpers
    // ================================================================
    void buildCameraUniforms(MonsterRender::Splat::FCameraUniforms& outUniforms) const;

    // ================================================================
    // Device / Pipeline
    // ================================================================
    RHI::IRHIDevice* m_device = nullptr;

    // Splat pipeline
    MonsterEngine::TUniquePtr<MonsterRender::Splat::FSplatPipeline> m_splatPipeline;
    MonsterRender::Splat::FSplatGPUData m_gpuData;
    String m_modelPath;
    Splat::FSplatLODRuntimeOptions m_runtime;
    Splat::FSplatSceneManifest m_scene;
    Splat::FSplatLODSelector m_selector;
    Splat::FSplatLODSelection m_selection;
    std::unique_ptr<Splat::FSplatPageStreamer> m_streamer;
    std::unique_ptr<Splat::FSplatGPUPagePool> m_pagePool;
    std::unique_ptr<Splat::FSplatLODBenchmark> m_benchmark;
    std::vector<uint32_t> m_leafNodes, m_residentTargetPages;
    Splat::FSplatGPUPagePoolAllocationStats m_legacyAllocations;
    uint64_t m_initialUploadStagingPeakBytes = 0;
    std::set<uint32_t> m_terminalPageErrors;
    uint64_t m_sceneGeneration = 1;
    uint64_t m_selectionCounter = 1;
    uint64_t m_fullLeafRequiredSlots = 0;
    uint64_t m_fusionCPUReservation = 0; ///< Membership copies plus bounded CPU descriptor/mapping working allowance.
    uint64_t m_cpuProgressMinimumBytes = 0; ///< Required streaming workspace after immutable/working reservations.
    std::map<uint64_t,double> m_readyFrameTimes;
    bool m_runFailed = false, m_loading = false, m_samplingStarted = false, m_benchmarkCompleted = false;
    bool m_firstUsableRecorded = false;
    bool m_pendingSceneSwitch = false;
    String m_nextScenePath, m_nextSourcePLY;
    std::unique_ptr<Splat::FSplatLODRuntimeOptions> m_pendingRuntime, m_uiRuntime;
    char m_scenePathUI[1024] = {}, m_sourcePathUI[1024] = {};
    char m_nextMode = 'D';
    size_t m_captureIndex = 0;
    uint32_t m_finishFrames = 0;
    double m_sequenceTime = 0, m_selectionMs = 0;
    std::chrono::steady_clock::time_point m_startupStart = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point m_sampleStart{}, m_updateStart{}, m_lastPresent{}, m_captureHoldStart{};
    Splat::FCameraUniforms m_lastCamera{};

    // Camera
    MonsterEngine::TUniquePtr<MonsterEngine::FCameraManager>       m_cameraManager;
    MonsterEngine::TUniquePtr<MonsterEngine::FFPSCameraController>  m_fpsCamera;
    MonsterEngine::TUniquePtr<MonsterEngine::Editor::FImGuiContext> m_imguiContext;
    MonsterEngine::TUniquePtr<MonsterEngine::Editor::FImGuiRenderer> m_imguiRenderer;
    MonsterEngine::TUniquePtr<MonsterEngine::Editor::FImGuiInputHandler> m_imguiInput;
    MonsterRender::Splat::FSplatControlState m_controls;
    MonsterRender::Splat::FSplatCameraSequence m_sequence;
    MonsterRender::Splat::FSplatVideoExporter m_videoExporter;
    MonsterRender::Splat::FSplatControlState m_preExportControls;
    bool m_restoreExportState = false;
    bool m_sequenceMode = false;
    bool m_controlUIVisible = true;
    int m_selectedKeyframe = -1;
    double m_newKeyframeTime = 0.0;
    char m_sequencePath[260] = "3dgs_sequence.json";
    char m_videoPath[260] = "3dgs_animation.mp4";
    char m_ffmpegPath[260] = "ffmpeg";
    int m_exportFps = 30;
    FStartupExportOptions m_startupExport;
    bool m_autoExitAfterExport = false;
    bool m_startupExportFailed = false;
    String m_uiMessage;
    float m_smoothedFrameMs = 16.67f;
    float m_memoryPollSeconds = 0.0f;
    uint64 m_gpuUsedBytes = 0;
    uint64 m_gpuAvailableBytes = 0;

    // Present pass (fullscreen triangle → swapchain)
    struct FPresentPass
    {
        MonsterEngine::TSharedPtr<class RHI::IRHIVertexShader>  vertexShader;
        MonsterEngine::TSharedPtr<class RHI::IRHIPixelShader>  fragmentShader;
        MonsterEngine::TSharedPtr<RHI::IRHIPipelineState>       pipelineState;
        MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout>      pipelineLayout;
        MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> setLayout;
        // Per-frame descriptor sets to avoid updating while command buffer is pending
        static constexpr uint32 kMaxFramesInFlight = 2;
        MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet>       descriptorSets[kMaxFramesInFlight];
        // Convenience accessor for current frame
        MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> getDescriptorSet(uint32 frameIndex) const {
            return frameIndex < kMaxFramesInFlight ? descriptorSets[frameIndex] : nullptr;
        }
        MonsterEngine::TSharedPtr<RHI::IRHISampler>             sampler;
    };
    FPresentPass m_present;

    // ================================================================
    // Window / Input state
    // ================================================================
    uint32 m_windowWidth  = 1280;
    uint32 m_windowHeight = 720;
    uint32 m_renderWidth = 1280;
    uint32 m_renderHeight = 720;
    float m_uiScale = 1.2f;

    bool   m_mouseLookActive  = false;
    bool   m_firstMouseInput  = true;
    float  m_lastMouseX       = 0.0f;
    float  m_lastMouseY       = 0.0f;

    float m_deltaTime    = 0.016f;
    int32 m_shDegree     = 0;      // auto-detected from PLY
    bool  m_firstFrame   = true;   // first render frame flag
};

} // namespace MonsterRender
