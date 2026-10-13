// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/SplatPLYLoader.h"
#include "Core/Logging/LogMacros.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#ifndef SPLAT_CPU_ONLY
#include "RHI/IRHICommandList.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#endif

namespace MonsterRender::Splat {
namespace {
struct PLYProperty { std::string name; uint32_t size; bool floating; bool signedValue; };
PLYProperty Property(const std::string& type,const std::string& name) {
    if(type=="float"||type=="float32")return {name,4,true,true};
    if(type=="double"||type=="float64")return {name,8,true,true};
    if(type=="char"||type=="int8")return {name,1,false,true};
    if(type=="uchar"||type=="uint8")return {name,1,false,false};
    if(type=="short"||type=="int16")return {name,2,false,true};
    if(type=="ushort"||type=="uint16")return {name,2,false,false};
    if(type=="int"||type=="int32")return {name,4,false,true};
    if(type=="uint"||type=="uint32")return {name,4,false,false};
    throw std::runtime_error("Unsupported PLY scalar type: "+type);
}
double Scalar(std::istream& f,const PLYProperty& p,bool ascii,bool bigEndian) {
    double x=0;
    if(ascii){std::string token;if(!(f>>token)||token.size()>128)throw std::runtime_error("Truncated ASCII PLY");size_t used=0;x=std::stod(token,&used);if(used!=token.size())throw std::runtime_error("Invalid ASCII scalar");if(!p.floating){const double lo=p.signedValue?-std::ldexp(1.0,p.size*8-1):0;const double hi=p.signedValue?std::ldexp(1.0,p.size*8-1)-1:std::ldexp(1.0,p.size*8)-1;if(std::floor(x)!=x||x<lo||x>hi)throw std::runtime_error("ASCII integer out of range");}}
    else{uint8_t bytes[8]{};if(!f.read(reinterpret_cast<char*>(bytes),p.size))throw std::runtime_error("Truncated binary PLY");uint64_t bits=0;for(uint32_t i=0;i<p.size;++i)bits|=uint64_t(bytes[i])<<(8*(bigEndian?p.size-1-i:i));if(p.floating){if(p.size==4){uint32_t u=uint32_t(bits);float v;std::memcpy(&v,&u,4);x=v;}else std::memcpy(&x,&bits,8);}else if(p.signedValue){int64_t v=int64_t(bits);if(p.size<8&&(bits&(uint64_t(1)<<(p.size*8-1))))v-=int64_t(uint64_t(1)<<(p.size*8));x=double(v);}else x=double(bits);}
    if(!std::isfinite(x)||std::abs(x)>std::numeric_limits<float>::max())throw std::runtime_error("Nonfinite/out-of-range PLY attribute");return x;
}
}
bool FSplatPLYLoader::LoadCPU(const std::string& path,std::vector<FSplatGaussian>& out,uint32_t& shDegree,std::string& error) {
    return LoadCPUWithAdmission(path,out,shDegree,error,{});
}
bool FSplatPLYLoader::LoadCPUWithAdmission(const std::string& path,std::vector<FSplatGaussian>& out,uint32_t& shDegree,std::string& error,
    const std::function<bool(uint64_t,uint32_t,std::string&)>& admission) {
    try {
        std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot open PLY: "+path);
        std::string line;auto readLine=[&](){line.clear();char c;while(f.get(c)){if(c=='\n')break;if(line.size()>=65536)throw std::runtime_error("PLY header line limit");line+=c;}if(!f&&line.empty())throw std::runtime_error("Truncated PLY header");if(!line.empty()&&line.back()=='\r')line.pop_back();};
        readLine();if(line!="ply")throw std::runtime_error("Missing PLY magic");
        bool format=false,ascii=false,big=false,vertex=false,inVertex=false,ended=false;uint64_t count=0,headerBytes=4;std::vector<PLYProperty> properties;std::map<std::string,size_t> index;
        while(!ended){readLine();headerBytes+=line.size()+1;if(headerBytes>(1u<<20))throw std::runtime_error("PLY header size limit");std::istringstream s(line);std::string t;s>>t;if(t=="comment"||t=="obj_info"||t.empty())continue;
            if(t=="format"){std::string v,version,extra;s>>v>>version;if(format||version!="1.0"||(s>>extra))throw std::runtime_error("Invalid PLY format declaration");format=true;if(v=="ascii")ascii=true;else if(v=="binary_big_endian")big=true;else if(v!="binary_little_endian")throw std::runtime_error("Unsupported PLY format");}
            else if(t=="element"){std::string name,n,extra;s>>name>>n;if(name.empty()||n.empty()||(s>>extra)||n.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid PLY element");auto amount=std::stoull(n);inVertex=name=="vertex";if(inVertex){if(vertex)throw std::runtime_error("Duplicate vertex element");vertex=true;count=amount;if(count>100000000)throw std::runtime_error("PLY vertex limit exceeded");}else if(!vertex&&amount)throw std::runtime_error("Vertex must be first nonempty PLY element");}
            else if(t=="property"&&inVertex){std::string type,name,extra;s>>type>>name;if(name.empty()||type=="list"||(s>>extra)||properties.size()>=1024)throw std::runtime_error("Invalid vertex property");if(!index.emplace(name,properties.size()).second)throw std::runtime_error("Duplicate PLY property");properties.push_back(Property(type,name));}
            else if(t=="property")continue;
            else if(t=="end_header"){std::string extra;if(s>>extra)throw std::runtime_error("Malformed end_header");ended=true;}
            else throw std::runtime_error("Unknown PLY header declaration");
        }
        if(!format||!vertex)throw std::runtime_error("Missing PLY format/vertex");
        const char* required[]={"x","y","z","f_dc_0","f_dc_1","f_dc_2","opacity","scale_0","scale_1","scale_2","rot_0","rot_1","rot_2","rot_3"};for(auto name:required)if(!index.count(name))throw std::runtime_error(std::string("Missing Gaussian property: ")+name);
        uint32_t rest=0;for(const auto& p:properties)if(p.name.rfind("f_rest_",0)==0){auto n=p.name.substr(7);if(n.empty()||n.find_first_not_of("0123456789")!=std::string::npos||n.size()>2)throw std::runtime_error("Invalid SH property");auto i=std::stoul(n);if(i>=45||n!=std::to_string(i))throw std::runtime_error("Invalid SH index");++rest;}
        uint32_t degree=rest==0?0:rest==9?1:rest==24?2:rest==45?3:4;if(degree>3)throw std::runtime_error("Incomplete SH degree layout");for(uint32_t i=0;i<rest;++i)if(!index.count("f_rest_"+std::to_string(i)))throw std::runtime_error("Noncontiguous SH properties");
        uint64_t stride=0;for(const auto& p:properties)stride+=p.size;
        auto begin=f.tellg();f.seekg(0,std::ios::end);auto end=f.tellg();if(begin<0||end<begin)throw std::runtime_error("Cannot query PLY data range");f.seekg(begin);if(!ascii&&count*stride>uint64_t(end-begin))throw std::runtime_error("Truncated PLY vertex payload");if(ascii&&count&&count>uint64_t(end-begin)/(properties.size()*2-1))throw std::runtime_error("Truncated ASCII payload");
        // Admission uses only validated header metadata: no vertex payload read,
        // CPU expansion, GPU memory allocation or initial upload has happened.
        if(admission&&!admission(count,degree,error))throw std::runtime_error(error);
        std::vector<FSplatGaussian> gs;gs.reserve(static_cast<size_t>(count));std::vector<double> row(properties.size());uint32_t coeff=(degree+1)*(degree+1),per=coeff-1;
        for(uint64_t i=0;i<count;++i){for(size_t p=0;p<row.size();++p)row[p]=Scalar(f,properties[p],ascii,big);auto value=[&](const std::string& n){return row[index.at(n)];};FSplatGaussian g;g.originalId=i;g.position[0]=float(value("x"));g.position[1]=float(value("y"));g.position[2]=float(value("z"));for(int a=0;a<3;++a){g.scale[a]=float(std::exp(value("scale_"+std::to_string(a))));if(!std::isfinite(g.scale[a])||g.scale[a]<=0)throw std::runtime_error("Invalid activated Gaussian scale");}
            double q[4],norm=0;for(int a=0;a<4;++a){q[a]=value("rot_"+std::to_string(a));norm+=q[a]*q[a];}if(norm<=1e-30)throw std::runtime_error("Zero Gaussian quaternion");norm=std::sqrt(norm);for(int a=0;a<4;++a)g.rotation[a]=float(q[a]/norm);
            double opacity=value("opacity");g.opacity=float(opacity>=0?1/(1+std::exp(-opacity)):std::exp(opacity)/(1+std::exp(opacity)));
            for(int c=0;c<3;++c){g.sh[c]=float(value("f_dc_"+std::to_string(c)));for(uint32_t k=0;k<per;++k)g.sh[3*(k+1)+c]=float(value("f_rest_"+std::to_string(c*per+k)));}gs.push_back(g);
        }
        out=std::move(gs);shDegree=degree;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
#ifndef SPLAT_CPU_ONLY
bool FSplatPLYLoader::loadAndUpload(RHI::IRHIDevice* device,const String& path,FSplatGPUData& outData,
    uint64_t sourceBudget,uint64_t stagingBudget) {
    if(!device)return false;
    std::vector<FSplatGaussian> gs;uint32_t degree=0,stagingBytes=0;std::string error;
    auto admission=[&](uint64_t count,uint32_t shDegree,std::string& failure){
        if(!count)return true;
        uint64_t total=0;
        for(uint32_t width:{16u,16u,16u,4u,12u*(shDegree+1)*(shDegree+1)}){
            if(count>UINT32_MAX/width){failure="Source exceeds RHI buffer size limit";return false;}
            RHI::BufferDesc d;d.size=uint32_t(count*width);d.usage=RHI::EResourceUsage::StorageBuffer|RHI::EResourceUsage::TransferDst;d.dedicatedAllocation=true;
            uint64_t physical=0,alignment=0;
            if(!device->getBufferAllocationRequirements(d,physical,alignment)||physical<d.size||!alignment){failure="Exact source allocation requirements unavailable";return false;}
            if(physical>sourceBudget||total>sourceBudget-physical){failure="Original source backing exceeds scene budget before allocation";return false;}
            total+=physical;
        }
        const uint32_t stride=52+12*(shDegree+1)*(shDegree+1);
        uint64_t lo=0,hi=std::min<uint64_t>({stagingBudget,UINT32_MAX,count*stride})/stride;
        while(lo<hi){uint64_t mid=lo+(hi-lo+1)/2;RHI::BufferDesc d;d.size=uint32_t(mid*stride);d.usage=RHI::EResourceUsage::TransferSrc;d.memoryUsage=RHI::EMemoryUsage::Upload;d.cpuAccessible=true;d.dedicatedAllocation=true;uint64_t required=0,alignment=0;
            if(!device->getBufferAllocationRequirements(d,required,alignment)||required<d.size||!alignment){failure="Exact staging requirements unavailable";return false;}
            if(required<=stagingBudget)lo=mid;else hi=mid-1;
        }
        if(!lo){failure="Original upload staging budget cannot hold one Gaussian";return false;}
        stagingBytes=uint32_t(lo*stride);return true;
    };
    if(!LoadCPUWithAdmission(path,gs,degree,error,admission)){MR_LOG(LogTemp,Error,"[SplatPLYLoader] %s",error.c_str());return false;}
    if(gs.empty()){outData.release();outData.shDegree=int32(degree);return true;}
    FSplatGPUData result;
    if(!uploadBuffers(device,gs,degree,stagingBytes,result))return false;
    result.gaussianCount=uint32(gs.size());result.shDegree=int32(degree);outData=std::move(result);return true;
}
bool FSplatPLYLoader::uploadBuffers(RHI::IRHIDevice* device,const std::vector<FSplatGaussian>& gs,
    uint32_t degree,uint32_t stagingBytes,FSplatGPUData& outData) {
    using namespace RHI;
    auto* vk=dynamic_cast<Vulkan::VulkanDevice*>(device);
    if(!vk||!vk->getCommandListContext()||!device->supportsSubmissionCompletion())return false;
    auto create=[&](uint32_t bytes,bool upload,const char* name){BufferDesc d;d.size=bytes;d.usage=upload?EResourceUsage::TransferSrc:EResourceUsage::StorageBuffer|EResourceUsage::TransferDst;d.memoryUsage=upload?EMemoryUsage::Upload:EMemoryUsage::Default;d.cpuAccessible=upload;d.dedicatedAllocation=true;d.debugName=name;
        uint64_t expected=0,alignment=0;if(!device->getBufferAllocationRequirements(d,expected,alignment))return MonsterEngine::TSharedPtr<IRHIBuffer>{};
        auto b=device->createBuffer(d);FBufferAllocationInfo info{};
        if(!b||!b->getAllocationInfo(info)||info.reservationBytes!=expected||info.backingBytes!=expected)return MonsterEngine::TSharedPtr<IRHIBuffer>{};return b;};
    const uint32_t widths[5]={16,16,16,4,12*(degree+1)*(degree+1)};
    MonsterEngine::TSharedPtr<IRHIBuffer>* dest[5]={&outData.positions,&outData.scales,&outData.rotations,&outData.opacities,&outData.shCoefficients};
    for(int k=0;k<5;++k){*dest[k]=create(uint32_t(gs.size()*widths[k]),false,"Splat_OriginalSource");if(!*dest[k])return false;}
    auto staging=create(stagingBytes,true,"Splat_OriginalUpload");if(!staging)return false;
    FBufferAllocationInfo stagingInfo{};staging->getAllocationInfo(stagingInfo);m_stagingBackingBytes=stagingInfo.backingBytes;
    const uint32_t stride=52+widths[4],rowsPerChunk=stagingBytes/stride;
    auto* cmd=device->getImmediateCommandList();if(!cmd)return false;
    for(size_t start=0;start<gs.size();start+=rowsPerChunk){const uint32_t rows=uint32_t(std::min<size_t>(rowsPerChunk,gs.size()-start));
        auto* mapped=static_cast<uint8_t*>(staging->map());if(!mapped)return false;
        uint32_t offsets[5]{},offset=0;
        for(int k=0;k<5;++k){offsets[k]=offset;for(uint32_t row=0;row<rows;++row){const auto& g=gs[start+row];const void* fields[5]={g.position,g.scale,g.rotation,&g.opacity,g.sh};std::memcpy(mapped+offset+row*widths[k],fields[k],widths[k]);}offset+=rows*widths[k];}
        staging->unmap();vk->getCommandListContext()->refreshCommandBuffer();cmd->begin();
        for(int k=0;k<5;++k)cmd->copyBuffer(*dest[k],staging,uint64_t(rows)*widths[k],uint64_t(start)*widths[k],offsets[k]);
        cmd->resourceBarrier();const auto before=device->getLastSubmissionToken();cmd->end();cmd->submitAndWait();const auto submitted=device->getLastSubmissionToken();
        if(!submitted.value||submitted.value==before.value||!device->isSubmissionComplete(submitted))return false;
    }
    // Initialization only: explicit chunk completion permits staging destruction.
    // No backend initialData uploader (and no hidden shared staging slab) is used.
    staging.Reset();cmd->reset();return vk->DrainCompletedDestructions();
}

#endif
} // namespace MonsterRender::Splat
