// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatProxyMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
using Matrix = std::array<std::array<double,3>,3>;
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
double GaussianArea(const FSplatGaussian& g) {
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
FSplatBounds GaussianSupport(const FSplatGaussian& g,double sigma) {
    const auto c=Covariance(g); FSplatBounds b;
    for(int k=0;k<3;++k){double r=sigma*std::sqrt(std::max(0.0,c[k][k]));
        b.min[k]=std::nextafter(float(double(g.position[k])-r),-INFINITY);
        b.max[k]=std::nextafter(float(double(g.position[k])+r),INFINITY);
        if(!std::isfinite(b.min[k])||!std::isfinite(b.max[k]))throw std::runtime_error("Gaussian support exceeds finite FP32 bounds");}
    return b;
}
void ExtendBounds(FSplatBounds& a,const FSplatBounds& b){for(int k=0;k<3;++k){a.min[k]=std::min(a.min[k],b.min[k]);a.max[k]=std::max(a.max[k],b.max[k]);}}
double SHAppearance(const FSplatGaussian& a,const FSplatGaussian& b,uint32_t stride) {
    double d=0;for(uint32_t k=0;k<stride;++k){double x=double(a.sh[k])-b.sh[k];d+=x*x;}return std::sqrt(d/std::max(1u,stride));
}
void MergeMoments(const FSplatGaussian& ga,const FSplatGaussian& gb,
    double geometryA,double appearanceA,double geometryB,double appearanceB,
    double ma,double mb,uint32_t stride,double sigma,uint64_t id,
    FSplatGaussian& output,double& geometry,double& appearance) {
    struct Input { const FSplatGaussian& g; double geometry, appearance; };
    const Input a{ga,geometryA,appearanceA},b{gb,geometryB,appearanceB};
    struct { FSplatGaussian g; double geometry=0,appearance=0; } p;
    double wa=ma/(ma+mb),wb=1-wa;
    Matrix ca=Covariance(a.g),cb=Covariance(b.g),c{};
    for(int k=0;k<3;++k)p.g.position[k]=float(wa*a.g.position[k]+wb*b.g.position[k]);
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){double ai=double(a.g.position[i])-p.g.position[i],aj=double(a.g.position[j])-p.g.position[j];double bi=double(b.g.position[i])-p.g.position[i],bj=double(b.g.position[j])-p.g.position[j];c[i][j]=wa*(ca[i][j]+ai*aj)+wb*(cb[i][j]+bi*bj);}
    SetCovariance(p.g,c);p.g.opacity=float(std::min(1.0,(ma+mb)/GaussianArea(p.g)));p.g.originalId=(uint64_t(1)<<63)|id;
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
        const double appearance=child->appearance+radius*(SHAppearance(child->g,p.g,stride)+
            std::abs(double(child->g.opacity)-p.g.opacity));
        const double weight=child==&a?wa:wb;
        geometrySquared+=weight*geometry*geometry;
        appearanceSquared+=weight*appearance*appearance;
    }
    p.geometry=std::sqrt(geometrySquared);p.appearance=std::sqrt(appearanceSquared);
    output=p.g; geometry=p.geometry; appearance=p.appearance;
}
uint64_t MortonKey(const FSplatGaussian& g,const FSplatBounds& b){uint32_t q[3];for(int k=0;k<3;++k){double extent=double(b.max[k])-b.min[k];double t=extent>0?(double(g.position[k])-b.min[k])/extent:0;q[k]=uint32_t(std::clamp(t,0.0,1.0)*2097151);}
    uint64_t key=0;for(int bit=0;bit<21;++bit)for(int k=0;k<3;++k)key|=uint64_t((q[k]>>bit)&1)<<(3*bit+k);return key;}

