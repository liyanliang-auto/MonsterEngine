#pragma once
#include <sstream>
#include <regex>
#include <set>
namespace SplatVerification {
inline std::vector<std::map<std::string,std::string>> CSVRows(const fs::path& path){
    std::ifstream file(path);Require(bool(file),"CSV artifact unavailable");std::string line,field;Require(bool(std::getline(file,line)),"CSV header missing");if(!line.empty()&&line.back()=='\r')line.pop_back();std::vector<std::string> headers;std::stringstream header(line);while(std::getline(header,field,','))headers.push_back(field);std::vector<std::map<std::string,std::string>> rows;
    while(std::getline(file,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;std::stringstream values(line);std::map<std::string,std::string> row;unsigned index=0;while(std::getline(values,field,',')){Require(index<headers.size(),"CSV excess columns");row.emplace(headers[index++],field);}Require(index==headers.size(),"CSV missing columns");rows.push_back(std::move(row));}return rows;
}
inline void RunRound3Integration(Suite& suite){
    suite.Test("R3 T13 actual app ten hardware scopes join original frame IDs",[]{
        Fixture fixture;auto options=fixture.Runtime('D');TimeoutScene app(options);app.SetControlUIVisible(false);auto result=app.run();app.shutdown();Require(result==0&&!app.timeout&&!app.HasRunFailed(),"hardware scope app failed");
        auto directory=fs::path(options.outputDirectory);auto rows=CSVRows(directory/"frames.csv"),samples=CSVRows(directory/"gpu_samples.csv");Require(!rows.empty(),"actual app recorded no frames");std::map<uint64_t,std::map<std::string,double>> hardware;
        for(auto& sample:samples){auto id=std::stoull(sample.at("frame"));Require(hardware[id].emplace(sample.at("stage"),std::stod(sample.at("gpu_ms"))).second,"duplicate scope on original frame ID");}
        uint64_t uploadBytes=0;double presentation=0;std::set<uint64_t> frames;
        for(auto& row:rows){auto id=std::stoull(row.at("frame"));Require(frames.insert(id).second&&row.at("gpu_available")=="1"&&row.at("count_available")=="1","missing/duplicate GPU frame or count join");const auto& scopes=hardware.at(id);Require(scopes.size()==10,"actual app hardware scope set incomplete");for(auto stage:{"WholeFrame","Upload","Presentation","Pipeline","Preprocess","Scan","AssignKeys","RadixSort","TileBounds","Render"})Require(scopes.count(stage)&&std::isfinite(scopes.at(stage))&&scopes.at(stage)>=0,"hardware scope unavailable/nonfinite");
            Require(std::stod(row.at("gpu_ms"))==scopes.at("WholeFrame")&&std::stod(row.at("pipeline_ms"))==scopes.at("Pipeline")&&std::stod(row.at("upload_ms"))==scopes.at("Upload")&&std::stod(row.at("presentation_ms"))==scopes.at("Presentation"),"aggregate relabeled compute/CPU/incorrect original frame sample");Require(scopes.at("WholeFrame")+.001>=std::max({scopes.at("Upload"),scopes.at("Presentation"),scopes.at("Pipeline")}),"whole frame does not enclose application GPU intervals");
            Require(std::stoull(row.at("allocator_available_bytes"))==std::stoull(row.at("allocator_pooled_reserved_bytes"))-std::stoull(row.at("allocator_pooled_used_bytes"))&&std::stoull(row.at("allocator_bytes"))==std::stoull(row.at("allocator_pooled_used_bytes"))+std::stoull(row.at("allocator_dedicated_committed_bytes")),"frame allocator categories inconsistent");uploadBytes=std::max<uint64_t>(uploadBytes,std::stoull(row.at("uploaded_bytes")));presentation=std::max(presentation,scopes.at("Presentation"));}
        Require(uploadBytes>0&&presentation>0,"actual frame did not upload and present");Require(hardware.size()==frames.size(),"hardware samples silently joined synthetic frame IDs");auto bytes=Read(directory/"metadata.json");std::string meta(bytes.begin(),bytes.end());Require(std::regex_search(meta,std::regex("\"missingGPUFrames\"\\s*:\\s*0"))&&std::regex_search(meta,std::regex("\"lateGPUSamples\"\\s*:\\s*0")),"terminal hardware scope drain missing events");
    });
    suite.Test("R3 T13 pipeline-only events cannot satisfy whole-frame evidence",[]{
        auto options=Options();FSplatLODBenchmark recorder;std::string error;Require(recorder.Begin(options,{},error),error);FSplatLODBenchmarkFrame row;row.frame=77;row.sampled=true;row.wholeFrameTimingRequired=true;row.pipeline.countSamples={{77,1,false}};for(auto stage:{"Pipeline","Preprocess","Scan","AssignKeys","RadixSort","TileBounds","Render"})row.pipeline.timestampSamples.push_back({77,stage,1});Require(recorder.Record(row,error),error);Require(!recorder.Finish(true,"unit synthetic event fixture only",error),"missing application scopes falsely accepted as whole GPU evidence");
    });
}
}
