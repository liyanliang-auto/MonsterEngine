// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatLODBuilder.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include "Renderer/Splat/LOD/SplatProxyMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
struct Proxy { FSplatGaussian g; double geometry=0, appearance=0; std::array<uint32_t,4> members{}; uint32_t memberCount=0; };
double Area(const FSplatGaussian& g) { return FSplatProxyMath::Area(g); }
FSplatBounds Support(const FSplatGaussian& g,double sigma) { return FSplatProxyMath::Support(g,sigma); }
void Extend(FSplatBounds& a,const FSplatBounds& b) { FSplatProxyMath::Extend(a,b); }
double Appearance(const FSplatGaussian& a,const FSplatGaussian& b,uint32_t stride) { return FSplatProxyMath::Appearance(a,b,stride); }
uint64_t Morton(const FSplatGaussian& g,const FSplatBounds& b) { return FSplatProxyMath::Morton(g,b); }
Proxy Merge(const Proxy& a,const Proxy& b,uint32_t stride,double sigma,uint64_t id) {
    Proxy p;
    if (a.memberCount && b.memberCount && a.memberCount+b.memberCount<=4) {
        p.memberCount=a.memberCount+b.memberCount;
        for(uint32_t i=0;i<a.memberCount;++i)p.members[i]=a.members[i];
        for(uint32_t i=0;i<b.memberCount;++i)p.members[a.memberCount+i]=b.members[i];
    }
    FSplatProxyMath::MergeLegacy(a.g,b.g,a.geometry,a.appearance,b.geometry,b.appearance,
        stride,sigma,id,p.g,p.geometry,p.appearance);
    return p;
}
class Construction {
public:
    const std::vector<FSplatGaussian>& source; const FSplatLODBuildConfig& cfg; uint32_t stride; std::filesystem::path dir; FSplatSceneManifest m;
    std::vector<uint32_t> indices,scratch; uint64_t proxyId=0;
    Construction(const std::vector<FSplatGaussian>& s,const FSplatLODBuildConfig& c,uint32_t degree,std::filesystem::path d):source(s),cfg(c),stride(3*(degree+1)*(degree+1)),dir(std::move(d)){m.shDegree=degree;indices.resize(s.size());scratch.resize(s.size());std::iota(indices.begin(),indices.end(),0u);}
    void Pages(uint32_t node,const std::vector<Proxy>& rep){m.nodes[node].gaussianCount=uint32_t(rep.size());
        for(size_t start=0;start<rep.size();start+=cfg.maxPageGaussians){size_t end=std::min(rep.size(),start+cfg.maxPageGaussians);std::vector<FSplatGaussian> records;records.reserve(end-start);for(size_t i=start;i<end;++i)records.push_back(rep[i].g);
            std::vector<uint8_t> bytes;std::string error;if(!FSplatSceneFormat::EncodePage(records,m.shDegree,bytes,error))throw std::runtime_error(error);
            if(m.pages.size()>=SplatInvalidId)throw std::runtime_error("Page identifier overflow");FSplatPageInfo p;p.id=uint32_t(m.pages.size());p.relativePath="pages/page_"+std::to_string(p.id)+".mgsp";p.byteSize=bytes.size();p.decodedByteSize=records.size()*sizeof(FSplatGaussian);p.gaussianCount=uint32_t(records.size());p.sha256=FSplatSceneFormat::SHA256(bytes);p.pinned=node==0;
            std::ofstream f(dir/p.relativePath,std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("Cannot create page");f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));f.close();if(!f)throw std::runtime_error("Page write failed");m.totalDataBytes+=p.byteSize;m.nodes[node].pageIds.push_back(p.id);m.pages.push_back(std::move(p));}
    }
    std::vector<Proxy> Reduce(std::vector<Proxy> input,const FSplatBounds& bounds){
        const size_t target=std::max<size_t>(1,size_t(std::ceil(input.size()*double(cfg.proxyReduction))));
        while(input.size()>target){std::vector<uint64_t> keys(input.size());std::vector<double> areas(input.size());for(size_t i=0;i<input.size();++i){keys[i]=Morton(input[i].g,bounds);areas[i]=Area(input[i].g);}
            std::vector<size_t> order(input.size());std::iota(order.begin(),order.end(),size_t(0));std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return keys[a]<keys[b];});
            std::vector<bool> used(input.size(),false);std::vector<Proxy> next;next.reserve(std::max(target,(input.size()+1)/2));size_t merges=std::min(input.size()-target,input.size()/2);
            for(size_t oi=0;oi<order.size();++oi){size_t a=order[oi];if(used[a])continue;size_t best=input.size();double bestCost=INFINITY;
                if(merges)for(size_t oj=oi+1;oj<std::min(order.size(),oi+17);++oj){size_t b=order[oj];if(used[b])continue;double spatial=0,shape=0;for(int k=0;k<3;++k){double d=double(input[a].g.position[k])-input[b].g.position[k];spatial+=d*d;shape+=std::abs(std::log(double(input[a].g.scale[k])/input[b].g.scale[k]));}
                    double app=Appearance(input[a].g,input[b].g,stride);double tolerance=std::max(1e-6,double(cfg.maxAppearanceError));double cost=spatial/(areas[a]+areas[b])+0.1*shape+cfg.appearanceWeight*(app/tolerance)*(app/tolerance);
                    if(cost<bestCost){bestCost=cost;best=b;}}
                used[a]=true;if(best<input.size()){used[best]=true;--merges;next.push_back(Merge(input[a],input[best],stride,cfg.supportSigma,proxyId++));}else next.push_back(std::move(input[a]));}
            if(next.size()>=input.size())throw std::runtime_error("Proxy reduction failed to progress");input=std::move(next);}
        return input;
    }
    std::pair<uint32_t,std::vector<Proxy>> Node(size_t begin,size_t end,uint32_t level){
        if(m.nodes.size()>=SplatInvalidId)throw std::runtime_error("Node identifier overflow");uint32_t id=uint32_t(m.nodes.size());m.nodes.emplace_back();m.nodes[id].level=level;
        FSplatBounds center,support;for(int k=0;k<3;++k){center.min[k]=center.max[k]=source[indices[begin]].position[k];}support=Support(source[indices[begin]],cfg.supportSigma);
        for(size_t i=begin+1;i<end;++i){auto& g=source[indices[i]];for(int k=0;k<3;++k){center.min[k]=std::min(center.min[k],g.position[k]);center.max[k]=std::max(center.max[k],g.position[k]);}Extend(support,Support(g,cfg.supportSigma));}
        bool distinct=false;double mid[3];for(int k=0;k<3;++k){mid[k]=(double(center.min[k])+center.max[k])/2;distinct|=center.min[k]<center.max[k];}
        std::vector<Proxy> rep;
        if(end-begin>cfg.maxLeafGaussians&&level<cfg.maxDepth&&distinct){std::array<size_t,8> counts{},starts{},cursor{};auto octant=[&](uint32_t i){unsigned o=0;for(int k=0;k<3;++k)if(source[i].position[k]>mid[k])o|=1u<<k;return o;};for(size_t i=begin;i<end;++i)++counts[octant(indices[i])];size_t pos=begin;for(int o=0;o<8;++o){starts[o]=cursor[o]=pos;pos+=counts[o];}for(size_t i=begin;i<end;++i)scratch[cursor[octant(indices[i])]++]=indices[i];std::copy(scratch.begin()+begin,scratch.begin()+end,indices.begin()+begin);
            for(int o=0;o<8;++o)if(counts[o]){auto child=Node(starts[o],starts[o]+counts[o],level+1);m.nodes[id].children.push_back(child.first);Extend(support,m.nodes[child.first].bounds);rep.insert(rep.end(),std::make_move_iterator(child.second.begin()),std::make_move_iterator(child.second.end()));}
            // Quantize centers in their own range, not the often much larger
            // anisotropic support range; support outliers must not erase locality.
            const bool leafParent=cfg.projectedFusionGroups&&std::all_of(m.nodes[id].children.begin(),m.nodes[id].children.end(),[&](uint32_t child){return m.nodes[child].children.empty();});
            if (leafParent) for (uint32_t row=0;row<rep.size();++row) { rep[row].members[0]=row;rep[row].memberCount=1; }
            rep=Reduce(std::move(rep),center);m.proxyCount+=rep.size();
            if (leafParent && std::all_of(rep.begin(),rep.end(),[](const Proxy& p){return p.memberCount>0;})) {
                auto& groups=m.nodes[id].originalFusionGroups;groups.reserve(rep.size());
                for(const auto& p:rep){FSplatOriginalFusionGroup group;group.count=p.memberCount;std::copy_n(p.members.begin(),p.memberCount,group.rows);groups.push_back(group);}
            }
        }else{rep.reserve(end-begin);for(size_t i=begin;i<end;++i)rep.push_back({source[indices[i]],0,0});m.leafCount+=rep.size();}
        m.nodes[id].centerBounds=center;for(auto c:m.nodes[id].children)Extend(m.nodes[id].centerBounds,m.nodes[c].centerBounds);
        double ge=0,ae=0,mass=0;for(const auto& p:rep){Extend(support,Support(p.g,cfg.supportSigma));for(int k=0;k<3;++k){m.nodes[id].centerBounds.min[k]=std::min(m.nodes[id].centerBounds.min[k],p.g.position[k]);m.nodes[id].centerBounds.max[k]=std::max(m.nodes[id].centerBounds.max[k],p.g.position[k]);}FSplatLODBuilder::AccumulateCoverage(m.nodes[id],p.g);const double weight=std::max(1e-30,double(p.g.opacity)*Area(p.g));mass+=weight;ge+=weight*p.geometry*p.geometry;ae+=weight*p.appearance*p.appearance;}
        ge=std::sqrt(ge/std::max(1e-30,mass));ae=std::sqrt(ae/std::max(1e-30,mass));
        for(auto c:m.nodes[id].children){ge=std::max(ge,double(m.nodes[c].geometricError)/cfg.geometricErrorScale);ae=std::max(ae,double(m.nodes[c].appearanceError));}
        m.nodes[id].bounds=support;m.nodes[id].geometricError=std::nextafter(float(ge*cfg.geometricErrorScale),INFINITY);m.nodes[id].appearanceError=float(ae);if(m.nodes[id].children.empty())m.nodes[id].geometricError=0;
        Pages(id,rep);return {id,std::move(rep)};
    }
};
}
std::string FSplatLODBuilder::CanonicalConfig(const FSplatLODBuildConfig& c){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(std::numeric_limits<float>::max_digits10)<<"{\"maxLeafGaussians\":"<<c.maxLeafGaussians<<",\"maxDepth\":"<<c.maxDepth<<",\"maxPageGaussians\":"<<c.maxPageGaussians<<",\"supportSigma\":"<<c.supportSigma<<",\"proxyReduction\":"<<c.proxyReduction<<",\"geometricErrorScale\":"<<c.geometricErrorScale<<",\"appearanceWeight\":"<<c.appearanceWeight<<",\"maxAppearanceError\":"<<c.maxAppearanceError<<",\"projectedFusionGroups\":"<<(c.projectedFusionGroups?1:0)<<"}";return s.str();}
void FSplatLODBuilder::AccumulateCoverage(FSplatLODNode& node,const FSplatGaussian& g) {
    node.coverageAreaUpper+=FSplatProxyMath::Coverage(g);
}
bool FSplatLODBuilder::BuildOriginalFusionGroups(const std::vector<FSplatGaussian>& originals,
    const FSplatBounds& centers,const FSplatLODBuildConfig& config,
    std::vector<FSplatOriginalFusionGroup>& output,std::string& error,uint32_t shDegree) {
    try {
        if(shDegree>3||originals.empty()||originals.size()>UINT32_MAX||!std::isfinite(config.proxyReduction)||
            config.proxyReduction<0.25f||config.proxyReduction>=1||!std::isfinite(config.maxAppearanceError)||
            config.maxAppearanceError<=0||!std::isfinite(config.appearanceWeight)||config.appearanceWeight<0)
            throw std::runtime_error("Invalid original fusion grouping configuration");
        std::vector<Proxy> input;input.reserve(originals.size());
        for(uint32_t i=0;i<originals.size();++i){Proxy p{originals[i],0,0};p.members[0]=i;p.memberCount=1;input.push_back(p);}
        Construction build(originals,config,shDegree,{});
        auto reduced=build.Reduce(std::move(input),centers);
        std::vector<FSplatOriginalFusionGroup> result;result.reserve(reduced.size());
        for(const auto& p:reduced){if(!p.memberCount)throw std::runtime_error("Original group exceeds four members");FSplatOriginalFusionGroup g;g.count=p.memberCount;std::copy_n(p.members.begin(),p.memberCount,g.rows);result.push_back(g);}
        output=std::move(result);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool FSplatLODBuilder::Build(const std::vector<FSplatGaussian>& source,uint32_t degree,const FSplatLODBuildConfig& cfg,const std::string& outputDirectory,FSplatSceneManifest& output,std::string& error){
    if(!std::isfinite(cfg.supportSigma)||cfg.supportSigma<3){error="supportSigma must be at least three to contain the renderer footprint";return false;}
    try{if(degree>3||source.size()>=SplatInvalidId||!cfg.maxLeafGaussians||!cfg.maxPageGaussians||cfg.maxPageGaussians>1000000||cfg.maxDepth>64||!std::isfinite(cfg.supportSigma)||cfg.supportSigma<=0||!std::isfinite(cfg.proxyReduction)||cfg.proxyReduction<=0||cfg.proxyReduction>=1||!std::isfinite(cfg.geometricErrorScale)||cfg.geometricErrorScale<1||!std::isfinite(cfg.appearanceWeight)||cfg.appearanceWeight<0||!std::isfinite(cfg.maxAppearanceError)||cfg.maxAppearanceError<=0)throw std::runtime_error("Invalid build configuration");
        for(const auto& g:source){for(float x:g.position)if(!std::isfinite(x))throw std::runtime_error("Nonfinite position");for(int k=0;k<3;++k)if(!std::isfinite(g.scale[k])||g.scale[k]<=0)throw std::runtime_error("Invalid activated scale");double norm=0;for(float x:g.rotation){if(!std::isfinite(x))throw std::runtime_error("Invalid quaternion");norm+=double(x)*x;}if(std::abs(norm-1)>1e-4)throw std::runtime_error("Quaternion is not normalized");if(!std::isfinite(g.opacity)||g.opacity<0||g.opacity>1)throw std::runtime_error("Invalid activated opacity");for(float x:g.sh)if(!std::isfinite(x))throw std::runtime_error("Invalid SH coefficient");}
        std::filesystem::path dir(outputDirectory);if(dir.empty()||std::filesystem::exists(dir))throw std::runtime_error("Output directory must be new; existing output is never overwritten");
        std::filesystem::create_directories(dir/"pages");Construction build(source,cfg,degree,dir);build.m.version=cfg.projectedFusionGroups?4:3;build.m.builderVersion=cfg.projectedFusionGroups?SplatFusionBuilderVersion:SplatCenterErrorBuilderVersion;build.m.sourceCount=source.size();build.m.supportSigma=cfg.supportSigma;build.m.sourceSHA256=cfg.sourceSHA256;build.m.builderHash=cfg.builderHash;build.m.buildConfig=CanonicalConfig(cfg);build.m.configHash=FSplatSceneFormat::SHA256(std::vector<uint8_t>(build.m.buildConfig.begin(),build.m.buildConfig.end()));
        if(!source.empty())build.m.rootNode=build.Node(0,source.size(),0).first;
        uint32_t maximumDepth=0;for(const auto& node:build.m.nodes)maximumDepth=std::max(maximumDepth,node.level);
        std::ofstream report(dir/"build-report.json");if(!report)throw std::runtime_error("Cannot create build report");report<<"{\"sourceCount\":"<<build.m.sourceCount<<",\"leafCount\":"<<build.m.leafCount<<",\"proxyCount\":"<<build.m.proxyCount<<",\"nodes\":"<<build.m.nodes.size()<<",\"pages\":"<<build.m.pages.size()<<",\"totalDataBytes\":"<<build.m.totalDataBytes<<",\"sourceSHA256\":\""<<build.m.sourceSHA256<<"\",\"builderHash\":\""<<build.m.builderHash<<"\",\"configHash\":\""<<build.m.configHash<<"\",\"mergeReference\":\"https://github.com/playcanvas/splat-transform/blob/435b972a97a278c655d8f7015b1355215cba0d71/src/lib/decimate/moment-match.ts\",\"referenceLicense\":\"MIT; independently implemented mathematical formulas, no source copied\",\"opacityPolicy\":\"Thomsen-area-alpha mass over proxy area, capped at one; not exact alpha-compositing equivalence\",\"proxyPolicy\":\"local-morton-appearance-covariance-moment-match\",\"appearanceTolerancePolicy\":\"cost-normalizer-not-hard-rejection\",\"maximumDepth\":"<<maximumDepth<<",\"rootGaussianCount\":"<<(build.m.rootNode==SplatInvalidId?0:build.m.nodes[build.m.rootNode].gaussianCount)<<",\"offlineWorstCaseWorkingBytesEstimate\":"<<(source.size()*(sizeof(FSplatGaussian)+4*sizeof(Proxy)+48))<<",\"config\":"<<build.m.buildConfig<<"}\n";report.close();if(!report)throw std::runtime_error("Build report write failed");if(!FSplatSceneFormat::SaveManifest((dir/"scene.mgs").string(),build.m,error))throw std::runtime_error(error);output=std::move(build.m);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