void ValidateGaussian(const FSplatGaussian& g) {
    for (float value : g.position) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite proxy position");
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(g.scale[axis]) || g.scale[axis] <= 0) throw std::runtime_error("Invalid activated proxy scale");
    double norm = 0;
    for (float value : g.rotation) {
        if (!std::isfinite(value)) throw std::runtime_error("Invalid proxy quaternion");
        norm += double(value) * value;
    }
    if (std::abs(norm - 1) > 1e-4) throw std::runtime_error("Proxy quaternion is not normalized");
    if (!std::isfinite(g.opacity) || g.opacity < 0 || g.opacity > 1) throw std::runtime_error("Invalid activated proxy opacity");
    for (float value : g.sh) if (!std::isfinite(value)) throw std::runtime_error("Invalid proxy SH coefficient");
}
void ValidateWeighted(const FSplatWeightedGaussian& record) {
    ValidateGaussian(record.gaussian);
    if (!std::isfinite(record.geometryErrorSquared) || record.geometryErrorSquared < 0 ||
        !std::isfinite(record.appearanceErrorSquared) || record.appearanceErrorSquared < 0 ||
        !std::isfinite(record.weight) || record.weight <= 0)
        throw std::runtime_error("Invalid cumulative proxy statistics");
}
void ValidateMergePolicy(uint32_t stride, double sigma) {
    if ((stride != 3 && stride != 12 && stride != 27 && stride != 48) || !std::isfinite(sigma) || sigma < 3)
        throw std::runtime_error("Invalid proxy merge stride or support multiplier");
}
double GaussianCoverage(const FSplatGaussian& g) {
    const double products[3] = {double(g.scale[1])*g.scale[2], double(g.scale[0])*g.scale[2], double(g.scale[0])*g.scale[1]};
    return double(g.opacity)*std::max({products[0],products[1],products[2]});
}
void IncludeGaussian(FSplatProxySummary& summary, const FSplatWeightedGaussian& record, double sigma) {
    const auto bounds = GaussianSupport(record.gaussian, sigma);
    FSplatBounds centers;
    for (int axis = 0; axis < 3; ++axis) centers.min[axis] = centers.max[axis] = record.gaussian.position[axis];
    if (!summary.hasBounds) {
        summary.supportBounds = bounds; summary.centerBounds = centers; summary.hasBounds = true;
    } else {
        ExtendBounds(summary.supportBounds, bounds); ExtendBounds(summary.centerBounds, centers);
    }
    summary.geometricError = std::max(summary.geometricError, std::sqrt(record.geometryErrorSquared));
    summary.appearanceError = std::max(summary.appearanceError, std::sqrt(record.appearanceErrorSquared));
}
uint64_t CheckedAdd(uint64_t a, uint64_t b) {
    if (a > std::numeric_limits<uint64_t>::max() - b) throw std::runtime_error("Proxy accounting overflow");
    return a + b;
}
uint64_t CheckedMultiply(uint64_t a, uint64_t b) {
    if (b && a > std::numeric_limits<uint64_t>::max() / b) throw std::runtime_error("Proxy accounting overflow");
    return a * b;
}
class FControlPoll {
public:
    FControlPoll(FSplatBuildControl& signal, uint32_t interval) : signal(signal), interval(std::min(interval, 256u)) {}
    void Now() {
        switch (signal.Poll()) {
        case ESplatBuildSignal::Pause: throw std::runtime_error("paused");
        case ESplatBuildSignal::Cancel: throw std::runtime_error("cancelled");
        default: break;
        }
    }
    void Step() { if (++steps >= interval) { steps = 0; Now(); } }
private:
    FSplatBuildControl& signal;
    uint32_t interval, steps = 0;
};
} // namespace

double FSplatProxyMath::Area(const FSplatGaussian& gaussian) { return GaussianArea(gaussian); }
FSplatBounds FSplatProxyMath::Support(const FSplatGaussian& gaussian, double sigma) {
    if (!std::isfinite(sigma) || sigma < 3) throw std::runtime_error("Invalid proxy support multiplier");
    return GaussianSupport(gaussian, sigma);
}
void FSplatProxyMath::Extend(FSplatBounds& destination, const FSplatBounds& source) { ExtendBounds(destination, source); }
double FSplatProxyMath::Appearance(const FSplatGaussian& a, const FSplatGaussian& b, uint32_t stride) {
    if (stride > 48) throw std::runtime_error("Proxy SH stride exceeds storage");
    return SHAppearance(a,b,stride);
}
double FSplatProxyMath::Coverage(const FSplatGaussian& gaussian) { return GaussianCoverage(gaussian); }
uint64_t FSplatProxyMath::Morton(const FSplatGaussian& gaussian, const FSplatBounds& centers) { return MortonKey(gaussian,centers); }

FSplatWeightedGaussian FSplatProxyMath::FromGaussian(const FSplatGaussian& gaussian) {
    ValidateGaussian(gaussian);
    FSplatWeightedGaussian record;
    record.gaussian = gaussian;
    record.weight = std::max(1e-30, double(gaussian.opacity)*GaussianArea(gaussian));
    ValidateWeighted(record); return record;
}

void FSplatProxyMath::MergeLegacy(const FSplatGaussian& a, const FSplatGaussian& b,
    double geometryA, double appearanceA, double geometryB, double appearanceB,
    uint32_t stride, double sigma, uint64_t id,
    FSplatGaussian& output, double& geometry, double& appearance) {
    // Preserve original evaluation order and reconstructed opacity-area masses.
    const double ma = std::max(1e-30,double(a.opacity)*GaussianArea(a));
    const double mb = std::max(1e-30,double(b.opacity)*GaussianArea(b));
    MergeMoments(a,b,geometryA,appearanceA,geometryB,appearanceB,ma,mb,stride,sigma,id,output,geometry,appearance);
}

