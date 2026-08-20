// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SplatSceneApplication.cpp
 * @brief 3DGS Splat render demo — loads .ply, renders interactively via SplatPipeline.
 */

#include "SplatSceneApplication.h"
#include "Core/Logging/LogMacros.h"
#include "Core/ShaderCompiler.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanPendingState.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanSampler.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/CameraTypes.h"
#include "Engine/Camera/FPSCameraController.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDescriptorSet.h"
#include "RHI/IRHIDevice.h"
#include "Core/Input.h"
#include "Math/MonsterMath.h"
#include <cstdio>

namespace MonsterRender
{

using namespace MonsterEngine;

DEFINE_LOG_CATEGORY_STATIC(LogSplatScene, Log, All);

// ============================================================================
// Construction / Destruction
// ============================================================================

static ApplicationConfig CreateSplatSceneConfig()
{
    ApplicationConfig config;
    config.name = "Monster Engine - 3DGS Splat";
    config.preferredBackend = RHI::ERHIBackend::Vulkan;
    config.windowProperties.width  = 1920;
    config.windowProperties.height = 1080;
    config.enableValidation = true;
    return config;
}

SplatSceneApplication::SplatSceneApplication(const String& modelPath)
    : Application(CreateSplatSceneConfig())
    , m_device(nullptr)
    , m_modelPath(modelPath)
    , m_windowWidth(1280)
    , m_windowHeight(720)
{
    // Default model if none specified
    // // https://huggingface.co/datasets/dylanebert/3dgs/tree/main/bonsai
    if (m_modelPath.empty())
        m_modelPath = "resources\\point_cloud\\bonsai_30k.ply"; 
}

SplatSceneApplication::~SplatSceneApplication()
{
    shutdown();
}

// ============================================================================
// Application Interface
// ============================================================================

void SplatSceneApplication::onInitialize()
{
    MR_LOG(LogSplatScene, Log, "Initializing SplatSceneApplication...");

    // Get window dimensions
    if (getWindow())
    {
        m_windowWidth  = getWindow()->getWidth();
        m_windowHeight = getWindow()->getHeight();
    }

    // Get device
    m_device = getEngine() ? getEngine()->getRHIDevice() : nullptr;
    if (!m_device)
    {
        MR_LOG(LogSplatScene, Error, "Failed to get RHI device");
        return;
    }

    // Wait for device to be idle before setup
    m_device->waitForIdle();

    // Load model
    if (!loadModel())
    {
        MR_LOG(LogSplatScene, Error, "Failed to load model: %s", m_modelPath.c_str());
        return;
    }

    // Initialize splat pipeline
    if (!initSplatPipeline())
    {
        MR_LOG(LogSplatScene, Error, "Failed to initialize SplatPipeline");
        return;
    }

    // Initialize camera
    if (!initCamera())
    {
        MR_LOG(LogSplatScene, Error, "Failed to initialize camera");
        return;
    }

    // Initialize fullscreen present pass
    if (!initPresentPass())
    {
        MR_LOG(LogSplatScene, Error, "Failed to initialize present pass");
        return;
    }

    // Adjust window to swapchain size
    if (getWindow() && m_device->getRHIBackend() == RHI::ERHIBackend::Vulkan)
    {
        auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);
        auto swapchainExtent = vulkanDevice->getSwapchainExtent();
        if (swapchainExtent.width != m_windowWidth || swapchainExtent.height != m_windowHeight)
        {
            getWindow()->setSize(swapchainExtent.width, swapchainExtent.height);
            m_windowWidth  = swapchainExtent.width;
            m_windowHeight = swapchainExtent.height;
        }
    }

    MR_LOG(LogSplatScene, Log, "SplatSceneApplication initialized — %u gaussians, SH degree %d",
           m_splatPipeline->getGaussianCount(), m_shDegree);
}

