// Formal CPU behavior tests. Assertions remain active in Release builds.
#include "Renderer/Splat/SplatPLYLoader.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include "Renderer/Splat/LOD/SplatLODBuilder.h"
#include "Renderer/Splat/LOD/SplatLODSelector.h"
#include "Renderer/Splat/Streaming/SplatPageStreamer.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <cstdlib>
using namespace MonsterRender::Splat;
namespace fs=std::filesystem;
#include "SplatLODStreamerAllocationProbe.h"
namespace {
void Require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
bool Near(double a,double b,double tolerance=1e-5){return std::abs(a-b)<=tolerance;}
std::vector<uint8_t> Read(const fs::path& p){std::ifstream f(p,std::ios::binary);Require(bool(f),"fixture read");return {std::istreambuf_iterator<char>(f),{}};}
void Write(const fs::path& p,const std::vector<uint8_t>& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());Require(bool(f),"fixture write");}
struct Context {
    fs::path dir;
    explicit Context(const std::string& name){static unsigned serial=0;const char* root=std::getenv("MONSTER_SPLAT_TEST_ARTIFACT_ROOT");dir=(fs::path(root&&*root?root:"TestFile/3DGSLODStreaming/verification")/"fixtures")/(name+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial++));fs::create_directories(dir);}
};
FSplatGaussian Gaussian(uint64_t id,float x=0,float y=0,float z=-10){FSplatGaussian g;g.originalId=id;g.position[0]=x;g.position[1]=y;g.position[2]=z;g.scale[0]=g.scale[1]=g.scale[2]=.1f;g.opacity=.4f;for(unsigned i=0;i<48;++i)g.sh[i]=float(id+i)*.001f;return g;}
bool Same(const FSplatGaussian& a,const FSplatGaussian& b){return a.originalId==b.originalId&&!std::memcmp(a.position,b.position,sizeof(a.position))&&!std::memcmp(a.scale,b.scale,sizeof(a.scale))&&!std::memcmp(a.rotation,b.rotation,sizeof(a.rotation))&&a.opacity==b.opacity&&!std::memcmp(a.sh,b.sh,sizeof(a.sh));}
FSplatLODBuildConfig Config(){FSplatLODBuildConfig c;c.maxLeafGaussians=2;c.maxPageGaussians=2;c.proxyReduction=.5f;c.maxDepth=8;c.sourceSHA256=std::string(64,'1');c.builderHash=std::string(64,'2');return c;}
FSplatSceneManifest Build(Context& c,const std::vector<FSplatGaussian>& g,const FSplatLODBuildConfig& cfg=Config(),const std::string& suffix="scene"){FSplatLODBuilder b;FSplatSceneManifest m;std::string error;Require(b.Build(g,3,cfg,(c.dir/suffix).string(),m,error),"build: "+error);return m;}
std::vector<FSplatGaussian> Page(const fs::path& dir,const FSplatSceneManifest& m,unsigned id){auto bytes=Read(dir/m.pages[id].relativePath);std::vector<FSplatGaussian> gs;std::string error;Require(FSplatSceneFormat::DecodePage(m,id,bytes,gs,error),"decode: "+error);return gs;}
fs::path PLY(Context& c,unsigned degree,const std::string& overrideName="",const std::string& overrideValue="",bool binary=false,bool big=false){
    std::vector<std::pair<std::string,float>> fields={{"x",1},{"y",2},{"z",-3},{"f_dc_0",.1f},{"f_dc_1",.2f},{"f_dc_2",.3f},{"opacity",0},{"scale_0",0},{"scale_1",std::log(2.f)},{"scale_2",std::log(3.f)},{"rot_0",2},{"rot_1",0},{"rot_2",0},{"rot_3",0}};
    for(unsigned i=0;i<3*((degree+1)*(degree+1)-1);++i)fields.emplace_back("f_rest_"+std::to_string(i),float(i+1));
    std::reverse(fields.begin(),fields.end());auto p=c.dir/"fixture.ply";std::ofstream f(p,std::ios::binary);f<<"ply\nformat "<<(binary?(big?"binary_big_endian":"binary_little_endian"):"ascii")<<" 1.0\nelement vertex 1\n";for(auto& field:fields)f<<"property float "<<field.first<<'\n';f<<"end_header\n";
    for(auto& field:fields){if(!binary)f<<(field.first==overrideName?overrideValue:std::to_string(field.second))<<' ';else{uint32_t bits;std::memcpy(&bits,&field.second,4);for(unsigned k=0;k<4;++k)f.put(char((bits>>(8*(big?3-k:k)))&255));}}Require(bool(f),"PLY write");return p;
}
FSplatSceneManifest SmallTree(){FSplatSceneManifest m;m.rootNode=0;m.nodes.resize(3);m.pages.resize(4);unsigned counts[]={1,1,1,1};for(unsigned i=0;i<4;++i){m.pages[i].id=i;m.pages[i].gaussianCount=counts[i];}m.pages[0].pinned=true;m.pages[1].pinned=true;
    for(auto& n:m.nodes){n.bounds.min[0]=n.bounds.min[1]=-.1f;n.bounds.max[0]=n.bounds.max[1]=.1f;n.bounds.min[2]=-10.1f;n.bounds.max[2]=-9.9f;}
    m.nodes[0].children={1,2};m.nodes[0].pageIds={0,1};m.nodes[0].gaussianCount=2;m.nodes[0].geometricError=1;
    for(unsigned i=1;i<3;++i){m.nodes[i].pageIds={i+1};m.nodes[i].gaussianCount=1;m.nodes[i].level=1;}return m;
}
FSplatResidencySnapshot Ready(const FSplatSceneManifest& m){FSplatResidencySnapshot r;r.sceneGeneration=1;r.residencyVersion=1;unsigned offset=0;for(auto& p:m.pages){r.pages.push_back({p.id,offset,p.gaussianCount,1,true});offset+=p.gaussianCount;}return r;}
FSplatLODView View(){FSplatLODView v;v.width=v.height=64;v.projection[0]=v.projection[5]=1;v.projection[10]=-1;v.projection[11]=-1;v.projection[14]=-.2f;v.projection[15]=0;return v;}
FSplatLODSettings Settings(){FSplatLODSettings s;s.activeBudget=100;s.sourceBudget=100;s.sortBudget=100000;s.qualityPixels=.1f;return s;}
std::vector<FSplatPageResult> Wait(FSplatPageStreamer& s,size_t count=1){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);std::vector<FSplatPageResult> out;while(out.size()<count&&std::chrono::steady_clock::now()<until){auto ready=s.DrainReady();for(auto& x:ready)out.push_back(std::move(x));std::this_thread::sleep_for(std::chrono::milliseconds(2));}Require(out.size()==count,"asynchronous completion deadline");return out;}
void Release(FSplatPageStreamer& s,FSplatPageResult& r){auto id=r.pageId;auto gen=r.sceneGeneration;std::vector<uint8_t>().swap(r.bytes);std::vector<FSplatGaussian>().swap(r.decodedGaussians);Require(s.ReleaseTransferred(id,gen),"release valid lease");}
struct Suite {unsigned passed=0,failed=0;void Test(const std::string& name,const std::function<void()>& f){try{f();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}};
}
#include "SplatLODCPUExtraTests.h"
#include "SplatLODRound3CPUTests.h"
#include "SplatLODStreamerLifetimeTests.h"
int main(){Suite t;
    for(unsigned degree=0;degree<=3;++degree)t.Test("T01 degree "+std::to_string(degree)+" activation reordered properties",[=]{Context c("ply");std::vector<FSplatGaussian> gs;unsigned d=99;std::string error;Require(FSplatPLYLoader::LoadCPU(PLY(c,degree).string(),gs,d,error),error);Require(gs.size()==1&&d==degree,"degree/count");auto& g=gs[0];Require(Near(g.scale[0],1)&&Near(g.scale[1],2)&&Near(g.scale[2],3)&&Near(g.opacity,.5)&&Near(g.rotation[0],1),"activation once");unsigned per=(degree+1)*(degree+1)-1;for(unsigned k=0;k<per;++k)for(unsigned ch=0;ch<3;++ch)Require(Near(g.sh[3*(k+1)+ch],ch*per+k+1),"SH channel-major to coefficient-major oracle");});
    for(bool big:{false,true})t.Test(std::string("T01 binary endian ")+(big?"big":"little"),[=]{Context c("binary");std::vector<FSplatGaussian> g;unsigned d;std::string e;Require(FSplatPLYLoader::LoadCPU(PLY(c,3,"","",true,big).string(),g,d,e),e);Require(g[0].position[0]==1&&g[0].position[2]==-3&&d==3,"binary scalar oracle");});
    for(auto bad:std::vector<std::pair<std::string,std::string>>{{"x","nan"},{"x","inf"},{"rot_0","0"},{"scale_0","1000"}})t.Test("T01 rejects "+bad.first+"="+bad.second,[=]{Context c("invalid");std::vector<FSplatGaussian> g={Gaussian(42)};unsigned d=2;std::string e;Require(!FSplatPLYLoader::LoadCPU(PLY(c,0,bad.first,bad.second).string(),g,d,e),"invalid input accepted");Require(g.size()==1&&g[0].originalId==42&&d==2,"failed parse mutated output");});
    t.Test("T01 rejects truncated binary payload",[]{Context c("trunc");auto p=PLY(c,2,"","",true);auto bytes=Read(p);bytes.pop_back();Write(p,bytes);std::vector<FSplatGaussian> g;unsigned d;std::string e;Require(!FSplatPLYLoader::LoadCPU(p.string(),g,d,e),"truncated input accepted");});
    t.Test("T02 deterministic builds and three useful levels",[]{Context c("determinism");std::vector<FSplatGaussian> g;for(unsigned i=0;i<64;++i)g.push_back(Gaussian(i,float(i%8),float(i/8)));auto a=Build(c,g,Config(),"a"),b=Build(c,g,Config(),"b");Require(Read(c.dir/"a/scene.mgs")==Read(c.dir/"b/scene.mgs"),"manifest nondeterminism");std::set<unsigned> levels;for(auto& n:a.nodes)levels.insert(n.level);Require(levels.size()>=3,"missing useful hierarchy levels");for(auto& p:a.pages)Require(Read(c.dir/"a"/p.relativePath)==Read(c.dir/"b"/p.relativePath),"page nondeterminism");});
    t.Test("T02 empty duplicates maximum-depth termination",[]{Context c("termination");auto empty=Build(c,{},Config(),"empty");Require(empty.rootNode==SplatInvalidId&&empty.pages.empty()&&empty.nodes.empty(),"empty invariant");std::vector<FSplatGaussian> g;for(unsigned i=0;i<20;++i)g.push_back(Gaussian(i));auto dup=Build(c,g,Config(),"duplicates");Require(dup.nodes.size()==1&&dup.leafCount==20,"duplicate termination");auto cfg=Config();cfg.maxDepth=1;for(unsigned i=0;i<20;++i)g[i].position[0]=float(i);auto shallow=Build(c,g,cfg,"shallow");for(auto& n:shallow.nodes)Require(n.level<=1,"depth bound");});
    t.Test("T03 unique exact leaves and conservative cross-cell support",[]{Context c("coverage");std::vector<FSplatGaussian> g;for(unsigned i=0;i<16;++i){g.push_back(Gaussian(i,float(i)-8));g.back().scale[0]=5;}auto m=Build(c,g);std::set<uint64_t> seen;for(auto& n:m.nodes){for(auto child:n.children)for(unsigned k=0;k<3;++k)Require(n.bounds.min[k]<=m.nodes[child].bounds.min[k]&&n.bounds.max[k]>=m.nodes[child].bounds.max[k],"parent support coverage");if(!n.children.empty())continue;for(auto p:n.pageIds)for(auto& x:Page(c.dir/"scene",m,p)){Require(seen.insert(x.originalId).second,"duplicate leaf identity");Require(Same(x,g[x.originalId]),"leaf attributes changed");for(unsigned k=0;k<3;++k)Require(n.bounds.min[k]<=x.position[k]-m.supportSigma*x.scale[k]&&n.bounds.max[k]>=x.position[k]+m.supportSigma*x.scale[k],"ellipsoid support omitted");}}Require(seen.size()==g.size(),"leaf identity missing");});
    t.Test("T04 two Gaussian moment covariance oracle",[]{Context c("moment");auto cfg=Config();cfg.maxLeafGaussians=1;auto a=Gaussian(0,-1),b=Gaussian(1,1);for(unsigned k=0;k<3;++k)a.scale[k]=b.scale[k]=1;for(auto& x:a.sh)x=0;for(auto& x:b.sh)x=0;auto m=Build(c,{a,b},cfg);auto gs=Page(c.dir/"scene",m,m.nodes[0].pageIds[0]);Require(gs.size()==1,"two equal moments did not merge");auto& g=gs[0];double w=g.rotation[0],x=g.rotation[1],y=g.rotation[2],z=g.rotation[3];double r[3][3]={{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},{2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},{2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){double cov=0;for(unsigned k=0;k<3;++k)cov+=r[i][k]*r[j][k]*g.scale[k]*g.scale[k];Require(Near(cov,i==j?(i==0?2:1):0,1e-4),"moment covariance mismatch");}Require(Near(g.position[0],0)&&g.opacity>=0&&g.opacity<=1,"moment center/opacity");});
    t.Test("T05 SHA256 known vector and format roundtrip",[]{Require(FSplatSceneFormat::SHA256({'a','b','c'})=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 oracle");Context c("roundtrip");auto m=Build(c,{Gaussian(0)});FSplatSceneManifest loaded;std::string e;Require(FSplatSceneFormat::LoadManifest((c.dir/"scene/scene.mgs").string(),loaded,e),e);Require(loaded.sourceCount==1&&loaded.configHash==m.configHash,"manifest identity");Require(Same(Page(c.dir/"scene",loaded,0)[0],Gaussian(0)),"FP32 roundtrip");});
    t.Test("T05 rejects checksum version offset path and size overflow",[]{Context c("format-invalid");auto m=Build(c,{Gaussian(0)});std::string e;auto bytes=Read(c.dir/"scene"/m.pages[0].relativePath);std::vector<FSplatGaussian> out;bytes.back()^=1;Require(!FSplatSceneFormat::DecodePage(m,0,bytes,out,e),"checksum accepted");bytes[4]=2;m.pages[0].sha256=FSplatSceneFormat::SHA256(bytes);Require(!FSplatSceneFormat::DecodePage(m,0,bytes,out,e),"version accepted");m.pages[0].offset=UINT64_MAX;Require(!FSplatSceneFormat::ValidateManifest(m,e),"offset overflow accepted");m.pages[0].offset=0;m.pages[0].relativePath="../escape";Require(!FSplatSceneFormat::ValidateManifest(m,e),"escaping page path accepted");m.pages[0].relativePath="pages/p";m.pages[0].decodedByteSize=UINT64_MAX;Require(!FSplatSceneFormat::ValidateManifest(m,e),"decoded allocation overflow accepted");});
    t.Test("T06 visibility resolution FOV orthographic and near-plane",[]{auto m=SmallTree();auto r=Ready(m);auto v=View();auto s=Settings();s.qualityPixels=5;FSplatLODSelector sel;Require(sel.Select(m,v,s,r).selectedNodeIds==std::vector<uint32_t>{0},"coarse low-resolution cut");v.width=v.height=1024;Require(sel.Select(m,v,s,r).selectedNodeIds==std::vector<uint32_t>({1,2}),"resolution did not refine");sel.Reset();v=View();v.projection[0]=v.projection[5]=4;Require(sel.Select(m,v,s,r).selectedNodeIds.size()==2,"narrower FOV did not refine");sel.Reset();v=View();v.projection[11]=0;v.projection[15]=1;s.qualityPixels=1;Require(sel.Select(m,v,s,r).selectedNodeIds.size()==2,"orthographic projected error");m.nodes[0].bounds.min[2]=-.2f;m.nodes[0].bounds.max[2]=.2f;Require(!sel.Select(m,v,s,r).selectedNodeIds.empty(),"near-plane support vanished");for(auto& n:m.nodes)n.bounds.min[0]=n.bounds.max[0]=100;Require(sel.Select(m,v,s,r).selectedNodeIds.empty(),"off-frustum cut retained");});
    t.Test("T06 hysteresis freeze and residency invalidation",[]{auto m=SmallTree();auto r=Ready(m);auto v=View();auto s=Settings();s.qualityPixels=3.3f;FSplatLODSelector sel;Require(sel.Select(m,v,s,r).selectedNodeIds.size()==1,"initial refine deadband");s.qualityPixels=1;auto fine=sel.Select(m,v,s,r);Require(fine.selectedNodeIds.size()==2,"initial fine cut");s.qualityPixels=3.3f;Require(sel.Select(m,v,s,r).selectedNodeIds.size()==2,"hysteresis oscillation");s.freeze=true;s.qualityPixels=1000;Require(sel.Select(m,v,s,r).selectedNodeIds.size()==2,"freeze changed cut");++r.residencyVersion;Require(sel.Select(m,v,s,r).selectionVersion>fine.selectionVersion,"residency did not invalidate");});
    t.Test("T07 partial multipage readiness preserves parent",[]{auto m=SmallTree();auto r=Ready(m);r.pages[3].ready=false;FSplatLODSelector sel;auto cut=sel.Select(m,View(),Settings(),r);Require(cut.selectedNodeIds==std::vector<uint32_t>{0},"partial child replaced parent");Require(cut.requestPageIds==std::vector<uint32_t>{3},"missing child request");r.pages[3].ready=true;++r.residencyVersion;Require(sel.Select(m,View(),Settings(),r).selectedNodeIds==std::vector<uint32_t>({1,2}),"complete children not selected");});
    t.Test("T07 transition active source budgets preserve coverage",[]{auto m=SmallTree();auto r=Ready(m);auto s=Settings();s.sourceBudget=3;FSplatLODSelector sel;Require(sel.Select(m,View(),s,r).selectedNodeIds==std::vector<uint32_t>{0},"transition reserve exceeded");s.sourceBudget=100;s.activeBudget=1;auto cut=sel.Select(m,View(),s,r);Require(cut.selectedNodeIds.empty()&&cut.degraded,"impossible root budget not rejected");});
    t.Test("T07 downgrade keeps ready children until ancestor ready",[]{auto m=SmallTree();auto r=Ready(m);auto s=Settings();FSplatLODSelector sel;Require(sel.Select(m,View(),s,r).selectedNodeIds.size()==2,"setup refined cut");r.pages[0].ready=false;r.pages[1].ready=false;++r.residencyVersion;s.qualityPixels=10000;auto cut=sel.Select(m,View(),s,r);Require(cut.selectedNodeIds==std::vector<uint32_t>({1,2}),"ready children disappeared before coarse ancestor upload completed");Require(cut.requestPageIds==std::vector<uint32_t>({0,1}),"ancestor refill missing");});
    t.Test("T08 async dedup and external leases survive shutdown",[]{Context c("leases");auto m=Build(c,{Gaussian(0)});FSplatPageStreamer stream;FSplatStreamingConfig cfg;cfg.cpuCacheBytes=4096;std::string e;Require(stream.Open(m,(c.dir/"scene").string(),cfg,e),e);stream.Request({{0,1,1},{0,10,1}});auto out=Wait(stream);Require(out[0].error.empty()&&out[0].decodedGaussians[0].originalId==0,"valid page read");Require(stream.GetStats().completed==1,"duplicate read");auto charge=stream.GetMemoryStats();Require(charge.transferredPages==1&&charge.transferredBytes>0&&charge.peakReservedBytes<=cfg.cpuCacheBytes,"external budget charge");stream.Shutdown();Require(stream.GetMemoryStats().transferredBytes>0,"shutdown uncharged live vector");Require(!stream.Open(m,(c.dir/"scene").string(),cfg,e),"reopen accepted live lease");Release(stream,out[0]);Require(!stream.ReleaseTransferred(0,1),"duplicate release accepted");Require(stream.GetMemoryStats().totalReservedBytes==0,"shutdown leak");Require(stream.Open(m,(c.dir/"scene").string(),cfg,e),e);stream.Request({{0,1,2}});out=Wait(stream);Release(stream,out[0]);stream.Shutdown();});
    for(unsigned fault=0;fault<3;++fault)t.Test("T08 bounded terminal retry fault "+std::to_string(fault),[=]{Context c("retry");auto m=Build(c,{Gaussian(0)});FSplatPageStreamer stream;FSplatStreamingConfig cfg;cfg.cpuCacheBytes=4096;cfg.retries=2;cfg.retryDelayMilliseconds=1;std::string e;Require(stream.Open(m,(c.dir/"scene").string(),cfg,e),e);std::atomic<unsigned> attempts{0};stream.SetFaultInjector([&](unsigned,unsigned){++attempts;FSplatPageReadFault f;f.failRead=fault==0;f.corruptChecksum=fault==1;f.truncateBytes=fault==2?1:0;return f;});stream.Request({{0,1,1}});auto out=Wait(stream);Require(!out[0].error.empty()&&attempts==3&&stream.GetStats().retries==2,"terminal retry bound");Release(stream,out[0]);for(unsigned i=0;i<100;++i)stream.Request({{0,1,1}});std::this_thread::sleep_for(std::chrono::milliseconds(30));Require(attempts==3&&stream.DrainReady().empty(),"released terminal request restarted");stream.Shutdown();Require(stream.GetMemoryStats().totalReservedBytes==0,"terminal history leak");});
    t.Test("T08 delayed generation cancellation and prompt shutdown",[]{Context c("cancel");auto m=Build(c,{Gaussian(0)});FSplatPageStreamer stream;FSplatStreamingConfig cfg;cfg.cpuCacheBytes=4096;std::string e;Require(stream.Open(m,(c.dir/"scene").string(),cfg,e),e);std::atomic<bool> started{false};stream.SetFaultInjector([&](unsigned,unsigned){started=true;FSplatPageReadFault f;f.delayMilliseconds=5000;return f;});stream.Request({{0,1,1}});auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(!started&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));Require(started,"worker never started");auto start=std::chrono::steady_clock::now();stream.CancelGeneration(1);stream.Shutdown();Require(std::chrono::steady_clock::now()-start<std::chrono::seconds(1),"shutdown failed interrupt delay");Require(stream.DrainReady().empty()&&stream.GetMemoryStats().totalReservedBytes==0,"old completion/leak");Require(stream.Open(m,(c.dir/"scene").string(),cfg,e),e);stream.SetFaultInjector({});stream.Request({{0,1,1}});std::this_thread::sleep_for(std::chrono::milliseconds(20));Require(stream.DrainReady().empty(),"stale scene generation accepted");stream.Request({{0,1,2}});auto out=Wait(stream);Require(out[0].sceneGeneration==2,"new generation rejected");Release(stream,out[0]);stream.Shutdown();});
    t.Test("T08 exact single-page CPU budget loads without optional cache",[]{Context c("exact-cpu");auto m=Build(c,{Gaussian(0)});FSplatStreamingConfig cfg;cfg.cpuCacheBytes=1024+m.pages[0].byteSize+m.pages[0].decodedByteSize;FSplatPageStreamer s;std::string e;Require(s.Open(m,(c.dir/"scene").string(),cfg,e),e);s.Request({{0,1,1}});auto out=Wait(s);Require(out[0].error.empty(),"exact working-set read failed");auto mem=s.GetMemoryStats();Require(mem.peakReservedBytes<=cfg.cpuCacheBytes&&mem.encodedCacheBytes==0,"optional cache exceeded hard budget");Release(s,out[0]);s.Shutdown();--cfg.cpuCacheBytes;Require(!s.Open(m,(c.dir/"scene").string(),cfg,e),"impossible coarse CPU working set accepted");});
    t.Test("T08 verified cache hit avoids repeated disk reads",[]{Context c("cache-hit");auto m=Build(c,{Gaussian(0)});FSplatStreamingConfig cfg;cfg.cpuCacheBytes=4096;FSplatPageStreamer s;std::string e;Require(s.Open(m,(c.dir/"scene").string(),cfg,e),e);s.Request({{0,1,1}});auto out=Wait(s);Release(s,out[0]);auto first=s.GetStats().bytesRead;s.Request({{0,1,1}});out=Wait(s);Require(out[0].error.empty()&&s.GetStats().cacheHits==1&&s.GetStats().bytesRead==first,"cache did not reuse verified bytes");Release(s,out[0]);s.Shutdown();});
    t.Test("T05 truncated and bad-version manifest with valid outer checksum",[]{Context c("manifest-invalid");Build(c,{Gaussian(0)});auto p=c.dir/"scene/scene.mgs";auto original=Read(p);FSplatSceneManifest m;std::string e;auto shortFile=original;shortFile.resize(17);Write(c.dir/"short.mgs",shortFile);Require(!FSplatSceneFormat::LoadManifest((c.dir/"short.mgs").string(),m,e),"short manifest accepted");original.resize(original.size()-64);original[4]=255;auto checksum=FSplatSceneFormat::SHA256(original);original.insert(original.end(),checksum.begin(),checksum.end());Write(c.dir/"version.mgs",original);Require(!FSplatSceneFormat::LoadManifest((c.dir/"version.mgs").string(),m,e),"unsupported manifest version with valid checksum accepted");});
    t.Test("T07 rejected expensive refinement preserves cheaper sibling and pinned charge",[]{
        auto m=SmallTree();m.nodes.resize(7);m.pages.resize(8);
        for(unsigned id=3;id<7;++id){m.nodes[id]=m.nodes[2];m.nodes[id].pageIds={id+1};m.nodes[id].level=2;}
        for(unsigned p=4;p<8;++p){m.pages[p]=m.pages[3];m.pages[p].id=p;}
        m.nodes[1].children={3,4};m.nodes[1].geometricError=100;
        m.nodes[2].children={5,6};m.nodes[2].geometricError=1;
        for(unsigned id:{3u,4u}){m.nodes[id].gaussianCount=3;m.pages[id+1].gaussianCount=3;}
        auto s=Settings();s.activeBudget=4;FSplatLODSelector selector;
        auto cut=selector.Select(m,View(),s,Ready(m));
        Require(cut.desiredNodeIds==std::vector<uint32_t>({1,5,6})&&cut.activeCount==3,"rejected branch corrupted sibling budget or priority");
        selector.Reset();s.sourceBudget=3;
        cut=selector.Select(m,View(),s,Ready(m));
        Require(cut.desiredNodeIds==std::vector<uint32_t>{0},"pinned root charge lost during replacement");
    });
    RunCPUExtra(t);
    t.Test("T05 v1-v5 roundtrip and leaf-self membership validation",[]{
        Context c("format-compatibility");auto cfg=Config();cfg.maxDepth=0;
        auto base=Build(c,{Gaussian(0),Gaussian(1)},cfg);std::string error;
        const char* builders[]={"MonsterEngine-SplatLOD-1",SplatRMSErrorBuilderVersion,SplatCenterErrorBuilderVersion,SplatFusionBuilderVersion,SplatLeafFusionBuilderVersion};
        for(unsigned version=1;version<=5;++version){
            auto m=base;m.version=version;m.builderVersion=builders[version-1];
            if(version==1)for(auto& n:m.nodes)n.coverageAreaUpper=0;
            auto path=c.dir/("v"+std::to_string(version)+".mgs");FSplatSceneManifest loaded;
            Require(FSplatSceneFormat::SaveManifest(path.string(),m,error),error);
            Require(FSplatSceneFormat::LoadManifest(path.string(),loaded,error)&&loaded.version==version,error);
        }
        auto m=base;m.version=5;m.builderVersion=SplatLeafFusionBuilderVersion;
        FSplatOriginalFusionGroup group;group.count=2;group.rows[0]=0;group.rows[1]=1;
        m.nodes[0].originalFusionGroups={group};Require(FSplatSceneFormat::ValidateManifest(m,error),error);
        auto path=c.dir/"leaf-self.mgs";FSplatSceneManifest loaded;
        Require(FSplatSceneFormat::SaveManifest(path.string(),m,error)&&FSplatSceneFormat::LoadManifest(path.string(),loaded,error),error);
        Require(loaded.nodes[0].originalFusionGroups[0].rows[1]==1,"leaf-self identity lost");
        m.nodes[0].originalFusionGroups[0].rows[1]=0;Require(!FSplatSceneFormat::ValidateManifest(m,error),"duplicate original accepted");
        m.nodes[0].originalFusionGroups[0].rows[1]=2;Require(!FSplatSceneFormat::ValidateManifest(m,error),"out-of-range original accepted");
        m.nodes[0].originalFusionGroups[0].count=1;Require(!FSplatSceneFormat::ValidateManifest(m,error),"missing original accepted");
        m.nodes[0].originalFusionGroups={group};m.version=4;m.builderVersion=SplatFusionBuilderVersion;
        Require(!FSplatSceneFormat::ValidateManifest(m,error),"v4 leaf-self accepted");
    });
    t.Test("T07 ready siblings remain charged across simultaneous refinements",[]{
        auto m=SmallTree();m.nodes.resize(7);m.pages.resize(8);
        for(unsigned i=0;i<8;++i){m.pages[i].id=i;m.pages[i].gaussianCount=1;}
        for(unsigned i=1;i<7;++i){m.nodes[i].bounds=m.nodes[0].bounds;m.nodes[i].pageIds={i+1};m.nodes[i].gaussianCount=1;m.nodes[i].level=i<3?1:2;}
        m.nodes[1].children={3,4};m.nodes[2].children={5,6};
        m.nodes[1].geometricError=m.nodes[2].geometricError=1;
        auto r=Ready(m);r.pages[5].ready=r.pages[7].ready=false;
        auto settings=Settings();settings.sourceBudget=7;FSplatLODSelector selector;
        auto cut=selector.Select(m,View(),settings,r);
        Require(cut.selectedNodeIds==std::vector<uint32_t>({1,2}),"incomplete children replaced a parent");
        Require(cut.requestPageIds.size()==1,"ready sibling reservation over-admitted two transitions");
        r.pages[cut.requestPageIds[0]].ready=true;++r.residencyVersion;
        cut=selector.Select(m,View(),settings,r);
        Require(cut.activeCount>=2&&!cut.selectedNodeIds.empty(),"completed replacement lost fallback coverage");
    });
    RunRound3CPU(t);
    t.Test("T07 evicted intermediate parent does not reload a complete fine frontier",[]{
        auto m=SmallTree();m.nodes.resize(7);m.pages.resize(8);
        for(unsigned i=0;i<8;++i){m.pages[i].id=i;m.pages[i].gaussianCount=1;}
        for(unsigned i=1;i<7;++i){m.nodes[i].bounds=m.nodes[0].bounds;m.nodes[i].pageIds={i+1};m.nodes[i].gaussianCount=1;m.nodes[i].level=i<3?1:2;}
        m.nodes[1].children={3,4};m.nodes[2].children={5,6};m.nodes[1].geometricError=m.nodes[2].geometricError=1;
        auto r=Ready(m);auto settings=Settings();FSplatLODSelector selector;
        Require(selector.Select(m,View(),settings,r).selectedNodeIds==std::vector<uint32_t>({3,4,5,6}),"initial fine cut");
        r.pages[2].ready=r.pages[3].ready=false;++r.residencyVersion;
        auto cut=selector.Select(m,View(),settings,r);
        Require(cut.selectedNodeIds==std::vector<uint32_t>({3,4,5,6})&&cut.requestPageIds.empty(),"fine cut discarded to reload obsolete parents");
        m.nodes[1].geometricError=m.nodes[2].geometricError=0;
        cut=selector.Select(m,View(),settings,r);
        Require(cut.selectedNodeIds==std::vector<uint32_t>({3,4,5,6})&&cut.requestPageIds.size()==2,"missing coarsening ancestors discarded complete fine coverage");
        m.nodes[1].geometricError=m.nodes[2].geometricError=1;
        r.pages[7].ready=false;++r.residencyVersion;
        cut=selector.Select(m,View(),settings,r);
        Require(cut.activeCount>0&&cut.selectedNodeIds!=std::vector<uint32_t>({3,4,5}),"incomplete descendant frontier reused");
    });
    RunStreamerLifetimeTests(t);
    t.Test("T07 sparse empty branch does not force root reload after parent eviction",[]{
        auto m=SmallTree();m.nodes.resize(7);m.pages.resize(8);
        for(unsigned i=0;i<8;++i){m.pages[i].id=i;m.pages[i].gaussianCount=1;}
        for(unsigned i=1;i<7;++i){m.nodes[i].bounds=m.nodes[0].bounds;m.nodes[i].pageIds={i+1};m.nodes[i].gaussianCount=1;m.nodes[i].level=i<3?1:2;}
        m.nodes[1].children={3,4};m.nodes[2].children={5,6};m.nodes[1].geometricError=m.nodes[2].geometricError=1;
        m.nodes[0].bounds.min[0]=m.nodes[1].bounds.min[0]=-21;m.nodes[0].bounds.max[0]=m.nodes[1].bounds.max[0]=21;
        m.nodes[3].bounds.min[0]=-21;m.nodes[3].bounds.max[0]=-20;
        m.nodes[4].bounds.min[0]=20;m.nodes[4].bounds.max[0]=21;
        auto r=Ready(m);auto settings=Settings();FSplatLODSelector selector;
        auto first=selector.Select(m,View(),settings,r);
        Require(first.selectedNodeIds==std::vector<uint32_t>({5,6}),"sparse visibility fixture");
        r.pages[2].ready=false;++r.residencyVersion;
        auto next=selector.Select(m,View(),settings,r);
        Require(next.selectedNodeIds==first.selectedNodeIds&&next.requestPageIds.empty(),"valid empty branch reloaded an obsolete parent");
    });
    std::cout<<"SUMMARY total="<<t.passed+t.failed<<" passed="<<t.passed<<" failed="<<t.failed<<'\n';return t.failed?1:0;
}
