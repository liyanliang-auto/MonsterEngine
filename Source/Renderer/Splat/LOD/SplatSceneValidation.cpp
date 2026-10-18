// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatSceneValidation.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include "Renderer/Splat/SplatPLYStream.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
namespace fs = std::filesystem;
void Require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
uint64_t Add(uint64_t a,uint64_t b){Require(a<=UINT64_MAX-b,"Validation size overflow");return a+b;}
uint64_t Mul(uint64_t a,uint64_t b){Require(!b||a<=UINT64_MAX/b,"Validation size overflow");return a*b;}
void Poll(const FSplatBuildControl* control){if(control){Require(control->Poll()!=ESplatBuildSignal::Cancel,"Cancelled");Require(control->Poll()!=ESplatBuildSignal::Pause,"Paused");}}
void NoLinks(const fs::path& path){auto current=fs::absolute(path).root_path();for(const auto& component:fs::absolute(path).relative_path()){Require(component!="..","Parent path component forbidden");current/=component;Require(!fs::is_symlink(fs::symlink_status(current)),"Scene symbolic links forbidden");}}
struct Wire {
    std::ifstream file;uint64_t offset=0,bytes=0;const FSplatBuildControl* control;
    Wire(const fs::path& path,uint64_t size,const FSplatBuildControl* c):file(path,std::ios::binary),bytes(size-64),control(c){Require(bool(file),"Cannot read manifest preflight");}
    uint32_t U32(){Poll(control);Require(offset<=bytes&&bytes-offset>=4,"Truncated manifest preflight");uint8_t b[4];Require(bool(file.read(reinterpret_cast<char*>(b),4)),"Manifest preflight read failed");offset+=4;return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);}
    void Skip(uint64_t n){Poll(control);Require(offset<=bytes&&n<=bytes-offset,"Manifest preflight range exceeds payload");offset+=n;file.seekg(static_cast<std::streamoff>(offset));Require(bool(file),"Manifest preflight seek failed");}
    void Text(){const auto n=U32();Require(n<=65536,"Manifest string limit");Skip(n);}
};
uint64_t Preflight(const fs::path& manifest,uint64_t bytes,const FSplatSceneValidationLimits& limits,const FSplatBuildControl* control){
    Require(bytes>=64&&bytes<=limits.maxManifestBytes,"Manifest byte admission exceeded");Wire wire(manifest,bytes,control);Require(wire.U32()==0x4d53474d,"Invalid manifest magic");const auto version=wire.U32();Require(version>=1&&version<=5,"Unsupported manifest version");wire.U32();for(int i=0;i<9;++i)wire.Text();wire.Skip(4+4+8*4);const auto nodes=wire.U32(),pages=wire.U32();Require(nodes<=limits.maxNodes&&pages<=limits.maxPages,"Manifest index admission exceeded");
    const auto working=Add(Mul(bytes,4),Add(Mul(nodes,sizeof(FSplatLODNode)),Mul(pages,sizeof(FSplatPageInfo))));Require(working<=limits.maxWorkingBytes,"Manifest decoded workspace admission exceeded");
    for(uint32_t i=0;i<nodes;++i){wire.Skip(24);const auto children=wire.U32();Require(children<=8,"Manifest child count invalid");wire.Skip(Mul(children,4));const auto ids=wire.U32();Require(ids<=pages,"Manifest page references invalid");wire.Skip(Mul(ids,4));wire.Skip(16);if(version>=2)wire.Skip(8);if(version>=3)wire.Skip(24);if(version>=4){const auto groups=wire.U32();Require(groups<=1000000,"Manifest group limit");for(uint32_t group=0;group<groups;++group){const auto n=wire.U32();Require(n>=1&&n<=4,"Invalid fusion group");wire.Skip(Mul(n,4));}}}
    for(uint32_t i=0;i<pages;++i){wire.Skip(4);wire.Text();wire.Skip(8*3+4);wire.Text();wire.Skip(4);}Require(wire.offset==wire.bytes,"Unexpected manifest payload bytes");return working;
}
fs::path PagePath(const fs::path& root,const std::string& name){Require(name!="descriptor.json"&&name!="delete-request.json"&&name!="package.json","Reserved scene-store page path");Require(!name.empty()&&name.find('\\')==std::string::npos&&name.find(':')==std::string::npos&&name.find('\0')==std::string::npos,"Invalid portable page path");const fs::path relative(name);Require(!relative.is_absolute(),"Absolute page path forbidden");for(const auto& part:relative)Require(part!=".."&&part!="."&&!part.empty(),"Invalid page path component");const auto full=root/relative;NoLinks(full);Require(fs::is_regular_file(full),"Page is not a regular file");return full;}
} // namespace