void SplatSceneApplication::onUpdate(float32 deltaTime)
{
    m_deltaTime = deltaTime;

    // ---- Diagnostic: confirm onUpdate is called and input system is alive ----
    {
        static int32 updateFrameCount = 0;
        ++updateFrameCount;
        if (updateFrameCount % 60 == 0)
        {
            IInputManager* inputMgr = getWindow() ? getWindow()->getInputManager() : nullptr;
            bool anyKeyDown = false;
            if (inputMgr)
            {
                anyKeyDown = inputMgr->isKeyDown(EKey::W) || inputMgr->isKeyDown(EKey::S)
                          || inputMgr->isKeyDown(EKey::A) || inputMgr->isKeyDown(EKey::D);
            }
            FVector camPos(0.0f);
            float yaw = 0.0f, pitch = 0.0f;
            if (m_fpsCamera)
            {
                camPos = m_fpsCamera->GetPosition();
                yaw   = m_fpsCamera->GetYaw();
                pitch = m_fpsCamera->GetPitch();
            }
            MR_LOG(LogSplatScene, Log, "[DIAG] onUpdate frame=%d dt=%.3f hasInputMgr=%d anyKey=%d "
                   "camPos=(%.2f,%.2f,%.2f) yaw=%.1f pitch=%.1f",
                   updateFrameCount, deltaTime, inputMgr ? 1 : 0, anyKeyDown ? 1 : 0,
                   camPos.X, camPos.Y, camPos.Z, yaw, pitch);
        }
    }

    // Process FPS camera input (WASD keyboard + mouse accumulated state)
    if (m_fpsCamera && getWindow())
    {
        IInputManager* inputManager = getWindow()->getInputManager();
        if (inputManager)
        {
            bool bSprinting = inputManager->isKeyDown(EKey::LeftShift) ||
                              inputManager->isKeyDown(EKey::RightShift);

            if (inputManager->isKeyDown(EKey::W))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Forward, deltaTime, bSprinting);
            if (inputManager->isKeyDown(EKey::S))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Backward, deltaTime, bSprinting);
            if (inputManager->isKeyDown(EKey::A))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Left, deltaTime, bSprinting);
            if (inputManager->isKeyDown(EKey::D))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Right, deltaTime, bSprinting);
            if (inputManager->isKeyDown(EKey::E) || inputManager->isKeyDown(EKey::Space))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Up, deltaTime, bSprinting);
            if (inputManager->isKeyDown(EKey::Q) || inputManager->isKeyDown(EKey::LeftControl))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Down, deltaTime, bSprinting);
        }

        // Apply all accumulated camera state to the camera manager
        m_fpsCamera->Update(deltaTime);
    }

    // Update camera manager (view targets, modifiers, etc.)
    if (m_cameraManager)
    {
        m_cameraManager->UpdateCamera(deltaTime);
    }
}

