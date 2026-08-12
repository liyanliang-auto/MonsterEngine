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

// Forward declarations
namespace MonsterEngine
{
    class FFPSCameraController;
    class FCameraManager;
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
 * Default model: assets/bonsai_30k.ply
 *  https://huggingface.co/datasets/dylanebert/3dgs/tree/main/bonsai
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
    SplatSceneApplication(const String& modelPath = "");
    virtual ~SplatSceneApplication();

    // Application interface
    void onInitialize() override;
    void onUpdate(float32 deltaTime) override;
    void onRender() override;
    void onShutdown() override;
    void onWindowResize(uint32 width, uint32 height) override;

    // Input events
    void onKeyPressed(EKey key) override;
    void onMouseButtonPressed(EKey button, const MousePosition& position) override;
    void onMouseButtonReleased(EKey button, const MousePosition& position) override;
    void onMouseMoved(const MousePosition& position) override;
    void onMouseScrolled(float64 xOffset, float64 yOffset) override;

private:
    // ================================================================
    // Initialization helpers
    // ================================================================
    bool loadModel();
    bool initSplatPipeline();
    bool initCamera();
    bool initPresentPass();

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

    // Camera
    MonsterEngine::TUniquePtr<MonsterEngine::FCameraManager>       m_cameraManager;
    MonsterEngine::TUniquePtr<MonsterEngine::FFPSCameraController>  m_fpsCamera;

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

    bool   m_mouseLookActive  = false;
    bool   m_firstMouseInput  = true;
    float  m_lastMouseX       = 0.0f;
    float  m_lastMouseY       = 0.0f;

    float m_deltaTime    = 0.016f;
    int32 m_shDegree     = 0;      // auto-detected from PLY
    bool  m_firstFrame   = true;   // first render frame flag
};

} // namespace MonsterRender
