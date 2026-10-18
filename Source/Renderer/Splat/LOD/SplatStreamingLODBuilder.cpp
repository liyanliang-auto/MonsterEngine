// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatStreamingLODBuilder.h"
#include "Renderer/Splat/LOD/SplatBuildJournal.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include "Renderer/Splat/LOD/SplatSceneValidation.h"
#include "Renderer/Splat/SplatPLYStream.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
namespace fs = std::filesystem;
void Need(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
uint64_t Add(uint64_t a, uint64_t b) { Need(a <= UINT64_MAX-b,"Builder size overflow"); return a+b; }
uint64_t Mul(uint64_t a, uint64_t b) { Need(!b || a <= UINT64_MAX/b,"Builder size overflow"); return a*b; }
void Poll(const FSplatBuildControl& control) {
    const auto signal = control.Poll(); Need(signal != ESplatBuildSignal::Cancel,"cancelled"); Need(signal != ESplatBuildSignal::Pause,"paused");
}
bool HashValid(const std::string& hash) { return hash.size()==64 && hash.find_first_not_of("0123456789abcdef")==std::string::npos; }
std::string UTF8(const fs::path& path) { const auto value=path.u8string(); return {reinterpret_cast<const char*>(value.data()),value.size()}; }
uint64_t ChunkID(uint64_t task, uint32_t slot) { Need(task && task<=UINT32_MAX,"Builder task identity exhausted"); return (task<<32)|slot; }
uint32_t Slot(uint64_t chunk) { return uint32_t(chunk); }
std::string NodeFile(uint32_t id,uint32_t state) { return "node-index/n"+std::to_string(id)+"-s"+std::to_string(state)+".bin"; }
std::string PageFile(uint32_t id) { return "pages/p"+std::to_string(id)+".mgsp"; }

// Private constant-space SHA256. Source hashing polls every 64KiB; the journal
// independently verifies the resulting identity before accepting any checkpoint.
struct HashState {
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint8_t block[64]{}; size_t used=0; uint64_t total=0;
    static uint32_t R(uint32_t x,int n){return (x>>n)|(x<<(32-n));}
    void Transform(){
        static constexpr uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t(block[4*i])<<24)|(uint32_t(block[4*i+1])<<16)|(uint32_t(block[4*i+2])<<8)|block[4*i+3];
        for(int i=16;i<64;++i)w[i]=w[i-16]+(R(w[i-15],7)^R(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(R(w[i-2],17)^R(w[i-2],19)^(w[i-2]>>10));
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
        for(int i=0;i<64;++i){uint32_t t=z+(R(e,6)^R(e,11)^R(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t u=(R(a,2)^R(a,13)^R(a,22))+((a&b)^(a&c)^(b&c));z=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
    }
    void Update(const uint8_t* p,size_t n){total+=n;while(n){size_t s=std::min(n,64-used);std::memcpy(block+used,p,s);used+=s;p+=s;n-=s;if(used==64){Transform();used=0;}}}
    std::string Finish(){uint64_t bits=total*8;uint8_t one=0x80;Update(&one,1);uint8_t zero=0;while(used!=56)Update(&zero,1);uint8_t tail[8];for(int i=0;i<8;++i)tail[i]=uint8_t(bits>>(56-8*i));Update(tail,8);std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto x:h)s<<std::setw(8)<<x;return s.str();}
};
std::string HashSource(const std::string& path,const FSplatBuildControl& control,const std::function<void(uint64_t)>& progress) {
    std::ifstream file(fs::u8path(path),std::ios::binary); Need(bool(file),"Cannot read stable source identity");
    HashState hash; std::array<uint8_t,65536> bytes;
    uint64_t processed=0;while(file){Poll(control);file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());hash.Update(bytes.data(),size_t(file.gcount()));processed+=uint64_t(file.gcount());if(progress)progress(processed);}
    Need(file.eof(),"Source identity read failed");Poll(control);return hash.Finish();
}
struct Wire {
    std::vector<uint8_t> bytes; size_t at=0;
    void U32(uint32_t n){for(int i=0;i<4;++i)bytes.push_back(uint8_t(n>>(i*8)));}
    void U64(uint64_t n){for(int i=0;i<8;++i)bytes.push_back(uint8_t(n>>(i*8)));}
    void F32(float n){uint32_t u;std::memcpy(&u,&n,4);U32(u);}
    void F64(double n){uint64_t u;std::memcpy(&u,&n,8);U64(u);}
    void Text(const std::string& s){Need(s.size()<=65536,"Builder wire text limit");U32(uint32_t(s.size()));bytes.insert(bytes.end(),s.begin(),s.end());}
    uint32_t R32(){Need(at<=bytes.size()&&bytes.size()-at>=4,"Truncated builder metadata");uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(bytes[at++])<<(i*8);return n;}
    uint64_t R64(){Need(at<=bytes.size()&&bytes.size()-at>=8,"Truncated builder metadata");uint64_t n=0;for(int i=0;i<8;++i)n|=uint64_t(bytes[at++])<<(i*8);return n;}
    float RF32(){auto u=R32();float n;std::memcpy(&n,&u,4);return n;}
    double RF64(){auto u=R64();double n;std::memcpy(&n,&u,8);return n;}
    void Bounds(const FSplatBounds& b){for(float f:b.min)F32(f);for(float f:b.max)F32(f);}
    FSplatBounds ReadBounds(){FSplatBounds b;for(auto& f:b.min)f=RF32();for(auto& f:b.max)f=RF32();return b;}
};
struct Node {
    uint32_t id=0,parent=SplatInvalidId,depth=0,state=0;
    uint64_t count=0,input=0,weighted=0,task=0;
    uint32_t childCount=0,children[8]{},representation=0,pageStart=0,pageCount=0;
    FSplatProxySummary summary;
};
struct Transaction { uint32_t nodeCount=0;std::vector<Node> nodes; };
Wire EncodeState(const Transaction& tx) {
    Wire w;w.U32(0x5342474d);w.U32(1);w.U32(tx.nodeCount);w.U32(uint32_t(tx.nodes.size()));
    for(const auto& n:tx.nodes){w.U32(n.id);w.U32(n.parent);w.U32(n.depth);w.U32(n.state);w.U64(n.count);w.U64(n.input);w.U64(n.weighted);w.U64(n.task);
        w.U32(n.childCount);for(auto c:n.children)w.U32(c);w.U32(n.representation);w.U32(n.pageStart);w.U32(n.pageCount);
        w.U32(n.summary.hasBounds?1:0);w.Bounds(n.summary.supportBounds);w.Bounds(n.summary.centerBounds);
        w.F64(n.summary.coverageAreaUpper);w.F64(n.summary.geometricError);w.F64(n.summary.appearanceError);}
    return w;
}
Transaction DecodeState(std::vector<uint8_t> bytes,const FSplatBoundedBuildConfig& config) {
    Wire w;w.bytes=std::move(bytes);Need(w.R32()==0x5342474d&&w.R32()==1,"Unsupported builder node schema");
    Transaction tx;tx.nodeCount=w.R32();const auto count=w.R32();Need(count>=1&&count<=9&&tx.nodeCount<=config.maxNodes,"Builder node metadata admission");tx.nodes.reserve(count);
    for(uint32_t i=0;i<count;++i){Node n;n.id=w.R32();n.parent=w.R32();n.depth=w.R32();n.state=w.R32();n.count=w.R64();n.input=w.R64();n.weighted=w.R64();n.task=w.R64();
        n.childCount=w.R32();for(auto& c:n.children)c=w.R32();n.representation=w.R32();n.pageStart=w.R32();n.pageCount=w.R32();
        const auto bounds=w.R32();n.summary.hasBounds=bounds!=0;n.summary.supportBounds=w.ReadBounds();n.summary.centerBounds=w.ReadBounds();
        n.summary.coverageAreaUpper=w.RF64();n.summary.geometricError=w.RF64();n.summary.appearanceError=w.RF64();
        Need(n.id<tx.nodeCount&&n.depth<=config.maxDepth&&n.state<=2&&n.childCount<=8&&n.count<=config.maxSourceRecords&&bounds<=1,"Invalid builder node metadata");
        Need(uint64_t(n.pageStart)+n.pageCount<=config.maxPages,"Builder page metadata admission");
        for(uint32_t j=0;j<n.childCount;++j)Need(n.children[j]<tx.nodeCount,"Invalid builder child identity");
        tx.nodes.push_back(n);}
    Need(w.at==w.bytes.size(),"Unexpected builder metadata tail");return tx;
}
Wire EncodeManifest(const FSplatSceneManifest& m,const FSplatBoundedBuildConfig& config,FSplatBuildControl& control) {
    std::string error;Need(FSplatSceneFormat::ValidateManifest(m,error),error);Wire w;
    w.U32(0x4d53474d);w.U32(3);w.U32(m.shDegree);
    for(const auto* s:{&m.sourceSHA256,&m.builderVersion,&m.builderHash,&m.configHash,&m.buildConfig,&m.endian,&m.coordinates,&m.quaternion,&m.shLayout})w.Text(*s);
    w.F32(m.supportSigma);w.U32(m.rootNode);w.U64(m.sourceCount);w.U64(m.leafCount);w.U64(m.proxyCount);w.U64(m.totalDataBytes);
    w.U32(uint32_t(m.nodes.size()));w.U32(uint32_t(m.pages.size()));
    for(const auto& n:m.nodes){Poll(control);w.Bounds(n.bounds);w.U32(uint32_t(n.children.size()));for(auto c:n.children)w.U32(c);w.U32(uint32_t(n.pageIds.size()));for(auto p:n.pageIds)w.U32(p);
        w.U32(n.gaussianCount);w.F32(n.geometricError);w.F32(n.appearanceError);w.U32(n.level);w.F64(n.coverageAreaUpper);w.Bounds(n.centerBounds);Need(w.bytes.size()<=config.maxManifestBytes,"Encoded manifest admission");}
    for(const auto& p:m.pages){Poll(control);w.U32(p.id);w.Text(p.relativePath);w.U64(p.offset);w.U64(p.byteSize);w.U64(p.decodedByteSize);w.U32(p.gaussianCount);w.Text(p.sha256);w.U32(p.pinned?1:0);Need(w.bytes.size()<=config.maxManifestBytes,"Encoded manifest admission");}
    const auto hash=FSplatSceneFormat::SHA256(w.bytes);w.bytes.insert(w.bytes.end(),hash.begin(),hash.end());Need(w.bytes.size()<=config.maxManifestBytes,"Encoded manifest admission");return w;
}

class Worker {
public:
    const FSplatBoundedBuildConfig& config; FSplatBuildControl& control; FSplatBuildProgressCallback callback;
    FSplatBuildJournal journal; FSplatBuildProgress progress; FSplatBuildIdentity identity; FSplatPLYHeader header;
    std::string source,root,output,error; uint32_t nodeCount=0;uint64_t working=0,peak=0;
    Worker(const FSplatBoundedBuildConfig& c,FSplatBuildControl& signal,FSplatBuildProgressCallback cb):config(c),control(signal),callback(std::move(cb)){}
    void Check(bool result){Need(result,error);}
    uint64_t NewTask() const {const auto n=Add(journal.Checkpoint().generation,1);Need(n<=UINT32_MAX,"Builder task limit");return n;}
    std::unique_ptr<FSplatBuildChunkWriter> Writer(uint64_t task,uint32_t slot,uint32_t stride){std::unique_ptr<FSplatBuildChunkWriter>w;Check(journal.CreateChunk(ChunkID(task,slot),task,stride,control,w,error));return w;}
    std::unique_ptr<FSplatBuildChunkReader> Reader(uint64_t id){std::unique_ptr<FSplatBuildChunkReader>r;Check(journal.OpenChunk(id,control,r,error));return r;}
    std::vector<uint8_t> Opaque(uint64_t id,uint64_t limit=65536){auto r=Reader(id);Need(r->Descriptor().recordBytes==1&&r->RemainingRecords()<=limit,"Opaque metadata admission");std::vector<uint8_t>out;out.reserve(size_t(r->RemainingRecords()));while(r->RemainingRecords()){std::vector<uint8_t>b;Check(r->ReadBytes(uint32_t(std::min<uint64_t>(r->RemainingRecords(),1u<<20)),b,error));out.insert(out.end(),b.begin(),b.end());}return out;}
    uint64_t DiskBytes(){uint64_t bytes=identity.sourceBytes;const auto sourcePath=fs::canonical(fs::u8path(source));const auto rootPath=fs::canonical(fs::u8path(root));const auto relative=sourcePath.lexically_relative(rootPath);if(!relative.empty()&&*relative.begin()!="..")bytes=0;
        auto scan=[&](const fs::path& directory){for(fs::recursive_directory_iterator it(directory),end;it!=end;++it){Poll(control);Need(it.depth()<=72&&!it->is_symlink(),"Unsafe builder job tree");bytes=Add(bytes,4096);if(it->is_regular_file())bytes=Add(bytes,it->file_size());}};scan(rootPath);
        if((journal.Checkpoint().published||journal.Checkpoint().stage==ESplatBuildStage::Publishing)&&fs::is_directory(fs::u8path(output)))scan(fs::u8path(output));return bytes;}
    void Admit(uint64_t dynamic,uint64_t additional=0){Poll(control);working=Add(config.metadataBytes,dynamic);FSplatBuildAdmission a;a.workingBytes=working;a.metadataBytes=config.metadataBytes;a.additionalDiskBytes=additional;Check(journal.CheckAdmission(a,control,error));peak=std::max(peak,working);progress.diskBytes=DiskBytes();}
    void Notify(ESplatBuildStage stage,uint64_t processed=0,uint64_t total=0){progress.stage=stage;progress.processedRecords=processed;progress.totalRecords=total;progress.totalBytes=identity.sourceBytes;progress.processedBytes=journal.Checkpoint().sourceCursor.byteOffset;
        progress.workingBytes=working;progress.metadataBytes=config.metadataBytes;progress.peakWorkingBytes=peak;progress.checkpointGeneration=journal.Checkpoint().generation;progress.status.clear();progress.error.clear();if(callback)callback(progress);}
    FSplatBuildChunk Finish(std::unique_ptr<FSplatBuildChunkWriter>& writer){FSplatBuildChunk d;Check(writer->Finish(d,error));writer.reset();return d;}
    FSplatBuildChunk Bytes(uint64_t task,uint32_t slot,std::span<const uint8_t> bytes){auto w=Writer(task,slot,1);for(size_t at=0;at<bytes.size();){const auto n=std::min<size_t>(1u<<20,bytes.size()-at);Check(w->AppendBytes(bytes.subspan(at,n),error));at+=n;}return Finish(w);}
    void Commit(FSplatBuildTask task,std::vector<FSplatBuildChunk> chunks,FSplatBuildCheckpoint next){Check(journal.CommitWorkUnit(task,chunks,next,control,error));}
    void MaterializeState(uint64_t chunk,const Transaction& tx){for(const auto& n:tx.nodes)Check(journal.MaterializeChunk(chunk,NodeFile(n.id,n.state),control,error));nodeCount=std::max(nodeCount,tx.nodeCount);}
    void Replay(){
        // This directory is a rebuildable cache, never a durable authority. A
        // fallback to checkpoint.previous may invalidate its newest entries.
        const auto index=fs::u8path(root)/"node-index";
        if(fs::exists(index)){Need(!fs::is_symlink(index)&&fs::is_directory(index)&&fs::canonical(index).parent_path()==fs::canonical(fs::u8path(root)),"Unsafe derived node index directory");
            for(const auto& entry:fs::directory_iterator(index)){Poll(control);const auto name=UTF8(entry.path().filename());Need(entry.is_regular_file()&&!entry.is_symlink()&&name.size()>8&&name.front()=='n',"Unexpected derived index entry");
                const auto separator=name.find("-s");Need(separator>1&&separator!=std::string::npos&&separator+7==name.size()&&name.substr(separator+3)==".bin"&&name[separator+2]>='0'&&name[separator+2]<='2'&&name.substr(1,separator-1).find_first_not_of("0123456789")==std::string::npos,"Unexpected derived index filename");
                Need(fs::remove(entry.path()),"Cannot discard derived node index cache");}}
        Check(journal.ReadChunks([&](const FSplatBuildChunk& d,bool retired,std::string&){if(!retired&&Slot(d.id)==1&&d.recordBytes==1){const auto tx=DecodeState(Opaque(d.id),config);MaterializeState(d.id,tx);}return true;},control,error));}
    Node LoadNode(uint32_t id){for(int state=2;state>=0;--state){auto path=fs::u8path(root)/NodeFile(id,uint32_t(state));if(!fs::exists(path))continue;Need(!fs::is_symlink(path)&&fs::is_regular_file(path)&&fs::file_size(path)<=65536,"Unsafe node index");std::ifstream f(path,std::ios::binary);Need(bool(f),"Cannot read builder node index");std::vector<uint8_t> bytes(size_t(fs::file_size(path)));Need(bool(f.read(reinterpret_cast<char*>(bytes.data()),bytes.size())),"Truncated node index");const auto tx=DecodeState(bytes,config);
            for(const auto& n:tx.nodes)if(n.id==id&&n.state==uint32_t(state)){auto verified=Reader(ChunkID(n.task,1));Need(verified->PayloadSHA256()==FSplatSceneFormat::SHA256(bytes),"Node index is not a committed artifact");return n;}
            throw std::runtime_error("Node index identity mismatch");}throw std::runtime_error("Missing committed node index");}
    void StateCommit(FSplatBuildTask task,Transaction tx,std::vector<FSplatBuildChunk> chunks,FSplatBuildCheckpoint next){const auto bytes=EncodeState(tx);chunks.push_back(Bytes(task.id,1,bytes.bytes));Commit(task,std::move(chunks),next);MaterializeState(ChunkID(task.id,1),tx);}
    void Normalize(){
        if(journal.Checkpoint().stage!=ESplatBuildStage::Created&&journal.Checkpoint().stage!=ESplatBuildStage::Normalizing)return;
        const auto dynamic=Add(FSplatPLYStream::ParserWorkingBytes(header,config.batchCapacity),Add(Mul(config.batchCapacity,504),2u<<20));
        Admit(dynamic);Notify(ESplatBuildStage::Normalizing,journal.Checkpoint().committedRecords,header.recordCount);
        Check(FSplatPLYStream::StreamRecords(source,header,config.batchCapacity,control,[&](std::span<const FSplatGaussian> batch,const FSplatBuildSourceCursor& cursor,std::string&){
            Admit(dynamic,Add(Mul(batch.size(),252),1u<<20));const uint64_t task=NewTask();auto writer=Writer(task,0,252);Check(writer->Append(batch,error));auto d=Finish(writer);
            FSplatBuildTask t;t.id=task;t.kind=ESplatBuildTaskKind::Normalize;t.inputRecords=batch.size();auto next=journal.Checkpoint();next.stage=ESplatBuildStage::Normalizing;next.resumeStage=next.stage;next.sourceCursor=cursor;next.committedRecords=cursor.nextOriginalId;Commit(t,{d},next);Notify(next.stage,cursor.nextOriginalId,header.recordCount);return true;
        },error,journal.Checkpoint().sourceCursor));
        auto next=journal.Checkpoint();next.stage=ESplatBuildStage::Partitioning;next.resumeStage=next.stage;Check(journal.CommitCheckpoint(next,control,error));
    }
    template<class Sink>void Input(const Node& node,const Sink& sink){uint64_t count=0,previous=0;bool first=true;
        auto consume=[&](uint64_t id){auto reader=Reader(id);while(reader->RemainingRecords()){std::vector<FSplatGaussian> batch;Check(reader->Read(config.batchCapacity,batch,error));for(const auto& g:batch){Poll(control);Need(g.originalId<identity.sourceRecords&&(first||g.originalId>previous),"Partition input stable IDs are not a strict source subset");first=false;previous=g.originalId;++count;}sink(std::span<const FSplatGaussian>(batch));}};
        if(node.input)consume(node.input);else Check(journal.ReadChunks([&](const FSplatBuildChunk& d,bool retired,std::string&){if(!retired&&Slot(d.id)==0&&d.recordBytes==252)consume(d.id);return true;},control,error));
        Need(count==node.count,"Partition input does not preserve source count");
    }
    void Root(){if(nodeCount||!identity.sourceRecords)return;Admit(2u<<20,1u<<20);FSplatBuildTask t;t.id=NewTask();t.kind=ESplatBuildTaskKind::Partition;t.inputRecords=identity.sourceRecords;Node rootNode;rootNode.count=identity.sourceRecords;rootNode.task=t.id;Transaction tx;tx.nodeCount=1;tx.nodes={rootNode};StateCommit(t,tx,{},journal.Checkpoint());}
    void Retire(const Node& parent){if(!parent.input||!parent.childCount)return;FSplatBuildChunk old;bool found=false,dead=false;Check(journal.FindChunk(parent.input,old,found,dead,control,error));Need(found,"Missing partition parent artifact");if(dead)return;std::array<uint64_t,8>children{};for(uint32_t i=0;i<parent.childCount;++i)children[i]=LoadNode(parent.children[i]).input;Check(journal.RetirePartitionInput(parent.input,std::span<const uint64_t>(children.data(),parent.childCount),control,error));}
    void Partition(Node node){
        Need(node.depth<config.maxDepth,"Maximum depth cannot represent this leaf within the hard leaf capacity");
        const uint64_t dynamic=Add(Mul(config.batchCapacity,Add(Mul(14,sizeof(FSplatGaussian)),1008)),2u<<20);
        Admit(dynamic,Add(Mul(node.count,252),2u<<20));Notify(ESplatBuildStage::Partitioning,0,node.count);
        FSplatBounds centers{};bool bounds=false;Input(node,[&](std::span<const FSplatGaussian>batch){for(const auto& g:batch){FSplatBounds b;for(int a=0;a<3;++a)b.min[a]=b.max[a]=g.position[a];if(!bounds){centers=b;bounds=true;}else FSplatProxyMath::Extend(centers,b);}});
        std::array<double,3> middle;for(int a=0;a<3;++a)middle[a]=double(centers.min[a])+(double(centers.max[a])-centers.min[a])*0.5;
        auto octant=[&](const FSplatGaussian& g){uint32_t index=0;for(int a=0;a<3;++a)if(double(g.position[a])>=middle[a])index|=1u<<a;return index;};
        std::array<uint64_t,8> counts{};Input(node,[&](std::span<const FSplatGaussian>batch){for(const auto& g:batch)++counts[octant(g)];});
        const bool fallback=*std::max_element(counts.begin(),counts.end())==node.count;
        const auto group=(node.count+7)/8;if(fallback){counts={};for(uint32_t i=0;i<8;++i)counts[i]=std::min(group,node.count-std::min(node.count,uint64_t(i)*group));}
        const uint64_t task=NewTask();std::array<std::unique_ptr<FSplatBuildChunkWriter>,8>writers;std::array<std::vector<FSplatGaussian>,8>buffers;std::array<Node,8>children;
        const uint32_t nonempty=uint32_t(std::count_if(counts.begin(),counts.end(),[](uint64_t n){return n!=0;}));Need(nonempty>=2&&uint64_t(nodeCount)+nonempty<=config.maxNodes,"Partition node admission or progress failed");
        Transaction tx;tx.nodeCount=nodeCount+nonempty;node.state=1;node.childCount=0;node.task=task;
        for(uint32_t i=0;i<8;++i)if(counts[i]){auto& child=children[i];child.id=nodeCount+node.childCount;child.parent=node.id;child.depth=node.depth+1;child.count=counts[i];child.input=ChunkID(task,10+i);child.task=task;node.children[node.childCount++]=child.id;writers[i]=Writer(task,10+i,252);buffers[i].reserve(size_t(std::min<uint64_t>(config.batchCapacity,counts[i])));}
        uint64_t rank=0;Input(node,[&](std::span<const FSplatGaussian> batch){for(const auto& g:batch){const auto index=fallback?uint32_t(rank/group):octant(g);++rank;auto& b=buffers[index];b.push_back(g);if(b.size()==b.capacity()){Check(writers[index]->Append(b,error));b.clear();}}});
        std::vector<FSplatBuildChunk> outputs;outputs.reserve(nonempty+1);for(uint32_t i=0;i<8;++i)if(writers[i]){if(!buffers[i].empty())Check(writers[i]->Append(buffers[i],error));outputs.push_back(Finish(writers[i]));Need(outputs.back().recordCount==counts[i],"Partition histogram mismatch");}
        tx.nodes.push_back(node);for(uint32_t i=0;i<8;++i)if(counts[i])tx.nodes.push_back(children[i]);
        FSplatBuildTask t;t.id=task;t.parentId=node.input?node.input>>32:UINT64_MAX;t.kind=ESplatBuildTaskKind::Partition;t.partition=fallback?ESplatBuildPartition::StableOriginalId:ESplatBuildPartition::SpatialOctants;t.depth=node.depth;t.inputRecords=node.count;
        auto next=journal.Checkpoint();next.stage=ESplatBuildStage::Partitioning;next.resumeStage=next.stage;StateCommit(t,tx,std::move(outputs),next);Retire(node);Notify(next.stage,node.count,node.count);
    }
    FSplatProxySummary Summary(std::span<const FSplatWeightedGaussian> records){FSplatProxySummary summary;for(const auto& r:records){Poll(control);const auto support=FSplatProxyMath::Support(r.gaussian,config.supportSigma);FSplatBounds center;for(int a=0;a<3;++a)center.min[a]=center.max[a]=r.gaussian.position[a];if(!summary.hasBounds){summary.hasBounds=true;summary.supportBounds=support;summary.centerBounds=center;}else{FSplatProxyMath::Extend(summary.supportBounds,support);FSplatProxyMath::Extend(summary.centerBounds,center);}summary.coverageAreaUpper+=FSplatProxyMath::Coverage(r.gaussian);summary.geometricError=std::max(summary.geometricError,std::sqrt(r.geometryErrorSquared));summary.appearanceError=std::max(summary.appearanceError,std::sqrt(r.appearanceErrorSquared));}Need(std::isfinite(summary.coverageAreaUpper),"Coverage statistic overflow");return summary;}
    void Representation(Node node,std::vector<FSplatWeightedGaussian> records,FSplatProxySummary summary,bool leaf){
        Need(!records.empty()&&records.size()<=std::max(config.leafCapacity,config.maxProxyCount),"Representation capacity exceeded");
        const uint32_t pages=uint32_t((records.size()+config.pageCapacity-1)/config.pageCapacity);Need(Add(journal.Checkpoint().committedPageCount,pages)<=config.maxPages,"Page count admission exceeded");
        const uint64_t task=NewTask();std::vector<FSplatBuildChunk> outputs;outputs.reserve(size_t(pages)+2);auto weighted=Writer(task,0,276);
        for(size_t at=0;at<records.size();){const auto n=std::min<size_t>(config.batchCapacity,records.size()-at);Check(weighted->AppendWeighted(std::span<const FSplatWeightedGaussian>(records).subspan(at,n),error));at+=n;}outputs.push_back(Finish(weighted));
        for(uint32_t page=0;page<pages;++page){Poll(control);const size_t begin=size_t(page)*config.pageCapacity,n=std::min<size_t>(config.pageCapacity,records.size()-begin);std::vector<FSplatGaussian>gaussians;gaussians.reserve(n);for(size_t i=0;i<n;++i)gaussians.push_back(records[begin+i].gaussian);std::vector<uint8_t> bytes;Check(FSplatSceneFormat::EncodePage(gaussians,header.shDegree,bytes,error));outputs.push_back(Bytes(task,100+page,bytes));}
        node.state=2;node.task=task;node.weighted=ChunkID(task,0);node.representation=uint32_t(records.size());node.pageStart=uint32_t(journal.Checkpoint().committedPageCount);node.pageCount=pages;node.summary=summary;
        Transaction tx;tx.nodeCount=nodeCount;tx.nodes={node};FSplatBuildTask t;t.id=task;t.parentId=node.input?node.input>>32:UINT64_MAX;t.kind=leaf?ESplatBuildTaskKind::Leaf:ESplatBuildTaskKind::Reduce;t.depth=node.depth;t.inputRecords=node.count;
        auto next=journal.Checkpoint();next.stage=leaf?ESplatBuildStage::BuildingLeaves:ESplatBuildStage::Reducing;next.resumeStage=next.stage;next.committedPageCount+=pages;StateCommit(t,tx,std::move(outputs),next);Notify(next.stage,node.count,node.count);
    }
    void Process(uint32_t id){Poll(control);Node node=LoadNode(id);if(node.state==2)return;
        if(node.count<=config.leafCapacity){Admit(Add(Mul(config.leafCapacity,4096),2u<<20),Add(Mul(node.count,1024),1u<<20));Notify(ESplatBuildStage::BuildingLeaves,0,node.count);std::vector<FSplatWeightedGaussian>records;records.reserve(size_t(node.count));Input(node,[&](std::span<const FSplatGaussian>batch){for(const auto& g:batch)records.push_back(FSplatProxyMath::FromGaussian(g));});auto summary=Summary(records);Representation(node,std::move(records),summary,true);return;}
        if(node.state==0){Partition(node);node=LoadNode(id);}else Retire(node);
        for(uint32_t i=0;i<node.childCount;++i)Process(node.children[i]);
        FSplatProxySummary descendants;uint64_t inputs=0;std::array<Node,8>children;
        for(uint32_t i=0;i<node.childCount;++i){children[i]=LoadNode(node.children[i]);Need(children[i].state==2,"Child representation is incomplete");inputs=Add(inputs,children[i].representation);FSplatProxyMath::ExtendSummary(descendants,children[i].summary);}
        const uint32_t cap=id==0?config.rootProxyCount:config.maxProxyCount;
        const uint32_t target=uint32_t(std::min<uint64_t>(cap,std::max<uint64_t>(1,uint64_t(std::ceil(double(inputs)*config.proxyReduction)))));
        const uint64_t dynamic=Add(Add(Mul(inputs,Add(Mul(4,sizeof(FSplatWeightedGaussian)),64)),Mul(target,sizeof(FSplatWeightedGaussian))),3u<<20);
        Admit(dynamic,Add(Mul(target,1024),1u<<20));Notify(ESplatBuildStage::Reducing,0,node.count);
        std::vector<FSplatWeightedGaussian> records;FSplatProxySummary summary;uint64_t scratch=0;
        {std::vector<FSplatWeightedGaussian>input;input.reserve(size_t(inputs));for(uint32_t i=0;i<node.childCount;++i){auto r=Reader(children[i].weighted);while(r->RemainingRecords()){std::vector<FSplatWeightedGaussian>b;Check(r->ReadWeighted(config.batchCapacity,b,error));input.insert(input.end(),b.begin(),b.end());}}
            Need(input.size()==inputs,"Weighted sidecar count mismatch");
            const auto proxyBase=Mul(id,Add(Mul(8,std::max(config.leafCapacity,config.maxProxyCount)),1));
            Check(FSplatProxyMath::Reduce(input,header.shDegree,target,config,control,records,summary,scratch,error,proxyBase));}
        FSplatProxyMath::PreserveDescendants(summary,descendants);Representation(node,std::move(records),summary,false);
    }
    FSplatSceneValidationLimits ValidationLimits(){FSplatSceneValidationLimits limits;limits.maxManifestBytes=config.maxManifestBytes;limits.maxWorkingBytes=config.workingMemoryBytes;limits.maxNodes=config.maxNodes;limits.maxPages=config.maxPages;limits.maxTotalPageBytes=config.maxDiskBytes;limits.maxPageBytes=Add(24,Mul(config.pageCapacity,252));limits.maxDecodedPageBytes=Mul(config.pageCapacity,sizeof(FSplatGaussian));return limits;}
    bool ValidateScene(const std::string& path,std::string& message){FSplatSceneValidationResult result;return FSplatSceneValidation::Validate(path,ValidationLimits(),result,message,&control);}
    void Manifest(){
        const uint64_t pages=journal.Checkpoint().committedPageCount;
        const uint64_t wireEstimate=Add(4096,Add(Mul(nodeCount,144),Mul(pages,196)));
        const uint64_t metadata=Add(journal.MetadataBytes(),Add(2u<<20,Add(Mul(Add(nodeCount,pages),512),Mul(wireEstimate,4))));
        Need(wireEstimate<=config.maxManifestBytes&&metadata<=config.metadataBytes,"Final manifest metadata does not fit admitted sub-budget");
        Admit(Add(Mul(config.pageCapacity,2048),2u<<20),Add(Add(Mul(config.pageCapacity,252),4096),wireEstimate));Notify(ESplatBuildStage::Validating);
        uint64_t manifestChunk=0;
        Check(journal.ReadTasks([&](const FSplatBuildTask&t,std::string&){if(t.kind==ESplatBuildTaskKind::Validate)manifestChunk=ChunkID(t.id,2);return true;},control,error));
        if(!manifestChunk){
            FSplatSceneManifest m;m.version=3;m.shDegree=header.shDegree;m.sourceSHA256=identity.sourceSHA256;m.builderVersion=identity.builderVersion;m.builderHash=identity.builderHash;m.configHash=identity.configHash;m.buildConfig=identity.canonicalConfig;m.supportSigma=config.supportSigma;m.sourceCount=identity.sourceRecords;m.rootNode=nodeCount?0:SplatInvalidId;
            m.nodes.resize(nodeCount);m.pages.resize(size_t(pages));
            for(uint32_t id=0;id<nodeCount;++id){Poll(control);const auto node=LoadNode(id);Need(node.state==2,"Manifest references incomplete node");auto& n=m.nodes[id];n.bounds=node.summary.supportBounds;n.centerBounds=node.summary.centerBounds;n.level=node.depth;n.gaussianCount=node.representation;n.coverageAreaUpper=node.summary.coverageAreaUpper;
                const double geometric=node.summary.geometricError*config.geometricErrorScale;Need(std::isfinite(geometric)&&geometric<=std::numeric_limits<float>::max()&&node.summary.appearanceError<=std::numeric_limits<float>::max(),"Manifest error exceeds finite FP32");
                n.geometricError=std::nextafter(float(geometric),INFINITY);if(geometric==0)n.geometricError=0;n.appearanceError=std::nextafter(float(node.summary.appearanceError),INFINITY);if(node.summary.appearanceError==0)n.appearanceError=0;
                n.children.assign(node.children,node.children+node.childCount);n.pageIds.reserve(node.pageCount);
                if(node.childCount)m.proxyCount=Add(m.proxyCount,node.representation);else m.leafCount=Add(m.leafCount,node.representation);
                for(uint32_t page=0;page<node.pageCount;++page){auto r=Reader(ChunkID(node.task,100+page));auto& p=m.pages[node.pageStart+page];p.id=node.pageStart+page;p.relativePath=PageFile(p.id);p.byteSize=r->RemainingRecords();p.gaussianCount=std::min(config.pageCapacity,node.representation-page*config.pageCapacity);p.decodedByteSize=Mul(p.gaussianCount,sizeof(FSplatGaussian));p.sha256=r->PayloadSHA256();p.pinned=id==0;n.pageIds.push_back(p.id);m.totalDataBytes=Add(m.totalDataBytes,p.byteSize);
                    Check(journal.MaterializeChunk(ChunkID(node.task,100+page),"scene-staging/"+p.relativePath,control,error));}
            }
            auto encoded=EncodeManifest(m,config,control);FSplatBuildTask task;task.id=NewTask();task.kind=ESplatBuildTaskKind::Validate;task.inputRecords=identity.sourceRecords;const auto descriptor=Bytes(task.id,2,encoded.bytes);auto next=journal.Checkpoint();next.stage=ESplatBuildStage::Validating;next.resumeStage=next.stage;Commit(task,{descriptor},next);manifestChunk=descriptor.id;
        }
        Check(journal.MaterializeChunk(manifestChunk,"scene-staging/scene.mgs",control,error));
        Check(ValidateScene(UTF8(fs::u8path(root)/"scene-staging"),error));
    }
    void Run(){
        Poll(control);FSplatPLYLimits limits;limits.maxRecords=config.maxSourceRecords;limits.maxFileBytes=config.maxSourceBytes;limits.maxHeaderBytes=config.maxHeaderBytes;limits.maxRecordBytes=config.maxRecordBytes;
        FSplatPLYHeader admissionHeader;admissionHeader.limits=limits;
        Need(Add(config.metadataBytes,Add(FSplatPLYStream::ParserWorkingBytes(admissionHeader,config.batchCapacity),Add(Mul(config.batchCapacity,504),2u<<20)))<=config.workingMemoryBytes,"Parser workspace cannot fit admitted policy");
        Check(FSplatPLYStream::Probe(source,header,error,limits,&control));
        progress.stage=ESplatBuildStage::Copying;progress.status="hashing_stable_private_source";progress.totalBytes=header.fileBytes;progress.metadataBytes=config.metadataBytes;progress.workingBytes=Add(config.metadataBytes,1u<<20);progress.peakWorkingBytes=progress.workingBytes;
        uint64_t lastHashReport=0;identity.sourceSHA256=HashSource(source,control,[&](uint64_t done){progress.processedBytes=done;if(callback&&(done-lastHashReport>=(4u<<20)||done==header.fileBytes)){lastHashReport=done;callback(progress);}});identity.sourceBytes=header.fileBytes;identity.sourceRecords=header.recordCount;identity.builderHash=config.builderHash;identity.canonicalConfig=FSplatStreamingLODBuilder::CanonicalConfig(config);identity.configHash=FSplatSceneFormat::SHA256(std::vector<uint8_t>(identity.canonicalConfig.begin(),identity.canonicalConfig.end()));
        Check(journal.Open(root,source,identity,config,control,error));
        Admit(2u<<20);if(!journal.Checkpoint().published&&journal.Checkpoint().stage!=ESplatBuildStage::Publishing){Normalize();Replay();Root();if(nodeCount)Process(0);Manifest();}
        Admit(2u<<20);Notify(ESplatBuildStage::Publishing);Check(journal.PublishScene("scene-staging",output,[&](const std::string& path,std::string& e){return ValidateScene(path,e);},control,error));progress.diskBytes=DiskBytes();Notify(ESplatBuildStage::Completed,identity.sourceRecords,identity.sourceRecords);
    }
};
}

std::string FSplatStreamingLODBuilder::CanonicalConfig(const FSplatBoundedBuildConfig& c){
    std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(9)<<'{';bool first=true;auto field=[&](const char* name,auto value){if(!first)s<<',';first=false;s<<'"'<<name<<"\":"<<value;};
#define C(name) field(#name,c.name)
    C(workingMemoryBytes);C(metadataBytes);C(diskReserveBytes);C(maxDiskBytes);C(maxSourceBytes);C(maxSourceRecords);C(maxManifestBytes);C(leafCapacity);C(pageCapacity);C(maxProxyCount);C(rootProxyCount);C(batchCapacity);C(maxDepth);C(maxNodes);C(maxPages);C(workerCount);C(pollIntervalRecords);C(maxHeaderBytes);C(maxRecordBytes);C(supportSigma);C(proxyReduction);C(geometricErrorScale);C(appearanceWeight);C(maxAppearanceError);C(schemaVersion);field("retainCancelledWork",c.retainCancelledWork?1:0);
#undef C
    s<<",\"builderVersion\":\""<<SplatBoundedBuilderVersion<<"\",\"parserVersion\":\""<<SplatBuildParserVersion<<"\",\"partitionPolicy\":\"stable-octants-original-id-rank-fallback-1\",\"reductionPolicy\":\"weighted-cumulative-rms-morton-1\"}";return s.str();
}
bool FSplatStreamingLODBuilder::ValidateConfig(const FSplatBoundedBuildConfig& c,std::string& error,bool requireBuildHash){try{
    Need(c.builderVersion==SplatBoundedBuilderVersion&&c.parserVersion==SplatBuildParserVersion&&c.schemaVersion==1,"Unsupported mobile builder/parser/schema identity");Need(!requireBuildHash||HashValid(c.builderHash),"Actual builder binary SHA256 is required");Need(c.builderHash.empty()||HashValid(c.builderHash),"Malformed builder SHA256");
    Need(c.workingMemoryBytes>=8ull<<20&&c.metadataBytes>=2ull<<20&&c.metadataBytes<c.workingMemoryBytes,"Invalid workspace/metadata cap");
    Need(c.leafCapacity&&c.pageCapacity&&c.maxProxyCount&&c.rootProxyCount&&c.rootProxyCount<=c.maxProxyCount&&c.batchCapacity,"Invalid record capacities");
    Need(c.maxDepth>=1&&c.maxDepth<=64&&c.maxNodes&&c.maxNodes<=2000000&&c.maxPages&&c.maxPages<=2000000&&c.maxSourceRecords&&(c.workerCount==1||c.workerCount==2),"Invalid finite build limits");
    Need(c.maxSourceBytes&&c.maxDiskBytes&&c.diskReserveBytes<c.maxDiskBytes&&c.maxManifestBytes>=4096&&c.maxManifestBytes<=64ull<<20,"Invalid source/disk/manifest cap");
    Need(c.maxHeaderBytes>=64&&c.maxHeaderBytes<=16u<<20&&c.maxRecordBytes>=64&&c.maxRecordBytes<=16u<<20&&c.pollIntervalRecords&&c.pollIntervalRecords<=256,"Invalid parser/control policy");
    Need(std::isfinite(c.supportSigma)&&c.supportSigma>=3&&std::isfinite(c.proxyReduction)&&c.proxyReduction>0&&c.proxyReduction<=1&&std::isfinite(c.geometricErrorScale)&&c.geometricErrorScale>=1&&std::isfinite(c.appearanceWeight)&&c.appearanceWeight>=0&&std::isfinite(c.maxAppearanceError)&&c.maxAppearanceError>=0,"Invalid Gaussian proxy policy");
    const auto max=std::max(c.leafCapacity,c.maxProxyCount);Need(max<=1000000&&c.pageCapacity<=1000000&&c.batchCapacity<=65536,"Record batch cap exceeds protocol");
    Need(Add(c.metadataBytes,Add(Mul(c.batchCapacity,Add(Mul(14,sizeof(FSplatGaussian)),1008)),2u<<20))<=c.workingMemoryBytes,"Partition workspace cannot fit configured batches");
    Need(Add(c.metadataBytes,Add(Mul(Mul(8,max),Add(Mul(4,sizeof(FSplatWeightedGaussian)),64)),Add(Mul(c.maxProxyCount,sizeof(FSplatWeightedGaussian)),3u<<20)))<=c.workingMemoryBytes,"Worst-case proxy workspace cannot fit configured child capacities");
    Need(Add(c.metadataBytes,Add(Mul(c.leafCapacity,4096),2u<<20))<=c.workingMemoryBytes,"Leaf workspace cannot fit configured capacity");
    error.clear();return true;
}catch(const std::exception& e){error=e.what();return false;}}
bool FSplatStreamingLODBuilder::ParseConfig(const std::string& text,FSplatBoundedBuildConfig& config,std::string& error){try{
    Need(text.size()<=65536,"Build config text limit");auto result=config;std::istringstream input(text);input.imbue(std::locale::classic());std::map<std::string,bool> seen;
    auto token=[&](char c){input>>std::ws;Need(input.get()==c,"Invalid flat numeric build configuration");};token('{');input>>std::ws;
    if(input.peek()!='}')while(true){token('"');std::string key;std::getline(input,key,'"');Need(!key.empty()&&key.size()<=64&&key.find_first_of("\\\n\r")==std::string::npos&&seen.emplace(key,true).second,"Invalid or duplicate configuration field");token(':');input>>std::ws;std::string numeric;while(input&&input.peek()!=','&&input.peek()!='}'&&input.peek()!=' '&&input.peek()!='\t'&&input.peek()!='\r'&&input.peek()!='\n')numeric+=char(input.get());
        Need(!numeric.empty()&&numeric.size()<=64&&numeric.front()!='+'&&numeric.find_first_not_of("0123456789.eE+-")==std::string::npos,"Invalid configuration number token");
        size_t position=numeric.front()=='-'?1:0;Need(position<numeric.size(),"Invalid configuration number");if(numeric[position]=='0'){++position;Need(position==numeric.size()||numeric[position]<'0'||numeric[position]>'9',"JSON numeric leading zero");}else{Need(numeric[position]>='1'&&numeric[position]<='9',"Invalid JSON integer part");while(position<numeric.size()&&numeric[position]>='0'&&numeric[position]<='9')++position;}
        if(position<numeric.size()&&numeric[position]=='.'){const auto begin=++position;while(position<numeric.size()&&numeric[position]>='0'&&numeric[position]<='9')++position;Need(position>begin,"Invalid JSON fraction");}
        if(position<numeric.size()&&(numeric[position]=='e'||numeric[position]=='E')){++position;if(position<numeric.size()&&(numeric[position]=='+'||numeric[position]=='-'))++position;const auto begin=position;while(position<numeric.size()&&numeric[position]>='0'&&numeric[position]<='9')++position;Need(position>begin,"Invalid JSON exponent");}Need(position==numeric.size(),"Invalid JSON number tail");
        std::istringstream number(numeric);number.imbue(std::locale::classic());long double value;number>>value;Need(bool(number)&&number.peek()==EOF&&std::isfinite(value),"Invalid finite configuration number");
        auto u64=[&](){uint64_t n=0;Need(numeric.find_first_not_of("0123456789")==std::string::npos&&(numeric.size()==1||numeric.front()!='0'),"Integer policy requires a canonical unsigned decimal");const auto parsed=std::from_chars(numeric.data(),numeric.data()+numeric.size(),n);Need(parsed.ec==std::errc{}&&parsed.ptr==numeric.data()+numeric.size(),"Configuration integer out of range");return n;};auto u32=[&](){const auto n=u64();Need(n<=UINT32_MAX,"Configuration u32 out of range");return uint32_t(n);};bool known=false;
#define U64(name) if(key==#name){result.name=u64();known=true;}
#define U32(name) if(key==#name){result.name=u32();known=true;}
#define F32(name) if(key==#name){result.name=float(value);known=true;}
        U64(workingMemoryBytes)U64(metadataBytes)U64(diskReserveBytes)U64(maxDiskBytes)U64(maxSourceBytes)U64(maxSourceRecords)U64(maxManifestBytes)
        U32(leafCapacity)U32(pageCapacity)U32(maxProxyCount)U32(rootProxyCount)U32(batchCapacity)U32(maxDepth)U32(maxNodes)U32(maxPages)U32(workerCount)U32(pollIntervalRecords)U32(maxHeaderBytes)U32(maxRecordBytes)U32(schemaVersion)
        F32(supportSigma)F32(proxyReduction)F32(geometricErrorScale)F32(appearanceWeight)F32(maxAppearanceError)
#undef U64
#undef U32
#undef F32
        if(key=="retainCancelledWork"){const auto n=u32();Need(n<=1,"retainCancelledWork must be 0 or 1");result.retainCancelledWork=n!=0;known=true;}Need(known,"Unknown mobile builder configuration field");input>>std::ws;if(input.peek()=='}')break;token(',');}
    token('}');input>>std::ws;Need(input.peek()==EOF,"Trailing build configuration data");Need(ValidateConfig(result,error,false),error);config=std::move(result);error.clear();return true;
}catch(const std::exception& e){error=e.what();return false;}}
bool FSplatStreamingLODBuilder::Run(const std::string& source,const std::string& root,const std::string& output,const FSplatBoundedBuildConfig& config,FSplatBuildControl& control,FSplatBuildProgressCallback callback,std::string& error){
    if(!ValidateConfig(config,error))return false;Worker worker(config,control,std::move(callback));worker.source=source;worker.root=root;worker.output=output;
    try{Need(!source.empty()&&!root.empty()&&!output.empty(),"Builder paths are required");const auto sourcePath=fs::absolute(fs::u8path(source)).lexically_normal();const auto rootPath=fs::absolute(fs::u8path(root)).lexically_normal();const auto outputPath=fs::absolute(fs::u8path(output)).lexically_normal();
        Need(outputPath!=rootPath&&sourcePath!=outputPath,"Builder source/job/output must be distinct");const auto outRelative=outputPath.lexically_relative(rootPath);Need(outRelative.empty()||*outRelative.begin()=="..","Output cannot be inside the job transaction directory");
        const auto sourceRelative=sourcePath.lexically_relative(rootPath);if(!sourceRelative.empty()&&*sourceRelative.begin()!=".."){const auto first=UTF8(*sourceRelative.begin());Need(first!="node-index"&&first!="scene-staging","Stable source overlaps builder-owned files");}
        worker.Run();error.clear();return true;
    }catch(const std::exception& e){error=e.what();worker.progress.error=error;worker.progress.status=error;const bool resources=error.find("admission")!=std::string::npos||error.find("disk")!=std::string::npos||error.find("workspace")!=std::string::npos;worker.progress.stage=control.Poll()==ESplatBuildSignal::Cancel?ESplatBuildStage::Cancelled:control.Poll()==ESplatBuildSignal::Pause?ESplatBuildStage::Paused:resources?ESplatBuildStage::WaitingForResources:ESplatBuildStage::Failed;if(worker.callback){try{worker.callback(worker.progress);}catch(...){}}return false;}
}
}
