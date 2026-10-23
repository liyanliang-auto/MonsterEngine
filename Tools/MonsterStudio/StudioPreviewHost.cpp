#include "StudioPreviewHost.h"
#include "Core/RuntimeLease.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Renderer/Splat/SplatViewerSession.h"
#include "Renderer/Splat/SplatPLYStream.h"
#include "RHI/IRHICommandList.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <thread>

using namespace MonsterRender;
using namespace MonsterRender::Splat;
using namespace MonsterRender::RHI;
using namespace MonsterRender::RHI::Vulkan;
namespace {
std::string UTF8(const std::filesystem::path& p) { auto s=p.u8string();return {s.begin(),s.end()}; }
uint64_t Nanos() {return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct PreparedBounds { FViewerOrbitCameraPreset pose; uint32_t count=0; };
// Fixed sample size keeps preparation independent of model size; canonical parser
// performs all coordinate/activation validation. Hash-priority sampling is stable.
PreparedBounds Bounds(const std::filesystem::path& path, FSplatBuildControl& control,
    const std::optional<MonsterEngine::VideoTo3D::ReconstructionView>& sourceView,double viewportAspect) {
    FSplatPLYLimits limits;limits.maxRecords=1000000;limits.maxFileBytes=512ull<<20;
    FSplatPLYHeader header;std::string error;
    if(!FSplatPLYStream::Probe(UTF8(path),header,error,limits,&control))throw std::runtime_error(error);
    struct Point {uint64_t key;std::array<float,3> p;};
    std::vector<Point> sample;sample.reserve(8192);
    auto less=[](const Point& a,const Point& b){return a.key<b.key;};
    if(!FSplatPLYStream::StreamRecords(UTF8(path),header,256,control,[&](std::span<const FSplatGaussian> rows,const FSplatBuildSourceCursor&,std::string&){
        for(const auto& g:rows){
            uint64_t h=g.originalId+0x9e3779b97f4a7c15ull;h=(h^(h>>30))*0xbf58476d1ce4e5b9ull;h=(h^(h>>27))*0x94d049bb133111ebull;h^=h>>31;
            Point point{h,{g.position[0],g.position[1],g.position[2]}};
            if(!std::all_of(point.p.begin(),point.p.end(),[](float v){return std::isfinite(v)&&std::abs(v)<1e12f;}))throw std::runtime_error("Preview coordinates outside finite supported range");
            if(sample.size()<8192){sample.push_back(point);std::push_heap(sample.begin(),sample.end(),less);}
            else if(h<sample.front().key){std::pop_heap(sample.begin(),sample.end(),less);sample.back()=point;std::push_heap(sample.begin(),sample.end(),less);}
        }return true;
    },error))throw std::runtime_error(error);
    if(sample.empty())throw std::runtime_error("Empty preview source");
    PreparedBounds result;result.count=uint32_t(header.recordCount);double radius2=0;
    for(int axis=0;axis<3;++axis){
        std::vector<float> values;values.reserve(sample.size());for(const auto& p:sample)values.push_back(p.p[axis]);std::sort(values.begin(),values.end());
        const size_t trim=values.size()>=100?values.size()/50:0;
        const double lo=values[trim],hi=values[values.size()-1-trim];
        result.pose.target[axis]=float((lo+hi)*0.5);radius2+=(hi-lo)*(hi-lo)*0.25;
    }
    const double radius=std::max(0.01,std::sqrt(radius2));
    const double distance=radius*3.4;
    // Source space stays unchanged. A moderate elevated diagonal orbit exposes depth.
    const double direction[3]={0.55,0.35,0.76};
    for(int k=0;k<3;++k)result.pose.eye[k]=float(result.pose.target[k]+direction[k]*distance);
    result.pose.nearPlane=float(std::max(0.0001,radius*0.001));
    result.pose.farPlane=float(std::max(distance+radius*20.0,1.0));
    if(sourceView){
        const auto& view=*sourceView;
        if(!std::isfinite(view.verticalFOVDegrees)||view.verticalFOVDegrees<1||view.verticalFOVDegrees>170||!std::isfinite(view.aspectRatio)||view.aspectRatio<=0)throw std::runtime_error("Invalid initial reconstruction camera intrinsics");
        double norm=0,upNorm=0,dot=0;
        for(int k=0;k<3;++k){if(!std::isfinite(view.eye[k])||!std::isfinite(view.forward[k])||!std::isfinite(view.up[k]))throw std::runtime_error("Nonfinite initial reconstruction camera");norm+=view.forward[k]*view.forward[k];upNorm+=view.up[k]*view.up[k];dot+=view.forward[k]*view.up[k];}
        if(std::abs(norm-1)>1e-4||std::abs(upNorm-1)>1e-4||std::abs(dot)>1e-4)throw std::runtime_error("Invalid initial reconstruction camera basis");
        const double pitch=std::asin(std::clamp(-view.forward[1],-1.0,1.0)),yaw=std::atan2(-view.forward[0],-view.forward[2]);
        const double cp=std::cos(pitch),sp=std::sin(pitch),sy=std::sin(yaw),cy=std::cos(yaw);
        const double right[3]={cy,0,-sy},up[3]={-sp*sy,cp,-sp*cy};
        double rollSin=0,rollCos=0;for(int k=0;k<3;++k){rollSin+=view.up[k]*right[k];rollCos+=view.up[k]*up[k];}
        std::vector<double> depths;double maxDepth=0;
        const double tangent=std::tan(view.verticalFOVDegrees*3.14159265358979323846/360);
        for(const auto& point:sample){
            double depth=0,length2=0;for(int k=0;k<3;++k){const double v=double(point.p[k])-view.eye[k];depth+=v*view.forward[k];length2+=v*v;}
            if(depth>0){maxDepth=std::max(maxDepth,depth);if(length2-depth*depth<=depth*depth*tangent*tangent)depths.push_back(depth);}
        }
        if(depths.empty())throw std::runtime_error("No Gaussian bounds in reconstructed camera view");
        std::sort(depths.begin(),depths.end());const double focus=depths[depths.size()/2];
        for(int k=0;k<3;++k){result.pose.eye[k]=float(view.eye[k]);result.pose.target[k]=float(view.eye[k]+view.forward[k]*focus);}
        // Fit the source camera's full field into the initial child rectangle.
        result.pose.verticalFOVDegrees=float(2*std::atan(tangent*std::max(1.0,view.aspectRatio/viewportAspect))*180/3.14159265358979323846);
        result.pose.rollDegrees=float(std::atan2(rollSin,rollCos)*180/3.14159265358979323846);
        result.pose.nearPlane=float(std::max(0.0001,std::min(focus*0.001,depths.front()*0.1)));
        result.pose.farPlane=float(std::max(focus*4,maxDepth+radius*2));
    }
    if(!FSplatViewerSession::IsValidOrbitCameraPreset(result.pose))throw std::runtime_error("Cannot frame preview bounds");
    return result;
}
void WriteFile(const std::filesystem::path& path,const char* data,size_t size) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot open capture output");
    out.write(data,std::streamsize(size));out.close();if(!out)throw std::runtime_error("Cannot write capture output");
}
std::string Quote(const std::string& value) {
    std::ostringstream out;out<<'"';for(unsigned char c:value){if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c);else out<<char(c);}out<<'"';return out.str();
}
}