bool FSplatSceneValidation::Validate(const std::string& input,const FSplatSceneValidationLimits& limits,FSplatSceneValidationResult& output,std::string& error,const FSplatBuildControl* control){
    try{
        Poll(control);Require(limits.maxWorkingBytes>0,"Invalid validation workspace");fs::path path(input);NoLinks(path);FSplatSceneValidationResult result;
        if(path.extension()==".ply"){
            Require(fs::is_regular_file(path)&&fs::file_size(path)<=limits.maxPLYBytes,"Small PLY publication size exceeded; build LOD instead");
            FSplatPLYLimits parserLimits;parserLimits.maxFileBytes=limits.maxPLYBytes;parserLimits.maxRecords=limits.maxPLYRecords;FSplatPLYHeader header;
            if(!FSplatPLYStream::Probe(path.string(),header,error,parserLimits,control))return false;
            Require(FSplatPLYStream::ParserWorkingBytes(header,256)<=limits.maxWorkingBytes,"PLY validation workspace admission exceeded");
            FSplatBuildControl local;auto& operation=control?const_cast<FSplatBuildControl&>(*control):local;
            if(!FSplatPLYStream::StreamRecords(path.string(),header,256,operation,[](std::span<const FSplatGaussian>,const FSplatBuildSourceCursor&,std::string&){return true;},error))return false;
            result.sourceRecords=header.recordCount;result.shDegree=header.shDegree;
            if(!FSplatSceneFormat::SHA256File(path.string(),result.contentSHA256,error))return false;
        }else{
            if(fs::is_directory(path))path/="scene.mgs";NoLinks(path);Require(fs::is_regular_file(path),"Missing scene manifest");const auto working=Preflight(path,fs::file_size(path),limits,control);FSplatSceneManifest manifest;
            if(!FSplatSceneFormat::LoadManifest(path.string(),manifest,error))return false;
            for(const auto& page:manifest.pages){
                Poll(control);Require(page.byteSize<=limits.maxPageBytes&&page.decodedByteSize<=limits.maxDecodedPageBytes,"Page validation admission exceeded");Require(Add(working,Add(page.byteSize,Mul(page.gaussianCount,sizeof(FSplatGaussian))))<=limits.maxWorkingBytes,"Page decoded workspace admission exceeded");
                const auto full=PagePath(path.parent_path(),page.relativePath);const auto fileBytes=fs::file_size(full);Require(page.offset<=fileBytes&&page.byteSize<=fileBytes-page.offset,"Page reference exceeds file");
                result.encodedPageBytes=Add(result.encodedPageBytes,page.byteSize);Require(result.encodedPageBytes<=limits.maxTotalPageBytes,"Scene data admission exceeded");std::ifstream file(full,std::ios::binary);Require(bool(file),"Cannot open page");file.seekg(static_cast<std::streamoff>(page.offset));Require(bool(file),"Cannot seek page");std::vector<uint8_t> bytes(static_cast<size_t>(page.byteSize));size_t done=0;while(done<bytes.size()){Poll(control);const auto n=std::min<size_t>(65536,bytes.size()-done);Require(bool(file.read(reinterpret_cast<char*>(bytes.data()+done),n)),"Truncated page read");done+=n;}
                std::vector<FSplatGaussian> decoded;if(!FSplatSceneFormat::DecodePage(manifest,page.id,bytes,decoded,error))return false;++result.pageCount;
            }
            result.sourceRecords=manifest.sourceCount;result.shDegree=manifest.shDegree;if(!FSplatSceneFormat::SHA256File(path.string(),result.contentSHA256,error))return false;
        }
        Poll(control);output=std::move(result);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
} // namespace MonsterRender::Splat
