// Copyright Monster Engine. All Rights Reserved.
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

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
#include "Platform/Vulkan/FVulkanMemoryManager.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/CameraTypes.h"
#include "Engine/Camera/FPSCameraController.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDescriptorSet.h"
#include "RHI/IRHIDevice.h"
#include "Core/Input.h"
#include "Math/MonsterMath.h"
#include "Editor/ImGui/ImGuiContext.h"
#include "Editor/ImGui/ImGuiRenderer.h"
#include "Editor/ImGui/ImGuiInputHandler.h"
#include "imgui.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iomanip>

namespace MonsterRender
{

using namespace MonsterEngine;

DEFINE_LOG_CATEGORY_STATIC(LogSplatScene, Log, All);
static std::string PathUTF8(const std::filesystem::path& path)
{
    const auto bytes=path.u8string();return std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}
static std::string HashText(const std::string& value)
{
    return Splat::FSplatSceneFormat::SHA256(std::vector<uint8_t>(value.begin(),value.end()));
}

static int GetSplatBenchmarkSeconds()
{
    static const int seconds = []() {
        char* value = nullptr;
        size_t length = 0;
        _dupenv_s(&value, &length, "MONSTER_SPLAT_BENCH_SECONDS");
        const int requested = value ? std::atoi(value) : 0;
        std::free(value);
        return requested > 0 ? requested : 0;
    }();
    return seconds;
}

// ============================================================================
// Construction / Destruction
// ============================================================================

static ApplicationConfig CreateSplatSceneConfig(const Splat::FSplatLODRuntimeOptions& runtime)
{
    ApplicationConfig config;
    config.name = "Monster Engine - 3DGS Splat";
    config.fpsLabel = "Render FPS";
    config.preferredBackend = RHI::ERHIBackend::Vulkan;
    config.windowProperties.width  = runtime.width;
    config.windowProperties.height = runtime.height;
    // Start at a 1:1 viewport size for readable UI and comparable FPS results.
    // Users can still maximize the window when they need more screen space.
    config.windowProperties.maximized = false;
    config.enableValidation = runtime.validation;
    config.enableVSync = runtime.vsync && !runtime.benchmark && GetSplatBenchmarkSeconds() == 0;
    config.windowProperties.vsync = config.enableVSync;
    if (runtime.benchmark) config.windowProperties.resizable = false;
    return config;
}

SplatSceneApplication::SplatSceneApplication(const String& modelPath,
                                             const FStartupExportOptions& exportOptions,
                                             const Splat::FSplatLODRuntimeOptions& runtime)
    : Application(CreateSplatSceneConfig(runtime))
    , m_device(nullptr)
    , m_modelPath(modelPath)
    , m_runtime(runtime)
    , m_windowWidth(1280)
    , m_windowHeight(720)
    , m_startupExport(exportOptions)
{
    // Default model if none specified
    if (m_modelPath.empty())
        m_modelPath = "resources/point_cloud/bicycle/point_cloud_30000.ply";
    if (!m_runtime.sourcePLY.empty()) m_modelPath = m_runtime.sourcePLY;
    else if (!m_runtime.scenePath.empty()) m_modelPath.clear();
    if (m_runtime.scenePath.empty()) m_runtime.mode = 'A';
    m_controls.LODQualityPixels = m_runtime.lod.qualityPixels;
    m_controls.bFreezeLOD = m_runtime.lod.freeze;
    if (m_runtime.benchmark) m_controlUIVisible = false;
}

SplatSceneApplication::~SplatSceneApplication()
{
    shutdown();
}

void SplatSceneApplication::failRun(const std::string& error)
{
    m_runFailed=true;m_uiMessage=error;
    MR_LOG(LogSplatScene,Error,"3DGS run failed: %s",error.c_str());requestExit();
}
bool SplatSceneApplication::QueueSceneSwitch(const String& path,const String& source,char mode)
{
    if(m_runtime.benchmark||mode<'A'||mode>'E'||(mode!='A'&&path.empty()))return false;
    m_nextScenePath=path;m_nextSourcePLY=source;m_nextMode=mode;m_pendingSceneSwitch=true;return true;
}
void SplatSceneApplication::releaseCPUTransfers()
{
    if(m_streamer&&m_pagePool)for(const auto& key:m_pagePool->DrainReleasedTransfers())m_streamer->ReleaseTransferred(key.first,key.second);
}
void SplatSceneApplication::destroyScene()
{
    if(m_device)m_device->waitForIdle();
    if(auto* vk=dynamic_cast<RHI::Vulkan::VulkanDevice*>(m_device))
        if(!vk->DrainCompletedDestructions()){failRun("Scene command bindings could not be safely released");return;}
    if(m_device)if(auto* commands=m_device->getImmediateCommandList())commands->reset();
    for(auto& descriptors:m_present.descriptorSets)descriptors.Reset();
    m_splatPipeline.Reset();m_gpuData.release();
    if(m_pagePool){
        if(m_benchmark){const auto stats=m_pagePool->GetStats();m_benchmark->SetMetadata("finalUploadedPages",std::to_string(stats.uploadedPages));m_benchmark->SetMetadata("finalEvictedPages",std::to_string(stats.evictedPages));}
        m_pagePool->Shutdown();releaseCPUTransfers();m_pagePool.reset();
    }
    if(m_streamer){
        m_streamer->CancelGeneration(m_sceneGeneration);m_streamer->Shutdown();
        const auto memory=m_streamer->GetMemoryStats();
        if(m_benchmark){m_benchmark->SetMetadata("CPUReservationsAfterShutdown",std::to_string(memory.totalReservedBytes));m_benchmark->SetMetadata("CPUTransfersAfterShutdown",std::to_string(memory.transferredPages));}
        if(memory.totalReservedBytes||memory.transferredPages)failRun("CPU transferred ownership did not close during shutdown");
        m_streamer.reset();
    }
    m_selector.Reset();m_scene={};m_selection={};m_leafNodes.clear();m_residentTargetPages.clear();m_fusionCPUReservation=0;m_cpuProgressMinimumBytes=0;
    m_benchmarkSelectionFrozen=false;m_selectionSettled=false;m_selectionSHA256.clear();m_cameraSHA256.clear();m_identityNodes.clear();m_identityResidencyVersion=~uint64_t(0);
    if(m_device){auto* vk=dynamic_cast<RHI::Vulkan::VulkanDevice*>(m_device);if(vk&&!vk->DrainCompletedDestructions())failRun("Scene backing could not be safely drained before replacement");}
}
bool SplatSceneApplication::initializeBenchmark()
{
    if(!m_runtime.benchmark)return true;
    std::string error;if(!m_runtime.Validate(error)){m_uiMessage=error;return false;}
    if(!m_startupExport.videoPath.empty()){m_uiMessage="Benchmark and video export require separate runs";return false;}
    if(!m_sequence.LoadJson(m_runtime.sequencePath,&error)||m_sequence.GetKeyframes().size()<2||m_sequence.GetDuration()<=0){m_uiMessage=error.empty()?"Invalid benchmark sequence":error;return false;}
    if(m_runtime.benchmarkFixedCameraSeconds>m_sequence.GetDuration()){m_uiMessage="Fixed camera time exceeds the registered sequence";return false;}
    if(m_runtime.captureOnly&&m_runtime.captureTimes.back()>m_sequence.GetDuration()){m_uiMessage="Capture times exceed the registered camera sequence";return false;}
    if(!m_runtime.captureOnly&&!m_device->supportsGPUTimestamps()){m_uiMessage="Hardware GPU timestamps unavailable; performance run is unverified";return false;}
    auto* vk=dynamic_cast<RHI::Vulkan::VulkanDevice*>(m_device);if(!vk){m_uiMessage="Benchmark requires Vulkan";return false;}
    VkPhysicalDeviceProperties properties{};RHI::Vulkan::VulkanAPI::getFunctions().vkGetPhysicalDeviceProperties(vk->getPhysicalDevice(),&properties);
    std::map<std::string,std::string> metadata;
    metadata["device"]=properties.deviceName;metadata["driverVersion"]=std::to_string(properties.driverVersion);metadata["vulkanVersion"]=std::to_string(properties.apiVersion);
    metadata["vendorID"]=std::to_string(properties.vendorID);metadata["deviceID"]=std::to_string(properties.deviceID);metadata["timestampPeriodNanoseconds"]=std::to_string(properties.limits.timestampPeriod);
    metadata["validation"]=m_config.enableValidation?"enabled":"disabled";metadata["VSyncRequested"]="disabled";metadata["frameCap"]="none";
    metadata["presentMode"]=vk->GetConfiguredPresentMode()==VK_PRESENT_MODE_IMMEDIATE_KHR?"IMMEDIATE":vk->GetConfiguredPresentMode()==VK_PRESENT_MODE_MAILBOX_KHR?"MAILBOX":"FIFO";
    metadata["presentModeImplications"]=vk->GetConfiguredPresentMode()==VK_PRESENT_MODE_MAILBOX_KHR?"Presentation is synchronized; mailbox can replace queued images and does not impose a software render cap":"Immediate presentation requested; no application render cap";
    metadata["colorSpace"]="linear RGBA8 shader output before sRGB present";metadata["background"]="black RGB with transparent alpha";metadata["exposure"]="1";
    metadata["sourcePLY"]=m_modelPath;metadata["manifest"]=m_runtime.scenePath;metadata["sequence"]=m_runtime.sequencePath;metadata["sourceSHDegree"]=std::to_string(m_gpuData.sourceSHDegree);
    metadata["displaySHDegree"]=std::to_string(m_runtime.displaySHDegree<0?m_shDegree:m_runtime.displaySHDegree);
    metadata["shStorageDegreeRequested"]=std::to_string(m_runtime.shStorageDegree);
    metadata["projectionColorCompression"]=m_runtime.projectionColorCompression?"1":"0";
    const auto& colorStats=m_splatPipeline->GetFrameStats();
    metadata["projectionColorStorageModeRequested"]=m_runtime.projectionColorCompression?"1":"0";
    metadata["projectionColorStorageModeEffective"]=std::to_string(colorStats.projectionColorStorageMode);
    metadata["projectionColorEncoding"]=colorStats.projectionColorStorageMode==1?"RGB21F":"FP32_VEC4";
    metadata["projectionColorStrideBytes"]=std::to_string(Splat::ESplatProjectionColor::Stride(colorStats.projectionColorStorageMode));
    metadata["projectionColorCapacity"]=std::to_string(colorStats.activeCapacity);
    metadata["projectionColorLogicalBytes"]=std::to_string(colorStats.projectionColorLogicalBytes);
    metadata["projectionColorReservedBytes"]=std::to_string(colorStats.projectionColorReservedBytes);
    metadata["projectionColorBackingBytes"]=std::to_string(colorStats.projectionColorBackingBytes);
    metadata["projectionColorRequiredAllocationBytes"]=std::to_string(colorStats.projectionColorRequiredAllocationBytes);
    metadata["projectionColorAllocationAlignmentBytes"]=std::to_string(colorStats.projectionColorAllocationAlignmentBytes);
    metadata["projectionColorPolicy"]="RGB21F: sign1/exponent8/mantissa12 per channel, packed into two uint32; truncate 11 low FP32 mantissa bits; no upper color clamp; NaN canonicalized, Inf/signed zero preserved; FP32 blend";
    metadata["projectionCenterConicStorage"]="unchanged FP32 vec2/vec4; quantization deferred";
    metadata["evidenceScope"]=m_runtime.functionalSmoke?"functional smoke; not formal performance/30-minute acceptance":"registered acceptance protocol";
    metadata["capturePolicy"]=m_runtime.functionalSmoke?"resident fallback may be captured during pending refinement":"wait for settled target";
    metadata["sourceSHA256"]=m_scene.sourceSHA256;metadata["sourceCount"]=std::to_string(m_runtime.scenePath.empty()?m_gpuData.gaussianCount:m_scene.sourceCount);
    metadata["builderVersion"]=m_scene.builderVersion;
    metadata["projectedFusion"]=m_runtime.projectedFusion&&m_runtime.mode>='C'&&!m_runtime.fullLeaf?"enabled":"bypassed";
    metadata["fusionImplementation"]=m_runtime.fusionImplementation;
    metadata["fusionErrorMetric"]=m_runtime.fusionImplementation=="split"?"max exact RGBA L2 or conservative temporal certificate bound":"max exact RGBA L2";
    metadata["fusionCheckpointABI"]="runtime only: 27 uvec4; current-view render attributes never cached";
    metadata["fusionRelativeRGBAError"]=std::to_string(m_runtime.fusionRelativeRGBAError);
    metadata["fusionPolicy"]="Current-camera original SH/filter covariance; ordered RGBA moment fit; immutable-group relative L2; singleton/culled members retained; image gates remain separate";
    metadata["fusionCPUReservedBytes"]=std::to_string(m_fusionCPUReservation);
    metadata["cpuProgressMinimumBytes"]=std::to_string(m_cpuProgressMinimumBytes);
    metadata["manifestVersion"]=std::to_string(m_scene.version);
    metadata["selectionErrorPolicy"]=(m_scene.builderVersion==Splat::SplatCenterErrorBuilderVersion||m_scene.builderVersion==Splat::SplatFusionBuilderVersion||m_scene.builderVersion==Splat::SplatLeafFusionBuilderVersion)?
        "All-descendant Gaussian-center nearest-depth transport plus SH/opacity RMS, weighted by upper emitting alpha-area; finite support controls visibility; no independent-noise discount; image gates remain separate":
        m_scene.builderVersion==Splat::SplatRMSErrorBuilderVersion?
        "Nearest-depth Gaussian transport plus SH/opacity RMS, weighted by an upper bound on emitting alpha-area over viewport; conservative support/near-plane safety; no independent-noise discount; image gates remain separate":
        m_scene.builderVersion==Splat::SplatRMSLegacyErrorBuilderVersion?
        "Nearest-depth Gaussian transport plus world-footprint SH/opacity RMS; conservative support/near-plane safety":
        "Legacy finite-support geometry and footprint-scaled dimensionless appearance, projected through nearest depth";
    metadata["sourceIdentityPolicy"]="Manifest identity from verified offline build; streaming startup does not reread source PLY";
    metadata["warmupPolicy"]="One initial warmup, then consecutive registered sample rounds; camera time restarts each round";
    metadata["GPUJoinPolicy"]="128-frame pending ring; missed/late samples remain explicit, invalidate timed evidence and remain in raw GPU CSV";
    metadata["allocatorScope"]="Engine live allocations = pooled used + dedicated committed; available = pooled reserved - pooled used; total retained backing = pooled reserved + dedicated committed; not driver free VRAM";
    metadata["gpuTimingScope"]="gpu_ms is WholeFrame hardware interval before Upload through fullscreen/UI draw and ending render-pass barriers; excludes queue presentation/compositor latency. Pipeline and its six overlapping subpasses, Upload and Presentation are separately reported; never sum overlapping scopes.";
    metadata["initialUploadStagingPeakBackingBytes"]=std::to_string(m_initialUploadStagingPeakBytes);
    metadata["tightTileCulling"]=std::to_string(m_runtime.tightTileCulling);
    metadata["shStorageModeRequested"]=std::to_string(m_runtime.shStorageMode);
    metadata["shStorageModeEffective"]=std::to_string(m_gpuData.shStorageMode);
    metadata["shStoragePolicy"]="0=FP32; 1=two IEEE binary16 scalars per uint32 low-half first; decode/evaluation FP32; full-resident A only";
    metadata["shInvalidInputPolicy"]="FP16 rejects nonfinite or abs(value)>65504 before GPU allocation; nearest-even rounding preserves signed zero and subnormals";
    const uint64_t shScalars=uint64_t(m_gpuData.gaussianCount)*3*(m_gpuData.shDegree+1)*(m_gpuData.shDegree+1);
    metadata["shLogicalBytes"]=std::to_string(Splat::SplatSHStorageBytes(shScalars,m_gpuData.shStorageMode));
    metadata["shTailPaddingBytes"]=std::to_string(m_gpuData.shStorageMode==1?(shScalars%2)*2:0);
    metadata["sourceLogicalBytes"]=std::to_string(uint64_t(m_gpuData.gaussianCount)*52+Splat::SplatSHStorageBytes(shScalars,m_gpuData.shStorageMode));
    metadata["sourceReservedBytes"]=std::to_string(m_pagePool?m_pagePool->GetAllocationStats().sourceReservedBytes:m_legacyAllocations.sourceReservedBytes);
    metadata["sourceBackingBytes"]=std::to_string(m_pagePool?m_pagePool->GetAllocationStats().sourceBackingBytes:m_legacyAllocations.sourceBackingBytes);
    metadata["initialUploadPayloadBytes"]=m_pagePool?"unavailable":std::to_string(m_gpuData.uploadedBytes);
    metadata["initialUploadStagingAlignmentBytes"]=m_pagePool?"unavailable":std::to_string(m_gpuData.stagingAlignmentBytes);
    metadata["initialUploadStagingLogicalBytes"]=m_pagePool?"unavailable":std::to_string(m_gpuData.stagingLogicalBytes);
    metadata["shConversionMilliseconds"]=m_pagePool?"unavailable":std::to_string(m_gpuData.shConversionMilliseconds);
    metadata["initialUploadMilliseconds"]=m_pagePool?"unavailable":std::to_string(m_gpuData.uploadMilliseconds);
    metadata["initialUploadTimingScope"]="CPU wall time for buffer creation/staging/copies/waits including packing; SH conversion separately includes validation and packing; excludes PLY parsing; no standalone GPU upload interval";
    metadata["driverProcessVRAMBytes"]="unavailable";
    if(m_gpuData.shCoefficients){
        RHI::FBufferAllocationInfo info{};uint64_t required=0,alignment=0;
        if(!m_gpuData.shCoefficients->getAllocationInfo(info)||!m_device->getBufferAllocationRequirements(m_gpuData.shCoefficients->getDesc(),required,alignment)){m_uiMessage="SH physical allocation requirements unavailable";return false;}
        metadata["shReservedBytes"]=std::to_string(info.reservationBytes);
        metadata["shBackingBytes"]=std::to_string(info.backingBytes);
        metadata["shRequiredAllocationBytes"]=std::to_string(required);
        metadata["shAllocationAlignmentBytes"]=std::to_string(alignment);
    }
    metadata["rasterSharedMemoryMode"]=std::to_string(m_runtime.rasterSharedMemoryMode);
    metadata["radixSortBlocksPerWorkgroupRequested"]=std::to_string(m_runtime.radixSortBlocksPerWorkgroup);
    metadata["radixSortBlocksPerWorkgroupEffective"]=std::to_string(m_splatPipeline->GetFrameStats().radixSortBlocksPerWorkgroupEffective);
    metadata["radixSortPhaseTimingRequested"]=m_runtime.radixSortPhaseTiming?"true":"false";
    std::ostringstream fixedTime;fixedTime<<std::setprecision(17)<<m_runtime.benchmarkFixedCameraSeconds;
    metadata["benchmarkFixedCameraSeconds"]=fixedTime.str();
    metadata["benchmarkFreezeSelection"]=m_runtime.benchmarkFreezeSelection?"true":"false";
    metadata["selectionSettled"]="false";
    metadata["selectionIdentityPolicy"]="SHA256 source identity and ordered active source indices; original A uses complete ordered range [0,count); pool identity includes selected node order and physical source mapping";
    metadata["capturePrecisionPolicy"]="RGBA8_UNORM linear raw readback; byte equality proves quantized output equality only, not prequantization FP32 equality";
    std::ostringstream canonical;canonical<<std::setprecision(17)<<m_runtime.projectedFusion<<'|'<<m_runtime.fusionImplementation<<'|'<<m_runtime.fusionRelativeRGBAError<<'|'<<m_runtime.mode<<'|'<<m_runtime.width<<'|'<<m_runtime.height<<'|'<<m_runtime.renderWidth<<'|'<<m_runtime.renderHeight<<'|'<<m_runtime.activeCapacity<<'|'<<m_runtime.sortCapacity<<'|'<<m_runtime.displaySHDegree<<'|'<<m_runtime.lod.qualityPixels<<'|'<<m_runtime.lod.hysteresis<<'|'<<m_runtime.fullLeaf<<'|'<<m_runtime.warmupSeconds<<'|'<<m_runtime.sampleSeconds<<'|'<<m_runtime.rounds<<'|'<<m_runtime.streaming.sceneGPUBytes<<'|'<<m_runtime.streaming.cpuCacheBytes<<'|'<<m_runtime.streaming.stagingBytes<<'|'<<m_runtime.streaming.temporaryGPUBytes<<'|'<<m_runtime.streaming.maxUploadBytesPerFrame<<'|'<<m_runtime.streaming.maxConcurrentReads<<'|'<<m_runtime.streaming.retries<<'|'<<m_runtime.streaming.retryDelayMilliseconds;
    canonical<<'|'<<m_runtime.benchmarkFixedCameraSeconds<<'|'<<m_runtime.benchmarkFreezeSelection<<'|'<<m_runtime.lod.freeze<<'|'<<m_runtime.validation<<'|'<<m_runtime.captureOnly<<'|'<<m_runtime.functionalSmoke<<'|'<<m_runtime.captureSettleSeconds;
    for(double t:m_runtime.captureTimes)canonical<<'|'<<t;
    metadata["comparisonConfigurationSHA256"]=HashText(canonical.str());
    canonical<<'|'<<m_runtime.tightTileCulling<<'|'<<m_runtime.radixSortBlocksPerWorkgroup<<'|'<<m_runtime.radixSortPhaseTiming<<'|'<<m_runtime.rasterSharedMemoryMode<<'|'<<m_runtime.shStorageMode<<'|'<<m_runtime.shStorageDegree<<'|'<<m_runtime.projectionColorCompression;metadata["configurationSHA256"]=HashText(canonical.str());
    const char* processor=std::getenv("PROCESSOR_IDENTIFIER");metadata["CPU"]=processor?processor:"unavailable";
    MEMORYSTATUSEX ram{};ram.dwLength=sizeof(ram);metadata["systemRAMBytes"]=GlobalMemoryStatusEx(&ram)?std::to_string(ram.ullTotalPhys):"unavailable";
    auto hash=[&](const std::string& name,const std::string& path){std::string value;if(!Splat::FSplatSceneFormat::SHA256File(path,value,error)){m_uiMessage=error;return false;}metadata[name]=value;return true;};
    if(!hash("sequenceSHA256",m_runtime.sequencePath))return false;
    if(!m_runtime.scenePath.empty()&&!hash("manifestSHA256",m_runtime.scenePath))return false;
    if(m_scene.sourceSHA256.empty()&&!hash("sourceSHA256",m_modelPath))return false;
    wchar_t executable[32768]{};const DWORD size=GetModuleFileNameW(nullptr,executable,32768);
    if(!size||size==32768||!hash("executableSHA256",PathUTF8(std::filesystem::path(executable))))return false;
    for(const char* shader:{"splat_preprocess.spv","splat_fusion.spv","splat_fusion_split_clear.spv","splat_fusion_visible.spv","splat_fusion_split_cheap.spv","splat_fusion_queue_args.spv","splat_fusion_split_exact.spv","splat_fusion_split_fresh.spv","splat_prepare_split_dispatch.spv","splat_prefix_sum.spv","splat_assign_keys.spv","splat_radix_histogram.spv","splat_radix_offsets.spv","splat_radix_scatter.spv","splat_tile_boundaries.spv","splat_prepare_dispatch.spv","splat_render.spv","splat_present.vert.spv","splat_present.frag.spv"}){
        const std::string fallback=std::string("Shaders/Splat/compiled/")+shader;
        if(!hash(std::string("shader_")+shader,Splat::SplatShaderPath(fallback.c_str())))return false;
    }
    m_benchmark=std::make_unique<Splat::FSplatLODBenchmark>();
    if(!m_pagePool){
        m_selectionSHA256=HashText("all-source-v1|"+metadata["sourceSHA256"]+"|ordered-range:0:"+std::to_string(m_gpuData.gaussianCount));
        metadata["selectionSHA256"]=m_selectionSHA256;
        metadata["selectedActiveCount"]=std::to_string(m_gpuData.gaussianCount);
    }
    if(!m_benchmark->Begin(m_runtime,metadata,error)){m_uiMessage=error;return false;}return true;
}
void SplatSceneApplication::updateSceneStreaming(RHI::IRHICommandList* command,const Splat::FCameraUniforms& camera)
{
    if(!m_pagePool||!m_streamer)return;
    const auto start=std::chrono::steady_clock::now();
    auto residency=m_pagePool->GetResidency();
    Splat::FSplatLODView view;std::memcpy(view.view,camera.viewMatrix,sizeof(view.view));std::memcpy(view.projection,camera.projMatrix,sizeof(view.projection));std::memcpy(view.cameraPosition,camera.camPos,sizeof(view.cameraPosition));view.width=m_renderWidth;view.height=m_renderHeight;view.nearPlane=m_controls.NearPlane;view.farPlane=m_controls.FarPlane;view.gaussianScale=m_controls.GaussianScale;view.distanceCulling=m_controls.bDistanceCulling;
    auto settings=m_runtime.lod;settings.qualityPixels=m_controls.LODQualityPixels;settings.freeze=m_controls.bFreezeLOD;settings.activeBudget=m_splatPipeline->GetFrameStats().activeCapacity;settings.sortBudget=m_splatPipeline->GetFrameStats().sortCapacity;settings.sourceBudget=m_pagePool->GetGPUData().gaussianCount;
    if(m_runtime.benchmark&&m_runtime.benchmarkFreezeSelection)settings.freeze=m_benchmarkSelectionFrozen;
    const bool fullLeavesRequested=m_runtime.mode=='B'||m_runtime.fullLeaf||!m_controls.bLODEnabled;
    const bool fusion=m_runtime.projectedFusion&&m_runtime.mode>='C'&&!fullLeavesRequested;
    const bool fullLeaves=fullLeavesRequested&&m_fullLeafRequiredSlots<=m_pagePool->GetGPUData().gaussianCount&&m_scene.sourceCount<=settings.activeBudget;
    if(fullLeaves)settings.qualityPixels=0.001f;
    // GPU image-impact controls approximation; spatial selection supplies original
    // leaf coverage whenever resource/transition budgets permit, else an explicit fallback.
    if(fusion)settings.qualityPixels=0.001f;
    m_selection=m_selector.Select(m_scene,view,settings,residency);
    if(fullLeavesRequested&&!fullLeaves){m_selection.degraded=true;m_selection.degradationReason="Full-leaf selection exceeds source/active capacity; bounded resident LOD retained";}
    bool allLeaves=true;
    if(fullLeaves)for(auto node:m_leafNodes)allLeaves=allLeaves&&residency.IsNodeReady(m_scene,node);
    if(fullLeaves&&allLeaves){m_selection.selectedNodeIds=m_leafNodes;m_selection.desiredNodeIds=m_leafNodes;m_selection.activeCount=m_scene.sourceCount;m_selection.selectedLeafNodes=m_leafNodes.size();m_selection.selectedProxyNodes=m_selection.proxyGaussianCount=0;m_selection.maxSelectedProxyErrorPixels=0;m_selection.selectionVersion=++m_selectionCounter;m_selection.requestPageIds.clear();m_selection.degraded=false;}
    if(fullLeaves){m_selection.desiredNodeIds=m_leafNodes;for(auto node:m_leafNodes)for(auto page:m_scene.nodes[node].pageIds)if(!residency.IsPageReady(page)&&std::find(m_selection.requestPageIds.begin(),m_selection.requestPageIds.end(),page)==m_selection.requestPageIds.end())m_selection.requestPageIds.push_back(page);}
    if(m_runtime.mode=='B'||m_runtime.mode=='C')for(auto page:m_residentTargetPages)if(!residency.IsPageReady(page)&&std::find(m_selection.requestPageIds.begin(),m_selection.requestPageIds.end(),page)==m_selection.requestPageIds.end())m_selection.requestPageIds.push_back(page);
    m_pagePool->ProtectSelection(m_selection);
    std::vector<Splat::FSplatPageRequest> requests;
    for(auto page:m_selection.requestPageIds)if(!m_terminalPageErrors.count(page)&&!m_pagePool->HasPage(page,m_sceneGeneration))requests.push_back({page,m_scene.pages[page].pinned?1e12:1000.0,m_sceneGeneration});
    for(auto page:m_selection.prefetchPageIds)if(!m_terminalPageErrors.count(page)&&!m_pagePool->HasPage(page,m_sceneGeneration))requests.push_back({page,1.0,m_sceneGeneration});
    releaseCPUTransfers();
    m_streamer->UpdateRequests(requests);
    for(auto& result:m_streamer->DrainReady()){
        if(result.sceneGeneration!=m_sceneGeneration||!result.error.empty()){
            const auto id=result.pageId;const auto generation=result.sceneGeneration;const auto failure=result.error;
            {Splat::FSplatPageResult destroyed=std::move(result);}m_streamer->ReleaseTransferred(id,generation);
            if(!failure.empty()&&generation==m_sceneGeneration){m_terminalPageErrors.insert(id);m_uiMessage=failure;if(id<m_scene.pages.size()&&m_scene.pages[id].pinned)failRun("Required resident page failed: "+failure);}
        }else m_pagePool->QueueUpload(std::move(result));
    }
    m_pagePool->Tick(command,m_sceneGeneration);releaseCPUTransfers();
    auto active=m_pagePool->BuildActiveSet(m_selection,fusion,m_runtime.fusionImplementation=="split");
    if(fusion&&m_selection.selectedProxyNodes){m_selection.degraded=true;m_selection.degradationReason="Original fusion sources unavailable: bounded spatial coarse fallback retained";}
    if(active.sourceIndices.size()!=m_selection.activeCount){failRun("Resident active cut failed coverage or generation validation");return;}
    m_splatPipeline->SetActiveSet(active);
    const auto after=m_pagePool->GetResidency();
    if(m_benchmark){
        m_selectionSettled=m_selection.requestPageIds.empty()&&!m_selection.degraded;
        for(auto node:m_selection.desiredNodeIds)m_selectionSettled=m_selectionSettled&&after.IsNodeReady(m_scene,node);
        if(m_identityNodes!=m_selection.selectedNodeIds||m_identityResidencyVersion!=active.residencyVersion||m_selectionSHA256.empty()){
            std::ostringstream identity;identity<<"pool-source-v1|"<<m_scene.sourceSHA256<<'|'<<active.sourceIndices.size()<<'|';
            for(auto node:m_selection.selectedNodeIds)identity<<node<<',';
            identity<<'|';
            std::string bytes=identity.str();
            // Encode the ordered mapping explicitly in little endian, independent of structure padding.
            bytes.reserve(bytes.size()+active.sourceIndices.size()*4);
            for(uint32_t index:active.sourceIndices)for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(char((index>>shift)&255));
            const auto digest=HashText(bytes);
            if(m_benchmarkSelectionFrozen&&digest!=m_selectionSHA256){failRun("Frozen benchmark active selection changed");return;}
            m_selectionSHA256=digest;m_identityNodes=m_selection.selectedNodeIds;m_identityResidencyVersion=active.residencyVersion;
        }
        if(m_runtime.benchmarkFreezeSelection&&m_selectionSettled)m_benchmarkSelectionFrozen=true;
        if(m_samplingStarted&&m_runtime.benchmarkFixedCameraSeconds>=0&&!m_selectionSettled){failRun("Fixed benchmark selection lost settled residency");return;}
        m_benchmark->SetMetadata("selectionSHA256",m_selectionSHA256);
        m_benchmark->SetMetadata("selectionSettled",m_selectionSettled?"true":"false");
        m_benchmark->SetMetadata("selectedActiveCount",std::to_string(active.sourceIndices.size()));
    }
    m_loading=false;for(auto page:m_residentTargetPages)if(!after.IsPageReady(page))m_loading=true;
    if(m_runtime.mode!='B'&&m_runtime.mode!='C'){
        m_loading=false;if(!m_scene.nodes.empty())for(auto page:m_scene.nodes[m_scene.rootNode].pageIds)if(!after.IsPageReady(page))m_loading=true;
    }
    m_selectionMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}

void SplatSceneApplication::SetControlUIVisible(bool visible)
{
    if (m_controlUIVisible == visible) return;
    m_controlUIVisible = visible;
    m_mouseLookActive = false;
    m_firstMouseInput = true;
    if (m_imguiContext)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();
    }
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
    if (m_runtime.renderWidth && m_runtime.renderHeight)
    {
        m_renderWidth = m_runtime.renderWidth;
        m_renderHeight = m_runtime.renderHeight;
    }
    else
    {
        const bool largeWindow = m_windowWidth > 2560 || m_windowHeight > 1440;
        const float maxRenderWidth = largeWindow ? 1600.0f : 1920.0f;
        const float maxRenderHeight = largeWindow ? 900.0f : 1080.0f;
        const float renderScale = std::min({1.0f, maxRenderWidth / m_windowWidth, maxRenderHeight / m_windowHeight});
        m_renderWidth = std::max(2u, (static_cast<uint32>(m_windowWidth * renderScale) & ~1u));
        m_renderHeight = std::max(2u, (static_cast<uint32>(m_windowHeight * renderScale) & ~1u));
    }
    MR_LOG(LogSplatScene, Log, "Splat startup: window=%ux%u render=%ux%u mode=%c validation=%d VSync=%d activeCapacity=%u sortCapacity=%u sceneGPUBytes=%llu",
           m_windowWidth, m_windowHeight, m_renderWidth, m_renderHeight, m_runtime.mode,
           m_config.enableValidation, m_config.enableVSync, m_runtime.activeCapacity, m_runtime.sortCapacity,
           static_cast<unsigned long long>(m_runtime.streaming.sceneGPUBytes));
    m_uiScale = std::clamp(1.2f * m_windowWidth / 1920.0f, 1.2f, 2.0f);

