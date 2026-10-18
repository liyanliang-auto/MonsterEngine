// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatBuildJournal.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include "Core/Platform.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#if PLATFORM_WINDOWS
#include <Windows.h>
#include <io.h>
#else
#include <cerrno>
#include <sys/file.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace MonsterRender::Splat {
namespace {
namespace fs = std::filesystem;
constexpr uint32_t ChunkHeaderBytes = 96;
constexpr uint32_t MaxFrameBytes = 8192;
constexpr uint32_t MaximumRootBytes = 256u << 10;
struct HashState {
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint8_t block[64]{}; size_t used=0; uint64_t total=0;
    static uint32_t R(uint32_t x,int n){return (x>>n)|(x<<(32-n));}
    void Transform() {
        static constexpr uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t(block[4*i])<<24)|(uint32_t(block[4*i+1])<<16)|(uint32_t(block[4*i+2])<<8)|block[4*i+3];
        for(int i=16;i<64;++i)w[i]=w[i-16]+(R(w[i-15],7)^R(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(R(w[i-2],17)^R(w[i-2],19)^(w[i-2]>>10));
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
        for(int i=0;i<64;++i){uint32_t t=z+(R(e,6)^R(e,11)^R(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t u=(R(a,2)^R(a,13)^R(a,22))+((a&b)^(a&c)^(b&c));z=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
    }
    void Add(const uint8_t* p,size_t n){total+=n;while(n){size_t s=std::min(n,64-used);std::memcpy(block+used,p,s);used+=s;p+=s;n-=s;if(used==64){Transform();used=0;}}}
    std::string Finish(){uint64_t bits=total*8;uint8_t one=0x80;Add(&one,1);uint8_t zero=0;while(used!=56)Add(&zero,1);uint8_t tail[8];for(int i=0;i<8;++i)tail[i]=uint8_t(bits>>(56-8*i));Add(tail,8);std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto x:h)s<<std::setw(8)<<x;return s.str();}
};
void Poll(FSplatBuildControl& control) {
    const auto signal = control.Poll();
    if (signal == ESplatBuildSignal::Cancel) throw std::runtime_error("cancelled");
    if (signal == ESplatBuildSignal::Pause) throw std::runtime_error("paused");
}
uint64_t Add(uint64_t a,uint64_t b) {
    if(a>UINT64_MAX-b)throw std::runtime_error("Journal size overflow"); return a+b;
}
uint64_t Multiply(uint64_t a,uint64_t b) {
    if(b&&a>UINT64_MAX/b)throw std::runtime_error("Journal size overflow"); return a*b;
}
bool HashValid(const std::string& hash) { return hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==std::string::npos; }
bool RelativeValid(const std::string& name) {
    if(name.empty()||name.size()>4096||name.front()=='/'||name.back()=='/'||name.find_first_of("\\:\0",0,3)!=std::string::npos)return false;
    size_t start=0;
    for(size_t i=0;i<=name.size();++i)if(i==name.size()||name[i]=='/') {
        const auto part=name.substr(start,i-start);
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')return false;
        for(unsigned char c:part)if(c<32||c==127||c=='?'||c=='*'||c=='"'||c=='<'||c=='>'||c=='|')return false;
        start=i+1;
    }
    return true;
}
bool Within(const fs::path& root,const fs::path& child) {
    auto a=root.begin(),b=child.begin();
    for(;a!=root.end();++a,++b)if(b==child.end()||*a!=*b)return false;
    return true;
}
fs::path SafePath(const fs::path& root,const std::string& relative,bool createParents=false) {
    if(!RelativeValid(relative))throw std::runtime_error("Invalid journal relative path");
    fs::path path=root;
    const fs::path suffix=fs::u8path(relative);
    for(auto it=suffix.begin();it!=suffix.end();++it) {
        path/=*it;
        const bool last=std::next(it)==suffix.end();
        std::error_code ec;const auto status=fs::symlink_status(path,ec);
        if(!ec&&fs::exists(status)) {
            if(fs::is_symlink(status)||(!last&&!fs::is_directory(status)))throw std::runtime_error("Unsafe journal path component");
            if(!Within(root,fs::canonical(path)))throw std::runtime_error("Journal path escapes root");
        } else if(createParents&&!last) fs::create_directory(path);
    }
    return path;
}
std::string UTF8(const fs::path& path) { const auto s=path.u8string();return std::string(s.begin(),s.end()); }
struct FNativeFile {
    int fd=-1;
    FNativeFile()=default;
    FNativeFile(const FNativeFile&)=delete;
    FNativeFile& operator=(const FNativeFile&)=delete;
    ~FNativeFile(){Close();}
    void Close(){if(fd>=0){
#if PLATFORM_WINDOWS
        ::_close(fd);
#else
        ::close(fd);
#endif
        fd=-1;}}
    void Open(const fs::path& path,bool write,bool create=false,bool exclusive=false) {
        Close();
#if PLATFORM_WINDOWS
        int flags=_O_BINARY|_O_NOINHERIT|(write?_O_RDWR:_O_RDONLY)|(create?_O_CREAT:0)|(exclusive?_O_EXCL:0);
        fd=::_wopen(path.c_str(),flags,_S_IREAD|_S_IWRITE);
#else
        int flags=O_CLOEXEC|O_NOFOLLOW|(write?O_RDWR:O_RDONLY)|(create?O_CREAT:0)|(exclusive?O_EXCL:0);
        fd=::open(path.c_str(),flags,0600);
#endif
        if(fd<0)throw std::runtime_error("Cannot open journal file");
    }
    void Seek(uint64_t offset) {
        if(offset>uint64_t(INT64_MAX))throw std::runtime_error("Journal offset overflow");
#if PLATFORM_WINDOWS
        if(::_lseeki64(fd,int64_t(offset),SEEK_SET)<0)throw std::runtime_error("Journal seek failed");
#else
        if(::lseek(fd,off_t(offset),SEEK_SET)<0)throw std::runtime_error("Journal seek failed");
#endif
    }
    size_t Read(uint8_t* bytes,size_t count) {
        size_t done=0;
        while(done<count) {
#if PLATFORM_WINDOWS
            const auto got=::_read(fd,bytes+done,unsigned(std::min<size_t>(count-done,1u<<20)));
#else
            const auto got=::read(fd,bytes+done,std::min<size_t>(count-done,1u<<20));
#endif
            if(got<0){if(errno==EINTR)continue;throw std::runtime_error("Journal read failed");}
            if(!got)break;done+=size_t(got);
        }
        return done;
    }
    void Write(std::span<const uint8_t> bytes) {
        size_t done=0;
        while(done<bytes.size()) {
#if PLATFORM_WINDOWS
            const auto n=::_write(fd,bytes.data()+done,unsigned(std::min<size_t>(bytes.size()-done,1u<<20)));
#else
            const auto n=::write(fd,bytes.data()+done,std::min<size_t>(bytes.size()-done,1u<<20));
#endif
            if(n<0&&errno==EINTR)continue;
            if(n<=0)throw std::runtime_error("Journal write failed");done+=size_t(n);
        }
    }
    void Sync(){
#if PLATFORM_WINDOWS
        if(::_commit(fd)!=0)throw std::runtime_error("Journal file flush failed");
#else
        if(::fsync(fd)!=0)throw std::runtime_error("Journal file flush failed");
#endif
    }
    void Truncate(uint64_t bytes) {
        if(bytes>uint64_t(INT64_MAX))throw std::runtime_error("Journal truncate overflow");
#if PLATFORM_WINDOWS
        if(::_chsize_s(fd,bytes)!=0)throw std::runtime_error("Journal truncate failed");
#else
        if(::ftruncate(fd,off_t(bytes))!=0)throw std::runtime_error("Journal truncate failed");
#endif
    }
};
void SyncDirectory(const fs::path& directory) {
#if PLATFORM_WINDOWS
    // Windows durable rename uses MoveFileExW(MOVEFILE_WRITE_THROUGH); directory
    // handles cannot portably be flushed with FlushFileBuffers on Windows.
    (void)directory;
#else
    const int fd=::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)throw std::runtime_error("Cannot open journal directory for flush");
    const int result=::fsync(fd);::close(fd);
    if(result!=0)throw std::runtime_error("Journal directory flush failed");
#endif
}
void AtomicRename(const fs::path& source,const fs::path& target,bool replace) {
#if PLATFORM_WINDOWS
    if(!MoveFileExW(source.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0)))
        throw std::runtime_error("Journal atomic rename failed");
#else
    if(!replace) {
#ifdef SYS_renameat2
        if(::syscall(SYS_renameat2,AT_FDCWD,source.c_str(),AT_FDCWD,target.c_str(),1)!=0)
            throw std::runtime_error("Journal atomic no-replace rename failed");
#else
        throw std::runtime_error("Atomic no-replace publication is unsupported on this platform");
#endif
    } else if(::rename(source.c_str(),target.c_str())!=0)throw std::runtime_error("Journal atomic rename failed");
    SyncDirectory(target.parent_path());
    if(source.parent_path()!=target.parent_path())SyncDirectory(source.parent_path());
#endif
}
struct FEncoder {
    std::vector<uint8_t> bytes;
    void U32(uint32_t value){for(int i=0;i<4;++i)bytes.push_back(uint8_t(value>>(8*i)));}
    void U64(uint64_t value){for(int i=0;i<8;++i)bytes.push_back(uint8_t(value>>(8*i)));}
    void F32(float value){uint32_t bits;std::memcpy(&bits,&value,4);U32(bits);}
    void F64(double value){uint64_t bits;std::memcpy(&bits,&value,8);U64(bits);}
    void Text(const std::string& value,uint32_t cap=4096){if(value.size()>cap)throw std::runtime_error("Journal string limit exceeded");U32(uint32_t(value.size()));bytes.insert(bytes.end(),value.begin(),value.end());}
};
struct FDecoder {
    std::span<const uint8_t> bytes;size_t position=0;
    void Need(size_t count){if(position>bytes.size()||count>bytes.size()-position)throw std::runtime_error("Truncated journal record");}
    uint32_t U32(){Need(4);uint32_t value=0;for(int i=0;i<4;++i)value|=uint32_t(bytes[position++])<<(8*i);return value;}
    uint64_t U64(){Need(8);uint64_t value=0;for(int i=0;i<8;++i)value|=uint64_t(bytes[position++])<<(8*i);return value;}
    float F32(){const auto bits=U32();float value;std::memcpy(&value,&bits,4);return value;}
    double F64(){const auto bits=U64();double value;std::memcpy(&value,&bits,8);return value;}
    std::string Text(uint32_t cap=4096){const auto n=U32();if(n>cap)throw std::runtime_error("Journal string limit exceeded");Need(n);std::string value(reinterpret_cast<const char*>(bytes.data()+position),n);position+=n;return value;}
    void End(){if(position!=bytes.size())throw std::runtime_error("Trailing journal record bytes");}
};
void Gaussian(FEncoder& out,const FSplatGaussian& g) {
    for(float x:g.position)out.F32(x);for(float x:g.scale)out.F32(x);for(float x:g.rotation)out.F32(x);
    out.F32(g.opacity);for(float x:g.sh)out.F32(x);out.U64(g.originalId);
}
FSplatGaussian Gaussian(FDecoder& in) {
    FSplatGaussian g;for(float& x:g.position)x=in.F32();for(float& x:g.scale)x=in.F32();for(float& x:g.rotation)x=in.F32();
    g.opacity=in.F32();for(float& x:g.sh)x=in.F32();g.originalId=in.U64();
    if(g.position[3]!=1||g.scale[3]!=0)throw std::runtime_error("Invalid normalized Gaussian padding values");
    (void)FSplatProxyMath::FromGaussian(g);return g;
}
void Identity(FEncoder& out,const FSplatBuildIdentity& id) {
    out.Text(id.sourceSHA256);out.U64(id.sourceBytes);out.U64(id.sourceRecords);out.Text(id.parserVersion);
    out.Text(id.builderVersion);out.Text(id.builderHash);out.Text(id.configHash);out.Text(id.canonicalConfig,65536);out.U32(id.schemaVersion);
}
FSplatBuildIdentity Identity(FDecoder& in) {
    FSplatBuildIdentity id;id.sourceSHA256=in.Text();id.sourceBytes=in.U64();id.sourceRecords=in.U64();id.parserVersion=in.Text();
    id.builderVersion=in.Text();id.builderHash=in.Text();id.configHash=in.Text();id.canonicalConfig=in.Text(65536);id.schemaVersion=in.U32();return id;
}
bool SameIdentity(const FSplatBuildIdentity& a,const FSplatBuildIdentity& b) {
    FEncoder aa,bb;Identity(aa,a);Identity(bb,b);return aa.bytes==bb.bytes;
}
std::vector<uint8_t> Envelope(std::vector<uint8_t> bytes) {
    const auto hash=FSplatSceneFormat::SHA256(bytes);bytes.insert(bytes.end(),hash.begin(),hash.end());return bytes;
}
std::vector<uint8_t> Unwrap(std::vector<uint8_t> bytes) {
    if(bytes.size()<64)throw std::runtime_error("Truncated journal envelope");
    const std::string hash(bytes.end()-64,bytes.end());bytes.resize(bytes.size()-64);
    if(!HashValid(hash)||FSplatSceneFormat::SHA256(bytes)!=hash)throw std::runtime_error("Journal envelope checksum mismatch");return bytes;
}
std::vector<uint8_t> EncodeCheckpoint(const FSplatBuildCheckpoint& cp) {
    FEncoder out;out.U32(0x4a53474d);out.U32(1);Identity(out,cp.identity);out.U64(cp.generation);out.U32(uint32_t(cp.stage));out.U32(uint32_t(cp.resumeStage));
    out.U64(cp.sourceCursor.byteOffset);out.U64(cp.sourceCursor.nextOriginalId);out.U64(cp.committedTaskCount);out.U64(cp.committedChunkCount);out.U64(cp.committedPageCount);out.U64(cp.committedRecords);
    out.Text(cp.taskLogHash);out.U64(cp.taskLogBytes);out.Text(cp.chunkLogHash);out.U64(cp.chunkLogBytes);out.U32(cp.published?1:0);return Envelope(std::move(out.bytes));
}
FSplatBuildCheckpoint DecodeCheckpoint(std::vector<uint8_t> bytes) {
    bytes=Unwrap(std::move(bytes));FDecoder in{bytes};
    if(in.U32()!=0x4a53474d||in.U32()!=1)throw std::runtime_error("Unsupported journal checkpoint");
    FSplatBuildCheckpoint cp;cp.identity=Identity(in);cp.generation=in.U64();cp.stage=ESplatBuildStage(in.U32());cp.resumeStage=ESplatBuildStage(in.U32());
    cp.sourceCursor.byteOffset=in.U64();cp.sourceCursor.nextOriginalId=in.U64();cp.committedTaskCount=in.U64();cp.committedChunkCount=in.U64();cp.committedPageCount=in.U64();cp.committedRecords=in.U64();
    cp.taskLogHash=in.Text();cp.taskLogBytes=in.U64();cp.chunkLogHash=in.Text();cp.chunkLogBytes=in.U64();const auto published=in.U32();cp.published=published!=0;in.End();
    if(!cp.generation||uint32_t(cp.stage)>12||uint32_t(cp.resumeStage)>12||published>1||!HashValid(cp.taskLogHash)||!HashValid(cp.chunkLogHash))throw std::runtime_error("Invalid journal checkpoint fields");
    return cp;
}
std::vector<uint8_t> ReadSmall(const fs::path& path,uint64_t cap) {
    const auto size=fs::file_size(path);if(size>cap)throw std::runtime_error("Journal metadata limit exceeded");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));FNativeFile file;file.Open(path,false);
    if(file.Read(bytes.data(),bytes.size())!=bytes.size())throw std::runtime_error("Truncated journal metadata");return bytes;
}
HashState HashPrefix(const fs::path& path,uint64_t count,FSplatBuildControl& control,uint64_t offset=0) {
    FNativeFile file;file.Open(path,false);file.Seek(offset);HashState hash;std::array<uint8_t,65536> buffer;
    while(count) {Poll(control);const auto n=size_t(std::min<uint64_t>(count,buffer.size()));if(file.Read(buffer.data(),n)!=n)throw std::runtime_error("Truncated committed journal data");hash.Add(buffer.data(),n);count-=n;}
    return hash;
}
std::string Digest(HashState hash){return hash.Finish();}

