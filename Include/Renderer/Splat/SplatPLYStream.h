// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatBuildTypes.h"
#include <span>

namespace MonsterRender::Splat {
/** Supported wire encodings; ASCII has exactly one vertex record per line. */
enum class ESplatPLYFormat : uint32_t { ASCII = 0, LittleEndian = 1, BigEndian = 2 };
/** Admission limits applied before reading records or allocating record batches. */
struct FSplatPLYLimits {
    uint64_t maxRecords = 100000000; ///< Maximum declared vertex count.
    uint64_t maxFileBytes = 16ull << 30; ///< Maximum stable source size.
    uint32_t maxHeaderBytes = 1u << 20; ///< Header storage bound, including newlines.
    uint32_t maxRecordBytes = 1u << 20; ///< ASCII line or binary stride limit.
    uint32_t maxProperties = 1024; ///< Property metadata bound.
};
/** Scalar vertex property; lists are not accepted in Gaussian records. */
struct FSplatPLYProperty {
    std::string name; ///< Unique PLY property name.
    uint32_t size = 0; ///< Scalar wire width in bytes.
    bool floating = false; ///< IEEE754 float or double rather than integer.
    bool signedValue = false; ///< Signed integer or floating scalar.
};
/** Validated bounded header; publish only after a successful Probe. */
struct FSplatPLYHeader {
    ESplatPLYFormat format = ESplatPLYFormat::ASCII; ///< Declared PLY 1.0 encoding.
    uint64_t recordCount = 0; ///< Declared vertex count; row IDs are [0,count).
    uint32_t shDegree = 0; ///< Contiguous source SH layout degree, zero through three.
    uint64_t stride = 0; ///< Binary vertex byte stride.
    uint64_t dataOffset = 0; ///< Absolute first vertex offset.
    uint64_t fileBytes = 0; ///< Stable source byte length at probe time.
    std::vector<FSplatPLYProperty> properties; ///< Bounded ordered scalar metadata.
    FSplatPLYLimits limits; ///< Actual admission policy used by Probe.
    std::string headerIdentity; ///< Exact bounded header bytes; compared before streaming.
};
/** Sink owns durability: true commits this batch; false stops without advancing.
 * The span expires on return. Cursor is immediately after the last complete row.
 * A resumed caller must first verify complete source hash/parser identity against
 * its durable journal; byte size/header alone are not a source content hash.
 */
using FSplatPLYBatchSink = std::function<bool(std::span<const FSplatGaussian>,
    const FSplatBuildSourceCursor&, std::string&)>;
/** CPU-only shared Gaussian parser. No renderer, Vulkan or input-sized storage. */
class FSplatPLYStream {
public:
    /** Parse only bounded header and payload range metadata; output changes on success. */
    static bool Probe(const std::string& path, FSplatPLYHeader& header, std::string& error,
        const FSplatPLYLimits& limits = {}, const FSplatBuildControl* control = nullptr);
    /** Stream activated rows with stable IDs; poll at least every row and bounded I/O.
     * resume={} begins at dataOffset. ASCII resume must name a committed line boundary;
     * binary resume must exactly match dataOffset + nextOriginalId * stride.
     */
    static bool StreamRecords(const std::string& path, const FSplatPLYHeader& header,
        uint32_t batchCapacity, FSplatBuildControl& control, const FSplatPLYBatchSink& sink,
        std::string& error, const FSplatBuildSourceCursor& resume = {});
    /** Conservative parser workspace including batch capacity and bounded metadata.
     * Throws on invalid capacity or overflow. Caller must admit it before streaming.
     */
    static uint64_t ParserWorkingBytes(const FSplatPLYHeader& header, uint32_t batchCapacity);
};
} // namespace MonsterRender::Splat
