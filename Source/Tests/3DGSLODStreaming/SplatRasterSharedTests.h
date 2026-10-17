#pragma once
#include <array>
#include <numeric>
namespace SplatVerification {
// Uses unmodified production SPIR-V, host push ABI and production RGBA8 target.
struct RasterSharedFixture {
    Device& d; unsigned w=17,h=19;
    TSharedPtr<IRHITexture> image;
    TSharedPtr<IRHIDescriptorSetLayout> sl;
    TSharedPtr<IRHIPipelineLayout> layout;
    TSharedPtr<IRHIPipelineState> pipeline;
    TSharedPtr<IRHIDescriptorSet> set;
    std::vector<TSharedPtr<IRHIBuffer>> buffers;
    RasterSharedFixture(Device& device):d(device){
        TextureDesc td;td.width=w;td.height=h;td.depth=1;td.mipLevels=td.arraySize=1;td.format=EPixelFormat::R8G8B8A8_UNORM;td.usage=EResourceUsage::UnorderedAccess|EResourceUsage::TransferSrc|EResourceUsage::ShaderResource;image=d.rhi->createTexture(td);Require(bool(image),"raster image");
        // Texture creation initializes SRV layout; Raster and capture both require GENERAL.
        auto prepare=d.Begin();prepare->transitionResource(image,EResourceUsage::ShaderResource,EResourceUsage::UnorderedAccess);d.Submit(prepare);
        FDescriptorSetLayoutDesc sd(0,"RasterSharedFixture");for(unsigned i=0;i<7;++i)sd.bindings.Add(FDescriptorSetLayoutBinding(i,i==5?EDescriptorType::StorageTexture:EDescriptorType::StorageBuffer,EShaderStage::Compute));sl=d.rhi->createDescriptorSetLayout(sd);FPipelineLayoutDesc ld("RasterSharedFixture");ld.setLayouts.Add(sl);ld.pushConstantRanges.Add(FPushConstantRange(EShaderStage::Compute,0,sizeof(FRenderPushConstants)));layout=d.rhi->createPipelineLayout(ld);
        const char* root=std::getenv("MONSTER_SPLAT_SHADER_DIR");Require(root&&*root,"production shaders required");auto code=Read(fs::path(root)/"splat_render.spv");auto shader=d.rhi->createComputeShader(MonsterEngine::TSpan<const uint8>(code.data(),code.size()));ComputePipelineStateDesc pd;pd.computeShader=shader;pd.pipelineLayout=layout;pipeline=d.rhi->createComputePipelineState(pd);Require(bool(pipeline),"raster pipeline");set=d.rhi->allocateDescriptorSet(sl);Require(bool(set),"raster descriptors");set->updateStorageImage(5,image);
    }
    std::vector<uint8_t> Run(unsigned mode,unsigned n,unsigned pattern=0,unsigned force=0,unsigned overflow=0,unsigned clamp=UINT32_MAX,unsigned wire=0,bool invalid=false){
        constexpr unsigned cap=320,start=3;
        std::array<unsigned,8> ranges{};for(unsigned tile=0;tile<4;++tile){ranges[tile*2]=start;ranges[tile*2+1]=start+n;}
        std::array<unsigned,cap+start> ids{};std::array<float,cap*4> rgb{},q{};std::array<float,cap*2> xy{};
        for(unsigned i=0;i<cap;++i){unsigned id=(i*73)%cap;ids[start+i]=id;xy[id*2]=8;xy[id*2+1]=8;q[id*4]=q[id*4+2]=pattern==2?0.f:.125f;q[id*4+3]=pattern? .99f:.008f;rgb[id*4]=float((id*7)%17)/16;rgb[id*4+1]=float(id%11)/10;rgb[id*4+2]=.2f;}
        // Distinguish exact threshold, positive power rejection, alpha cap and skip.
        if(!pattern){q[3]=.08f;q[12]=q[14]=0;q[15]=1.f/255.f;q[4]=q[6]=0;q[7]=std::nextafter(1.f/255.f,0.f);q[8]=q[10]=-1;q[11]=.8f;}
        if(invalid&&n)ids[start+n/2]=UINT32_MAX;
        std::array<unsigned,5> state{std::min(start+n,clamp),overflow,cap,0,0};buffers.clear();
        auto bind=[&](unsigned binding,const void* p,unsigned bytes){auto b=Buffer(d.rhi,bytes,EMemoryUsage::Upload,p);buffers.push_back(b);set->updateStorageBuffer(binding,b,0,0);};
        bind(0,ranges.data(),sizeof(ranges));bind(1,ids.data(),sizeof(ids));bind(2,rgb.data(),sizeof(rgb));bind(3,q.data(),sizeof(q));bind(4,xy.data(),sizeof(xy));bind(6,state.data(),sizeof(state));
        FSplatLODRuntimeOptions o;o.benchmark=o.captureOnly=true;o.width=w;o.height=h;o.sequencePath="raster-focused";o.captureTimes={0};o.rasterSharedMemoryMode=mode;o.outputDirectory=Directory("raster-focused").string();FSplatLODBenchmark recorder;std::string error;Require(recorder.Begin(o,{},error),error);
        auto cmd=d.Begin();cmd->resourceBarrier();cmd->setPipelineState(pipeline);cmd->bindDescriptorSet(layout,0,set);FRenderPushConstants pc{w,h,wire,force,mode};cmd->pushConstants(layout,EShaderStage::Compute,0,sizeof(pc),&pc);cmd->dispatch(2,2,1);Require(recorder.RecordCapture(d.rhi,cmd,image,0,0,Camera(),0,error),error);auto token=d.Submit(cmd);Require(recorder.FinishCapture(token,error),error);Require(recorder.Finish(true,"Raster production RGBA8 fixture",error),error);auto bytes=Read(fs::path(o.outputDirectory)/"capture_000.rgba");Require(bytes.size()==w*h*4,"raster capture size");auto meta=Read(fs::path(o.outputDirectory)/"capture_000.json");std::string text(meta.begin(),meta.end());Require(text.find("\"rasterSharedMemoryMode\":"+std::to_string(mode))!=std::string::npos,"capture mode identity missing");return bytes;
    }
};
inline void RunRasterSharedTests(Suite& suite,Device& d){
    suite.Test("RASTER strict runtime switch defaults valid invalid and atomic parsing",[&]{
        Require(FSplatRenderSettings{}.rasterSharedMemoryMode==0&&FSplatLODRuntimeOptions{}.rasterSharedMemoryMode==0,"original default changed");auto root=Directory("raster-config");std::string error;
        auto parse=[&](std::string value,FSplatLODRuntimeOptions& out){auto file=root/"runtime.json";std::ofstream f(file);f<<"{\"mode\":\"A\",\"sourcePLY\":\"fixture.ply\",\"sequence\":\"fixture.json\",\"output\":\"fixture\",\"rasterSharedMemoryMode\":"<<value<<"}";f.close();return FSplatLODRuntimeOptions::LoadConfig(file.string(),out,error);};
        for(unsigned mode:{0u,1u}){FSplatLODRuntimeOptions out;Require(parse(std::to_string(mode),out)&&out.rasterSharedMemoryMode==mode,error);}
        for(const char* value:{"-1","2","1.25","true","\"1\"","null","4294967297"}){FSplatLODRuntimeOptions out;out.rasterSharedMemoryMode=1;Require(!parse(value,out),std::string("invalid accepted ")+value);Require(out.rasterSharedMemoryMode==1,"failed parse changed output");}
        FSplatPipeline p;FSplatRenderSettings settings;settings.rasterSharedMemoryMode=17;p.setRenderSettings(settings);
    });
    suite.Test("RASTER live full pipeline switch shared original and unsupported fallback",[&]{
        auto data=Source(d);FSplatPipeline p;p.SetCapacityBudgets(3,3,1024,32ull<<20);Require(p.initialize(d.rhi,3,64,64),"raster full pipeline init");p.setGaussianData(data);p.setCamera(Camera());std::vector<uint8_t> reference;
        for(unsigned mode:{0u,1u,0u,17u}){FSplatRenderSettings settings;settings.rasterSharedMemoryMode=mode;p.setRenderSettings(settings);FSplatLODRuntimeOptions o;o.benchmark=o.captureOnly=true;o.width=o.height=64;o.sequencePath="raster-full-pipeline";o.captureTimes={0};o.rasterSharedMemoryMode=mode==1?1:0;o.outputDirectory=Directory("raster-pipeline-toggle").string();FSplatLODBenchmark recorder;std::string error;Require(recorder.Begin(o,{},error),error);auto cmd=d.Begin();auto output=p.execute(cmd);Require(bool(output),"raster pipeline output");Require(recorder.RecordCapture(d.rhi,cmd,output,p.GetFrameStats().frame,0,Camera(),p.GetFrameStats().temporaryBytes,error),error);auto token=d.Submit(cmd);p.MarkFrameSubmitted(token);Require(recorder.FinishCapture(token,error),error);auto bytes=Read(fs::path(o.outputDirectory)/"capture_000.rgba");if(reference.empty()){reference=bytes;Require(std::any_of(bytes.begin(),bytes.end(),[](uint8_t x){return x!=0;}),"full pipeline fixture transparent");}else Require(reference==bytes,"live mode toggle changed RGBA8 bytes");Require(recorder.Finish(true,"Raster full pipeline live toggle",error),error);}
    });
    for(unsigned n:{0u,1u,63u,64u,65u,128u,193u})suite.Test("RASTER actual edge partial full multi batch N="+std::to_string(n),[&,n]{RasterSharedFixture f(d);auto a=f.Run(0,n),b=f.Run(1,n);Require(a==b,"batch changed production RGBA8 bytes");if(n==0)Require(std::all_of(b.begin(),b.end(),[](uint8_t x){return !x;}),"empty not clear");else Require(std::any_of(b.begin(),b.end(),[](uint8_t x){return x!=0;}),"fixture transparent");});
    for(unsigned pattern:{1u,2u})suite.Test("RASTER actual mixed or all completed lanes pattern="+std::to_string(pattern),[&,pattern]{RasterSharedFixture f(d);Require(f.Run(0,193,pattern)==f.Run(1,193,pattern),"done lanes changed output or failed progress");});
    suite.Test("RASTER actual invalid ID tail initialization state clamp and force modes",[&]{RasterSharedFixture f(d);Require(f.Run(0,65,0,0,0,UINT32_MAX,0,true)==f.Run(1,65,0,0,0,UINT32_MAX,0,true),"invalid ID changed");Require(f.Run(0,193,0,0,0,68)==f.Run(1,193,0,0,0,68),"state count clamp changed");for(unsigned mode:{0u,1u}){auto before=f.Run(mode,65);auto overflow=f.Run(mode,193,0,0,1);Require(before==overflow,"overflow failed to retain previous image");auto clear=f.Run(mode,193,0,1,1);Require(std::all_of(clear.begin(),clear.end(),[](uint8_t x){return !x;}),"forceClear1 failed");}Require(f.Run(0,193,0,2,1,0)==f.Run(1,193,0,2,1,0),"forceClear2 failed state bypass");});
    suite.Test("RASTER actual wireframe uses original Gaussian ID not staged position",[&]{RasterSharedFixture f(d);auto plain=f.Run(0,65);auto wire=f.Run(0,65,0,0,0,UINT32_MAX,1);Require(plain!=wire,"wireframe fixture did not exercise contour");Require(wire==f.Run(1,65,0,0,0,UINT32_MAX,1),"wireframe gaussIdx changed");});
}
}
