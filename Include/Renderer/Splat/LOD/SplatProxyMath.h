// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatBuildTypes.h"
#include <span>

namespace MonsterRender::Splat {
/** Weighted activated Gaussian. Persist errors and weight as three explicit F64
 * values using FSplatBuildRecordSchema; never serialize this C++ object layout. */
struct FSplatWeightedGaussian {
    FSplatGaussian gaussian; ///< Current Gaussian moment-matched representation.
    double geometryErrorSquared = 0; ///< Cumulative squared transport RMS bound.
    double appearanceErrorSquared = 0; ///< Cumulative squared footprint appearance RMS.
    double weight = 0; ///< Additive original alpha-area mass, retained after opacity caps.
};

/** Conservative summary for a disjoint set of represented input subtrees. */
struct FSplatProxySummary {
    bool hasBounds = false; ///< False before the first valid Gaussian/subtree.
    FSplatBounds supportBounds; ///< Contains all supplied and generated finite supports.
    FSplatBounds centerBounds; ///< Contains all supplied and generated centers.
    double coverageAreaUpper = 0; ///< Maximum representation coverage, including descendants.
    double geometricError = 0; ///< Unscaled cumulative transport RMS bound, never reset.
    double appearanceError = 0; ///< Cumulative footprint appearance bound, never reset.
};

/** Shared CPU moment matching and explicitly admitted deterministic reduction.
 * These helpers do not require a renderer, graphics API, or device allocation. */
class FSplatProxyMath {
public:
    /** Alpha-independent Thomsen area factor, shared with the legacy builder. */
    static double Area(const FSplatGaussian& gaussian);
    /** Outward-rounded finite support; throws on nonfinite output. */
    static FSplatBounds Support(const FSplatGaussian& gaussian, double sigma);
    /** Extend a previously initialized axis-aligned bound. */
    static void Extend(FSplatBounds& destination, const FSplatBounds& source);
    /** RMS SH difference over the active coefficient stride. */
    static double Appearance(const FSplatGaussian& a, const FSplatGaussian& b, uint32_t stride);
    /** Conservative alpha-weighted maximum projected area factor. */
    static double Coverage(const FSplatGaussian& gaussian);
    /** Deterministic 21-bit-per-axis Morton key inside center bounds. */
    static uint64_t Morton(const FSplatGaussian& gaussian, const FSplatBounds& centers);
    /** Create an original zero-error weighted record; validates activated attributes. */
    static FSplatWeightedGaussian FromGaussian(const FSplatGaussian& gaussian);
    /** Constant-size pair merge using persisted additive mass and historical errors.
     * Throws on invalid/nonfinite input or unrepresentable output. */
    static FSplatWeightedGaussian Merge(const FSplatWeightedGaussian& a,
        const FSplatWeightedGaussian& b, uint32_t stride, double sigma, uint64_t proxyId);
    /** Legacy adapter retains area-opacity weighting and RMS arithmetic exactly;
     * callers retain their existing grouping and original fusion memberships. */
    static void MergeLegacy(const FSplatGaussian& a, const FSplatGaussian& b,
        double geometryA, double appearanceA, double geometryB, double appearanceB,
        uint32_t stride, double sigma, uint64_t proxyId,
        FSplatGaussian& output, double& geometry, double& appearance);
    /** Union disjoint child summaries; sums their conservative coverage bounds. */
    static void ExtendSummary(FSplatProxySummary& destination, const FSplatProxySummary& source);
    /** Preserve the union of descendant supports, centers and historical errors
     * after reduction. Coverage is the maximum of this representation and the
     * disjoint-child aggregate, rather than counting the same subtree twice.
     * Weighted spool records cannot reconstruct descendant finite supports. */
    static void PreserveDescendants(FSplatProxySummary& representation, const FSplatProxySummary& descendants);
    /** Reduce at most 8*max(leafCapacity,maxProxyCount) weighted records to targetCount.
     * targetCount must be positive and <= maxProxyCount. Caller-owned input and
     * previous output capacity are charged along with conservative scratch under
     * workingMemoryBytes minus metadataBytes. scratchBytes returns peak additional
     * bytes required, including final output and fixed allocator/stack headroom.
     * Polls pause/cancel at <=256 bounded record/comparison operations. On failure
     * output and summary remain unchanged; no partial representation is published.
     * Proxy IDs derive from deterministic local merge order plus proxyIdBase;
     * callers reserve disjoint ID ranges when global uniqueness is required.
     */
    static bool Reduce(std::span<const FSplatWeightedGaussian> input,
        uint32_t shDegree, uint32_t targetCount, const FSplatBoundedBuildConfig& config,
        FSplatBuildControl& control, std::vector<FSplatWeightedGaussian>& output,
        FSplatProxySummary& summary, uint64_t& scratchBytes, std::string& error,
        uint64_t proxyIdBase = 0);
};
} // namespace MonsterRender::Splat