struct FJournalState {
    fs::path root,source;
    FSplatBoundedBuildConfig config;
    FSplatBuildCheckpoint checkpoint;
    HashState taskHash,chunkHash;
    uint64_t diskEstimate=0,externalSourceBytes=0,externalOutputBytes=0,metadataBytes=0;
    bool ready=false,poisoned=false;
#if PLATFORM_WINDOWS
    HANDLE lock=INVALID_HANDLE_VALUE;
    ~FJournalState(){if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);}
#else
    int lock=-1;
    ~FJournalState(){if(lock>=0){::flock(lock,LOCK_UN);::close(lock);}}
#endif
};
void Ready(const FJournalState& state){if(!state.ready||state.poisoned)throw std::runtime_error("Journal must be reopened after failed commit");}
uint64_t ScanDisk(FJournalState& state,FSplatBuildControl& control) {
    uint64_t bytes=0;
    for(fs::recursive_directory_iterator it(state.root),end;it!=end;++it) {
        Poll(control);if(it.depth()>72)throw std::runtime_error("Journal directory depth limit exceeded");
        const auto status=it->symlink_status();if(fs::is_symlink(status))throw std::runtime_error("Symlink in private journal");
        bytes=Add(bytes,4096); // Conservative per-entry inode/directory allocation allowance.
        if(fs::is_regular_file(status))bytes=Add(bytes,it->file_size());
        else if(!fs::is_directory(status))throw std::runtime_error("Unsupported private journal file kind");
    }
    state.diskEstimate=bytes;return Add(Add(bytes,state.externalSourceBytes),state.externalOutputBytes);
}
void CheckDisk(FJournalState& state,uint64_t additional,FSplatBuildControl& control) {
    Poll(control);const auto available=fs::space(state.root).available;
    if(Add(Add(Add(state.diskEstimate,state.externalSourceBytes),state.externalOutputBytes),additional)>state.config.maxDiskBytes||
        Add(additional,state.config.diskReserveBytes)>available)throw std::runtime_error("Insufficient admitted disk live set or reserve");
}
std::atomic<uint64_t> TempSequence{0};
fs::path Temporary(const fs::path& target) {
#if PLATFORM_WINDOWS
    const auto pid=GetCurrentProcessId();
#else
    const auto pid=::getpid();
#endif
    return target.parent_path()/(target.filename().string()+".tmp-"+std::to_string(pid)+"-"+std::to_string(++TempSequence));
}
void AtomicBytes(FJournalState& state,const fs::path& target,std::span<const uint8_t> bytes,FSplatBuildControl& control,bool replace=true) {
    CheckDisk(state,Add(bytes.size(),4096),control);const auto temp=Temporary(target);
    try {FNativeFile file;file.Open(temp,true,true,true);file.Write(bytes);file.Sync();file.Close();AtomicRename(temp,target,replace);state.diskEstimate=Add(state.diskEstimate,Add(bytes.size(),4096));}
    catch(...){std::error_code ec;fs::remove(temp,ec);throw;}
}
void Task(FEncoder& out,const FSplatBuildTask& task) {
    out.U64(task.id);out.U64(task.parentId);out.U32(uint32_t(task.kind));out.U32(uint32_t(task.partition));out.U32(task.depth);out.U64(task.inputRecords);out.U64(task.committedGeneration);
}
FSplatBuildTask Task(FDecoder& in) {
    FSplatBuildTask task;task.id=in.U64();task.parentId=in.U64();task.kind=ESplatBuildTaskKind(in.U32());task.partition=ESplatBuildPartition(in.U32());task.depth=in.U32();task.inputRecords=in.U64();task.committedGeneration=in.U64();
    if(uint32_t(task.kind)>5||uint32_t(task.partition)>1||task.depth>64)throw std::runtime_error("Invalid journal task");return task;
}
void Chunk(FEncoder& out,const FSplatBuildChunk& chunk) {
    out.U64(chunk.id);out.U64(chunk.taskId);out.Text(chunk.relativePath);out.Text(chunk.sha256);out.U64(chunk.recordCount);out.U64(chunk.byteSize);out.U32(chunk.recordBytes);out.U64(chunk.committedGeneration);
}
FSplatBuildChunk Chunk(FDecoder& in) {
    FSplatBuildChunk chunk;chunk.id=in.U64();chunk.taskId=in.U64();chunk.relativePath=in.Text();chunk.sha256=in.Text();chunk.recordCount=in.U64();chunk.byteSize=in.U64();chunk.recordBytes=in.U32();chunk.committedGeneration=in.U64();
    if(!RelativeValid(chunk.relativePath)||!HashValid(chunk.sha256)||(chunk.recordBytes!=1&&chunk.recordBytes!=252&&chunk.recordBytes!=276)||chunk.byteSize!=Add(96,Multiply(chunk.recordCount,chunk.recordBytes)))throw std::runtime_error("Invalid journal chunk descriptor");return chunk;
}
template<class Visitor> void Frames(const fs::path& path,uint64_t prefix,FSplatBuildControl& control,Visitor visitor) {
    FNativeFile file;file.Open(path,false);uint64_t position=0;
    while(position<prefix) {
        Poll(control);std::array<uint8_t,4> sizeBytes{};if(file.Read(sizeBytes.data(),4)!=4)throw std::runtime_error("Truncated journal frame");FDecoder sizeIn{sizeBytes};const auto n=sizeIn.U32();
        if(!n||n>MaxFrameBytes||Add(position,Add(n,68))>prefix)throw std::runtime_error("Invalid committed journal frame length");
        std::vector<uint8_t> payload(n+64);if(file.Read(payload.data(),payload.size())!=payload.size())throw std::runtime_error("Truncated journal frame");
        payload=Unwrap(std::move(payload));FDecoder in{payload};visitor(in);in.End();position=Add(position,Add(n,68));
    }
}
void AppendFrame(FJournalState& state,const char* name,HashState& hash,uint64_t& prefix,std::vector<uint8_t> payload,FSplatBuildControl& control) {
    if(payload.empty()||payload.size()>MaxFrameBytes)throw std::runtime_error("Journal frame exceeds metadata bound");
    FEncoder frame;frame.U32(uint32_t(payload.size()));payload=Envelope(std::move(payload));frame.bytes.insert(frame.bytes.end(),payload.begin(),payload.end());
    CheckDisk(state,frame.bytes.size(),control);FNativeFile file;file.Open(SafePath(state.root,name),true,true);file.Seek(prefix);file.Write(frame.bytes);file.Sync();
    hash.Add(frame.bytes.data(),frame.bytes.size());prefix=Add(prefix,frame.bytes.size());state.diskEstimate=Add(state.diskEstimate,frame.bytes.size());
}
void CommitRoot(FJournalState& state,FSplatBuildCheckpoint next,FSplatBuildControl& control) {
    Poll(control);
    if(state.checkpoint.generation==UINT64_MAX)throw std::runtime_error("Journal generation exhausted");
    next.identity=state.checkpoint.identity;next.generation=state.checkpoint.generation+1;
    if(uint32_t(next.stage)>12||uint32_t(next.resumeStage)>12||next.sourceCursor.byteOffset>next.identity.sourceBytes||
        next.sourceCursor.nextOriginalId>next.identity.sourceRecords||next.committedRecords>next.identity.sourceRecords||next.committedPageCount>state.config.maxPages)
        throw std::runtime_error("Invalid checkpoint progress bounds");
    next.taskLogHash=Digest(state.taskHash);next.chunkLogHash=Digest(state.chunkHash);
    const auto oldBytes=EncodeCheckpoint(state.checkpoint),newBytes=EncodeCheckpoint(next);
    // Once durable publication begins, finish the small root transaction without
    // interpreting a new pause as permission to undo an already-published root.
    FSplatBuildControl commitControl;
    AtomicBytes(state,SafePath(state.root,"checkpoint.previous"),oldBytes,commitControl);
    AtomicBytes(state,SafePath(state.root,"checkpoint.current"),newBytes,commitControl);
    state.checkpoint=std::move(next);
}
void VerifyChunk(FJournalState& state,const FSplatBuildChunk& chunk,FSplatBuildControl& control,std::string* payloadHash=nullptr) {
    const auto path=SafePath(state.root,chunk.relativePath);
    if(fs::file_size(path)!=chunk.byteSize||Digest(HashPrefix(path,chunk.byteSize,control))!=chunk.sha256)throw std::runtime_error("Committed chunk content mismatch");
    FNativeFile file;file.Open(path,false);std::array<uint8_t,96> header{};
    if(file.Read(header.data(),header.size())!=header.size())throw std::runtime_error("Truncated chunk header");FDecoder in{header};
    const auto magic=in.U32(),version=in.U32(),stride=in.U32(),reserved=in.U32();const auto count=in.U64(),length=in.U64();
    std::string hash(reinterpret_cast<const char*>(header.data()+32),64);
    if(magic!=0x4353474d||version!=1||reserved||stride!=chunk.recordBytes||count!=chunk.recordCount||length!=Multiply(count,stride)||!HashValid(hash)||
        Digest(HashPrefix(path,length,control,96))!=hash)throw std::runtime_error("Chunk payload/header identity mismatch");
    if(payloadHash)*payloadHash=std::move(hash);
}
void RestorePublicationAccounting(FJournalState& state,FSplatBuildControl& control);
} // namespace

