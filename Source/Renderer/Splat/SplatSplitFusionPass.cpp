// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/SplatSplitFusionPass.h"
#include "Core/ShaderCompiler.h"
#include <algorithm>
#include <cstring>

namespace MonsterRender::Splat {
namespace {
using namespace RHI;
constexpr auto Storage = EResourceUsage::StorageBuffer | EResourceUsage::TransferSrc | EResourceUsage::TransferDst;
BufferDesc Desc(uint64_t bytes, EResourceUsage usage=Storage, EMemoryUsage memory=EMemoryUsage::Default) {
    BufferDesc d;d.size=bytes;d.usage=usage;d.memoryUsage=memory;d.cpuAccessible=memory!=EMemoryUsage::Default;
    d.dedicatedAllocation=true;d.debugName="Splat_SplitFusion";return d;
}
std::vector<BufferDesc> Descriptions(uint32_t active) {
    const uint64_t groups=(uint64_t(active)+1)/2,slots=(uint64_t(active)+3)/4;
    return {Desc(groups*8+FSplatSplitFusionPass::CounterBytes,Storage,EMemoryUsage::Dynamic),
        Desc(groups*8+FSplatSplitFusionPass::CounterBytes,Storage,EMemoryUsage::Dynamic),
        Desc(FSplatSplitFusionPass::CounterBytes),Desc(slots*27*16),Desc(groups*4),Desc(groups*4),
        Desc(16,Storage|EResourceUsage::IndirectArgs),Desc(16,Storage|EResourceUsage::IndirectArgs)};
}
}
bool FSplatSplitFusionPass::Preflight(RHI::IRHIDevice* device,uint32_t active,uint64_t&bytes) {
    bytes=0;if(!device||!active||((uint64_t(active)+3)/4)*27*16>UINT32_MAX)return false;
    for(const auto&d:Descriptions(active)){uint64_t required=0,alignment=0;if(!device->getBufferAllocationRequirements(d,required,alignment)||!required||required>UINT64_MAX-bytes)return false;bytes+=required;}return true;
}
bool FSplatSplitFusionPass::Initialize(RHI::IRHIDevice*device,uint32_t active,const FSplatPreprocessOutput&out) {
    if(m_device||!device||!active)return false;m_device=device;m_activeCapacity=active;m_maxGroups=(active+1)/2;m_cacheSlots=(active+3)/4;
    auto descriptions=Descriptions(active);auto buffers=GetOwnedBuffers();buffers.clear();for(auto&desc:descriptions){auto b=device->createBuffer(desc);if(!b)return false;buffers.push_back(b);}
    m_groups[0]=buffers[0];m_groups[1]=buffers[1];m_counter=buffers[2];m_checkpoint=buffers[3];m_freshQueue=buffers[4];m_exactQueue=buffers[5];m_freshArgs=buffers[6];m_exactArgs=buffers[7];
    return Create(m_clear,"Shaders/Splat/compiled/splat_fusion_split_clear.spv",out)&&
        Create(m_visible,"Shaders/Splat/compiled/splat_fusion_visible.spv",out)&&
        Create(m_cheap,"Shaders/Splat/compiled/splat_fusion_split_cheap.spv",out)&&
        Create(m_args,"Shaders/Splat/compiled/splat_fusion_queue_args.spv",out)&&
        Create(m_exact,"Shaders/Splat/compiled/splat_fusion_split_exact.spv",out)&&
        Create(m_fresh,"Shaders/Splat/compiled/splat_fusion_split_fresh.spv",out);
}
bool FSplatSplitFusionPass::Create(FPass&pass,const char*path,const FSplatPreprocessOutput&out) {
    using namespace RHI;FDescriptorSetLayoutDesc sd(0,"SplatSplitFusion");
    for(uint32_t i=0;i<14;++i)sd.bindings.Add(FDescriptorSetLayoutBinding(i,EDescriptorType::StorageBuffer,EShaderStage::Compute));
    pass.sets=m_device->createDescriptorSetLayout(sd);if(!pass.sets)return false;FPipelineLayoutDesc ld("SplatSplitFusion");ld.setLayouts.Add(pass.sets);ld.pushConstantRanges.Add(FPushConstantRange(EShaderStage::Compute,0,sizeof(FPush)));
    pass.layout=m_device->createPipelineLayout(ld);if(!pass.layout)return false;auto bytes=ShaderCompiler::readFileBytes(SplatShaderPath(path));if(bytes.empty())return false;
    auto shader=m_device->createComputeShader(MonsterEngine::TSpan<const uint8>(bytes.data(),bytes.size()));if(!shader)return false;ComputePipelineStateDesc pd;pd.computeShader=shader;pd.pipelineLayout=pass.layout;pd.debugName="SplatSplitFusion";pass.pipeline=m_device->createComputePipelineState(pd);if(!pass.pipeline)return false;
    for(uint32_t slot=0;slot<2;++slot){pass.descriptors[slot]=m_device->allocateDescriptorSet(pass.sets);if(!pass.descriptors[slot])return false;
        const MonsterEngine::TSharedPtr<IRHIBuffer> buffers[]={out.radii,out.depth,out.rgb,out.conicOpacity,out.pointsXY,out.tilesTouched,out.bbox,m_groups[slot],m_counter,m_checkpoint,m_freshQueue,m_exactQueue,m_freshArgs,m_exactArgs};
        for(uint32_t i=0;i<14;++i)pass.descriptors[slot]->updateStorageBuffer(i,buffers[i],0,i==7?uint64_t(m_maxGroups)*8:0);
    }return true;
}
bool FSplatSplitFusionPass::SetGroups(const std::vector<FSplatFusionGroup>&groups) {
    if(groups.size()>m_maxGroups)return false;uint64_t end=0;
    for(auto&g:groups){if(g.Count()<2||g.First()<end||uint64_t(g.First())+g.Count()>m_activeCapacity)return false;end=uint64_t(g.First())+g.Count();}
    m_cpuGroups=groups;return true;
}
void FSplatSplitFusionPass::InvalidateCheckpoint(){m_checkpointCleared=false;m_pendingClear=false;}
void FSplatSplitFusionPass::ConfirmSubmitted(){if(m_pendingClear)m_checkpointCleared=true;m_pendingClear=false;}
void FSplatSplitFusionPass::DiscardRecording(){m_pendingClear=false;}
void FSplatSplitFusionPass::Dispatch(RHI::IRHICommandList*cmd,FPass&pass,uint32_t slot,const FPush&push,uint32_t groups) {
    cmd->setPipelineState(pass.pipeline);cmd->bindDescriptorSet(pass.layout,0,pass.descriptors[slot]);cmd->pushConstants(pass.layout,RHI::EShaderStage::Compute,0,sizeof(push),&push);
    if(groups)cmd->dispatch(std::min(groups,65535u),(groups+65534u)/65535u,1);cmd->resourceBarrier();
}
bool FSplatSplitFusionPass::Execute(RHI::IRHICommandList*cmd,uint32_t slot,uint32_t sourceCount,uint32_t x,uint32_t y,uint64_t frame,float error) {
    if(!m_device||!cmd||slot>=2||sourceCount>m_activeCapacity||(!m_cpuGroups.empty()&&uint64_t(m_cpuGroups.back().First())+m_cpuGroups.back().Count()>sourceCount))return false;
    void*mapped=m_groups[slot]->map();if(!mapped)return false;if(!m_cpuGroups.empty())std::memcpy(mapped,m_cpuGroups.data(),m_cpuGroups.size()*8);const uint32_t offset=m_maxGroups*8;uint32_t counters[12]={0,0,1};std::memcpy(static_cast<uint8*>(mapped)+offset,counters,CounterBytes);m_groups[slot]->unmap();
    cmd->copyBuffer(m_counter,m_groups[slot],CounterBytes,0,offset);cmd->resourceBarrier();FPush push{uint32_t(m_cpuGroups.size()),sourceCount,x,y,error,m_cacheSlots,m_maxGroups,uint32_t(frame)};
    cmd->writeGPUTimestamp(frame,"FusionCheap",true);if(!m_checkpointCleared){Dispatch(cmd,m_clear,slot,push,(m_cacheSlots+127)/128);m_pendingClear=true;}
    Dispatch(cmd,m_visible,slot,push,(sourceCount+255)/256);Dispatch(cmd,m_cheap,slot,push,(push.groups+31)/32);cmd->writeGPUTimestamp(frame,"FusionCheap",false);
    cmd->writeGPUTimestamp(frame,"FusionQueues",true);Dispatch(cmd,m_args,slot,push,1);cmd->writeGPUTimestamp(frame,"FusionQueues",false);
    cmd->writeGPUTimestamp(frame,"FusionExact",true);cmd->setPipelineState(m_exact.pipeline);cmd->bindDescriptorSet(m_exact.layout,0,m_exact.descriptors[slot]);cmd->pushConstants(m_exact.layout,RHI::EShaderStage::Compute,0,sizeof(push),&push);cmd->dispatchIndirect(m_exactArgs.Get(),0);cmd->resourceBarrier();cmd->writeGPUTimestamp(frame,"FusionExact",false);
    // Exact failures stay original this frame. Fresh search is only for misses,
    // collisions and scheduled fine groups; its queue was closed before exact.
    cmd->writeGPUTimestamp(frame,"FusionFresh",true);cmd->setPipelineState(m_fresh.pipeline);cmd->bindDescriptorSet(m_fresh.layout,0,m_fresh.descriptors[slot]);cmd->pushConstants(m_fresh.layout,RHI::EShaderStage::Compute,0,sizeof(push),&push);cmd->dispatchIndirect(m_freshArgs.Get(),0);cmd->resourceBarrier();cmd->writeGPUTimestamp(frame,"FusionFresh",false);return true;
}
std::vector<MonsterEngine::TSharedPtr<RHI::IRHIBuffer>> FSplatSplitFusionPass::GetOwnedBuffers() const {
    return {m_groups[0],m_groups[1],m_counter,m_checkpoint,m_freshQueue,m_exactQueue,m_freshArgs,m_exactArgs};
}
}
