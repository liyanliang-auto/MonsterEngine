// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/SplatViewerSession.h"
#include "Renderer/Splat/SplatPLYStream.h"
#include "Core/ShaderCompiler.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanPendingState.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanSampler.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "RHI/IRHICommandList.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
FViewerStatus Good(const char* code="ready") { return {true,code,{}}; }
FViewerStatus Bad(const char* code,const std::string& message) { return {false,code,message}; }
uint64_t Sum(uint64_t a,uint64_t b) { if(a>UINT64_MAX-b)throw std::runtime_error("Budget overflow");return a+b; }
uint64_t Now() {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void CheckCancel(FSplatBuildControl& c) {if(c.Poll()!=ESplatBuildSignal::Continue)throw std::runtime_error("cancelled");}
struct HashState {
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint8_t block[64]{}; size_t used=0; uint64_t total=0;
    static uint32_t R(uint32_t x,int n){return (x>>n)|(x<<(32-n));}
    void Transform() {
        static constexpr uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t(block[4*i])<<24)|(uint32_t(block[4*i+1])<<16)|(uint32_t(block[4*i+2])<<8)|block[4*i+3];
        for(int i=16;i<64;++i)w[i]=w[i-16]+(R(w[i-15],7)^R(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(R(w[i-2],17)^R(w[i-2],19)^(w[i-2]>>10));
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
        for(int i=0;i<64;++i){uint32_t t=z+(R(e,6)^R(e,11)^R(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t u=(R(a,2)^R(a,13)^R(a,22))+((a&b)^(a&c)^(b&c));z=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
    }
    void Add(const uint8_t* p,size_t n){total+=n;while(n){size_t s=std::min(n,64-used);std::memcpy(block+used,p,s);used+=s;p+=s;n-=s;if(used==64){Transform();used=0;}}}
    std::string Finish(){uint64_t bits=total*8;uint8_t one=0x80;Add(&one,1);uint8_t zero=0;while(used!=56)Add(&zero,1);uint8_t tail[8];for(int i=0;i<8;++i)tail[i]=uint8_t(bits>>(56-8*i));Add(tail,8);std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto x:h)s<<std::setw(8)<<x;return s.str();}
};

bool VerifyHash(const std::string& path,const std::string& expected,FSplatBuildControl& control) {
    if(expected.empty())return true; // desktop legacy descriptors have no store identity
    if(expected.size()!=64||expected.find_first_not_of("0123456789abcdef")!=std::string::npos)return false;
    std::ifstream stream(std::filesystem::u8path(path),std::ios::binary);if(!stream)return false;
    HashState hash;std::array<uint8_t,65536> buffer{};
    while(stream){CheckCancel(control);stream.read(reinterpret_cast<char*>(buffer.data()),buffer.size());hash.Add(buffer.data(),size_t(stream.gcount()));}
    CheckCancel(control);return stream.eof()&&hash.Finish()==expected;
}
struct FPrepared {
    FViewerStatus status;
    FSplatSceneManifest manifest;
    std::vector<FSplatGaussian> records;
    uint32_t degree=0;
};
}
struct FSplatViewerSession::FImpl {
    RHI::IRHIDevice* device=nullptr;
    IResourceProvider* provider=nullptr;
    FViewerConfig config;
    FSceneDescriptor scene;
    std::unique_ptr<FSplatBudgetCoordinator> localBudget;
    FSplatBudgetCoordinator* budget=nullptr;
    std::optional<FSplatBudgetCoordinator::FReservation> cpu,source,stagingLease,temporary,metadata;
    std::unique_ptr<FSplatPipeline> pipeline;
    std::unique_ptr<FSplatStreamingController> streaming;
    FSplatGPUData gpu;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> upload;
    std::vector<FSplatGaussian> records;
    std::shared_ptr<FSplatBuildControl> control;
    std::future<FPrepared> preparation;
    FSplatViewerDiagnostics diagnostics;
    std::shared_ptr<const FViewerDiagnosticsSnapshot> snapshot=std::make_shared<FViewerDiagnosticsSnapshot>();
    FViewerStatus status=Bad("not_initialized","");
    FCameraUniforms camera{};
    FSplatRenderSettings settings;
    bool initialized=false,suspended=false,shaderRead=false,committedShaderRead=false,pendingRecording=false,externalCamera=false,explicitCaps=true;
    uint32_t width=1280,height=720,uploaded=0,recordedRows=0,uploadRows=0;
    uint32_t presentationWidth=0,presentationHeight=0,displayWidth=0,displayHeight=0;
    FViewerPresentationScalePolicy presentationScale;
    uint64_t presentationReservationEstimate=0;
    std::chrono::steady_clock::time_point nextMemoryObservation{};
    std::optional<uint64_t> managedLiveBytes,managedBackingBytes,pooledUnusedBytes;
    FViewerFrameMeasurement measurement;
    FViewerCameraProvenance appliedCamera,pendingCamera;
    uint64_t cameraRevision=0;
    bool pendingCameraConsumed=false;
    uint64_t profilingRequestId=0;
    uint64_t pipelineGeneration=0;
    uint64_t highestScene=0,surface=0,streamedFrame=0,recordNanos=0;
    RHI::FGPUCompletionToken uploadToken{},lastSubmission{},baseline{};
    float yaw=0,pitch=0.215f,distance=5.63f,target[3]{};
    std::optional<FViewerOrbitCameraPreset> orbitPreset;
    void ResetOrbit() {
        if(orbitPreset){
            const auto& p=*orbitPreset;float delta[3];
            for(int axis=0;axis<3;++axis){target[axis]=p.target[axis];delta[axis]=p.eye[axis]-p.target[axis];}
            distance=std::hypot(delta[0],delta[1],delta[2]);
            yaw=std::atan2(delta[0],delta[2]);pitch=std::asin(std::clamp(delta[1]/distance,-1.0f,1.0f));
        }else {yaw=0;pitch=0.215f;distance=5.63f;std::fill(std::begin(target),std::end(target),0);}
        externalCamera=false;
    }
    MonsterEngine::TSharedPtr<RHI::IRHIVertexShader> vertex;
    MonsterEngine::TSharedPtr<RHI::IRHIPixelShader> fragment;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> setLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout> presentLayout;
    MonsterEngine::TSharedPtr<RHI::IRHIPipelineState> presentPipeline;
    MonsterEngine::TSharedPtr<RHI::IRHISampler> sampler;
    MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> sets[2];
    std::optional<FSplatBudgetCoordinator::FReservation> Reserve(ESplatBudgetComponent tag,uint64_t bytes) {
        std::string error;auto result=budget->TryReserve(tag,bytes,error);if(!result)throw std::runtime_error(error);return result;
    }
    void Publish(uint64_t frame=0) {
        auto next=std::make_shared<FViewerDiagnosticsSnapshot>();next->frameId=frame;
        next->measurement=measurement;next->measurement.pipelineGeneration=pipelineGeneration;
        std::copy_n(camera.camPos,3,next->measurement.cameraPosition.begin());
        next->measurement.cameraYaw=yaw;next->measurement.cameraPitch=pitch;next->measurement.cameraDistance=distance;next->measurement.externalCamera=externalCamera;
        next->sceneGeneration=scene.generation;next->surfaceGeneration=surface;next->configGeneration=config.generation;
        next->sceneId=scene.sceneId;next->contentHash=scene.contentHash;next->requestedConfig=config;
        next->renderWidth=pipeline?width:0;next->renderHeight=pipeline?height:0;
        next->presentationWidth=presentationWidth;next->presentationHeight=presentationHeight;
        next->displayWidth=displayWidth;next->displayHeight=displayHeight;
        next->presentationDisplayScale=presentationScale.displayScale;
        next->presentationSharpeningStrength=presentationScale.sharpeningStrength;
        next->presentationReservationEstimate=presentationReservationEstimate;
        if(initialized)next->effectiveConfig=config;
        if(auto* vk=dynamic_cast<RHI::Vulkan::VulkanDevice*>(device)) {
            const VkFormat format=vk->getSwapchainFormat();
            next->presentationFormat=static_cast<uint32_t>(format);
            next->presentationColorSpace=static_cast<uint32_t>(vk->getSwapchainColorSpace());
            next->presentationFormatSRGB=format==VK_FORMAT_R8G8B8A8_SRGB||format==VK_FORMAT_B8G8R8A8_SRGB||
                format==VK_FORMAT_R8G8B8_SRGB||format==VK_FORMAT_B8G8R8_SRGB||
                format==VK_FORMAT_R8_SRGB;
            const auto now=std::chrono::steady_clock::now();
            if(initialized&&now>=nextMemoryObservation){
                // One owner-thread manager observation per second, not one lock
                // traversal per frame. These bytes include host-visible backing
                // and exclude opaque WSI/driver allocations; they are not VRAM.
                uint64 used=0,unused=0;vk->getMemoryStats(used,unused);
                managedLiveBytes=used;pooledUnusedBytes=unused;
                managedBackingBytes=unused<=UINT64_MAX-used?std::optional<uint64_t>(used+unused):std::nullopt;
                nextMemoryObservation=now+std::chrono::seconds(1);
            }
        }
        next->vulkanManagedLiveBytes=managedLiveBytes;next->vulkanManagedBackingBytes=managedBackingBytes;
        next->vulkanPooledUnusedBytes=pooledUnusedBytes;
        next->budget=budget?budget->Snapshot():FSplatBudgetSnapshot{};next->status=status;
        if(pipeline){const auto& p=pipeline->GetFrameStats();next->counters.activeGaussians=p.activeCount;
            next->measurement.gpuStatus.fill(p.timingAvailable?0:2);next->measurement.sortStatus=p.countRecordingSkipped?4:0;
            if(p.countAvailable){next->counters.sortEntries=p.actualSortCount;next->counters.sortCountFrame=p.countFrame;}
            // Android's viewer records the complete Gaussian compute/raster workload
            // as the Vulkan timestamp interval named Pipeline. Keep WholeFrame for
            // desktop callers that provide an enclosing scope, and export the
            // measured GPU interval instead of leaving Android timing unavailable.
            for(const auto& sample:p.timestampSamples)if(sample.label=="WholeFrame"||sample.label=="Pipeline") {next->counters.gpuMilliseconds=sample.milliseconds;next->counters.gpuTimingFrame=sample.frame;}
            next->degraded=p.degraded||p.overflow;next->degradationReason=p.overflow?"sort_capacity_overflow_previous_complete_image_retained":p.degradationReason;
            auto& cause=next->pipelineDegradation;cause.status=p.frame==frame?"observed":"stale_identity";
            if(p.degraded)cause.codes.push_back(p.degradationCode.empty()?"unknown_reason":p.degradationCode);
            if(p.overflow&&std::find(cause.codes.begin(),cause.codes.end(),"sort_capacity_overflow_previous_complete_image_retained")==cause.codes.end())
                cause.codes.push_back("sort_capacity_overflow_previous_complete_image_retained");
            cause.counters={{"sourceFrameId",p.frame?std::optional<uint64_t>(p.frame):std::nullopt},
                {"sortCountFrame",p.countAvailable?std::optional<uint64_t>(p.countFrame):std::nullopt},
                {"sortOverflow",p.countAvailable?std::optional<uint64_t>(p.overflow?1:0):std::nullopt},
                {"activeCount",p.activeCount},{"activeCapacity",p.activeCapacity},{"sortCapacity",p.sortCapacity},
                {"activeConsumptionRecorded",p.activeConsumptionRecorded?1:0},{"telemetrySkippedFrames",p.telemetrySkippedFrames}};
            next->resourceCosts["temporary"]={"actual",p.temporaryLogicalBytes,p.temporaryRequiredBytes,p.temporaryBytes,p.temporaryAlignmentBytes,std::nullopt,"temporary_gpu_includes_sort_output"};
            next->resourceCosts["sort"]={"overlapping",p.sortLogicalBytes,p.sortRequiredBytes,p.radixKeysBytes+p.radixValuesBytes+p.radixHistogramBytes,p.sortAlignmentBytes,std::nullopt,"included_in_temporary_gpu"};
            next->resourceCosts["output"]={"overlapping",p.outputLogicalBytes,p.outputRequiredBytes,p.outputImageBytes,p.outputAlignmentBytes,std::nullopt,"included_in_temporary_gpu"};
        }
        if(streaming){const auto s=streaming->Snapshot();next->counters.uploadedBytes=s.pool.uploadedBytes;next->counters.residentBytes=s.pool.residentBytes;next->counters.cpuLeaseBytes=s.cpu.totalReservedBytes;next->degraded|=s.degraded;if(s.degraded)next->degradationReason=s.degradationReason;
            auto& cause=next->streamingDegradation;cause.status=s.frameId==frame&&s.sceneGeneration==scene.generation&&
                s.surfaceGeneration==surface&&s.configGeneration==config.generation&&s.pipelineGeneration==pipelineGeneration?"observed":"stale_identity";
            cause.codes=s.degradationCodes;if(s.degraded&&cause.codes.empty())cause.codes.push_back("unknown_reason");
            cause.counters={{"sourceFrameId",s.frameId},{"memoryPressure",s.memoryPressure?1:0},{"terminalPageFailures",s.terminalPageFailures},
                {"deferredPages",s.deferredPages},{"poolDegraded",s.pool.degraded?1:0},{"rejectedUploads",s.pool.rejectedUploads},
                {"pendingReads",s.io.pendingReads},{"activeReads",s.io.activeReads},{"pendingUploads",s.pool.pendingUploads}};
            const auto& a=s.allocations;
            next->resourceCosts["source"]={"actual",a.sourceLogicalBytes,a.sourceReservedBytes,a.sourceBackingBytes,a.sourceAlignmentBytes,std::nullopt,"source_gpu_disjoint"};
            next->resourceCosts["staging"]={"actual",a.stagingLogicalBytes,a.stagingReservedBytes,a.stagingBackingBytes,a.stagingAlignmentBytes,std::nullopt,"staging_gpu_disjoint"};
            next->resourceCosts["worker"]={"overlapping",s.cpu.inFlightReservedBytes,std::nullopt,std::nullopt,std::nullopt,std::nullopt,"included_in_cpu_cache"};
            next->resourceCosts["transition"]={"overlapping",a.transitionRequiredRecords*(52+12*(gpu.shDegree+1)*(gpu.shDegree+1)),std::nullopt,std::nullopt,std::nullopt,std::nullopt,"protected_union_within_source_gpu"};
        }
        else if(gpu.isValid()){next->counters.residentBytes=gpu.sourceLogicalBytes;next->counters.uploadedBytes=gpu.uploadedBytes;}
        std::shared_ptr<const FViewerDiagnosticsSnapshot> immutable=std::move(next);
        std::atomic_store_explicit(&snapshot,std::move(immutable),std::memory_order_release);
    }
    bool MakePresent() {
        if(presentPipeline)return true;
        auto v=ShaderCompiler::readFileBytes(SplatShaderPath("Shaders/Splat/compiled/splat_present.vert.spv"));
        auto f=ShaderCompiler::readFileBytes(SplatShaderPath("Shaders/Splat/compiled/splat_present.frag.spv"));
        if(v.empty()||f.empty())return false;
        vertex=device->createVertexShader(MonsterEngine::TSpan<const uint8>(v.data(),v.size()));
        fragment=device->createPixelShader(MonsterEngine::TSpan<const uint8>(f.data(),f.size()));if(!vertex||!fragment)return false;
        RHI::FDescriptorSetLayoutDesc ds(0,"SharedSplatPresentSet");ds.bindings.Add(RHI::FDescriptorSetLayoutBinding(0,RHI::EDescriptorType::CombinedTextureSampler,RHI::EShaderStage::Fragment));
        setLayout=device->createDescriptorSetLayout(ds);if(!setLayout)return false;
        RHI::FPipelineLayoutDesc layout("SharedSplatPresentLayout");layout.setLayouts.Add(setLayout);
        layout.pushConstantRanges.Add(RHI::FPushConstantRange(RHI::EShaderStage::Fragment, 0, sizeof(FViewerPresentationPushConstants)));
        presentLayout=device->createPipelineLayout(layout);if(!presentLayout)return false;
        RHI::PipelineStateDesc desc;desc.vertexShader=vertex;desc.pixelShader=fragment;desc.rasterizerState.cullMode=RHI::ECullMode::None;
        desc.pipelineLayout=presentLayout;
        desc.depthStencilState.depthEnable=false;desc.depthStencilState.depthWriteEnable=false;desc.primitiveTopology=RHI::EPrimitiveTopology::TriangleList;desc.renderTargetFormats.Add(device->getSwapChainFormat());
        desc.vkVertexShaderModule=reinterpret_cast<uint64>(static_cast<RHI::Vulkan::VulkanVertexShader*>(vertex.get())->getShaderModule());
        desc.vkPixelShaderModule=reinterpret_cast<uint64>(static_cast<RHI::Vulkan::VulkanPixelShader*>(fragment.get())->getShaderModule());
        presentPipeline=device->createPipelineState(desc);if(!presentPipeline)return false;
        RHI::SamplerDesc sd;sd.filter=RHI::ESamplerFilter::Bilinear;sd.addressU=sd.addressV=sd.addressW=RHI::ESamplerAddressMode::Clamp;sampler=device->createSampler(sd);if(!sampler)return false;
        for(auto& set:sets){set=device->allocateDescriptorSet(setLayout);if(!set)return false;}return true;
    }
    bool MakePipeline(uint32_t w,uint32_t h) {
        if(pipeline){device->waitForIdle();if(auto* command=device->getImmediateCommandList())command->reset();for(auto& set:sets)set.Reset();}
        auto next=std::make_unique<FSplatPipeline>();const auto& r=config.runtime;
        if(!next->SetProjectionColorCompression(r.projectionColorCompression)||!next->SetRadixSortBlocksPerWorkgroup(r.radixSortBlocksPerWorkgroup)||!next->SetRadixPhaseTiming(r.benchmark&&!r.captureOnly&&r.radixSortPhaseTiming))return false;
        if(r.projectedFusion&&r.mode>='C'&&!r.fullLeaf&&!next->EnableProjectedFusion(r.fusionImplementation=="split"))return false;
        if(explicitCaps)next->SetCapacityBudgets(gpu.gaussianCount,r.activeCapacity,r.sortCapacity,r.streaming.temporaryGPUBytes);
        uint64_t bytes=0;if(!next->QueryTemporaryBytes(device,gpu.gaussianCount,w,h,bytes))return false;
        auto lease=Reserve(ESplatBudgetComponent::TemporaryGPU,bytes); // old+new overlap stays charged
        const auto discard=[&] {next.reset();device->waitForIdle();if(auto* vk=dynamic_cast<RHI::Vulkan::VulkanDevice*>(device))vk->DrainCompletedDestructions();};
        if(!next->initialize(device,gpu.gaussianCount,w,h)){discard();return false;}
        if(next->GetFrameStats().temporaryBytes>bytes){discard();return false;}
        next->setGaussianData(gpu);device->waitForIdle();pipeline=std::move(next);++pipelineGeneration;
        if(auto* vk=dynamic_cast<RHI::Vulkan::VulkanDevice*>(device))vk->DrainCompletedDestructions();
        temporary=std::move(lease);width=w;height=h;shaderRead=committedShaderRead=false;return true;
    }
    void Clamp(uint32_t& w,uint32_t& h) {
        const double scale=std::min({1.0,double(config.maximumInternalEdge)/std::max(w,h),std::sqrt(double(config.maximumInternalPixels)/(double(w)*h))});
        w=std::max(1u,uint32_t(w*scale));h=std::max(1u,uint32_t(h*scale));
    }
    void OrbitCamera() {
        const float cp=std::cos(pitch),sp=std::sin(pitch),sy=std::sin(yaw),cy=std::cos(yaw);
        float eye[3]={target[0]+distance*cp*sy,target[1]+distance*sp,target[2]+distance*cp*cy};
        float forward[3]={-cp*sy,-sp,-cp*cy},right[3]={cy,0,-sy},up[3]={-sp*sy,cp,-sp*cy};
        if(orbitPreset&&orbitPreset->rollDegrees!=0){
            const float radians=orbitPreset->rollDegrees*3.14159265358979323846f/180;
            const float cosine=std::cos(radians),sine=std::sin(radians);
            for(int axis=0;axis<3;++axis){const float r=right[axis],u=up[axis];right[axis]=r*cosine-u*sine;up[axis]=u*cosine+r*sine;}
        }
        camera=FSplatViewerSession::BuildCamera(eye,right,up,forward,orbitPreset?orbitPreset->verticalFOVDegrees:45,
            orbitPreset?orbitPreset->nearPlane:settings.nearPlane,orbitPreset?orbitPreset->farPlane:settings.farPlane,
            width,height,config.runtime.displaySHDegree<0?gpu.shDegree:std::min(config.runtime.displaySHDegree,gpu.shDegree));
    }
};
FSplatViewerSession::FSplatViewerSession():Impl(std::make_unique<FImpl>()){}
FSplatViewerSession::~FSplatViewerSession(){Shutdown();}
void FSplatViewerSession::SetBudgetCoordinator(FSplatBudgetCoordinator& b){if(Impl->initialized)throw std::logic_error("Inject budget before initialize");Impl->budget=&b;}
FViewerStatus FSplatViewerSession::Initialize(RHI::IRHIDevice& d,IResourceProvider& p,const FViewerConfig& c) {
    auto& i=*Impl;if(i.initialized)return Bad("already_initialized","");
    if(!c.maximumInternalEdge||!c.maximumInternalPixels||!c.runtime.activeCapacity||!c.runtime.sortCapacity||!d.supportsSubmissionCompletion())return Bad("invalid_config","Invalid extent/capacity or missing completion support");
    try {i.config=c;i.device=&d;i.provider=&p;i.settings.tightTileCulling=c.runtime.tightTileCulling;i.settings.rasterSharedMemoryMode=c.runtime.rasterSharedMemoryMode;
        i.nextMemoryObservation={};i.managedLiveBytes.reset();i.managedBackingBytes.reset();i.pooledUnusedBytes.reset();
        if(!i.budget){uint64_t total=65536;for(uint64_t bytes:{c.runtime.streaming.cpuCacheBytes,c.runtime.streaming.sceneGPUBytes,c.runtime.streaming.stagingBytes,c.runtime.streaming.temporaryGPUBytes})total=Sum(total,bytes);i.localBudget=std::make_unique<FSplatBudgetCoordinator>(total);i.budget=i.localBudget.get();}
#if PLATFORM_ANDROID
        ShaderCompiler::setResourceProvider(std::shared_ptr<IResourceProvider>(&p,[](IResourceProvider*){}));
#endif
        i.metadata=i.Reserve(ESplatBudgetComponent::Metadata,65536+FSplatViewerDiagnostics::LifecycleMetadataBytes+ViewerCameraMetadataBytes);
        i.width=1280;i.height=720;i.Clamp(i.width,i.height);i.initialized=true;i.status=Good();i.Publish();return i.status;
    }catch(const std::exception& e){
#if PLATFORM_ANDROID
        ShaderCompiler::setResourceProvider({});
#endif
        i.metadata.reset();i.device=nullptr;i.provider=nullptr;return i.status=Bad("initialize_failed",e.what());}
}
FViewerStatus FSplatViewerSession::OpenScene(const FSceneDescriptor& scene) {
    auto& i=*Impl;if(!i.initialized)return Bad("not_initialized","");
    if(!scene.generation||scene.generation<=i.highestScene)return Bad("stale_scene","");
    if(scene.initialCamera&&!IsValidOrbitCameraPreset(*scene.initialCamera))return Bad("invalid_camera_preset","");
    ReleaseScene();i.highestScene=scene.generation;i.scene=scene;i.suspended=false;
    if(scene.initialCamera||i.orbitPreset)SetOrbitCameraPreset(scene.initialCamera);
    try {
        i.cpu=i.Reserve(ESplatBudgetComponent::CPUCache,i.config.runtime.streaming.cpuCacheBytes);
        i.control=std::make_shared<FSplatBuildControl>();auto control=i.control;const auto runtime=i.config.runtime;
        i.preparation=std::async(std::launch::async,[scene,runtime,control] {
            FPrepared prepared;try {
                CheckCancel(*control);const auto path=scene.kind==EViewerSceneKind::PLY?scene.sourcePLY:scene.manifestPath;
                if(!VerifyHash(path,scene.contentHash,*control))throw std::runtime_error("Scene content identity mismatch");
                if(scene.kind==EViewerSceneKind::LOD){std::string error;if(!FSplatStreamingController::PrepareScene(scene,runtime,prepared.manifest,error))throw std::runtime_error(error);}
                else {
                    FSplatPLYLimits limits;limits.maxRecords=runtime.activeCapacity;limits.maxFileBytes=runtime.streaming.cpuCacheBytes;
                    FSplatPLYHeader header;std::string error;if(!FSplatPLYStream::Probe(path,header,error,limits,control.get()))throw std::runtime_error(error);
                    const auto work=FSplatPLYStream::ParserWorkingBytes(header,256);
                    if(header.recordCount>(runtime.streaming.cpuCacheBytes-std::min(work,runtime.streaming.cpuCacheBytes))/sizeof(FSplatGaussian))throw std::runtime_error("PLY exceeds bounded native path; build LOD first");
                    prepared.records.reserve(size_t(header.recordCount));prepared.degree=header.shDegree;
                    if(!FSplatPLYStream::StreamRecords(path,header,256,*control,[&](std::span<const FSplatGaussian> rows,const FSplatBuildSourceCursor&,std::string&){prepared.records.insert(prepared.records.end(),rows.begin(),rows.end());return true;},error))throw std::runtime_error(error);
                    if(prepared.records.empty())throw std::runtime_error("Empty Gaussian source");
                }
                CheckCancel(*control);prepared.status=Good("prepared");
            }catch(const std::exception& e){prepared.status=Bad("prepare_failed",e.what());}return prepared;
        });i.status=Good("loading");
    }catch(const std::exception& e){i.status=Bad("open_failed",e.what());}i.Publish();return i.status;
}
FViewerStatus FSplatViewerSession::OpenPreparedLOD(const FSceneDescriptor& scene,FSplatSceneManifest manifest) {
    auto& i=*Impl;if(!i.initialized)return Bad("not_initialized","");
    if(!scene.generation||(scene.generation<=i.highestScene&&scene.generation!=i.scene.generation)||i.streaming||i.pipeline)return Bad("stale_scene","");
    try {
        if(!i.cpu)i.cpu=i.Reserve(ESplatBudgetComponent::CPUCache,i.config.runtime.streaming.cpuCacheBytes);
        i.source=i.Reserve(ESplatBudgetComponent::SourceGPU,i.config.runtime.streaming.sceneGPUBytes);
        i.stagingLease=i.Reserve(ESplatBudgetComponent::StagingGPU,i.config.runtime.streaming.stagingBytes);
        auto controller=std::make_unique<FSplatStreamingController>();std::string error;
        if(!controller->InitializePrepared(i.device,scene,i.config.runtime,std::move(manifest),error))throw std::runtime_error(error);
        i.streaming=std::move(controller);i.scene=scene;i.highestScene=std::max(i.highestScene,scene.generation);i.gpu=i.streaming->GetGPUData();i.explicitCaps=true;
        if(!i.MakePipeline(i.width,i.height))throw std::runtime_error("Pipeline admission/allocation failed");
        i.suspended=false;i.status=Good("loading");
    }catch(const std::exception& e){i.status=Bad("lod_admission_failed",e.what());}i.Publish();return i.status;
}
FViewerStatus FSplatViewerSession::AdoptDesktopGPUData(const FSplatGPUData& data,const FSceneDescriptor& scene,uint32_t w,uint32_t h,bool explicitCaps) {
    auto& i=*Impl;try {
        uint64_t bytes=0;for(auto b:{data.positions,data.scales,data.rotations,data.opacities,data.shCoefficients}){RHI::FBufferAllocationInfo info{};if(!b||!b->getAllocationInfo(info))throw std::runtime_error("Source allocation info unavailable");bytes=Sum(bytes,info.backingBytes);}
        i.source=i.Reserve(ESplatBudgetComponent::SourceGPU,bytes);i.gpu=data;i.scene=scene;i.highestScene=std::max(i.highestScene,scene.generation);i.explicitCaps=explicitCaps;
        if(!i.MakePipeline(w,h))throw std::runtime_error("Desktop pipeline allocation failed");i.status=Good();i.suspended=false;
    }catch(const std::exception& e){i.status=Bad("adopt_failed",e.what());}i.Publish();return i.status;
}
EViewerCameraInputDisposition FSplatViewerSession::ApplyCameraInput(const FCameraInputState& input) {
    auto& i=*Impl;if(input.surfaceGeneration!=i.surface&&i.surface)return EViewerCameraInputDisposition::StaleSurface;
    if(input.phase==EViewerPointerPhase::Cancel){i.appliedCamera.gestureBeginRequestId=0;return EViewerCameraInputDisposition::CancelledNoMotion;}
    if(input.reset)i.ResetOrbit();
    if(!std::isfinite(input.orbitX)||!std::isfinite(input.orbitY)||!std::isfinite(input.panX)||!std::isfinite(input.panY)||!std::isfinite(input.pinchScale)||input.pinchScale<=0){i.appliedCamera={};return EViewerCameraInputDisposition::InvalidInput;}
    i.yaw+=input.orbitX*6.2831853f;i.pitch=std::clamp(i.pitch+input.orbitY*3.1415927f,-1.5f,1.5f);
    i.distance=std::clamp(i.distance/input.pinchScale,0.1f,100000.0f);i.target[0]-=input.panX*i.distance;i.target[1]+=input.panY*i.distance;i.externalCamera=false;
    const bool motion=input.reset||input.orbitX!=0||input.orbitY!=0||input.panX!=0||input.panY!=0||input.pinchScale!=1;
    if(motion&&i.cameraRevision!=UINT64_MAX)++i.cameraRevision;
    auto& p=i.appliedCamera;
    const bool changed=p.requestScope!=input.requestScope||p.sceneGeneration!=i.scene.generation||p.surfaceGeneration!=input.surfaceGeneration||p.configGeneration!=i.config.generation||p.pipelineGeneration!=i.pipelineGeneration;
    if(input.reset||changed){p={};p.lineageComplete=input.lineageComplete;}
    p.requestScope=input.requestScope;p.sceneGeneration=i.scene.generation;p.surfaceGeneration=input.surfaceGeneration;
    p.configGeneration=i.config.generation;p.pipelineGeneration=i.pipelineGeneration;p.cameraRevision=i.cameraRevision;
    p.lastAppliedRequestId=input.requestId;p.applicationGroup=input.applicationGroup;p.phase=input.phase;
    if(input.phase==EViewerPointerPhase::Begin)p.gestureBeginRequestId=input.requestId;
    if(input.reset)p.lastResetRequestId=input.lastResetRequestId;
    const uint32_t members=input.contributorCount?input.contributorCount:1;
    for(uint32_t n=0;n<std::min<uint32_t>(members,ViewerCameraContributorCapacity);++n)
        if(uint64_t(p.contributorCount)+n<ViewerCameraContributorCapacity)p.contributorIds[p.contributorCount+n]=input.contributorCount?input.contributorIds[n]:input.requestId;
    p.contributorCount=uint32_t(std::min<uint64_t>(UINT32_MAX,uint64_t(p.contributorCount)+members));
    p.lineageComplete=p.lineageComplete&&input.lineageComplete&&p.contributorCount<=ViewerCameraContributorCapacity&&i.cameraRevision!=UINT64_MAX;
    if(!input.requestScope||!input.requestId)return EViewerCameraInputDisposition::UnavailableLegacy;
    if(!p.lineageComplete)return EViewerCameraInputDisposition::LineageOverflow;
    return motion?EViewerCameraInputDisposition::AppliedMotion:EViewerCameraInputDisposition::AppliedNoMotion;
}
bool FSplatViewerSession::IsValidOrbitCameraPreset(const FViewerOrbitCameraPreset& p) {
    float delta[3];
    for(int axis=0;axis<3;++axis){
        if(!std::isfinite(p.eye[axis])||!std::isfinite(p.target[axis])||std::abs(p.eye[axis])>1e7f||std::abs(p.target[axis])>1e7f)return false;
        delta[axis]=p.eye[axis]-p.target[axis];
    }
    const float distance=std::hypot(delta[0],delta[1],delta[2]);
    return distance>=0.1f&&distance<=100000&&std::abs(delta[1]/distance)<=std::sin(1.5f)&&
        std::isfinite(p.rollDegrees)&&std::abs(p.rollDegrees)<=180&&
        std::isfinite(p.verticalFOVDegrees)&&p.verticalFOVDegrees>=1&&p.verticalFOVDegrees<=170&&
        std::isfinite(p.nearPlane)&&std::isfinite(p.farPlane)&&p.nearPlane>0&&p.farPlane>p.nearPlane&&p.farPlane<=1e7f;
}
FViewerStatus FSplatViewerSession::SetOrbitCameraPreset(std::optional<FViewerOrbitCameraPreset> preset) {
    if(preset&&!IsValidOrbitCameraPreset(*preset))return Bad("invalid_camera_preset","");
    Impl->orbitPreset=std::move(preset);Impl->ResetOrbit();Impl->appliedCamera={};Impl->pendingCamera={};return Good("camera_preset_applied");
}
void FSplatViewerSession::ApplySettings(const FViewerConfig& c) {
    auto& i=*Impl;if(c.generation<i.config.generation)return;
    // Allocation-time settings are immutable until explicit scene release/re-admission.
    if(c.runtime.activeCapacity!=i.config.runtime.activeCapacity||c.runtime.sortCapacity!=i.config.runtime.sortCapacity||
       c.runtime.streaming.sceneGPUBytes!=i.config.runtime.streaming.sceneGPUBytes||c.runtime.streaming.cpuCacheBytes!=i.config.runtime.streaming.cpuCacheBytes||
       c.runtime.streaming.stagingBytes!=i.config.runtime.streaming.stagingBytes||c.runtime.streaming.temporaryGPUBytes!=i.config.runtime.streaming.temporaryGPUBytes||
       c.runtime.shStorageMode!=i.config.runtime.shStorageMode||c.runtime.projectionColorCompression!=i.config.runtime.projectionColorCompression||
       c.runtime.radixSortBlocksPerWorkgroup!=i.config.runtime.radixSortBlocksPerWorkgroup){i.status=Bad("readmission_required","Capacity/layout changes require scene release");i.Publish();return;}
    i.config=c;if(i.streaming){std::string error;if(!i.streaming->ApplySettings(c.runtime.lod,error))i.status=Bad("settings_failed",error);}i.Publish();
}
FSplatStreamingFrameResult FSplatViewerSession::UpdateStreaming(RHI::IRHICommandList* command,const FSplatLODView& view,uint64_t frame) {
    auto& i=*Impl;if(!i.streaming)return {};i.streaming->SetDiagnosticFrameIdentity(frame,i.profilingRequestId,i.surface,i.config.generation,i.pipelineGeneration);
    auto result=i.streaming->UpdateAndRecord(command,view,frame,i.profilingRequestId);i.streamedFrame=frame;
    if(i.pipeline)i.pipeline->SetActiveSet(result.activeSet);i.status=result.status;return result;
}
void FSplatViewerSession::SetCameraUniforms(const FCameraUniforms& c){Impl->appliedCamera={};Impl->pendingCamera={};Impl->camera=c;Impl->externalCamera=true;if(Impl->pipeline)Impl->pipeline->setCamera(c);}
void FSplatViewerSession::SetRenderSettings(const FSplatRenderSettings& s){Impl->settings=s;if(Impl->pipeline)Impl->pipeline->setRenderSettings(s);}
void FSplatViewerSession::PrepareOutputForCompute(RHI::IRHICommandList& command){auto& i=*Impl;if(i.shaderRead&&i.pipeline){command.transitionResource(i.pipeline->getOutputTexture(),RHI::EResourceUsage::ShaderResource,RHI::EResourceUsage::UnorderedAccess);i.shaderRead=false;}}
FViewerFrameResult FSplatViewerSession::RecordCompute(const FViewerFrameRequest& request,bool frameAlreadyReserved) {
    auto& i=*Impl;FViewerFrameResult result;i.measurement={};
    // Invalidate before every early return, including stale/loading/upload-only calls.
    i.pendingCamera={};i.pendingCameraConsumed=false;
    if(!i.initialized||!request.commandList||!request.viewportWidth||!request.viewportHeight){result.status=Bad("not_drawable","");return result;}
    if(request.sceneGeneration!=i.scene.generation||request.configGeneration!=i.config.generation||
       (i.surface&&request.surfaceGeneration<i.surface)){result.status=Bad("stale_generation","");return result;}
    if(i.suspended&&request.surfaceGeneration<=i.surface){result.status=Bad("suspended","");return result;}
    i.surface=request.surfaceGeneration;i.suspended=false;i.shaderRead=i.committedShaderRead;i.pendingRecording=false;i.recordNanos=request.monotonicNanoseconds;i.baseline=i.device->getLastSubmissionToken();
    try {
        if(!i.pipeline&&!i.gpu.gaussianCount){i.width=request.viewportWidth;i.height=request.viewportHeight;i.Clamp(i.width,i.height);}
        if(i.preparation.valid()&&i.preparation.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            auto prepared=i.preparation.get();if(!prepared.status.ok)throw std::runtime_error(prepared.status.message);
            if(i.scene.kind==EViewerSceneKind::LOD){auto status=OpenPreparedLOD(i.scene,std::move(prepared.manifest));if(!status.ok)throw std::runtime_error(status.message);}
            else {
                i.records=std::move(prepared.records);i.gpu.gaussianCount=uint32_t(i.records.size());i.gpu.shDegree=i.gpu.sourceSHDegree=int32_t(prepared.degree);
                const uint32_t scalars=3*(prepared.degree+1)*(prepared.degree+1);const uint32_t widths[5]={16,16,16,4,scalars*4};
                RHI::BufferDesc descs[6];uint64_t expected[6]{},total=0;
                for(int k=0;k<5;++k){auto& d=descs[k];d.size=i.gpu.gaussianCount*widths[k];d.usage=RHI::EResourceUsage::StorageBuffer|RHI::EResourceUsage::TransferDst;d.dedicatedAllocation=true;
                    uint64_t alignment=0;if(!i.device->getBufferAllocationRequirements(d,expected[k],alignment)||expected[k]<d.size)throw std::runtime_error("Source preflight unavailable");total=Sum(total,expected[k]);}
                if(total>i.config.runtime.streaming.sceneGPUBytes)throw std::runtime_error("Source physical cap exceeded");
                i.uploadRows=uint32_t(std::min<uint64_t>({i.config.runtime.streaming.maxUploadBytesPerFrame,i.config.runtime.streaming.stagingBytes/2,UINT32_MAX})/(52+scalars*4));
                i.uploadRows=std::min(i.uploadRows,i.gpu.gaussianCount);if(!i.uploadRows)throw std::runtime_error("Upload budget cannot hold one row");
                auto& stage=descs[5];stage.size=i.uploadRows*(52+scalars*4);stage.usage=RHI::EResourceUsage::TransferSrc;stage.memoryUsage=RHI::EMemoryUsage::Upload;stage.cpuAccessible=true;stage.dedicatedAllocation=true;
                uint64_t alignment=0;if(!i.device->getBufferAllocationRequirements(stage,expected[5],alignment)||expected[5]>i.config.runtime.streaming.stagingBytes)throw std::runtime_error("Upload backing cap exceeded");
                i.source=i.Reserve(ESplatBudgetComponent::SourceGPU,total);i.stagingLease=i.Reserve(ESplatBudgetComponent::StagingGPU,expected[5]);
                MonsterEngine::TSharedPtr<RHI::IRHIBuffer>* buffers[6]={&i.gpu.positions,&i.gpu.scales,&i.gpu.rotations,&i.gpu.opacities,&i.gpu.shCoefficients,&i.upload};
                for(int k=0;k<6;++k){*buffers[k]=i.device->createBuffer(descs[k]);RHI::FBufferAllocationInfo actual{};if(!*buffers[k]||!(*buffers[k])->getAllocationInfo(actual)||actual.backingBytes!=expected[k]||actual.reservationBytes!=expected[k])throw std::runtime_error("Allocation diverged from admission");}
                i.gpu.sourceLogicalBytes=uint64_t(i.gpu.gaussianCount)*(52+scalars*4);i.gpu.shLogicalBytes=uint64_t(i.gpu.gaussianCount)*scalars*4;
            }
        }
        if(!i.records.empty()) {
            if(i.uploadToken.value&&!i.device->isSubmissionComplete(i.uploadToken)){result.status=Good("upload_pending");return result;}
            i.uploadToken={};
            if(i.uploaded==i.gpu.gaussianCount){
                i.records.clear();i.records.shrink_to_fit();i.upload.Reset();if(auto* vk=dynamic_cast<RHI::Vulkan::VulkanDevice*>(i.device))vk->DrainCompletedDestructions();i.stagingLease.reset();i.cpu.reset();
                if(!i.MakePipeline(i.width,i.height))throw std::runtime_error("Pipeline admission failed");
            }else {
                const uint32_t rows=std::min(i.uploadRows,i.gpu.gaussianCount-i.uploaded),scalars=3*(i.gpu.shDegree+1)*(i.gpu.shDegree+1),widths[5]={16,16,16,4,scalars*4};
                auto* memory=static_cast<uint8_t*>(i.upload->map());if(!memory)throw std::runtime_error("Upload map failed");
                uint32_t offset=0;MonsterEngine::TSharedPtr<RHI::IRHIBuffer> buffers[5]={i.gpu.positions,i.gpu.scales,i.gpu.rotations,i.gpu.opacities,i.gpu.shCoefficients};
                for(int k=0;k<5;++k){for(uint32_t r=0;r<rows;++r){const auto& g=i.records[i.uploaded+r];const void* fields[5]={g.position,g.scale,g.rotation,&g.opacity,g.sh};std::memcpy(memory+offset+r*widths[k],fields[k],widths[k]);}offset+=rows*widths[k];}i.upload->unmap();offset=0;
                for(int k=0;k<5;++k){request.commandList->copyBuffer(buffers[k],i.upload,rows*widths[k],uint64_t(i.uploaded)*widths[k],offset);offset+=rows*widths[k];}
                request.commandList->resourceBarrier();i.recordedRows=rows;i.pendingRecording=true;result.recorded=true;result.status=Good("uploading");i.status=result.status;i.Publish();result.diagnostics=i.snapshot;return result;
            }
        }
        if(!i.pipeline){result.status=i.status.ok?Good("loading"):i.status;return result;}
        const auto frame=frameAlreadyReserved?i.pipeline->GetFrameStats().frame:i.pipeline->ReserveFrameId();
        if(!i.externalCamera)i.OrbitCamera();i.pipeline->setCamera(i.camera);
        // Freeze the actual uniform consumer before later Publish/live camera mutations.
        i.pendingCamera=i.appliedCamera;auto& consumed=i.pendingCamera;
        consumed.frameId=frame;consumed.sceneGeneration=i.scene.generation;consumed.surfaceGeneration=i.surface;
        consumed.configGeneration=i.config.generation;consumed.pipelineGeneration=i.pipelineGeneration;
        consumed.lineageComplete=consumed.lineageComplete&&i.appliedCamera.sceneGeneration==i.scene.generation&&
            i.appliedCamera.surfaceGeneration==i.surface&&i.appliedCamera.configGeneration==i.config.generation&&i.appliedCamera.pipelineGeneration==i.pipelineGeneration;
        const bool sameCameraScope=i.appliedCamera.sceneGeneration==i.scene.generation&&i.appliedCamera.surfaceGeneration==i.surface&&
            i.appliedCamera.configGeneration==i.config.generation&&i.appliedCamera.pipelineGeneration==i.pipelineGeneration;
        consumed.unavailable=!consumed.requestScope?EViewerCameraProvenanceUnavailable::Legacy:
            !sameCameraScope?EViewerCameraProvenanceUnavailable::ScopeChange:EViewerCameraProvenanceUnavailable::LineageOverflow;
        std::memcpy(consumed.uniforms.data(),&i.camera,sizeof(i.camera));
        // Canonicalize only unused evidence bytes; never modify GPU uniforms.
        std::fill_n(consumed.uniforms.begin()+140,4,0);std::fill_n(consumed.uniforms.begin()+172,4,0);
        std::copy_n(i.target,3,consumed.target.begin());std::copy_n(i.camera.camPos,3,consumed.position.begin());
        consumed.yaw=i.yaw;consumed.pitch=i.pitch;consumed.distance=i.distance;consumed.externalCamera=i.externalCamera;
        auto renderSettings=i.settings;
        if(i.orbitPreset){renderSettings.nearPlane=i.orbitPreset->nearPlane;renderSettings.farPlane=i.orbitPreset->farPlane;}
        i.pipeline->setRenderSettings(renderSettings);
        if(i.streaming&&i.streamedFrame!=frame){FSplatLODView view;std::copy_n(i.camera.viewMatrix,16,view.view);std::copy_n(i.camera.projMatrix,16,view.projection);std::copy_n(i.camera.camPos,3,view.cameraPosition);view.width=i.width;view.height=i.height;view.nearPlane=i.orbitPreset?i.orbitPreset->nearPlane:i.settings.nearPlane;view.farPlane=i.orbitPreset?i.orbitPreset->farPlane:i.settings.farPlane;view.gaussianScale=i.settings.gaussianScale;UpdateStreaming(request.commandList,view,frame);}
        PrepareOutputForCompute(*request.commandList);result.outputTexture=i.pipeline->execute(request.commandList);i.diagnostics.Resolve(i.pipelineGeneration,i.pipeline->GetFrameStats());result.recorded=bool(result.outputTexture);i.pendingRecording=result.recorded;result.pipelineFrameId=frame;
        i.pendingCameraConsumed=result.recorded&&i.pipeline->GetFrameStats().frame==frame&&i.pipeline->GetFrameStats().activeConsumptionRecorded;
        i.pendingCamera.recorded=i.pendingCameraConsumed;
        i.status=result.outputTexture?Good():Bad("compute_failed",i.pipeline->GetFrameStats().degradationReason);i.Publish(frame);result.counters=i.snapshot->counters;result.diagnostics=i.snapshot;result.status=i.status;
    }catch(const std::exception& e){i.pendingCamera={};i.pendingCameraConsumed=false;i.status=Bad("record_failed",e.what());i.Publish();result.status=i.status;result.diagnostics=i.snapshot;}return result;
}
FViewerStatus FSplatViewerSession::RecordPresent(RHI::IRHICommandList& command,MonsterEngine::TSharedPtr<RHI::IRHITexture> output,uint32_t w,uint32_t h) {
    auto& i=*Impl;if(!output||!w||!h)return Bad("not_drawable","");if(!i.MakePresent())return Bad("present_pipeline_failed","");
    command.transitionResource(output,RHI::EResourceUsage::UnorderedAccess,RHI::EResourceUsage::ShaderResource);i.shaderRead=true;
    MonsterEngine::TArray<MonsterEngine::TSharedPtr<RHI::IRHITexture>> targets;command.setRenderTargets(MonsterEngine::TSpan<MonsterEngine::TSharedPtr<RHI::IRHITexture>>(targets),nullptr);
    RHI::Viewport viewport;viewport.width=float(w);viewport.height=float(h);viewport.minDepth=0;viewport.maxDepth=1;command.setViewport(viewport);
    RHI::ScissorRect scissor;scissor.left=scissor.top=0;scissor.right=int32_t(w);scissor.bottom=int32_t(h);command.setScissorRect(scissor);command.setPipelineState(i.presentPipeline);
    auto* vk=dynamic_cast<RHI::Vulkan::VulkanDevice*>(i.device);if(!vk)return Bad("backend_unavailable","");
    auto* pending=vk->getCommandListContext()->getPendingState();auto* texture=static_cast<RHI::Vulkan::VulkanTexture*>(output.get());auto* sampler=static_cast<RHI::Vulkan::VulkanSampler*>(i.sampler.get());
    if(pending)pending->setTexture(0,0,texture->getImageView(),sampler->getSampler(),texture->getImage(),texture->getVulkanFormat());
    auto& set=i.sets[vk->getCurrentFrame()%2];if(!set)set=i.device->allocateDescriptorSet(i.setLayout);if(!set)return Bad("present_descriptor_failed","");set->updateCombinedTextureSampler(0,output,i.sampler);command.bindDescriptorSet(i.presentLayout,0,set);
    const VkFormat format=vk->getSwapchainFormat();
    const bool srgb=format==VK_FORMAT_R8G8B8A8_SRGB||format==VK_FORMAT_B8G8R8A8_SRGB||format==VK_FORMAT_R8G8B8_SRGB||format==VK_FORMAT_B8G8R8_SRGB||format==VK_FORMAT_R8_SRGB;
    i.presentationWidth=w;i.presentationHeight=h;
    i.presentationScale=EvaluatePresentationScale(output->getWidth(),output->getHeight(),w,h,i.displayWidth,i.displayHeight);
    const FViewerPresentationPushConstants transfer{srgb?1u:0u,i.presentationScale.sharpeningStrength};
    command.pushConstants(i.presentLayout,RHI::EShaderStage::Fragment,0,sizeof(transfer),&transfer);
    command.draw(3,0);return Good();
}
void FSplatViewerSession::SetProfilingRequestId(uint64_t requestId){Impl->profilingRequestId=requestId;}
void FSplatViewerSession::SetPresentationReservationEstimate(uint64_t bytes){Impl->presentationReservationEstimate=bytes;}
FViewerFrameResult FSplatViewerSession::TickAndRecord(const FViewerFrameRequest& request) {
    const auto start=Now();auto result=RecordCompute(request);if(result.outputTexture){auto status=RecordPresent(*request.commandList,result.outputTexture,request.viewportWidth,request.viewportHeight);if(!status.ok)result.status=status;}Impl->measurement.sessionMilliseconds=double(Now()-start)/1000000.0;return result;
}
void FSplatViewerSession::OnSubmitted(RHI::FGPUCompletionToken token,const FViewerFrameMeasurement* owner) {
    auto& i=*Impl;if(!i.pendingRecording||!token.value||token.value==i.lastSubmission.value||token.value==i.baseline.value){i.pendingCamera={};i.pendingCameraConsumed=false;return;}
    const bool freshCameraToken=token.value>i.lastSubmission.value&&token.value>i.baseline.value;
    // Tokens are accepted only if observed on this device, never synthesized frame IDs.
    if(i.device->getLastSubmissionToken().value!=token.value){i.pendingCamera={};i.pendingCameraConsumed=false;return;}i.lastSubmission=token;i.pendingRecording=false;i.committedShaderRead=i.shaderRead;
    const bool uploadOnly=i.recordedRows!=0;
    if(i.recordedRows){i.uploaded+=i.recordedRows;i.gpu.uploadedBytes=uint64_t(i.uploaded)*(52+3*(i.gpu.shDegree+1)*(i.gpu.shDegree+1)*4);i.recordedRows=0;i.uploadToken=token;}
    // This path is the only source of successful consumed-cut fingerprints; a recording alone cannot bind quality.
    if(i.pipeline)i.pipeline->MarkFrameSubmitted(token);if(i.streaming)i.streaming->OnSubmitted(token,i.pipeline&&i.pipeline->GetFrameStats().activeConsumptionRecorded);
    if(owner){i.measurement.ownerStartNanoseconds=owner->ownerStartNanoseconds;i.measurement.presentReturnNanoseconds=owner->presentReturnNanoseconds;
        i.measurement.acquireMilliseconds=owner->acquireMilliseconds;i.measurement.presentMilliseconds=owner->presentMilliseconds;i.measurement.ownerWorkMilliseconds=owner->ownerWorkMilliseconds;}
    i.diagnostics.OnScreenshotSubmitted(token);i.Publish(i.pipeline?i.pipeline->GetFrameStats().frame:0);
    auto submitted=*i.snapshot;
    if(uploadOnly)submitted.cameraProvenance.unavailable=EViewerCameraProvenanceUnavailable::UploadOnly;
    // Success belongs only to this immutable original compute row, never current Publish.
    const auto& camera=i.pendingCamera;
    const bool cameraSubmitted=freshCameraToken&&i.pendingCameraConsumed&&camera.recorded&&camera.frameId&&i.pipeline&&
        camera.frameId==submitted.frameId&&camera.sceneGeneration==submitted.sceneGeneration&&
        camera.surfaceGeneration==submitted.surfaceGeneration&&camera.configGeneration==submitted.configGeneration&&
        camera.pipelineGeneration==submitted.measurement.pipelineGeneration&&i.pipeline->GetFrameStats().activeConsumptionRecorded;
    if(cameraSubmitted) {
        submitted.cameraProvenance=camera;submitted.cameraProvenance.submissionToken=token.value;
        submitted.measurement.cameraPosition=camera.position;submitted.measurement.cameraYaw=camera.yaw;
        submitted.measurement.cameraPitch=camera.pitch;submitted.measurement.cameraDistance=camera.distance;submitted.measurement.externalCamera=camera.externalCamera;
        // A later owner input must remain pending even if a split-phase caller applies it before submission.
        if(i.appliedCamera.requestScope==camera.requestScope&&i.appliedCamera.lastAppliedRequestId==camera.lastAppliedRequestId&&
            i.appliedCamera.applicationGroup==camera.applicationGroup) {
            i.appliedCamera.contributorIds={};i.appliedCamera.contributorCount=0;
        }
    }
    i.pendingCamera={};i.pendingCameraConsumed=false;
    const auto stream=i.streaming?std::optional<FSplatStreamingControllerSnapshot>(i.streaming->Snapshot()):std::nullopt;i.diagnostics.Record(submitted,stream?&*stream:nullptr,token,owner?owner->presentReturnNanoseconds:Now());
    i.diagnostics.PollScreenshot();
}
FViewerStatus FSplatViewerSession::ResizeInternal(uint32_t w,uint32_t h) {
    auto& i=*Impl;if(!w||!h)return Bad("not_drawable","");i.suspended=false;i.Clamp(w,h);if(w==i.width&&h==i.height)return Good();
    try {if(i.pipeline&&!i.MakePipeline(w,h))return Bad("resize_failed","Old output retained");i.width=w;i.height=h;i.status=Good();}catch(const std::exception& e){i.status=Bad("resize_failed",e.what());}i.Publish();return i.status;
}
FViewerPresentationScalePolicy FSplatViewerSession::EvaluatePresentationScale(uint32_t rw,uint32_t rh,
    uint32_t pw,uint32_t ph,uint32_t dw,uint32_t dh) {
    FViewerPresentationScalePolicy policy;
    // The Surface boundary uses the same limit. Invalid or unavailable metadata
    // must never turn on compensation, and this calculation never allocates.
    for(uint32_t extent:{rw,rh,pw,ph,dw,dh})if(!extent||extent>16384)return policy;
    policy.displayScale=std::max({1.0f,float(dw)/float(pw),float(dh)/float(ph)});
    // Existing native Catmull-Rom/upscale contrast recovery remains authoritative;
    // do not stack another sharpening pass when native presentation already scales.
    if(pw>rw+1||ph>rh+1)return policy;
    policy.sharpeningStrength=0.22f*std::clamp(2.0f*(policy.displayScale-1.0f),0.0f,1.0f);
    return policy;
}
void FSplatViewerSession::SetPresentationDisplayExtent(uint32_t w,uint32_t h) {
    auto& i=*Impl;
    if(!w||!h||w>16384||h>16384)w=h=0;
    i.displayWidth=w;i.displayHeight=h;i.presentationWidth=i.presentationHeight=0;
    i.presentationScale={};i.Publish();
}
void FSplatViewerSession::Suspend(){Impl->pendingCamera={};Impl->pendingCameraConsumed=false;Impl->suspended=true;Impl->diagnostics.ResetCadence();Impl->measurement={};}
void FSplatViewerSession::ReleaseScene() {
    if(!Impl)return;auto& i=*Impl;if(i.control)i.control->RequestCancel();if(i.preparation.valid()){i.preparation.wait();i.preparation.get();}i.control.reset();
    if(i.device){i.device->waitForIdle();if(auto* command=i.device->getImmediateCommandList())command->reset();for(auto& set:i.sets)set.Reset();i.diagnostics.Shutdown();}if(i.streaming)i.streaming->Shutdown();i.streaming.reset();i.pipeline.reset();i.gpu.release();i.upload.Reset();
    i.records.clear();i.records.shrink_to_fit();if(auto* vk=dynamic_cast<RHI::Vulkan::VulkanDevice*>(i.device))vk->DrainCompletedDestructions();
    i.cpu.reset();i.source.reset();i.stagingLease.reset();i.temporary.reset();i.appliedCamera={};i.pendingCamera={};i.pendingCameraConsumed=false;i.scene={};i.uploaded=i.recordedRows=0;i.uploadToken={};i.shaderRead=i.committedShaderRead=i.pendingRecording=false;i.streamedFrame=0;i.status=Good("scene_released");i.Publish();
}
void FSplatViewerSession::Shutdown() {
    if(!Impl||!Impl->initialized)return;ReleaseScene();auto& i=*Impl;i.device->waitForIdle();for(auto& set:i.sets)set.Reset();i.sampler.Reset();i.presentPipeline.Reset();i.presentLayout.Reset();i.setLayout.Reset();i.vertex.Reset();i.fragment.Reset();
    if(auto* vk=dynamic_cast<RHI::Vulkan::VulkanDevice*>(i.device))vk->DrainCompletedDestructions();i.metadata.reset();i.initialized=false;i.device=nullptr;i.provider=nullptr;
    i.managedLiveBytes.reset();i.managedBackingBytes.reset();i.pooledUnusedBytes.reset();i.nextMemoryObservation={};
    SetPresentationDisplayExtent(0,0);
#if PLATFORM_ANDROID
    ShaderCompiler::setResourceProvider({});
#endif
}
void FSplatViewerSession::OnMemoryPressure(){if(Impl->streaming)Impl->streaming->OnMemoryPressure();Impl->Publish();}
std::shared_ptr<const FViewerDiagnosticsSnapshot> FSplatViewerSession::Snapshot()const{return std::atomic_load_explicit(&Impl->snapshot,std::memory_order_acquire);}
FSplatViewerDiagnostics& FSplatViewerSession::Diagnostics(){return Impl->diagnostics;}
FSplatBudgetCoordinator& FSplatViewerSession::Budget(){return *Impl->budget;}
FSplatPipeline* FSplatViewerSession::Pipeline()const{return Impl->pipeline.get();}
FSplatStreamingController* FSplatViewerSession::Streaming()const{return Impl->streaming.get();}
const FSplatGPUData& FSplatViewerSession::GPUData()const{return Impl->gpu;}
bool FSplatViewerSession::BuildCalibratedCamera(const FCalibratedCameraDesc& desc,FCameraUniforms& output,std::string& error) {
    error.clear();
    const auto fail=[&error](const char* reason){error=reason;return false;};
    if(!desc.width||!desc.height||desc.width>16384||desc.height>16384)
        return fail("Calibrated camera dimensions must be in [1,16384]");
    if(desc.shDegree<0||desc.shDegree>3)return fail("Calibrated camera SH degree must be in [0,3]");
    if(!std::isfinite(desc.fx)||!std::isfinite(desc.fy)||desc.fx<=0||desc.fy<=0||
       !std::isfinite(desc.cx)||!std::isfinite(desc.cy))return fail("Invalid calibrated camera intrinsics");
    if(!std::isfinite(desc.nearPlane)||!std::isfinite(desc.farPlane)||desc.nearPlane<=0||desc.farPlane<=desc.nearPlane)
        return fail("Invalid calibrated camera clip planes");
    const auto* view=desc.viewMatrix;
    for(int n=0;n<16;++n)if(!std::isfinite(view[n]))return fail("Nonfinite calibrated camera pose");
    if(view[3]!=0||view[7]!=0||view[11]!=0||view[15]!=1)
        return fail("Calibrated camera pose must have affine bottom row [0,0,0,1]");
    constexpr double rigidTolerance=1e-4;
    for(int a=0;a<3;++a)for(int b=a;b<3;++b){
        double dot=0;
        for(int row=0;row<3;++row)dot+=double(view[4*a+row])*view[4*b+row];
        if(std::abs(dot-(a==b?1.0:0.0))>rigidTolerance)return fail("Calibrated camera rotation is not orthonormal");
    }
    const double determinant=double(view[0])*(double(view[5])*view[10]-double(view[9])*view[6])-
        double(view[4])*(double(view[1])*view[10]-double(view[9])*view[2])+
        double(view[8])*(double(view[1])*view[6]-double(view[5])*view[2]);
    if(std::abs(determinant-1.0)>rigidTolerance)return fail("Calibrated camera rotation must have determinant +1");

    // Check before casting: conversion outside the finite float range is not portable.
    // Nonzero subnormals are rejected because the GPU may flush them to zero.
    const auto store=[](double value,float& destination){
        if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max()||
           (value!=0&&std::abs(value)<std::numeric_limits<float>::min()))return false;
        destination=static_cast<float>(value);return true;
    };
    FCameraUniforms out{};
    std::copy_n(view,16,out.viewMatrix);
    for(int axis=0;axis<3;++axis){
        double eye=0;
        for(int row=0;row<3;++row)eye-=double(view[4*axis+row])*view[12+row];
        if(!store(eye,out.camPos[axis]))return fail("Calibrated camera position is not representable as float");
    }
    out.camPos[3]=1;
    const double width=desc.width,height=desc.height,nearPlane=desc.nearPlane,farPlane=desc.farPlane;
    // For depth d=-z, desired pixels are u=fx*x/d+cx and top-down v=cy-fy*y/d.
    // ndc2Pix(s,N)=((s+1)*N-1)/2, and raw row y=(H-1)-v. Hence
    // P02=1-(2*cx+1)/W, P12=(2*cy+1)/H-1. The half-pixel terms are required.
    if(!store(desc.fx,out.focalX)||!store(desc.fy,out.focalY)||
       !store(width/(2*desc.fx),out.tanFovX)||!store(height/(2*desc.fy),out.tanFovY)||
       !store(2*desc.fx/width,out.projMatrix[0])||!store(2*desc.fy/height,out.projMatrix[5])||
       !store(1-(2*desc.cx+1)/width,out.projMatrix[8])||
       !store((2*desc.cy+1)/height-1,out.projMatrix[9])||
       !store(farPlane/(nearPlane-farPlane),out.projMatrix[10])||
       !store(-(farPlane*nearPlane)/(farPlane-nearPlane),out.projMatrix[14]))
        return fail("Calibrated camera derived uniforms are not representable as finite normal floats");
    // Independently rounded focal/projection values must still describe one camera
    // to subpixel precision; reject extreme finite intrinsics that cannot do so.
    if(std::abs(double(out.focalX)-desc.fx)>0.25||std::abs(double(out.focalY)-desc.fy)>0.25||
       std::abs(double(out.projMatrix[0])*width/2-desc.fx)>0.25||
       std::abs(double(out.projMatrix[5])*height/2-desc.fy)>0.25||
       std::abs((1-double(out.projMatrix[8]))*width/2-0.5-desc.cx)>0.25||
       std::abs((1+double(out.projMatrix[9]))*height/2-0.5-desc.cy)>0.25)
        return fail("Calibrated intrinsics lose more than 0.25 pixel precision in the GPU uniforms");
    out.projMatrix[11]=-1;out.imageWidth=int32_t(desc.width);out.imageHeight=int32_t(desc.height);out.shDegree=desc.shDegree;
    output=out;return true;
}
FCameraUniforms FSplatViewerSession::BuildCamera(const float eye[3],const float right[3],const float up[3],const float forward[3],float fov,float nearPlane,float farPlane,uint32_t w,uint32_t h,int32_t degree) {
    FCameraUniforms out{};if(!w||!h||!std::isfinite(fov)||fov<=0||fov>=179||nearPlane<=0||farPlane<=nearPlane)return out;
    for(int axis=0;axis<3;++axis){out.viewMatrix[axis*4]=right[axis];out.viewMatrix[axis*4+1]=up[axis];out.viewMatrix[axis*4+2]=-forward[axis];out.viewMatrix[12]-=right[axis]*eye[axis];out.viewMatrix[13]-=up[axis]*eye[axis];out.viewMatrix[14]+=forward[axis]*eye[axis];out.camPos[axis]=eye[axis];}
    out.viewMatrix[15]=out.camPos[3]=1;out.tanFovY=std::tan(fov*3.14159265358979323846f/360);out.tanFovX=out.tanFovY*float(w)/float(h);
    out.projMatrix[0]=1/out.tanFovX;out.projMatrix[5]=1/out.tanFovY;out.projMatrix[10]=farPlane/(nearPlane-farPlane);out.projMatrix[11]=-1;out.projMatrix[14]=-(farPlane*nearPlane)/(farPlane-nearPlane);
    out.focalX=w/(2*out.tanFovX);out.focalY=h/(2*out.tanFovY);out.imageWidth=int32_t(w);out.imageHeight=int32_t(h);out.shDegree=degree;return out;
}
} // namespace MonsterRender::Splat