struct FSplatBuildJournal::FImpl { std::shared_ptr<FJournalState> state; };
struct FSplatBuildChunkWriter::FImpl {
    std::shared_ptr<FJournalState> state;FSplatBuildControl* control=nullptr;FNativeFile file;
    fs::path temporary,finalPath;FSplatBuildChunk chunk;HashState hash;bool finished=false,failed=false;
    ~FImpl(){file.Close();if(!finished){std::error_code ec;fs::remove(temporary,ec);}}
};
struct FSplatBuildChunkReader::FImpl {
    std::shared_ptr<FJournalState> state;FSplatBuildControl* control=nullptr;FNativeFile file;
    FSplatBuildChunk chunk;std::string payloadHash;uint64_t remaining=0;bool failed=false;
};

namespace {
template<class T> void WriterReady(T& writer) {
    Ready(*writer.state);Poll(*writer.control);
    if(writer.finished||writer.failed||writer.chunk.committedGeneration!=writer.state->checkpoint.generation+1)
        throw std::runtime_error("Chunk writer is finished, failed or stale");
}
template<class T> void AppendEncoded(T& writer,std::span<const uint8_t> bytes,uint64_t records) {
    WriterReady(writer);CheckDisk(*writer.state,bytes.size(),*writer.control);
    if(Add(writer.chunk.recordCount,records)>writer.state->config.maxSourceRecords&&writer.chunk.recordBytes!=1)
        throw std::runtime_error("Chunk record count exceeds supported source scale");
    writer.file.Write(bytes);writer.hash.Add(bytes.data(),bytes.size());
    writer.chunk.recordCount=Add(writer.chunk.recordCount,records);
    writer.state->diskEstimate=Add(writer.state->diskEstimate,bytes.size());
}
void ValidateWeightedRecord(const FSplatWeightedGaussian& record) {
    (void)FSplatProxyMath::FromGaussian(record.gaussian);
    if(record.gaussian.position[3]!=1||record.gaussian.scale[3]!=0||
        !std::isfinite(record.geometryErrorSquared)||record.geometryErrorSquared<0||
        !std::isfinite(record.appearanceErrorSquared)||record.appearanceErrorSquared<0||
        !std::isfinite(record.weight)||record.weight<=0)throw std::runtime_error("Invalid weighted spool record");
}
template<class T> uint64_t ReadCount(T& reader,uint32_t maximum,uint32_t stride,uint64_t priorOutputBytes) {
    Ready(*reader.state);Poll(*reader.control);
    if(reader.failed||reader.chunk.recordBytes!=stride||!maximum||maximum>(stride==1?(1u<<20):reader.state->config.batchCapacity))
        throw std::runtime_error("Invalid chunk reader state or batch size");
    const auto count=std::min<uint64_t>(maximum,reader.remaining);
    const auto scratch=Add(Multiply(count,stride+(stride==1?1:sizeof(FSplatWeightedGaussian))),priorOutputBytes);
    if(Add(scratch,reader.state->metadataBytes)>reader.state->config.workingMemoryBytes)throw std::runtime_error("Chunk read exceeds admitted workspace");
    return count;
}
template<class T> std::vector<uint8_t> ReadEncoded(T& reader,uint64_t count) {
    const auto bytes=Multiply(count,reader.chunk.recordBytes);std::vector<uint8_t> result(static_cast<size_t>(bytes));
    size_t done=0;
    while(done<result.size()) {
        Poll(*reader.control);const auto n=std::min<size_t>(65536,result.size()-done);
        if(reader.file.Read(result.data()+done,n)!=n)throw std::runtime_error("Truncated chunk payload while reading");done+=n;
    }
    return result;
}
}