    // Get device
    m_device = getEngine() ? getEngine()->getRHIDevice() : nullptr;
    if (!m_device)
    {
        MR_LOG(LogSplatScene, Error, "Failed to get RHI device");
        m_runFailed = true; m_startupExportFailed = !m_startupExport.videoPath.empty();
        requestExit();
        return;
    }

    // Wait for device to be idle before setup
    m_device->waitForIdle();
    if (!m_config.enableVSync) {
        auto* vk = dynamic_cast<RHI::Vulkan::VulkanDevice*>(m_device);
        if (!vk || !vk->ConfigurePresentationVSync(false)) { failRun("Cannot configure uncapped Vulkan presentation"); return; }
        if (m_runtime.benchmark && !m_runtime.captureOnly && vk->GetConfiguredPresentMode() == VK_PRESENT_MODE_FIFO_KHR) {
            failRun("Only FIFO presentation is available; performance protocol is capped"); return;
        }
    }

    // Load model
    if (!loadModel())
    {
        MR_LOG(LogSplatScene, Error, "Failed to load model: %s", m_modelPath.c_str());
        m_runFailed = true; m_startupExportFailed = !m_startupExport.videoPath.empty();
        requestExit();
        return;
    }

    // Initialize splat pipeline
    if (!initSplatPipeline())
    {
        MR_LOG(LogSplatScene, Error, "Failed to initialize SplatPipeline");
        m_runFailed = true; m_startupExportFailed = !m_startupExport.videoPath.empty();
        requestExit();
        return;
    }

