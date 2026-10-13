// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/LOD/SplatLODSelector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

namespace MonsterRender::Splat {
namespace {
constexpr uint64_t TileSize = 16;
constexpr size_t MaxPrefetchPages = 64;
struct FProjection {
    bool visible = true;
    bool nearIntersect = false;
    double errorPixels = 0;
    double localErrorPixels = 0;
    uint64_t tileUpperBound = 0;
};
uint64_t Add(uint64_t a, uint64_t b) {
    return b > UINT64_MAX - a ? UINT64_MAX : a + b;
}
uint64_t Multiply(uint64_t a, uint64_t b) {
    return a && b > UINT64_MAX / a ? UINT64_MAX : a * b;
}
std::array<double,4> Transform(const float* matrix, const std::array<double,4>& p) {
    std::array<double,4> result{};
    for (size_t r = 0; r < 4; ++r)
        for (size_t c = 0; c < 4; ++c) result[r] += matrix[c * 4 + r] * p[c];
    return result;
}
FProjection Project(const FSplatLODNode& node, const FSplatLODView& view, bool rmsError = false, bool footprintError = false, double supportSigma = 3, bool centerDepth = false) {
    FProjection out;
    // All represented centers and their support lie inside this AABB. Expanding
    // it about its midpoint by S>=1 encloses every individually scaled support;
    // shrinking it would incorrectly move centers, so retain it for S<1.
    const double supportScale = std::max(1.0, double(view.gaussianScale));
    double lower[3], upper[3];
    for (size_t axis=0; axis<3; ++axis) {
        const double center=(double(node.bounds.min[axis])+node.bounds.max[axis])*0.5;
        const double extent=(double(node.bounds.max[axis])-node.bounds.min[axis])*0.5*supportScale;
        lower[axis]=center-extent; upper[axis]=center+extent;
    }
    double nearest = std::numeric_limits<double>::infinity();
    double farthest = -nearest;
    double minX = nearest, minY = nearest, maxX = -nearest, maxY = -nearest;
    bool outside[4] = {true,true,true,true};
    bool inside = true;
    for (size_t axis = 0; axis < 3; ++axis)
        inside = inside && view.cameraPosition[axis] >= lower[axis]
            && view.cameraPosition[axis] <= upper[axis];
    for (unsigned corner = 0; corner < 8; ++corner) {
        std::array<double,4> world{{lower[0], lower[1], lower[2], 1}};
        for (unsigned axis = 0; axis < 3; ++axis)
            if (corner & (1u << axis)) world[axis] = upper[axis];
        const auto eye = Transform(view.view, world);
        const double depth = -eye[2]; // Same right-handed convention as splat_preprocess.
        nearest = std::min(nearest, depth);
        farthest = std::max(farthest, depth);
        const auto clip = Transform(view.projection, eye);
        // Avoid false rejection when the bound crosses the eye or near plane.
        if (clip[3] <= 0) {
            std::fill(std::begin(outside), std::end(outside), false);
            continue;
        }
        // Two pixels protect the renderer's subpixel low-pass footprint.
        const double guardX = 4.0 * clip[3] / view.width;
        const double guardY = 4.0 * clip[3] / view.height;
        outside[0] = outside[0] && clip[0] < -clip[3] - guardX;
        outside[1] = outside[1] && clip[0] >  clip[3] + guardX;
        outside[2] = outside[2] && clip[1] < -clip[3] - guardY;
        outside[3] = outside[3] && clip[1] >  clip[3] + guardY;
        minX = std::min(minX, clip[0] / clip[3]); maxX = std::max(maxX, clip[0] / clip[3]);
        minY = std::min(minY, clip[1] / clip[3]); maxY = std::max(maxY, clip[1] / clip[3]);
    }
    out.visible = (view.distanceCulling ? farthest >= view.nearPlane && nearest <= view.farPlane : farthest > 0.0001)
        && !outside[0] && !outside[1] && !outside[2] && !outside[3];
    if (!out.visible) return out;
    const bool supportNearIntersect = inside || nearest <= view.nearPlane;
    double projectionDepth=nearest;
    if(centerDepth){
        // The shader clips/projects Gaussian centers, not their ellipsoid
        // support. All original AND proxy descendant centers must stay safely
        // beyond near. Support still controls visibility and screen coverage.
        projectionDepth=std::numeric_limits<double>::infinity();
        for(unsigned corner=0;corner<8;++corner){
            std::array<double,4> center{{node.centerBounds.min[0],node.centerBounds.min[1],node.centerBounds.min[2],1}};
            for(unsigned axis=0;axis<3;++axis)if(corner&(1u<<axis))center[axis]=node.centerBounds.max[axis];
            projectionDepth=std::min(projectionDepth,-Transform(view.view,center)[2]);
        }
    }
    out.nearIntersect = centerDepth ? projectionDepth <= view.nearPlane : supportNearIntersect;
    const double focal = std::max(std::abs(view.projection[0]) * view.width,
        std::abs(view.projection[5]) * view.height) * 0.5;
    // Appearance metadata is auxiliary, not a certified image error bound.
    const bool orthographic = std::abs(view.projection[11]) < 1e-7f
        && std::abs(view.projection[15]) > 1e-7f;
    double appearanceWorld=node.appearanceError;
    if (!rmsError) {
        // Legacy appearance has no world units. Weight it by finite-support
        // radius before projection; never use it as a distance-independent veto.
        double radiusSquared=0;
        for(size_t axis=0;axis<3;++axis){const double extent=(double(node.bounds.max[axis])-node.bounds.min[axis])*0.5;radiusSquared+=extent*extent;}
        appearanceWorld*=std::sqrt(radiusSquared);
    }
    out.errorPixels = out.nearIntersect ? std::numeric_limits<double>::infinity()
        : focal * (double(node.geometricError)+appearanceWorld) * supportScale
            / (orthographic ? std::abs(view.projection[15]) : projectionDepth);
    out.localErrorPixels=out.errorPixels;
    uint64_t tilesX = (uint64_t(view.width) + TileSize - 1) / TileSize;
    uint64_t tilesY = (uint64_t(view.height) + TileSize - 1) / TileSize;
    if (!supportNearIntersect && std::isfinite(minX) && std::isfinite(minY)) {
        const auto span = [](double minimum, double maximum, uint32_t pixels) {
            const double lo = std::clamp((minimum + 1) * pixels * 0.5 - 2, 0.0, double(pixels));
            const double hi = std::clamp((maximum + 1) * pixels * 0.5 + 2, 0.0, double(pixels));
            return uint64_t(std::max(0.0, std::ceil(hi / TileSize) - std::floor(lo / TileSize)));
        };
        tilesX = span(minX, maxX, view.width); tilesY = span(minY, maxY, view.height);
    }
    out.tileUpperBound = Multiply(node.gaussianCount, Multiply(tilesX, tilesY));
    if(footprintError&&!out.nearIntersect){
        // Sum(alpha*largest pair of standard deviations) bounds projected
        // Gaussian alpha-area above for every camera direction, including
        // anisotropic splats. Empty portions of an AABB do not emit radiance.
        const double areaWorld=node.coverageAreaUpper;
        const double depth=orthographic?std::abs(view.projection[15]):projectionDepth;
        const double kernelIntegral=6.2831853071795864769*(1-std::exp(-0.5*supportSigma*supportSigma));
        const double integratedPixels=kernelIntegral*areaWorld*std::abs(double(view.projection[0])*view.projection[5])*view.width*view.height*0.25*view.gaussianScale*view.gaussianScale/(depth*depth);
        const double rectanglePixels=double(tilesX)*tilesY*TileSize*TileSize;
        const double visibleFraction=std::min(rectanglePixels,integratedPixels)/double(uint64_t(view.width)*view.height);
        // Occupancy-weighted RMS contribution, without assuming independent
        // noise or granting an overlap-density discount. Local correlated edges
        // still need the fixed foreground/ROI image gates; this is an estimate.
        out.errorPixels*=std::sqrt(std::clamp(visibleFraction,0.0,1.0));
    }
    return out;
}
void Degrade(FSplatLODSelection& out, const char* reason) {
    out.degraded = true;
    if (out.degradationReason.find(reason) != std::string::npos) return;
    if (!out.degradationReason.empty()) out.degradationReason += "; ";
    out.degradationReason += reason;
}
bool ValidView(const FSplatLODView& view, const FSplatLODSettings& settings) {
    if (!view.width || !view.height || !std::isfinite(view.nearPlane)
        || !std::isfinite(view.farPlane) || view.nearPlane <= 0 || view.farPlane <= view.nearPlane
        || !std::isfinite(view.gaussianScale) || view.gaussianScale < 0.01f || view.gaussianScale > 10
        || !std::isfinite(settings.qualityPixels) || settings.qualityPixels <= 0
        || !std::isfinite(settings.hysteresis) || settings.hysteresis < 0 || settings.hysteresis >= 1)
        return false;
    for (auto value : view.view) if (!std::isfinite(value)) return false;
    for (auto value : view.projection) if (!std::isfinite(value)) return false;
    for (auto value : view.cameraPosition) if (!std::isfinite(value)) return false;
    return true;
}
bool Validate(const FSplatSceneManifest& manifest, std::vector<uint32_t>& parents,
    std::vector<uint32_t>& order) {
    if (!std::isfinite(manifest.supportSigma) || manifest.supportSigma < 3) return false;
    if (manifest.rootNode >= manifest.nodes.size()) return false;
    parents.assign(manifest.nodes.size(), SplatInvalidId);
    std::vector<bool> seen(manifest.nodes.size());
    order.push_back(manifest.rootNode); seen[manifest.rootNode] = true;
    for (size_t i = 0; i < order.size(); ++i) {
        const auto id = order[i]; const auto& node = manifest.nodes[id];
        if (node.children.size() > 8 || !std::isfinite(node.geometricError) || node.geometricError < 0
            || !std::isfinite(node.appearanceError) || node.appearanceError < 0
            || !std::isfinite(node.coverageAreaUpper) || node.coverageAreaUpper < 0) return false;
        for (size_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(node.bounds.min[axis]) || !std::isfinite(node.bounds.max[axis])
                || node.bounds.min[axis] > node.bounds.max[axis]) return false;
        if(manifest.version>=3)for(size_t axis=0;axis<3;++axis)
            if(!std::isfinite(node.centerBounds.min[axis])||!std::isfinite(node.centerBounds.max[axis])
                ||node.centerBounds.min[axis]>node.centerBounds.max[axis]
                ||node.centerBounds.min[axis]<node.bounds.min[axis]||node.centerBounds.max[axis]>node.bounds.max[axis])return false;
        uint64_t count = 0;
        std::vector<uint32_t> uniquePages = node.pageIds;
        std::sort(uniquePages.begin(), uniquePages.end());
        if (std::adjacent_find(uniquePages.begin(), uniquePages.end()) != uniquePages.end()) return false;
        for (const auto page : node.pageIds) {
            if (page >= manifest.pages.size()) return false;
            count = Add(count, manifest.pages[page].gaussianCount);
        }
        if (count != node.gaussianCount) return false;
        for (const auto child : node.children) {
            if (child >= manifest.nodes.size() || seen[child]) return false;
            seen[child] = true; parents[child] = id; order.push_back(child);
        }
    }
    return order.size() == manifest.nodes.size();
}
bool Ready(const FSplatSceneManifest& manifest, uint32_t id,
    const FSplatResidencySnapshot& residency, uint64_t sourceBudget) {
    if (!residency.IsNodeReady(manifest, id)) return false;
    for (const auto pageId : manifest.nodes[id].pageIds) {
        const auto& page = residency.pages[pageId];
        if (page.pageId != pageId || page.gaussianCount != manifest.pages[pageId].gaussianCount
            || uint64_t(page.sourceOffset) + page.gaussianCount > sourceBudget
            || uint64_t(page.sourceOffset) + page.gaussianCount > uint64_t(UINT32_MAX)) return false;
    }
    return true;
}
std::vector<bool> Refinements(const std::vector<uint32_t>& cut,
    const std::vector<uint32_t>& parents) {
    std::vector<bool> refined(parents.size());
    for (auto id : cut) {
        if (id >= parents.size()) continue;
        for (auto p = parents[id]; p != SplatInvalidId; p = parents[p]) refined[p] = true;
    }
    return refined;
}
uint64_t ActiveCount(const FSplatSceneManifest& manifest, const std::vector<uint32_t>& cut) {
    uint64_t result = 0;
    for (auto id : cut) result = Add(result, manifest.nodes[id].gaussianCount);
    return result;
}
uint64_t SourceCount(const FSplatSceneManifest& manifest, const std::vector<uint32_t>& cut,
    const std::vector<uint32_t>& additionalNodes = {}, const std::vector<uint32_t>& additionalPages = {}) {
    std::vector<bool> used(manifest.pages.size());
    for (size_t p = 0; p < manifest.pages.size(); ++p) used[p] = manifest.pages[p].pinned;
    for (const auto id : cut) for (auto p : manifest.nodes[id].pageIds) used[p] = true;
    for (const auto id : additionalNodes) for (auto p : manifest.nodes[id].pageIds) used[p] = true;
    for (auto p : additionalPages) used[p] = true;
    // A missing sibling starts an atomic replacement of the entire child set.
    // Already-ready siblings still occupy transition capacity, matching the pool's
    // ProtectSelection contract. Counting only missing pages can over-admit several
    // concurrent refinements and make the pool reject every upload indefinitely.
    std::vector<bool> requested(manifest.pages.size());
    for (auto p : additionalPages) requested[p] = true;
    for (auto id : cut) {
        bool replacing = false;
        for (auto child : manifest.nodes[id].children)
            for (auto p : manifest.nodes[child].pageIds) replacing = replacing || requested[p];
        if (replacing)
            for (auto child : manifest.nodes[id].children)
                for (auto p : manifest.nodes[child].pageIds) used[p] = true;
    }
    uint64_t count = 0;
    for (size_t p = 0; p < used.size(); ++p) if (used[p]) count = Add(count, manifest.pages[p].gaussianCount);
    return count;
}
std::vector<uint32_t> Replace(const std::vector<uint32_t>& cut, uint32_t parent,
    const std::vector<uint32_t>& children, const std::vector<FProjection>& projected) {
    std::vector<uint32_t> next; next.reserve(cut.size() + children.size());
    for (auto id : cut) if (id != parent) next.push_back(id);
    for (auto child : children) if (projected[child].visible) next.push_back(child);
    return next;
}
} // namespace

void FSplatLODSelector::Reset() {
    m_manifest = nullptr; m_sceneGeneration = 0; m_residencyVersion = 0;
    m_previousSelected.clear(); m_previousDesired.clear();
}

FSplatLODSelection FSplatLODSelector::Select(const FSplatSceneManifest& manifest,
    const FSplatLODView& view, const FSplatLODSettings& settings,
    const FSplatResidencySnapshot& residency) {
    FSplatLODSelection out;
    const bool sameScene = m_manifest == &manifest && m_sceneGeneration == residency.sceneGeneration;
    if (!sameScene) Reset();
    std::vector<FProjection> projected;
    auto finish = [&]() {
        std::sort(out.selectedNodeIds.begin(), out.selectedNodeIds.end());
        std::sort(out.desiredNodeIds.begin(), out.desiredNodeIds.end());
        if (!sameScene || out.selectedNodeIds != m_previousSelected
            || residency.residencyVersion != m_residencyVersion) ++m_version;
        out.activeCount = ActiveCount(manifest, out.selectedNodeIds);
        for (auto id : out.selectedNodeIds) {
            if (manifest.nodes[id].children.empty()) ++out.selectedLeafNodes;
            else {
                ++out.selectedProxyNodes; out.proxyGaussianCount += manifest.nodes[id].gaussianCount;
                if (id < projected.size() && !std::isfinite(projected[id].errorPixels)) out.maxSelectedProxyErrorPixels = -1;
                else if (id < projected.size() && out.maxSelectedProxyErrorPixels >= 0)
                    out.maxSelectedProxyErrorPixels = std::max(out.maxSelectedProxyErrorPixels, projected[id].errorPixels);
            }
        }
        out.selectionVersion = m_version;
        m_previousSelected = out.selectedNodeIds; m_previousDesired = out.desiredNodeIds;
        m_manifest = &manifest; m_sceneGeneration = residency.sceneGeneration;
        m_residencyVersion = residency.residencyVersion;
        return out;
    };
    if (!ValidView(view, settings)) { Degrade(out, "invalid camera or LOD settings"); return finish(); }
    if (manifest.nodes.empty() && manifest.rootNode == SplatInvalidId) return finish();
    std::vector<uint32_t> parents, order;
    if (!Validate(manifest, parents, order)) { Degrade(out, "invalid hierarchy or representation"); return finish(); }
    projected.resize(manifest.nodes.size());
    for (auto id : order) projected[id] = Project(manifest.nodes[id], view,
        manifest.builderVersion == SplatRMSErrorBuilderVersion||manifest.builderVersion==SplatRMSLegacyErrorBuilderVersion||manifest.builderVersion==SplatCenterErrorBuilderVersion||manifest.builderVersion==SplatFusionBuilderVersion||manifest.builderVersion==SplatLeafFusionBuilderVersion,
        (manifest.version==2&&manifest.builderVersion==SplatRMSErrorBuilderVersion)||(manifest.version>=3&&(manifest.builderVersion==SplatCenterErrorBuilderVersion||manifest.builderVersion==SplatFusionBuilderVersion||manifest.builderVersion==SplatLeafFusionBuilderVersion)),manifest.supportSigma,
        manifest.version>=3&&(manifest.builderVersion==SplatCenterErrorBuilderVersion||manifest.builderVersion==SplatFusionBuilderVersion||manifest.builderVersion==SplatLeafFusionBuilderVersion));
    const auto root = manifest.rootNode;
    if (!projected[root].visible) return finish();
    std::vector<uint32_t> rootCut{root};
    if (manifest.nodes[root].gaussianCount > settings.activeBudget
        || SourceCount(manifest, rootCut) > settings.sourceBudget) {
        Degrade(out, "coarse fallback exceeds hard active/source budget"); return finish();
    }
    if (!settings.sortBudget && manifest.nodes[root].gaussianCount) {
        Degrade(out, "zero sort capacity for nonempty scene"); return finish();
    }
    const auto previousDesiredRefined = Refinements(m_previousDesired, parents);
    std::vector<bool> desiredRefined(manifest.nodes.size());
    out.desiredNodeIds = rootCut;
    const bool validHistory = std::all_of(m_previousDesired.begin(), m_previousDesired.end(),
        [&](uint32_t id) { return id < manifest.nodes.size(); });
    if (settings.freeze && sameScene && validHistory && !m_previousDesired.empty()) {
        // Preserve the frozen frontier, but still enforce changed hard budgets.
        if (ActiveCount(manifest, m_previousDesired) <= settings.activeBudget
            && SourceCount(manifest, m_previousDesired) <= settings.sourceBudget) {
            out.desiredNodeIds = m_previousDesired;
            desiredRefined = Refinements(out.desiredNodeIds, parents);
        } else Degrade(out, "frozen cut reduced by changed budgets");
    } else {
        // Repeatedly choose the greatest projected squared-error benefit per added record.
        struct Candidate { double priority; uint32_t id; };
        const auto lowerPriority = [](const Candidate& a, const Candidate& b) {
            return a.priority != b.priority ? a.priority < b.priority : a.id > b.id;
        };
        std::priority_queue<Candidate, std::vector<Candidate>, decltype(lowerPriority)> candidates(lowerPriority);
        const auto enqueue = [&](uint32_t id) {
                const auto& node = manifest.nodes[id];
                if (node.children.empty()) return;
                const double threshold = settings.qualityPixels * (previousDesiredRefined[id]
                    ? 1.0 - settings.hysteresis : 1.0 + settings.hysteresis);
                if (projected[id].errorPixels <= threshold) return;
                uint64_t childCount = 0;
                for (auto child : node.children) if (projected[child].visible)
                    childCount = Add(childCount, manifest.nodes[child].gaussianCount);
                const double cost = double(childCount > node.gaussianCount ? childCount - node.gaussianCount : 1)
                    * (1.0 + std::min(1.0, double(projected[id].tileUpperBound) / double(std::max<uint64_t>(1, settings.sortBudget))));
                const double priority = projected[id].nearIntersect ? std::numeric_limits<double>::infinity()
                    : projected[id].errorPixels / std::sqrt(cost);
                candidates.push({priority,id});
        };
        // Priorities are fixed within this view. Page reference counts retain
        // exact shared/pinned union charges while avoiding frontier rescans.
        std::vector<uint32_t> pageReferences(manifest.pages.size());
        uint64_t desiredSource = 0, desiredActive = manifest.nodes[root].gaussianCount;
        for (size_t p=0;p<manifest.pages.size();++p) if (manifest.pages[p].pinned) {
            pageReferences[p]=1; desiredSource+=manifest.pages[p].gaussianCount;
        }
        const auto charge = [&](uint32_t id, bool add) {
            for (auto p:manifest.nodes[id].pageIds) {
                if (add) { if (pageReferences[p]++ == 0) desiredSource+=manifest.pages[p].gaussianCount; }
                else if (--pageReferences[p] == 0) desiredSource-=manifest.pages[p].gaussianCount;
            }
        };
        charge(root,true); enqueue(root);
        while (!candidates.empty()) {
            const auto best=candidates.top().id; candidates.pop();
            uint64_t childActive=0;
            charge(best,false);
            for (auto child:manifest.nodes[best].children) if (projected[child].visible) {
                childActive=Add(childActive,manifest.nodes[child].gaussianCount);charge(child,true);
            }
            const uint64_t candidateActive=Add(desiredActive-manifest.nodes[best].gaussianCount,childActive);
            if (candidateActive > settings.activeBudget || desiredSource > settings.sourceBudget) {
                for (auto child:manifest.nodes[best].children) if (projected[child].visible) charge(child,false);
                charge(best,true);
                Degrade(out, "refinement blocked by hard active/source budget"); continue;
            }
            // Node support area is a scheduling hint, not a Gaussian-tile count.
            // The same-frame GPU preflight enforces the actual sort capacity.
            desiredActive=candidateActive;
            desiredRefined[best] = true;
            out.desiredNodeIds = Replace(out.desiredNodeIds, best, manifest.nodes[best].children, projected);
            for (auto child:manifest.nodes[best].children) if (projected[child].visible) enqueue(child);
        }
    }

    if (!Ready(manifest, root, residency, settings.sourceBudget)) {
        out.requestPageIds = manifest.nodes[root].pageIds;
        out.requestPageIds.erase(std::remove_if(out.requestPageIds.begin(), out.requestPageIds.end(),
            [&](uint32_t p) { return residency.IsPageReady(p); }), out.requestPageIds.end());
        // A downgrade cannot retire a complete resident descendant cut before
        // its ancestor upload finishes. Resolve every visible branch, choosing
        // its first complete resident representation; never publish a partial cut.
        bool complete = true;
        std::vector<uint32_t> fallback{root};
        for (size_t i = 0; i < fallback.size(); ++i) {
            const auto id = fallback[i];
            if (!projected[id].visible) continue;
            if (Ready(manifest, id, residency, settings.sourceBudget)) {
                out.selectedNodeIds.push_back(id);
            } else if (!manifest.nodes[id].children.empty()) {
                fallback.insert(fallback.end(), manifest.nodes[id].children.begin(), manifest.nodes[id].children.end());
            } else complete = false;
        }
        if (!complete || ActiveCount(manifest, out.selectedNodeIds) > settings.activeBudget
            || SourceCount(manifest, out.selectedNodeIds) > settings.sourceBudget)
            out.selectedNodeIds.clear();
        else if (SourceCount(manifest, out.selectedNodeIds, {}, out.requestPageIds) > settings.sourceBudget) {
            out.requestPageIds.clear();
            Degrade(out, "coarsening lacks parent-child transition reserve");
        }
        Degrade(out, "coarse fallback loading or source mapping unavailable"); return finish();
    }
    out.selectedNodeIds = rootCut;
    // Resume an already-resident refined frontier directly. Intermediate parent
    // pages may have been evicted after replacement; restarting from the root on
    // every frame would reload those parents and evict the valid descendants.
    // Rebuild coverage against CURRENT visibility, never reuse an old cut blindly.
    if (sameScene && !m_previousSelected.empty()) {
        std::vector<uint32_t> residentCut;
        std::vector<uint32_t> coarseningPages;
        const auto collect = [&](auto&& self, uint32_t id) -> bool {
            if (!projected[id].visible) return true;
            const auto begin = residentCut.size();
            const auto requestBegin = coarseningPages.size();
            const bool ready = Ready(manifest, id, residency, settings.sourceBudget);
            // Sparse branches may be visible by their parent AABB while every
            // child is outside the view. Such a complete EMPTY branch has no
            // node in the previous cut, so ancestor flags from that cut are not
            // sufficient to decide whether its resident descendants cover it.
            if ((desiredRefined[id] || !ready) && !manifest.nodes[id].children.empty()) {
                bool complete = true;
                for (auto child : manifest.nodes[id].children) complete = self(self, child) && complete;
                if (complete) {
                    if (!desiredRefined[id] && !ready)
                        for (auto p : manifest.nodes[id].pageIds) if (!residency.IsPageReady(p)) coarseningPages.push_back(p);
                    return true;
                }
                residentCut.resize(begin);
                coarseningPages.resize(requestBegin);
            }
            if (!ready) return false;
            residentCut.push_back(id); return true;
        };
        if (collect(collect, root) && ActiveCount(manifest, residentCut) <= settings.activeBudget
            && SourceCount(manifest, residentCut) <= settings.sourceBudget) {
            out.selectedNodeIds = std::move(residentCut);
            for (auto p : coarseningPages) {
                auto requested = out.requestPageIds; requested.push_back(p);
                if (SourceCount(manifest, out.selectedNodeIds, {}, requested) <= settings.sourceBudget)
                    out.requestPageIds.push_back(p);
            }
        }
    }
    std::vector<bool> requestSeen(manifest.pages.size());
    for (auto p : out.requestPageIds) requestSeen[p] = true;
    std::vector<double> pagePriority(manifest.pages.size(), 0);
    std::vector<uint32_t> pending = out.selectedNodeIds;
    for (size_t i = 0; i < pending.size(); ++i) {
        const auto id = pending[i]; const auto& node = manifest.nodes[id];
        if (!desiredRefined[id] || node.children.empty()) continue;
        // Every child representation must be complete before atomically replacing
        // the parent, including multipage and currently invisible child nodes.
        if (SourceCount(manifest, out.selectedNodeIds, node.children, out.requestPageIds) > settings.sourceBudget) {
            Degrade(out, "refinement lacks parent-child transition reserve"); continue;
        }
        bool allReady = true;
        for (auto child : node.children) allReady = allReady && Ready(manifest, child, residency, settings.sourceBudget);
        if (!allReady) {
            for (auto child : node.children) for (auto page : manifest.nodes[child].pageIds)
                if (!residency.IsPageReady(page) && !requestSeen[page]) {
                    requestSeen[page] = true; out.requestPageIds.push_back(page);
                    pagePriority[page] = projected[id].errorPixels;
                }
            Degrade(out, "target refinement waiting for complete child residency"); continue;
        }
        auto candidate = Replace(out.selectedNodeIds, id, node.children, projected);
        if (ActiveCount(manifest, candidate) > settings.activeBudget
            || SourceCount(manifest, candidate) > settings.sourceBudget) {
            Degrade(out, "resident cut exceeds changed hard budget"); continue;
        }
        out.selectedNodeIds = std::move(candidate);
        for (auto child : node.children) if (projected[child].visible) pending.push_back(child);
    }
    // Prioritize requests by their owning node's projected error. Stable IDs break ties.
    for (auto id : order) for (auto page : manifest.nodes[id].pageIds)
        pagePriority[page] = std::max(pagePriority[page], projected[id].errorPixels);
    const auto priorityOrder = [&](uint32_t a, uint32_t b) {
        return pagePriority[a] != pagePriority[b] ? pagePriority[a] > pagePriority[b] : a < b;
    };
    std::stable_sort(out.requestPageIds.begin(), out.requestPageIds.end(), priorityOrder);

    // Bounded one-level lookahead, including adjacent invisible siblings. Visible
    // requests take precedence; prefetch never spends protected transition capacity.
    if (!settings.freeze && !out.degraded && out.requestPageIds.empty()) {
        std::vector<uint32_t> candidates;
        for (auto id : out.selectedNodeIds) for (auto child : manifest.nodes[id].children)
            for (auto page : manifest.nodes[child].pageIds)
                if (!residency.IsPageReady(page) && !requestSeen[page]) {
                    requestSeen[page] = true; candidates.push_back(page);
                }
        std::stable_sort(candidates.begin(), candidates.end(), priorityOrder);
        for (auto page : candidates) {
            if (out.prefetchPageIds.size() == MaxPrefetchPages) break;
            auto reserve = out.requestPageIds;
            reserve.insert(reserve.end(), out.prefetchPageIds.begin(), out.prefetchPageIds.end());
            reserve.push_back(page);
            if (SourceCount(manifest, out.selectedNodeIds, {}, reserve) <= settings.sourceBudget)
                out.prefetchPageIds.push_back(page);
        }
    }
    return finish();
}
} // namespace MonsterRender::Splat
