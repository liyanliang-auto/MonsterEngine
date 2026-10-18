// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/SplatPLYLoader.h"
#include "Renderer/Splat/SplatPLYStream.h"
#include "Core/Logging/LogMacros.h"
#include <algorithm>
#include <chrono>
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
bool FSplatPLYLoader::LoadCPU(const std::string& path, std::vector<FSplatGaussian>& out,
    uint32_t& shDegree, std::string& error) {
    return LoadCPUWithAdmission(path, out, shDegree, error, {});
}
bool FSplatPLYLoader::LoadCPUWithAdmission(const std::string& path,
    std::vector<FSplatGaussian>& out, uint32_t& shDegree, std::string& error,
    const std::function<bool(uint64_t,uint32_t,std::string&)>& admission) {
    try {
        FSplatPLYHeader header;
        if (!FSplatPLYStream::Probe(path, header, error)) return false;
        // Header admission precedes collector expansion and every GPU allocation.
        if (admission && !admission(header.recordCount, header.shDegree, error)) return false;
        if (header.recordCount > std::numeric_limits<size_t>::max() / sizeof(FSplatGaussian)) {
            error = "PLY collector size exceeds address space"; return false;
        }
        std::vector<FSplatGaussian> gaussians;
        gaussians.reserve(static_cast<size_t>(header.recordCount));
        FSplatBuildControl control;
        const FSplatPLYBatchSink collect = [&](std::span<const FSplatGaussian> batch,
            const FSplatBuildSourceCursor&, std::string&) {
            gaussians.insert(gaussians.end(), batch.begin(), batch.end());
            return true;
        };
        if (!FSplatPLYStream::StreamRecords(path, header, 1024, control, collect, error)) return false;
        out = std::move(gaussians); shDegree = header.shDegree; error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
#ifndef SPLAT_CPU_ONLY
bool FSplatPLYLoader::loadAndUpload(RHI::IRHIDevice* device,const String& path,FSplatGPUData& outData,
    uint64_t sourceBudget,uint64_t stagingBudget,uint32_t storageMode,int32_t requestedSHDegree) {
    m_stagingBackingBytes=0;
    if(!device||storageMode>1)return false;
    std::vector<FSplatGaussian> gs;uint32_t degree=0,stagingBytes=0;std::string error;
    auto admission=[&](uint64_t count,uint32_t shDegree,std::string& failure){
        if(!count)return true;
        uint64_t total=0;
        const uint32_t effective=requestedSHDegree<0?shDegree:std::min<uint32_t>(shDegree,uint32_t(requestedSHDegree));
        const uint64_t sizes[5]={count*16,count*16,count*16,count*4,SplatSHStorageBytes(count*3*(effective+1)*(effective+1),storageMode)};
        for(uint64_t bytes:sizes){
            if(bytes>UINT32_MAX){failure="Source exceeds RHI buffer size limit";return false;}
            RHI::BufferDesc d;d.size=uint32_t(bytes);d.usage=RHI::EResourceUsage::StorageBuffer|RHI::EResourceUsage::TransferDst;d.dedicatedAllocation=true;
            uint64_t physical=0,alignment=0;
            if(!device->getBufferAllocationRequirements(d,physical,alignment)||physical<d.size||!alignment){failure="Exact source allocation requirements unavailable";return false;}
            if(physical>sourceBudget||total>sourceBudget-physical){failure="Original source backing exceeds scene budget before allocation";return false;}
            total+=physical;
        }
        const uint32_t scalars=3*(effective+1)*(effective+1);
        const uint32_t stride=52+uint32_t(SplatSHStorageBytes(scalars,storageMode));
        uint64_t lo=0,hi=std::min<uint64_t>({stagingBudget,UINT32_MAX,count*stride})/stride;
        while(lo<hi){uint64_t mid=lo+(hi-lo+1)/2;RHI::BufferDesc d;d.size=uint32_t(mid*stride);d.usage=RHI::EResourceUsage::TransferSrc;d.memoryUsage=RHI::EMemoryUsage::Upload;d.cpuAccessible=true;d.dedicatedAllocation=true;uint64_t required=0,alignment=0;
            if(!device->getBufferAllocationRequirements(d,required,alignment)||required<d.size||!alignment){failure="Exact staging requirements unavailable";return false;}
            if(required<=stagingBudget)lo=mid;else hi=mid-1;
        }
        if(storageMode==1 && (scalars&1) && count>1)lo-=lo%2;
        if(!lo){failure="Original upload staging budget cannot hold one Gaussian";return false;}
        stagingBytes=uint32_t(lo*stride);return true;
    };
    if(!LoadCPUWithAdmission(path,gs,degree,error,admission)){MR_LOG(LogTemp,Error,"[SplatPLYLoader] %s",error.c_str());return false;}
    const uint32_t sourceDegree=degree; degree=requestedSHDegree<0?degree:std::min<uint32_t>(degree,uint32_t(requestedSHDegree));
    if(gs.empty()){outData.release();outData.shDegree=int32(degree);outData.shStorageMode=storageMode;return true;}
    FSplatGPUData result;
    const auto validationStart=std::chrono::steady_clock::now();
    if(storageMode==1)for(const auto& g:gs)for(uint32_t i=0;i<3*(degree+1)*(degree+1);++i){
        uint16_t half;if(!SplatFloatToHalf(g.sh[i],half)){MR_LOG(LogTemp,Error,"[SplatPLYLoader] SH FP16 rejects nonfinite or abs(value)>65504 before GPU allocation");return false;}}
    result.shConversionMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-validationStart).count();
    if(!uploadBuffers(device,gs,degree,stagingBytes,storageMode,result))return false;
    result.gaussianCount=uint32(gs.size());result.shDegree=int32(degree);result.sourceSHDegree=int32(sourceDegree);outData=std::move(result);return true;
}
bool FSplatPLYLoader::uploadBuffers(RHI::IRHIDevice* device,const std::vector<FSplatGaussian>& gs,
    uint32_t degree,uint32_t stagingBytes,uint32_t storageMode,FSplatGPUData& outData) {
    const auto uploadStart=std::chrono::steady_clock::now();
    using namespace RHI;
    auto* vk=dynamic_cast<Vulkan::VulkanDevice*>(device);
    if(!vk||!vk->getCommandListContext()||!device->supportsSubmissionCompletion())return false;
    auto create=[&](uint32_t bytes,bool upload,const char* name){BufferDesc d;d.size=bytes;d.usage=upload?EResourceUsage::TransferSrc:EResourceUsage::StorageBuffer|EResourceUsage::TransferDst;d.memoryUsage=upload?EMemoryUsage::Upload:EMemoryUsage::Default;d.cpuAccessible=upload;d.dedicatedAllocation=true;d.debugName=name;
        uint64_t expected=0,alignment=0;if(!device->getBufferAllocationRequirements(d,expected,alignment))return MonsterEngine::TSharedPtr<IRHIBuffer>{};
        auto b=device->createBuffer(d);FBufferAllocationInfo info{};
        if(!b||!b->getAllocationInfo(info)||info.reservationBytes!=expected||info.backingBytes!=expected)return MonsterEngine::TSharedPtr<IRHIBuffer>{};return b;};
    const uint32_t scalars=3*(degree+1)*(degree+1);
    const uint32_t widths[5]={16,16,16,4,scalars*4};
    outData.shStorageMode=storageMode;
    outData.shLogicalBytes=SplatSHStorageBytes(uint64_t(gs.size())*scalars,storageMode);
    outData.sourceLogicalBytes=uint64_t(gs.size())*52+outData.shLogicalBytes;
    outData.stagingLogicalBytes=stagingBytes;
    MonsterEngine::TSharedPtr<IRHIBuffer>* dest[5]={&outData.positions,&outData.scales,&outData.rotations,&outData.opacities,&outData.shCoefficients};
    for(int k=0;k<5;++k){*dest[k]=create(k==4?uint32_t(outData.shLogicalBytes):uint32_t(gs.size()*widths[k]),false,"Splat_OriginalSource");if(!*dest[k])return false;}
    auto staging=create(stagingBytes,true,"Splat_OriginalUpload");if(!staging)return false;
    uint64_t stagingRequired=0;
    if(!device->getBufferAllocationRequirements(staging->getDesc(),stagingRequired,outData.stagingAlignmentBytes))return false;
    FBufferAllocationInfo stagingInfo{};staging->getAllocationInfo(stagingInfo);m_stagingBackingBytes=stagingInfo.backingBytes;
    const uint32_t stride=52+uint32_t(SplatSHStorageBytes(scalars,storageMode));
    uint32_t rowsPerChunk=stagingBytes/stride;
    if(storageMode==1&&(scalars&1)&&gs.size()>1)rowsPerChunk-=rowsPerChunk%2;
    if(!rowsPerChunk)return false;
    auto* cmd=device->getImmediateCommandList();if(!cmd)return false;
    for(size_t start=0;start<gs.size();start+=rowsPerChunk){const uint32_t rows=uint32_t(std::min<size_t>(rowsPerChunk,gs.size()-start));
        auto* mapped=static_cast<uint8_t*>(staging->map());if(!mapped)return false;
        uint32_t offsets[5]{},sizes[5]{},offset=0;
        for(int k=0;k<5;++k){
            offsets[k]=offset;
            sizes[k]=k==4?uint32_t(SplatSHStorageBytes(uint64_t(rows)*scalars,storageMode)):rows*widths[k];
            if(k==4&&storageMode==1){
                const auto conversionStart=std::chrono::steady_clock::now();
                // Chunks begin at even scalar offsets. Only the global final word is padded.
                std::memset(mapped+offset,0,sizes[k]);
                for(uint32_t row=0;row<rows;++row)for(uint32_t c=0;c<scalars;++c){
                    uint16_t half;if(!SplatFloatToHalf(gs[start+row].sh[c],half)){staging->unmap();return false;}
                    const uint64_t scalar=uint64_t(row)*scalars+c;
                    uint32_t word;std::memcpy(&word,mapped+offset+(scalar/2)*4,4);
                    word|=uint32_t(half)<<((scalar&1)*16);std::memcpy(mapped+offset+(scalar/2)*4,&word,4);
                }
                outData.shConversionMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-conversionStart).count();
            }else for(uint32_t row=0;row<rows;++row){const auto& g=gs[start+row];const void* fields[5]={g.position,g.scale,g.rotation,&g.opacity,g.sh};std::memcpy(mapped+offset+row*widths[k],fields[k],widths[k]);}
            offset+=sizes[k];
        }
        staging->unmap();vk->getCommandListContext()->refreshCommandBuffer();cmd->begin();
        for(int k=0;k<5;++k){
            const uint64_t destination=k==4&&storageMode==1?uint64_t(start)*scalars*2:uint64_t(start)*widths[k];
            cmd->copyBuffer(*dest[k],staging,sizes[k],destination,offsets[k]);outData.uploadedBytes+=sizes[k];
        }
        cmd->resourceBarrier();const auto before=device->getLastSubmissionToken();cmd->end();cmd->submitAndWait();const auto submitted=device->getLastSubmissionToken();
        if(!submitted.value||submitted.value==before.value||!device->isSubmissionComplete(submitted))return false;
    }
    // Initialization only: explicit chunk completion permits staging destruction.
    // No backend initialData uploader (and no hidden shared staging slab) is used.
    staging.Reset();cmd->reset();
    outData.uploadMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count();
    return vk->DrainCompletedDestructions();
}

#endif
} // namespace MonsterRender::Splat