FSplatBuildChunkWriter::FSplatBuildChunkWriter(std::unique_ptr<FImpl> value):impl(std::move(value)){}
FSplatBuildChunkWriter::~FSplatBuildChunkWriter()=default;
bool FSplatBuildChunkWriter::Append(std::span<const FSplatGaussian> records,std::string& error) {
    try {
        WriterReady(*impl);
        if(impl->chunk.recordBytes!=252||records.size()>impl->state->config.batchCapacity)throw std::runtime_error("Invalid Gaussian spool batch");
        FEncoder encoded;encoded.bytes.reserve(static_cast<size_t>(Multiply(records.size(),252)));
        size_t i=0;for(const auto& record:records) {
            if((i++&255)==0)Poll(*impl->control);(void)FSplatProxyMath::FromGaussian(record);
            if(record.position[3]!=1||record.scale[3]!=0)throw std::runtime_error("Invalid normalized Gaussian padding");Gaussian(encoded,record);
        }
        AppendEncoded(*impl,encoded.bytes,records.size());error.clear();return true;
    }catch(const std::exception& exception){impl->failed=true;error=exception.what();return false;}
}
bool FSplatBuildChunkWriter::AppendWeighted(std::span<const FSplatWeightedGaussian> records,std::string& error) {
    try {
        WriterReady(*impl);
        if(impl->chunk.recordBytes!=276||records.size()>impl->state->config.batchCapacity)throw std::runtime_error("Invalid weighted spool batch");
        FEncoder encoded;encoded.bytes.reserve(static_cast<size_t>(Multiply(records.size(),276)));
        size_t i=0;for(const auto& record:records) {
            if((i++&255)==0)Poll(*impl->control);ValidateWeightedRecord(record);Gaussian(encoded,record.gaussian);
            encoded.F64(record.geometryErrorSquared);encoded.F64(record.appearanceErrorSquared);encoded.F64(record.weight);
        }
        AppendEncoded(*impl,encoded.bytes,records.size());error.clear();return true;
    }catch(const std::exception& exception){impl->failed=true;error=exception.what();return false;}
}
bool FSplatBuildChunkWriter::AppendBytes(std::span<const uint8_t> bytes,std::string& error) {
    try {
        if(impl->chunk.recordBytes!=1||bytes.size()>(1u<<20))throw std::runtime_error("Invalid opaque spool batch");
        AppendEncoded(*impl,bytes,bytes.size());error.clear();return true;
    }catch(const std::exception& exception){impl->failed=true;error=exception.what();return false;}
}
bool FSplatBuildChunkWriter::Finish(FSplatBuildChunk& descriptor,std::string& error) {
    try {
        WriterReady(*impl);FEncoder header;header.U32(0x4353474d);header.U32(1);header.U32(impl->chunk.recordBytes);header.U32(0);
        header.U64(impl->chunk.recordCount);const auto payloadBytes=Multiply(impl->chunk.recordCount,impl->chunk.recordBytes);header.U64(payloadBytes);
        const auto hash=Digest(impl->hash);header.bytes.insert(header.bytes.end(),hash.begin(),hash.end());
        if(header.bytes.size()!=96)throw std::runtime_error("Invalid encoded spool header");
        impl->file.Seek(0);impl->file.Write(header.bytes);impl->file.Sync();impl->file.Close();
        impl->chunk.byteSize=Add(96,payloadBytes);
        impl->chunk.sha256=Digest(HashPrefix(impl->temporary,impl->chunk.byteSize,*impl->control));
        Poll(*impl->control);AtomicRename(impl->temporary,impl->finalPath,false);impl->finished=true;
        descriptor=impl->chunk;error.clear();return true;
    }catch(const std::exception& exception){impl->failed=true;error=exception.what();return false;}
}

FSplatBuildChunkReader::FSplatBuildChunkReader(std::unique_ptr<FImpl> value):impl(std::move(value)){}
FSplatBuildChunkReader::~FSplatBuildChunkReader()=default;
const FSplatBuildChunk& FSplatBuildChunkReader::Descriptor()const{return impl->chunk;}
const std::string& FSplatBuildChunkReader::PayloadSHA256()const{return impl->payloadHash;}
uint64_t FSplatBuildChunkReader::RemainingRecords()const{return impl->remaining;}
bool FSplatBuildChunkReader::Read(uint32_t maximum,std::vector<FSplatGaussian>& records,std::string& error) {
    records.clear();
    try {
        const auto count=ReadCount(*impl,maximum,252,Multiply(records.capacity(),sizeof(FSplatGaussian)));
        const auto encoded=ReadEncoded(*impl,count);FDecoder in{encoded};std::vector<FSplatGaussian> result;result.reserve(static_cast<size_t>(count));
        for(uint64_t i=0;i<count;++i){if((i&255)==0)Poll(*impl->control);result.push_back(Gaussian(in));}in.End();
        impl->remaining-=count;records=std::move(result);error.clear();return true;
    }catch(const std::exception& exception){impl->failed=true;error=exception.what();return false;}
}
bool FSplatBuildChunkReader::ReadWeighted(uint32_t maximum,std::vector<FSplatWeightedGaussian>& records,std::string& error) {
    records.clear();
    try {
        const auto count=ReadCount(*impl,maximum,276,Multiply(records.capacity(),sizeof(FSplatWeightedGaussian)));
        const auto encoded=ReadEncoded(*impl,count);FDecoder in{encoded};std::vector<FSplatWeightedGaussian> result;result.reserve(static_cast<size_t>(count));
        for(uint64_t i=0;i<count;++i) {
            if((i&255)==0)Poll(*impl->control);FSplatWeightedGaussian record;record.gaussian=Gaussian(in);
            record.geometryErrorSquared=in.F64();record.appearanceErrorSquared=in.F64();record.weight=in.F64();ValidateWeightedRecord(record);result.push_back(record);
        }
        in.End();impl->remaining-=count;records=std::move(result);error.clear();return true;
    }catch(const std::exception& exception){impl->failed=true;error=exception.what();return false;}
}
bool FSplatBuildChunkReader::ReadBytes(uint32_t maximum,std::vector<uint8_t>& bytes,std::string& error) {
    bytes.clear();
    try {
        const auto count=ReadCount(*impl,maximum,1,bytes.capacity());auto result=ReadEncoded(*impl,count);
        impl->remaining-=count;bytes=std::move(result);error.clear();return true;
    }catch(const std::exception& exception){impl->failed=true;error=exception.what();return false;}
}

