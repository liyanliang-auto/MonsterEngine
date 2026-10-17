// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <vector>
#include <utility>
#include <limits>
namespace MonsterRender::Splat {
/** GPU SH representation: FP32 default, or two binary16 scalars per uint32. */
namespace ESplatSHStorage { constexpr uint32_t FP32 = 0, FP16 = 1; }
/** Word-aligned bytes including final half padding; zero on invalid size/mode. */
inline uint64_t SplatSHStorageBytes(uint64_t scalars, uint32_t mode) {
    if (mode > 1 || scalars > (std::numeric_limits<uint64_t>::max() - 3) / 4) return 0;
    return mode == ESplatSHStorage::FP16 ? ((scalars + 1) / 2) * 4 : scalars * 4;
}
/** Reject nonfinite/overflow, preserve signed zero, round to nearest-even. */
inline bool SplatFloatToHalf(float value, uint16_t& out) {
    if (!std::isfinite(value) || std::abs(value) > 65504.0f) return false;
    uint32_t bits; std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u, exponent = (bits >> 23) & 255u;
    if (exponent < 102u) { out = uint16_t(sign); return true; }
    const uint32_t significand = (bits & 0x7fffffu) | 0x800000u;
    const uint32_t shift = exponent < 113u ? 126u - exponent : 13u;
    uint32_t rounded = significand >> shift;
    const uint32_t remainder = significand & ((1u << shift) - 1u), halfway = 1u << (shift - 1u);
    if (remainder > halfway || (remainder == halfway && (rounded & 1u))) ++rounded;
    const uint32_t half = exponent < 113u ? rounded : ((exponent - 113u) << 10) + rounded;
    out = uint16_t(sign | half); return true;
}
/** Sequential RGB scalars, low half first; output is unchanged on rejection. */
inline bool SplatPackSHHalf(const float* values, size_t count, std::vector<uint32_t>& out) {
    if (count && !values) return false;
    std::vector<uint32_t> words(count / 2 + count % 2, 0);
    for (size_t i = 0; i < count; ++i) {
        uint16_t half; if (!SplatFloatToHalf(values[i], half)) return false;
        words[i / 2] |= uint32_t(half) << ((i & 1) * 16);
    }
    out = std::move(words); return true;
}
} // namespace MonsterRender::Splat