void SplatSceneApplication::onRender()
{
    if (!m_device || !m_splatPipeline || !m_splatPipeline->isInitialized())
    {
        MR_LOG(LogSplatScene, Warning, "onRender: Skipping - device=%p, pipeline=%p",
               (void*)m_device, (void*)m_splatPipeline.Get());
        return;
    }

    // ================================================================
    // Vulkan command recording
    // ================================================================
    auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);
    auto* context = vulkanDevice->getCommandListContext();
    if (!context) { MR_LOG(LogSplatScene, Error, "onRender: No command list context"); return; }

    RHI::IRHICommandList* cmdList = m_device->getImmediateCommandList();
    if (!cmdList) { MR_LOG(LogSplatScene, Error, "onRender: No immediate command list"); return; }

    // Log frame start
    MR_LOG(LogSplatScene, Verbose, "onRender: Frame %u begin, window=%ux%u, firstFrame=%d",
           vulkanDevice->getCurrentFrame(), m_windowWidth, m_windowHeight, (int)m_firstFrame);

    context->prepareForNewFrame();

    // Build camera & update pipeline BEFORE command buffer recording.
    // updateCamera() pre-bakes the camera UBO descriptor binding, which
    // calls vkUpdateDescriptorSets. Doing this before cmdList->begin()
    // avoids VUID-03047 (updating a set while a pending CB references it).
    Splat::FCameraUniforms camera;
    buildCameraUniforms(camera);
    m_splatPipeline->setCamera(camera);

    cmdList->begin();
    MR_LOG(LogSplatScene, Verbose, "onRender: Command buffer recorded, executing splat pipeline...");

    // ---- Step 0: Transition output from previous frame's shader-read back to compute-write ----
    {
        auto outputTex = m_splatPipeline->getOutputTexture();
        if (outputTex)
        {
            // First frame: texture layout is UNDEFINED after creation.
            // Subsequent frames: layout is SHADER_READ_ONLY_OPTIMAL from Step 2.
            RHI::EResourceUsage srcLayout = m_firstFrame
                ? RHI::EResourceUsage::None
                : RHI::EResourceUsage::ShaderResource;
            cmdList->transitionResource(outputTex, srcLayout, RHI::EResourceUsage::UnorderedAccess);
            MR_LOG(LogSplatScene, Verbose, "onRender: Step0 - Output texture layout transition, firstFrame=%d", (int)m_firstFrame);
        }
    }

    // ---- Step 1: Execute splat pipeline (camera already set before begin) ----
    MR_LOG(LogSplatScene, Verbose, "onRender: Calling m_splatPipeline->execute()...");
    auto splatOutput = m_splatPipeline->execute(cmdList);
    if (!splatOutput)
    {
        MR_LOG(LogSplatScene, Error, "onRender: SplatPipeline execute returned null texture");
        cmdList->end();
        m_device->present();
        return;
    }
    MR_LOG(LogSplatScene, Verbose, "onRender: SplatPipeline executed, output texture=%s (%ux%u)",
           splatOutput->getDebugName().c_str(), splatOutput->getWidth(), splatOutput->getHeight());

    // ---- Step 2: Transition output from compute (UnorderedAccess) to shader-read ----
    cmdList->transitionResource(
        splatOutput,
        RHI::EResourceUsage::UnorderedAccess,
        RHI::EResourceUsage::ShaderResource);
    MR_LOG(LogSplatScene, Verbose, "onRender: Step2 - Output transition to ShaderResource complete");

    // ---- Step 3: Fullscreen present pass → swapchain ----
    TArray<TSharedPtr<RHI::IRHITexture>> renderTargets;
    cmdList->setRenderTargets(
        TSpan<TSharedPtr<RHI::IRHITexture>>(renderTargets), nullptr);
    MR_LOG(LogSplatScene, Verbose, "onRender: setRenderTargets (swapchain) done");

    // Viewport
    RHI::Viewport viewport;
    viewport.x = 0.0f;  viewport.y = 0.0f;
    viewport.width  = static_cast<float>(m_windowWidth);
    viewport.height = static_cast<float>(m_windowHeight);
    viewport.minDepth = 0.0f;  viewport.maxDepth = 1.0f;
    cmdList->setViewport(viewport);

    // Scissor
    RHI::ScissorRect scissor;
    scissor.left = 0;   scissor.top = 0;
    scissor.right  = static_cast<int32>(m_windowWidth);
    scissor.bottom = static_cast<int32>(m_windowHeight);
    cmdList->setScissorRect(scissor);

    // Bind present pipeline
    cmdList->setPipelineState(m_present.pipelineState);
    MR_LOG(LogSplatScene, Verbose, "onRender: Present pipeline bound, registering texture in pending state...");

    // Register present pass texture in the pending state so that
    // prepareForDraw() can correctly allocate and update the descriptor
    // set via the cache, rather than being overridden by a stale cached set.
    // This avoids VUID-08114: the cached set would otherwise be allocated but
    // never updated with the splatOutput texture.
    {
        auto* pendingState = context->getPendingState();
        auto* vulkanTex = static_cast<RHI::Vulkan::VulkanTexture*>(splatOutput.get());
        auto* vulkanSamp = static_cast<RHI::Vulkan::VulkanSampler*>(m_present.sampler.get());
        if (pendingState && vulkanTex && vulkanSamp)
        {
            pendingState->setTexture(
                0, 0,
                vulkanTex->getImageView(),
                vulkanSamp->getSampler(),
                vulkanTex->getImage(),
                vulkanTex->getVulkanFormat());
            MR_LOG(LogSplatScene, Verbose, "onRender: Texture registered in pending state — imageView=0x%llx, sampler=0x%llx, image=0x%llx, format=%d",
                   (uint64)vulkanTex->getImageView(), (uint64)vulkanSamp->getSampler(),
                   (uint64)vulkanTex->getImage(), (int)vulkanTex->getVulkanFormat());
        }
        else
        {
            MR_LOG(LogSplatScene, Error, "onRender: Failed to register texture — pendingState=%p, vulkanTex=%p, vulkanSamp=%p",
                   (void*)pendingState, (void*)vulkanTex, (void*)vulkanSamp);
        }
    }

    // Bind the descriptor set explicitly so the recorder records and replays it.
    // This ensures vkCmdBindDescriptorSets is called during replay, which the
    // pending state's prepareForDraw() cannot do for the deferred replay path.
    {
        uint32 currentFrame = vulkanDevice->getCurrentFrame();
        uint32 dsIndex = currentFrame % FPresentPass::kMaxFramesInFlight;
        auto& descSet = m_present.descriptorSets[dsIndex];

        // Update the pre-allocated descriptor set with the current splatOutput texture
        descSet->updateCombinedTextureSampler(0, splatOutput, m_present.sampler);
        MR_LOG(LogSplatScene, Verbose, "onRender: Updated descriptor set[%u] with splatOutput texture", dsIndex);

        // Record bindDescriptorSet command for the recorder/replay system
        cmdList->bindDescriptorSet(m_present.pipelineLayout, 0, descSet);
        MR_LOG(LogSplatScene, Verbose, "onRender: Recorded bindDescriptorSet(pipelineLayout=%p, set=0, descSet=%p)",
               (void*)m_present.pipelineLayout.Get(), (void*)descSet.Get());
    }

    // Draw fullscreen triangle (no vertex buffer)
    MR_LOG(LogSplatScene, Verbose, "onRender: Calling cmdList->draw(3, 0)...");
    cmdList->draw(3, 0);
    MR_LOG(LogSplatScene, Verbose, "onRender: draw(3, 0) completed");

    // ---- Step 4: Finish & present ----
    cmdList->endRenderPass();
    cmdList->end();
    m_device->present();

    MR_LOG(LogSplatScene, Verbose, "onRender: Frame %u presented", vulkanDevice->getCurrentFrame());
    m_firstFrame = false;
}