FSplatWeightedGaussian FSplatProxyMath::Merge(const FSplatWeightedGaussian& a,
    const FSplatWeightedGaussian& b, uint32_t stride, double sigma, uint64_t id) {
    ValidateMergePolicy(stride,sigma); ValidateWeighted(a); ValidateWeighted(b);
    if (id >= (uint64_t(1) << 63)) throw std::runtime_error("Proxy identifier exceeds reserved namespace");
    FSplatWeightedGaussian result;
    result.weight = a.weight + b.weight;
    if (!std::isfinite(result.weight)) throw std::runtime_error("Proxy mass overflow");
    double geometry, appearance;
    MergeMoments(a.gaussian,b.gaussian,std::sqrt(a.geometryErrorSquared),std::sqrt(a.appearanceErrorSquared),
        std::sqrt(b.geometryErrorSquared),std::sqrt(b.appearanceErrorSquared),a.weight,b.weight,stride,sigma,id,
        result.gaussian,geometry,appearance);
    result.geometryErrorSquared = geometry*geometry;
    result.appearanceErrorSquared = appearance*appearance;
    ValidateWeighted(result); return result;
}

void FSplatProxyMath::ExtendSummary(FSplatProxySummary& destination, const FSplatProxySummary& source) {
    if (!source.hasBounds) return;
    if (!std::isfinite(source.coverageAreaUpper) || source.coverageAreaUpper < 0 ||
        !std::isfinite(source.geometricError) || source.geometricError < 0 ||
        !std::isfinite(source.appearanceError) || source.appearanceError < 0)
        throw std::runtime_error("Invalid inherited proxy summary");
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(source.supportBounds.min[axis]) || !std::isfinite(source.supportBounds.max[axis]) ||
            source.supportBounds.min[axis] > source.supportBounds.max[axis] ||
            !std::isfinite(source.centerBounds.min[axis]) || !std::isfinite(source.centerBounds.max[axis]) ||
            source.centerBounds.min[axis] > source.centerBounds.max[axis])
            throw std::runtime_error("Invalid inherited proxy bounds");
    FSplatProxySummary combined = destination;
    if (!combined.hasBounds) combined = source;
    else {
        ExtendBounds(combined.supportBounds,source.supportBounds);
        ExtendBounds(combined.centerBounds,source.centerBounds);
        combined.coverageAreaUpper += source.coverageAreaUpper;
        combined.geometricError = std::max(combined.geometricError,source.geometricError);
        combined.appearanceError = std::max(combined.appearanceError,source.appearanceError);
    }
    if (!std::isfinite(combined.coverageAreaUpper)) throw std::runtime_error("Proxy coverage overflow");
    destination = combined;
}

void FSplatProxyMath::PreserveDescendants(FSplatProxySummary& representation, const FSplatProxySummary& descendants) {
    const double coverage = std::max(representation.coverageAreaUpper,descendants.coverageAreaUpper);
    FSplatProxySummary combined = representation;
    ExtendSummary(combined,descendants);
    combined.coverageAreaUpper = coverage;
    representation = combined;
}