    // Initialize camera
    if (!initCamera())
    {
        MR_LOG(LogSplatScene, Error, "Failed to initialize camera");
        m_runFailed = true; m_startupExportFailed = !m_startupExport.videoPath.empty();
        requestExit();
        return;
    }

    // Initialize fullscreen present pass
    if (!initPresentPass())
    {
        MR_LOG(LogSplatScene, Error, "Failed to initialize present pass");
        m_runFailed = true; m_startupExportFailed = !m_startupExport.videoPath.empty();
        requestExit();
        return;
    }

    if ((!m_runtime.benchmark || m_runtime.captureOnly) && !initImGui())
        MR_LOG(LogSplatScene, Warning, "3DGS controls unavailable: ImGui initialization failed");

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
    m_device->getMemoryStats(m_gpuUsedBytes,m_gpuAvailableBytes);
    if (!initializeBenchmark()) { failRun(m_uiMessage.empty() ? "Benchmark initialization failed" : m_uiMessage); return; }

    if (!m_startupExport.sequencePath.empty() && !m_startupExport.videoPath.empty())
    {
        std::string error;
        if (!m_sequence.LoadJson(m_startupExport.sequencePath, &error) ||
            m_sequence.GetKeyframes().size() < 2)
        {
            MR_LOG(LogSplatScene, Error, "Startup sequence load failed: %s", error.c_str());
            m_startupExportFailed = true;
            requestExit();
            return;
        }
        m_sequenceMode = true;
        m_preExportControls = m_controls;
        m_autoExitAfterExport = true;
        if (!m_videoExporter.start(m_device, m_startupExport.videoPath,
                                   m_renderWidth, m_renderHeight,
                                   m_startupExport.fps, m_sequence.GetDuration(),
                                   m_startupExport.ffmpegPath))
        {
            MR_LOG(LogSplatScene, Error, "Startup export failed: %s",
                   m_videoExporter.getLastError().c_str());
            m_startupExportFailed = true;
            requestExit();
        }
    }
}