struct StudioPreviewHost::Impl {
    HWND child,target;UINT message;
    mutable std::mutex mutex;std::condition_variable changed;std::thread owner;
    StudioPreviewStatus status;
    mutable std::atomic<bool> notificationPending{false};
    std::optional<StudioPreviewRequest> pending;
    std::shared_ptr<FSplatBuildControl> preparation;
    uint64_t retire=0,revision=0;bool stop=false,visible=false,reset=false;
    uint32_t width=0,height=0;float dx=0,dy=0,wheel=0;
    struct Capture {uint64_t id;std::filesystem::path png,metadata;};
    std::optional<Capture> capture;
    std::string captureIdentityJSON;
    std::string initialViewSourceJSON="null";
    FProcessRuntimeLease runtime;
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<FFileResourceProvider> provider;
    std::unique_ptr<FSplatBudgetCoordinator> budget;
    std::unique_ptr<FSplatViewerSession> session;
    FViewerOrbitCameraPreset initial;
    uint64_t sceneGeneration=0,surfaceGeneration=1;
    uint32_t actualWidth=0,actualHeight=0;
    Impl(HWND c,HWND t,UINT m):child(c),target(t),message(m){status.childWindow=reinterpret_cast<std::uintptr_t>(child);owner=std::thread([this]{Run();});}
    void Notify() {if(target&&message&&!notificationPending.exchange(true)){if(!PostMessageW(target,message,0,0))notificationPending.store(false);}}
    template<class F> void Publish(F f) {{std::lock_guard lock(mutex);f(status);++status.sequence;}Notify();}
    bool Cancelled(uint64_t value) {std::lock_guard lock(mutex);return stop||retire>status.retiredToken||revision!=value;}
    void Retire() {
        if(session){session->Shutdown();session.reset();Publish([](auto& s){++s.sessionShutdowns;});}
        provider.reset();
        const uint64_t reserved=budget?budget->Snapshot().reservedBytes:0;
        budget.reset();
        if(device){device->waitForIdle();device.reset();Publish([](auto& s){++s.deviceDestructions;});}
        runtime.Reset();actualWidth=actualHeight=0;
        Publish([&](auto& s){s.reservedBytesAfterRetire=reserved;s.gaussianCount=0;s.diagnosticsJSON.clear();});
        std::optional<Capture> cancelled;
        {std::lock_guard lock(mutex);cancelled=std::move(capture);capture.reset();}
        if(cancelled)Publish([](auto& s){s.capturePending=false;s.captureSucceeded=false;s.captureError="Capture cancelled by retirement";});
    }
    void Load(StudioPreviewRequest request,uint64_t value) {
        Retire();
        Publish([&](auto& s){s.identity=request.identity;s.state=StudioPreviewState::Loading;s.error.clear();s.submittedFrames=0;});
        runtime=FProcessRuntimeLease::Acquire();
        auto control=std::make_shared<FSplatBuildControl>();
        {std::lock_guard lock(mutex);preparation=control;if(stop||revision!=value||retire>status.retiredToken)control->RequestCancel();}
        double viewportAspect;{std::lock_guard lock(mutex);viewportAspect=double(std::max(1u,width))/std::max(1u,height);}
        const auto framed=Bounds(request.output,*control,request.initialView,viewportAspect);
        initialViewSourceJSON=request.initialView?"{\"camerasSha256\":"+Quote(request.initialView->camerasSha256)+",\"imagesSha256\":"+Quote(request.initialView->imagesSha256)+",\"imageName\":"+Quote(request.initialView->imageName)+"}":"null";
        if(Cancelled(value))return;
        device=std::make_unique<VulkanDevice>();
        Publish([](auto& s){++s.deviceCreations;});
        RHICreateInfo info;info.windowHandle=child;
        {std::lock_guard lock(mutex);info.windowWidth=std::max(1u,width);info.windowHeight=std::max(1u,height);}
        info.applicationName="MonsterStudio Preview";info.enableValidation=true;
        if(!device->initialize(info))throw std::runtime_error("Vulkan embedded surface initialization failed");
        Publish([&](auto& s){s.deviceName=device->getCapabilities().deviceName.c_str();});
        if(Cancelled(value))return;
        budget=std::make_unique<FSplatBudgetCoordinator>(1536ull<<20);
        provider=std::make_unique<FFileResourceProvider>(UTF8(request.output.parent_path()));
        session=std::make_unique<FSplatViewerSession>();session->SetBudgetCoordinator(*budget);
        FViewerConfig config;config.generation=1;config.maximumInternalEdge=1920;config.maximumInternalPixels=1920ull*1080;
        config.runtime.activeCapacity=framed.count;config.runtime.sortCapacity=4000000;
        config.runtime.lod.activeBudget=framed.count;config.runtime.lod.sortBudget=config.runtime.sortCapacity;
        config.runtime.streaming.cpuCacheBytes=512ull<<20;config.runtime.streaming.sceneGPUBytes=256ull<<20;
        config.runtime.streaming.temporaryGPUBytes=512ull<<20;config.runtime.displaySHDegree=-1;config.runtime.shStorageMode=0;
        auto initialized=session->Initialize(*device,*provider,config);if(!initialized.ok)throw std::runtime_error(initialized.code+": "+initialized.message);
        initial=framed.pose;
        FSceneDescriptor scene;scene.sceneId=request.identity.executionId;scene.sourcePLY=UTF8(request.output);scene.directory=UTF8(request.output.parent_path());scene.generation=++sceneGeneration;scene.contentHash=request.identity.contentHash;scene.initialCamera=initial;
        const auto opened=session->OpenScene(scene);if(!opened.ok)throw std::runtime_error(opened.code+": "+opened.message);
        const auto extent=device->getSwapchainExtent();actualWidth=extent.width;actualHeight=extent.height;
        Publish([&](auto& s){s.gaussianCount=framed.count;});
    }
    void Frame(uint32_t w,uint32_t h,float x,float y,float z,bool resetCamera) {
        if(!session||!device)return;
        if(w!=actualWidth||h!=actualHeight){
            if(!device->recreateSwapchain(w,h))throw std::runtime_error("Preview swapchain resize failed");
            auto resized=session->ResizeInternal(w,h);if(!resized.ok)throw std::runtime_error(resized.message);
            actualWidth=w;actualHeight=h;++surfaceGeneration;
        }
        if(resetCamera)session->SetOrbitCameraPreset(initial);
        if(x||y||z){FCameraInputState input;input.orbitX=x;input.orbitY=y;input.pinchScale=std::exp(std::clamp(z,-12.f,12.f)*0.12f);input.surfaceGeneration=surfaceGeneration;session->ApplyCameraInput(input);}
        session->Diagnostics().PollScreenshot();FViewerScreenshot shot;
        if(session->Diagnostics().TakeScreenshot(shot)){
            std::optional<Capture> completed;{std::lock_guard lock(mutex);completed=std::move(capture);capture.reset();}
            if(completed){try{if(!shot.status.ok)throw std::runtime_error(shot.status.message);WriteFile(completed->png,reinterpret_cast<const char*>(shot.png.data()),shot.png.size());const auto metadata="{\"previewIdentity\":"+captureIdentityJSON+",\"initialViewSource\":"+initialViewSourceJSON+",\"frame\":"+shot.metadataJSON+"}";WriteFile(completed->metadata,metadata.data(),metadata.size());Publish([](auto& s){s.capturePending=false;s.captureSucceeded=true;});}catch(const std::exception& e){Publish([&](auto& s){s.capturePending=false;s.captureSucceeded=false;s.captureError=e.what();});}}
        }
        auto acquired=device->beginFrame(1000000);
        if(acquired.status==EFrameResult::OutOfDate){device->recreateSwapchain(w,h);return;}
        if(acquired.status==EFrameResult::NotReady||acquired.status==EFrameResult::Timeout)return;
        if(acquired.status!=EFrameResult::Success)throw std::runtime_error("Preview surface acquisition failed");
        auto* command=device->getImmediateCommandList();if(!command){device->cancelFrame();throw std::runtime_error("Missing Vulkan command list");}
        command->begin();
        FViewerFrameRequest request;request.commandList=command;request.monotonicNanoseconds=Nanos();request.sceneGeneration=sceneGeneration;request.surfaceGeneration=surfaceGeneration;request.configGeneration=1;request.viewportWidth=w;request.viewportHeight=h;
        session->SetPresentationReservationEstimate(device->GetPresentationReservationEstimate());
        auto recorded=session->TickAndRecord(request);command->endRenderPass();
        if(!recorded.status.ok){device->cancelFrame();throw std::runtime_error(recorded.status.code+": "+recorded.status.message);}
        if(!recorded.recorded){device->cancelFrame();return;}
        bool screenshot=false;
        if(recorded.outputTexture){
            bool wanted;{std::lock_guard lock(mutex);wanted=capture.has_value()&&status.capturePending&&!captureRecorded;}
            if(wanted){session->PrepareOutputForCompute(*command);std::string error;screenshot=session->Diagnostics().RecordScreenshot(*device,*command,recorded.outputTexture,*recorded.diagnostics,*budget,error);std::lock_guard lock(mutex);if(!screenshot){capture.reset();status.capturePending=false;status.captureError=error;}else captureRecorded=true;}
        }else{
            // Upload-only commands must be submitted; the acquired image still needs PRESENT layout.
            auto* native=dynamic_cast<FVulkanRHICommandListImmediate*>(command);
            VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=device->getCurrentSwapchainImage();barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            vkCmdPipelineBarrier(native->GetVulkanCommandBuffer()->getHandle(),VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        }
        command->end();const auto submitted=device->presentFrame();const auto presentReturn=Nanos();
        if(submitted.submitted){session->OnSubmitted(submitted.token);if(screenshot)session->Diagnostics().OnScreenshotSubmitted(submitted.token);}
        if(screenshot&&submitted.submitted){StudioPreviewIdentity identity;uint64_t id;{std::lock_guard lock(mutex);identity=status.identity;id=status.captureId;}captureIdentityJSON="{\"requestId\":"+std::to_string(identity.requestId)+",\"projectGeneration\":"+std::to_string(identity.projectGeneration)+",\"resultGeneration\":"+std::to_string(identity.resultGeneration)+",\"executionId\":"+Quote(identity.executionId)+",\"projectDirectory\":"+Quote(UTF8(identity.projectDirectory))+",\"contentHash\":"+Quote(identity.contentHash)+",\"captureId\":"+std::to_string(id)+",\"submissionToken\":"+std::to_string(submitted.token.value)+",\"presentReturnNanoseconds\":"+std::to_string(presentReturn)+",\"presentResult\":"+std::to_string(submitted.presentResult)+"}";}
        if(!submitted.submitted)throw std::runtime_error("Preview command submission failed");
        if(submitted.status==EFrameResult::DeviceLost||submitted.status==EFrameResult::SurfaceLost)throw std::runtime_error("Preview presentation failed");
        if(submitted.status==EFrameResult::OutOfDate){device->recreateSwapchain(w,h);return;}
        if(submitted.submitted&&recorded.outputTexture){
            bool first;{std::lock_guard lock(mutex);first=status.submittedFrames==0;}
            const std::string diagnostics=first?session->Diagnostics().SnapshotJSON():std::string{};
            Publish([&](auto& s){s.state=StudioPreviewState::Ready;++s.submittedFrames;s.lastSubmissionToken=submitted.token.value;s.lastPresentReturnNanoseconds=presentReturn;s.lastPresentResult=submitted.presentResult;s.width=w;s.height=h;if(!diagnostics.empty())s.diagnosticsJSON=diagnostics;});
        }
    }
    bool captureRecorded=false;
    void Run() {
        for(;;){
            std::optional<StudioPreviewRequest> load;uint64_t token,value;bool stopping,draw,resetCamera;uint32_t w,h;float x,y,z;
            {std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(16));stopping=stop;token=retire;value=revision;
                if(token<=status.retiredToken&&!stopping){load=std::move(pending);pending.reset();}
                w=width;h=height;draw=visible&&w&&h;x=dx;y=dy;z=wheel;resetCamera=reset;dx=dy=wheel=0;reset=false;
            }
            try{
                if(stopping||token>SnapshotRetired()){
                    Publish([](auto& s){s.state=StudioPreviewState::Retiring;});Retire();
                    Publish([&](auto& s){s.retiredToken=std::max(s.retiredToken,token);s.state=stopping?StudioPreviewState::Stopped:StudioPreviewState::Unloaded;});
                    if(stopping)return;continue;
                }
                if(load)Load(std::move(*load),value);
                if(!Cancelled(value)&&draw)Frame(w,h,x,y,z,resetCamera);
            }catch(const std::exception& e){const std::string error=e.what();Retire();if(!Cancelled(value))Publish([&](auto& s){s.state=StudioPreviewState::Failed;s.error=error;});}
            catch(...){Retire();Publish([](auto& s){s.state=StudioPreviewState::Failed;s.error="Unexpected preview failure";});}
        }
    }
    uint64_t SnapshotRetired(){std::lock_guard lock(mutex);return status.retiredToken;}
};