void SplatSceneApplication::onShutdown()
{
    MR_LOG(LogSplatScene, Log, "Shutting down SplatSceneApplication...");

    // Release splat resources in reverse order
    m_gpuData.release();

    m_splatPipeline.Reset();

    // Release present pass resources
    for (uint32 i = 0; i < FPresentPass::kMaxFramesInFlight; ++i)
    {
        m_present.descriptorSets[i].Reset();
    }
    m_present.pipelineState.Reset();
    m_present.pipelineLayout.Reset();
    m_present.setLayout.Reset();
    m_present.vertexShader.Reset();
    m_present.fragmentShader.Reset();
    m_present.sampler.Reset();

    // Release camera
    m_fpsCamera.Reset();
    m_cameraManager.Reset();

    m_device = nullptr;
    MR_LOG(LogSplatScene, Log, "SplatSceneApplication shutdown complete");
}

void SplatSceneApplication::onWindowResize(uint32 width, uint32 height)
{
    m_windowWidth  = width;
    m_windowHeight = height;

    // Recreate swapchain
    if (m_device && m_device->getRHIBackend() == RHI::ERHIBackend::Vulkan)
    {
        auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);
        vulkanDevice->recreateSwapchain(width, height);

        auto swapchainExtent = vulkanDevice->getSwapchainExtent();
        m_windowWidth  = swapchainExtent.width;
        m_windowHeight = swapchainExtent.height;
    }
}

// ============================================================================
// Input Events
// ============================================================================

void SplatSceneApplication::onKeyPressed(EKey key)
{
    if (key == EKey::Escape)
    {
        requestExit();
        return;
    }

    // Forward to FPS camera via InputManager
    if (m_fpsCamera && getWindow())
    {
        // FPS camera handles WASD internally via onUpdate
        (void)key;
    }
}

void SplatSceneApplication::onMouseButtonPressed(EKey button, const MousePosition& position)
{
    if (button == EKey::MouseRight)
    {
        m_mouseLookActive = true;
        m_firstMouseInput = true;
        m_lastMouseX = static_cast<float>(position.x);
        m_lastMouseY = static_cast<float>(position.y);
    }
}

void SplatSceneApplication::onMouseButtonReleased(EKey button, const MousePosition& position)
{
    (void)position;
    if (button == EKey::MouseRight)
    {
        m_mouseLookActive = false;
    }
}

void SplatSceneApplication::onMouseMoved(const MousePosition& position)
{
    if (!m_mouseLookActive || !m_fpsCamera)
        return;

    float xpos = static_cast<float>(position.x);
    float ypos = static_cast<float>(position.y);

    if (m_firstMouseInput)
    {
        m_lastMouseX = xpos;
        m_lastMouseY = ypos;
        m_firstMouseInput = false;
        return;
    }

    float xoffset = xpos - m_lastMouseX;
    float yoffset = m_lastMouseY - ypos; // reversed: y goes bottom→top
    m_lastMouseX = xpos;
    m_lastMouseY = ypos;

    m_fpsCamera->ProcessMouseMovement(xoffset, yoffset);
}

void SplatSceneApplication::onMouseScrolled(float64 xOffset, float64 yOffset)
{
    (void)xOffset;
    if (m_fpsCamera)
    {
        m_fpsCamera->ProcessMouseScroll(static_cast<float>(yOffset));
    }
}

// ============================================================================
// Initialization Helpers
// ============================================================================

bool SplatSceneApplication::loadModel()
{
    MR_LOG(LogSplatScene, Log, "Loading PLY: %s", m_modelPath.c_str());

    Splat::FSplatPLYLoader loader;
    if (!loader.loadAndUpload(m_device, m_modelPath, m_gpuData))
    {
        MR_LOG(LogSplatScene, Error, "Failed to load PLY file");
        return false;
    }

    m_shDegree = m_gpuData.shDegree;
    MR_LOG(LogSplatScene, Log, "Loaded %u gaussians, SH degree %d",
           m_gpuData.gaussianCount, m_shDegree);
    return true;
}

