// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatStreamingLODBuilder.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include "Renderer/Splat/LOD/SplatSceneValidation.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

using namespace MonsterRender::Splat;
namespace {
std::atomic<bool> Interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free);
void OnSignal(int) { Interrupted.store(true,std::memory_order_relaxed); }
std::string ReadConfig(const std::string& path){
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
    if(!file||file.tellg()<0||file.tellg()>65536)throw std::runtime_error("Cannot read bounded build config");
    std::string text(size_t(file.tellg()),'\0');file.seekg(0);
    if(!file.read(text.data(),text.size()))throw std::runtime_error("Build config read failed");return text;
}
void Usage(){std::cout<<
    "SplatMobileLODBuilder --input STABLE_SOURCE.ply --job-directory PRIVATE_JOB --output NEW_SCENE [--config numeric.json]\n"
    "Same source/config/tool resumes committed work automatically; source must remain immutable.\n"
    "Ctrl+C requests cooperative cancellation and retains durable work for explicit resume.\n"
    "Output: validated manifest v3 and page v1 files; independent mobile algorithm, no fusion.\n"
    "SplatMobileLODBuilder --validate-scene DIRECTORY\n";}
}
int main(int argc,char** argv){try{
    std::map<std::string,std::string> options;
    for(int i=1;i<argc;++i){const std::string key=argv[i];if(key=="--help"||key=="-h"){Usage();return 0;}
        if(key!="--input"&&key!="--job-directory"&&key!="--output"&&key!="--config"&&key!="--validate-scene")throw std::runtime_error("Unknown command option");
        if(i+1>=argc||!options.emplace(key,argv[++i]).second)throw std::runtime_error("Duplicate option or missing value");}
    if(options.contains("--validate-scene")){
        if(options.size()!=1)throw std::runtime_error("Validation accepts only --validate-scene DIRECTORY");
        FSplatSceneValidationResult result;std::string error;
        if(!FSplatSceneValidation::Validate(options.at("--validate-scene"),{},result,error))throw std::runtime_error(error);
        std::cout<<"{\"schemaVersion\":1,\"ok\":true,\"sourceRecords\":"<<result.sourceRecords<<",\"pageCount\":"<<result.pageCount
            <<",\"encodedPageBytes\":"<<result.encodedPageBytes<<",\"contentSHA256\":\""<<result.contentSHA256<<"\"}\n";return 0;
    }
    if(!options.contains("--input")||!options.contains("--job-directory")||!options.contains("--output")){Usage();return 2;}
    FSplatBoundedBuildConfig config;std::string error;
    if(options.contains("--config")&&!FSplatStreamingLODBuilder::ParseConfig(ReadConfig(options.at("--config")),config,error))throw std::runtime_error(error);
    // Exact executable identity. The Android host supplies its own native binary
    // identity on its worker, rather than claiming this host executable identity.
    const auto executable=std::filesystem::absolute(std::filesystem::u8path(argv[0]));
    const auto encoded=executable.u8string();const std::string executablePath(reinterpret_cast<const char*>(encoded.data()),encoded.size());
    if(!FSplatSceneFormat::SHA256File(executablePath,config.builderHash,error))throw std::runtime_error("Cannot identify actual builder executable: "+error);
    FSplatBuildControl control;std::signal(SIGINT,OnSignal);std::signal(SIGTERM,OnSignal);
    std::jthread cancellation([&](std::stop_token stop){while(!stop.stop_requested()){
        if(Interrupted.load(std::memory_order_relaxed)){control.RequestCancel();return;}
        std::this_thread::sleep_for(std::chrono::milliseconds(25));}});
    FSplatStreamingLODBuilder builder;const auto started=std::chrono::steady_clock::now();
    const bool completed=builder.Run(options.at("--input"),options.at("--job-directory"),options.at("--output"),config,control,
        [](const FSplatBuildProgress& p){std::cerr<<"stage="<<uint32_t(p.stage)<<" records="<<p.processedRecords<<'/'<<p.totalRecords
            <<" bytes="<<p.processedBytes<<'/'<<p.totalBytes<<" workspace="<<p.workingBytes<<" metadata="<<p.metadataBytes
            <<" disk="<<p.diskBytes<<" checkpoint="<<p.checkpointGeneration<<'\n';},error);
    cancellation.request_stop();if(!completed)throw std::runtime_error(error);
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    std::cout<<"{\"schemaVersion\":1,\"ok\":true,\"builderVersion\":\""<<SplatBoundedBuilderVersion<<"\",\"builderHash\":\""
        <<config.builderHash<<"\",\"seconds\":"<<seconds<<",\"workingMemoryLimitBytes\":"<<config.workingMemoryBytes<<"}\n";return 0;
}catch(const std::exception& error){std::cerr<<"SplatMobileLODBuilder failed: "<<error.what()<<'\n';return Interrupted.load(std::memory_order_relaxed)?130:1;}}
