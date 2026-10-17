#pragma once
#include "Renderer/Splat/SplatSHStorage.h"
#include <array>
#include <limits>
namespace SplatVerification {
inline void RunSHStorageHostTests(Suite& suite) {
    suite.Test("SH half conversion boundaries and nearest-even", [] {
        const std::pair<float,uint16_t> values[]={{0.f,0},{-0.f,0x8000},{1.f,0x3c00},{-2.f,0xc000},{65504.f,0x7bff},{-65504.f,0xfbff},{std::ldexp(1.f,-24),1},{std::ldexp(1.f,-14),0x400},{std::ldexp(1.f,-25),0},{3*std::ldexp(1.f,-25),2},{1.f+std::ldexp(1.f,-11),0x3c00},{1.f+3*std::ldexp(1.f,-11),0x3c02}};
        for(auto v:values){uint16_t h=0xffff;Require(SplatFloatToHalf(v.first,h)&&h==v.second,"binary16 boundary or tie mismatch");}
        for(float v:{65505.f,-65505.f,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){uint16_t h=123;Require(!SplatFloatToHalf(v,h)&&h==123,"invalid input accepted or output changed");}
    });
    suite.Test("SH packing order tail empty and rejection atomicity", [] {
        const float x[]={1,-2,std::ldexp(1.f,-24)};std::vector<uint32_t> words;
        Require(SplatPackSHHalf(x,3,words)&&words==std::vector<uint32_t>{0xc0003c00,1},"low/high order or final padding wrong");
        auto before=words;const float invalid[]={1,65505};Require(!SplatPackSHHalf(invalid,2,words)&&words==before,"failed packing published partial output");
        Require(!SplatPackSHHalf(nullptr,1,words)&&words==before,"null source accepted");
        Require(SplatPackSHHalf(nullptr,0,words)&&words.empty(),"empty packing failed");
        Require(SplatSHStorageBytes(48,0)==192&&SplatSHStorageBytes(48,1)==96&&SplatSHStorageBytes(3,1)==8&&SplatSHStorageBytes(0,1)==0&&SplatSHStorageBytes(48,2)==0,"representation byte accounting");
    });
    suite.Test("SH mode defaults rollback and unsupported path guards", [] {
        FSplatLODRuntimeOptions o;Require(o.shStorageMode==0&&FSplatGPUData{}.shStorageMode==0,"FP32 default changed");
        o.mode='A';o.shStorageMode=1;std::string error;Require(o.Validate(error),error);
        for(char mode:{'B','C'}){o.mode=mode;Require(!o.Validate(error),"unsupported LOD/streaming accepted");}o.mode='A';
        o.fullLeaf=true;Require(!o.Validate(error),"fullLeaf accepted");o.fullLeaf=false;o.projectedFusion=true;Require(!o.Validate(error),"fusion accepted");o.projectedFusion=false;
        o.shStorageMode=2;Require(!o.Validate(error),"invalid storage mode accepted");o.shStorageMode=0;Require(o.Validate(error),error);
    });
}
inline void RunSHStorageGPUTests(Suite& suite,Device& d) {
    for(unsigned degree:{0u,3u})suite.Test("SH production preprocess FP32 FP16 parity degree="+std::to_string(degree),[&,degree]{
        const unsigned count=3,stride=3*(degree+1)*(degree+1);auto data=Source(d,count,degree);std::vector<float> sh(count*stride);
        for(unsigned i=0;i<sh.size();++i)sh[i]=float(int(i%13)-6)/128.f;
        // Final RGB scalar is read at the last active source and last packed word.
        sh.back()=.125f;std::vector<uint32_t> packed;Require(SplatPackSHHalf(sh.data(),sh.size(),packed),"pack fixture");
        FSplatPreprocessPass pass;Require(pass.initialize(d.rhi,count),"preprocess init");pass.updateCamera(Camera(degree));std::vector<float> reference;
        for(unsigned mode:{0u,1u,0u}){
            data.shStorageMode=mode;data.shCoefficients=mode?Buffer(d.rhi,unsigned(packed.size()*4),EMemoryUsage::Upload,packed.data()):Buffer(d.rhi,unsigned(sh.size()*4),EMemoryUsage::Upload,sh.data());
            Require(data.shCoefficients->getSize()==SplatSHStorageBytes(sh.size(),mode),"bound logical bytes mismatch");
            FBufferAllocationInfo allocation;Require(data.shCoefficients->getAllocationInfo(allocation)&&allocation.backingBytes>=data.shCoefficients->getSize(),"allocation below logical bytes");
            pass.setInputs(data);Require(pass.setActiveIndices({2,0},true),"active map");auto cmd=d.Begin();Require(pass.execute(cmd,2),"preprocess dispatch");d.Submit(cmd);
            auto colors=d.ReadBuffer<float>(pass.getOutput().rgb,8);for(float v:colors)Require(std::isfinite(v),"nonfinite decoded color");
            if(reference.empty()){reference=colors;Require(colors[0]>0,"fixture produced no color");}else for(unsigned i=0;i<colors.size();++i)Require(std::abs(colors[i]-reference[i])<1e-6f,"FP16 decode changed exactly representable SH output");
        }
        data.shStorageMode=1;data.shCoefficients=Buffer(d.rhi,4,EMemoryUsage::Upload,packed.data());pass.setInputs(data);auto cmd=d.Begin();Require(!pass.execute(cmd,2),"short SH buffer accepted");d.Submit(cmd);
        Require(!pass.setActiveIndices({3},true),"out-of-range source accepted");cmd=d.Begin();Require(!pass.execute(cmd,count+1),"dispatch exceeds output capacity");d.Submit(cmd);
    });
}
}
