#pragma once
#include "Renderer/Splat/SplatViewerSession.h"
namespace SplatVerification {
inline void RunCameraProvenanceTests(Suite& suite,Device& d) {
    struct Fixture {
        Device& d;FSplatBudgetCoordinator account{805306368};FFileResourceProvider provider{"."};FSplatViewerSession session;FViewerConfig config;
        uint64_t id=0;FViewerFrameRequest request;
        Fixture(Device& device):d(device) {
            config.generation=1;config.maximumInternalEdge=64;config.maximumInternalPixels=4096;
            config.runtime.activeCapacity=8;config.runtime.sortCapacity=4096;
            config.runtime.streaming.temporaryGPUBytes=64ull<<20;
            session.SetBudgetCoordinator(account);Require(session.Initialize(*d.rhi,provider,config).ok,"camera fixture initialize");
            FSceneDescriptor scene;scene.sceneId="camera-proof";scene.generation=1;
            auto data=Source(d);
            for(auto* buffer:{&data.positions,&data.scales,&data.rotations,&data.opacities,&data.shCoefficients}) {
                BufferDesc desc;desc.size=(*buffer)->getSize();desc.usage=EResourceUsage::StorageBuffer;desc.memoryUsage=EMemoryUsage::Upload;desc.dedicatedAllocation=true;
                desc.initialData=(*buffer)->map();desc.initialDataSize=desc.size;auto dedicated=d.rhi->createBuffer(desc);(*buffer)->unmap();Require(bool(dedicated),"dedicated source fixture");*buffer=dedicated;
            }
            const auto adopted=session.AdoptDesktopGPUData(data,scene,64,64,true);Require(adopted.ok,"camera fixture adopt: "+adopted.code+":"+adopted.message);
            request.sceneGeneration=request.surfaceGeneration=request.configGeneration=1;request.viewportWidth=request.viewportHeight=64;
        }
        FCameraInputState Input(bool reset=false,EViewerPointerPhase phase=EViewerPointerPhase::Move) {
            FCameraInputState input;input.requestScope=77;input.requestId=++id;input.surfaceGeneration=request.surfaceGeneration;
            input.phase=phase;input.reset=reset;input.lineageComplete=true;input.applicationGroup=id;
            input.contributorIds[0]=id;input.contributorCount=1;input.lastResetRequestId=reset?id:0;return input;
        }
        FViewerFrameResult Record(){request.commandList=d.Begin();request.monotonicNanoseconds=100+id;auto result=session.RecordCompute(request);if(!result.recorded)throw std::runtime_error("record: "+result.status.code+":"+result.status.message);return result;}
        FGPUCompletionToken Submit(){auto token=d.Submit(request.commandList);session.OnSubmitted(token);return token;}
        std::string JSON(){return session.Diagnostics().SnapshotJSON();}
        ~Fixture(){session.Shutdown();}
    };
    suite.Test("CAMERA actual consumed reset binds successful real frame token and exact uniform bytes",[&]{
        Fixture f(d);auto reset=f.Input(true);Require(f.session.ApplyCameraInput(reset)==EViewerCameraInputDisposition::AppliedMotion,"reset not applied");
        auto recorded=f.Record();Require(recorded.recorded&&recorded.outputTexture,"compute not recorded");
        Require(f.JSON().find("\"status\":\"consumed\"")==std::string::npos,"recording claimed success");
        auto token=f.Submit();const auto json=f.JSON();Require(json.find("\"status\":\"consumed\"")!=std::string::npos,"submitted reset missing");
        Require(json.find("\"lastResetRequestId\":1")!=std::string::npos&&json.find("\"contributorIds\":[1]")!=std::string::npos,"specific reset lineage missing");
        Require(json.find("\"submissionToken\":"+std::to_string(token.value))!=std::string::npos,"real token missing");
        std::ofstream(Directory("camera-provenance")/"camera-real-consumed.json")<<json;
        const auto published=f.session.Snapshot()->cameraProvenance;Require(!published.recorded&&!published.submissionToken,"unsent Publish inherited success");
    });
    suite.Test("CAMERA actual frozen compute remains earlier pose after later unsent input",[&]{
        Fixture f(d);f.session.ApplyCameraInput(f.Input(true));Require(f.Record().recorded,"record failed");
        auto move=f.Input();move.orbitX=.125f;f.session.ApplyCameraInput(move);f.Submit();auto json=f.JSON();
        Require(json.find("\"lastAppliedRequestId\":1")!=std::string::npos,"later live input relabeled frozen frame");
        Require(f.Record().recorded,"second record failed");f.Submit();json=f.JSON();Require(json.find("\"lastAppliedRequestId\":2")!=std::string::npos,"later input lost");
    });
    suite.Test("CAMERA actual zero unobserved duplicate and invalid-record submission never invent association",[&]{
        Fixture f(d);f.session.ApplyCameraInput(f.Input(true));Require(f.Record().recorded,"record failed");
        f.session.OnSubmitted({0});f.session.OnSubmitted({UINT64_MAX});f.Submit();
        Require(f.JSON().find("\"status\":\"consumed\"")==std::string::npos,"invalidated pending evidence revived");
        Require(f.Record().recorded,"second record failed");auto token=f.Submit();auto before=f.JSON();f.session.OnSubmitted(token);Require(before==f.JSON(),"duplicate token inserted row");
        Require(f.Record().recorded,"third record failed");auto invalid=f.request;invalid.commandList=nullptr;Require(!f.session.RecordCompute(invalid).recorded,"invalid request recorded");f.Submit();
        const auto after=f.JSON();Require(after.find("\"lastAppliedRequestId\":1")!=std::string::npos,"earlier origin erased");
    });
    suite.Test("CAMERA actual applied lifecycle stale cancel legacy and overflow dispositions",[&]{
        Fixture f(d);auto begin=f.Input(false,EViewerPointerPhase::Begin);Require(f.session.ApplyCameraInput(begin)==EViewerCameraInputDisposition::AppliedNoMotion,"Begin not no-motion");
        auto end=f.Input(false,EViewerPointerPhase::End);Require(f.session.ApplyCameraInput(end)==EViewerCameraInputDisposition::AppliedNoMotion,"End not no-motion");
        auto cancel=f.Input(false,EViewerPointerPhase::Cancel);Require(f.session.ApplyCameraInput(cancel)==EViewerCameraInputDisposition::CancelledNoMotion,"Cancel applied motion");
        Require(f.Record().recorded,"record failed");f.Submit();
        auto stale=f.Input();stale.surfaceGeneration=2;Require(f.session.ApplyCameraInput(stale)==EViewerCameraInputDisposition::StaleSurface,"stale applied");
        auto reset=f.Input(true);Require(f.session.ApplyCameraInput(reset)==EViewerCameraInputDisposition::AppliedMotion,"reset failed");
        auto overflow=f.Input();overflow.contributorCount=17;overflow.lineageComplete=false;
        Require(f.session.ApplyCameraInput(overflow)==EViewerCameraInputDisposition::LineageOverflow,"overflow looks complete");
        Require(f.Record().recorded,"overflow record failed");f.Submit();Require(f.JSON().find("unavailable_lineage_overflow")!=std::string::npos,"overflow became success");
        auto legacy=f.Input();legacy.requestId=legacy.requestScope=0;legacy.lineageComplete=false;
        Require(f.session.ApplyCameraInput(legacy)==EViewerCameraInputDisposition::UnavailableLegacy,"legacy invented identity");
    });
    suite.Test("CAMERA actual settings and surface transitions invalidate earlier camera scope",[&]{
        Fixture f(d);f.session.ApplyCameraInput(f.Input(true));f.config.generation=2;f.session.ApplySettings(f.config);f.request.configGeneration=2;
        Require(f.Record().recorded,"config record failed");f.Submit();Require(f.JSON().find("unavailable_scope_change")!=std::string::npos,"config changed scope inherited claim");
        f.session.ApplyCameraInput(f.Input(true));f.request.surfaceGeneration=2;
        Require(f.Record().recorded,"surface record failed");f.Submit();Require(f.JSON().find("unavailable_scope_change")!=std::string::npos,"surface changed scope inherited claim");
        f.session.ReleaseScene();Require(!f.session.Snapshot()->cameraProvenance.recorded,"release retained success");
    });
}
}
