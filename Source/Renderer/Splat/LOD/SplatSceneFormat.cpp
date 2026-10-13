// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
constexpr uint64_t MaxManifestBytes = 256ull << 20;
constexpr uint64_t MaxPageBytes = 256ull << 20;
constexpr uint32_t MaxEntries = 2000000;
constexpr uint32_t MaxString = 65536;
constexpr uint32_t PageHeaderBytes = 24;
constexpr uint32_t DiskRecordBytes = 252;
struct Writer {
    std::vector<uint8_t> bytes;
    void U32(uint32_t x) { for (int i=0;i<4;++i) bytes.push_back(uint8_t(x>>(8*i))); }
    void U64(uint64_t x) { for (int i=0;i<8;++i) bytes.push_back(uint8_t(x>>(8*i))); }
    void F32(float x) { uint32_t u; std::memcpy(&u,&x,4); U32(u); }
    void F64(double x) { uint64_t u; std::memcpy(&u,&x,8); U64(u); }
    void Text(const std::string& s) { if(s.size()>MaxString) throw std::runtime_error("String limit exceeded"); U32(uint32_t(s.size())); bytes.insert(bytes.end(),s.begin(),s.end()); }
    void IDs(const std::vector<uint32_t>& v) { U32(uint32_t(v.size())); for(auto x:v) U32(x); }
};
struct Reader {
    const std::vector<uint8_t>& bytes; size_t pos=0;
    void Need(uint64_t n) { if(n>bytes.size()-pos) throw std::runtime_error("Truncated resource"); }
    uint32_t U32() { Need(4); uint32_t x=0; for(int i=0;i<4;++i)x|=uint32_t(bytes[pos++])<<(8*i); return x; }
    uint64_t U64() { Need(8); uint64_t x=0; for(int i=0;i<8;++i)x|=uint64_t(bytes[pos++])<<(8*i); return x; }
    float F32() { auto u=U32(); float x; std::memcpy(&x,&u,4); return x; }
    double F64() { auto u=U64(); double x; std::memcpy(&x,&u,8); return x; }
    std::string Text() { auto n=U32(); if(n>MaxString)throw std::runtime_error("String limit exceeded"); Need(n); std::string s(bytes.begin()+pos,bytes.begin()+pos+n); pos+=n; return s; }
    std::vector<uint32_t> IDs(uint32_t limit) { auto n=U32(); if(n>limit)throw std::runtime_error("Array limit exceeded"); Need(uint64_t(n)*4); std::vector<uint32_t> v(n);for(auto& x:v)x=U32();return v; }
};
bool HashValid(const std::string& h) { return h.size()==64 && h.find_first_not_of("0123456789abcdef")==std::string::npos; }
bool PathValid(const std::string& s) {
    if(s.empty() || s.find('\0')!=std::string::npos || s.find(':')!=std::string::npos || s.front()=='/' || s.front()=='\\')return false;
    std::string part; for(char c:s) { if(c=='/'||c=='\\') { if(part.empty()||part==".."||part==".")return false;part.clear(); }else part+=c; }
    return !part.empty()&&part!=".."&&part!=".";
}
bool GaussianValid(const FSplatGaussian& g,uint32_t degree) {
    for(float f:g.position)if(!std::isfinite(f))return false;
    for(float f:g.scale)if(!std::isfinite(f))return false;
    double q=0;for(float f:g.rotation){if(!std::isfinite(f))return false;q+=double(f)*f;}
    if(g.position[3]!=1 || g.scale[3]!=0 || g.scale[0]<=0||g.scale[1]<=0||g.scale[2]<=0 || std::abs(q-1)>0.001 || !std::isfinite(g.opacity)||g.opacity<0||g.opacity>1)return false;
    for(uint32_t i=0;i<48;++i)if(!std::isfinite(g.sh[i]) || (i>=3*(degree+1)*(degree+1)&&g.sh[i]!=0))return false;
    return true;
}
bool CoverageValid(const FSplatLODNode& n,uint32_t version) {
    return std::isfinite(n.coverageAreaUpper)&&n.coverageAreaUpper>=0&&(version!=1||n.coverageAreaUpper==0);
}
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
}
std::string FSplatSceneFormat::SHA256(const std::vector<uint8_t>& bytes){HashState s;s.Add(bytes.data(),bytes.size());return s.Finish();}
bool FSplatSceneFormat::SHA256File(const std::string& path,std::string& hash,std::string& error){
    try{std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot open source for hashing");HashState s;std::array<uint8_t,65536>b;while(f){f.read(reinterpret_cast<char*>(b.data()),b.size());s.Add(b.data(),size_t(f.gcount()));}if(!f.eof())throw std::runtime_error("Source hash read failed");hash=s.Finish();error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}
}
bool FSplatSceneFormat::ValidateManifest(const FSplatSceneManifest& m,std::string& error){
    auto fail=[&](const char* s){error=s;return false;};
    if(!std::isfinite(m.supportSigma)||m.supportSigma<3)return fail("Scene supportSigma must be at least three for the renderer");
    if((m.version!=1&&m.version!=2&&m.version!=3&&m.version!=4&&m.version!=5)||m.shDegree>3||m.endian!="little"||m.coordinates!="source-world"||m.quaternion!="wxyz"||m.shLayout!="coefficient-major-rgb")return fail("Unsupported scene convention/version");
    if((m.version==2)!=(m.builderVersion==SplatRMSErrorBuilderVersion)||(m.version==3)!=(m.builderVersion==SplatCenterErrorBuilderVersion)||(m.version==4)!=(m.builderVersion==SplatFusionBuilderVersion)||(m.version==5)!=(m.builderVersion==SplatLeafFusionBuilderVersion))return fail("Unsupported selection metadata policy/version");
    if(!HashValid(m.sourceSHA256)||!HashValid(m.builderHash)||!HashValid(m.configHash)||!std::isfinite(m.supportSigma)||m.supportSigma<=0)return fail("Invalid scene identity or support");
    if(m.nodes.size()>MaxEntries||m.pages.size()>MaxEntries)return fail("Scene index limit exceeded");
    if(m.nodes.empty()){if(m.rootNode!=SplatInvalidId||!m.pages.empty()||m.sourceCount||m.leafCount||m.proxyCount||m.totalDataBytes)return fail("Invalid empty scene");error.clear();return true;}
    if(m.rootNode>=m.nodes.size()||m.nodes[m.rootNode].level!=0)return fail("Invalid root");
    uint64_t bytes=0;std::vector<uint32_t> owners(m.pages.size(),0),parents(m.nodes.size(),0);
    for(size_t i=0;i<m.pages.size();++i){const auto& p=m.pages[i];if(p.id!=i||!PathValid(p.relativePath)||!HashValid(p.sha256)||p.gaussianCount==0||p.byteSize>MaxPageBytes||p.byteSize!=PageHeaderBytes+uint64_t(p.gaussianCount)*DiskRecordBytes||p.decodedByteSize!=uint64_t(p.gaussianCount)*sizeof(FSplatGaussian)||p.offset>UINT64_MAX-p.byteSize||bytes>UINT64_MAX-p.byteSize)return fail("Invalid page metadata");bytes+=p.byteSize;}
    uint64_t leaves=0,proxies=0;
    for(const auto& n:m.nodes){if(!CoverageValid(n,m.version))return fail("Invalid emitting alpha-area statistic");if(m.version>=3)for(int a=0;a<3;++a){if(!std::isfinite(n.centerBounds.min[a])||!std::isfinite(n.centerBounds.max[a])||n.centerBounds.min[a]>n.centerBounds.max[a]||n.centerBounds.min[a]<n.bounds.min[a]||n.centerBounds.max[a]>n.bounds.max[a])return fail("Invalid center projection bounds");for(auto c:n.children)if(c<m.nodes.size()&&(m.nodes[c].centerBounds.min[a]<n.centerBounds.min[a]||m.nodes[c].centerBounds.max[a]>n.centerBounds.max[a]))return fail("Child centers outside parent");}}
    for(size_t i=0;i<m.nodes.size();++i){const auto& n=m.nodes[i];if(n.level>64||n.children.size()>8||!std::isfinite(n.geometricError)||n.geometricError<0||!std::isfinite(n.appearanceError)||n.appearanceError<0)return fail("Invalid node error/depth");for(int a=0;a<3;++a)if(!std::isfinite(n.bounds.min[a])||!std::isfinite(n.bounds.max[a])||n.bounds.min[a]>n.bounds.max[a])return fail("Invalid bounds");uint64_t count=0;for(auto p:n.pageIds){if(p>=m.pages.size()||++owners[p]!=1)return fail("Page reused or missing");count+=m.pages[p].gaussianCount;}if(count!=n.gaussianCount||count==0)return fail("Representation count mismatch");for(auto c:n.children){if(c>=m.nodes.size()||c==i||++parents[c]!=1||m.nodes[c].level!=n.level+1)return fail("Invalid tree edge");for(int a=0;a<3;++a)if(m.nodes[c].bounds.min[a]<n.bounds.min[a]||m.nodes[c].bounds.max[a]>n.bounds.max[a])return fail("Child support outside parent");}if(n.children.empty())leaves+=count;else proxies+=count;}
    for(size_t i=0;i<parents.size();++i)if(parents[i]!=(i==m.rootNode?0u:1u))return fail("Unreachable node");
    for(auto x:owners)if(x!=1)return fail("Unowned page");
    for(const auto& node:m.nodes) {
        if(m.version<4&&!node.originalFusionGroups.empty())return fail("Original membership requires manifest v4");
        if(node.originalFusionGroups.empty())continue;
        uint64_t rows=0;
        if(node.children.empty()){
            if(m.version<5)return fail("Leaf self-page membership requires manifest v5");
            rows=node.gaussianCount;
        }else for(auto child:node.children){if(!m.nodes[child].children.empty())return fail("Fusion membership references a proxy");rows+=m.nodes[child].gaussianCount;}
        uint64_t memberCount=0;for(const auto& g:node.originalFusionGroups){if(!g.count||g.count>4)return fail("Invalid fusion group size");memberCount+=g.count;}
        if(memberCount!=rows||rows>UINT32_MAX)return fail("Incomplete original fusion membership");
        std::vector<bool> seen(size_t(rows),false);
        for(const auto& g:node.originalFusionGroups)for(uint32_t i=0;i<g.count;++i){if(g.rows[i]>=rows||seen[g.rows[i]])return fail("Repeated or invalid original fusion member");seen[g.rows[i]]=true;}
    }
    if(bytes!=m.totalDataBytes||leaves!=m.leafCount||leaves!=m.sourceCount||proxies!=m.proxyCount)return fail("Scene statistics mismatch");
    for(auto p:m.nodes[m.rootNode].pageIds)if(!m.pages[p].pinned)return fail("Root fallback must be pinned");error.clear();return true;
}
bool FSplatSceneFormat::SaveManifest(const std::string& path,const FSplatSceneManifest& m,std::string& error){
    if(!ValidateManifest(m,error))return false;
    try{Writer w;w.U32(0x4d53474d);w.U32(m.version);w.U32(m.shDegree);w.Text(m.sourceSHA256);w.Text(m.builderVersion);w.Text(m.builderHash);w.Text(m.configHash);w.Text(m.buildConfig);w.Text(m.endian);w.Text(m.coordinates);w.Text(m.quaternion);w.Text(m.shLayout);w.F32(m.supportSigma);w.U32(m.rootNode);w.U64(m.sourceCount);w.U64(m.leafCount);w.U64(m.proxyCount);w.U64(m.totalDataBytes);w.U32(uint32_t(m.nodes.size()));w.U32(uint32_t(m.pages.size()));for(const auto& n:m.nodes){for(float f:n.bounds.min)w.F32(f);for(float f:n.bounds.max)w.F32(f);w.IDs(n.children);w.IDs(n.pageIds);w.U32(n.gaussianCount);w.F32(n.geometricError);w.F32(n.appearanceError);w.U32(n.level);if(m.version>=2)w.F64(n.coverageAreaUpper);if(m.version>=3){for(float x:n.centerBounds.min)w.F32(x);for(float x:n.centerBounds.max)w.F32(x);}if(m.version>=4){w.U32(uint32_t(n.originalFusionGroups.size()));for(const auto& g:n.originalFusionGroups){w.U32(g.count);for(uint32_t i=0;i<g.count;++i)w.U32(g.rows[i]);}}}for(const auto& p:m.pages){w.U32(p.id);w.Text(p.relativePath);w.U64(p.offset);w.U64(p.byteSize);w.U64(p.decodedByteSize);w.U32(p.gaussianCount);w.Text(p.sha256);w.U32(p.pinned?1:0);}if(w.bytes.size()>MaxManifestBytes)throw std::runtime_error("Manifest size limit exceeded");std::string hash=SHA256(w.bytes);w.bytes.insert(w.bytes.end(),hash.begin(),hash.end());std::ofstream f(path,std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("Cannot create manifest");f.write(reinterpret_cast<const char*>(w.bytes.data()),w.bytes.size());f.close();if(!f)throw std::runtime_error("Manifest write failed");error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}
}
bool FSplatSceneFormat::LoadManifest(const std::string& path,FSplatSceneManifest& out,std::string& error){
    try{std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("Cannot open manifest");auto size=f.tellg();if(size<64||uint64_t(size)>MaxManifestBytes+64)throw std::runtime_error("Invalid manifest size");std::vector<uint8_t>b(static_cast<size_t>(size));f.seekg(0);if(!f.read(reinterpret_cast<char*>(b.data()),b.size()))throw std::runtime_error("Manifest read failed");std::string expected(b.end()-64,b.end());b.resize(b.size()-64);if(!HashValid(expected)||SHA256(b)!=expected)throw std::runtime_error("Manifest checksum mismatch");Reader r{b};if(r.U32()!=0x4d53474d)throw std::runtime_error("Invalid manifest magic");FSplatSceneManifest m;m.version=r.U32();if(m.version!=1&&m.version!=2&&m.version!=3&&m.version!=4&&m.version!=5)throw std::runtime_error("Unsupported manifest version");m.shDegree=r.U32();m.sourceSHA256=r.Text();m.builderVersion=r.Text();m.builderHash=r.Text();m.configHash=r.Text();m.buildConfig=r.Text();m.endian=r.Text();m.coordinates=r.Text();m.quaternion=r.Text();m.shLayout=r.Text();m.supportSigma=r.F32();m.rootNode=r.U32();m.sourceCount=r.U64();m.leafCount=r.U64();m.proxyCount=r.U64();m.totalDataBytes=r.U64();auto nodes=r.U32(),pages=r.U32();if(nodes>MaxEntries||pages>MaxEntries)throw std::runtime_error("Index count limit exceeded");r.Need(uint64_t(nodes)*(m.version>=3?80:m.version==2?56:48)+uint64_t(pages)*104);m.nodes.resize(nodes);m.pages.resize(pages);for(auto& n:m.nodes){for(auto& x:n.bounds.min)x=r.F32();for(auto& x:n.bounds.max)x=r.F32();n.children=r.IDs(8);n.pageIds=r.IDs(pages);n.gaussianCount=r.U32();n.geometricError=r.F32();n.appearanceError=r.F32();n.level=r.U32();if(m.version>=2)n.coverageAreaUpper=r.F64();if(m.version>=3){for(float& x:n.centerBounds.min)x=r.F32();for(float& x:n.centerBounds.max)x=r.F32();}if(m.version>=4){const auto groupCount=r.U32();if(groupCount>MaxEntries)throw std::runtime_error("Fusion group count limit exceeded");r.Need(uint64_t(groupCount)*8);n.originalFusionGroups.resize(groupCount);for(auto& g:n.originalFusionGroups){g.count=r.U32();if(g.count<1||g.count>4)throw std::runtime_error("Invalid original fusion group");for(uint32_t i=0;i<g.count;++i)g.rows[i]=r.U32();}}}for(auto& p:m.pages){p.id=r.U32();p.relativePath=r.Text();p.offset=r.U64();p.byteSize=r.U64();p.decodedByteSize=r.U64();p.gaussianCount=r.U32();p.sha256=r.Text();auto pinned=r.U32();if(pinned>1)throw std::runtime_error("Invalid pinned flag");p.pinned=pinned!=0;}if(r.pos!=b.size())throw std::runtime_error("Unexpected manifest trailing data");if(!ValidateManifest(m,error))return false;out=std::move(m);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}
}
bool FSplatSceneFormat::EncodePage(const std::vector<FSplatGaussian>& gs,uint32_t degree,std::vector<uint8_t>& out,std::string& error){
    try{if(degree>3||gs.empty()||gs.size()>(MaxPageBytes-PageHeaderBytes)/DiskRecordBytes)throw std::runtime_error("Invalid page degree/count");Writer w;w.bytes.reserve(PageHeaderBytes+gs.size()*DiskRecordBytes);w.U32(0x5053474d);w.U32(1);w.U32(degree);w.U32(uint32_t(gs.size()));w.U32(DiskRecordBytes);w.U32(0);for(const auto& g:gs){if(!GaussianValid(g,degree))throw std::runtime_error("Invalid activated Gaussian");for(float x:g.position)w.F32(x);for(float x:g.scale)w.F32(x);for(float x:g.rotation)w.F32(x);w.F32(g.opacity);for(float x:g.sh)w.F32(x);w.U64(g.originalId);}out=std::move(w.bytes);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}
}
bool FSplatSceneFormat::DecodePage(const FSplatSceneManifest& m,uint32_t id,const std::vector<uint8_t>& b,std::vector<FSplatGaussian>& out,std::string& error){
    try{if(id>=m.pages.size()||m.shDegree>3)throw std::runtime_error("Invalid page ID/degree");const auto& p=m.pages[id];if(b.size()>MaxPageBytes||b.size()!=p.byteSize||!HashValid(p.sha256)||SHA256(b)!=p.sha256)throw std::runtime_error("Page checksum/size mismatch");Reader r{b};if(r.U32()!=0x5053474d||r.U32()!=1||r.U32()!=m.shDegree)throw std::runtime_error("Unsupported page header");auto count=r.U32();if(count!=p.gaussianCount||r.U32()!=DiskRecordBytes||r.U32()!=0||uint64_t(count)*DiskRecordBytes+PageHeaderBytes!=b.size()||uint64_t(count)*sizeof(FSplatGaussian)!=p.decodedByteSize)throw std::runtime_error("Invalid page layout");std::vector<FSplatGaussian> gs(count);for(auto& g:gs){for(auto& x:g.position)x=r.F32();for(auto& x:g.scale)x=r.F32();for(auto& x:g.rotation)x=r.F32();g.opacity=r.F32();for(auto& x:g.sh)x=r.F32();g.originalId=r.U64();if(!GaussianValid(g,m.shDegree))throw std::runtime_error("Invalid page Gaussian");}out=std::move(gs);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}
}
} // namespace MonsterRender::Splat
