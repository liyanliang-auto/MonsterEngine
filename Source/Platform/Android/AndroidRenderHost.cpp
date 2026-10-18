// Copyright Monster Engine. All Rights Reserved.
#include "Platform/Android/AndroidRenderHost.h"
#include "Core/RuntimeLease.h"
#include "Core/Logging/OutputDeviceDebug.h"
#include "Core/Logging/OutputDeviceRedirector.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#if PLATFORM_ANDROID
#include <android/native_window.h>
#endif

namespace MonsterRender::Splat {
namespace {
using namespace RHI;
using namespace RHI::Vulkan;
FViewerStatus OK(const char* code="queued") { return {true,code,{}}; }
FViewerStatus Fail(const char* code,const std::string& detail={}) { return {false,code,detail}; }
std::mutex RegistryMutex;
std::map<uint64_t,std::shared_ptr<FAndroidRenderHost>> Registry;
uint64_t NextHandle=1;
constexpr size_t MaximumControls=64,MaximumWindows=4;
#if PLATFORM_ANDROID
// Keep the platform log sink alive through owner teardown; no scene data is exported here.
class FAndroidOwnerLog {
    FOutputDeviceDebug output;
    std::chrono::steady_clock::time_point nextFlush{};
public:
    FAndroidOwnerLog(){GLog->AddOutputDevice(&output);}
    ~FAndroidOwnerLog(){GLog->Flush();GLog->RemoveOutputDevice(&output);}
    void Poll(){auto now=std::chrono::steady_clock::now();if(now>=nextFlush){GLog->Flush();nextFlush=now+std::chrono::seconds(1);}}
};
#endif
int PressureRank(int32_t level){return level>=80||level==15?3:level>=40?2:level==5||level==10?1:0;}
void RetainWindow(ANativeWindow* window) {
#if PLATFORM_ANDROID
    ANativeWindow_acquire(window);
#else
    (void)window;
#endif
}
void ReleaseWindow(ANativeWindow* window) {
#if PLATFORM_ANDROID
    ANativeWindow_release(window);
#else
    (void)window;
#endif
}
struct FWindow {
    ANativeWindow* window=nullptr;
    uint64_t generation=0;
    uint32_t width=0,height=0;
    std::atomic<bool> valid{true},inUse{false};
    bool attached=false,detachRequested=false,retired=false;
    ~FWindow(){if(window)ReleaseWindow(window);}
};
struct FAck {bool done=false;FViewerStatus status;};
enum class EControl { Scene,Settings,Input,Pressure,Thermal,Release,Retry };
struct FControl {
    EControl kind=EControl::Retry;
    FSceneDescriptor scene;
    std::shared_ptr<void> lease;
    FViewerConfig config;
    FCameraInputState input;
    int32_t value=0;
    std::shared_ptr<FAck> ack;
};
struct FReadback {
    MonsterEngine::TSharedPtr<IRHIBuffer> buffer;
    std::optional<FSplatBudgetCoordinator::FReservation> lease;
    FGPUCompletionToken token{};
    FAndroidExecutionObservation observation;
    bool graphics=false;
    uint32_t samples=0;
};
}
struct FAndroidRenderHost::FImpl {
    // Declare the runtime pin first so it retires after every dependent member.
    FProcessRuntimeLease runtime;
    FAndroidRenderHostCreateInfo info;
    std::optional<FSplatBudgetCoordinator::FReservation> metadataLease,deviceLease;
    std::thread owner;
    mutable std::mutex mutex;
    std::condition_variable changed;
    bool quit=false,stopped=false,resumeRequested=false,lifecycleDirty=false;
    bool screenshotRequested=false,retryPending=false;
    uint64_t highestWindow=0;
    uint64_t highestSceneRequest=0;
    std::map<uint64_t,std::shared_ptr<FWindow>> windows;
    std::deque<FControl> controls;
    std::optional<std::pair<uint64_t,EAndroidFrameScheduler>> frame;
    std::shared_ptr<const FAndroidRenderHostSnapshot> snapshot=std::make_shared<FAndroidRenderHostSnapshot>();
    std::optional<FViewerScreenshot> screenshot;
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<FSplatViewerSession> session;
    std::shared_ptr<FWindow> current;
    FSceneDescriptor scene;
    std::shared_ptr<void> sceneLease;
    uint64_t sceneSequence=0;
    FAndroidCapabilityReport capabilities;
    FAndroidPerformancePolicy performance;
    FViewerStatus status=OK("idle");
    bool resumed=false,manualRetry=false,autoRetryUsed=false,pressureRetired=false;
    bool graphicsObserved=false,splatObserved=false;
    std::optional<FReadback> readback;
    std::vector<FReadback> retiredReadbacks;
    uint32_t physicalIndex=0;
    bool physicalMatched=false;
    uint64_t smokeSequence=0;
#if PLATFORM_ANDROID
    std::unique_ptr<FAndroidOwnerLog> ownerLog;
#endif
    explicit FImpl(FAndroidRenderHostCreateInfo value):runtime(FProcessRuntimeLease::Acquire()),info(std::move(value)){}
    void Publish() {
#if PLATFORM_ANDROID
        if(ownerLog)ownerLog->Poll();
#endif
        auto value=std::make_shared<FAndroidRenderHostSnapshot>();value->status=status;
        value->viewer=session?session->Snapshot():nullptr;value->capabilities=capabilities;
        value->performance=performance.Snapshot();value->budget=info.budget->Snapshot();
        value->surfaceGeneration=current?current->generation:0;value->resumed=resumed;
        value->windowInUse=current&&current->inUse.load(std::memory_order_acquire);
        value->manualRetryRequired=manualRetry;value->pressureRetired=pressureRetired;
        value->presentationReservationEstimate=device?device->GetPresentationReservationEstimate():0;
        if(session){value->diagnosticsJSON=session->Diagnostics().SnapshotJSON();value->framesCSV=session->Diagnostics().FramesCSV();}
        std::shared_ptr<const FAndroidRenderHostSnapshot> immutable=std::move(value);
        std::atomic_store_explicit(&snapshot,std::move(immutable),std::memory_order_release);
    }
    void Ack(const std::shared_ptr<FAck>& ack,FViewerStatus result) {
        if(!ack)return;std::lock_guard lock(mutex);ack->status=std::move(result);ack->done=true;changed.notify_all();
    }
    FViewerStatus Queue(FControl value,bool coalesce=false) {
        std::unique_lock lock(mutex);if(quit)return Fail("host_stopping");
        if(value.kind==EControl::Scene&&value.scene.generation<=highestSceneRequest)return Fail("stale_scene");
        if(coalesce)for(auto it=controls.rbegin();it!=controls.rend();++it)if(it->kind==value.kind){
            if(value.kind==EControl::Pressure&&PressureRank(it->value)>PressureRank(value.value))value.value=it->value;
            if(value.kind==EControl::Thermal)value.value=std::max(it->value,value.value);
            if(value.kind==EControl::Scene)highestSceneRequest=value.scene.generation;
            auto previous=std::move(*it);*it=std::move(value);changed.notify_one();lock.unlock();return OK();
        }
        if(controls.size()>=MaximumControls)return Fail("control_queue_full");
        if(value.kind==EControl::Scene)highestSceneRequest=value.scene.generation;
        controls.push_back(std::move(value));changed.notify_one();return OK();
    }
    void MatchDevice(VulkanDevice& selected) {
        std::string error;auto& f=VulkanAPI::getFunctions();VkPhysicalDeviceProperties properties{};
        f.vkGetPhysicalDeviceProperties(selected.getPhysicalDevice(),&properties);
        FAndroidPhysicalDeviceIdentity identity;identity.vendorID=properties.vendorID;identity.deviceID=properties.deviceID;
        identity.driverVersion=properties.driverVersion;identity.name=properties.deviceName;
        if(f.vkGetPhysicalDeviceProperties2&&properties.apiVersion>=VK_API_VERSION_1_1){
            VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 extended{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};extended.pNext=&id;
            f.vkGetPhysicalDeviceProperties2(selected.getPhysicalDevice(),&extended);static constexpr char hex[]="0123456789abcdef";
            for(auto byte:id.deviceUUID){identity.deviceUUID+=hex[byte>>4];identity.deviceUUID+=hex[byte&15];}
        }
        physicalMatched=FAndroidCapabilities::MatchPhysicalDevice(capabilities,identity,physicalIndex,error,true);
        if(!physicalMatched)status=Fail("selected_device_identity_unavailable",error);
    }
    bool EnsureDevice() {
        if(device){MatchDevice(*device);return physicalMatched;}
        if(!capabilities.renderEligible){status=Fail("diagnostics_only","Static Vulkan admission failed");return false;}
        std::string error;
        auto base=info.budget->TryReserve(ESplatBudgetComponent::Native,32ull<<20,error);
        if(!base){status=Fail("device_budget_rejected",error);return false;}
        auto next=std::make_unique<VulkanDevice>();
        RHICreateInfo create;create.applicationName="MonsterEngine Android";create.windowWidth=create.windowHeight=1;
        if(!next->initialize(create)){next.reset();status=Fail("device_initialize_failed");return false;}
        auto account=info.budget;
        next->SetPresentationBudget([account](uint64 bytes)->std::shared_ptr<void>{
            std::string reason;auto lease=account->TryReserve(ESplatBudgetComponent::Swapchain,bytes,reason);
            return lease?std::make_shared<FSplatBudgetCoordinator::FReservation>(std::move(*lease)):nullptr;
        });
        MatchDevice(*next);
        if(!physicalMatched){next.reset();return false;}
        auto viewer=std::make_unique<FSplatViewerSession>();viewer->SetBudgetCoordinator(*info.budget);
        auto initialized=viewer->Initialize(*next,*info.resources,info.config);
        if(!initialized.ok){viewer.reset();next.reset();status=initialized;return false;}
        deviceLease=std::move(base);device=std::move(next);session=std::move(viewer);
        graphicsObserved=splatObserved=false;return true;
    }
    void Observe(FAndroidExecutionObservation observation) {
        if(!physicalMatched){status=Fail("execution_device_identity_unavailable");return;}
        observation.physicalDeviceIndex=physicalIndex;
        if(device){
            const auto& requested=device->getRequestedFeatures();const auto& enabled=device->getEnabledFeatures();
#define FEATURE(name) if(requested.name)observation.requestedFeatures.emplace_back(#name);if(enabled.name)observation.enabledFeatures.emplace_back(#name)
            FEATURE(shaderInt64);FEATURE(shaderFloat64);FEATURE(shaderStorageImageExtendedFormats);
            FEATURE(shaderStorageImageReadWithoutFormat);FEATURE(shaderStorageImageWriteWithoutFormat);
#undef FEATURE
        }
        std::string error;if(!FAndroidCapabilities::ObserveExecution(capabilities,observation,error))status=Fail("execution_report_failed",error);
    }
    void PollReadback() {
        if(!readback||!readback->token.value||!device->isSubmissionComplete(readback->token))return;
        auto& capture=*readback;auto observation=capture.observation;
        observation.submissionToken=capture.token.value;observation.submissionCompleted=true;
        const auto* pixels=static_cast<const uint8_t*>(capture.buffer->map());
        bool valid=false;
        if(pixels){
            if(capture.graphics)valid=pixels[0]<=2&&pixels[1]>=253&&pixels[2]<=2&&pixels[3]>=253;
            else {
                uint32_t colored=0;bool varied=false;
                for(uint32_t n=0;n<capture.samples;++n){const auto* p=pixels+4*n;
                    if(p[0]>3||p[1]>3||p[2]>3)++colored;
                    if(n&&(std::abs(int(p[0])-int(pixels[0]))>2||std::abs(int(p[1])-int(pixels[1]))>2||std::abs(int(p[2])-int(pixels[2]))>2))varied=true;
                }
                valid=colored>=2&&varied;
            }
            capture.buffer->unmap();
        }
        observation.readbackValidated=valid;
        // VK_SUBOPTIMAL_KHR still represents a completed submission and valid
        // readback. It is not a failed frame or a reason to rebuild a stable
        // swapchain continuously.
        const bool presentationCompleted=observation.vkResult==VK_SUCCESS||observation.vkResult==VK_SUBOPTIMAL_KHR;
        observation.passed=valid&&presentationCompleted;
        observation.reason=observation.passed?(capture.graphics?"Known green graphics clear matched GPU readback":"16x16 output samples contain nonblack varying rendered pixels; smoke check only"):
            "GPU completed but smoke pixel validation or native presentation failed";
        Observe(std::move(observation));if(capture.graphics)graphicsObserved=true;else splatObserved=true;
        // Keep dedicated backing/accounting until explicit teardown; normal
        // completion polling must never introduce a device-idle wait.
        retiredReadbacks.push_back(std::move(capture));readback.reset();
    }
    bool RecordReadback(IRHICommandList& command,VkImage image,VkImageLayout layout,uint32_t width,uint32_t height,bool graphics,uint64_t frameId) {
        if(readback||!width||!height)return false;
        auto* list=dynamic_cast<FVulkanRHICommandListImmediate*>(&command);
        if(!list||!list->GetVulkanCommandBuffer())return false;
        FReadback pending;
        if(!retiredReadbacks.empty()){pending=std::move(retiredReadbacks.back());retiredReadbacks.pop_back();}
        pending.token={};pending.observation={};pending.graphics=graphics;pending.samples=graphics?1:256;
        BufferDesc desc;desc.size=1024;desc.usage=EResourceUsage::TransferDst;
        desc.memoryUsage=EMemoryUsage::Readback;desc.cpuAccessible=true;desc.dedicatedAllocation=true;
        uint64 bytes=0,alignment=0;std::string error;
        if(!pending.buffer){
            if(!device->getBufferAllocationRequirements(desc,bytes,alignment))return false;
            pending.lease=info.budget->TryReserve(ESplatBudgetComponent::TemporaryGPU,bytes,error);if(!pending.lease)return false;
            pending.buffer=device->createBuffer(desc);FBufferAllocationInfo allocation;
            if(!pending.buffer||!pending.buffer->getAllocationInfo(allocation)||allocation.backingBytes>bytes)return false;
        }
        auto* buffer=dynamic_cast<VulkanBuffer*>(pending.buffer.Get());if(!buffer)return false;
        const auto native=list->GetVulkanCommandBuffer()->getHandle();
        VkImageMemoryBarrier before{};before.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        before.oldLayout=layout;before.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        before.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;before.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        before.srcQueueFamilyIndex=before.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        before.image=image;before.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&before);
        std::array<VkBufferImageCopy,256> regions{};
        for(uint32_t n=0;n<pending.samples;++n){auto& region=regions[n];region.bufferOffset=n*4;region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={1,1,1};
            region.imageOffset={int32_t(graphics?width/2:std::min(width-1,uint32_t(uint64_t(n%16)*width/16))),int32_t(graphics?height/2:std::min(height-1,uint32_t(uint64_t(n/16)*height/16))),0};}
        vkCmdCopyImageToBuffer(native,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer->getBuffer(),pending.samples,regions.data());
        auto after=before;after.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;after.newLayout=layout;
        after.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;after.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&after);
        VkBufferMemoryBarrier host{};host.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=buffer->getBuffer();host.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
        pending.observation.stage=graphics?"graphicsSmoke":"splatSmoke";
        pending.observation.frameId=frameId;pending.observation.sceneGeneration=scene.generation;
        pending.observation.surfaceGeneration=current->generation;readback=std::move(pending);return true;
    }
    bool RetireWindow(std::shared_ptr<FWindow> window) {
        window->valid.store(false,std::memory_order_release);
        if(window==current){
            if(device){
                session->Suspend();
                const auto detached=device->DetachExternalSurface(window->generation);
                if(detached.status!=EFrameResult::Success&&detached.status!=EFrameResult::DeviceLost){status=Fail("surface_retirement_pending");return false;}
                if(readback&&readback->token.value)PollReadback();
            }
            current.reset();
        }
        window->inUse.store(false,std::memory_order_release);
        // Retire the host reference before acknowledging the synchronous waiter.
        if(window->window){ReleaseWindow(window->window);window->window=nullptr;}
        {std::lock_guard lock(mutex);window->retired=true;window->attached=false;windows.erase(window->generation);changed.notify_all();}
        return true;
    }
    void TeardownDevice() {
        if(session)session->Shutdown();session.reset();
        if(device){device->waitForIdle();if(readback)readback->buffer.Reset();for(auto& capture:retiredReadbacks)capture.buffer.Reset();device->DrainCompletedDestructions();device.reset();}
        readback.reset();retiredReadbacks.clear();deviceLease.reset();
        if(current){current->inUse.store(false,std::memory_order_release);current->attached=false;}
    }
    bool OpenCurrentScene() {
        if(!session||scene.sceneId.empty())return true;
        if(sceneSequence==UINT64_MAX){status=Fail("scene_generation_exhausted");return false;}
        scene.generation=std::max(scene.generation,++sceneSequence);sceneSequence=scene.generation;
        status=session->OpenScene(scene);splatObserved=false;return status.ok;
    }
    void LostDevice() {
        const bool retry=!autoRetryUsed;autoRetryUsed=true;
        TeardownDevice();manualRetry=!retry;status=Fail(retry?"device_lost_auto_retry":"device_lost_manual_retry_required");
        if(retry){if(EnsureDevice()&&current&&current->valid.load(std::memory_order_acquire)){
            std::string error;if(device->AttachExternalSurface(current->window,current->generation,current->width,current->height,error)){
                current->attached=true;session->ResizeInternal(current->width,current->height);OpenCurrentScene();
            }else{manualRetry=true;status=Fail("device_recovery_attach_failed",error);}
        }else manualRetry=true;}
    }
    void Render(uint64_t timestamp,EAndroidFrameScheduler source) {
        if(!performance.AcceptFrame(source,timestamp)||!resumed||manualRetry||!current||!current->valid.load(std::memory_order_acquire)||!current->attached||!device||!session)return;
        current->inUse.store(true,std::memory_order_release);
        if(!current->valid.load(std::memory_order_acquire)){current->inUse.store(false,std::memory_order_release);return;}
        PollReadback();session->Diagnostics().PollScreenshot();
        FViewerScreenshot completed;if(session->Diagnostics().TakeScreenshot(completed)){std::lock_guard lock(mutex);screenshot=std::move(completed);}
        const auto acquired=device->beginFrame(1000000);
        if(acquired.status==EFrameResult::DeviceLost){LostDevice();return;}
        if(acquired.status==EFrameResult::OutOfDate){
            if(acquired.acquired)device->cancelFrame();
            if(device->recreateSwapchain(current->width,current->height))session->ResizeInternal(current->width,current->height);return;
        }
        if(acquired.status==EFrameResult::SurfaceLost){current->valid.store(false,std::memory_order_release);{std::lock_guard lock(mutex);current->detachRequested=true;}return;}
        if(acquired.status!=EFrameResult::Success)return;
        auto* command=device->getImmediateCommandList();if(!command){device->cancelFrame();return;}
        command->begin();
        auto* nativeList=dynamic_cast<FVulkanRHICommandListImmediate*>(command);
        if(!nativeList||!nativeList->isRecording()||!nativeList->GetVulkanCommandBuffer()){
            device->cancelFrame();status=Fail("command_recording_unavailable");return;
        }
        FViewerFrameResult recorded;bool graphics=false;
        if(!graphicsObserved&&!device->supportsSwapchainReadback()){
            FAndroidExecutionObservation observation;observation.stage="graphicsSmoke";observation.reason="Swapchain transfer-source readback unsupported; execution validation unavailable";Observe(observation);graphicsObserved=true;
        }
        if(!graphicsObserved&&!readback&&device->supportsSwapchainReadback()){
            graphics=true;MonsterEngine::TArray<MonsterEngine::TSharedPtr<IRHITexture>> targets;
            command->setRenderTargets(MonsterEngine::TSpan<MonsterEngine::TSharedPtr<IRHITexture>>(targets),nullptr);
            auto* list=dynamic_cast<FVulkanRHICommandListImmediate*>(command);
            VkClearAttachment clear{};clear.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;clear.colorAttachment=0;clear.clearValue.color.float32[1]=1;clear.clearValue.color.float32[3]=1;
            VkClearRect rect{};rect.rect.extent=device->getSwapchainExtent();rect.layerCount=1;
            vkCmdClearAttachments(list->GetVulkanCommandBuffer()->getHandle(),1,&clear,1,&rect);command->endRenderPass();
            if(!RecordReadback(*command,device->getCurrentSwapchainImage(),VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,rect.rect.extent.width,rect.rect.extent.height,true,++smokeSequence)){
                FAndroidExecutionObservation observation;observation.stage="graphicsSmoke";observation.reason="GPU readback allocation/recording unavailable";Observe(observation);graphicsObserved=true;
            }
        }else {
            FViewerFrameRequest request;request.commandList=command;request.monotonicNanoseconds=timestamp;
            request.sceneGeneration=scene.generation;request.surfaceGeneration=current->generation;request.configGeneration=info.config.generation;
            request.viewportWidth=current->width;request.viewportHeight=current->height;
            recorded=session->TickAndRecord(request);command->endRenderPass();status=recorded.status;
            if(recorded.outputTexture){
                session->PrepareOutputForCompute(*command);
                if(!splatObserved&&!readback){auto* texture=dynamic_cast<VulkanTexture*>(recorded.outputTexture.Get());if(texture)RecordReadback(*command,texture->getImage(),VK_IMAGE_LAYOUT_GENERAL,recorded.outputTexture->getWidth(),recorded.outputTexture->getHeight(),false,recorded.pipelineFrameId);}
                bool requested=false;{std::lock_guard lock(mutex);requested=screenshotRequested&&!screenshot;screenshotRequested=false;}
                if(requested){std::string error;if(!session->Diagnostics().RecordScreenshot(*device,*command,recorded.outputTexture,*recorded.diagnostics,*info.budget,error))status=Fail("screenshot_rejected",error);}
            }
        }
        if(!graphics&&!recorded.recorded){const auto cancelled=device->cancelFrame();if(cancelled.status==EFrameResult::DeviceLost)LostDevice();return;}
        if(!graphics&&!recorded.outputTexture){
            // Upload-only recordings still own an acquired WSI image. Establish
            // its presentation layout without discarding the actual uploads.
            auto* list=dynamic_cast<FVulkanRHICommandListImmediate*>(command);
            VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            barrier.image=device->getCurrentSwapchainImage();barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            vkCmdPipelineBarrier(list->GetVulkanCommandBuffer()->getHandle(),VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        }
        std::unique_lock submissionLock(mutex);
        if(!current->valid.load(std::memory_order_acquire)){
            submissionLock.unlock();
            device->cancelFrame();
            if(readback&&!readback->token.value){retiredReadbacks.push_back(std::move(*readback));readback.reset();}
            session->Diagnostics().Shutdown();return;
        }
        // A recorded upload-only frame has no output but must still be submitted.
        command->end();
        const auto submitted=device->presentFrame();
        submissionLock.unlock();
        if(submitted.submitted&&submitted.token.value){if(!graphics)session->OnSubmitted(submitted.token);if(readback&&!readback->token.value){readback->token=submitted.token;readback->observation.vkResult=submitted.presentResult;}}
        if(!submitted.submitted){
            if(readback&&!readback->token.value){retiredReadbacks.push_back(std::move(*readback));readback.reset();}
            session->Diagnostics().Shutdown();
        }
        if(submitted.status==EFrameResult::DeviceLost)LostDevice();
        else if(submitted.status==EFrameResult::SurfaceLost){current->valid.store(false,std::memory_order_release);std::lock_guard lock(mutex);current->detachRequested=true;}
        else if(submitted.status==EFrameResult::OutOfDate)retryPending=true;
        else if(submitted.status==EFrameResult::Error){manualRetry=true;status=Fail("frame_submit_or_present_failed","Manual retry required after observed backend error");}
    }
    void Pressure(const FAndroidPerformanceDecision& decision) {
        pressureRetired=false;
        if(session&&decision.stopPrefetch)session->OnMemoryPressure();
        if(session&&decision.pressure==EAndroidPressureAction::ReleaseScene){TeardownDevice();scene={};sceneLease.reset();manualRetry=true;pressureRetired=true;}
        else if(session&&decision.pressure==EAndroidPressureAction::RetireSafePages){
            if(device->DrainCompletedDestructions()){
                if(auto* stream=session->Streaming())if(auto* pool=stream->GetPagePool())pool->TrimUnprotected();
                pressureRetired=true;
            }
        }else if(decision.pressure==EAndroidPressureAction::TrimCache){
            if(session&&session->Streaming())if(auto* streamer=session->Streaming()->GetStreamer())streamer->OnMemoryPressure();
            pressureRetired=true;
        }else if(decision.pressure==EAndroidPressureAction::None)pressureRetired=true;
        if(decision.qualityChanged&&session){auto config=info.config;if(config.generation!=UINT64_MAX){++config.generation;config.runtime.lod.qualityPixels=float(decision.qualityPixels);info.config=config;session->ApplySettings(config);}}
        status=pressureRetired?OK("pressure_action_retired"):Fail("pressure_retirement_pending");
    }
    void Control(FControl command) {
        switch(command.kind){
        case EControl::Scene:
            if(session)session->ReleaseScene();sceneLease.reset();scene=std::move(command.scene);sceneLease=std::move(command.lease);
            if(manualRetry){
                // Explicitly opening a scene resumes a deliberate release; backend failures require Retry.
                if(status.code!="scene_released")break;
                manualRetry=false;
            }
            if(EnsureDevice())OpenCurrentScene();break;
        case EControl::Settings:
            if(command.config.generation<=info.config.generation){status=Fail("stale_settings");break;}
            if(session){
                session->ApplySettings(command.config);const auto observed=session->Snapshot();
                if(observed->status.code=="readmission_required"){
                    TeardownDevice();info.config=std::move(command.config);
                    if(!manualRetry&&EnsureDevice())OpenCurrentScene();
                }else if(observed->effectiveConfig&&observed->effectiveConfig->generation==command.config.generation){info.config=std::move(command.config);status=observed->status;}
                else status=observed->status;
            }else{info.config=std::move(command.config);status=OK("settings_applied");}break;
        case EControl::Input:if(session&&current&&current->valid.load()&&command.input.surfaceGeneration==current->generation)session->ApplyCameraInput(command.input);break;
        case EControl::Pressure:Pressure(performance.OnMemoryPressure(command.value));break;
        case EControl::Thermal:Pressure(performance.OnThermalStatus(command.value));break;
        case EControl::Release:
            TeardownDevice();scene={};sceneLease.reset();manualRetry=true;
            {std::deque<FControl> discarded;{std::lock_guard lock(mutex);discarded.swap(controls);frame.reset();}for(auto& pending:discarded)Ack(pending.ack,Fail("control_cancelled_by_release"));}
            status=OK("scene_released");Ack(command.ack,status);break;
        case EControl::Retry:
            performance.RequestManualRecovery();manualRetry=false;autoRetryUsed=false;TeardownDevice();
            if(EnsureDevice()){
                if(current&&current->valid.load()){std::string error;if(device->AttachExternalSurface(current->window,current->generation,current->width,current->height,error)){current->attached=true;session->ResizeInternal(current->width,current->height);}else{manualRetry=true;status=Fail("manual_attach_failed",error);}}
                OpenCurrentScene();
            }else manualRetry=true;break;
        }
    }
    void Run() noexcept {
        try {
#if PLATFORM_ANDROID
            ownerLog=std::make_unique<FAndroidOwnerLog>();
#endif
            std::string error;if(!performance.Configure(info.performance,info.experiment,error))status=Fail("performance_policy_rejected",error);
            auto initialProbe=info.probe;initialProbe.nativeWindow=nullptr;capabilities=FAndroidCapabilities::Probe(initialProbe);Publish();
            for(;;){
                std::shared_ptr<FWindow> detach,attach;std::optional<FControl> control;std::optional<std::pair<uint64_t,EAndroidFrameScheduler>> token;
                bool lifecycle=false,resume=false,shouldQuit=false;
                {
                    std::unique_lock lock(mutex);
                    auto ready=[&]{if(quit||lifecycleDirty||!controls.empty()||frame||retryPending)return true;for(const auto& item:windows)if(item.second->detachRequested||(!manualRetry&&!item.second->attached))return true;return false;};
                    changed.wait(lock,ready);shouldQuit=quit;
                    for(const auto& item:windows)if(item.second->detachRequested||!item.second->valid.load()){detach=item.second;break;}
                    if(!detach&&!shouldQuit&&!manualRetry)for(const auto& item:windows)if(!item.second->attached){attach=item.second;break;}
                    lifecycle=lifecycleDirty;resume=resumeRequested;if(!detach&&!shouldQuit)lifecycleDirty=false;
                    if(!detach&&!attach&&!shouldQuit&&!controls.empty()){
                        auto selected=std::find_if(controls.begin(),controls.end(),[](const auto& c){return c.kind==EControl::Release||c.kind==EControl::Pressure||c.kind==EControl::Thermal||c.kind==EControl::Retry;});
                        if(selected==controls.end())selected=controls.begin();control=std::move(*selected);controls.erase(selected);
                    }
                    if(!detach&&!attach&&!shouldQuit&&!control){token=frame;frame.reset();}
                }
                if(shouldQuit){if(current&&!RetireWindow(current)){std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(16));continue;}break;}
                if(detach){if(!RetireWindow(detach)){std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(16));}Publish();continue;}
                if(lifecycle){resumed=resume;if(session){if(resume&&current)session->ResizeInternal(current->width,current->height);else session->Suspend();}}
                if(attach){
                    if(current&&current!=attach){if(!RetireWindow(current)){std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(16));continue;}}
                    auto options=info.probe;options.nativeWindow=attach->window;capabilities=FAndroidCapabilities::Probe(options);
                    graphicsObserved=splatObserved=false;
                    const bool newDevice=!device;
                    if(EnsureDevice()){
                        std::string detail;
                        if(device->AttachExternalSurface(attach->window,attach->generation,attach->width,attach->height,detail)){
                            current=attach;attach->attached=true;attach->inUse.store(true,std::memory_order_release);status=session->ResizeInternal(attach->width,attach->height);if(newDevice&&!scene.sceneId.empty())OpenCurrentScene();if(!resumed)session->Suspend();
                        }else{status=Fail("surface_attach_failed",detail);MR_LOG_ERROR("Android Surface attach failed: "+detail);manualRetry=true;attach->valid.store(false);std::lock_guard lock(mutex);attach->detachRequested=true;}
                    }else{attach->valid.store(false);std::lock_guard lock(mutex);attach->detachRequested=true;}
                }else if(control)Control(std::move(*control));
                else if(token){
                    if(retryPending&&device&&current){retryPending=false;if(device->recreateSwapchain(current->width,current->height))session->ResizeInternal(current->width,current->height);}
                    Render(token->first,token->second);
                }else retryPending=false;
                Publish();
            }
        }catch(const std::exception& e){status=Fail("owner_exception",e.what());}catch(...){status=Fail("owner_exception");}
        try{TeardownDevice();sceneLease.reset();}catch(...){status=Fail("owner_teardown_failed");}
