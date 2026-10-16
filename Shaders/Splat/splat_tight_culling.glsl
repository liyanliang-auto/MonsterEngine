// Copyright Monster Engine. All Rights Reserved.
#ifndef SPLAT_TIGHT_CULLING_GLSL
#define SPLAT_TIGHT_CULLING_GLSL
// Shared count/emit contract. All coordinates refer to integer raster samples.
struct SplatTightSupport {
    vec3 q;
    float limit;
    vec2 extent;
    bool safe;
};
bool splatFinite(float x) { return !isnan(x) && !isinf(x); }
SplatTightSupport splatTightSupport(vec4 conicOpacity, vec2 center) {
    SplatTightSupport s;
    s.q = conicOpacity.xyz; s.limit = 0.0; s.extent = vec2(0.0); s.safe = false;
    if (any(isnan(conicOpacity)) || any(isinf(conicOpacity)) ||
        any(isnan(center)) || any(isinf(center)) || any(greaterThan(abs(center), vec2(1048576.0)))) return s;
    float a = s.q.x, b = s.q.y, c = s.q.z;
    float ac = a * c;
    float det = ac - b * b;
    // Avoid inversion and cancellation in ill-conditioned or extreme conics.
    if (a < 1e-12 || c < 1e-12 || a > 1e12 || c > 1e12 ||
        !splatFinite(ac) || !splatFinite(det) || det <= 1e-4 * ac ||
        min(a,c) < 1e-4 * max(a,c) || conicOpacity.w <= 0.0) return s;
    // Padding covers log/exp and FP32 quadratic rounding; equality is retained.
    float rawLimit = 2.0 * log(max(conicOpacity.w, 1.0 / 255.0) * 255.0);
    if (!splatFinite(rawLimit)) return s;
    s.limit = max(0.0, rawLimit) + 0.001 * max(1.0, abs(rawLimit));
    // The 1% inflation also covers inverse-determinant rounding near the
    // conditioning boundary; one extra pixel protects coordinate/tile conversion.
    s.extent = 1.01 * sqrt(s.limit * vec2(c,a) / det) + vec2(1.0);
    if (any(isnan(s.extent)) || any(isinf(s.extent))) return s;
    s.safe = true;
    return s;
}
uvec4 splatTightRect(uvec4 oldRect, vec2 center, SplatTightSupport s, uvec2 grid, uint mode) {
    if (mode == 0u || !s.safe) return oldRect;
    // Intersect TILE candidates, never clip old tile-rounded support to 3 sigma.
    vec2 lo = clamp(floor((center - s.extent) / 16.0), vec2(0.0), vec2(grid));
    vec2 hi = clamp(floor((center + s.extent) / 16.0) + 1.0, vec2(0.0), vec2(grid));
    return uvec4(max(oldRect.xy, uvec2(lo)), min(oldRect.zw, uvec2(hi)));
}
float splatQuadratic(vec2 d, vec3 q) {
    return q.x*d.x*d.x + q.z*d.y*d.y + 2.0*q.y*d.x*d.y;
}
bool splatTileRetained(uvec2 tile, vec2 center, SplatTightSupport s, uvec2 imageSize, uint mode) {
    if (mode < 2u || !s.safe) return true;
    vec2 lo = vec2(tile * 16u) - center;
    vec2 hi = vec2(min(tile * 16u + 15u, imageSize - 1u)) - center;
    if (all(lessThanEqual(lo, vec2(0.0))) && all(greaterThanEqual(hi, vec2(0.0)))) return true;
    // Convex quadratic box minimum: origin or one of four clamped edge minima.
    // Continuous box contains every valid integer pixel, including partial tiles.
    float best = min(
        splatQuadratic(vec2(lo.x, clamp(-s.q.y*lo.x/s.q.z, lo.y, hi.y)), s.q),
        splatQuadratic(vec2(hi.x, clamp(-s.q.y*hi.x/s.q.z, lo.y, hi.y)), s.q));
    best = min(best, splatQuadratic(vec2(clamp(-s.q.y*lo.y/s.q.x, lo.x, hi.x), lo.y), s.q));
    best = min(best, splatQuadratic(vec2(clamp(-s.q.y*hi.y/s.q.x, lo.x, hi.x), hi.y), s.q));
    vec2 d = max(abs(lo), abs(hi));
    float magnitude = s.q.x*d.x*d.x + s.q.z*d.y*d.y + 2.0*abs(s.q.y)*d.x*d.y;
    // Fail open on overflow/cancellation uncertainty; reject strict separation only.
    float margin = 0.001 * max(1.0, magnitude);
    return !splatFinite(best) || !splatFinite(margin) || best <= s.limit + margin;
}
#endif
