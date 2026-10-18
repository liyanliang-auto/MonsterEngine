// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatLODBenchmark.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
struct Value {
    enum Kind { String, Number, Boolean, Array } kind = String;
    std::string text;
    double number = 0;
    bool boolean = false;
    std::vector<double> array;
};
class ConfigReader {
    const std::string& input; size_t pos = 0;
    void Space() { while (pos < input.size() && (input[pos]==' ' || input[pos]=='\r' || input[pos]=='\n' || input[pos]=='\t')) ++pos; }
    bool Take(char c) { Space(); if (pos < input.size() && input[pos] == c) { ++pos; return true; } return false; }
    void Need(char c) { if (!Take(c)) throw std::runtime_error("Malformed runtime JSON"); }
    std::string Text() {
        Need('"'); std::string out;
        while (pos < input.size()) {
            const unsigned char c = input[pos++]; if (c == '"') return out;
            if (c < 32) throw std::runtime_error("Invalid JSON string");
            if (c != '\\') { out += char(c); continue; }
            if (pos == input.size()) break;
            const char escape=input[pos++];
            switch (escape) {
            case '"': case '\\': case '/': out+=escape; break;
            case 'b': out+='\b'; break; case 'f': out+='\f'; break; case 'n': out+='\n'; break;
            case 'r': out+='\r'; break; case 't': out+='\t'; break;
            default: throw std::runtime_error("Use UTF-8 paths directly; unsupported JSON escape");
            }
        }
        throw std::runtime_error("Unterminated JSON string");
    }
    double Number() {
        Space(); const size_t start=pos; if(pos<input.size() && input[pos]=='-') ++pos;
        if(pos==input.size() || input[pos]<'0' || input[pos]>'9') throw std::runtime_error("Invalid JSON number");
        if(input[pos]=='0') ++pos; else while(pos<input.size() && input[pos]>='0' && input[pos]<='9') ++pos;
        if(pos<input.size() && input[pos]=='.') { ++pos; const size_t digits=pos; while(pos<input.size() && input[pos]>='0' && input[pos]<='9') ++pos; if(pos==digits) throw std::runtime_error("Invalid JSON fraction"); }
        if(pos<input.size() && (input[pos]=='e'||input[pos]=='E')) { ++pos; if(pos<input.size() && (input[pos]=='+'||input[pos]=='-')) ++pos; const size_t digits=pos; while(pos<input.size() && input[pos]>='0' && input[pos]<='9') ++pos; if(pos==digits) throw std::runtime_error("Invalid JSON exponent"); }
        const double result=std::stod(input.substr(start,pos-start)); if(!std::isfinite(result)) throw std::runtime_error("Nonfinite JSON number"); return result;
    }
public:
    explicit ConfigReader(const std::string& in):input(in){}
    std::map<std::string,Value> Read() {
        std::map<std::string,Value> result; Need('{'); if(!Take('}')) for(;;) {
            auto key=Text(); Need(':'); Space(); Value value;
            if(pos<input.size() && input[pos]=='"') { value.kind=Value::String; value.text=Text(); }
            else if(Take('[')) { value.kind=Value::Array; if(!Take(']')) for(;;) { if(value.array.size()>=256) throw std::runtime_error("Too many capture times"); value.array.push_back(Number()); if(Take(']')) break; Need(','); } }
            else if(input.compare(pos,4,"true")==0) { pos+=4; value.kind=Value::Boolean; value.boolean=true; }
            else if(input.compare(pos,5,"false")==0) { pos+=5; value.kind=Value::Boolean; }
            else { value.kind=Value::Number; value.number=Number(); }
            if(!result.emplace(key,std::move(value)).second) throw std::runtime_error("Duplicate runtime config field");
            if(Take('}')) break; Need(',');
        }
        Space(); if(pos!=input.size()) throw std::runtime_error("Trailing runtime JSON data"); return result;
    }
};
} // namespace

