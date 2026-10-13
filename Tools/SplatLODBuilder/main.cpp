// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatLODBuilder.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include "Renderer/Splat/SplatPLYLoader.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace MonsterRender::Splat;
namespace {
// The configuration schema is deliberately numeric-only; reject unknown fields and duplicates.
std::map<std::string,double> ReadConfigText(const std::string& text) {
    if(text.size()>65536)throw std::runtime_error("Configuration exceeds size limit");std::istringstream s(text);s.imbue(std::locale::classic());
    auto token=[&](char wanted){s>>std::ws;if(s.get()!=wanted)throw std::runtime_error("Invalid numeric configuration JSON");};token('{');std::map<std::string,double> values;s>>std::ws;if(s.peek()!='}')while(true){token('"');std::string key;std::getline(s,key,'"');if(key.empty()||key.find_first_of("\\\r\n")!=std::string::npos)throw std::runtime_error("Invalid configuration key");token(':');double value;s>>value;if(!s||!std::isfinite(value)||!values.emplace(key,value).second)throw std::runtime_error("Invalid or duplicate configuration value");s>>std::ws;if(s.peek()=='}')break;token(',');}token('}');s>>std::ws;if(s.peek()!=EOF)throw std::runtime_error("Trailing configuration data");return values;
}
std::map<std::string,double> ReadConfig(const std::string& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f||f.tellg()<0||f.tellg()>65536)throw std::runtime_error("Cannot read bounded configuration");f.seekg(0);
    return ReadConfigText(std::string((std::istreambuf_iterator<char>(f)),{}));
}
void Configure(FSplatLODBuildConfig& c,const std::map<std::string,double>& values){
    for(const auto& kv:values){const auto& key=kv.first;double v=kv.second;auto integer=[&](){if(v<0||v>UINT32_MAX||v!=std::floor(v))throw std::runtime_error("Configuration integer out of range: "+key);return uint32_t(v);};
        if(key=="maxLeafGaussians")c.maxLeafGaussians=integer();else if(key=="maxDepth")c.maxDepth=integer();else if(key=="maxPageGaussians")c.maxPageGaussians=integer();else if(key=="supportSigma")c.supportSigma=float(v);else if(key=="proxyReduction")c.proxyReduction=float(v);else if(key=="geometricErrorScale")c.geometricErrorScale=float(v);else if(key=="appearanceWeight")c.appearanceWeight=float(v);else if(key=="maxAppearanceError")c.maxAppearanceError=float(v);else if(key=="projectedFusionGroups"){if(integer()>1)throw std::runtime_error("projectedFusionGroups must be zero or one");c.projectedFusionGroups=v!=0;}else throw std::runtime_error("Unknown configuration field: "+key);}
}
void AugmentFusion(const std::string& input,const std::string& output,const char* executable,bool complete=false) {
    FSplatSceneManifest m;std::string error; if(!FSplatSceneFormat::LoadManifest(input,m,error))throw std::runtime_error(error);
    if(m.version!=3&&!(complete&&m.version==4))throw std::runtime_error("Fusion augmentation requires v3, or v4 with --augment-fusion-complete");
    if(std::filesystem::exists(output))throw std::runtime_error("Output directory already exists");
    FSplatLODBuildConfig cfg;Configure(cfg,ReadConfigText(m.buildConfig));cfg.projectedFusionGroups=true;
    const auto source=std::filesystem::absolute(input).parent_path(),target=std::filesystem::absolute(output);
    std::string baseHash; if(!FSplatSceneFormat::SHA256File(input,baseHash,error))throw std::runtime_error(error);
    const auto previousBuilderHash=m.builderHash;
    std::vector<bool> covered(m.nodes.size(),false);
    uint64_t groups=0,members=0,addedLeafGroups=0;for(auto& node:m.nodes){
        if(node.children.empty()||!std::all_of(node.children.begin(),node.children.end(),[&](uint32_t child){return m.nodes[child].children.empty();}))continue;
        if(!node.originalFusionGroups.empty()){groups+=node.originalFusionGroups.size();for(const auto& g:node.originalFusionGroups)members+=g.count;for(auto child:node.children)covered[child]=true;continue;}
        std::vector<FSplatGaussian> originals;
        for(auto child:node.children)for(auto id:m.nodes[child].pageIds){const auto& p=m.pages[id];std::ifstream f(source/p.relativePath,std::ios::binary);f.seekg(p.offset);std::vector<uint8_t> bytes(p.byteSize);if(!f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Original page read failed");std::vector<FSplatGaussian> decoded;if(!FSplatSceneFormat::DecodePage(m,id,bytes,decoded,error))throw std::runtime_error(error);originals.insert(originals.end(),decoded.begin(),decoded.end());}
        if(!FSplatLODBuilder::BuildOriginalFusionGroups(originals,node.centerBounds,cfg,node.originalFusionGroups,error,m.shDegree))throw std::runtime_error(error);
        groups+=node.originalFusionGroups.size();members+=originals.size();
        for(auto child:node.children)covered[child]=true;
    }
    if(complete)for(uint32_t nodeId=0;nodeId<m.nodes.size();++nodeId){auto& node=m.nodes[nodeId];if(!node.children.empty()||covered[nodeId])continue;std::vector<FSplatGaussian> originals;
        for(auto id:node.pageIds){const auto& p=m.pages[id];std::ifstream f(source/p.relativePath,std::ios::binary);f.seekg(p.offset);std::vector<uint8_t> bytes(p.byteSize);if(!f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Original leaf page read failed");std::vector<FSplatGaussian> decoded;if(!FSplatSceneFormat::DecodePage(m,id,bytes,decoded,error))throw std::runtime_error(error);originals.insert(originals.end(),decoded.begin(),decoded.end());}
        if(!FSplatLODBuilder::BuildOriginalFusionGroups(originals,node.centerBounds,cfg,node.originalFusionGroups,error,m.shDegree))throw std::runtime_error(error);
        addedLeafGroups+=node.originalFusionGroups.size();groups+=node.originalFusionGroups.size();members+=originals.size();
    }
    m.version=complete?5:4;m.builderVersion=complete?SplatLeafFusionBuilderVersion:SplatFusionBuilderVersion;m.buildConfig=FSplatLODBuilder::CanonicalConfig(cfg);m.configHash=FSplatSceneFormat::SHA256(std::vector<uint8_t>(m.buildConfig.begin(),m.buildConfig.end()));
    if(!FSplatSceneFormat::SHA256File(std::filesystem::absolute(executable).string(),m.builderHash,error)||!FSplatSceneFormat::ValidateManifest(m,error))throw std::runtime_error(error);
    std::filesystem::create_directories(target);std::set<std::string> copied;
    for(const auto& page:m.pages){std::ifstream f(source/page.relativePath,std::ios::binary);f.seekg(page.offset);std::vector<uint8_t> bytes(page.byteSize);if(!f.read(reinterpret_cast<char*>(bytes.data()),bytes.size())||FSplatSceneFormat::SHA256(bytes)!=page.sha256)throw std::runtime_error("Input page identity changed before copy");if(copied.insert(page.relativePath).second){auto destination=target/page.relativePath;std::filesystem::create_directories(destination.parent_path());std::error_code ec;std::filesystem::create_hard_link(source/page.relativePath,destination,ec);if(ec)std::filesystem::copy_file(source/page.relativePath,destination);}}
    if(!FSplatSceneFormat::SaveManifest((target/"scene.mgs").string(),m,error))throw std::runtime_error(error);
    std::ofstream report(target/"fusion-build-report.json");report<<"{\"baseManifestSHA256\":\""<<baseHash<<"\",\"baseBuilderHash\":\""<<previousBuilderHash<<"\",\"sourceSHA256\":\""<<m.sourceSHA256<<"\",\"manifestVersion\":"<<m.version<<",\"originalMembers\":"<<members<<",\"groups\":"<<groups<<",\"addedLeafSelfGroups\":"<<addedLeafGroups<<",\"membershipDecodedBytes\":"<<groups*sizeof(FSplatOriginalFusionGroup)<<",\"pagePolicy\":\"unchanged verified FP32 bytes, hardlink or exact copy; no camera metadata\"}\n";report.close();if(!report)throw std::runtime_error("Fusion report write failed");
    std::cout<<"Added "<<groups<<" original groups covering "<<members<<" leaf records. No PLY reconstruction. Manifest: "<<(target/"scene.mgs").string()<<'\n';
}
}
int main(int argc,char** argv) {
    try{std::string input,output,configPath,augment;bool complete=false;uint64_t maxInputBytes=8ull<<30;std::set<std::string> seen;
        for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--help"||arg=="-h"){std::cout<<"SplatLODBuilder --input source.ply --output NEW_DIRECTORY [--config numeric.json] [--max-input-bytes N]\nOutput: scene.mgs, pages/*.mgsp, build-report.json.\nLeaf attributes are preserved; coarse quality requires image validation.\nSplatLODBuilder --augment-fusion existing-v3-scene.mgs --output NEW_DIRECTORY\nAdds v4 current-view fusion membership from verified original leaf pages, no PLY rebuild.\nSplatLODBuilder --augment-fusion-complete existing-v3-or-v4-scene.mgs --output NEW_DIRECTORY\nAdds manifest v5 leaf-self membership without changing any original/proxy page or old parent partition.\n";return 0;}if(!seen.insert(arg).second||i+1>=argc)throw std::runtime_error("Duplicate option or missing value: "+arg);std::string value=argv[++i];if(arg=="--augment-fusion"||arg=="--augment-fusion-complete"){if(!augment.empty())throw std::runtime_error("Choose one augmentation mode");augment=value;complete=arg=="--augment-fusion-complete";}else if(arg=="--input")input=value;else if(arg=="--output")output=value;else if(arg=="--config")configPath=value;else if(arg=="--max-input-bytes"){size_t pos=0;if(value.empty()||value.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid byte limit");maxInputBytes=std::stoull(value,&pos);if(pos!=value.size()||!maxInputBytes)throw std::runtime_error("Invalid byte limit");}else throw std::runtime_error("Unknown option: "+arg);}
        if(!augment.empty()){if(!input.empty()||output.empty()||!configPath.empty())throw std::runtime_error("Augmentation requires only --augment-fusion[-complete] supported_scene.mgs --output NEW_DIRECTORY");AugmentFusion(augment,output,argv[0],complete);return 0;}if(input.empty()||output.empty())throw std::runtime_error("Required options: --input and --output");if(std::filesystem::exists(output))throw std::runtime_error("Output directory already exists");if(std::filesystem::file_size(input)>maxInputBytes)throw std::runtime_error("Source exceeds configured input byte limit");
        FSplatLODBuildConfig cfg;if(!configPath.empty())Configure(cfg,ReadConfig(configPath));std::string error;if(!FSplatSceneFormat::SHA256File(input,cfg.sourceSHA256,error))throw std::runtime_error(error);
        // Fingerprint the actual executable rather than a timestamp or an unverified git HEAD.
        if(!FSplatSceneFormat::SHA256File(std::filesystem::absolute(argv[0]).string(),cfg.builderHash,error))throw std::runtime_error("Builder executable hash: "+error);
        std::cout<<"Loading normalized source (offline working set includes FP32 source and transient proxy arrays)...\n";auto start=std::chrono::steady_clock::now();std::vector<FSplatGaussian> source;uint32_t degree=0;if(!FSplatPLYLoader::LoadCPU(input,source,degree,error))throw std::runtime_error(error);std::cout<<"Source count: "<<source.size()<<", SH degree: "<<degree<<", FP32 source bytes: "<<source.size()*sizeof(FSplatGaussian)<<"\n";
        FSplatSceneManifest manifest;FSplatLODBuilder builder;if(!builder.Build(source,degree,cfg,output,manifest,error))throw std::runtime_error(error);double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"Built "<<manifest.nodes.size()<<" nodes, "<<manifest.pages.size()<<" pages, "<<manifest.proxyCount<<" proxies in "<<seconds<<" seconds. Manifest: "<<(std::filesystem::path(output)/"scene.mgs").string()<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<"SplatLODBuilder failed: "<<e.what()<<"\n";return 1;}
}
