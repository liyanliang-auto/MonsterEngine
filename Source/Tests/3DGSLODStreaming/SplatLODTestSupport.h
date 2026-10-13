#pragma once
#include "Core/Application.h"
#include "Core/Logging/Logging.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Renderer/Splat/SplatPipeline.h"
#include "Renderer/Splat/LOD/SplatLODBenchmark.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <cstdlib>
namespace SplatVerification {
using namespace MonsterRender;
using namespace MonsterRender::Splat;
using namespace MonsterRender::RHI;
using MonsterEngine::TSharedPtr;
namespace fs=std::filesystem;
inline fs::path ArtifactRoot(){const char* value=std::getenv("MONSTER_SPLAT_TEST_ARTIFACT_ROOT");return value&&*value?fs::path(value):fs::path("TestFile/3DGSLODStreaming/verification");}
inline void Require(bool v,const std::string& e){if(!v)throw std::runtime_error(e);}
struct Suite{unsigned passed=0,failed=0;Suite(){std::cout.setf(std::ios::unitbuf);}void Test(const std::string& n,const std::function<void()>& f){try{f();++passed;std::cout<<"PASS "<<n<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<n<<": "<<e.what()<<'\n';}}int Finish(){std::cout<<"SUMMARY total="<<passed+failed<<" passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;}};
inline fs::path Directory(const std::string& name){auto p=ArtifactRoot()/(name+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(p);return p;}
inline std::vector<uint8_t> Read(const fs::path& p){std::ifstream f(p,std::ios::binary);Require(bool(f),"artifact read: "+p.string());return {std::istreambuf_iterator<char>(f),{}};}
inline TSharedPtr<IRHIBuffer> Buffer(IRHIDevice* d,uint32_t bytes,EMemoryUsage memory=EMemoryUsage::Default,const void* data=nullptr){BufferDesc desc;desc.size=bytes;desc.usage=EResourceUsage::StorageBuffer|EResourceUsage::TransferSrc|EResourceUsage::TransferDst;desc.memoryUsage=memory;desc.initialData=data;desc.initialDataSize=data?bytes:0;desc.debugName="FormalSplatFixture";auto b=d->createBuffer(desc);Require(bool(b),"real Vulkan buffer creation");return b;}
struct Device {
    Application app;
    IRHIDevice* rhi=nullptr;
    RHI::Vulkan::VulkanDevice* vk=nullptr;
    Device():app([]{ApplicationConfig c;c.name="FormalSplatGPUVerification";c.preferredBackend=ERHIBackend::Vulkan;c.enableValidation=true;c.enableVSync=false;c.windowProperties.vsync=false;c.windowProperties.width=c.windowProperties.height=64;return c;}()){
        Require(app.initialize(),"ENV: actual Vulkan application initialization failed");rhi=app.getEngine()->getRHIDevice();vk=dynamic_cast<RHI::Vulkan::VulkanDevice*>(rhi);Require(vk&&rhi->supportsSubmissionCompletion()&&rhi->supportsDispatchIndirect(),"ENV: required real-device capabilities unavailable");
    }
    IRHICommandList* Begin(){vk->getCommandListContext()->refreshCommandBuffer();auto c=rhi->getImmediateCommandList();c->begin();return c;}
    FGPUCompletionToken Submit(IRHICommandList* c){auto previous=rhi->getLastSubmissionToken();c->end();c->submitAndWait();auto token=rhi->getLastSubmissionToken();Require(token.value&&token.value!=previous.value,"real submission did not mint changed token");Require(rhi->isSubmissionComplete(token),"completed real fence not published");return token;}
    template<class T> std::vector<T> ReadBuffer(TSharedPtr<IRHIBuffer> input,unsigned count){
        auto b=Buffer(rhi,count*sizeof(T),EMemoryUsage::Readback);
        TSharedPtr<IRHIDescriptorSetLayout> setLayout;TSharedPtr<IRHIPipelineLayout> layout;TSharedPtr<IRHIPipelineState> pipeline;TSharedPtr<IRHIDescriptorSet> set;
        const bool transfer=(input->getUsage()&EResourceUsage::TransferSrc)!=EResourceUsage::None;
        if(!transfer){
            Require((input->getUsage()&EResourceUsage::StorageBuffer)!=EResourceUsage::None,"verification cannot read this resource usage");
            FDescriptorSetLayoutDesc desc(0,"FormalStorageReadback");desc.bindings.Add(FDescriptorSetLayoutBinding(0,EDescriptorType::StorageBuffer,EShaderStage::Compute));desc.bindings.Add(FDescriptorSetLayoutBinding(1,EDescriptorType::StorageBuffer,EShaderStage::Compute));setLayout=rhi->createDescriptorSetLayout(desc);Require(bool(setLayout),"readback descriptor layout");
            FPipelineLayoutDesc ld("FormalStorageReadback");ld.setLayouts.Add(setLayout);ld.pushConstantRanges.Add(FPushConstantRange(EShaderStage::Compute,0,4));layout=rhi->createPipelineLayout(ld);auto code=Read(fs::path(MONSTER_SPLAT_TEST_SHADER_DIR)/"storage_readback.spv");auto shader=rhi->createComputeShader(MonsterEngine::TSpan<const uint8>(code.data(),code.size()));ComputePipelineStateDesc pd;pd.computeShader=shader;pd.pipelineLayout=layout;pipeline=rhi->createComputePipelineState(pd);Require(bool(pipeline),"actual storage inspection pipeline");set=rhi->allocateDescriptorSet(setLayout);Require(bool(set),"readback descriptor allocation");set->updateStorageBuffer(0,input,0,0);set->updateStorageBuffer(1,b,0,0);
        }
        auto c=Begin();c->resourceBarrier();if(transfer)c->copyBuffer(b,input,count*sizeof(T));else{unsigned words=count*sizeof(T)/4;c->setPipelineState(pipeline);c->bindDescriptorSet(layout,0,set);c->pushConstants(layout,EShaderStage::Compute,0,4,&words);c->dispatch((words+63)/64,1,1);}c->resourceBarrier();Submit(c);auto p=b->map();Require(p!=nullptr,"real readback map");std::vector<T> out(count);std::memcpy(out.data(),p,count*sizeof(T));b->unmap();return out;
    }
    ~Device(){if(vk){rhi->waitForIdle();rhi->getImmediateCommandList()->reset();vk->DrainCompletedDestructions();}app.shutdown();}
};
inline FCameraUniforms Camera(int degree=0){FCameraUniforms c{};c.viewMatrix[0]=c.viewMatrix[5]=c.viewMatrix[10]=c.viewMatrix[15]=1;c.projMatrix[0]=c.projMatrix[5]=1;c.projMatrix[10]=-1.0002f;c.projMatrix[11]=-1;c.projMatrix[14]=-.20002f;c.focalX=c.focalY=32;c.tanFovX=c.tanFovY=1;c.imageWidth=c.imageHeight=64;c.shDegree=degree;return c;}
inline FSplatGPUData Source(Device& d,unsigned count=3,int degree=1){FSplatGPUData g;g.gaussianCount=count;g.shDegree=degree;unsigned stride=3*(degree+1)*(degree+1);std::vector<float> pos(count*4),scale(count*4,.15f),rot(count*4),opacity(count,.8f),sh(count*stride);for(unsigned i=0;i<count;++i){pos[i*4+2]=-float(3+i);pos[i*4+3]=1;rot[i*4]=1;sh[i*stride]=float(i)-1;sh[i*stride+1]=1-float(i)*.5f;sh[i*stride+2]=-.5f;}g.positions=Buffer(d.rhi,pos.size()*4,EMemoryUsage::Upload,pos.data());g.scales=Buffer(d.rhi,scale.size()*4,EMemoryUsage::Upload,scale.data());g.rotations=Buffer(d.rhi,rot.size()*4,EMemoryUsage::Upload,rot.data());g.opacities=Buffer(d.rhi,opacity.size()*4,EMemoryUsage::Upload,opacity.data());g.shCoefficients=Buffer(d.rhi,sh.size()*4,EMemoryUsage::Upload,sh.data());return g;}
}
