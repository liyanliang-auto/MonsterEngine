// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatLODBuilder.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
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
using Matrix = std::array<std::array<double,3>,3>;
struct Proxy { FSplatGaussian g; double geometry=0, appearance=0; std::array<uint32_t,4> members{}; uint32_t memberCount=0; };
Matrix Rotation(const FSplatGaussian& g) {
    const double w=g.rotation[0],x=g.rotation[1],y=g.rotation[2],z=g.rotation[3];
    return {{{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},
             {2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},
             {2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}}};
}
Matrix Covariance(const FSplatGaussian& g) {
    const auto r=Rotation(g); Matrix c{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)
        c[i][j]+=r[i][k]*r[j][k]*double(g.scale[k])*g.scale[k];
    return c;
}
Matrix CovarianceRoot(const FSplatGaussian& g) {
    const auto r=Rotation(g); Matrix c{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)
        c[i][j]+=r[i][k]*r[j][k]*double(g.scale[k]);
    return c;
}
double Area(const FSplatGaussian& g) {
    // Knud Thomsen ellipsoid surface-area approximation. The constant 4*pi
    // cancels from both normalized weights and merged opacity.
    constexpr double p=1.6075;
    return std::max(1e-30,std::pow((std::pow(double(g.scale[0])*g.scale[1],p)+
        std::pow(double(g.scale[1])*g.scale[2],p)+std::pow(double(g.scale[2])*g.scale[0],p))/3,1/p));
}
// Jacobi symmetric eigensolver, followed by a proper rotation/quaternion conversion.
void SetCovariance(FSplatGaussian& g, Matrix a) {
    Matrix v{{{1,0,0},{0,1,0},{0,0,1}}};
    for(int sweep=0;sweep<32;++sweep) {
        int p=0,q=1; for(int i=0;i<3;++i)for(int j=i+1;j<3;++j)if(std::abs(a[i][j])>std::abs(a[p][q])){p=i;q=j;}
        if(std::abs(a[p][q])<=1e-14*std::max(1e-30,a[0][0]+a[1][1]+a[2][2]))break;
        double t=0.5*std::atan2(2*a[p][q],a[q][q]-a[p][p]),c=std::cos(t),s=std::sin(t);
        // Columns p,q rotate by [c,-s; s,c].
        for(int k=0;k<3;++k){double ap=a[k][p],aq=a[k][q];a[k][p]=c*ap-s*aq;a[k][q]=s*ap+c*aq;}
        for(int k=0;k<3;++k){double ap=a[p][k],aq=a[q][k];a[p][k]=c*ap-s*aq;a[q][k]=s*ap+c*aq;}
        for(int k=0;k<3;++k){double vp=v[k][p],vq=v[k][q];v[k][p]=c*vp-s*vq;v[k][q]=s*vp+c*vq;}
    }
    for(int k=0;k<3;++k)g.scale[k]=float(std::sqrt(std::max(1e-30,a[k][k])));
    double quat[4]; const double tr=v[0][0]+v[1][1]+v[2][2];
    if(tr>0){double s=2*std::sqrt(tr+1);quat[0]=s/4;quat[1]=(v[2][1]-v[1][2])/s;quat[2]=(v[0][2]-v[2][0])/s;quat[3]=(v[1][0]-v[0][1])/s;}
    else {int i=0;if(v[1][1]>v[i][i])i=1;if(v[2][2]>v[i][i])i=2;int j=(i+1)%3,k=(i+2)%3;double s=2*std::sqrt(std::max(1e-30,1+v[i][i]-v[j][j]-v[k][k]));quat[0]=(v[k][j]-v[j][k])/s;quat[i+1]=s/4;quat[j+1]=(v[j][i]+v[i][j])/s;quat[k+1]=(v[k][i]+v[i][k])/s;}
    double n=0;for(double x:quat)n+=x*x;n=std::sqrt(n);for(int i=0;i<4;++i)g.rotation[i]=float(quat[i]/n);
}
FSplatBounds Support(const FSplatGaussian& g,double sigma) {
    const auto c=Covariance(g); FSplatBounds b;
    for(int k=0;k<3;++k){double r=sigma*std::sqrt(std::max(0.0,c[k][k]));
        b.min[k]=std::nextafter(float(double(g.position[k])-r),-INFINITY);
        b.max[k]=std::nextafter(float(double(g.position[k])+r),INFINITY);
        if(!std::isfinite(b.min[k])||!std::isfinite(b.max[k]))throw std::runtime_error("Gaussian support exceeds finite FP32 bounds");}
    return b;
}
void Extend(FSplatBounds& a,const FSplatBounds& b){for(int k=0;k<3;++k){a.min[k]=std::min(a.min[k],b.min[k]);a.max[k]=std::max(a.max[k],b.max[k]);}}
double Appearance(const FSplatGaussian& a,const FSplatGaussian& b,uint32_t stride) {
    double d=0;for(uint32_t k=0;k<stride;++k){double x=double(a.sh[k])-b.sh[k];d+=x*x;}return std::sqrt(d/std::max(1u,stride));
}
Proxy Merge(const Proxy& a,const Proxy& b,uint32_t stride,double sigma,uint64_t id) {
    Proxy p;
    if (a.memberCount && b.memberCount && a.memberCount + b.memberCount <= 4) {
        p.memberCount = a.memberCount + b.memberCount;
        for (uint32_t i=0;i<a.memberCount;++i) p.members[i]=a.members[i];
        for (uint32_t i=0;i<b.memberCount;++i) p.members[a.memberCount+i]=b.members[i];
    }
    double ma=std::max(1e-30,double(a.g.opacity)*Area(a.g)),mb=std::max(1e-30,double(b.g.opacity)*Area(b.g));double wa=ma/(ma+mb),wb=1-wa;
    Matrix ca=Covariance(a.g),cb=Covariance(b.g),c{};
    for(int k=0;k<3;++k)p.g.position[k]=float(wa*a.g.position[k]+wb*b.g.position[k]);
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){double ai=double(a.g.position[i])-p.g.position[i],aj=double(a.g.position[j])-p.g.position[j];double bi=double(b.g.position[i])-p.g.position[i],bj=double(b.g.position[j])-p.g.position[j];c[i][j]=wa*(ca[i][j]+ai*aj)+wb*(cb[i][j]+bi*bj);}
    SetCovariance(p.g,c);p.g.opacity=float(std::min(1.0,(ma+mb)/Area(p.g)));p.g.originalId=(uint64_t(1)<<63)|id;
    for(uint32_t k=0;k<stride;++k)p.g.sh[k]=float(wa*a.g.sh[k]+wb*b.g.sh[k]);
    const Matrix actualRoot=CovarianceRoot(p.g);
    double geometrySquared=0,appearanceSquared=0;
    for(const auto* child:{&a,&b}){double dist=0,diff=0;auto cc=CovarianceRoot(child->g);for(int i=0;i<3;++i){double d=double(child->g.position[i])-p.g.position[i];dist+=d*d;for(int j=0;j<3;++j){double v=actualRoot[i][j]-cc[i][j];diff+=v*v;}}
        // Coupling both Gaussians to one standard normal gives a transport RMS
        // upper bound. Triangle inequality carries each child's prior error.
        // This measures distribution discrepancy, not maximum support motion.
        const double geometry=child->geometry+std::sqrt(dist+diff);
        const double radius=sigma*std::sqrt(double(child->g.scale[0])*child->g.scale[0]+
            double(child->g.scale[1])*child->g.scale[1]+double(child->g.scale[2])*child->g.scale[2]);
        const double appearance=child->appearance+radius*(Appearance(child->g,p.g,stride)+
            std::abs(double(child->g.opacity)-p.g.opacity));
        const double weight=child==&a?wa:wb;
        geometrySquared+=weight*geometry*geometry;
        appearanceSquared+=weight*appearance*appearance;
    }
    p.geometry=std::sqrt(geometrySquared);p.appearance=std::sqrt(appearanceSquared);
    return p;
}
uint64_t Morton(const FSplatGaussian& g,const FSplatBounds& b){uint32_t q[3];for(int k=0;k<3;++k){double extent=double(b.max[k])-b.min[k];double t=extent>0?(double(g.position[k])-b.min[k])/extent:0;q[k]=uint32_t(std::clamp(t,0.0,1.0)*2097151);}
    uint64_t key=0;for(int bit=0;bit<21;++bit)for(int k=0;k<3;++k)key|=uint64_t((q[k]>>bit)&1)<<(3*bit+k);return key;}
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
    const double products[3]={double(g.scale[1])*g.scale[2],double(g.scale[0])*g.scale[2],double(g.scale[0])*g.scale[1]};
    node.coverageAreaUpper+=double(g.opacity)*std::max({products[0],products[1],products[2]});
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