void SplatSceneApplication::onUpdate(float32 deltaTime)
{
    m_updateStart = std::chrono::steady_clock::now();
    m_deltaTime = deltaTime;
    if (m_pendingSceneSwitch) {
        m_pendingSceneSwitch = false;
        destroyScene();
        if(m_runFailed)return;
        ++m_sceneGeneration;
        if(m_pendingRuntime){m_runtime=*m_pendingRuntime;m_pendingRuntime.reset();}
        m_runtime.scenePath = m_nextScenePath; m_runtime.sourcePLY = m_nextSourcePLY; m_runtime.mode = m_nextMode;
        m_modelPath = m_nextSourcePLY;
        if (!loadModel() || !initSplatPipeline() || !initCamera()) { failRun("Scene replacement failed: " + m_uiMessage); return; }
        for(auto& descriptors:m_present.descriptorSets){
            descriptors=m_device->allocateDescriptorSet(m_present.setLayout);
            if(!descriptors){failRun("Scene replacement present descriptor allocation failed");return;}
        }
        m_uiRuntime.reset();
        m_firstFrame = true; m_samplingStarted = false;
    }

    if (m_restoreExportState)
    {
        m_controls = m_preExportControls;
        m_restoreExportState = false;
        applyControlState();
    }
    if (m_fpsCamera && !m_runtime.benchmark && !m_sequence.IsPlaying() && !m_videoExporter.isActive())
    {
        const FVector& p = m_fpsCamera->GetPosition();
        m_controls.PositionX = static_cast<float>(p.X);
        m_controls.PositionY = static_cast<float>(p.Y);
        m_controls.PositionZ = static_cast<float>(p.Z);
        m_controls.Yaw = m_fpsCamera->GetYaw();
        m_controls.Pitch = m_fpsCamera->GetPitch();
        m_controls.Roll = m_fpsCamera->GetRoll();
        m_controls.FOV = m_fpsCamera->GetFOV();
    }
    m_smoothedFrameMs = m_smoothedFrameMs * 0.9f + FMath::Max(deltaTime * 1000.0f, 0.01f) * 0.1f;
    m_memoryPollSeconds += deltaTime;
    if (m_memoryPollSeconds >= 1.0f && m_device)
    {
        m_device->getMemoryStats(m_gpuUsedBytes, m_gpuAvailableBytes);
        m_memoryPollSeconds = 0.0f;
    }
    if (m_controlUIVisible && m_imguiContext && m_imguiRenderer)
    {
        m_imguiContext->BeginFrame(deltaTime, m_windowWidth, m_windowHeight);
        drawControlPanels();
        m_imguiContext->EndFrame();
    }
    const bool wasPlaying = m_sequence.IsPlaying();
    if (!m_videoExporter.isActive() && !m_runtime.benchmark) m_sequence.Tick(deltaTime);
    const auto playbackControls = m_controls;
    if (m_videoExporter.isActive())
        m_controls = m_sequence.Sample(m_videoExporter.getSampleTime());
    else if (wasPlaying)
        m_controls = m_sequence.Sample(m_sequence.GetPlaybackTime());
    if (m_videoExporter.isActive() || wasPlaying) {
        // Camera keyframes do not serialize LOD controls. Sampling their default
        // state must not reset the live quality, freeze, enable or debug settings.
        m_controls.bLODEnabled = playbackControls.bLODEnabled;
        m_controls.bFreezeLOD = playbackControls.bFreezeLOD;
        m_controls.bLODLevelOverlay = playbackControls.bLODLevelOverlay;
        m_controls.bLODBoundsOverlay = playbackControls.bLODBoundsOverlay;
        m_controls.LODQualityPixels = playbackControls.LODQualityPixels;
    }
    if (m_runtime.benchmark) {
        double elapsed = m_samplingStarted ? std::chrono::duration<double>(m_updateStart - m_sampleStart).count() : 0;
        m_sequenceTime = m_runtime.captureOnly && m_captureIndex < m_runtime.captureTimes.size() ? m_runtime.captureTimes[m_captureIndex] :
            std::fmod(elapsed < m_runtime.warmupSeconds ? elapsed : elapsed - m_runtime.warmupSeconds, m_runtime.sampleSeconds);
        if (!m_runtime.captureOnly) m_sequenceTime = std::fmod(m_sequenceTime, m_sequence.GetDuration());
        if(m_runtime.benchmarkFixedCameraSeconds>=0)m_sequenceTime=m_runtime.benchmarkFixedCameraSeconds;
        const auto old = m_controls; m_controls = m_sequence.Sample(m_sequenceTime);
        m_controls.bLODEnabled = old.bLODEnabled; m_controls.bFreezeLOD = old.bFreezeLOD;
        m_controls.bLODLevelOverlay = old.bLODLevelOverlay; m_controls.bLODBoundsOverlay = old.bLODBoundsOverlay;
        m_controls.LODQualityPixels = old.LODQualityPixels;
    }
    m_controls.ClampToValid();
    applyControlState();

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

            const bool bCaptureKeyboard = m_runtime.benchmark || m_videoExporter.isActive() || (m_controlUIVisible && m_imguiContext && m_imguiContext->WantsCaptureKeyboard());
            if (!bCaptureKeyboard && !m_sequence.IsPlaying() && inputManager->isKeyDown(EKey::W))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Forward, deltaTime, bSprinting);
            if (!bCaptureKeyboard && !m_sequence.IsPlaying() && inputManager->isKeyDown(EKey::S))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Backward, deltaTime, bSprinting);
            if (!bCaptureKeyboard && !m_sequence.IsPlaying() && inputManager->isKeyDown(EKey::A))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Left, deltaTime, bSprinting);
            if (!bCaptureKeyboard && !m_sequence.IsPlaying() && inputManager->isKeyDown(EKey::D))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Right, deltaTime, bSprinting);
            if (!bCaptureKeyboard && !m_sequence.IsPlaying() && inputManager->isKeyDown(EKey::C))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Up, deltaTime, bSprinting);
            if (!bCaptureKeyboard && !m_sequence.IsPlaying() && inputManager->isKeyDown(EKey::Space))
                m_fpsCamera->ProcessKeyboard(ECameraMovement::Down, deltaTime, bSprinting);
            if (!bCaptureKeyboard && !m_sequence.IsPlaying() && inputManager->isKeyDown(EKey::Q))
                m_fpsCamera->ProcessRoll(-90.0f, deltaTime);
            if (!bCaptureKeyboard && !m_sequence.IsPlaying() && inputManager->isKeyDown(EKey::E))
                m_fpsCamera->ProcessRoll(90.0f, deltaTime);
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
    if(m_runFailed||shouldExit())return;
    const auto renderStart=std::chrono::steady_clock::now();
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
    if (!context) { failRun("No Vulkan command list context"); return; }

    RHI::IRHICommandList* cmdList = m_device->getImmediateCommandList();
    if (!cmdList) { failRun("No immediate command list"); return; }

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
    Splat::FSplatRenderSettings renderSettings;
    renderSettings.nearPlane = m_controls.NearPlane;
    renderSettings.farPlane = m_controls.FarPlane;
    renderSettings.distanceCulling = m_controls.bDistanceCulling;
    renderSettings.wireframe = m_controls.bWireframe;
    renderSettings.gaussianScale = m_controls.GaussianScale;
    renderSettings.fusionRelativeRGBAError = m_runtime.fusionRelativeRGBAError;
    renderSettings.tightTileCulling = m_runtime.tightTileCulling;
    renderSettings.rasterSharedMemoryMode = m_runtime.rasterSharedMemoryMode;
    m_splatPipeline->setRenderSettings(renderSettings);
    if(m_benchmark){
        const std::string cameraBytes(reinterpret_cast<const char*>(&camera),sizeof(camera));
        const auto digest=HashText(cameraBytes);
        if(m_samplingStarted&&m_runtime.benchmarkFixedCameraSeconds>=0&&!m_cameraSHA256.empty()&&digest!=m_cameraSHA256){failRun("Fixed benchmark camera changed");return;}
        m_cameraSHA256=digest;m_benchmark->SetMetadata("cameraSHA256",digest);
        if(!m_pagePool){m_selectionSettled=true;m_benchmarkSelectionFrozen=m_runtime.benchmarkFreezeSelection;m_benchmark->SetMetadata("selectionSettled","true");}
    }

    cmdList->begin();
    const uint64_t recordingFrame=m_splatPipeline->ReserveFrameId();
    cmdList->writeGPUTimestamp(recordingFrame,"WholeFrame",true);
    cmdList->writeGPUTimestamp(recordingFrame,"Upload",true);
    m_lastCamera=camera;
    updateSceneStreaming(cmdList,camera);
    cmdList->resourceBarrier();
    cmdList->writeGPUTimestamp(recordingFrame,"Upload",false);
    if(m_runFailed){cmdList->end();return;}
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
        if(m_runtime.benchmark||m_pagePool)failRun(m_splatPipeline->GetFrameStats().degradationReason);
        if (m_autoExitAfterExport) { m_startupExportFailed = true; requestExit(); }
        cmdList->end();
        m_device->present();
        return;
    }
    bool capturing=false;
    if(m_benchmark&&m_runtime.captureOnly&&m_samplingStarted&&m_captureIndex<m_runtime.captureTimes.size()&&
       std::chrono::duration<double>(std::chrono::steady_clock::now()-m_captureHoldStart).count()>=m_runtime.captureSettleSeconds){
        const bool settled=m_selectionSettled||m_runtime.mode=='E';
        if(settled||m_runtime.functionalSmoke){std::string error;capturing=m_benchmark->RecordCapture(m_device,cmdList,splatOutput,m_splatPipeline->GetFrameStats().frame,m_sequenceTime,camera,m_splatPipeline->GetFrameStats().temporaryBytes,error);if(!capturing)failRun(error);}
        else if(std::chrono::duration<double>(std::chrono::steady_clock::now()-m_captureHoldStart).count()>60)failRun("Capture did not converge within 60 seconds");
    }
    if (m_videoExporter.isActive() && !m_videoExporter.captureFrame(cmdList, splatOutput))
    {
        m_uiMessage = m_videoExporter.getLastError();
        m_videoExporter.cancel();
        m_restoreExportState = true;
        if (m_autoExitAfterExport) { m_startupExportFailed = true; requestExit(); }
    }
    MR_LOG(LogSplatScene, Verbose, "onRender: SplatPipeline executed, output texture=%s (%ux%u)",
           splatOutput->getDebugName().c_str(), splatOutput->getWidth(), splatOutput->getHeight());

    // ---- Step 2: Transition output from compute (UnorderedAccess) to shader-read ----
    cmdList->writeGPUTimestamp(recordingFrame,"Presentation",true);
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

    if (m_controlUIVisible && m_imguiRenderer && m_imguiContext)
        m_imguiRenderer->RenderDrawData(cmdList, ImGui::GetDrawData());

    // ---- Step 4: Finish & present ----
    cmdList->endRenderPass();
    cmdList->writeGPUTimestamp(recordingFrame,"Presentation",false);
    cmdList->writeGPUTimestamp(recordingFrame,"WholeFrame",false);
    cmdList->end();
    const auto beforeSubmission=m_device->getLastSubmissionToken();
    m_device->present();
    const auto submitted=m_device->getLastSubmissionToken();
    const bool successfulSubmission=submitted.value&&submitted.value!=beforeSubmission.value;
    if(successfulSubmission){m_splatPipeline->MarkFrameSubmitted(submitted);if(m_pagePool)m_pagePool->MarkFrameSubmitted(submitted);}
    else if(m_runtime.benchmark||m_pagePool)failRun("Frame did not produce a new actual submission token");
    const auto presented=std::chrono::steady_clock::now();
    if(m_benchmark&&successfulSubmission){
        auto pipeline=m_splatPipeline->GetFrameStats();
        const uint64_t originalCount=m_runtime.scenePath.empty()?m_gpuData.gaussianCount:m_scene.sourceCount;
        const bool coarseReady=(!m_pagePool||m_scene.nodes.empty()||m_pagePool->GetResidency().IsNodeReady(m_scene,m_scene.rootNode))&&(pipeline.activeCount>0||originalCount==0);
        if(coarseReady&&!m_firstUsableRecorded)m_readyFrameTimes.emplace(pipeline.frame,std::chrono::duration<double>(presented-m_startupStart).count());
        for(const auto& sample:pipeline.countSamples){auto submittedReady=m_readyFrameTimes.find(sample.frame);if(!m_firstUsableRecorded&&submittedReady!=m_readyFrameTimes.end()&&!sample.overflow&&(sample.count>0||originalCount==0)){m_firstUsableRecorded=true;m_benchmark->SetMetadata("firstUsableFrameSeconds",std::to_string(submittedReady->second));m_benchmark->SetMetadata("firstUsableFrameID",std::to_string(sample.frame));}}
        if(m_firstUsableRecorded)m_readyFrameTimes.clear();while(m_readyFrameTimes.size()>128)m_readyFrameTimes.erase(m_readyFrameTimes.begin());
        const bool fixedReady=m_runtime.benchmarkFixedCameraSeconds<0||m_selectionSettled;
        if(coarseReady&&!m_loading&&fixedReady&&!m_samplingStarted){m_samplingStarted=true;m_sampleStart=presented;m_captureHoldStart=presented;m_benchmark->SetMetadata("residentTargetReadySeconds",std::to_string(std::chrono::duration<double>(presented-m_startupStart).count()));}
        Splat::FSplatLODBenchmarkFrame row;row.frame=pipeline.frame;row.pipeline=pipeline;row.sequenceSeconds=m_sequenceTime;row.wholeFrameTimingRequired=true;
        row.elapsedSeconds=m_samplingStarted?std::chrono::duration<double>(presented-m_sampleStart).count():0;
        row.round=row.elapsedSeconds>=m_runtime.warmupSeconds?uint32_t((row.elapsedSeconds-m_runtime.warmupSeconds)/m_runtime.sampleSeconds):0;
        row.sampled=!m_runtime.captureOnly&&m_samplingStarted&&row.elapsedSeconds>=m_runtime.warmupSeconds&&row.round<m_runtime.rounds;
        row.excluded=m_runtime.captureOnly||capturing;row.loading=m_loading;row.degraded=m_selection.degraded||pipeline.degraded;
        row.frameMilliseconds=m_lastPresent==std::chrono::steady_clock::time_point{}?0:std::chrono::duration<double,std::milli>(presented-m_lastPresent).count();
        row.cpuMilliseconds=std::chrono::duration<double,std::milli>(presented-(m_updateStart==std::chrono::steady_clock::time_point{}?renderStart:m_updateStart)).count();row.selectionMilliseconds=m_selectionMs;
        if(auto* manager=vulkanDevice->getMemoryManager()){
            RHI::FVulkanMemoryManager::FMemoryStats allocation;manager->GetMemoryStats(allocation);
            row.allocatorBytes=allocation.TotalAllocated;row.allocatorAvailableBytes=allocation.PooledReserved-allocation.PooledUsed;
            row.allocatorPooledReservedBytes=allocation.PooledReserved;row.allocatorPooledUsedBytes=allocation.PooledUsed;row.allocatorDedicatedCommittedBytes=allocation.DedicatedCommitted;
        }
        if(m_streamer){row.io=m_streamer->GetStats();row.cpu=m_streamer->GetMemoryStats();row.cpu.totalReservedBytes+=m_fusionCPUReservation;row.cpu.metadataBytes+=m_fusionCPUReservation;}
        row.selectedLeafNodes=m_selection.selectedLeafNodes;row.selectedProxyNodes=m_selection.selectedProxyNodes;
        row.proxyGaussianCount=m_selection.proxyGaussianCount;row.maxSelectedProxyErrorPixels=m_selection.maxSelectedProxyErrorPixels;
        if(m_pagePool){row.pool=m_pagePool->GetStats();row.allocations=m_pagePool->GetAllocationStats();}
        else {row.allocations=m_legacyAllocations;row.pool.residentBytes=m_legacyAllocations.sourceReservedBytes;}
        row.pipeline.temporaryBytes+=m_benchmark->GetCaptureAllocatedBytes();
        std::string error;if(!m_benchmark->Record(row,error))failRun(error);
        if(capturing){if(!m_benchmark->FinishCapture(submitted,error))failRun(error);else{++m_captureIndex;m_captureHoldStart=std::chrono::steady_clock::now();if(m_captureIndex==m_runtime.captureTimes.size()){m_benchmarkCompleted=true;requestExit();}}}
        if(!m_runtime.captureOnly&&m_samplingStarted&&row.elapsedSeconds>=m_runtime.warmupSeconds+m_runtime.sampleSeconds*m_runtime.rounds){
            if(++m_finishFrames>=32){m_benchmarkCompleted=true;if(m_runtime.mode=='E'&&m_pagePool->GetStats().evictedPages==0)failRun("Restricted E completed without actual eviction");requestExit();}
        }
        if(!m_samplingStarted&&std::chrono::duration<double>(presented-m_startupStart).count()>300)failRun("Required startup residency did not converge within 300 seconds");
        m_lastPresent=presented;
    }
    if (m_videoExporter.isActive())
    {
        if (!m_videoExporter.finishFrame())
        {
            m_uiMessage = m_videoExporter.getLastError();
            m_videoExporter.cancel();
            m_restoreExportState = true;
            if (m_autoExitAfterExport) { m_startupExportFailed = true; requestExit(); }
        }
        else if (m_videoExporter.getFrameIndex() >= m_videoExporter.getFrameCount())
        {
            const bool success = m_videoExporter.finish();
            m_uiMessage = success ? "视频导出完成" : m_videoExporter.getLastError();
            if (!success && m_autoExitAfterExport) m_startupExportFailed = true;
            m_restoreExportState = true;
            if (m_autoExitAfterExport) requestExit();
        }
    }

    MR_LOG(LogSplatScene, Verbose, "onRender: Frame %u presented", vulkanDevice->getCurrentFrame());
    if(successfulSubmission)m_firstFrame = false;

    // Opt-in, fixed-camera benchmark. Count completed presents with a monotonic
    // clock, independent of Application's clamped float delta time and FPS title.
    const int benchmarkSeconds = GetSplatBenchmarkSeconds();
    if (benchmarkSeconds > 0 && !m_runtime.benchmark)
    {
        using Clock = std::chrono::steady_clock;
        static const auto firstPresent = Clock::now();
        static Clock::time_point sampleStart;
        static Clock::time_point bucketStart;
        static uint64 sampleFrames = 0;
        static uint64 bucketFrames = 0;
        static bool firstBucket = true;
        const auto now = Clock::now();
        if (std::chrono::duration<double>(now - firstPresent).count() >= 5.0)
        {
            if (sampleFrames == 0)
            {
                sampleStart = now;
                bucketStart = now;
            }
            ++sampleFrames;
            ++bucketFrames;
            const double elapsed = std::chrono::duration<double>(now - sampleStart).count();
            const double bucketElapsed = std::chrono::duration<double>(now - bucketStart).count();
            if (bucketElapsed >= 5.0)
            {
                std::printf("[SPLAT_BENCH_BUCKET] t=%.1fs frames=%llu fps=%.3f\n",
                            elapsed, static_cast<unsigned long long>(bucketFrames - (firstBucket ? 1 : 0)),
                            static_cast<double>(bucketFrames - (firstBucket ? 1 : 0)) / bucketElapsed);
                std::fflush(stdout);
                bucketStart = now;
                bucketFrames = 0;
                firstBucket = false;
            }
            if (elapsed >= benchmarkSeconds)
            {
                std::printf("[SPLAT_BENCH] warmup=5s frames=%llu seconds=%.3f average_fps=%.3f\n",
                            static_cast<unsigned long long>(sampleFrames), elapsed,
                            static_cast<double>(sampleFrames - 1) / elapsed);
                std::fflush(stdout);
                requestExit();
            }
        }
    }
}

