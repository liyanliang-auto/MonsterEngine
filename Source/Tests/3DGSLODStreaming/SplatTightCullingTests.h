#pragma once
#include <random>
#include <set>
#include <limits>
#include <array>
namespace SplatVerification {
struct TightFixture { float conic[4]; float center[4]; unsigned rect[4]; unsigned image[4]; };
// Independent oracle: enumerate the actual old integer-pixel raster domain.
// No inverse conic, continuous minimum, support logarithm or AABB is used here.
inline bool TightPixelContributes(const float* q,const float* center,unsigned x,unsigned y) {
    float dx=center[0]-float(x),dy=center[1]-float(y);
    float power=-.5f*(q[0]*dx*dx+q[2]*dy*dy)-q[1]*dx*dy;
    if(power>0) return false;
    float alpha=std::min(.99f,q[3]*std::exp(power));
    return !(alpha<1.f/255.f);
}
inline bool TightTileContributes(const float* q,const float* center,unsigned tx,unsigned ty,unsigned w,unsigned h) {
    for(unsigned y=ty*16;y<std::min(h,ty*16+16);++y)
        for(unsigned x=tx*16;x<std::min(w,tx*16+16);++x)
            if(TightPixelContributes(q,center,x,y)) return true;
    return false;
}
inline void RunTightCullingTests(Suite& suite,Device& d) {
    suite.Test("TTC full indirect pipeline image equality and active-to-zero lifecycle all modes",[&]{
        auto data=Source(d);std::vector<uint8_t> reference;
        for(unsigned mode:{0u,1u,2u}){
            FSplatPipeline pipeline;pipeline.SetCapacityBudgets(3,3,1024,16ull<<20);Require(pipeline.initialize(d.rhi,3,64,64),"full pipeline init");pipeline.setGaussianData(data);pipeline.setCamera(Camera());FSplatRenderSettings settings;settings.tightTileCulling=mode;pipeline.setRenderSettings(settings);
            FSplatLODRuntimeOptions options;options.benchmark=true;options.captureOnly=true;options.width=options.height=64;options.sequencePath="synthetic-fixed-camera";options.captureTimes={0,1};options.outputDirectory=Directory("tight-mode"+std::to_string(mode)).string();options.streaming.temporaryGPUBytes=32ull<<20;FSplatLODBenchmark recorder;std::string error;Require(recorder.Begin(options,{},error),error);
            auto capture=[&](unsigned time){auto cmd=d.Begin();auto image=pipeline.execute(cmd);Require(bool(image),"full output");Require(recorder.RecordCapture(d.rhi,cmd,image,pipeline.GetFrameStats().frame,time,Camera(),pipeline.GetFrameStats().temporaryBytes,error),error);auto token=d.Submit(cmd);pipeline.MarkFrameSubmitted(token);Require(recorder.FinishCapture(token,error),error);return Read(fs::path(options.outputDirectory)/(time?"capture_001.rgba":"capture_000.rgba"));};
            FSplatActiveSet active;active.sourceIndices={2,0,1};pipeline.SetActiveSet(active);auto first=capture(0);Require(pipeline.GetFrameStats().tightTileCullingRequested==mode&&pipeline.GetFrameStats().tightTileCullingEffective==mode&&pipeline.GetFrameStats().tightTileCullingFallback.empty(),"effective mode differs without fusion");if(mode==0)reference=first;else Require(reference==first,"mode changed linear RGBA8 bytes");active.sourceIndices.clear();++active.selectionVersion;pipeline.SetActiveSet(active);auto empty=capture(1);Require(std::all_of(empty.begin(),empty.end(),[](uint8_t x){return x==0;}),"zero active stale image");auto state=d.ReadBuffer<unsigned>(pipeline.GetDispatchStateBuffer(),5);Require(state[0]==0&&state[1]==0,"zero active indirect count/overflow");Require(recorder.Finish(true,"tight synthetic correctness",error),error);
        }
    });
    suite.Test("TTC projected fusion capability records explicit safe baseline fallback",[&]{
        auto data=Source(d);FSplatPipeline p;p.SetCapacityBudgets(3,3,1024,32ull<<20);Require(p.EnableProjectedFusion(false),"enable fusion capability");Require(p.initialize(d.rhi,3,64,64),"fusion fixture init");p.setGaussianData(data);p.setCamera(Camera());FSplatRenderSettings settings;settings.tightTileCulling=2;p.setRenderSettings(settings);auto cmd=d.Begin();Require(bool(p.execute(cmd)),"fusion fallback output");auto token=d.Submit(cmd);p.MarkFrameSubmitted(token);const auto& stats=p.GetFrameStats();Require(stats.tightTileCullingRequested==2&&stats.tightTileCullingEffective==0&&!stats.tightTileCullingFallback.empty(),"fusion fallback not explicit");
    });
    suite.Test("TTC runtime strict config modes and fixed camera boundary validation",[&]{
        auto root=Directory("tight-config"); std::string error;
        auto parse=[&](std::string fields,FSplatLODRuntimeOptions& out){auto file=root/"runtime.json";std::ofstream f(file);f<<"{\"mode\":\"A\",\"sourcePLY\":\"fixture.ply\",\"sequence\":\"fixture.json\",\"output\":\"fixture\","<<fields<<"}";f.close();return FSplatLODRuntimeOptions::LoadConfig(file.string(),out,error);};
        for(unsigned mode=0;mode<3;++mode){FSplatLODRuntimeOptions o;Require(parse("\"tightTileCulling\":"+std::to_string(mode),o),error);Require(o.tightTileCulling==mode,"mode roundtrip");}
        for(auto bad:{"-1","3","1.25","true","\"1\"","null"}){FSplatLODRuntimeOptions o;Require(!parse(std::string("\"tightTileCulling\":")+bad,o),std::string("accepted mode ")+bad);}
        FSplatLODRuntimeOptions o;Require(!parse("\"tightTileCulling\":0,\"benchmarkFreezeSelection\":true",o),"freeze without fixed camera accepted");
        Require(!parse("\"tightTileCulling\":0,\"benchmarkFixedCameraSeconds\":-2",o),"invalid fixed time accepted");
        Require(parse("\"tightTileCulling\":2,\"benchmarkFixedCameraSeconds\":0,\"benchmarkFreezeSelection\":true",o),error);
    });
    suite.Test("TTC real GLSL predicate versus independent exhaustive pixel oracle",[&]{
        std::vector<TightFixture> cases;
        auto add=[&](float a,float b,float c,float opacity,float x,float y,unsigned w,unsigned h){
            for(unsigned mode:{0u,1u,2u}){TightFixture v{{a,b,c,opacity},{x,y,0,0},{0,0,(w+15)/16,(h+15)/16},{w,h,mode,0}};cases.push_back(v);}
        };
        for(auto wh:std::vector<std::pair<unsigned,unsigned>>{{1,1},{17,31},{63,65},{127,97}})
            for(float opacity:{0.f,-1.f,1.f/255.f,std::nextafter(1.f/255.f,0.f),std::nextafter(1.f/255.f,1.f),.01f,.8f,2.f})
                for(auto xy:std::vector<std::pair<float,float>>{{0,0},{15,16},{16.01f,15.99f},{-.5f,2.3f},{float(wh.first)-1,float(wh.second)-1}})
                    add(.5f,.19f,.08f,opacity,xy.first,xy.second,wh.first,wh.second);
        for(auto q:std::vector<std::array<float,3>>{{1,.99999f,1},{1e-13f,0,1},{1e13f,0,1},{-1,0,1},{1,2,1},{0,0,0}})
            add(q[0],q[1],q[2],.8f,31.5f,31.5f,63,65);
        add(1,0,1,std::numeric_limits<float>::infinity(),16,16,63,65);
        add(std::numeric_limits<float>::quiet_NaN(),0,1,.8f,16,16,63,65);
        std::mt19937 rng(20261016);std::uniform_real_distribution<float> unit(0,1);
        for(unsigned i=0;i<512;++i){float angle=unit(rng)*6.2831853f,cs=std::cos(angle),sn=std::sin(angle);float l0=std::pow(10.f,-3.f+unit(rng)*5),l1=std::pow(10.f,-3.f+unit(rng)*5);add(l0*cs*cs+l1*sn*sn,(l0-l1)*cs*sn,l0*sn*sn+l1*cs*cs,std::pow(10.f,-4.f+unit(rng)*4.5f),-16+unit(rng)*144,-16+unit(rng)*128,127,97);}
        auto input=Buffer(d.rhi,unsigned(cases.size()*sizeof(TightFixture)),EMemoryUsage::Upload,cases.data());
        auto output=Buffer(d.rhi,unsigned(cases.size()*64*4));
        FDescriptorSetLayoutDesc sd(0,"TightOracle");for(unsigned i=0;i<2;++i)sd.bindings.Add(FDescriptorSetLayoutBinding(i,EDescriptorType::StorageBuffer,EShaderStage::Compute));auto sl=d.rhi->createDescriptorSetLayout(sd);Require(bool(sl),"probe set layout");FPipelineLayoutDesc ld("TightOracle");ld.setLayouts.Add(sl);ld.pushConstantRanges.Add(FPushConstantRange(EShaderStage::Compute,0,4));auto layout=d.rhi->createPipelineLayout(ld);
        auto code=Read(fs::path(MONSTER_SPLAT_TEST_SHADER_DIR)/"tight_culling_probe.spv");auto shader=d.rhi->createComputeShader(MonsterEngine::TSpan<const uint8>(code.data(),code.size()));ComputePipelineStateDesc pd;pd.computeShader=shader;pd.pipelineLayout=layout;auto pipeline=d.rhi->createComputePipelineState(pd);Require(bool(pipeline),"probe pipeline");auto set=d.rhi->allocateDescriptorSet(sl);set->updateStorageBuffer(0,input,0,0);set->updateStorageBuffer(1,output,0,0);auto cmd=d.Begin();unsigned count=unsigned(cases.size());cmd->setPipelineState(pipeline);cmd->bindDescriptorSet(layout,0,set);cmd->pushConstants(layout,EShaderStage::Compute,0,4,&count);cmd->dispatch((count+63)/64,1,1);d.Submit(cmd);auto actual=d.ReadBuffer<unsigned>(output,count*64);
        uint64_t checked=0,removed=0,contributing=0;
        for(unsigned i=0;i<count;++i){auto& v=cases[i];for(unsigned ty=0;ty<8;++ty)for(unsigned tx=0;tx<8;++tx){bool old=tx<v.rect[2]&&ty<v.rect[3],keep=actual[i*64+ty*8+tx]!=0;Require(!keep||old,"expanded old tile support");if(!old)continue;++checked;if(!keep)++removed;bool contributes=TightTileContributes(v.conic,v.center,tx,ty,v.image[0],v.image[1]);if(contributes){++contributing;Require(keep,"false negative fixture="+std::to_string(i)+" tile="+std::to_string(tx)+","+std::to_string(ty));}if(v.image[2]==0)Require(keep,"mode0 changed old candidates");}}
        Require(removed>0,"oracle exercised no culling");std::cout<<"ORACLE fixtures="<<count<<" checkedTiles="<<checked<<" contributingTiles="<<contributing<<" removedTiles="<<removed<<" falseNegativeTiles=0\n";
    });
    for(unsigned n:{0u,1u,2u,255u,256u,257u})for(unsigned mode:{0u,1u,2u})suite.Test("TTC actual preprocess scan emit N="+std::to_string(n)+" mode="+std::to_string(mode),[&,n,mode]{
        const unsigned cap=std::max(1u,n),w=63,h=65,gx=(w+15)/16,gy=(h+15)/16,maxEntries=cap*gx*gy;
        auto data=Source(d,cap,0);auto scalePtr=static_cast<float*>(data.scales->map());Require(scalePtr!=nullptr,"anisotropic fixture map");for(unsigned i=0;i<cap;++i){scalePtr[i*4]=.8f;scalePtr[i*4+1]=.015f;scalePtr[i*4+2]=.2f;}data.scales->unmap();FSplatPreprocessPass p;Require(p.initialize(d.rhi,cap),"preprocess init");p.setInputs(data);auto camera=Camera();camera.imageWidth=w;camera.imageHeight=h;p.updateCamera(camera);
        std::vector<unsigned> indices(n);for(unsigned i=0;i<n;++i)indices[i]=n-1-i;Require(p.setActiveIndices(indices,true),"active reverse mapping");
        p.setTightTileCulling(0);auto cmd=d.Begin();Require(p.execute(cmd,n),"baseline preprocess");d.Submit(cmd);auto old=d.ReadBuffer<unsigned>(p.getOutput().bbox,cap*4);
        p.setTightTileCulling(mode);cmd=d.Begin();Require(p.execute(cmd,n),"candidate preprocess");d.Submit(cmd);auto counts=d.ReadBuffer<unsigned>(p.getOutput().tilesTouched,cap);auto boxes=d.ReadBuffer<unsigned>(p.getOutput().bbox,cap*4);auto q=d.ReadBuffer<float>(p.getOutput().conicOpacity,cap*4);auto xy=d.ReadBuffer<float>(p.getOutput().pointsXY,cap*2);auto depth=d.ReadBuffer<float>(p.getOutput().depth,cap);
        FSplatPrefixSumPass scan;Require(scan.initialize(d.rhi,cap),"scan init");FSplatAssignKeysPass emit;Require(emit.initialize(d.rhi,maxEntries,cap),"assign init");emit.setTightTileCulling(mode,w,h);unsigned sentinel=0xffffffffu;std::vector<unsigned> fill(maxEntries,sentinel);auto upload=Buffer(d.rhi,maxEntries*4,EMemoryUsage::Upload,fill.data());
        cmd=d.Begin();cmd->copyBuffer(scan.getBufferA(),p.getOutput().tilesTouched,cap*4);cmd->resourceBarrier();scan.execute(cmd,n);cmd->resourceBarrier();emit.setInputBuffers(scan.getResultBuffer(),p.getOutput().bbox,p.getOutput().depth,p.getOutput().conicOpacity,p.getOutput().pointsXY);cmd->copyBuffer(emit.getSortValues(),upload,maxEntries*4);cmd->resourceBarrier();emit.execute(cmd,n,gx,gy);d.Submit(cmd);
        unsigned total=std::accumulate(counts.begin(),counts.begin()+n,0u);auto prefix=d.ReadBuffer<unsigned>(scan.getResultBuffer(),cap);if(n)Require(prefix[n-1]==total,"scan total mismatch");auto keys=d.ReadBuffer<uint64_t>(emit.getSortKeys(),maxEntries);auto values=d.ReadBuffer<unsigned>(emit.getSortValues(),maxEntries);
        unsigned offset=0;for(unsigned i=0;i<n;++i){std::set<unsigned> tiles;for(unsigned j=0;j<counts[i];++j){unsigned slot=offset+j;Require(values[slot]==i,"count/emit hole or compact source mismatch");unsigned tile=unsigned(keys[slot]>>32),tx=tile%gx,ty=tile/gx;Require(tx>=old[i*4]&&tx<old[i*4+2]&&ty>=old[i*4+1]&&ty<old[i*4+3],"emit expanded old bbox");Require(tiles.insert(tile).second,"duplicate emitted tile");unsigned bits;std::memcpy(&bits,&depth[i],4);Require(unsigned(keys[slot])==bits,"depth bits changed");}for(unsigned ty=old[i*4+1];ty<old[i*4+3];++ty)for(unsigned tx=old[i*4];tx<old[i*4+2];++tx)if(TightTileContributes(q.data()+i*4,xy.data()+i*2,tx,ty,w,h))Require(tiles.count(ty*gx+tx)>0,"pipeline false negative");offset+=counts[i];}Require(offset==total,"emitted count mismatch");for(unsigned i=total;i<maxEntries;++i)Require(values[i]==sentinel,"out of bounds emission after scan tail");
        if(n){Require(std::abs(depth[0]-float(3+n-1))<1e-4,"reverse active mapping ignored");}
        std::cout<<"COUNT_EMIT n="<<n<<" mode="<<mode<<" entries="<<total<<" initialized="<<offset<<" holes=0 outOfBounds=0\n";
    });
}
}
