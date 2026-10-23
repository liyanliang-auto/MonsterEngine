#pragma once
#include "Renderer/Splat/SplatViewerDiagnostics.h"
#include "Renderer/Splat/SplatPipeline.h"
#include "Renderer/Splat/SplatStreamingController.h"
#include <fstream>
#include <sstream>
namespace {
void RunViewerMeasurementTests(SplatVerification::Suite& suite) {
    using namespace MonsterRender::Splat;
    auto make=[](uint64_t frame,uint64_t generation=1) { FViewerDiagnosticsSnapshot s; s.frameId=frame;s.sceneGeneration=1;s.surfaceGeneration=1;s.configGeneration=1;s.measurement.pipelineGeneration=generation;s.measurement.ownerStartNanoseconds=frame*100000000;s.measurement.sortStatus=0;s.measurement.gpuStatus.fill(0);return s; };
    auto write=[](const char* name,FSplatViewerDiagnostics& d) { const auto directory=SplatVerification::Directory("viewer-measurement-contract");std::ofstream(directory/(std::string(name)+".json"))<<d.SnapshotJSON();std::ofstream(directory/(std::string(name)+".csv"))<<d.FramesCSV(); };
    suite.Test("M01 original GPU origin resolves behind newer CPU row",[&] {
        FSplatViewerDiagnostics d(3);auto a=make(101),b=make(102);a.counters.gpuMilliseconds=99;a.counters.gpuTimingFrame=77;a.counters.sortEntries=88;a.counters.sortCountFrame=77;
        d.Record(a,nullptr,{201},100000000);d.Record(b,nullptr,{202},150000000);write("pending",d);
        FSplatPipelineFrameStats stats;stats.timestampSamples={{101,"Render",0},{101,"Pipeline",0}};stats.countSamples={{101,0,false}};d.Resolve(1,stats);write("resolved",d);
        const auto csv=d.FramesCSV();std::istringstream lines(csv);std::string header,first,second;std::getline(lines,header);std::getline(lines,first);std::getline(lines,second);
        Require(first.find("101,1,1,1,201,,0,101,")==0,"count did not resolve originating row");Require(second.find("102,1,1,1,202,,,,")==0,"old count leaked into newest row");
        const auto json=d.SnapshotJSON();Require(json.find("\"gpuTimingFrame\":101")!=std::string::npos,"GPU aggregate lost origin");Require(json.find("\"gpuRenderMilliseconds\":0")!=std::string::npos,"measured zero missing");Require(json.find("\"gpuMilliseconds\":99")==std::string::npos,"delayed input aggregate copied");
    });
    suite.Test("M02 pipeline lifetime isolates repeated recording IDs",[&] {
        FSplatViewerDiagnostics d(3);d.Record(make(1,1),nullptr,{1});d.Record(make(1,2),nullptr,{2});FSplatPipelineFrameStats stats;stats.countSamples={{1,7,false}};d.Resolve(2,stats);write("generation",d);
        const auto csv=d.FramesCSV();std::istringstream lines(csv);std::string line;std::getline(lines,line);std::getline(lines,line);Require(line.find("1,1,1,1,1,,,,")==0,"new pipeline resolved old row");std::getline(lines,line);Require(line.find("1,1,1,1,2,,7,1,")==0,"new pipeline count unresolved");
    });
    suite.Test("M03 bounded retention names unresolved and unmatched loss",[&] {
        FSplatViewerDiagnostics d(1);d.Record(make(1),nullptr,{1});d.Record(make(2),nullptr,{2});FSplatPipelineFrameStats stats;stats.countSamples={{1,7,false}};d.Resolve(1,stats);const auto json=d.SnapshotJSON();Require(json.find("\"droppedFrames\":1")!=std::string::npos,"eviction not counted");Require(json.find("\"unresolvedOverwrittenFrames\":1")!=std::string::npos,"unresolved eviction not counted");Require(json.find("\"unmatchedGPURecords\":1")!=std::string::npos,"late discarded origin not counted");write("retention",d);
    });
    suite.Test("M04 unsupported differs from measured zero and invalid interval",[&] {
        FSplatViewerDiagnostics d(2);auto a=make(1);a.measurement.gpuStatus.fill(2);d.Record(a,nullptr,{1});FSplatPipelineFrameStats stats;stats.timestampSamples={{1,"Render",std::numeric_limits<double>::quiet_NaN()}};d.Resolve(1,stats);const auto json=d.SnapshotJSON();Require(json.find("\"gpuRenderStatus\":3")!=std::string::npos,"invalid interval looks valid");Require(json.find("\"gpuRenderMilliseconds\":null")!=std::string::npos,"invalid interval replaced with zero");Require(json.find("\"gpuScanStatus\":2")!=std::string::npos,"unsupported lost");write("unsupported",d);
    });
    suite.Test("M05 lifecycle reset breaks CPU and present cadence",[&] {
        FSplatViewerDiagnostics d(3);d.Record(make(1),nullptr,{1},100000000);d.Record(make(2),nullptr,{2},200000000);d.ResetCadence();d.Record(make(100),nullptr,{3},10000000000);write("pause",d);const auto json=d.SnapshotJSON();const auto latest=json.substr(json.find("\"latest\":"),json.find("\"frames\":"));Require(latest.find("\"cpuFrameMilliseconds\":null")!=std::string::npos,"pause generated stale CPU interval");Require(latest.find("\"presentReturnCadenceMilliseconds\":null")!=std::string::npos,"pause generated stale present interval");
    });
    suite.Test("M06 streaming values require matching frame and scene",[&] {
        FSplatViewerDiagnostics d(2);FSplatStreamingControllerSnapshot stream;stream.frameId=2;stream.sceneGeneration=1;d.Record(make(1),&stream,{1});Require(d.SnapshotJSON().find("\"streaming\":null")!=std::string::npos,"mismatched streaming attached");write("stream-mismatch",d);
    });
    suite.Test("P0 independent causes redact private unknown prose and retain simultaneous causes",[&] {
        FSplatViewerDiagnostics d(2);auto s=make(1);s.degraded=true;
        s.pipelineDegradation={"observed",{"D:/private/model.ply","active_capacity_exceeded"},{}};
        s.streamingDegradation={"observed",{"transition_reserve_refused"},{}};
        d.Record(s,nullptr,{1});const auto json=d.SnapshotJSON();
        Require(json.find("D:/private")==std::string::npos,"private reason leaked");
        for(auto code:{"unknown_reason","active_capacity_exceeded","transition_reserve_refused"})Require(json.find(code)!=std::string::npos,"independent cause missing");
        write("p0-causes",d);
    });
    suite.Test("P0 observed nondegraded differs from unavailable and stale identity",[&] {
        FSplatViewerDiagnostics d(3);auto s=make(1);s.pipelineDegradation.status="observed";
        s.streamingDegradation.status="stale_identity";d.Record(s,nullptr,{1});auto json=d.SnapshotJSON();
        Require(json.find("\"pipeline\":{\"status\":\"observed\",\"codes\":[]")!=std::string::npos,"empty observed cause lost");
        Require(json.find("\"streaming\":{\"status\":\"stale_identity\",\"codes\":[\"unknown_reason\"]")!=std::string::npos,"stale became empty success");
        Require(json.find("\"admission\":{\"status\":\"unknown\"")!=std::string::npos,"missing admission invented");
        Require(json.find("\"driverBackingBytes\":null")!=std::string::npos,"unknown physical allocation invented");write("p0-unknown",d);
    });
    suite.Test("P0 detail ring bounds deltas and preserves evicted origin as unknown",[&] {
        namespace Profile=MonsterEngine::Profiling;
        const bool enabled=Profile::IsEnabled();Profile::SetEnabled(true);
        struct Restore{bool value;~Restore(){Profile::SetEnabled(value);}} restore{enabled};
        FSplatDetailDiagnostics detail;Profile::Context origin{};origin.sceneId=2;origin.frameId=1;origin.requestId=11;
        detail.BindFrame(origin,3,4,5);detail.Emit(ESplatDetailStage::Demand,origin,1,nullptr,7,9,1);
        std::vector<FSplatDetailEvent> rows;uint64_t overwritten=0,dropped=0;
        detail.Drain(rows,16,overwritten,dropped);Require(rows.size()==1&&rows[0].surfaceGeneration==3,"exact origin missing");
        for(uint64_t i=2;i<=65;++i){auto context=origin;context.frameId=i;context.requestId=i+10;detail.BindFrame(context,3,4,5);}
        for(unsigned i=0;i<300;++i)detail.Emit(ESplatDetailStage::ReadComplete,origin,2,nullptr,7,10,2);
        rows.clear();detail.Drain(rows,16,overwritten,dropped);
        Require(rows.size()==16&&overwritten==44&&dropped==0,"bounded pending history accounting mismatch");
        Require(!rows.front().surfaceGeneration&&rows.front().context.frameId==1,"evicted origin relabeled with export identity");
        Require(rows.front().pageLifetime==2&&rows.front().demandSequence==2,"physical and demand lifetimes lost");
        auto sequence=rows.back().sequence;rows.clear();detail.Drain(rows,16,overwritten,dropped);
        Require(rows.front().sequence==sequence+1&&overwritten==44,"delta drain repeated history or summed loss");
    });
    suite.Test("P0 controlled native Config exports SH3 float32 identity and consumed cut",[&]{
        // Unit-controlled serializer contract, not a live loaded-GPU reference.
        FSplatViewerDiagnostics d(2);auto s=make(17,5);
        s.sceneGeneration=2;s.surfaceGeneration=3;s.configGeneration=4;
        s.contentHash="007121454769ff9d816c37af1f0381f12bfea8179bb10c82b34f42cca381c816";
        s.requestedConfig.generation=4;s.requestedConfig.configHash=std::string(64,'c');
        s.requestedConfig.shaderIdentity=std::string(64,'d');
        s.requestedConfig.runtime.displaySHDegree=3;s.requestedConfig.runtime.shStorageMode=0;
        s.requestedConfig.runtime.lod.qualityPixels=.1f;s.effectiveConfig=s.requestedConfig;
        s.renderWidth=1876;s.renderHeight=1105;s.status.ok=true;s.status.code="ok";
        s.budget.limitBytes=805306368;s.budget.admissionSequence=1;s.budget.admissionGranted=true;
        s.budget.admissionReason="admitted";
        FSplatStreamingControllerSnapshot stream;stream.frameId=stream.submittedFrameId=17;
        stream.sceneGeneration=2;stream.surfaceGeneration=3;stream.configGeneration=4;stream.pipelineGeneration=5;
        stream.lastSubmission={31};stream.rootReady=true;stream.loading=false;
        stream.activeCount=23;stream.selectionVersion=7;stream.residencyVersion=9;
        stream.maxSelectedProxyErrorPixels=s.requestedConfig.runtime.lod.qualityPixels;
        stream.allocations.selectedPageCount=stream.allocations.usedSelectedPages=3;
        stream.allocations.consumedCutHash=std::string(64,'e');stream.allocations.consumedCutFrameId=17;stream.allocations.consumedCutSubmissionToken=31;
        stream.allocations.transitionAdmitted=true;d.Record(s,&stream,{31});
        const auto json=d.SnapshotJSON();
        Require(json.find("\"displaySHDegree\":3")!=std::string::npos,"SH3 producer missing");
        Require(json.find("\"qualityPixels\":0.10000000149011612")!=std::string::npos,"native float32 precision lost");
        write("p0-controlled-config",d);
    });
    suite.Test("P0 full diagnostic ring exports bounded JSON for transport verification",[&] {
        FSplatViewerDiagnostics d(256);
        for(uint64_t i=1;i<=256;++i){auto s=make(i);s.effectiveConfig=s.requestedConfig;
            s.pipelineDegradation.status="observed";s.streamingDegradation.status="observed";
            d.Record(s,nullptr,{i});}
        const auto json=d.SnapshotJSON();Require(json.size()<=8388608,"snapshot exceeded byte cap");write("p0-full-ring",d);
    });
    suite.Test("P0 export omissions preserve newest rows and distinct CSV origins",[&]{
        FSplatViewerDiagnostics d(512);
        for(uint64_t i=1;i<=512;++i){auto snapshot=make(i);FSplatStreamingControllerSnapshot stream;
            stream.frameId=i;stream.sceneGeneration=1;stream.surfaceGeneration=stream.configGeneration=stream.pipelineGeneration=1;
            snapshot.effectiveConfig=snapshot.requestedConfig;
            for(unsigned n=0;n<32;++n){snapshot.pipelineDegradation.counters["p0_observed_diagnostic_counter_"+std::to_string(n)]=UINT64_MAX;snapshot.streamingDegradation.counters["p0_observed_diagnostic_counter_"+std::to_string(n)]=UINT64_MAX;}
            for(unsigned n=0;n<16;++n){FSplatDetailEvent event;event.sequence=i*16+n;event.timestampNanoseconds=UINT64_MAX;
                event.context.sceneId=1;event.context.frameId=i;event.context.requestId=UINT64_MAX;event.context.pageId=n;
                event.context.submissionId=i;event.demandFrameId=i;event.demandRequestId=UINT64_MAX;
                event.pageLifetime=UINT64_MAX;event.selectionVersion=UINT64_MAX;event.residencyVersion=UINT64_MAX;event.demandSequence=UINT64_MAX;
                event.surfaceGeneration=event.configGeneration=event.pipelineGeneration=UINT64_MAX;
                event.demandSurfaceGeneration=event.demandConfigGeneration=event.demandPipelineGeneration=UINT64_MAX;stream.detailEvents.push_back(event);}
            d.Record(snapshot,&stream,{i});}
        auto json=d.SnapshotJSON();Require(json.size()<=8388608,"8MiB diagnostic bound violated");
        Require(json.find("\"exportOmittedFrames\":0,")==std::string::npos,"oversized export did not disclose omissions");
        Require(json.find("\"frameId\":512,")!=std::string::npos,"newest diagnostic row omitted");
        Require(json.find("\"droppedFrames\":0,")!=std::string::npos,"export omission confused with ring eviction");
        std::istringstream csv(d.FramesCSV());std::string line;unsigned count=0;while(std::getline(csv,line))++count;
        Require(count==513,"CSV silently adopted JSON export omissions");write("p0-export-omissions",d);
    });

    suite.Test("L01 native upstream fixture preserves accepted cause through serializer",[&]{
        namespace Profile=MonsterEngine::Profiling;const bool was=Profile::IsEnabled();Profile::SetEnabled(true);
        struct Restore{bool value;~Restore(){Profile::SetEnabled(value);}} restore{was};
        FSplatDetailDiagnostics detail;Profile::Context context{};context.sceneId=1;context.pageId=1;context.frameId=17;context.requestId=41;context.submissionId=31;
        detail.BindFrame(context,1,1,1);FSplatUpstreamOrigin origin;origin.id=101;origin.context=context;origin.surface=origin.config=origin.pipeline=1;origin.representationFlags=1;
        FSplatPageCausality work;work.origin=origin;work.workId=201;
        auto demand=context;demand.submissionId=0;
        detail.Emit(ESplatDetailStage::UpstreamDemand,demand,0,nullptr,0,0,0,nullptr,&origin);
        for(auto stage:{ESplatDetailStage::Queued,ESplatDetailStage::ReadStarted,ESplatDetailStage::ReadComplete,ESplatDetailStage::Validated,ESplatDetailStage::CPUReady,ESplatDetailStage::ReadyTransferred,ESplatDetailStage::UploadRecorded,ESplatDetailStage::UploadSubmitted,ESplatDetailStage::GPUComplete,ESplatDetailStage::ActiveReady,ESplatDetailStage::FirstUse})
            detail.Emit(stage,context,301,nullptr,401,501,601,&work,&origin);
        FSplatStreamingControllerSnapshot stream;stream.frameId=17;stream.sceneGeneration=1;stream.surfaceGeneration=stream.configGeneration=stream.pipelineGeneration=1;uint64_t overwritten=0,dropped=0;detail.Drain(stream.detailEvents,16,overwritten,dropped);
        Require(stream.detailEvents.size()==12&&stream.detailEvents.back().upstream.id==101&&stream.detailEvents.back().workId==201,"immutable native work carrier lost");
        FSplatViewerDiagnostics diagnostics(2);diagnostics.Record(make(17),&stream,{31});write("lifecycle-v2-native",diagnostics);
        Profile::SetEnabled(false);detail.Emit(ESplatDetailStage::FirstUse,context,301,nullptr,401,501,601,&work,&origin);
        std::vector<FSplatDetailEvent> disabled;detail.Drain(disabled,16,overwritten,dropped);Require(disabled.empty(),"disabled capture emitted rows");
        Require(work.origin.id==101&&work.workId==201,"profiling off changed causal carrier");
    });
}
}
