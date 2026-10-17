// Copyright Monster Engine. All Rights Reserved.
#ifndef SPLAT_PROJECTION_COLOR_GLSL
#define SPLAT_PROJECTION_COLOR_GLSL
// RGB21F: 3 independent sign:1/exponent:8/mantissa:12 channels in 63 bits.
// Drop the low 11 mantissa bits (round toward zero). No HDR/UNORM clamp;
// every finite FP32 exponent is supported, including subnormals and signed zero.
// Preserve infinities and canonicalize NaNs so a discarded payload cannot become infinity.
uint splatEncodeColorChannel(float value) {
    uint bits = floatBitsToUint(value);
    uint code = bits >> 11u;
    if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0u)
        code |= 0x800u;
    return code;
}
uvec2 splatEncodeProjectionColor(vec3 color) {
    uvec3 c = uvec3(splatEncodeColorChannel(color.r), splatEncodeColorChannel(color.g), splatEncodeColorChannel(color.b));
    return uvec2(c.r | (c.g << 21u), (c.g >> 11u) | (c.b << 10u));
}
vec3 splatDecodeProjectionColor(uvec2 words) {
    uvec3 c = uvec3(words.x & 0x1fffffu,
        ((words.x >> 21u) | (words.y << 11u)) & 0x1fffffu,
        (words.y >> 10u) & 0x1fffffu);
    return uintBitsToFloat(c << 11u);
}
#endif