StudioPreviewHost::StudioPreviewHost(HWND child,HWND target,UINT message):impl(std::make_unique<Impl>(child,target,message)){}
StudioPreviewHost::~StudioPreviewHost(){RequestStop();if(impl->owner.joinable())impl->owner.join();}
void StudioPreviewHost::RequestLoad(StudioPreviewRequest request){std::lock_guard lock(impl->mutex);if(impl->stop||impl->retire>impl->status.retiredToken)return;if(impl->preparation)impl->preparation->RequestCancel();impl->pending=std::move(request);++impl->revision;impl->changed.notify_one();}
void StudioPreviewHost::RequestRetire(uint64_t token){std::lock_guard lock(impl->mutex);if(impl->stop)return;impl->retire=std::max(impl->retire,token);impl->pending.reset();if(impl->preparation)impl->preparation->RequestCancel();++impl->revision;impl->changed.notify_one();}
void StudioPreviewHost::Resize(uint32_t w,uint32_t h,bool visible){std::lock_guard lock(impl->mutex);impl->width=std::min(w,16384u);impl->height=std::min(h,16384u);impl->visible=visible;impl->changed.notify_one();}
void StudioPreviewHost::Orbit(float x,float y){if(!std::isfinite(x)||!std::isfinite(y))return;std::lock_guard lock(impl->mutex);impl->dx=std::clamp(impl->dx+x,-2.f,2.f);impl->dy=std::clamp(impl->dy+y,-2.f,2.f);}
void StudioPreviewHost::Zoom(float steps){if(!std::isfinite(steps))return;std::lock_guard lock(impl->mutex);impl->wheel=std::clamp(impl->wheel+steps,-12.f,12.f);}
void StudioPreviewHost::ResetView(){std::lock_guard lock(impl->mutex);impl->reset=true;impl->dx=impl->dy=impl->wheel=0;}
bool StudioPreviewHost::RequestCapture(uint64_t id,std::filesystem::path png,std::filesystem::path metadata){std::lock_guard lock(impl->mutex);if(impl->stop||impl->capture||impl->status.state!=StudioPreviewState::Ready)return false;impl->capture=Impl::Capture{id,std::move(png),std::move(metadata)};impl->captureRecorded=false;impl->status.captureId=id;impl->status.capturePending=true;impl->status.captureSucceeded=false;impl->status.captureError.clear();return true;}
void StudioPreviewHost::RequestStop(){std::lock_guard lock(impl->mutex);impl->stop=true;impl->pending.reset();if(impl->preparation)impl->preparation->RequestCancel();++impl->revision;impl->changed.notify_one();}
bool StudioPreviewHost::IsStopped()const{std::lock_guard lock(impl->mutex);return impl->status.state==StudioPreviewState::Stopped;}
StudioPreviewStatus StudioPreviewHost::Snapshot()const{std::lock_guard lock(impl->mutex);impl->notificationPending.store(false);return impl->status;}