void SplatSceneApplication::onShutdown()
{
    MR_LOG(LogSplatScene, Log, "Shutting down SplatSceneApplication...");

    // The last presented frame can still reference splat and present resources.
    if (m_device)
        m_device->waitForIdle();
    if(m_benchmark){
        std::string error;
        if(m_splatPipeline&&!m_benchmark->Resolve(m_splatPipeline->CollectCompletedFrameStats(),error))m_runFailed=true;
    }
    m_videoExporter.cancel();

    m_imguiInput.Reset();
    if (m_imguiRenderer) { m_imguiRenderer->Shutdown(); m_imguiRenderer.Reset(); }
    if (m_imguiContext) { m_imguiContext->Shutdown(); m_imguiContext.Reset(); }

    // Release splat resources in reverse order
    destroyScene();
    if(m_benchmark){std::string error;if(!m_benchmark->Finish(m_benchmarkCompleted&&!m_runFailed,m_uiMessage.empty()?"Run ended; numeric acceptance gates require downstream evaluation":m_uiMessage,error)){m_runFailed=true;if(!error.empty())MR_LOG(LogSplatScene,Error,"Benchmark finalization: %s",error.c_str());}m_benchmark.reset();}

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

    // Recorder readback and present references are released after scene teardown.
    // Retire their allocations only after another explicit completion proof.
    if (auto* vk = dynamic_cast<RHI::Vulkan::VulkanDevice*>(m_device))
        if (!vk->DrainCompletedDestructions()) m_runFailed = true;
    m_device = nullptr;
    MR_LOG(LogSplatScene, Log, "SplatSceneApplication shutdown complete");
}

void SplatSceneApplication::onWindowResize(uint32 width, uint32 height)
{
    if(m_runtime.benchmark&&(width!=m_runtime.width||height!=m_runtime.height)){failRun("Benchmark resolution changed during a registered run");return;}
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
    if (m_imguiRenderer)
        m_imguiRenderer->OnWindowResize(m_windowWidth, m_windowHeight);
    const float newUiScale = std::clamp(1.2f * m_windowWidth / 1920.0f, 1.2f, 2.0f);
    if (m_imguiContext && newUiScale != m_uiScale)
    {
        ImGui::GetStyle().ScaleAllSizes(newUiScale / m_uiScale);
        ImGui::GetIO().FontGlobalScale = newUiScale;
    }
    m_uiScale = newUiScale;
}

// ============================================================================
// Input Events
// ============================================================================

