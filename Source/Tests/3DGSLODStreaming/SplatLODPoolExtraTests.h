#pragma once
#include "Renderer/Splat/Streaming/SplatPageStreamer.h"
namespace SplatVerification {
inline void RunPoolExtra(Suite& suite,Device& d){
    auto fixture=[&](unsigned count){auto dir=Directory("partial-pool");std::vector<FSplatGaussian> gs(count);for(unsigned i=0;i<count;++i){gs[i].originalId=i;gs[i].position[0]=float(i)*.1f;gs[i].position[2]=-5;gs[i].scale[0]=gs[i].scale[1]=gs[i].scale[2]=.1f;}FSplatLODBuildConfig cfg;cfg.sourceSHA256=std::string(64,'1');cfg.builderHash=std::string(64,'2');FSplatSceneManifest m;std::string error;FSplatLODBuilder builder;Require(builder.Build(gs,0,cfg,(dir/"scene").string(),m,error),error);return std::make_pair(dir/"scene",m);};
    auto result=[&](const auto& f){FSplatPageResult r;r.pageId=0;r.sceneGeneration=1;r.bytes=Read(f.first/f.second.pages[0].relativePath);std::string error;Require(FSplatSceneFormat::DecodePage(f.second,0,r.bytes,r.decodedGaussians,error),error);return r;};
    suite.Test("T09 partial staging retains payload lease until all rows copied",[&]{auto f=fixture(4);FSplatStreamingConfig cfg;cfg.sceneGPUBytes=4096;cfg.stagingBytes=64;cfg.maxUploadBytesPerFrame=64;FSplatGPUPagePool p;std::string e;Require(p.Initialize(d.rhi,f.second,cfg,e),e);Require(p.TryQueueUpload(result(f),e),e);for(unsigned i=0;i<4;++i){auto c=d.Begin();p.Tick(c,1);Require(!p.GetResidency().IsPageReady(0),"partial/unsubmitted page became ready");Require(p.GetStats().stagingBytes<=64,"upload chunk exceeded registered cap");if(i<3)Require(p.GetPendingCPUBytes()>0&&p.DrainReleasedTransfers().empty(),"partial payload CPU ownership prematurely destroyed");auto token=d.Submit(c);p.MarkFrameSubmitted(token);}Require(p.GetPendingCPUBytes()==0&&p.DrainReleasedTransfers()==std::vector<std::pair<uint32_t,uint64_t>>{{0,1}},"last chunk did not close payload ownership");auto c=d.Begin();p.Tick(c,1);d.Submit(c);Require(p.GetResidency().IsPageReady(0)&&p.GetStats().uploadedPages==1&&p.GetStats().uploadedBytes==256,"completion or byte accounting mismatch");p.Shutdown();});
    suite.Test("T09 discarded submission replays retained staging with real new token",[&]{auto f=fixture(1);FSplatStreamingConfig cfg;cfg.sceneGPUBytes=4096;cfg.stagingBytes=64;cfg.maxUploadBytesPerFrame=64;FSplatGPUPagePool p;std::string e;Require(p.Initialize(d.rhi,f.second,cfg,e),e);Require(p.TryQueueUpload(result(f),e),e);auto old=d.rhi->getLastSubmissionToken();auto c=d.Begin();p.Tick(c,1);c->end();Require(d.rhi->getLastSubmissionToken().value==old.value&&!p.GetResidency().IsPageReady(0),"recording falsely became submitted");p.MarkFrameSubmitted(old);c=d.Begin();p.Tick(c,1);auto token=d.Submit(c);p.MarkFrameSubmitted(token);c=d.Begin();p.Tick(c,1);d.Submit(c);Require(p.GetResidency().IsPageReady(0)&&p.GetStats().uploadedBytes==64&&p.GetStats().uploadedPages==1,"discarded chunk not replayed exactly once");p.Shutdown();});
    suite.Test("T08 T09 GPU-pending duplicate is rejected safely and counter remains explicit",[&]{auto f=fixture(1);FSplatStreamingConfig cfg;cfg.sceneGPUBytes=4096;cfg.stagingBytes=64;cfg.maxUploadBytesPerFrame=64;FSplatGPUPagePool p;std::string e;Require(p.Initialize(d.rhi,f.second,cfg,e),e);Require(p.TryQueueUpload(result(f),e),e);auto c=d.Begin();p.Tick(c,1);Require(!p.GetResidency().IsPageReady(0)&&p.GetPendingCPUBytes()==0,"fixture did not enter GPU-pending after CPU destruction");auto duplicate=result(f);Require(!p.TryQueueUpload(std::move(duplicate),e)&&e=="Duplicate page upload","GPU-pending duplicate admission corrupted slot");Require(!duplicate.decodedGaussians.empty()&&p.GetStats().rejectedUploads==1,"rejected ownership/counter lost");auto token=d.Submit(c);p.MarkFrameSubmitted(token);c=d.Begin();p.Tick(c,1);d.Submit(c);Require(p.GetResidency().IsPageReady(0)&&p.GetStats().uploadedPages==1,"duplicate rejection prevented eventual readiness");p.Shutdown();});
    suite.Test("P0 actual pool first use excludes fallback and rejected tokens across reselection",[&]{
        namespace Profile=MonsterEngine::Profiling;const bool was=Profile::IsEnabled();Profile::SetEnabled(true);
        struct Restore {bool was;~Restore(){Profile::SetEnabled(was);}} restore{was};
        auto f=fixture(1);FSplatStreamingConfig cfg;cfg.sceneGPUBytes=4096;cfg.stagingBytes=64;cfg.maxUploadBytesPerFrame=64;
        FSplatGPUPagePool p;std::string error;Require(p.Initialize(d.rhi,f.second,cfg,error),error);
        auto sink=std::make_shared<FSplatDetailDiagnostics>();p.SetDetailDiagnostics(sink);
        Profile::Context origin{};origin.sceneId=1;origin.frameId=10;origin.requestId=99;
        sink->BindFrame(origin,2,3,4);auto upload=result(f);upload.profiling=origin;
        Require(p.TryQueueUpload(std::move(upload),error),error);auto c=d.Begin();p.Tick(c,1,origin);
        auto token=d.Submit(c);p.MarkFrameSubmitted(token,false);c=d.Begin();p.Tick(c,1,origin);d.Submit(c);
        Require(p.GetResidency().IsPageReady(0),"real upload completion required");
        FSplatLODSelection cut;cut.selectedNodeIds={0};cut.selectionVersion=7;
        std::vector<FSplatDetailEvent> rows;uint64_t overwritten=0,dropped=0;
        auto drain=[&]{rows.clear();sink->Drain(rows,256,overwritten,dropped);};
        auto firstUses=[&]{return std::count_if(rows.begin(),rows.end(),[](const auto& r){return r.stage==ESplatDetailStage::FirstUse;});};
        Require(p.BuildActiveSet(cut).sourceIndices.size()==1,"resident active fixture");p.MarkFrameSubmitted({});p.MarkFrameSubmitted(token);drain();
        Require(firstUses()==0&&p.GetAllocationStats().usedSelectedPages==0,"invalid or old token became consumption");
        c=d.Begin();token=d.Submit(c);p.MarkFrameSubmitted(token,false);drain();
        Require(firstUses()==0&&p.GetAllocationStats().usedSelectedPages==0,"fallback became first use");
        p.BuildActiveSet(cut);c=d.Begin();token=d.Submit(c);p.MarkFrameSubmitted(token,true);drain();
        Require(firstUses()==1&&p.GetAllocationStats().usedSelectedPages==1,"first real consumption missing");
        auto first=*std::find_if(rows.begin(),rows.end(),[](const auto& r){return r.stage==ESplatDetailStage::FirstUse;});
        Require(first.context.submissionId==token.value&&first.context.requestId==99&&first.context.sceneId==1,"consumption cause/token lost");
        auto proof=p.GetAllocationStats();Require(proof.consumedCutHash.size()==64&&proof.consumedCutFrameId==10&&proof.consumedCutSubmissionToken==token.value,"actual successful pool cut fingerprint missing");
        const auto fingerprint=proof.consumedCutHash;Profile::SetEnabled(false);p.BuildActiveSet(cut);c=d.Begin();token=d.Submit(c);p.MarkFrameSubmitted(token,true);
        Require(p.GetAllocationStats().consumedCutHash==fingerprint&&p.GetAllocationStats().consumedCutSubmissionToken==token.value,"profiling-off changed successful cut proof");Profile::SetEnabled(true);
        auto changed=cut;changed.selectionVersion=8;p.BuildActiveSet(changed);c=d.Begin();token=d.Submit(c);p.MarkFrameSubmitted(token,true);
        Require(p.GetAllocationStats().consumedCutHash!=fingerprint,"same counts different native cut version reused fingerprint");
        p.BuildActiveSet(cut);Require(p.GetAllocationStats().consumedCutHash.empty(),"construction exposed previous submission proof");
        p.BuildActiveSet(cut);c=d.Begin();token=d.Submit(c);p.MarkFrameSubmitted(token);drain();Require(firstUses()==0,"continuous selection repeated first use");
        FSplatLODSelection empty;p.BuildActiveSet(empty);p.BuildActiveSet(cut);c=d.Begin();token=d.Submit(c);p.MarkFrameSubmitted(token);drain();
        Require(firstUses()==1,"resident reselection lost new demand first use");
        auto second=*std::find_if(rows.begin(),rows.end(),[](const auto& r){return r.stage==ESplatDetailStage::FirstUse;});
        Require(second.pageLifetime==first.pageLifetime&&second.demandSequence!=first.demandSequence,"resident demand reuse lost lifetime distinction");
        p.Shutdown();
    });

}
}
