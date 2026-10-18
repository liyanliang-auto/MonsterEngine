// Public CPU behavior oracles for the Android/shared-core change set.
// Release assertions are explicit and fixtures are isolated per case.
#include "Renderer/Splat/SplatPLYStream.h"
#include "Renderer/Splat/SplatPLYLoader.h"
#include "Renderer/Splat/LOD/SplatStreamingLODBuilder.h"
#include "Renderer/Splat/LOD/SplatBuildJournal.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include "Renderer/Splat/LOD/SplatSceneValidation.h"
#include "Renderer/Splat/SplatResourceProvider.h"
#include "Renderer/Splat/SplatBudgetCoordinator.h"
#include "Platform/Android/AndroidPerformancePolicy.h"
#include "Core/RuntimeLease.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
using namespace MonsterRender::Splat;
namespace fs = std::filesystem;
namespace {
void Require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
bool Near(double a,double b,double tolerance=1e-5) { return std::abs(a-b)<=tolerance; }
struct Context {
    fs::path dir;
    explicit Context(const char* name) {
        static std::atomic<unsigned> serial{0};
        const char* base=std::getenv("MONSTER_MOBILE_TEST_ARTIFACT_ROOT");
        dir=fs::path(base?base:"TestFile/3DGSLODStreaming/mobile-verification")/
            (std::string(name)+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial++));
        fs::create_directories(dir);
    }
};
std::vector<uint8_t> Read(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);Require(bool(file),"read fixture");return {std::istreambuf_iterator<char>(file),{}};
}
void Write(const fs::path& path,const std::vector<uint8_t>& bytes) {
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());Require(bool(file),"write fixture");
}
void Text(const fs::path& path,const std::string& text) { Write(path,{text.begin(),text.end()}); }
std::vector<std::string> Names(unsigned degree) {
    std::vector<std::string> names={"x","y","z","f_dc_0","f_dc_1","f_dc_2","opacity","scale_0","scale_1","scale_2","rot_0","rot_1","rot_2","rot_3"};
    for(unsigned i=0;i<3*((degree+1)*(degree+1)-1);++i)names.push_back("f_rest_"+std::to_string(i));
    std::reverse(names.begin(),names.end());return names;
}
float Value(const std::string& name,uint64_t id,bool coincident) {
    if(name=="x")return coincident?0.f:float(id%9)-4;
    if(name=="y")return coincident?0.f:float((id/9)%9)-4;
    if(name=="z")return -10.f;
    if(name=="rot_0")return 2.f;
    if(name=="opacity"||name.rfind("rot_",0)==0)return 0.f;
    if(name=="scale_0")return std::log(.1f);
    if(name=="scale_1")return std::log(.2f);
    if(name=="scale_2")return std::log(.3f);
    if(name.rfind("f_rest_",0)==0)return float(std::stoi(name.substr(7))+1)*.01f;
    return .1f;
}
fs::path PLY(Context& context,uint64_t count,unsigned degree=0,int encoding=0,bool coincident=false,const std::string& label="source.ply") {
    auto path=context.dir/label;std::ofstream file(path,std::ios::binary);auto names=Names(degree);
    file<<"ply\nformat "<<(encoding==0?"ascii":encoding==1?"binary_little_endian":"binary_big_endian")<<" 1.0\nelement vertex "<<count<<'\n';
    for(const auto& name:names)file<<"property float "<<name<<'\n';file<<"end_header\n";
    file.precision(std::numeric_limits<float>::max_digits10);
    for(uint64_t i=0;i<count;++i) { for(const auto& name:names) {
        const float value=Value(name,i,coincident);
        if(!encoding)file<<value<<' ';
        else {uint32_t bits;std::memcpy(&bits,&value,4);for(unsigned k=0;k<4;++k)file.put(char(bits>>(8*(encoding==2?3-k:k))));}
    }if(!encoding)file<<'\n'; }
    Require(bool(file),"generate PLY");return path;
}
bool Same(const FSplatGaussian& a,const FSplatGaussian& b) {
    return a.originalId==b.originalId&&!std::memcmp(a.position,b.position,sizeof(a.position))&&
        !std::memcmp(a.scale,b.scale,sizeof(a.scale))&&!std::memcmp(a.rotation,b.rotation,sizeof(a.rotation))&&
        a.opacity==b.opacity&&!std::memcmp(a.sh,b.sh,sizeof(a.sh));
}
FSplatBoundedBuildConfig Config() {
    FSplatBoundedBuildConfig c;c.workingMemoryBytes=32ull<<20;c.metadataBytes=8ull<<20;c.diskReserveBytes=0;c.maxDiskBytes=128ull<<20;
    c.leafCapacity=8;c.pageCapacity=4;c.maxProxyCount=2;c.rootProxyCount=1;c.batchCapacity=8;c.maxHeaderBytes=4096;c.maxRecordBytes=4096;
    c.builderHash=std::string(64,'a');return c;
}
std::vector<FSplatGaussian> Parse(const fs::path& source,FSplatPLYHeader* observed=nullptr) {
    FSplatPLYHeader h;std::string error;Require(FSplatPLYStream::Probe(source.string(),h,error),error);FSplatBuildControl ctl;
    std::vector<FSplatGaussian> records;
    Require(FSplatPLYStream::StreamRecords(source.string(),h,7,ctl,[&](auto batch,const auto&,auto&){records.insert(records.end(),batch.begin(),batch.end());return true;},error),error);
    if(observed)*observed=h;return records;
}
FSplatBuildIdentity Identity(const fs::path& source,const FSplatBoundedBuildConfig& config) {
    FSplatBuildIdentity identity;FSplatPLYHeader h;std::string e;Require(FSplatPLYStream::Probe(source.string(),h,e),e);
    Require(FSplatSceneFormat::SHA256File(source.string(),identity.sourceSHA256,e),e);identity.sourceBytes=h.fileBytes;identity.sourceRecords=h.recordCount;
    identity.builderHash=config.builderHash;identity.canonicalConfig=FSplatStreamingLODBuilder::CanonicalConfig(config);
    identity.configHash=FSplatSceneFormat::SHA256({identity.canonicalConfig.begin(),identity.canonicalConfig.end()});return identity;
}
FSplatSceneManifest Build(Context& ctx,const fs::path& source,const FSplatBoundedBuildConfig& config=Config(),const std::string& label="a") {
    FSplatBuildControl ctl;FSplatStreamingLODBuilder builder;std::string error;uint64_t peak=0;bool complete=false;
    Require(builder.Run(source.string(),(ctx.dir/("job-"+label)).string(),(ctx.dir/("scene-"+label)).string(),config,ctl,[&](auto p){
        Require(p.workingBytes<=config.workingMemoryBytes&&p.metadataBytes<=config.metadataBytes,"observed admitted workspace exceeded cap");
        peak=std::max(peak,p.peakWorkingBytes);if(p.stage==ESplatBuildStage::Completed)complete=true;
    },error),"bounded builder: "+error);
    Require(complete&&peak>0&&peak<=config.workingMemoryBytes,"completed stage and measured workspace");
    FSplatSceneManifest m;Require(FSplatSceneFormat::LoadManifest((ctx.dir/("scene-"+label)/"scene.mgs").string(),m,error),error);return m;
}
void CheckLeaves(Context& ctx,const fs::path& source,const FSplatSceneManifest& m,const std::string& label="a") {
    const auto originals=Parse(source);std::set<uint64_t> seen;std::string error;
    Require(m.version==3&&m.builderVersion==SplatBoundedBuilderVersion,"mobile protocol and algorithm identity");
    Require(m.sourceCount==originals.size()&&m.leafCount==originals.size(),"manifest source/leaf count");
    for(const auto& node:m.nodes) {
        Require(node.originalFusionGroups.empty(),"mobile builder must not emit fusion groups");
        for(auto child:node.children)for(unsigned axis=0;axis<3;++axis){
            Require(node.bounds.min[axis]<=m.nodes[child].bounds.min[axis]&&node.bounds.max[axis]>=m.nodes[child].bounds.max[axis],"parent support must contain descendants");
            Require(node.centerBounds.min[axis]<=m.nodes[child].centerBounds.min[axis]&&node.centerBounds.max[axis]>=m.nodes[child].centerBounds.max[axis],"parent centers must contain descendants");
        }
        if(!node.children.empty())continue;
        Require(node.gaussianCount<=Config().leafCapacity,"hard leaf capacity");
        for(auto page:node.pageIds) {
            std::vector<FSplatGaussian> rows;Require(FSplatSceneFormat::DecodePage(m,page,Read(ctx.dir/("scene-"+label)/m.pages[page].relativePath),rows,error),error);
            for(const auto& row:rows){Require(row.originalId<originals.size()&&seen.insert(row.originalId).second,"lost/duplicate leaf ID");Require(Same(row,originals[row.originalId]),"leaf activation changed");}
        }
    }
    Require(seen.size()==originals.size(),"all original rows occur exactly once");
    FSplatSceneValidationResult result;Require(FSplatSceneValidation::Validate((ctx.dir/("scene-"+label)).string(),{},result,error),error);
    Require(result.pageCount==m.pages.size()&&result.contentSHA256.size()==64,"independent publication gate validates all pages");
}
struct Suite {
    unsigned passed=0,failed=0;
    void Test(const std::string& name,const std::function<void()>& test){try{test();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
};
}
int main() { Suite suite;
    for(unsigned degree=0;degree<=3;++degree)for(int encoding=0;encoding<3;++encoding)
    suite.Test("PLY degree="+std::to_string(degree)+" encoding="+std::to_string(encoding)+" independent activation and shared loader",[=]{
        Context c("activation");auto source=PLY(c,5,degree,encoding);auto rows=Parse(source);std::vector<FSplatGaussian> legacy;uint32_t loadedDegree=99;std::string e;
        Require(FSplatPLYLoader::LoadCPU(source.string(),legacy,loadedDegree,e),e);Require(rows.size()==5&&legacy.size()==5&&loadedDegree==degree,"shared parse count/degree");
        for(size_t i=0;i<rows.size();++i){const auto& g=rows[i];Require(g.originalId==i&&g.position[0]==float(i)-4,"stable source IDs/position");
            Require(Near(g.scale[0],.1)&&Near(g.scale[1],.2)&&Near(g.scale[2],.3)&&Near(g.opacity,.5)&&Near(g.rotation[0],1),"activation exactly once");
            const unsigned per=(degree+1)*(degree+1)-1;for(unsigned k=0;k<per;++k)for(unsigned ch=0;ch<3;++ch)Require(Near(g.sh[3*(k+1)+ch],float(ch*per+k+1)*.01),"SH layout oracle");
            Require(Same(g,legacy[i]),"shared loader and streaming activation disagree");}
    });
    suite.Test("PLY exact cursor resume and malformed byte offset rejection",[]{
        Context c("cursor");auto source=PLY(c,13);FSplatPLYHeader h;std::string e;Require(FSplatPLYStream::Probe(source.string(),h,e),e);FSplatBuildControl ctl;FSplatBuildSourceCursor cursor;size_t first=0;
        Require(!FSplatPLYStream::StreamRecords(source.string(),h,4,ctl,[&](auto rows,const auto& next,auto&){first+=rows.size();cursor=next;ctl.RequestPause();return true;},e),"pause must stop parser");
        Require(first==4&&cursor.nextOriginalId==4,"committed bounded batch");FSplatBuildControl resumed;std::vector<uint64_t> ids;
        Require(FSplatPLYStream::StreamRecords(source.string(),h,3,resumed,[&](auto rows,const auto&,auto&){for(auto& g:rows)ids.push_back(g.originalId);return true;},e,cursor),e);
        Require(ids.size()==9&&ids.front()==4&&ids.back()==12,"resume exact suffix");++cursor.byteOffset;
        Require(!FSplatPLYStream::StreamRecords(source.string(),h,3,resumed,[](auto,const auto&,auto&){return true;},e,cursor),"ASCII arbitrary seek accepted");
    });
    suite.Test("PLY cancellation, probe limits and transactional output",[]{
        Context c("parser-fail");auto source=PLY(c,20);FSplatPLYHeader h;h.recordCount=999;std::string e;FSplatPLYLimits limits;limits.maxRecords=19;
        Require(!FSplatPLYStream::Probe(source.string(),h,e,limits)&&h.recordCount==999,"count admission or failed probe mutated header");
        Require(FSplatPLYStream::Probe(source.string(),h,e),e);FSplatBuildControl ctl;ctl.RequestCancel();bool emitted=false;
        Require(!FSplatPLYStream::StreamRecords(source.string(),h,4,ctl,[&](auto,const auto&,auto&){emitted=true;return true;},e)&&!emitted,"cancelled parser emitted records");
    });
    for(const std::string bad:{"nan","inf","1e999"})suite.Test("PLY rejects nonfinite activated position "+bad,[=]{
        Context c("nonfinite");auto source=PLY(c,1);auto bytes=Read(source);std::string text(bytes.begin(),bytes.end());const auto line=text.find("end_header\n")+11;const auto x=text.rfind("-4 ");Require(x>=line,"fixture position");text.replace(x,2,bad);Text(source,text);
        FSplatPLYHeader h;std::string e;Require(FSplatPLYStream::Probe(source.string(),h,e),e);FSplatBuildControl ctl;
        Require(!FSplatPLYStream::StreamRecords(source.string(),h,4,ctl,[](auto,const auto&,auto&){return true;},e),"nonfinite position accepted");
    });
    suite.Test("PLY rejects truncation and duplicate property",[]{
        Context c("malformed");auto source=PLY(c,2,3,1);auto bytes=Read(source);bytes.pop_back();Write(source,bytes);FSplatPLYHeader h;std::string e;
        Require(!FSplatPLYStream::Probe(source.string(),h,e),"truncated binary accepted");source=PLY(c,1);bytes=Read(source);std::string text(bytes.begin(),bytes.end());auto at=text.find("property float x");text.insert(at,"property float x\n");Text(source,text);
        Require(!FSplatPLYStream::Probe(source.string(),h,e),"duplicate property accepted");
    });
    for(const std::string invalid:{"{\"unknown\":1}","{\"batchCapacity\":8,\"batchCapacity\":9}","{\"batchCapacity\":1.5}","{\"batchCapacity\":01}","{\"schemaVersion\":2}","{\"leafCapacity\":0}"})
    suite.Test("Builder policy rejects "+invalid,[=]{auto config=Config();const auto original=FSplatStreamingLODBuilder::CanonicalConfig(config);std::string e;Require(!FSplatStreamingLODBuilder::ParseConfig(invalid,config,e),"invalid policy accepted");Require(FSplatStreamingLODBuilder::CanonicalConfig(config)==original,"invalid policy mutated output");});
    suite.Test("Builder policy canonical identity and worst-case admission",[]{
        auto config=Config();std::string e;Require(FSplatStreamingLODBuilder::ValidateConfig(config,e),e);auto other=config;other.rootProxyCount=2;
        Require(FSplatStreamingLODBuilder::CanonicalConfig(other)!=FSplatStreamingLODBuilder::CanonicalConfig(config),"effective policy omitted from identity");
        other=config;other.workingMemoryBytes=8ull<<20;Require(!FSplatStreamingLODBuilder::ValidateConfig(other,e),"unadmitted scratch accepted");
        other=config;other.builderHash.clear();Require(!FSplatStreamingLODBuilder::ValidateConfig(other,e)&&FSplatStreamingLODBuilder::ValidateConfig(other,e,false),"actual binary identity required only for run");
    });
    suite.Test("Bounded builder exact leaves, cumulative support, v3/v1 and deterministic bytes",[]{
        Context c("builder");auto source=PLY(c,81,3);auto a=Build(c,source);CheckLeaves(c,source,a);auto b=Build(c,source,Config(),"b");
        Require(Read(c.dir/"scene-a/scene.mgs")==Read(c.dir/"scene-b/scene.mgs"),"manifest nondeterminism");
        for(const auto& page:a.pages)Require(Read(c.dir/"scene-a"/page.relativePath)==Read(c.dir/"scene-b"/page.relativePath),"page nondeterminism");
        Require(a.nodes.size()>9&&a.nodes[a.rootNode].gaussianCount<=Config().rootProxyCount,"multilevel hierarchy/root cap");
        for(const auto& node:a.nodes)for(auto child:node.children)Require(node.geometricError+1e-5>=a.nodes[child].geometricError,"historical error reset");
    });
    suite.Test("Bounded coincident centers terminate by stable-ID partition without loss",[]{Context c("coincident");auto source=PLY(c,65,0,0,true);auto m=Build(c,source);CheckLeaves(c,source,m);Require(m.nodes.size()>1,"degenerate input bypassed hard leaf cap");});
    suite.Test("Bounded pause checkpoint resumes to identical complete publication",[]{
        Context c("resume");auto source=PLY(c,41);auto config=Config();FSplatStreamingLODBuilder b;FSplatBuildControl ctl;std::string e;bool paused=false;
        const auto job=(c.dir/"job-a").string(),output=(c.dir/"scene-a").string();
        Require(!b.Run(source.string(),job,output,config,ctl,[&](auto p){if(p.stage==ESplatBuildStage::Normalizing&&p.processedRecords>=8){paused=true;ctl.RequestPause();}},e),"pause run unexpectedly completed");
        Require(paused&&!fs::exists(c.dir/"scene-a"),"pause published incomplete scene");FSplatBuildControl resumed;Require(b.Run(source.string(),job,output,config,resumed,{},e),e);
        FSplatSceneManifest m;Require(FSplatSceneFormat::LoadManifest((c.dir/"scene-a/scene.mgs").string(),m,e),e);CheckLeaves(c,source,m);
        Build(c,source,config,"b");Require(Read(c.dir/"scene-a/scene.mgs")==Read(c.dir/"scene-b/scene.mgs"),"resume changed result");
    });
    suite.Test("Bounded source mutation/config mismatch cannot resume committed work",[]{
        Context c("identity");auto source=PLY(c,17);auto config=Config();FSplatBuildControl ctl;FSplatStreamingLODBuilder b;std::string e;
        Require(!b.Run(source.string(),(c.dir/"job").string(),(c.dir/"out").string(),config,ctl,[&](auto p){if(p.stage==ESplatBuildStage::Normalizing&&p.processedRecords)ctl.RequestCancel();},e),"cancel completed");
        auto changed=config;changed.maxDepth--;FSplatBuildControl again;
        Require(!b.Run(source.string(),(c.dir/"job").string(),(c.dir/"out").string(),changed,again,{},e),"incompatible config resumed");
        auto bytes=Read(source);auto at=std::find(bytes.begin()+std::distance(bytes.begin(),std::search(bytes.begin(),bytes.end(),std::begin("end_header\n"),std::end("end_header\n")-1))+11,bytes.end(),uint8_t('2'));
        Require(at!=bytes.end(),"mutation fixture");*at='3';Write(source,bytes);FSplatBuildControl fresh;
        Require(!b.Run(source.string(),(c.dir/"job").string(),(c.dir/"out").string(),config,fresh,{},e),"mutated source resumed");Require(!fs::exists(c.dir/"out"),"incompatible checkpoint published");
    });
    suite.Test("Bounded disk cap, source cap and depth exhaustion never publish",[]{
        Context c("admission");auto source=PLY(c,65,0,0,true);FSplatStreamingLODBuilder b;std::string e;
        for(unsigned mode=0;mode<3;++mode){auto cfg=Config();if(mode==0)cfg.maxDiskBytes=100;if(mode==1)cfg.maxSourceRecords=64;if(mode==2)cfg.maxDepth=1;FSplatBuildControl ctl;
            const auto output=c.dir/("out"+std::to_string(mode));Require(!b.Run(source.string(),(c.dir/("job"+std::to_string(mode))).string(),output.string(),cfg,ctl,{},e),"invalid resource/depth admitted");Require(!fs::exists(output),"resource failure mispublished scene");}
    });
    suite.Test("Publication validation rejects corrupt page without changing prior evidence",[]{
        Context c("validation");auto source=PLY(c,9);auto m=Build(c,source);auto path=c.dir/"scene-a"/m.pages.back().relativePath;auto bytes=Read(path);bytes.back()^=1;Write(path,bytes);
        FSplatSceneValidationResult result;result.sourceRecords=999;std::string e;Require(!FSplatSceneValidation::Validate((c.dir/"scene-a").string(),{},result,e)&&result.sourceRecords==999,"corrupt scene validated or partial evidence published");
    });
    suite.Test("Proxy additive moments and historical errors survive repeated merges",[]{
        FSplatGaussian a,b;a.position[0]=-1;b.position[0]=1;a.opacity=b.opacity=.5;auto x=FSplatProxyMath::FromGaussian(a),y=FSplatProxyMath::FromGaussian(b);
        x.geometryErrorSquared=4;y.geometryErrorSquared=9;x.appearanceErrorSquared=1;y.appearanceErrorSquared=4;auto merged=FSplatProxyMath::Merge(x,y,3,3,55);
        Require(Near(merged.gaussian.position[0],0)&&Near(merged.weight,x.weight+y.weight),"mass/moment oracle");
        Require(merged.geometryErrorSquared>=6.5&&merged.appearanceErrorSquared>=2.5,"historical error reset");
        auto next=FSplatProxyMath::Merge(merged,merged,3,3,56);Require(next.geometryErrorSquared>=merged.geometryErrorSquared&&Near(next.weight,2*merged.weight),"merge lost history/mass");
    });
    suite.Test("Proxy cancellation preserves previous output and rejects unadmitted scratch",[]{
        auto cfg=Config();std::vector<FSplatWeightedGaussian> input;for(unsigned i=0;i<12;++i){FSplatGaussian g;g.position[0]=float(i);g.originalId=i;input.push_back(FSplatProxyMath::FromGaussian(g));}
        std::vector<FSplatWeightedGaussian> output(1,input.back());FSplatProxySummary summary;summary.geometricError=99;uint64_t scratch=0;std::string e;FSplatBuildControl ctl;ctl.RequestCancel();
        Require(!FSplatProxyMath::Reduce(input,0,2,cfg,ctl,output,summary,scratch,e),"cancelled reduction succeeded");Require(output.size()==1&&output[0].gaussian.originalId==11&&summary.geometricError==99,"partial reduction published");
        FSplatBuildControl fresh;cfg.workingMemoryBytes=cfg.metadataBytes+1;Require(!FSplatProxyMath::Reduce(input,0,2,cfg,fresh,output,summary,scratch,e),"unadmitted scratch allocated");
    });
    suite.Test("Journal weighted explicit-LE wire, reopen and checksum rejection",[]{
        Context c("journal");auto source=PLY(c,2);auto cfg=Config();auto identity=Identity(source,cfg);FSplatBuildControl ctl;std::string e;FSplatBuildChunk descriptor;
        std::vector<FSplatWeightedGaussian> weighted;for(auto g:Parse(source)){auto w=FSplatProxyMath::FromGaussian(g);w.geometryErrorSquared=3.25;w.appearanceErrorSquared=5.5;w.weight=7.75;weighted.push_back(w);}
        {FSplatBuildJournal j;Require(j.Open((c.dir/"job").string(),source.string(),identity,cfg,ctl,e),e);std::unique_ptr<FSplatBuildChunkWriter>w;
            Require(j.CreateChunk(1,1,276,ctl,w,e)&&w->AppendWeighted(weighted,e)&&w->Finish(descriptor,e),e);w.reset();auto next=j.Checkpoint();next.stage=ESplatBuildStage::Reducing;
            FSplatBuildTask task;task.id=1;task.kind=ESplatBuildTaskKind::Reduce;task.inputRecords=2;Require(j.CommitWorkUnit(task,std::span<const FSplatBuildChunk>(&descriptor,1),next,ctl,e),e);}
        const auto encoded=Read(c.dir/"job"/descriptor.relativePath);Require(encoded.size()==96+2*276&&encoded[0]=='M'&&encoded[1]=='G'&&encoded[2]=='S'&&encoded[3]=='C',"wire magic/explicit stride");
        // First originalId is zero; second is one, at 244B in each 252B Gaussian.
        Require(encoded[96+276+244]==1&&encoded[96+276+245]==0,"LE originalId location");
        {FSplatBuildJournal j;Require(j.Open((c.dir/"job").string(),source.string(),identity,cfg,ctl,e),e);std::unique_ptr<FSplatBuildChunkReader>r;
            Require(j.OpenChunk(1,ctl,r,e),e);std::vector<FSplatWeightedGaussian> rows;Require(r->ReadWeighted(2,rows,e),e);Require(rows.size()==2&&rows[1].geometryErrorSquared==3.25&&rows[1].appearanceErrorSquared==5.5&&rows[1].weight==7.75,"weighted errors reset on reload");}
        auto corrupt=encoded;corrupt.back()^=1;Write(c.dir/"job"/descriptor.relativePath,corrupt);
        FSplatBuildJournal j;std::unique_ptr<FSplatBuildChunkReader>r;
        const bool reopened=j.Open((c.dir/"job").string(),source.string(),identity,cfg,ctl,e);
        Require(!reopened||!j.OpenChunk(1,ctl,r,e),"corrupt committed chunk accepted");
        Require(!e.empty(),"corruption rejection must explain failure");
    });
    suite.Test("Budget aggregate cap, move lifetime, wrong account and overflow",[]{
        FSplatBudgetCoordinator a(100),b(100);std::string e;auto x=a.TryReserve(ESplatBudgetComponent::CPUCache,70,e);Require(bool(x),e);
        Require(!a.TryReserve(ESplatBudgetComponent::SourceGPU,31,e)&&!a.TryReserve(ESplatBudgetComponent::Native,UINT64_MAX,e),"aggregate overflow admitted");
        Require(a.Snapshot().reservedBytes==70&&!b.Release(*x),"failed reserve/wrong account changed charge");auto moved=std::move(*x);Require(!*x&&moved.Bytes()==70,"lease move double ownership");
        moved.Reset();moved.Reset();Require(a.Snapshot().reservedBytes==0&&a.Snapshot().peakBytes==70,"retirement/peak");
        std::optional<FSplatBudgetCoordinator::FReservation> survivor;{FSplatBudgetCoordinator shortOwner(9);survivor=shortOwner.TryReserve(ESplatBudgetComponent::Metadata,9,e);}Require(survivor&&survivor->Bytes()==9,"lease must survive coordinator");survivor.reset();
    });
    suite.Test("Budget build exclusion stays until final Builder lease retirement",[]{
        FSplatBudgetCoordinator budget(1024);std::string e;auto scene=budget.TryReserve(ESplatBudgetComponent::SourceGPU,100,e);Require(scene&&!budget.RequestExclusiveBuild(e),"live scene allowed build");scene.reset();
        auto ui=budget.TryReserve(ESplatBudgetComponent::Java,10,e);Require(budget.RequestExclusiveBuild(e),e);
        Require(!budget.TryReserve(ESplatBudgetComponent::CPUCache,0,e),"zero-byte heavy lease bypassed exclusion");auto build=budget.TryReserve(ESplatBudgetComponent::Builder,400,e);Require(bool(build),e);
        budget.ReleaseExclusiveBuild();Require(budget.Snapshot().exclusiveBuild,"released build exclusion before worker lease");build.reset();budget.ReleaseExclusiveBuild();Require(!budget.Snapshot().exclusiveBuild&&budget.Snapshot().reservedBytes==10,"release after worker retirement");
    });
    suite.Test("Budget concurrent reservations preserve disjoint aggregate invariants",[]{
        FSplatBudgetCoordinator budget(64);std::atomic<bool> invalid{false};std::vector<std::thread> threads;
        for(unsigned i=0;i<8;++i)threads.emplace_back([&]{for(unsigned n=0;n<500;++n){std::string e;auto lease=budget.TryReserve(ESplatBudgetComponent::Metadata,8,e);const auto s=budget.Snapshot();if(s.reservedBytes>s.limitBytes)invalid=true;}});
        for(auto& t:threads)t.join();Require(!invalid&&budget.Snapshot().reservedBytes==0&&budget.Snapshot().peakBytes<=64,"thread-safe accounting invariant");
    });
    suite.Test("Resource provider bounded hash validation and path confinement",[]{
        Context c("resource");Text(c.dir/"known.bin","abc");FResourceIdentity id{"known.bin",3,FSplatSceneFormat::SHA256({'a','b','c'})};FFileResourceProvider provider(c.dir.string(),{id},3);std::vector<uint8_t> bytes;std::string e,path;
        Require(provider.ReadBytes("known.bin",bytes,e)&&bytes==std::vector<uint8_t>({'a','b','c'}),e);Require(provider.ResolveValidatedFile("known.bin",path,e)&&fs::path(path).filename()=="known.bin",e);
        for(const std::string bad:{"../known.bin","/known.bin","a//b","a/../known.bin","a\\b","C:/known.bin"})Require(!provider.ReadBytes(bad,bytes,e)&&bytes.empty(),"resource traversal accepted");
        Text(c.dir/"known.bin","abd");Require(!provider.ReadBytes("known.bin",bytes,e)&&bytes.empty(),"resource corruption accepted");
        FFileResourceProvider small(c.dir.string(),{},2);Require(!small.ReadBytes("known.bin",bytes,e),"read cap exceeded");
        std::vector<FResourceIdentity> entries;Require(!ValidateResourceInventory({id,id},3,e),"duplicate inventory accepted");
    });
    suite.Test("Frame policy one scheduler, sticky pressure and explicit recovery",[]{
        FAndroidPerformancePolicy p;std::string e;Require(p.Configure({}, {},e),e);
        Require(p.AcceptFrame(EAndroidFrameScheduler::Choreographer,10)&&!p.AcceptFrame(EAndroidFrameScheduler::Choreographer,10)&&!p.AcceptFrame(EAndroidFrameScheduler::Swappy,11),"multiple pacing loops/stale token accepted");
        Require(p.OnMemoryPressure(20).pressure==EAndroidPressureAction::None,"UI hidden treated as memory pressure");
        Require(p.OnMemoryPressure(10).pressure==EAndroidPressureAction::TrimCache&&p.OnMemoryPressure(40).pressure==EAndroidPressureAction::RetireSafePages,"ordered pressure");
        Require(p.OnMemoryPressure(80).manualRecoveryRequired&&!p.AcceptFrame(EAndroidFrameScheduler::Choreographer,12),"critical pressure continued rendering");
        Require(p.OnMemoryPressure(5).pressure==EAndroidPressureAction::ReleaseScene,"pressure downgrade cleared sticky failure");p.RequestManualRecovery();Require(p.AcceptFrame(EAndroidFrameScheduler::Choreographer,1),"explicit retry failed to reset timestamps");
    });
    suite.Test("Thermal baseline fixed quality and optional policy requires real evidence",[]{
        FAndroidPerformancePolicy p;std::string e;Require(p.Configure({}, {},e),e);Require(!p.OnThermalStatus(3).qualityChanged&&p.Snapshot().qualityPixels==3,"baseline quality silently tuned");
        FAndroidPerformanceConfig cfg;cfg.swappyOptIn=true;Require(!p.Configure(cfg,{},e),"unmeasured Swappy admitted");
        Require(p.OnThermalStatus(4).manualRecoveryRequired,"critical thermal did not require release");
    });
    suite.Test("Process allocator runtime remains leased across independent owners",[]{
        const auto before=MonsterRender::FProcessRuntimeLease::ActiveCount();auto a=MonsterRender::FProcessRuntimeLease::Acquire();auto b=MonsterRender::FProcessRuntimeLease::Acquire();
        Require(a.IsValid()&&b.IsValid()&&MonsterRender::FProcessRuntimeLease::ActiveCount()==before+2,"runtime owner admission");auto moved=std::move(a);Require(!a.IsValid()&&moved.IsValid(),"runtime move");
        b.Reset();Require(MonsterRender::FProcessRuntimeLease::ActiveCount()==before+1,"early allocator teardown");moved.Reset();Require(MonsterRender::FProcessRuntimeLease::ActiveCount()==before,"runtime final retirement");
    });
    std::cout<<"SUMMARY total="<<suite.passed+suite.failed<<" passed="<<suite.passed<<" failed="<<suite.failed<<'\n';return suite.failed?1:0;
}