void SplatSceneApplication::onKeyPressed(EKey key)
{
    if (key == EKey::F1)
    {
        SetControlUIVisible(!m_controlUIVisible);
        return;
    }
    if (m_controlUIVisible && m_imguiInput) m_imguiInput->OnKeyEvent(key, EInputAction::Pressed);
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

void SplatSceneApplication::onKeyReleased(EKey key)
{
    if (m_controlUIVisible && key != EKey::F1 && m_imguiInput) m_imguiInput->OnKeyEvent(key, EInputAction::Released);
}

void SplatSceneApplication::onMouseButtonPressed(EKey button, const MousePosition& position)
{
    if (m_controlUIVisible && m_imguiInput) m_imguiInput->OnMouseButton(button, true);
    if (m_controlUIVisible && m_imguiContext && m_imguiContext->WantsCaptureMouse()) return;
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
    if (m_controlUIVisible && m_imguiInput) m_imguiInput->OnMouseButton(button, false);
    (void)position;
    if (button == EKey::MouseRight)
    {
        m_mouseLookActive = false;
    }
}

void SplatSceneApplication::onMouseMoved(const MousePosition& position)
{
    if (m_controlUIVisible && m_imguiInput) m_imguiInput->OnMouseMove(static_cast<float>(position.x), static_cast<float>(position.y));
    if (m_runtime.benchmark || m_sequence.IsPlaying() || m_videoExporter.isActive() || (m_controlUIVisible && m_imguiContext && m_imguiContext->WantsCaptureMouse())) return;
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
    if (m_controlUIVisible && m_imguiInput) m_imguiInput->OnMouseScroll(static_cast<float>(xOffset), static_cast<float>(yOffset));
    if (m_runtime.benchmark || m_sequence.IsPlaying() || m_videoExporter.isActive() || (m_controlUIVisible && m_imguiContext && m_imguiContext->WantsCaptureMouse())) return;
    if (m_fpsCamera)
    {
        m_fpsCamera->ProcessMouseScroll(static_cast<float>(yOffset));
    }
}

// ============================================================================
// Initialization Helpers
// ============================================================================

bool SplatSceneApplication::initImGui()
{
    m_imguiContext = MakeUnique<Editor::FImGuiContext>();
    if (!m_imguiContext->Initialize()) { m_imguiContext.Reset(); return false; }
    // Merely focusing a panel must not reserve WASD for ImGui navigation.
    // Active widgets/text fields still request keyboard capture normally.
    ImGui::GetIO().ConfigNavCaptureKeyboard = false;
    ImGui::GetIO().FontGlobalScale = m_uiScale;
    ImGui::GetStyle().ScaleAllSizes(m_uiScale);
    m_imguiRenderer = MakeUnique<Editor::FImGuiRenderer>();
    if (!m_imguiRenderer->Initialize(m_device))
    {
        m_imguiRenderer.Reset();
        m_imguiContext->Shutdown();
        m_imguiContext.Reset();
        return false;
    }
    m_imguiInput = MakeUnique<Editor::FImGuiInputHandler>(m_imguiContext.Get());
    m_imguiRenderer->OnWindowResize(m_windowWidth, m_windowHeight);
    return true;
}

void SplatSceneApplication::applyControlState()
{
    if (m_fpsCamera)
    {
        m_fpsCamera->SetPosition(FVector(m_controls.PositionX, m_controls.PositionY, m_controls.PositionZ));
        m_fpsCamera->SetRotation(m_controls.Yaw, m_controls.Pitch, m_controls.Roll);
        auto settings = m_fpsCamera->GetSettings();
        settings.MouseSensitivity = m_controls.MouseSensitivity;
        settings.MovementSpeed = m_controls.MovementSpeed;
        settings.MinFOV = 30.0f;
        settings.MaxFOV = 120.0f;
        settings.bLocalVerticalMovement = true;
        m_fpsCamera->SetSettings(settings);
        m_fpsCamera->SetFOV(m_controls.FOV);
    }
}

void SplatSceneApplication::drawControlPanels()
{
    // Keep every panel in its own region, including when an old ImGui layout
    // has saved overlapping positions. Fixed panels retain normal scrolling.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float gap = 12.0f;
    const float availableWidth = std::max(1.0f, display.x - 4.0f * gap);
    const float availableHeight = std::max(1.0f, display.y - 2.0f * gap);
    const float leftWidth = std::min(390.0f, availableWidth * 0.25f);
    const float rightWidth = std::min(450.0f, availableWidth * 0.30f);
    const float middleWidth = availableWidth - leftWidth - rightWidth;
    const float middleX = leftWidth + 2.0f * gap;
    const float rightX = display.x - rightWidth - gap;
    const float leftScale = std::min(1.0f, availableHeight / 818.0f);
    const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    const double gib = 1073741824.0;
    auto budgetGiB = [gib](const char* label, uint64_t& bytes) {
        double value = static_cast<double>(bytes) / gib;
        if (ImGui::InputDouble(label, &value, 0.03125, 0.25, "%.5f") &&
            std::isfinite(value) && value > 0.0 && value <= 262144.0)
            bytes = static_cast<uint64_t>(std::llround(value * gib));
    };
    auto capacityMillions = [](const char* label, uint32_t& count) {
        double value = count / 1000000.0;
        if (ImGui::InputDouble(label, &value, 0.1, 1.0, "%.6f") &&
            std::isfinite(value) && value > 0.0 && value <= UINT32_MAX / 1000000.0)
            count = static_cast<uint32_t>(std::llround(value * 1000000.0));
    };
    if(!m_uiRuntime){m_uiRuntime=std::make_unique<Splat::FSplatLODRuntimeOptions>(m_runtime);std::snprintf(m_scenePathUI,sizeof(m_scenePathUI),"%s",m_runtime.scenePath.c_str());std::snprintf(m_sourcePathUI,sizeof(m_sourcePathUI),"%s",m_modelPath.c_str());}
    ImGui::SetNextWindowPos(ImVec2(middleX, gap), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(middleWidth, availableHeight), ImGuiCond_Always);
    if(ImGui::Begin("LOD / Streaming", nullptr, panelFlags)){
        ImGui::Text("Mode %c | generation %llu",m_runtime.mode,static_cast<unsigned long long>(m_sceneGeneration));
        ImGui::TextUnformatted(m_loading?"Loading required coarse/resident pages...":"Required representation ready");
        ImGui::Checkbox("LOD enabled",&m_controls.bLODEnabled);ImGui::Checkbox("Freeze selection",&m_controls.bFreezeLOD);
        ImGui::SliderFloat("Quality pixels",&m_controls.LODQualityPixels,0.001f,8.0f,"%.3f",ImGuiSliderFlags_Logarithmic);
        ImGui::Checkbox("Level outlines",&m_controls.bLODLevelOverlay);ImGui::Checkbox("Support bounds",&m_controls.bLODBoundsOverlay);
        if(m_pagePool&&m_streamer){const auto p=m_pagePool->GetStats();const auto a=m_pagePool->GetAllocationStats();const auto c=m_streamer->GetMemoryStats();const auto io=m_streamer->GetStats();
            ImGui::Text("Active %.2f M | selected nodes %u",m_selection.activeCount/1000000.0,unsigned(m_selection.selectedNodeIds.size()));
            ImGui::Text("Resident %.3f GiB | source physical %.3f GiB",p.residentBytes/gib,a.sourceReservedBytes/gib);
            ImGui::Text("CPU charged %.3f GiB | encoded cache %.3f GiB",c.totalReservedBytes/gib,c.encodedCacheBytes/gib);
            ImGui::Text("Reads %u pending / %u active | uploads %llu",io.pendingReads,io.activeReads,static_cast<unsigned long long>(p.pendingUploads));
            ImGui::Text("Staging live / reserved: %.5f / %.5f GiB",p.stagingBytes/gib,a.stagingReservedBytes/gib);
            if(m_selection.degraded)ImGui::TextWrapped("Degraded: %s",m_selection.degradationReason.c_str());
        }
        if(m_splatPipeline){const auto& s=m_splatPipeline->GetFrameStats();ImGui::Text("Source / active / sort capacity: %.2f / %.2f / %.2f M",s.sourceCapacity/1000000.0,s.activeCapacity/1000000.0,s.sortCapacity/1000000.0);
            ImGui::Text("Temporary physical %.3f GiB | overflow %llu",s.temporaryBytes/gib,static_cast<unsigned long long>(s.overflowFrames));
            if(s.countAvailable)ImGui::Text("Sort entries %.2f M (completed frame %llu)",s.actualSortCount/1000000.0,static_cast<unsigned long long>(s.countFrame));
            for(const auto& sample:s.timestampSamples)ImGui::Text("GPU %s %.3f ms (frame %llu)",sample.label.c_str(),sample.milliseconds,static_cast<unsigned long long>(sample.frame));
            if(!s.timingAvailable)ImGui::TextUnformatted("Hardware GPU timing unavailable");
        }
        ImGui::Separator();ImGui::InputText("Scene manifest",m_scenePathUI,sizeof(m_scenePathUI));ImGui::InputText("Original PLY",m_sourcePathUI,sizeof(m_sourcePathUI));
        int mode=m_uiRuntime->mode-'A';if(ImGui::Combo("Requested mode",&mode,"A Original\0B Full leaf\0C Resident LOD\0D Streaming\0E Restricted\0"))m_uiRuntime->mode=char('A'+mode);
        ImGui::TextDisabled("GiB = 1024^3 bytes | M = 1,000,000 items");
        int shStorage=int(m_uiRuntime->shStorageMode);
        if(ImGui::Combo("SH storage (reload on Apply)",&shStorage,"FP32\0FP16 packed (A only)\0"))m_uiRuntime->shStorageMode=uint32_t(shStorage);
        ImGui::Text("Effective SH storage: %s",m_gpuData.shStorageMode==1?"FP16 packed / FP32 compute":"FP32");
        budgetGiB("Scene GPU budget (GiB)",m_uiRuntime->streaming.sceneGPUBytes);
        budgetGiB("CPU cache budget (GiB)",m_uiRuntime->streaming.cpuCacheBytes);
        budgetGiB("Staging budget (GiB)",m_uiRuntime->streaming.stagingBytes);
        budgetGiB("Temporary GPU budget (GiB)",m_uiRuntime->streaming.temporaryGPUBytes);
        double uploadMiB=m_uiRuntime->streaming.maxUploadBytesPerFrame/1048576.0;
        if(ImGui::InputDouble("Upload / frame (MiB)",&uploadMiB,1.0,8.0,"%.2f")&&std::isfinite(uploadMiB)&&uploadMiB>0.0&&uploadMiB<=268435456.0)
            m_uiRuntime->streaming.maxUploadBytesPerFrame=static_cast<uint64_t>(std::llround(uploadMiB*1048576.0));
        capacityMillions("Active capacity (M)",m_uiRuntime->activeCapacity);
        capacityMillions("Sort capacity (M)",m_uiRuntime->sortCapacity);
        if(ImGui::Button("Apply scene / mode / budgets")){m_uiRuntime->scenePath=m_scenePathUI;m_uiRuntime->sourcePLY=m_sourcePathUI;std::string error;if(!m_uiRuntime->Validate(error))m_uiMessage=error;else if(QueueSceneSwitch(m_scenePathUI,m_sourcePathUI,m_uiRuntime->mode))m_pendingRuntime=std::make_unique<Splat::FSplatLODRuntimeOptions>(*m_uiRuntime);else m_uiMessage="Scene switch is invalid for this run";}
        if(!m_uiMessage.empty())ImGui::TextWrapped("%s",m_uiMessage.c_str());
    }ImGui::End();
    drawLODOverlay();
    ImGui::SetNextWindowPos(ImVec2(gap, gap), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftWidth, 190.0f * leftScale), ImGuiCond_Always);
    if (ImGui::Begin("性能 / Performance", nullptr, panelFlags))
    {
        ImGui::Text("3DGS 画面 FPS: %u", m_fps);
        ImGui::Text("整帧间隔（平滑）: %.2f ms", m_smoothedFrameMs);
        ImGui::TextDisabled("含 UI 与呈现；GPU pass 耗时需单独测量");
        if (m_gpuUsedBytes || m_gpuAvailableBytes)
            ImGui::Text("GPU 资源池 已用 / 已保留: %.3f / %.3f GiB", m_gpuUsedBytes / gib,
                        (m_gpuUsedBytes + m_gpuAvailableBytes) / gib);
        else
            ImGui::TextDisabled("GPU 内存统计不可用");
        ImGui::Text("窗口 %u x %u px | 渲染 %u x %u px",m_windowWidth,m_windowHeight,m_renderWidth,m_renderHeight);
        ImGui::Text("高斯数量: %.2f M",m_splatPipeline ? m_splatPipeline->getGaussianCount()/1000000.0 : 0.0);
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(gap, gap + 202.0f * leftScale), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftWidth, 315.0f * leftScale), ImGuiCond_Always);
    if (ImGui::Begin("相机 / Camera", nullptr, panelFlags))
    {
        const bool locked = m_sequence.IsPlaying();
        ImGui::BeginDisabled(locked);
        float p[3] = {m_controls.PositionX, m_controls.PositionY, m_controls.PositionZ};
        if (ImGui::DragFloat3("XYZ（单位）", p, 0.05f))
        { m_controls.PositionX = p[0]; m_controls.PositionY = p[1]; m_controls.PositionZ = p[2]; }
        float rot[3] = {m_controls.Yaw, m_controls.Pitch, m_controls.Roll};
        if (ImGui::DragFloat3("旋转（°）", rot, 0.25f))
        { m_controls.Yaw = rot[0]; m_controls.Pitch = rot[1]; m_controls.Roll = rot[2]; }
        ImGui::SliderFloat("FOV", &m_controls.FOV, 30.0f, 120.0f, "%.1f°");
        ImGui::SliderFloat("鼠标灵敏度", &m_controls.MouseSensitivity, 0.01f, 0.4f, "%.2f");
        ImGui::SliderFloat("移动速度（单位/s）", &m_controls.MovementSpeed, 1.0f, 10.0f, "%.1f");
        ImGui::EndDisabled();
        ImGui::TextDisabled("WASD 移动 | C/空格 升降 | Q/E 翻滚");
        ImGui::TextDisabled("右键拖动 环顾 | 滚轮 调整视野");
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(gap, gap + 529.0f * leftScale), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftWidth, 285.0f * leftScale), ImGuiCond_Always);
    if (ImGui::Begin("渲染 / Rendering", nullptr, panelFlags))
    {
        ImGui::BeginDisabled(m_sequence.IsPlaying());
        ImGui::Checkbox("距离裁剪", &m_controls.bDistanceCulling);
        ImGui::DragFloat("近裁剪（单位）", &m_controls.NearPlane, 0.01f, 0.01f, 5000.0f);
        ImGui::DragFloat("远裁剪（单位）", &m_controls.FarPlane, 0.1f, 0.02f, 100000.0f);
        ImGui::Checkbox("高斯线框", &m_controls.bWireframe);
        ImGui::SliderFloat("高斯缩放", &m_controls.GaussianScale, 0.01f, 10.0f, "%.2f x");
        ImGui::EndDisabled();
        if (m_controls.bWireframe)
            ImGui::TextDisabled("线框预览采样率: 1/256");
        ImGui::TextDisabled("图块大小: 16 x 16（固定）");
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(rightX, gap), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightWidth, std::min(650.0f, availableHeight)), ImGuiCond_Always);
    if (ImGui::Begin("摄像机序列 / Sequence", nullptr, panelFlags))
    {
        if (ImGui::Button(m_sequenceMode ? "退出序列" : "进入序列"))
        {
            m_sequenceMode = !m_sequenceMode;
            if (!m_sequenceMode && m_sequence.IsPlaying()) m_sequence.Stop();
        }
        if (m_sequenceMode)
        {
            ImGui::SameLine();
            if (ImGui::Button("添加关键帧"))
            {
                Splat::FSplatCameraKeyframe key;
                key.TimeSeconds = m_newKeyframeTime;
                key.State = m_controls;
                std::string error;
                if (!m_sequence.AddOrReplace(key, &error)) m_uiMessage = error;
                else { m_uiMessage = "已添加关键帧"; m_newKeyframeTime += 1.0; }
            }
            ImGui::SameLine();
            if (ImGui::Button("载入示例"))
            {
                std::string error;
                m_uiMessage = m_sequence.LoadJson("resources/3dgs_sequences/bonsai_demo.json", &error)
                    ? "示例已载入：4 个关键帧，时长 6 秒" : error;
                m_selectedKeyframe = -1;
                m_newKeyframeTime = m_sequence.GetDuration() + 1.0;
            }
            float current = static_cast<float>(m_sequence.GetPlaybackTime());
            float duration = static_cast<float>(m_sequence.GetDuration());
            if (ImGui::SliderFloat("时间轴", &current, 0.0f, FMath::Max(duration, 0.01f), "%.2f s"))
            { m_sequence.Seek(current); m_controls = m_sequence.Sample(current); }
            if (ImGui::Button(m_sequence.IsPlaying() ? "暂停" : "播放"))
            {
                if (m_sequence.IsPlaying()) m_sequence.Pause();
                else if (!m_sequence.Play()) m_uiMessage = "至少需要两个关键帧";
            }
            ImGui::SameLine();
            if (ImGui::Button("停止")) { m_sequence.Stop(); m_controls = m_sequence.Sample(0.0); }
            bool looping = m_sequence.IsLooping();
            if (ImGui::Checkbox("循环", &looping)) m_sequence.SetLooping(looping);
            float nextTime = static_cast<float>(m_newKeyframeTime);
            if (ImGui::DragFloat("新关键帧时间", &nextTime, 0.1f, 0.0f, 36000.0f))
                m_newKeyframeTime = nextTime;
            ImGui::Separator();
            const auto& keys = m_sequence.GetKeyframes();
            for (size_t i = 0; i < keys.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                char label[80];
                std::snprintf(label, sizeof(label), "关键帧 %u  @ %.2f s", static_cast<unsigned>(i), keys[i].TimeSeconds);
                if (ImGui::Selectable(label, m_selectedKeyframe == static_cast<int>(i)))
                    m_selectedKeyframe = static_cast<int>(i);
                ImGui::PopID();
            }
            if (m_selectedKeyframe >= 0 && static_cast<size_t>(m_selectedKeyframe) < keys.size())
            {
                auto selected = keys[m_selectedKeyframe];
                float t = static_cast<float>(selected.TimeSeconds);
                if (ImGui::DragFloat("选中时间", &t, 0.1f, 0.0f, 36000.0f))
                {
                    std::string error;
                    if (!m_sequence.SetKeyframeTime(m_selectedKeyframe, t, &error)) m_uiMessage = error;
                    else
                    {
                        selected.TimeSeconds = t;
                        const auto& reordered = m_sequence.GetKeyframes();
                        for (size_t i = 0; i < reordered.size(); ++i)
                            if (reordered[i].TimeSeconds == t) { m_selectedKeyframe = static_cast<int>(i); break; }
                    }
                }
                if (ImGui::Button("预览选中"))
                { m_sequence.Seek(selected.TimeSeconds); m_controls = m_sequence.Sample(selected.TimeSeconds); }
                ImGui::SameLine();
                if (ImGui::Button("覆盖选中"))
                { Splat::FSplatCameraKeyframe key{selected.TimeSeconds, m_controls}; std::string error; if (!m_sequence.AddOrReplace(key, &error)) m_uiMessage = error; }
                ImGui::SameLine();
                if (ImGui::Button("删除选中"))
                { m_sequence.Remove(m_selectedKeyframe); m_selectedKeyframe = -1; }
            }
            ImGui::InputText("序列文件", m_sequencePath, sizeof(m_sequencePath));
            if (ImGui::Button("保存 JSON"))
            { std::string error; m_uiMessage = m_sequence.SaveJson(m_sequencePath, &error) ? "保存成功" : error; }
            ImGui::SameLine();
            if (ImGui::Button("导入 JSON"))
            { std::string error; m_uiMessage = m_sequence.LoadJson(m_sequencePath, &error) ? "导入成功" : error; m_selectedKeyframe = -1; }
            ImGui::Separator();
            ImGui::InputText("视频文件", m_videoPath, sizeof(m_videoPath));
            ImGui::InputText("FFmpeg 程序", m_ffmpegPath, sizeof(m_ffmpegPath));
            ImGui::SliderInt("导出 FPS", &m_exportFps, 24, 60);
            if (!m_videoExporter.isActive())
            {
                if (ImGui::Button("导出 MP4"))
                {
                    if (m_sequence.GetKeyframes().size() < 2)
                        m_uiMessage = "至少需要两个关键帧";
                    else
                    {
                        m_sequence.Pause();
                        m_preExportControls = m_controls;
                        if (!m_videoExporter.start(m_device, m_videoPath, m_renderWidth, m_renderHeight,
                                                   static_cast<uint32>(m_exportFps), m_sequence.GetDuration(),
                                                   m_ffmpegPath))
                            m_uiMessage = m_videoExporter.getLastError();
                        else
                            m_uiMessage = "正在导出视频";
                    }
                }
            }
            else
            {
                ImGui::Text("导出进度: %llu / %llu", static_cast<unsigned long long>(m_videoExporter.getFrameIndex()),
                            static_cast<unsigned long long>(m_videoExporter.getFrameCount()));
                if (ImGui::Button("取消导出"))
                { m_videoExporter.cancel(); m_restoreExportState = true; m_uiMessage = "已取消导出"; }
            }
            if (!m_uiMessage.empty()) ImGui::TextWrapped("%s", m_uiMessage.c_str());
        }
    }
    ImGui::End();
}