bool FSplatLODRuntimeOptions::Validate(std::string& error) const {
    auto fail=[&](const char* message){error=message;return false;};
    if(fusionImplementation!="split"&&fusionImplementation!="scalar")return fail("Fusion implementation must be split or scalar");
    if(mode<'A'||mode>'E') return fail("Mode must be A, B, C, D or E");
    if(tightTileCulling>2) return fail("tightTileCulling must be 0, 1 or 2");
    if(shStorageMode>1) return fail("shStorageMode must be 0 or 1");
    if(shStorageDegree < -1 || shStorageDegree > 3) return fail("shStorageDegree must be -1 or 0..3");
    if(projectionColorCompression && projectedFusion) return fail("Projection color compression requires projectedFusion=false");
    if(shStorageMode==1 && (mode!='A' || fullLeaf || projectedFusion)) return fail("SH FP16 supports full-resident A only; LOD/Streaming/fullLeaf/fusion are unsupported");
    if(rasterSharedMemoryMode>1) return fail("rasterSharedMemoryMode must be 0 or 1");
    if(!IsValidRadixSortBlocks(radixSortBlocksPerWorkgroup)) return fail("radixSortBlocksPerWorkgroup must be 4, 8, 16 or 32");
    if(!std::isfinite(benchmarkFixedCameraSeconds)||(benchmarkFixedCameraSeconds<0&&benchmarkFixedCameraSeconds!=-1)) return fail("Invalid fixed camera time");
    if(benchmarkFreezeSelection&&benchmarkFixedCameraSeconds<0) return fail("Frozen benchmark selection requires a fixed camera");
    if(!std::isfinite(fusionRelativeRGBAError)||fusionRelativeRGBAError<=0||fusionRelativeRGBAError>1) return fail("Invalid projected fusion RGBA error tolerance");
    if(width<16||height<16||width>16384||height>16384||!activeCapacity||!sortCapacity||uint64(sortCapacity)>uint64(65535u)*RADIX_SORT_WORKGROUP*radixSortBlocksPerWorkgroup||displaySHDegree < -1||displaySHDegree>3) return fail("Invalid resolution, capacity or SH degree");
    if((renderWidth==0)!=(renderHeight==0)||(renderWidth&&(renderWidth<16||renderHeight<16||renderWidth>width||renderHeight>height))) return fail("Invalid internal render resolution");
    if(!streaming.sceneGPUBytes||!streaming.cpuCacheBytes||!streaming.stagingBytes||!streaming.temporaryGPUBytes||!streaming.maxUploadBytesPerFrame||!streaming.maxConcurrentReads||streaming.maxConcurrentReads>64||streaming.retries>32||streaming.retryDelayMilliseconds>60000) return fail("Invalid streaming budgets or concurrency");
    if(!std::isfinite(lod.qualityPixels)||lod.qualityPixels<=0||!std::isfinite(lod.hysteresis)||lod.hysteresis<0||lod.hysteresis>=1) return fail("Invalid LOD quality or hysteresis");
    if(!std::isfinite(warmupSeconds)||warmupSeconds<0||!std::isfinite(sampleSeconds)||sampleSeconds<=0||sampleSeconds>3600||!rounds||rounds>100||!std::isfinite(captureSettleSeconds)||captureSettleSeconds<0||captureSettleSeconds>60) return fail("Invalid benchmark timing");
    if(benchmark && (sequencePath.empty()||outputDirectory.empty())) return fail("Benchmark requires sequence and output paths");
    if(benchmark && !captureOnly && !functionalSmoke && (warmupSeconds<10||sampleSeconds<60||rounds<3)) return fail("Performance runs require 10s warmup, 60s rounds and three rounds");
    if(benchmark && !captureOnly && !functionalSmoke && mode=='E' && sampleSeconds*rounds<1800) return fail("Restricted E requires at least 30 minutes of registered sampling");
    if(captureOnly && (!benchmark||captureTimes.empty())) return fail("Capture-only requires benchmark and capture times");
    double previous=-1; for(double time:captureTimes) { if(!std::isfinite(time)||time<0||time<=previous) return fail("Capture times must be finite, nonnegative and increasing"); previous=time; }
    if(cacheCondition!="unknown"&&cacheCondition!="warm-file-cache"&&cacheCondition!="cold-file-cache") return fail("Invalid cache-condition label");
    if(outputDirectory.find('\0')!=std::string::npos) return fail("Invalid output directory");
    error.clear(); return true;
}
bool FSplatLODRuntimeOptions::LoadConfig(const std::string& path,FSplatLODRuntimeOptions& options,std::string& error) {
    try {
        std::ifstream in(std::filesystem::u8path(path),std::ios::binary|std::ios::ate); if(!in) throw std::runtime_error("Cannot open runtime config");
        const auto length=in.tellg(); if(length<0||length>65536) throw std::runtime_error("Runtime config exceeds 64 KiB");
        in.seekg(0); std::string source(static_cast<size_t>(length),'\0'); if(!in.read(source.data(),length)) throw std::runtime_error("Cannot read runtime config");
        auto parsed=ConfigReader(source).Read(); auto value=options;
        auto text=[&](const Value& v){if(v.kind!=Value::String) throw std::runtime_error("Expected config string");return v.text;};
        auto number=[&](const Value& v){if(v.kind!=Value::Number) throw std::runtime_error("Expected config number");return v.number;};
        auto boolean=[&](const Value& v){if(v.kind!=Value::Boolean) throw std::runtime_error("Expected config boolean");return v.boolean;};
        auto integer=[&](const Value& v,uint64_t limit){const double n=number(v);if(n<0||n>double(limit)||n!=std::floor(n)) throw std::runtime_error("Invalid config integer");return uint64_t(n);};
        for(const auto& field:parsed) { const auto& k=field.first;const auto& v=field.second;
            if(k=="version") { if(integer(v,1)!=1) throw std::runtime_error("Unsupported runtime config version"); }
            else if(k=="scene") value.scenePath=text(v); else if(k=="sourcePLY") value.sourcePLY=text(v); else if(k=="sequence") value.sequencePath=text(v); else if(k=="output") value.outputDirectory=text(v); else if(k=="cacheCondition") value.cacheCondition=text(v);
            else if(k=="mode") { const auto m=text(v);if(m.size()!=1) throw std::runtime_error("Invalid mode");value.mode=m[0]; }
            else if(k=="functionalSmoke") value.functionalSmoke=boolean(v); else if(k=="benchmark") value.benchmark=boolean(v); else if(k=="captureOnly") value.captureOnly=boolean(v); else if(k=="validation") value.validation=boolean(v); else if(k=="vsync") value.vsync=boolean(v); else if(k=="fullLeaf") value.fullLeaf=boolean(v);
            else if(k=="fusionImplementation")value.fusionImplementation=text(v);
            else if(k=="tightTileCulling") value.tightTileCulling=uint32_t(integer(v,2));
            else if(k=="shStorageMode") value.shStorageMode=uint32_t(integer(v,1));
            else if(k=="shStorageDegree") value.shStorageDegree=int32_t(integer(v,3));
            else if(k=="projectionColorCompression") value.projectionColorCompression=boolean(v);
            else if(k=="rasterSharedMemoryMode") value.rasterSharedMemoryMode=uint32_t(integer(v,1));
            else if(k=="radixSortBlocksPerWorkgroup") value.radixSortBlocksPerWorkgroup=uint32(integer(v,32));
            else if(k=="radixSortPhaseTiming") value.radixSortPhaseTiming=boolean(v);
            else if(k=="benchmarkFixedCameraSeconds") value.benchmarkFixedCameraSeconds=number(v);
            else if(k=="benchmarkFreezeSelection") value.benchmarkFreezeSelection=boolean(v);
            else if(k=="projectedFusion") value.projectedFusion=boolean(v); else if(k=="fusionRelativeRGBAError") value.fusionRelativeRGBAError=float(number(v));
            else if(k=="width") value.width=uint32_t(integer(v,16384)); else if(k=="height") value.height=uint32_t(integer(v,16384));
            else if(k=="renderWidth") value.renderWidth=uint32_t(integer(v,16384)); else if(k=="renderHeight") value.renderHeight=uint32_t(integer(v,16384)); else if(k=="rounds") value.rounds=uint32_t(integer(v,100));
            else if(k=="activeCapacity") value.activeCapacity=uint32_t(integer(v,UINT32_MAX)); else if(k=="sortCapacity") value.sortCapacity=uint32_t(integer(v,UINT32_MAX));
            else if(k=="displaySHDegree") { const double n=number(v);if(n<-1||n>3||n!=std::floor(n)) throw std::runtime_error("Invalid SH degree");value.displaySHDegree=int32_t(n); }
            else if(k=="warmupSeconds") value.warmupSeconds=number(v); else if(k=="sampleSeconds") value.sampleSeconds=number(v); else if(k=="captureSettleSeconds") value.captureSettleSeconds=number(v);
            else if(k=="captureTimes") { if(v.kind!=Value::Array) throw std::runtime_error("Expected capture times");value.captureTimes=v.array; }
            else if(k=="qualityPixels") value.lod.qualityPixels=float(number(v)); else if(k=="hysteresis") value.lod.hysteresis=float(number(v)); else if(k=="freeze") value.lod.freeze=boolean(v);
            else if(k=="sceneGPUBytes") value.streaming.sceneGPUBytes=integer(v,1ull<<48); else if(k=="cpuCacheBytes") value.streaming.cpuCacheBytes=integer(v,1ull<<48); else if(k=="stagingBytes") value.streaming.stagingBytes=integer(v,1ull<<48); else if(k=="temporaryGPUBytes") value.streaming.temporaryGPUBytes=integer(v,1ull<<48); else if(k=="maxUploadBytesPerFrame") value.streaming.maxUploadBytesPerFrame=integer(v,1ull<<48);
            else if(k=="maxConcurrentReads") value.streaming.maxConcurrentReads=uint32_t(integer(v,64)); else if(k=="retries") value.streaming.retries=uint32_t(integer(v,32)); else if(k=="retryDelayMilliseconds") value.streaming.retryDelayMilliseconds=uint32_t(integer(v,60000));
            else throw std::runtime_error("Unknown runtime config field: "+k);
        }
        if(!value.Validate(error)) return false; options=std::move(value);error.clear();return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}

} // namespace MonsterRender::Splat