bool SplatSceneApplication::initSplatPipeline()
{
    if (!m_gpuData.isValid())
        return false;

    m_splatPipeline = MakeUnique<Splat::FSplatPipeline>();
    if (!m_splatPipeline->initialize(m_device, m_gpuData.gaussianCount,
                                     m_windowWidth, m_windowHeight))
    {
        MR_LOG(LogSplatScene, Error, "SplatPipeline::initialize failed");
        return false;
    }

    m_splatPipeline->setGaussianData(m_gpuData);
    return true;
}

bool SplatSceneApplication::initCamera()
{
    // Create camera manager
    m_cameraManager = MakeUnique<FCameraManager>();
    m_cameraManager->Initialize(nullptr);

    FMinimalViewInfo viewInfo;
    viewInfo.Location = FVector(0.0, 1.0, 4.0);  // position in front of origin
    viewInfo.Rotation = FRotator(0.0, -180.0, 0.0);  // look towards -Z
    viewInfo.FOV = 60.0f;

    auto* vulkanDevice = static_cast<RHI::Vulkan::VulkanDevice*>(m_device);
    auto swapchainExtent = vulkanDevice->getSwapchainExtent();
    viewInfo.AspectRatio = static_cast<float>(swapchainExtent.width) /
                           static_cast<float>(swapchainExtent.height);
    viewInfo.ProjectionMode = ECameraProjectionMode::Perspective;
    viewInfo.PerspectiveNearClipPlane = 0.1f;

    m_cameraManager->SetCameraCachePOV(viewInfo);
    m_cameraManager->SetViewTargetPOV(viewInfo);

    // Create FPS camera controller — position slightly above & in front
    m_fpsCamera = MakeUnique<FFPSCameraController>(
        FVector(0.0, 0.0, 0.0),   // position
        FVector(0.0, 1.0, 0.0),   // world up (Y-up)
        80.0f,                    // yaw (LearnOpenGL convention: -90 = look -Z)
        -10.0f                        // pitch
    );

    m_fpsCamera->Initialize(m_cameraManager.Get());
    m_fpsCamera->SetFOV(60.0f);

    return true;
}

