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
}
}