FSplatBuildJournal::FSplatBuildJournal():impl(std::make_unique<FImpl>()){}
FSplatBuildJournal::~FSplatBuildJournal()=default;
const FSplatBuildCheckpoint& FSplatBuildJournal::Checkpoint()const {
    if(!impl->state)throw std::runtime_error("Journal is not open");Ready(*impl->state);return impl->state->checkpoint;
}
uint64_t FSplatBuildJournal::MetadataBytes()const{return impl->state?impl->state->metadataBytes:0;}

bool FSplatBuildJournal::ReadTasks(const TaskVisitor& visitor,FSplatBuildControl& control,std::string& error)const {
    try {
        if(!impl->state||!visitor)throw std::runtime_error("Journal/task visitor is unavailable");const auto& state=*impl->state;Ready(state);
        uint64_t count=0;
        Frames(SafePath(state.root,"tasks.log"),state.checkpoint.taskLogBytes,control,[&](FDecoder& in) {
            const auto task=Task(in);if(!task.committedGeneration||task.committedGeneration>state.checkpoint.generation)throw std::runtime_error("Uncommitted task in checkpoint prefix");
            ++count;if(!visitor(task,error))throw std::runtime_error(error.empty()?"Task visitor stopped":error);
        });
        if(count!=state.checkpoint.committedTaskCount)throw std::runtime_error("Committed task count mismatch");error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool FSplatBuildJournal::ReadChunks(const ChunkVisitor& visitor,FSplatBuildControl& control,std::string& error)const {
    try {
        if(!impl->state||!visitor)throw std::runtime_error("Journal/chunk visitor is unavailable");const auto& state=*impl->state;Ready(state);
        uint64_t created=0;
        Frames(SafePath(state.root,"chunks.log"),state.checkpoint.chunkLogBytes,control,[&](FDecoder& in) {
            const auto event=in.U32();if(event!=1&&event!=2)throw std::runtime_error("Unknown journal chunk event");
            const auto chunk=Chunk(in);if(!chunk.committedGeneration||chunk.committedGeneration>state.checkpoint.generation)throw std::runtime_error("Uncommitted chunk in checkpoint prefix");
            if(event==1)++created;
            if(!visitor(chunk,event==2,error))throw std::runtime_error(error.empty()?"Chunk visitor stopped":error);
        });
        if(created!=state.checkpoint.committedChunkCount)throw std::runtime_error("Committed chunk count mismatch");error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool FSplatBuildJournal::FindChunk(uint64_t id,FSplatBuildChunk& chunk,bool& found,bool& retired,FSplatBuildControl& control,std::string& error)const {
    found=false;retired=false;FSplatBuildChunk value;
    const bool ok=ReadChunks([&](const FSplatBuildChunk& candidate,bool dead,std::string&) {
        if(candidate.id==id){found=true;retired=dead;value=candidate;}return true;
    },control,error);
    if(ok&&found)chunk=std::move(value);if(!ok){found=false;retired=false;}return ok;
}

bool FSplatBuildJournal::Open(const std::string& jobRoot,const std::string& stableSource,
    const FSplatBuildIdentity& expected,const FSplatBoundedBuildConfig& config,FSplatBuildControl& control,std::string& error) {
    if(impl->state){error="Journal is already open";return false;}
    try {
        Poll(control);
        if(jobRoot.empty()||stableSource.empty()||!HashValid(expected.sourceSHA256)||!HashValid(expected.builderHash)||!HashValid(expected.configHash)||
            expected.builderVersion!=SplatBoundedBuilderVersion||expected.parserVersion!=SplatBuildParserVersion||expected.schemaVersion!=1||
            expected.canonicalConfig.empty()||expected.canonicalConfig.size()>65536||config.builderVersion!=expected.builderVersion||config.parserVersion!=expected.parserVersion||
            config.schemaVersion!=expected.schemaVersion||config.builderHash!=expected.builderHash||!config.batchCapacity||!config.maxDiskBytes||!config.maxSourceRecords||
            expected.sourceBytes>config.maxSourceBytes||expected.sourceRecords>config.maxSourceRecords||config.metadataBytes>config.workingMemoryBytes||!config.maxPages||config.maxDepth>64)
            throw std::runtime_error("Invalid journal identity or resource policy");
        const std::vector<uint8_t> canonical(expected.canonicalConfig.begin(),expected.canonicalConfig.end());
        if(FSplatSceneFormat::SHA256(canonical)!=expected.configHash)throw std::runtime_error("Canonical build configuration hash mismatch");
        auto state=std::make_shared<FJournalState>();state->config=config;
        fs::create_directories(fs::u8path(jobRoot));state->root=fs::canonical(fs::u8path(jobRoot));
        if(!fs::is_directory(state->root))throw std::runtime_error("Journal root is not a directory");
        const auto lockPath=SafePath(state->root,"journal.lock");
#if PLATFORM_WINDOWS
        state->lock=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(state->lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Another worker owns this journal");
#else
        state->lock=::open(lockPath.c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
        if(state->lock<0||::flock(state->lock,LOCK_EX|LOCK_NB)!=0)throw std::runtime_error("Another worker owns this journal");
#endif
        state->source=fs::canonical(fs::u8path(stableSource));
        if(Within(state->root,state->source)) {
            const auto relative=state->source.lexically_relative(state->root);
            const auto first=UTF8(*relative.begin());
            if(first=="chunks"||first=="tasks.log"||first=="chunks.log"||first=="journal.lock"||
                first.starts_with("checkpoint.")||first=="publication.intent")
                throw std::runtime_error("Stable source overlaps journal-owned transaction files");
        }
        if(!fs::is_regular_file(state->source)||fs::file_size(state->source)!=expected.sourceBytes||Digest(HashPrefix(state->source,expected.sourceBytes,control))!=expected.sourceSHA256)
            throw std::runtime_error("Stable source identity differs from checkpoint request");
        state->externalSourceBytes=Within(state->root,state->source)?0:expected.sourceBytes;
        state->metadataBytes=Add(Add(1u<<20,Multiply(config.batchCapacity,276)),Add(Multiply(expected.canonicalConfig.size(),4),MaximumRootBytes));
        state->metadataBytes=Add(state->metadataBytes,Add(UTF8(state->root).size(),UTF8(state->source).size()));
        if(state->metadataBytes>config.metadataBytes)throw std::runtime_error("Journal metadata exceeds admitted sub-budget");
        SafePath(state->root,"chunks/placeholder",true);SyncDirectory(state->root);
        const auto current=SafePath(state->root,"checkpoint.current"),previous=SafePath(state->root,"checkpoint.previous");
        const auto taskPath=SafePath(state->root,"tasks.log"),chunkPath=SafePath(state->root,"chunks.log");
        for(const auto& path:{taskPath,chunkPath}){FNativeFile file;file.Open(path,true,true);file.Sync();}
        ScanDisk(*state,control);
        if(!fs::exists(current)&&!fs::exists(previous)) {
            if(fs::file_size(taskPath)||fs::file_size(chunkPath))throw std::runtime_error("Nonempty orphan logs have no compatible checkpoint");
            state->checkpoint.identity=expected;state->checkpoint.generation=1;
            state->checkpoint.taskLogHash=Digest(state->taskHash);state->checkpoint.chunkLogHash=Digest(state->chunkHash);
            AtomicBytes(*state,current,EncodeCheckpoint(state->checkpoint),control,false);
        } else {
            bool loaded=false;std::string last;
            for(const auto& candidate:{current,previous}) {
                if(!fs::exists(candidate))continue;
                try {
                    auto cp=DecodeCheckpoint(ReadSmall(candidate,MaximumRootBytes));
                    if(!SameIdentity(cp.identity,expected))throw std::runtime_error("Incompatible checkpoint source/parser/builder/config identity");
                    if(cp.sourceCursor.byteOffset>expected.sourceBytes||cp.sourceCursor.nextOriginalId>expected.sourceRecords||cp.committedRecords>expected.sourceRecords||cp.committedPageCount>config.maxPages)
                        throw std::runtime_error("Checkpoint cursor or output count exceeds identity bounds");
                    auto tasks=HashPrefix(taskPath,cp.taskLogBytes,control),chunks=HashPrefix(chunkPath,cp.chunkLogBytes,control);
                    if(Digest(tasks)!=cp.taskLogHash||Digest(chunks)!=cp.chunkLogHash)throw std::runtime_error("Committed log prefix checksum mismatch");
                    state->checkpoint=std::move(cp);state->taskHash=tasks;state->chunkHash=chunks;loaded=true;break;
                }catch(const std::exception& exception){
                    last=exception.what();Poll(control);
                    if(last.find("Incompatible checkpoint")==0)throw;
                }
            }
            if(!loaded)throw std::runtime_error(last.empty()?"No valid checkpoint root":last);
        }
        for(const auto& item:{std::pair{taskPath,state->checkpoint.taskLogBytes},std::pair{chunkPath,state->checkpoint.chunkLogBytes}}) {
            FNativeFile file;file.Open(item.first,true);file.Truncate(item.second);file.Sync();
        }
        state->ready=true;impl->state=state;
        // Verify all live references with bounded event replay. Retired payloads
        // may be absent because their durable child coverage was committed first.
        if(!ReadTasks([](const FSplatBuildTask&,std::string&){return true;},control,error))throw std::runtime_error(error);
        if(!ReadChunks([&](const FSplatBuildChunk& chunk,bool retired,std::string& detail) {
            if(retired) return true;
            FSplatBuildChunk latest;bool found=false,dead=false;
            if(!FindChunk(chunk.id,latest,found,dead,control,detail))return false;
            if(!found)throw std::runtime_error("Committed chunk lookup failed");
            if(!dead)VerifyChunk(*state,latest,control);return true;
        },control,error))throw std::runtime_error(error);
        // Keep only referenced committed chunk files. Future-generation outputs
        // and unique temporary files cannot be adopted without a root commit.
        const auto chunksDirectory=SafePath(state->root,"chunks");
        for(fs::directory_iterator it(chunksDirectory),end;it!=end;++it) {
            Poll(control);const auto name=it->path().filename().string();
            if(it->is_symlink()||!it->is_regular_file())throw std::runtime_error("Unsafe chunk directory entry");
            bool remove=name.find(".tmp-")!=std::string::npos;
            if(!remove&&name.size()>8&&name.front()=='g'&&name.ends_with(".mgbc")) {
                const auto dash=name.find('-'),dot=name.rfind('.');
                if(dash==std::string::npos||dot<=dash+1)throw std::runtime_error("Invalid chunk artifact name");
                size_t consumed=0;const auto id=std::stoull(name.substr(dash+1,dot-dash-1),&consumed);
                if(consumed!=dot-dash-1)throw std::runtime_error("Invalid chunk artifact ID");
                FSplatBuildChunk chunk;bool found=false,dead=false;
                if(!FindChunk(id,chunk,found,dead,control,error))throw std::runtime_error(error);
                remove=!found||dead||chunk.relativePath!="chunks/"+name;
            }
            if(remove)fs::remove(it->path());
        }
        SyncDirectory(chunksDirectory);
        // Restore a valid current root when recovery selected the previous one.
        AtomicBytes(*state,current,EncodeCheckpoint(state->checkpoint),control);
        RestorePublicationAccounting(*state,control);
        ScanDisk(*state,control);CheckDisk(*state,0,control);error.clear();return true;
    }catch(const std::exception& exception){impl->state.reset();error=exception.what();return false;}
}

bool FSplatBuildJournal::CheckAdmission(FSplatBuildAdmission& admission,FSplatBuildControl& control,std::string& error) {
    try {
        if(!impl->state)throw std::runtime_error("Journal is not open");auto& state=*impl->state;Ready(state);
        const uint64_t actual=ScanDisk(state,control);
        uint64_t estimate=0;for(const auto value:{admission.sourceCopyBytes,admission.normalizedBytes,admission.partitionOverlapBytes,admission.leafPageBytes,
            admission.proxyAndSidecarBytes,admission.journalBytes,admission.packageBytes})estimate=Add(estimate,value);
        if(admission.requiredDiskBytes&&admission.requiredDiskBytes!=estimate)throw std::runtime_error("Phase disk estimate is not a disjoint checked sum");
        admission.requiredDiskBytes=estimate;admission.availableDiskBytes=fs::space(state.root).available;
        if(admission.metadataBytes<state.metadataBytes||admission.metadataBytes>state.config.metadataBytes||admission.metadataBytes>admission.workingBytes||admission.workingBytes>state.config.workingMemoryBytes||
            std::max(estimate,Add(actual,admission.additionalDiskBytes))>state.config.maxDiskBytes||Add(admission.additionalDiskBytes,state.config.diskReserveBytes)>admission.availableDiskBytes)
            throw std::runtime_error("Phase workspace, metadata or disk admission failed");
        error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

bool FSplatBuildJournal::CreateChunk(uint64_t id,uint64_t taskId,uint32_t stride,FSplatBuildControl& control,
    std::unique_ptr<FSplatBuildChunkWriter>& writer,std::string& error) {
    writer.reset();
    try {
        if(!impl->state)throw std::runtime_error("Journal is not open");auto state=impl->state;Ready(*state);Poll(control);
        if(stride!=1&&stride!=252&&stride!=276)throw std::runtime_error("Unsupported spool stride");
        if(state->checkpoint.generation==UINT64_MAX)throw std::runtime_error("Journal generation exhausted");
        FSplatBuildChunk previous;bool found=false,dead=false;
        if(!FindChunk(id,previous,found,dead,control,error))throw std::runtime_error(error);
        if(found)throw std::runtime_error("Chunk identifier cannot be reused");
        ScanDisk(*state,control);CheckDisk(*state,96+4096,control);
        auto value=std::make_unique<FSplatBuildChunkWriter::FImpl>();value->state=state;value->control=&control;
        value->chunk.id=id;value->chunk.taskId=taskId;value->chunk.recordBytes=stride;value->chunk.committedGeneration=state->checkpoint.generation+1;
        value->chunk.relativePath="chunks/g"+std::to_string(value->chunk.committedGeneration)+"-"+std::to_string(id)+".mgbc";
        value->finalPath=SafePath(state->root,value->chunk.relativePath);
        if(fs::exists(value->finalPath))throw std::runtime_error("Uncommitted artifact exists; reopen the journal before retry");
        value->temporary=Temporary(value->finalPath);value->file.Open(value->temporary,true,true,true);
        const std::array<uint8_t,96> empty{};value->file.Write(empty);state->diskEstimate=Add(state->diskEstimate,96+4096);
        writer=std::unique_ptr<FSplatBuildChunkWriter>(new FSplatBuildChunkWriter(std::move(value)));error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool FSplatBuildJournal::OpenChunk(uint64_t id,FSplatBuildControl& control,std::unique_ptr<FSplatBuildChunkReader>& reader,std::string& error)const {
    reader.reset();
    try {
        if(!impl->state)throw std::runtime_error("Journal is not open");auto state=impl->state;Ready(*state);
        FSplatBuildChunk chunk;bool found=false,dead=false;
        if(!FindChunk(id,chunk,found,dead,control,error))throw std::runtime_error(error);
        if(!found||dead)throw std::runtime_error("Chunk is not a live committed artifact");
        auto value=std::make_unique<FSplatBuildChunkReader::FImpl>();value->state=state;value->control=&control;value->chunk=chunk;value->remaining=chunk.recordCount;
        VerifyChunk(*state,chunk,control,&value->payloadHash);value->file.Open(SafePath(state->root,chunk.relativePath),false);value->file.Seek(96);
        reader=std::unique_ptr<FSplatBuildChunkReader>(new FSplatBuildChunkReader(std::move(value)));error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

namespace {
FSplatBuildCheckpoint NextState(const FJournalState& state,const FSplatBuildCheckpoint& requested) {
    FSplatBuildCheckpoint next=state.checkpoint;
    if(requested.sourceCursor.byteOffset<next.sourceCursor.byteOffset||requested.sourceCursor.nextOriginalId<next.sourceCursor.nextOriginalId||
        requested.committedRecords<next.committedRecords||requested.committedPageCount<next.committedPageCount)
        throw std::runtime_error("Checkpoint progress cannot move behind durable work");
    next.stage=requested.stage;next.resumeStage=requested.resumeStage;next.sourceCursor=requested.sourceCursor;
    next.committedRecords=requested.committedRecords;next.committedPageCount=requested.committedPageCount;return next;
}
bool SameChunk(const FSplatBuildChunk& a,const FSplatBuildChunk& b) {
    FEncoder aa,bb;Chunk(aa,a);Chunk(bb,b);return aa.bytes==bb.bytes;
}
}
bool FSplatBuildJournal::CommitWorkUnit(FSplatBuildTask task,std::span<const FSplatBuildChunk> outputs,
    const FSplatBuildCheckpoint& requested,FSplatBuildControl& control,std::string& error) {
    try {
        if(!impl->state)throw std::runtime_error("Journal is not open");auto& state=*impl->state;Ready(state);Poll(control);
        auto next=NextState(state,requested);
        if(task.depth>state.config.maxDepth||uint32_t(task.kind)>5||uint32_t(task.partition)>1||task.inputRecords>state.config.maxSourceRecords)
            throw std::runtime_error("Work-unit task exceeds build policy");
        bool duplicate=false;
        if(!ReadTasks([&](const FSplatBuildTask& old,std::string&){if(old.id==task.id)duplicate=true;return true;},control,error))throw std::runtime_error(error);
        if(duplicate)throw std::runtime_error("Work-unit task identifier cannot be reused");
        uint64_t metadata=state.metadataBytes;
        for(size_t i=0;i<outputs.size();++i) {
            Poll(control);const auto& chunk=outputs[i];metadata=Add(metadata,Add(sizeof(FSplatBuildChunk),Add(chunk.relativePath.capacity(),chunk.sha256.capacity())));
            if(metadata>state.config.metadataBytes)throw std::runtime_error("Work-unit descriptor metadata exceeds sub-budget");
            if(chunk.taskId!=task.id||chunk.committedGeneration!=state.checkpoint.generation+1)throw std::runtime_error("Output belongs to another task or generation");
            FEncoder encoded;Chunk(encoded,chunk);FDecoder check{encoded.bytes};(void)Chunk(check);check.End();
            const std::string canonical="chunks/g"+std::to_string(chunk.committedGeneration)+"-"+std::to_string(chunk.id)+".mgbc";
            if(chunk.relativePath!=canonical)throw std::runtime_error("Output artifact name does not match its identity");
            for(size_t j=0;j<i;++j)if(outputs[j].id==chunk.id)throw std::runtime_error("Duplicate work-unit output ID");
            FSplatBuildChunk old;bool found=false,retired=false;
            if(!FindChunk(chunk.id,old,found,retired,control,error))throw std::runtime_error(error);
            if(found)throw std::runtime_error("Output ID is already committed");
            VerifyChunk(state,chunk,control);
        }
        ScanDisk(state,control);CheckDisk(state,Add(2*MaximumRootBytes,Multiply(outputs.size()+1,MaxFrameBytes+68)),control);
        task.committedGeneration=state.checkpoint.generation+1;
        FEncoder taskBytes;Task(taskBytes,task);
        AppendFrame(state,"tasks.log",state.taskHash,next.taskLogBytes,std::move(taskBytes.bytes),control);
        next.committedTaskCount=Add(next.committedTaskCount,1);
        for(const auto& chunk:outputs) {
            FEncoder event;event.U32(1);Chunk(event,chunk);
            AppendFrame(state,"chunks.log",state.chunkHash,next.chunkLogBytes,std::move(event.bytes),control);
            next.committedChunkCount=Add(next.committedChunkCount,1);
        }
        CommitRoot(state,std::move(next),control);error.clear();return true;
    }catch(const std::exception& exception){if(impl->state)impl->state->poisoned=true;error=exception.what();return false;}
}
bool FSplatBuildJournal::CommitCheckpoint(const FSplatBuildCheckpoint& requested,FSplatBuildControl& control,std::string& error) {
    try {
        if(!impl->state)throw std::runtime_error("Journal is not open");auto& state=*impl->state;Ready(state);
        CommitRoot(state,NextState(state,requested),control);error.clear();return true;
    }catch(const std::exception& exception){if(impl->state)impl->state->poisoned=true;error=exception.what();return false;}
}
bool FSplatBuildJournal::RetirePartitionInput(uint64_t parentId,std::span<const uint64_t> children,FSplatBuildControl& control,std::string& error) {
    try {
        if(!impl->state)throw std::runtime_error("Journal is not open");auto& state=*impl->state;Ready(state);
        if(children.empty()||children.size()>8)throw std::runtime_error("Partition retirement requires one to eight complete children");
        FSplatBuildChunk parent;bool found=false,dead=false;
        if(!FindChunk(parentId,parent,found,dead,control,error))throw std::runtime_error(error);
        if(!found||dead||parent.recordBytes==1)throw std::runtime_error("Partition parent is not a live record chunk");
        VerifyChunk(state,parent,control);uint64_t records=0;
        for(size_t i=0;i<children.size();++i) {
            Poll(control);if(children[i]==parentId)throw std::runtime_error("Parent cannot replace itself");
            for(size_t j=0;j<i;++j)if(children[j]==children[i])throw std::runtime_error("Repeated replacement chunk");
            FSplatBuildChunk child;bool exists=false,retired=false;
            if(!FindChunk(children[i],child,exists,retired,control,error))throw std::runtime_error(error);
            if(!exists||retired||child.recordBytes!=parent.recordBytes)throw std::runtime_error("Replacement child is not durably committed");
            bool direct=false;
            if(!ReadTasks([&](const FSplatBuildTask& task,std::string&) {
                if(task.id==child.taskId&&task.parentId==parent.taskId&&task.kind==ESplatBuildTaskKind::Partition&&task.inputRecords==parent.recordCount)direct=true;return true;
            },control,error))throw std::runtime_error(error);
            if(!direct)throw std::runtime_error("Replacement is not a direct partition child");
            VerifyChunk(state,child,control);records=Add(records,child.recordCount);
        }
        if(records!=parent.recordCount)throw std::runtime_error("Partition children do not preserve every parent record");
        auto next=state.checkpoint;parent.committedGeneration=state.checkpoint.generation+1;
        FEncoder tombstone;tombstone.U32(2);Chunk(tombstone,parent);
        AppendFrame(state,"chunks.log",state.chunkHash,next.chunkLogBytes,std::move(tombstone.bytes),control);
        CommitRoot(state,std::move(next),control);
        // Only after the tombstone's checkpoint is durable is its sole old input
        // removable. Cancellation after this point leaves an extra safe file.
        const auto path=SafePath(state.root,parent.relativePath);fs::remove(path);SyncDirectory(path.parent_path());
        ScanDisk(state,control);error.clear();return true;
    }catch(const std::exception& exception){if(impl->state)impl->state->poisoned=true;error=exception.what();return false;}
}
bool FSplatBuildJournal::MaterializeChunk(uint64_t id,const std::string& relative,FSplatBuildControl& control,std::string& error) {
    fs::path temporary;
    try {
        if(!impl->state)throw std::runtime_error("Journal is not open");auto& state=*impl->state;Ready(state);
        const auto first=relative.substr(0,relative.find('/'));
        if(first=="chunks"||first=="tasks.log"||first=="chunks.log"||first=="journal.lock"||first.starts_with("checkpoint.")||first=="publication.intent")
            throw std::runtime_error("Scene payload cannot replace journal metadata");
        const auto target=SafePath(state.root,relative,true);
        std::unique_ptr<FSplatBuildChunkReader> reader;
        if(!OpenChunk(id,control,reader,error))throw std::runtime_error(error);
        const auto payloadBytes=Multiply(reader->Descriptor().recordCount,reader->Descriptor().recordBytes);
        if(fs::exists(target)) {
            if(!fs::is_regular_file(target)||fs::file_size(target)!=payloadBytes||Digest(HashPrefix(target,payloadBytes,control))!=reader->PayloadSHA256())
                throw std::runtime_error("Existing materialized payload differs from committed chunk");
            error.clear();return true;
        }
        ScanDisk(state,control);CheckDisk(state,Add(payloadBytes,4096),control);
        temporary=Temporary(target);FNativeFile output;output.Open(temporary,true,true,true);state.diskEstimate=Add(state.diskEstimate,4096);
        std::array<uint8_t,65536> buffer;uint64_t remaining=payloadBytes;HashState hash;
        while(remaining) {
            Poll(control);const auto n=size_t(std::min<uint64_t>(remaining,buffer.size()));CheckDisk(state,n,control);
            if(reader->impl->file.Read(buffer.data(),n)!=n)throw std::runtime_error("Truncated source during payload materialization");
            output.Write(std::span<const uint8_t>(buffer.data(),n));hash.Add(buffer.data(),n);remaining-=n;state.diskEstimate=Add(state.diskEstimate,n);
        }
        if(Digest(hash)!=reader->PayloadSHA256())throw std::runtime_error("Materialized payload checksum mismatch");
        output.Sync();output.Close();Poll(control);AtomicRename(temporary,target,false);error.clear();return true;
    }catch(const std::exception& exception){if(!temporary.empty()){std::error_code ec;fs::remove(temporary,ec);}error=exception.what();return false;}
}

namespace {
struct FPublicationIntent {
    FSplatBuildIdentity identity;
    std::string staging,output,manifestHash,token;
    uint64_t generation=0;
};
std::vector<uint8_t> EncodeIntent(const FPublicationIntent& intent) {
    FEncoder out;out.U32(0x5053474d);out.U32(1);Identity(out,intent.identity);out.Text(intent.staging);out.Text(intent.output);
    out.Text(intent.manifestHash);out.Text(intent.token);out.U64(intent.generation);return Envelope(std::move(out.bytes));
}
FPublicationIntent DecodeIntent(std::vector<uint8_t> bytes) {
    bytes=Unwrap(std::move(bytes));FDecoder in{bytes};
    if(in.U32()!=0x5053474d||in.U32()!=1)throw std::runtime_error("Unsupported scene publication intent");
    FPublicationIntent intent;intent.identity=Identity(in);intent.staging=in.Text();intent.output=in.Text();intent.manifestHash=in.Text();intent.token=in.Text();intent.generation=in.U64();in.End();
    if(!RelativeValid(intent.staging)||!HashValid(intent.manifestHash)||!HashValid(intent.token)||!intent.generation||!fs::u8path(intent.output).is_absolute())
        throw std::runtime_error("Invalid scene publication intent");return intent;
}
uint64_t SceneTreeBytes(const fs::path& directory,FSplatBuildControl& control,bool flush) {
    if(fs::is_symlink(fs::symlink_status(directory))||!fs::is_directory(directory))throw std::runtime_error("Published scene is not a real directory");
    uint64_t bytes=4096;
    for(fs::recursive_directory_iterator it(directory),end;it!=end;++it) {
        Poll(control);if(it.depth()>72)throw std::runtime_error("Scene staging depth exceeds supported limit");
        const auto status=it->symlink_status();if(fs::is_symlink(status))throw std::runtime_error("Symlink in scene publication");
        bytes=Add(bytes,4096);
        if(fs::is_regular_file(status)) {
            bytes=Add(bytes,it->file_size());
            if(flush){FNativeFile file;file.Open(it->path(),true);file.Sync();}
        } else if(fs::is_directory(status)){if(flush)SyncDirectory(it->path());}
        else throw std::runtime_error("Unsupported scene file kind");
    }
    if(flush)SyncDirectory(directory);return bytes;
}
void VerifyPublicationOwner(const fs::path& output,const FPublicationIntent& intent,FSplatBuildControl& control) {
    const auto marker=output/".monster-publication";
    if(fs::is_symlink(fs::symlink_status(output))||!fs::is_directory(output)||fs::is_symlink(fs::symlink_status(marker)))
        throw std::runtime_error("Unsafe published scene owner marker");
    const auto bytes=ReadSmall(marker,64);const std::string token(bytes.begin(),bytes.end());
    if(token!=intent.token)throw std::runtime_error("Existing output belongs to another publication");
    const auto manifest=output/"scene.mgs";
    if(fs::is_symlink(fs::symlink_status(manifest))||!fs::is_regular_file(manifest)||Digest(HashPrefix(manifest,fs::file_size(manifest),control))!=intent.manifestHash)
        throw std::runtime_error("Published manifest differs from durable intent");
}
void RestorePublicationAccounting(FJournalState& state,FSplatBuildControl& control) {
    const auto intentPath=SafePath(state.root,"publication.intent");
    if(!fs::exists(intentPath)) {
        if(state.checkpoint.published)throw std::runtime_error("Completed checkpoint has no publication intent");return;
    }
    const auto intent=DecodeIntent(ReadSmall(intentPath,MaximumRootBytes));
    if(!SameIdentity(intent.identity,state.checkpoint.identity))throw std::runtime_error("Publication identity is incompatible with checkpoint");
    const auto output=fs::u8path(intent.output);
    if(fs::exists(output)) {
        VerifyPublicationOwner(output,intent,control);
        const auto bytes=SceneTreeBytes(output,control,false);
        state.externalOutputBytes=Within(state.root,fs::canonical(output))?0:bytes;
    } else if(state.checkpoint.published)throw std::runtime_error("Completed scene output is missing");
}
}

bool FSplatBuildJournal::PublishScene(const std::string& relative,const std::string& outputDirectory,
    const SceneValidator& validate,FSplatBuildControl& control,std::string& error) {
    try {
        if(!impl->state||!validate)throw std::runtime_error("Journal/scene validator is unavailable");auto& state=*impl->state;Ready(state);Poll(control);
        const auto first=relative.substr(0,relative.find('/'));
        if(first=="chunks"||!RelativeValid(relative))throw std::runtime_error("Invalid scene staging directory");
        const auto staging=SafePath(state.root,relative);
        const auto requested=fs::absolute(fs::u8path(outputDirectory)).lexically_normal();
        if(outputDirectory.empty()||requested.filename().empty())throw std::runtime_error("Invalid scene output directory");
        const auto target=fs::canonical(requested.parent_path())/requested.filename();
        if(Within(staging,target)||Within(target,state.root))throw std::runtime_error("Scene output overlaps journal/staging ancestry");
        if(Within(staging,state.source))throw std::runtime_error("Scene publication would move the retained stable source");
        const auto intentPath=SafePath(state.root,"publication.intent");FPublicationIntent intent;
        if(fs::exists(intentPath)) {
            intent=DecodeIntent(ReadSmall(intentPath,MaximumRootBytes));
            if(!SameIdentity(intent.identity,state.checkpoint.identity)||intent.staging!=relative||intent.output!=UTF8(target))
                throw std::runtime_error("Existing publication intent targets a different scene");
        } else {
            if(fs::exists(target))throw std::runtime_error("Existing output is never overwritten");
            if(!fs::is_directory(staging))throw std::runtime_error("Scene staging directory is missing");
            const auto manifest=SafePath(state.root,relative+"/scene.mgs");
            if(!fs::is_regular_file(manifest)||fs::file_size(manifest)>state.config.maxManifestBytes)
                throw std::runtime_error("Staged scene manifest exceeds admission");
            if(!validate(UTF8(staging),error))throw std::runtime_error(error.empty()?"Staged scene validation failed":error);
            intent.identity=state.checkpoint.identity;intent.staging=relative;intent.output=UTF8(target);intent.generation=state.checkpoint.generation;
            intent.manifestHash=Digest(HashPrefix(manifest,fs::file_size(manifest),control));
            FEncoder owner;Identity(owner,intent.identity);owner.Text(UTF8(state.root));owner.Text(relative);owner.Text(intent.output);owner.Text(intent.manifestHash);owner.U64(intent.generation);
            intent.token=FSplatSceneFormat::SHA256(owner.bytes);
            const auto marker=SafePath(state.root,relative+"/.monster-publication");
            const std::vector<uint8_t> token(intent.token.begin(),intent.token.end());
            if(fs::exists(marker)) {
                if(ReadSmall(marker,64)!=token)throw std::runtime_error("Staging directory has another publication marker");
            } else AtomicBytes(state,marker,token,control,false);
            (void)SceneTreeBytes(staging,control,true);
            AtomicBytes(state,intentPath,EncodeIntent(intent),control,false);
        }
        if(!fs::exists(target)) {
            if(state.checkpoint.published)throw std::runtime_error("Completed scene output disappeared");
            VerifyPublicationOwner(staging,intent,control);
            if(!validate(UTF8(staging),error))throw std::runtime_error(error.empty()?"Staged scene validation failed":error);
            (void)SceneTreeBytes(staging,control,true);
            ScanDisk(state,control);CheckDisk(state,2*MaximumRootBytes,control);
            auto publishing=state.checkpoint;publishing.stage=ESplatBuildStage::Publishing;publishing.resumeStage=ESplatBuildStage::Publishing;
            CommitRoot(state,std::move(publishing),control);
            Poll(control);AtomicRename(staging,target,false);
        }
        VerifyPublicationOwner(target,intent,control);
        if(!validate(UTF8(target),error))throw std::runtime_error(error.empty()?"Published scene validation failed":error);
        state.externalOutputBytes=Within(state.root,target)?0:SceneTreeBytes(target,control,false);
        ScanDisk(state,control);CheckDisk(state,2*MaximumRootBytes,control);
        if(!state.checkpoint.published) {
            auto completed=state.checkpoint;completed.stage=ESplatBuildStage::Completed;completed.resumeStage=ESplatBuildStage::Completed;completed.published=true;
            CommitRoot(state,std::move(completed),control);
        }
        error.clear();return true;
    }catch(const std::exception& exception){if(impl->state)impl->state->poisoned=true;error=exception.what();return false;}
}

} // namespace MonsterRender::Splat