#if PLATFORM_ANDROID
        ownerLog.reset();
#endif
        {
            std::lock_guard lock(mutex);
            for(auto& [generation,window]:windows){window->inUse.store(false);window->valid.store(false);if(window->window){ReleaseWindow(window->window);window->window=nullptr;}window->retired=true;}
            windows.clear();current.reset();controls.clear();frame.reset();stopped=true;
        }
        Publish();changed.notify_all();
    }
    void Stop() {
        {std::lock_guard lock(mutex);quit=true;for(auto& [generation,window]:windows)window->valid.store(false,std::memory_order_release);changed.notify_all();}
        if(owner.joinable())owner.join();
    }
};

FAndroidRenderHost::FAndroidRenderHost(FAndroidRenderHostCreateInfo info):Impl(std::make_unique<FImpl>(std::move(info))){}
FAndroidRenderHost::~FAndroidRenderHost(){Impl->Stop();}
uint64_t FAndroidRenderHost::Create(FAndroidRenderHostCreateInfo info,std::string& error) {
    if(!info.resources||!info.budget||!info.config.generation||info.probe.shaderManifestJSON.size()>(4ull<<20)){error="Invalid native host resources/account/config/manifest";return 0;}
    try {
        auto host=std::shared_ptr<FAndroidRenderHost>(new FAndroidRenderHost(std::move(info)));
        std::string reason;host->Impl->metadataLease=host->Impl->info.budget->TryReserve(ESplatBudgetComponent::Metadata,16ull<<20,reason);
        if(!host->Impl->metadataLease){error=reason;return 0;}
        host->Impl->owner=std::thread([raw=host->Impl.get()]{raw->Run();});
        std::lock_guard lock(RegistryMutex);if(!NextHandle){error="Native handle space exhausted";return 0;}
        const auto handle=NextHandle++;Registry.emplace(handle,std::move(host));error.clear();return handle;
    }catch(const std::exception& e){error=e.what();return 0;}
}
std::shared_ptr<FAndroidRenderHost> FAndroidRenderHost::Acquire(uint64_t handle){std::lock_guard lock(RegistryMutex);auto it=Registry.find(handle);return it==Registry.end()?nullptr:it->second;}
bool FAndroidRenderHost::Destroy(uint64_t handle){std::shared_ptr<FAndroidRenderHost> host;{std::lock_guard lock(RegistryMutex);auto it=Registry.find(handle);if(it==Registry.end())return false;host=std::move(it->second);Registry.erase(it);}host->Impl->Stop();return true;}
FViewerStatus FAndroidRenderHost::Attach(ANativeWindow* window,uint64_t generation,uint32_t width,uint32_t height){
#if !PLATFORM_ANDROID
    (void)window;(void)generation;(void)width;(void)height;return Fail("android_unavailable");
#else
    if(!window||!generation||!width||!height||width>16384||height>16384)return Fail("invalid_surface");
    auto value=std::make_shared<FWindow>();RetainWindow(window);value->window=window;value->generation=generation;value->width=width;value->height=height;
    std::lock_guard lock(Impl->mutex);if(Impl->quit)return Fail("host_stopping");if(generation<=Impl->highestWindow)return Fail("stale_surface");if(Impl->windows.size()>=MaximumWindows)return Fail("surface_queue_full");
    Impl->highestWindow=generation;Impl->windows.emplace(generation,value);Impl->changed.notify_one();return OK();
#endif
}
FViewerStatus FAndroidRenderHost::Detach(uint64_t generation,std::chrono::milliseconds timeout){
    auto& i=*Impl;std::unique_lock lock(i.mutex);auto it=i.windows.find(generation);if(it==i.windows.end())return generation&&generation<=i.highestWindow?OK("surface_released"):Fail("unknown_surface");
    auto window=it->second;window->valid.store(false,std::memory_order_release);window->detachRequested=true;i.changed.notify_one();
    if(!i.changed.wait_for(lock,std::clamp(timeout,std::chrono::milliseconds(0),std::chrono::milliseconds(10000)),[&]{return window->retired||i.stopped;}))return Fail("surface_detach_timeout","Ownership retained; retry detach acknowledgement");
    return window->retired?OK("surface_released"):Fail("host_stopped");
}
FViewerStatus FAndroidRenderHost::SetResumed(bool value){std::lock_guard lock(Impl->mutex);if(Impl->quit)return Fail("host_stopping");Impl->resumeRequested=value;Impl->lifecycleDirty=true;if(!value)Impl->frame.reset();Impl->changed.notify_one();return OK();}
FViewerStatus FAndroidRenderHost::RequestFrame(uint64_t nanos,EAndroidFrameScheduler source){if(!nanos)return Fail("invalid_frame_timestamp");std::lock_guard lock(Impl->mutex);if(Impl->quit)return Fail("host_stopping");if(!Impl->frame||nanos>Impl->frame->first)Impl->frame=std::make_pair(nanos,source);Impl->changed.notify_one();return OK();}
FViewerStatus FAndroidRenderHost::OpenScene(FSceneDescriptor scene,std::shared_ptr<void> lease){if(scene.sceneId.empty()||scene.sceneId.size()>512||!scene.generation||!lease||scene.directory.size()>4096||scene.sourcePLY.size()>4096||scene.manifestPath.size()>4096)return Fail("invalid_scene_or_lease");FControl c;c.kind=EControl::Scene;c.scene=std::move(scene);c.lease=std::move(lease);return Impl->Queue(std::move(c),true);}
FViewerStatus FAndroidRenderHost::ApplySettings(FViewerConfig config){if(!config.generation||config.configHash.size()>128||config.shaderIdentity.size()>128)return Fail("invalid_settings");FControl c;c.kind=EControl::Settings;c.config=std::move(config);return Impl->Queue(std::move(c),true);}
FViewerStatus FAndroidRenderHost::CameraInput(FCameraInputState input){
    if(!input.surfaceGeneration||!std::isfinite(input.orbitX)||!std::isfinite(input.orbitY)||!std::isfinite(input.panX)||!std::isfinite(input.panY)||!std::isfinite(input.pinchScale)||input.pinchScale<=0)return Fail("invalid_input");
    auto& i=*Impl;std::lock_guard lock(i.mutex);if(i.quit)return Fail("host_stopping");
    if(input.phase==EViewerPointerPhase::Move&&!i.controls.empty()){auto& last=i.controls.back();if(last.kind==EControl::Input&&last.input.phase==input.phase&&last.input.pointerId==input.pointerId&&last.input.surfaceGeneration==input.surfaceGeneration){last.input.orbitX=std::clamp(last.input.orbitX+input.orbitX,-16.0f,16.0f);last.input.orbitY=std::clamp(last.input.orbitY+input.orbitY,-16.0f,16.0f);last.input.panX=std::clamp(last.input.panX+input.panX,-16.0f,16.0f);last.input.panY=std::clamp(last.input.panY+input.panY,-16.0f,16.0f);last.input.pinchScale=std::clamp(last.input.pinchScale*input.pinchScale,0.001f,1000.0f);last.input.reset|=input.reset;i.changed.notify_one();return OK();}}
    if(i.controls.size()>=MaximumControls)return Fail("control_queue_full");FControl c;c.kind=EControl::Input;c.input=input;i.controls.push_back(std::move(c));i.changed.notify_one();return OK();
}
FViewerStatus FAndroidRenderHost::MemoryPressure(int32_t level){FControl c;c.kind=EControl::Pressure;c.value=level;return Impl->Queue(std::move(c),true);}
FViewerStatus FAndroidRenderHost::ThermalStatus(int32_t status){if(status<-1||status>6)return Fail("invalid_thermal_status");FControl c;c.kind=EControl::Thermal;c.value=status;return Impl->Queue(std::move(c),true);}
FViewerStatus FAndroidRenderHost::ReleaseScene(std::chrono::milliseconds timeout){auto ack=std::make_shared<FAck>();FControl c;c.kind=EControl::Release;c.ack=ack;auto status=Impl->Queue(std::move(c));if(!status.ok)return status;std::unique_lock lock(Impl->mutex);if(!Impl->changed.wait_for(lock,std::clamp(timeout,std::chrono::milliseconds(0),std::chrono::milliseconds(10000)),[&]{return ack->done||Impl->stopped;}))return Fail("scene_release_timeout");return ack->done?ack->status:Fail("host_stopped");}
FViewerStatus FAndroidRenderHost::Retry(){FControl c;c.kind=EControl::Retry;return Impl->Queue(std::move(c),true);}
FViewerStatus FAndroidRenderHost::RequestScreenshot(){std::lock_guard lock(Impl->mutex);if(Impl->quit)return Fail("host_stopping");if(Impl->screenshot)return Fail("screenshot_not_consumed");Impl->screenshotRequested=true;return OK();}
bool FAndroidRenderHost::TakeScreenshot(FViewerScreenshot& value){std::lock_guard lock(Impl->mutex);if(!Impl->screenshot)return false;value=std::move(*Impl->screenshot);Impl->screenshot.reset();return true;}
std::shared_ptr<const FAndroidRenderHostSnapshot> FAndroidRenderHost::Snapshot()const{return std::atomic_load_explicit(&Impl->snapshot,std::memory_order_acquire);}
} // namespace MonsterRender::Splat