bool SplatSceneApplication::initPresentPass()
{
    using namespace RHI;

    // ---- Load SPIR-V ----
    auto readShader = [](const String& path) -> TSharedPtr<RHI::IRHIShader> {
        // Dummy — actual load happens below with correct device call
        return nullptr;
    };

    auto vertBytes = MonsterRender::ShaderCompiler::readFileBytes(
        "Shaders/Splat/compiled/splat_present.vert.spv");
    auto fragBytes = MonsterRender::ShaderCompiler::readFileBytes(
        "Shaders/Splat/compiled/splat_present.frag.spv");

    if (vertBytes.empty() || fragBytes.empty())
    {
        MR_LOG(LogSplatScene, Error, "Failed to read present shader SPIR-V");
        return false;
    }

    // DEBUG: Log SPIR-V sizes and first 8 bytes to verify version
    MR_LOG(LogSplatScene, Log, "initPresentPass: vert SPIR-V size=%zu, frag SPIR-V size=%zu",
           vertBytes.size(), fragBytes.size());
    MR_LOG(LogSplatScene, Log, "initPresentPass: frag first 8 bytes = %02x %02x %02x %02x %02x %02x %02x %02x",
           (uint32)fragBytes[0], (uint32)fragBytes[1], (uint32)fragBytes[2], (uint32)fragBytes[3],
           (uint32)fragBytes[4], (uint32)fragBytes[5], (uint32)fragBytes[6], (uint32)fragBytes[7]);

    TSpan<const uint8> vertCode(vertBytes.data(), vertBytes.size());
    TSpan<const uint8> fragCode(fragBytes.data(), fragBytes.size());

    m_present.vertexShader   = m_device->createVertexShader(vertCode);
    m_present.fragmentShader = m_device->createPixelShader(fragCode);

    if (!m_present.vertexShader || !m_present.fragmentShader)
    {
        MR_LOG(LogSplatScene, Error, "Failed to create present shaders");
        return false;
    }

    // DIAG: Log shader pointers and validity right after creation
    {
        auto* rawVert = static_cast<RHI::Vulkan::VulkanVertexShader*>(m_present.vertexShader.get());
        auto* rawFrag = static_cast<RHI::Vulkan::VulkanPixelShader*>(m_present.fragmentShader.get());
        MR_LOG(LogSplatScene, Log, "[DIAG] Shaders created: vert ptr=%p module=%p isValid=%d, frag ptr=%p module=%p isValid=%d",
               (void*)rawVert, (void*)(uintptr_t)rawVert->getShaderModule(), rawVert->isValid() ? 1 : 0,
               (void*)rawFrag, (void*)(uintptr_t)rawFrag->getShaderModule(), rawFrag->isValid() ? 1 : 0);
    }

    // ---- Descriptor set layout: Set 0, Binding 0 = combined image sampler ----
    {
        FDescriptorSetLayoutDesc setLayoutDesc(0, "SplatPresentSet");
        setLayoutDesc.bindings.Add(FDescriptorSetLayoutBinding(
            0, EDescriptorType::CombinedTextureSampler, EShaderStage::Fragment));
        m_present.setLayout = m_device->createDescriptorSetLayout(setLayoutDesc);
        if (!m_present.setLayout)
        {
            MR_LOG(LogSplatScene, Error, "Failed to create present set layout");
            return false;
        }
    }

    // ---- Pipeline layout ----
    {
        FPipelineLayoutDesc layoutDesc("SplatPresentLayout");
        layoutDesc.setLayouts.Add(m_present.setLayout);
        m_present.pipelineLayout = m_device->createPipelineLayout(layoutDesc);
        if (!m_present.pipelineLayout)
        {
            MR_LOG(LogSplatScene, Error, "Failed to create present pipeline layout");
            return false;
        }
    }

    // ---- Graphics pipeline ----
    {
        PipelineStateDesc desc;
        desc.rasterizerState.cullMode = ECullMode::None;  // 全屏三角形：禁用背面剔除（Y-flip viewport + CW 正面会把 CCW 三角形判为背面剔除，导致灰屏）
        desc.vertexShader        = m_present.vertexShader;
        desc.pixelShader         = m_present.fragmentShader;
        desc.primitiveTopology   = EPrimitiveTopology::TriangleList;
        desc.depthStencilState.depthEnable  = false;
        desc.depthStencilState.depthWriteEnable = false;
        desc.renderTargetFormats.Add(m_device->getSwapChainFormat());
        desc.debugName = "SplatPresent";
        // No vertex layout needed (gl_VertexIndex is used)

        // Workaround: TSharedPtr copy can fail with multiple-inheritance types
        // Store raw VkShaderModule handles as fallback for pipeline creation.
        {
            auto* rawVert = static_cast<RHI::Vulkan::VulkanVertexShader*>(m_present.vertexShader.get());
            auto* rawFrag = static_cast<RHI::Vulkan::VulkanPixelShader*>(m_present.fragmentShader.get());
            desc.vkVertexShaderModule = reinterpret_cast<uint64>(rawVert->getShaderModule());
            desc.vkPixelShaderModule  = reinterpret_cast<uint64>(rawFrag->getShaderModule());
        }

        // DIAG: Log PipelineStateDesc before creating pipeline
        {
            auto* descVert = static_cast<RHI::Vulkan::VulkanVertexShader*>(desc.vertexShader.get());
            MR_LOG(LogSplatScene, Log, "[DIAG] PipelineStateDesc: vertShader ptr=%p module=%p isValid=%d, use_count=%d, vkModule=%p",
                   (void*)descVert, descVert ? (void*)(uintptr_t)descVert->getShaderModule() : nullptr,
                   descVert ? (descVert->isValid() ? 1 : 0) : -1,
                   desc.vertexShader.GetSharedReferenceCount(),
                   (void*)desc.vkVertexShaderModule);
        }

        m_present.pipelineState = m_device->createPipelineState(desc);
        if (!m_present.pipelineState)
        {
            MR_LOG(LogSplatScene, Error, "Failed to create present pipeline");
            return false;
        }
    }

    // ---- Per-frame descriptor sets ----
    // Descriptor sets are allocated fresh each frame in onRender() to avoid
    // VUID-08114 (never-updated set) and VUID-03047 (updating while pending).
    // Pre-allocate once here so the array slots are non-null for the first frame.
    for (uint32 i = 0; i < FPresentPass::kMaxFramesInFlight; ++i)
    {
        m_present.descriptorSets[i] = m_device->allocateDescriptorSet(m_present.setLayout);
        if (!m_present.descriptorSets[i])
        {
            MR_LOG(LogSplatScene, Error, "Failed to allocate present descriptor set %u", i);
            return false;
        }
    }

    // ---- Sampler ----
    {
        SamplerDesc samplerDesc;
        samplerDesc.filter    = ESamplerFilter::Bilinear;
        samplerDesc.addressU  = ESamplerAddressMode::Clamp;
        samplerDesc.addressV  = ESamplerAddressMode::Clamp;
        samplerDesc.addressW  = ESamplerAddressMode::Clamp;
        samplerDesc.debugName = "SplatPresentSampler";
        m_present.sampler = m_device->createSampler(samplerDesc);
        if (!m_present.sampler)
        {
            MR_LOG(LogSplatScene, Error, "Failed to create present sampler");
            return false;
        }
    }

    MR_LOG(LogSplatScene, Log, "Present pass initialized");
    return true;
}

// ============================================================================
// Camera Helpers
// ============================================================================