bool FSplatProxyMath::Reduce(std::span<const FSplatWeightedGaussian> input,
    uint32_t degree, uint32_t target, const FSplatBoundedBuildConfig& config,
    FSplatBuildControl& control, std::vector<FSplatWeightedGaussian>& output,
    FSplatProxySummary& summary, uint64_t& scratchBytes, std::string& error, uint64_t proxyIdBase) {
    scratchBytes = 0;
    try {
        if (degree > 3 || input.empty() || !target || target > config.maxProxyCount ||
            !config.leafCapacity || !config.maxProxyCount || !config.pollIntervalRecords ||
            input.size() > CheckedMultiply(8,std::max(config.leafCapacity,config.maxProxyCount)) ||
            input.size() > UINT32_MAX || !std::isfinite(config.appearanceWeight) || config.appearanceWeight < 0 ||
            !std::isfinite(config.maxAppearanceError) || config.maxAppearanceError < 0 ||
            !std::isfinite(config.supportSigma) || config.supportSigma < 3 || config.metadataBytes > config.workingMemoryBytes)
            throw std::runtime_error("Invalid bounded proxy reduction policy");
        const uint32_t stride = 3*(degree+1)*(degree+1);
        const size_t count = input.size(), finalCount = std::min<size_t>(target,count);
        if (proxyIdBase >= (uint64_t(1) << 63) || count > (uint64_t(1) << 63) - proxyIdBase)
            throw std::runtime_error("Insufficient proxy identifier range");
        // Two full ping-pong weighted arrays plus a compact final result; fixed
        // headroom accounts allocator bookkeeping and bounded introsort stack.
        const uint64_t arrays = CheckedMultiply(count,2*sizeof(FSplatWeightedGaussian)+sizeof(uint64_t)+sizeof(double)+sizeof(uint32_t)+sizeof(uint8_t));
        scratchBytes = CheckedAdd(CheckedAdd(arrays,CheckedMultiply(finalCount,sizeof(FSplatWeightedGaussian))),16384);
        const uint64_t borrowed = CheckedAdd(CheckedMultiply(count,sizeof(FSplatWeightedGaussian)),CheckedMultiply(output.capacity(),sizeof(FSplatWeightedGaussian)));
        if (CheckedAdd(scratchBytes,borrowed) > config.workingMemoryBytes-config.metadataBytes)
            throw std::runtime_error("Proxy reduction exceeds admitted workspace");
        FControlPoll poll(control,config.pollIntervalRecords); poll.Now();
        std::vector<FSplatWeightedGaussian> current, next;
        current.reserve(count); next.reserve(count);
        std::vector<uint64_t> keys(count);
        std::vector<double> areas(count);
        std::vector<uint32_t> order(count);
        std::vector<uint8_t> used(count);
        FSplatProxySummary resultSummary;
        double inputCoverage = 0;
        for (const auto& record : input) {
            poll.Step(); ValidateWeighted(record); current.push_back(record);
            IncludeGaussian(resultSummary,record,config.supportSigma);
            inputCoverage += GaussianCoverage(record.gaussian);
        }
        if (!std::isfinite(inputCoverage)) throw std::runtime_error("Proxy coverage overflow");
        resultSummary.coverageAreaUpper = inputCoverage;
        const FSplatBounds centers = resultSummary.centerBounds;
        uint64_t proxyId = proxyIdBase;
        while (current.size() > finalCount) {
            poll.Now(); const size_t size = current.size();
            for (size_t i = 0; i < size; ++i) {
                poll.Step(); keys[i] = MortonKey(current[i].gaussian,centers);
                areas[i] = GaussianArea(current[i].gaussian); order[i] = uint32_t(i); used[i] = 0;
            }
            // Explicit index tie-break produces the stable Morton order without
            // stable_sort's implementation-dependent auxiliary allocation.
            std::sort(order.begin(),order.begin()+size,[&](uint32_t a,uint32_t b) {
                poll.Step(); return keys[a] != keys[b] ? keys[a] < keys[b] : a < b;
            });
            next.clear(); size_t merges = std::min(size-finalCount,size/2);
            double coverage = 0;
            for (size_t oi = 0; oi < size; ++oi) {
                poll.Step(); const size_t a = order[oi]; if (used[a]) continue;
                size_t best = size; double bestCost = INFINITY;
                if (merges) for (size_t oj = oi+1; oj < std::min(size,oi+17); ++oj) {
                    poll.Step(); const size_t b = order[oj]; if (used[b]) continue;
                    const auto& ga = current[a].gaussian; const auto& gb = current[b].gaussian;
                    double spatial = 0, shape = 0;
                    for (int axis = 0; axis < 3; ++axis) {
                        const double delta = double(ga.position[axis])-gb.position[axis]; spatial += delta*delta;
                        shape += std::abs(std::log(double(ga.scale[axis])/gb.scale[axis]));
                    }
                    const double app = SHAppearance(ga,gb,stride), tolerance = std::max(1e-6,double(config.maxAppearanceError));
                    const double cost = spatial/(areas[a]+areas[b])+0.1*shape+config.appearanceWeight*(app/tolerance)*(app/tolerance);
                    if (!std::isfinite(cost)) throw std::runtime_error("Nonfinite proxy pairing cost");
                    if (cost < bestCost) { bestCost = cost; best = b; }
                }
                used[a] = 1;
                if (best < size) {
                    used[best] = 1; --merges;
                    next.push_back(Merge(current[a],current[best],stride,config.supportSigma,proxyId++));
                } else next.push_back(current[a]);
                IncludeGaussian(resultSummary,next.back(),config.supportSigma);
                coverage += GaussianCoverage(next.back().gaussian);
            }
            if (next.size() >= size) throw std::runtime_error("Bounded proxy reduction failed to progress");
            if (!std::isfinite(coverage)) throw std::runtime_error("Proxy coverage overflow");
            resultSummary.coverageAreaUpper = std::max(resultSummary.coverageAreaUpper,coverage);
            current.swap(next);
        }
        std::vector<FSplatWeightedGaussian> result; result.reserve(finalCount);
        for (const auto& record : current) { poll.Step(); result.push_back(record); }
        poll.Now();
        output = std::move(result); summary = resultSummary; error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
} // namespace MonsterRender::Splat
