// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatLODBenchmark.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanRHICommandList.h"
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
std::string Quote(const std::string& s) {
    std::ostringstream out; out << '"';
    for (unsigned char c:s) { if(c=='"'||c=='\\') out<<'\\'<<char(c); else if(c<32) out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec; else out<<char(c); }
    out << '"'; return out.str();
}
const std::array<const char*,7> Stages = {"Pipeline","Preprocess","Scan","AssignKeys","RadixSort","TileBounds","Render"};
}

struct FSplatLODBenchmark::FImpl {
    struct Row {FSplatLODBenchmarkFrame frame;std::map<std::string,double> gpu;bool count=false;uint32_t sort=0;bool overflow=false;uint32_t originalActive=0,rendered=0,fused=0;bool fusionApplied=false;float fusionError=0;};
    FSplatLODRuntimeOptions options;
    std::map<std::string,std::string> metadata;
    std::map<uint64_t,Row> pending;
    std::ofstream frames,gpu,counts;
    bool begun=false,finished=false,overflow=false;
    uint64_t rows=0,sampleRows=0,missingGPU=0,missingCounts=0,lateGPU=0,captures=0;
    RHI::IRHIDevice* captureDevice=nullptr;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> readback;
    uint64_t captureBytes=0,captureFrame=0;
    double captureTime=0;
    std::string captureSelectionSHA256, captureCameraSHA256; // Snapshot the recorded submission, not later metadata.
    FCameraUniforms camera{};
    bool capturePending=false;
    bool TimingComplete(const Row& row) const {
        for(const char* stage:Stages)if(!row.gpu.count(stage))return false;
        if(row.frame.pipeline.radixPhaseTimingEnabled)
            for(const char* stage:{"RadixHistogram","RadixOffsets","RadixScatter"})if(!row.gpu.count(stage))return false;
        if(row.frame.wholeFrameTimingRequired)for(const char* stage:{"WholeFrame","Upload","Presentation"})if(!row.gpu.count(stage))return false;
        return true;
    }
    bool WriteMetadata(const std::string& status,const std::string& reason,std::string& error) {
        std::ofstream out(std::filesystem::u8path(options.outputDirectory)/"metadata.json",std::ios::trunc);
        if(!out){error="Cannot write benchmark metadata";return false;}
        out<<std::setprecision(17)<<"{\n  \"status\": "<<Quote(status)<<",\n  \"reason\": "<<Quote(reason)<<",\n  \"mode\": "<<Quote(std::string(1,options.mode))<<",\n  \"width\": "<<options.width<<", \"height\": "<<options.height<<",\n  \"renderWidth\": "<<(options.renderWidth?options.renderWidth:options.width)<<", \"renderHeight\": "<<(options.renderHeight?options.renderHeight:options.height)<<",\n  \"warmupSeconds\": "<<options.warmupSeconds<<", \"sampleSeconds\": "<<options.sampleSeconds<<", \"rounds\": "<<options.rounds<<",\n  \"captureOnly\": "<<(options.captureOnly?"true":"false")<<", \"cacheCondition\": "<<Quote(options.cacheCondition)<<",\n  \"qualityPixels\": "<<options.lod.qualityPixels<<", \"hysteresis\": "<<options.lod.hysteresis<<", \"fullLeaf\": "<<(options.fullLeaf?"true":"false")<<",\n  \"activeCapacity\": "<<options.activeCapacity<<", \"sortCapacity\": "<<options.sortCapacity<<",\n  \"sceneGPUBytes\": "<<options.streaming.sceneGPUBytes<<", \"cpuCacheBytes\": "<<options.streaming.cpuCacheBytes<<", \"stagingBytes\": "<<options.streaming.stagingBytes<<", \"temporaryGPUBytes\": "<<options.streaming.temporaryGPUBytes<<",\n  \"maxUploadBytesPerFrame\": "<<options.streaming.maxUploadBytesPerFrame<<", \"maxConcurrentReads\": "<<options.streaming.maxConcurrentReads<<",\n  \"rows\": "<<rows<<", \"missingGPUFrames\": "<<missingGPU<<", \"lateGPUSamples\": "<<lateGPU<<", \"captures\": "<<captures<<",\n  \"metadata\": {";
        bool first=true;for(const auto& item:metadata){if(!first)out<<',';first=false;out<<'\n'<<"    "<<Quote(item.first)<<": "<<Quote(item.second);}out<<"\n  }\n}\n";
        out.close();if(!out){error="Benchmark metadata write failed";return false;}return true;
    }
    void FlushRow(std::map<uint64_t,Row>::iterator it) {
        const auto& r=it->second;const auto& f=r.frame;const bool timing=TimingComplete(r);
        if(f.sampled&&!f.excluded&&!timing)++missingGPU;
        if(f.sampled&&!f.excluded&&!r.count)++missingCounts;
        if(f.sampled&&!f.excluded)++sampleRows;
        frames<<f.frame<<','<<f.elapsedSeconds<<','<<f.sequenceSeconds<<','<<f.round<<','<<f.sampled<<','<<f.excluded<<','<<f.loading<<','<<f.degraded<<','<<f.frameMilliseconds<<','<<f.cpuMilliseconds<<','<<f.selectionMilliseconds;
        for(const char* stage:Stages){frames<<',';auto sample=r.gpu.find(f.wholeFrameTimingRequired&&std::strcmp(stage,"Pipeline")==0?"WholeFrame":stage);if(sample!=r.gpu.end())frames<<sample->second;}
        for(const char* stage:{"Pipeline","Upload","Presentation"}){frames<<',';auto sample=r.gpu.find(stage);if(sample!=r.gpu.end())frames<<sample->second;}
        frames<<','<<timing<<','<<r.count<<',';if(r.count)frames<<r.sort;
        frames<<','<<r.overflow<<','<<f.pipeline.sourceCapacity<<','<<f.pipeline.activeCount<<','<<f.pipeline.activeCapacity<<','<<f.pipeline.sortCapacity<<','<<f.pipeline.temporaryBytes<<','<<f.pipeline.outputImageBytes<<','<<f.pool.residentBytes<<','<<f.allocations.sourceReservedBytes<<','<<f.allocations.sourceBackingBytes<<','<<f.pool.stagingBytes<<','<<f.allocations.stagingReservedBytes<<','<<f.allocations.stagingBackingBytes<<','<<f.cpu.totalReservedBytes<<','<<f.cpu.encodedCacheBytes<<','<<f.cpu.inFlightReservedBytes<<','<<f.cpu.readyBytes<<','<<f.cpu.transferredBytes<<','<<f.cpu.metadataBytes<<','<<f.allocatorBytes<<','<<f.allocatorAvailableBytes<<','<<f.io.bytesRead<<','<<f.io.cacheHits<<','<<f.io.requests<<','<<f.io.retries<<','<<f.io.failed<<','<<f.io.cancelled<<','<<f.io.pendingReads<<','<<f.io.activeReads<<','<<f.pool.uploadedBytes<<','<<f.pool.uploadedPages<<','<<f.pool.evictedPages<<','<<f.pool.pendingUploads<<','<<f.pool.pinnedPages<<','<<f.pool.inFlightPages<<','<<f.pool.rejectedUploads<<','<<f.pipeline.telemetrySkippedFrames<<','<<f.allocatorPooledReservedBytes<<','<<f.allocatorPooledUsedBytes<<','<<f.allocatorDedicatedCommittedBytes;
        frames<<','<<f.selectedLeafNodes<<','<<f.selectedProxyNodes<<','<<f.proxyGaussianCount<<','<<f.maxSelectedProxyErrorPixels<<','<<r.originalActive<<','<<r.rendered<<','<<r.fused<<','<<r.fusionApplied<<','<<r.fusionError<<','<<f.pipeline.tightTileCullingRequested<<','<<f.pipeline.tightTileCullingEffective;
        frames<<','<<f.pipeline.radixSortBlocksPerWorkgroupRequested<<','<<f.pipeline.radixSortBlocksPerWorkgroupEffective;
        for(const char* stage:{"RadixHistogram","RadixOffsets","RadixScatter"}){frames<<',';const auto sample=r.gpu.find(stage);if(sample!=r.gpu.end())frames<<sample->second;}
        frames<<','<<f.pipeline.radixKeysBytes<<','<<f.pipeline.radixValuesBytes<<','<<f.pipeline.radixHistogramBytes<<','<<f.pipeline.radixPhaseTimingEnabled<<','<<options.rasterSharedMemoryMode<<'\n';
        ++rows;pending.erase(it);
    }
};
FSplatLODBenchmark::FSplatLODBenchmark():Impl(std::make_unique<FImpl>()){}
FSplatLODBenchmark::~FSplatLODBenchmark(){std::string error;if(Impl->begun&&!Impl->finished)Finish(false,"Recorder destroyed before completion",error);}
bool FSplatLODBenchmark::Begin(const FSplatLODRuntimeOptions& options,const std::map<std::string,std::string>& metadata,std::string& error){
    if(Impl->begun){error="Recorder already initialized";return false;}if(!options.Validate(error))return false;
    try{std::filesystem::create_directories(std::filesystem::u8path(options.outputDirectory));auto& s=*Impl;s.options=options;s.metadata=metadata;s.metadata["rasterSharedMemoryMode"]=std::to_string(options.rasterSharedMemoryMode);
        s.frames.open(std::filesystem::u8path(options.outputDirectory)/"frames.csv",std::ios::trunc);s.gpu.open(std::filesystem::u8path(options.outputDirectory)/"gpu_samples.csv",std::ios::trunc);s.counts.open(std::filesystem::u8path(options.outputDirectory)/"gpu_counts.csv",std::ios::trunc);
        if(!s.frames||!s.gpu||!s.counts)throw std::runtime_error("Cannot open benchmark CSV files");
        s.frames<<std::setprecision(17)<<"frame,elapsed_s,sequence_s,round,sampled,excluded,loading,degraded,frame_ms,cpu_ms,selection_ms,gpu_ms,preprocess_ms,scan_ms,assign_ms,radix_ms,bounds_ms,render_ms,pipeline_ms,upload_ms,presentation_ms,gpu_available,count_available,sort_count,overflow,source_capacity,active_count,active_capacity,sort_capacity,temporary_bytes,output_image_bytes,resident_bytes,source_reserved_bytes,source_backing_bytes,live_staging_bytes,staging_reserved_bytes,staging_backing_bytes,cpu_charged_bytes,encoded_cache_bytes,inflight_cpu_bytes,ready_cpu_bytes,transferred_cpu_bytes,metadata_cpu_bytes,allocator_bytes,allocator_available_bytes,disk_bytes,cache_hits,requests,retries,failures,cancellations,pending_reads,active_reads,uploaded_bytes,uploaded_pages,evicted_pages,pending_uploads,pinned_pages,inflight_pages,rejected_uploads,telemetry_skipped,allocator_pooled_reserved_bytes,allocator_pooled_used_bytes,allocator_dedicated_committed_bytes,selected_leaf_nodes,selected_proxy_nodes,proxy_gaussians,max_proxy_error_px,measured_source_active,rendered_primitives,fused_primitives,fusion_applied,max_fusion_rgba_l2,tight_tile_culling_requested,tight_tile_culling_effective,radix_blocks_requested,radix_blocks_effective,radix_histogram_ms,radix_offsets_ms,radix_scatter_ms,radix_keys_bytes,radix_values_bytes,radix_histogram_bytes,radix_phase_timing,raster_shared_memory_mode\n";
        s.gpu<<std::setprecision(17)<<"frame,stage,gpu_ms\n";s.counts<<"frame,sort_count,overflow,source_active,rendered_primitives,fused_primitives,fusion_applied,max_fusion_rgba_l2,fresh_queued,exact_queued,certificate_pass,certificate_fail,exact_checks,original_visible,queue_overflow,uses_certificates\n";s.begun=true;
        return s.WriteMetadata("RUNNING","Acceptance gates require downstream evaluation",error);
    }catch(const std::exception& e){error=e.what();return false;}
}
bool FSplatLODBenchmark::Record(const FSplatLODBenchmarkFrame& frame,std::string& error){
    auto& s=*Impl;if(!s.begun||s.finished){error="Recorder is not open";return false;}
    try{
        s.metadata["tightTileCulling"]=std::to_string(s.options.tightTileCulling);
        s.metadata["tightTileCullingRequested"]=std::to_string(frame.pipeline.tightTileCullingRequested);
        s.metadata["tightTileCullingEffective"]=std::to_string(frame.pipeline.tightTileCullingEffective);
        s.metadata["tightTileCullingFallback"]=frame.pipeline.tightTileCullingFallback;
        s.metadata["radixSortBlocksPerWorkgroupRequested"]=std::to_string(frame.pipeline.radixSortBlocksPerWorkgroupRequested);
        s.metadata["radixSortBlocksPerWorkgroupEffective"]=std::to_string(frame.pipeline.radixSortBlocksPerWorkgroupEffective);
        s.metadata["radixSortPhaseTimingEffective"]=frame.pipeline.radixPhaseTimingEnabled?"true":"false";
        s.metadata["radixTimingPolicy"]="Child phase sums require all six/eight unique pass intervals; parent RadixSort/Pipeline/WholeFrame overlap. Missing queries remain unavailable.";
        auto copy=frame;copy.pipeline.timestampSamples.clear();copy.pipeline.countSamples.clear();if(!s.pending.emplace(frame.frame,FImpl::Row{std::move(copy),{},false,0,false}).second)throw std::runtime_error("Duplicate benchmark frame ID");
        for(const auto& sample:frame.pipeline.timestampSamples){s.gpu<<sample.frame<<','<<sample.label<<','<<sample.milliseconds<<'\n';auto found=s.pending.find(sample.frame);if(found!=s.pending.end())found->second.gpu[sample.label]=sample.milliseconds;else ++s.lateGPU;}
        for(const auto& sample:frame.pipeline.countSamples){s.counts<<sample.frame<<','<<sample.count<<','<<sample.overflow<<','<<sample.originalActiveCount<<','<<sample.renderedPrimitives<<','<<sample.fusedPrimitives<<','<<sample.fusionApplied<<','<<sample.maxFusionRelativeRGBAError<<','<<sample.fusionFreshQueued<<','<<sample.fusionExactQueued<<','<<sample.fusionCertificatePass<<','<<sample.fusionCertificateFail<<','<<sample.fusionExactChecks<<','<<sample.fusionOriginalVisible<<','<<sample.fusionQueueOverflow<<','<<sample.fusionUsesCertificates<<'\n';s.overflow=s.overflow||sample.overflow;auto found=s.pending.find(sample.frame);if(found!=s.pending.end()){found->second.count=true;found->second.sort=sample.count;found->second.overflow=sample.overflow;found->second.originalActive=sample.originalActiveCount;found->second.rendered=sample.renderedPrimitives;found->second.fused=sample.fusedPrimitives;found->second.fusionApplied=sample.fusionApplied;found->second.fusionError=sample.maxFusionRelativeRGBAError;}}
        while(!s.pending.empty()){auto oldest=s.pending.begin();if(oldest->second.count&&s.TimingComplete(oldest->second))s.FlushRow(oldest);else if(s.pending.size()>128)s.FlushRow(oldest);else break;}
        if(!s.frames||!s.gpu||!s.counts)throw std::runtime_error("Benchmark CSV write failed");error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool FSplatLODBenchmark::Resolve(const FSplatPipelineFrameStats& stats,std::string& error){
    auto& s=*Impl;if(!s.begun||s.finished){error="Recorder is not open";return false;}
    for(const auto& sample:stats.timestampSamples){s.gpu<<sample.frame<<','<<sample.label<<','<<sample.milliseconds<<'\n';auto found=s.pending.find(sample.frame);if(found!=s.pending.end())found->second.gpu[sample.label]=sample.milliseconds;else ++s.lateGPU;}
    for(const auto& sample:stats.countSamples){s.counts<<sample.frame<<','<<sample.count<<','<<sample.overflow<<','<<sample.originalActiveCount<<','<<sample.renderedPrimitives<<','<<sample.fusedPrimitives<<','<<sample.fusionApplied<<','<<sample.maxFusionRelativeRGBAError<<','<<sample.fusionFreshQueued<<','<<sample.fusionExactQueued<<','<<sample.fusionCertificatePass<<','<<sample.fusionCertificateFail<<','<<sample.fusionExactChecks<<','<<sample.fusionOriginalVisible<<','<<sample.fusionQueueOverflow<<','<<sample.fusionUsesCertificates<<'\n';s.overflow=s.overflow||sample.overflow;auto found=s.pending.find(sample.frame);if(found!=s.pending.end()){found->second.count=true;found->second.sort=sample.count;found->second.overflow=sample.overflow;found->second.originalActive=sample.originalActiveCount;found->second.rendered=sample.renderedPrimitives;found->second.fused=sample.fusedPrimitives;found->second.fusionApplied=sample.fusionApplied;found->second.fusionError=sample.maxFusionRelativeRGBAError;}}
    if(!s.gpu||!s.counts){error="GPU sample write failed";return false;}return true;
}
void FSplatLODBenchmark::SetMetadata(const std::string& key,const std::string& value){Impl->metadata[key]=value;}
bool FSplatLODBenchmark::RecordCapture(RHI::IRHIDevice* device,RHI::IRHICommandList* cmd,MonsterEngine::TSharedPtr<RHI::IRHITexture> image,uint64_t frame,double time,const FCameraUniforms& camera,uint64_t pipelineBytes,std::string& error){
    auto& s=*Impl;auto fail=[&](const char* e){error=e;return false;};if(!s.begun||s.finished||!s.options.captureOnly||s.capturePending||!device||!cmd||!image)return fail("Invalid capture-only state");
    if(image->getDesc().format!=RHI::EPixelFormat::R8G8B8A8_UNORM||image->getWidth()!=s.options.width||image->getHeight()!=s.options.height)return fail("Capture image must match registered RGBA8 resolution");
    auto* list=dynamic_cast<RHI::Vulkan::FVulkanRHICommandListImmediate*>(cmd);auto* texture=dynamic_cast<RHI::Vulkan::VulkanTexture*>(image.Get());if(!list||!texture||!list->GetVulkanCommandBuffer())return fail("Capture requires native Vulkan recording");
    if(!s.readback){RHI::BufferDesc desc;desc.size=uint64_t(s.options.width)*s.options.height*4;desc.usage=RHI::EResourceUsage::TransferDst;desc.memoryUsage=RHI::EMemoryUsage::Readback;desc.cpuAccessible=true;desc.dedicatedAllocation=true;desc.debugName="Splat_BenchmarkCapture";
        uint64 required=0,alignment=0;if(!device->getBufferAllocationRequirements(desc,required,alignment)||pipelineBytes>s.options.streaming.temporaryGPUBytes||required>s.options.streaming.temporaryGPUBytes-pipelineBytes)return fail("Capture readback exceeds measured temporary budget");
        s.readback=device->createBuffer(desc);RHI::FBufferAllocationInfo allocation;if(!s.readback||!s.readback->getAllocationInfo(allocation))return fail("Capture allocation accounting unavailable");s.captureBytes=allocation.backingBytes;if(s.captureBytes>s.options.streaming.temporaryGPUBytes-pipelineBytes){s.readback.Reset();s.captureBytes=0;return fail("Capture backing exceeds temporary budget");}
    }
    auto* buffer=dynamic_cast<RHI::Vulkan::VulkanBuffer*>(s.readback.Get());if(!buffer)return fail("Capture buffer backend mismatch");const auto native=list->GetVulkanCommandBuffer()->getHandle();if(!native)return fail("Capture command buffer is invalid");
    VkImageMemoryBarrier before{};before.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;before.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;before.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;before.oldLayout=VK_IMAGE_LAYOUT_GENERAL;before.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;before.srcQueueFamilyIndex=before.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;before.image=texture->getImage();before.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&before);
    VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={s.options.width,s.options.height,1};vkCmdCopyImageToBuffer(native,texture->getImage(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer->getBuffer(),1,&region);
    auto after=before;after.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;after.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;after.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;after.newLayout=VK_IMAGE_LAYOUT_GENERAL;vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&after);
    VkBufferMemoryBarrier host{};host.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=buffer->getBuffer();host.size=VK_WHOLE_SIZE;vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    s.captureDevice=device;s.captureFrame=frame;s.captureTime=time;s.camera=camera;
    s.captureSelectionSHA256=s.metadata["selectionSHA256"];s.captureCameraSHA256=s.metadata["cameraSHA256"];
    s.capturePending=true;error.clear();return true;
}
bool FSplatLODBenchmark::FinishCapture(RHI::FGPUCompletionToken token,std::string& error){
    auto& s=*Impl;if(!s.capturePending||!token.value||!s.captureDevice){error="Capture has no actual submission token";return false;}
    s.captureDevice->waitForIdle();if(!s.captureDevice->isSubmissionComplete(token)){error="Capture submission did not complete";return false;}
    const auto* pixels=static_cast<const uint8_t*>(s.readback->map());if(!pixels){error="Cannot map capture readback";return false;}
    bool success=false;
    try{
        std::ostringstream stem;stem<<"capture_"<<std::setw(3)<<std::setfill('0')<<s.captures;const auto root=std::filesystem::u8path(s.options.outputDirectory)/stem.str();
        std::ofstream rgba(root.string()+".rgba",std::ios::binary);rgba.write(reinterpret_cast<const char*>(pixels),std::streamsize(uint64_t(s.options.width)*s.options.height*4));rgba.close();
        std::ofstream ppm(root.string()+".ppm",std::ios::binary);ppm<<"P6\n"<<s.options.width<<' '<<s.options.height<<"\n255\n";for(uint64_t i=0;i<uint64_t(s.options.width)*s.options.height;++i)ppm.write(reinterpret_cast<const char*>(pixels+i*4),3);ppm.close();
        std::ofstream meta(root.string()+".json");meta<<std::setprecision(17)<<"{\"frame\":"<<s.captureFrame<<",\"sequenceSeconds\":"<<s.captureTime<<",\"mode\":"<<Quote(std::string(1,s.options.mode))<<",\"width\":"<<s.options.width<<",\"height\":"<<s.options.height<<",\"format\":\"RGBA8_UNORM\",\"colorSpace\":\"linear shader output before sRGB present\",\"background\":\"black RGB with transparent alpha\",\"exposure\":1,\"shDegree\":"<<s.camera.shDegree<<",\"selectionSHA256\":"<<Quote(s.captureSelectionSHA256)<<",\"cameraSHA256\":"<<Quote(s.captureCameraSHA256)<<",\"tightTileCullingRequested\":"<<Quote(s.metadata["tightTileCullingRequested"])<<",\"tightTileCullingEffective\":"<<Quote(s.metadata["tightTileCullingEffective"])<<",\"radixSortBlocksPerWorkgroupRequested\":"<<Quote(s.metadata["radixSortBlocksPerWorkgroupRequested"])<<",\"radixSortBlocksPerWorkgroupEffective\":"<<Quote(s.metadata["radixSortBlocksPerWorkgroupEffective"])<<",\"rasterSharedMemoryMode\":"<<s.options.rasterSharedMemoryMode<<",\"view\":[";
        for(size_t i=0;i<16;++i){if(i)meta<<',';meta<<s.camera.viewMatrix[i];}meta<<"],\"projection\":[";for(size_t i=0;i<16;++i){if(i)meta<<',';meta<<s.camera.projMatrix[i];}meta<<"],\"cameraPosition\":["<<s.camera.camPos[0]<<','<<s.camera.camPos[1]<<','<<s.camera.camPos[2]<<"],\"focal\":["<<s.camera.focalX<<','<<s.camera.focalY<<"],\"excludedFromPerformance\":true}\n";meta.close();
        if(!rgba||!ppm||!meta)throw std::runtime_error("Capture artifact write failed");++s.captures;success=true;error.clear();
    }catch(const std::exception& e){error=e.what();}
    s.readback->unmap();s.capturePending=false;return success;
}
uint64_t FSplatLODBenchmark::GetCaptureAllocatedBytes()const{return Impl->captureBytes;}
bool FSplatLODBenchmark::Finish(bool completed,const std::string& reason,std::string& error){
    auto& s=*Impl;if(!s.begun){error="Recorder never initialized";return false;}if(s.finished)return true;
    while(!s.pending.empty())s.FlushRow(s.pending.begin());s.frames.close();s.gpu.close();s.counts.close();s.finished=true;
    s.metadata["missingCountFrames"]=std::to_string(s.missingCounts);
    const bool files=bool(s.frames)&&bool(s.gpu)&&bool(s.counts);const bool verified=completed&&files&&!s.overflow&&(s.options.captureOnly?s.captures==s.options.captureTimes.size():s.sampleRows>0&&s.missingGPU==0&&s.missingCounts==0);
    const std::string status=!completed||!files||s.overflow?"FAILED":verified?"RECORDED":"UNVERIFIED";
    const bool metadata=s.WriteMetadata(status,reason,error);s.readback.Reset();s.captureBytes=0;
    if(!verified&&error.empty())error="Run did not record complete valid evidence";return verified&&metadata;
}
}