void SplatSceneApplication::buildCameraUniforms(Splat::FCameraUniforms& outUniforms) const
{
    using namespace MonsterEngine;

    if (!m_cameraManager)
    {
        FMemory::Memset(&outUniforms, 0, sizeof(outUniforms));
        return;
    }

    // ---- Camera state from FPS controller ----
    const FMinimalViewInfo& viewInfo = m_cameraManager->GetCameraCacheView();
    FVector camPos;
    FVector forward, right, up;

    if (m_fpsCamera)
    {
        camPos  = m_fpsCamera->GetPosition();
        forward = m_fpsCamera->GetFront();
        right   = FVector::CrossProduct(m_fpsCamera->GetWorldUp(), forward).GetSafeNormal();
        up      = FVector::CrossProduct(forward, right);
    }
    else
    {
        camPos  = viewInfo.Location;
        forward = FVector(0, 0, -1);  // look -Z
        right   = FVector::CrossProduct(FVector(0, 1, 0), forward).GetSafeNormal();
        up      = FVector::CrossProduct(forward, right);
    }

    FMatrix projMatrix = viewInfo.CalculateProjectionMatrix();

    // ---- Write view matrix: OpenGL/Vulkan column-major, M*v convention ----
    // GLSL expects objects in front of the camera to have NEGATIVE view-space Z.
    // col0 = right, col1 = up, col2 = -forward (so front objects map to -Z),
    // col3 = translation (-dot(right,eye), -dot(up,eye), dot(forward,eye))
    {
        float32* v = outUniforms.viewMatrix;
        v[0]  = static_cast<float32>(right.X);
        v[1]  = static_cast<float32>(right.Y);
        v[2]  = static_cast<float32>(right.Z);
        v[3]  = 0.0f;
        v[4]  = static_cast<float32>(up.X);
        v[5]  = static_cast<float32>(up.Y);
        v[6]  = static_cast<float32>(up.Z);
        v[7]  = 0.0f;
        v[8]  = static_cast<float32>(-forward.X);
        v[9]  = static_cast<float32>(-forward.Y);
        v[10] = static_cast<float32>(-forward.Z);
        v[11] = 0.0f;
        v[12] = -static_cast<float32>(FVector::DotProduct(right, camPos));
        v[13] = -static_cast<float32>(FVector::DotProduct(up, camPos));
        v[14] =  static_cast<float32>(FVector::DotProduct(forward, camPos));
        v[15] = 1.0f;
    }

    // ---- Write projection matrix: Vulkan column-major, depth=[0,1], -Z forward ----
    // Standard Vulkan perspective: pClip.w = -pView.z, so objects with -Z
    // in view space produce positive w for correct frustum clipping.
    {
        float fovYRad  = viewInfo.FOV * MR_PI / 180.0f;
        float aspect   = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
        float tanHalf  = FMath::Tan(fovYRad * 0.5f);
        float nearVal  = 0.1f;    // Match preprocess near plane
        float farVal   = 1000.0f;  // Match preprocess far plane
        float sx = 1.0f / (aspect * tanHalf);
        float sy = 1.0f / tanHalf;
        float A  = farVal / (nearVal - farVal);  // Vulkan [0,1] depth: A = far/(near-far)
        float B  = -(farVal * nearVal) / (farVal - nearVal);

        float32* p = outUniforms.projMatrix;
        p[0]  = sx;  p[1]  = 0;   p[2]  = 0;   p[3]  = 0;
        p[4]  = 0;   p[5]  = sy;  p[6]  = 0;   p[7]  = 0;
        p[8]  = 0;   p[9]  = 0;   p[10] = A;   p[11] = -1.0f;
        p[12] = 0;   p[13] = 0;   p[14] = B;   p[15] = 0;
    }

    outUniforms.camPos[0] = static_cast<float32>(camPos.X);
    outUniforms.camPos[1] = static_cast<float32>(camPos.Y);
    outUniforms.camPos[2] = static_cast<float32>(camPos.Z);
    outUniforms.camPos[3] = 1.0f;

    // ---- Focal & FOV ----
    float fovY  = viewInfo.FOV * MR_PI / 180.0f;
    float aspect = static_cast<float>(m_windowWidth) /
                   static_cast<float>(m_windowHeight);
    float tanFovY = FMath::Tan(fovY * 0.5f);
    float tanFovX = tanFovY * aspect;

    outUniforms.focalX = static_cast<float>(m_windowWidth)  / (2.0f * tanFovX);
    outUniforms.focalY = static_cast<float>(m_windowHeight) / (2.0f * tanFovY);
    outUniforms.tanFovX = tanFovX;
    outUniforms.tanFovY = tanFovY;

    // ---- Image dimensions & SH degree ----
    outUniforms.imageWidth  = static_cast<int32>(m_windowWidth);
    outUniforms.imageHeight = static_cast<int32>(m_windowHeight);
    outUniforms.shDegree = m_shDegree;
    outUniforms.pad0 = 0;

    // ---- Diagnostic: dump view matrix for frustum-culling analysis ----
    {
        const float32* v = outUniforms.viewMatrix;
        MR_LOG(LogSplatScene, Verbose, "[VIEW_MATRIX] col0=(%.3f, %.3f, %.3f, %.3f)", v[0], v[1], v[2], v[3]);
        MR_LOG(LogSplatScene, Verbose, "[VIEW_MATRIX] col1=(%.3f, %.3f, %.3f, %.3f)", v[4], v[5], v[6], v[7]);
        MR_LOG(LogSplatScene, Verbose, "[VIEW_MATRIX] col2=(%.3f, %.3f, %.3f, %.3f)", v[8], v[9], v[10], v[11]);
        MR_LOG(LogSplatScene, Verbose, "[VIEW_MATRIX] col3=(%.3f, %.3f, %.3f, %.3f)", v[12], v[13], v[14], v[15]);
#if 0  // Set to 1 to re-enable per-frame stderr camera diagnostics
        fprintf(stderr, "[STDERR] ViewMatrix: R=(%.3f,%.3f,%.3f) U=(%.3f,%.3f,%.3f) -F=(%.3f,%.3f,%.3f) T=(%.3f,%.3f,%.3f)\n",
                v[0], v[1], v[2], v[4], v[5], v[6], v[8], v[9], v[10], v[12], v[13], v[14]);
#endif
    }
    {
        const float32* p = outUniforms.projMatrix;
        MR_LOG(LogSplatScene, Verbose, "[PROJ_MATRIX] col0=(%.4f, %.4f, %.4f, %.4f)", p[0], p[1], p[2], p[3]);
        MR_LOG(LogSplatScene, Verbose, "[PROJ_MATRIX] col1=(%.4f, %.4f, %.4f, %.4f)", p[4], p[5], p[6], p[7]);
        MR_LOG(LogSplatScene, Verbose, "[PROJ_MATRIX] col2=(%.4f, %.4f, %.4f, %.4f)", p[8], p[9], p[10], p[11]);
        MR_LOG(LogSplatScene, Verbose, "[PROJ_MATRIX] col3=(%.4f, %.4f, %.4f, %.4f)", p[12], p[13], p[14], p[15]);
    }
    MR_LOG(LogSplatScene, Verbose, "buildCameraUniforms: camPos=(%.3f, %.3f, %.3f) focalX=%.1f focalY=%.1f tanFovX=%.4f tanFovY=%.4f image=%ux%u shDegree=%d",
           outUniforms.camPos[0], outUniforms.camPos[1], outUniforms.camPos[2],
           outUniforms.focalX, outUniforms.focalY, outUniforms.tanFovX, outUniforms.tanFovY,
           outUniforms.imageWidth, outUniforms.imageHeight, outUniforms.shDegree);

    // ---- Diagnostic: log camera state at uniform-build time ----
    {
        static int32 buildFrameCount = 0;
        ++buildFrameCount;
        if (buildFrameCount % 60 == 0)
        {
            bool hasFPS = (m_fpsCamera != nullptr);
            FVector pos    = hasFPS ? m_fpsCamera->GetPosition() : FVector(0);
            FVector front  = hasFPS ? m_fpsCamera->GetFront()    : FVector(0);
            float yaw      = hasFPS ? m_fpsCamera->GetYaw()      : 0.0f;
            float pitch    = hasFPS ? m_fpsCamera->GetPitch()    : 0.0f;
            float fov      = hasFPS ? m_fpsCamera->GetFOV()      : 0.0f;
            float nearPlane = 0.1f;  // 与投影矩阵 nearVal、preprocess setClipPlanes 保持一致
            MR_LOG(LogSplatScene, Log, "[DIAG] buildCam frame=%d hasFPS=%d "
                   "pos=(%.2f,%.2f,%.2f) front=(%.3f,%.3f,%.3f) yaw=%.1f pitch=%.1f "
                   "fov=%.2f near=%.2f focalX=%.1f focalY=%.1f tanFovX=%.4f tanFovY=%.4f",
                   buildFrameCount, hasFPS ? 1 : 0,
                   pos.X, pos.Y, pos.Z,
                   front.X, front.Y, front.Z, yaw, pitch,
                   fov, nearPlane, outUniforms.focalX, outUniforms.focalY,
                   outUniforms.tanFovX, outUniforms.tanFovY);
        }
    }
}

} // namespace MonsterRender