bool SplatSceneApplication::loadModel()
{
    std::string error;
    if(m_runtime.shStorageMode>1 || (m_runtime.shStorageMode==1 && (m_runtime.mode!='A'||m_runtime.fullLeaf||m_runtime.projectedFusion))){m_uiMessage="SH FP16 requires full-resident A without LOD/Streaming/fullLeaf/fusion";return false;}
    m_scene = {}; m_leafNodes.clear(); m_residentTargetPages.clear(); m_terminalPageErrors.clear();
    m_selector.Reset(); m_selection = {}; m_selectionCounter = 1;
    if (!m_runtime.scenePath.empty()) {
        if (!Splat::FSplatSceneFormat::LoadManifest(m_runtime.scenePath, m_scene, error)) { m_uiMessage = error; return false; }
        if(m_runtime.projectedFusion&&m_runtime.mode>='C'&&!m_runtime.fullLeaf&&m_scene.version<4){m_uiMessage="Projected fusion requires versioned original membership (manifest v4)";return false;}
        if (m_scene.sourceSHA256 == "64d357cb25bd85f710f8551a18d830f8497277fbd8c5805adfd72ffe9ca78227" && m_runtime.sequencePath.empty())
            m_runtime.sequencePath = "resources/3dgs_sequences/bicycle_lod_mid_far.json";
        for (uint32_t i=0; i<m_scene.nodes.size(); ++i) if (m_scene.nodes[i].children.empty()) m_leafNodes.push_back(i);
        if ((m_runtime.mode == 'B' || m_runtime.fullLeaf) && m_scene.sourceCount > m_runtime.activeCapacity) {
            m_uiMessage = "Full-leaf mode requires activeCapacity >= manifest sourceCount"; return false;
        }
        if (m_runtime.mode == 'B'||m_runtime.fullLeaf) for (auto node:m_leafNodes) for (auto page:m_scene.nodes[node].pageIds) m_scene.pages[page].pinned = true;
        if (m_runtime.mode == 'C') for (auto& page:m_scene.pages) page.pinned = true;
        m_fullLeafRequiredSlots=m_scene.sourceCount;std::vector<bool> leafPages(m_scene.pages.size());for(auto node:m_leafNodes)for(auto page:m_scene.nodes[node].pageIds)leafPages[page]=true;
        for(const auto& page:m_scene.pages)if(page.pinned&&!leafPages[page.id])m_fullLeafRequiredSlots+=page.gaussianCount;
        if (m_runtime.mode != 'A') {
            const uint64_t rootCount = m_scene.nodes.empty()?0:m_scene.nodes[m_scene.rootNode].gaussianCount;
            if (rootCount > m_runtime.activeCapacity) { m_uiMessage = "Mandatory coarse representation exceeds activeCapacity"; return false; }
            uint64_t membershipBytes=0;for(const auto& node:m_scene.nodes)membershipBytes+=node.originalFusionGroups.capacity()*sizeof(Splat::FSplatOriginalFusionGroup);
            // Three immutable manifest owners (app/pool/streamer), plus bounded
            // optional CPU descriptor copies and the temporary source permutation.
            m_fusionCPUReservation=membershipBytes*3;
            if(m_runtime.projectedFusion&&m_runtime.mode>='C'&&!m_runtime.fullLeaf)
                m_fusionCPUReservation+=uint64_t(m_runtime.activeCapacity)*(2*sizeof(Splat::FSplatFusionGroup)+2*sizeof(uint32_t));
            if(m_fusionCPUReservation>=m_runtime.streaming.cpuCacheBytes){m_uiMessage="Original fusion metadata/working reservation exceeds CPU budget";return false;}
            auto streaming=m_runtime.streaming;streaming.cpuCacheBytes-=m_fusionCPUReservation;
            if(m_scene.version>=5&&(!Splat::FSplatPageStreamer::EstimateMinimumProgressBytes(m_scene,streaming,m_cpuProgressMinimumBytes,error)||streaming.cpuCacheBytes<m_cpuProgressMinimumBytes)){m_uiMessage=error.empty()?"CPU budget leaves insufficient worker encode/decode/lease progress workspace":error;return false;}
            m_pagePool = std::make_unique<Splat::FSplatGPUPagePool>();
            if (!m_pagePool->Initialize(m_device,m_scene,streaming,error)) { m_uiMessage=error; return false; }
            m_streamer = std::make_unique<Splat::FSplatPageStreamer>();
            const auto directory=std::filesystem::u8path(m_runtime.scenePath).parent_path();
            if (!m_streamer->Open(m_scene,directory.empty()?".":PathUTF8(directory),streaming,error)) {m_uiMessage=error;return false;}
            m_gpuData=m_pagePool->GetGPUData();m_shDegree=m_gpuData.shDegree;m_loading=true;
            for (const auto& page:m_scene.pages) if (page.pinned) m_residentTargetPages.push_back(page.id);
            if (m_runtime.mode == 'E') {
                const uint64_t fineBytes=m_scene.sourceCount*(52ull+12ull*(m_scene.shDegree+1)*(m_scene.shDegree+1));
                const uint64_t sourceBytes=m_pagePool->GetAllocationStats().sourceReservedBytes;
                if (fineBytes < sourceBytes || fineBytes-sourceBytes < sourceBytes) {m_uiMessage="Restricted E requires fine-level bytes >= twice the effective source allocation";return false;}
            }
            std::vector<Splat::FSplatPageRequest> roots;
            if(!m_scene.nodes.empty())for(auto page:m_scene.nodes[m_scene.rootNode].pageIds)roots.push_back({page,1e12,m_sceneGeneration});
            m_streamer->Request(roots);
            return true;
        }
    }
    if (m_runtime.benchmark && m_runtime.mode == 'A' && m_runtime.sourcePLY.empty()) {
        m_uiMessage="Benchmark A requires an explicit sourcePLY baseline";return false;
    }
    if(m_runtime.benchmark&&m_runtime.mode=='A'&&!m_runtime.scenePath.empty()){
        std::string actualHash;
        if(!Splat::FSplatSceneFormat::SHA256File(m_modelPath,actualHash,error)||actualHash!=m_scene.sourceSHA256){m_uiMessage=error.empty()?"Original baseline SHA256 differs from scene source identity":error;return false;}
    }
    MR_LOG(LogSplatScene, Log, "Loading PLY: %s", m_modelPath.c_str());

    Splat::FSplatPLYLoader loader;
    if (!loader.loadAndUpload(m_device, m_modelPath, m_gpuData,m_runtime.streaming.sceneGPUBytes,m_runtime.streaming.stagingBytes,m_runtime.shStorageMode,m_runtime.shStorageDegree))
    {
        MR_LOG(LogSplatScene, Error, "Failed to load PLY file");
        return false;
    }

    m_shDegree = m_gpuData.shDegree;
    m_initialUploadStagingPeakBytes=loader.GetStagingBackingBytes();
    if(!m_runtime.scenePath.empty()&&m_runtime.mode=='A'&&(m_gpuData.gaussianCount!=m_scene.sourceCount||uint32_t(m_gpuData.sourceSHDegree)!=m_scene.shDegree)){m_uiMessage="Original baseline count/SH degree differs from scene manifest";return false;}
    m_legacyAllocations={};std::set<uint64_t> backing;
    for(auto buffer:{m_gpuData.positions,m_gpuData.scales,m_gpuData.rotations,m_gpuData.opacities,m_gpuData.shCoefficients}){
        RHI::FBufferAllocationInfo info{};
        if(!buffer||!buffer->getAllocationInfo(info)){m_uiMessage="Original source physical allocation accounting unavailable";return false;}
        m_legacyAllocations.sourceReservedBytes+=info.reservationBytes;
        if(backing.insert(info.backingId).second)m_legacyAllocations.sourceBackingBytes+=info.backingBytes;
    }
    if(m_legacyAllocations.sourceBackingBytes>m_runtime.streaming.sceneGPUBytes){m_uiMessage="Original source backing exceeds scene budget";return false;}
    if (m_runtime.benchmark && m_gpuData.gaussianCount > m_runtime.activeCapacity) {
        m_uiMessage="Original baseline requires activeCapacity >= actual PLY count";return false;
    }
    MR_LOG(LogSplatScene, Log, "Loaded %u gaussians, SH degree %d",
           m_gpuData.gaussianCount, m_shDegree);
    return true;
}

void SplatSceneApplication::drawLODOverlay()
{
    if(!m_controlUIVisible||m_runtime.benchmark||(!m_controls.bLODLevelOverlay&&!m_controls.bLODBoundsOverlay))return;
    auto* draw=ImGui::GetForegroundDrawList();const auto& camera=m_lastCamera;
    auto project=[&](const float* position,ImVec2& screen){double v[4]={position[0],position[1],position[2],1},view[4]={},clip[4]={};for(int row=0;row<4;++row)for(int col=0;col<4;++col)view[row]+=camera.viewMatrix[col*4+row]*v[col];for(int row=0;row<4;++row)for(int col=0;col<4;++col)clip[row]+=camera.projMatrix[col*4+row]*view[col];if(clip[3]<=0.00001)return false;const double x=clip[0]/clip[3],y=clip[1]/clip[3];if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>100||std::abs(y)>100)return false;screen=ImVec2(float((x*.5+.5)*m_windowWidth),float((.5-y*.5)*m_windowHeight));return true;};
    for(auto id:m_selection.selectedNodeIds){if(id>=m_scene.nodes.size())continue;const auto& node=m_scene.nodes[id];const ImU32 color=m_controls.bLODLevelOverlay?static_cast<ImU32>(ImColor::HSV(float(node.level%12)/12,0.8f,1.0f)):IM_COL32(255,255,0,180);ImVec2 corners[8];bool valid[8];for(int i=0;i<8;++i){float p[3]={i&1?node.bounds.max[0]:node.bounds.min[0],i&2?node.bounds.max[1]:node.bounds.min[1],i&4?node.bounds.max[2]:node.bounds.min[2]};valid[i]=project(p,corners[i]);}
        for(int i=0;i<8;++i)for(int axis=0;axis<3;++axis){const int other=i^(1<<axis);if(other>i&&valid[i]&&valid[other])draw->AddLine(corners[i],corners[other],color,1);}
        if(m_controls.bLODLevelOverlay){float center[3];for(int a=0;a<3;++a)center[a]=(node.bounds.min[a]+node.bounds.max[a])*.5f;ImVec2 at;if(project(center,at)){char label[32];std::snprintf(label,sizeof(label),"L%u n%u",node.level,id);draw->AddText(at,color,label);}}
    }
}

bool SplatSceneApplication::initSplatPipeline()
{
    if(m_runtime.displaySHDegree>m_shDegree){m_uiMessage="Displayed SH degree exceeds source SH degree";return false;}
    if (!m_gpuData.isValid())
        return false;

    m_splatPipeline = MakeUnique<Splat::FSplatPipeline>();
    if(!m_splatPipeline->SetProjectionColorCompression(m_runtime.projectionColorCompression))return false;
    if(!m_splatPipeline->SetRadixSortBlocksPerWorkgroup(m_runtime.radixSortBlocksPerWorkgroup)||
        !m_splatPipeline->SetRadixPhaseTiming(m_runtime.benchmark&&!m_runtime.captureOnly&&m_runtime.radixSortPhaseTiming))return false;
    if(m_runtime.projectedFusion&&m_runtime.mode>='C'&&!m_runtime.fullLeaf&&!m_splatPipeline->EnableProjectedFusion(m_runtime.fusionImplementation=="split"))return false;
    // A configured original-scene run needs the same capacities as its
    // capture/benchmark run; legacy unconfigured PLY demos keep auto sizing.
    if (m_pagePool || m_runtime.benchmark || !m_runtime.scenePath.empty())
        m_splatPipeline->SetCapacityBudgets(m_gpuData.gaussianCount,m_runtime.activeCapacity,m_runtime.sortCapacity,m_runtime.streaming.temporaryGPUBytes);
    if (!m_splatPipeline->initialize(m_device, m_gpuData.gaussianCount,
                                     m_renderWidth, m_renderHeight))
    {
        MR_LOG(LogSplatScene, Error, "SplatPipeline::initialize failed");
        m_uiMessage=m_splatPipeline->GetFrameStats().degradationReason;
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

    viewInfo.AspectRatio = static_cast<float>(m_renderWidth) /
                           static_cast<float>(m_renderHeight);
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
    m_controls.FOV = 60.0f;
    m_controls.PositionX = static_cast<float>(m_fpsCamera->GetPosition().X);
    m_controls.PositionY = static_cast<float>(m_fpsCamera->GetPosition().Y);
    m_controls.PositionZ = static_cast<float>(m_fpsCamera->GetPosition().Z);
    m_controls.Yaw = m_fpsCamera->GetYaw();
    m_controls.Pitch = m_fpsCamera->GetPitch();
    if (!m_runtime.sequencePath.empty()) {
        std::string error;
        if (!m_sequence.LoadJson(m_runtime.sequencePath,&error) || m_sequence.GetKeyframes().size()<2) {m_uiMessage=error;return false;}
        m_controls=m_sequence.Sample(0);m_controls.LODQualityPixels=m_runtime.lod.qualityPixels;m_controls.bFreezeLOD=m_runtime.lod.freeze;
        applyControlState();m_fpsCamera->Update(0);m_cameraManager->UpdateCamera(0);
    } else if (!m_scene.nodes.empty()) {
        const auto& bounds=m_scene.nodes[m_scene.rootNode].bounds;
        const float size=std::max({bounds.max[0]-bounds.min[0],bounds.max[1]-bounds.min[1],bounds.max[2]-bounds.min[2],1.0f});
        m_controls.PositionX=(bounds.min[0]+bounds.max[0])*0.5f;m_controls.PositionY=(bounds.min[1]+bounds.max[1])*0.5f;
        m_controls.PositionZ=(bounds.min[2]+bounds.max[2])*0.5f+size;m_controls.Yaw=-90;m_controls.Pitch=0;m_controls.FarPlane=std::max(1000.0f,size*10);
        applyControlState();m_fpsCamera->Update(0);m_cameraManager->UpdateCamera(0);
    }

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

    auto vertBytes = MonsterRender::ShaderCompiler::readFileBytes(Splat::SplatShaderPath(
        "Shaders/Splat/compiled/splat_present.vert.spv"));
    auto fragBytes = MonsterRender::ShaderCompiler::readFileBytes(Splat::SplatShaderPath(
        "Shaders/Splat/compiled/splat_present.frag.spv"));

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
        right   = m_fpsCamera->GetRight();
        up      = m_fpsCamera->GetUp();
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
    // World-to-view rotation has rows right, up, -forward. Store those rows
    // in column-major order; translation uses the same basis.
    {
        float32* v = outUniforms.viewMatrix;
        v[0]  = static_cast<float32>(right.X);
        v[1]  = static_cast<float32>(up.X);
        v[2]  = static_cast<float32>(-forward.X);
        v[3]  = 0.0f;
        v[4]  = static_cast<float32>(right.Y);
        v[5]  = static_cast<float32>(up.Y);
        v[6]  = static_cast<float32>(-forward.Y);
        v[7]  = 0.0f;
        v[8]  = static_cast<float32>(right.Z);
        v[9]  = static_cast<float32>(up.Z);
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
        float aspect   = static_cast<float>(m_renderWidth) / static_cast<float>(m_renderHeight);
        float tanHalf  = FMath::Tan(fovYRad * 0.5f);
        float nearVal  = m_controls.NearPlane;
        float farVal   = m_controls.FarPlane;
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
    float aspect = static_cast<float>(m_renderWidth) /
                   static_cast<float>(m_renderHeight);
    float tanFovY = FMath::Tan(fovY * 0.5f);
    float tanFovX = tanFovY * aspect;

    outUniforms.focalX = static_cast<float>(m_renderWidth)  / (2.0f * tanFovX);
    outUniforms.focalY = static_cast<float>(m_renderHeight) / (2.0f * tanFovY);
    outUniforms.tanFovX = tanFovX;
    outUniforms.tanFovY = tanFovY;

    // ---- Image dimensions & SH degree ----
    outUniforms.imageWidth  = static_cast<int32>(m_renderWidth);
    outUniforms.imageHeight = static_cast<int32>(m_renderHeight);
    outUniforms.shDegree = m_runtime.displaySHDegree<0?m_shDegree:std::min(m_runtime.displaySHDegree,m_shDegree);
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
            float nearPlane = 0.1f;  // Matches projection and preprocess clip plane.
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
