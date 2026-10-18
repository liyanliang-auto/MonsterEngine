// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/SplatPLYStream.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
void Require(bool condition, const std::string& error) {
    if (!condition) throw std::runtime_error(error);
}
uint64_t Add(uint64_t a, uint64_t b) {
    Require(a <= UINT64_MAX - b, "PLY byte count overflow"); return a + b;
}
uint64_t Multiply(uint64_t a, uint64_t b) {
    Require(!b || a <= UINT64_MAX / b, "PLY byte count overflow"); return a * b;
}
void Poll(const FSplatBuildControl* control) {
    if (!control) return;
    const auto signal = control->Poll();
    Require(signal != ESplatBuildSignal::Cancel, "Cancelled");
    Require(signal != ESplatBuildSignal::Pause, "Paused");
}
uint64_t Offset(std::istream& stream) {
    const auto position = stream.tellg();
    Require(position >= 0, "Cannot query PLY cursor");
    return static_cast<uint64_t>(position);
}
// Unlike getline/operator>>, this never allocates an unbounded line/token.
std::string Line(std::istream& stream, uint64_t& offset, uint32_t limit,
    const FSplatBuildControl* control, std::string* raw = nullptr) {
    std::string line;
    char c = 0;
    uint32_t polled = 0;
    while (stream.get(c)) {
        ++offset;
        if (raw) {
            Require(raw->size() < limit, "PLY header size limit");
            raw->push_back(c);
        }
        if ((++polled & 4095u) == 0) Poll(control);
        if (c == '\n') break;
        Require(line.size() < limit, "PLY record/header line limit");
        line.push_back(c);
    }
    Require(!stream.bad(), "PLY read failed");
    Require(!line.empty() || c == '\n', "Truncated PLY record/header");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}
uint64_t Unsigned(const std::string& token) {
    uint64_t value = 0;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    Require(!token.empty() && parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size(), "Invalid PLY unsigned count");
    return value;
}
FSplatPLYProperty Property(const std::string& type, const std::string& name) {
    if (type == "float" || type == "float32") return {name,4,true,true};
    if (type == "double" || type == "float64") return {name,8,true,true};
    if (type == "char" || type == "int8") return {name,1,false,true};
    if (type == "uchar" || type == "uint8") return {name,1,false,false};
    if (type == "short" || type == "int16") return {name,2,false,true};
    if (type == "ushort" || type == "uint16") return {name,2,false,false};
    if (type == "int" || type == "int32") return {name,4,false,true};
    if (type == "uint" || type == "uint32") return {name,4,false,false};
    throw std::runtime_error("Unsupported PLY scalar type: " + type);
}
bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
double Scalar(std::istream& file, const FSplatPLYProperty& property, bool big,
    const std::string* line, size_t& cursor) {
    double value = 0;
    if (line) {
        while (cursor < line->size() && Space((*line)[cursor])) ++cursor;
        const auto start = cursor;
        while (cursor < line->size() && !Space((*line)[cursor])) {
            Require(cursor - start < 128, "ASCII PLY scalar length limit"); ++cursor;
        }
        Require(cursor > start, "Truncated ASCII PLY row");
        auto first = line->data() + start;
        auto last = line->data() + cursor;
        if (*first == '+') ++first;
        const auto parsed = std::from_chars(first, last, value, std::chars_format::general);
        Require(parsed.ec == std::errc{} && parsed.ptr == last, "Invalid ASCII PLY scalar");
        if (!property.floating) {
            const auto bits = property.size * 8;
            const double low = property.signedValue ? -std::ldexp(1.0, bits - 1) : 0;
            const double high = property.signedValue ? std::ldexp(1.0, bits - 1) - 1 : std::ldexp(1.0, bits) - 1;
            Require(std::floor(value) == value && value >= low && value <= high, "ASCII PLY integer out of range");
        }
    } else {
        uint8_t bytes[8]{};
        Require(bool(file.read(reinterpret_cast<char*>(bytes), property.size)), "Truncated binary PLY record");
        uint64_t bits = 0;
        for (uint32_t i = 0; i < property.size; ++i)
            bits |= uint64_t(bytes[i]) << (8 * (big ? property.size - 1 - i : i));
        if (property.floating) {
            if (property.size == 4) { const auto word = uint32_t(bits); float scalar; std::memcpy(&scalar, &word, 4); value = scalar; }
            else std::memcpy(&value, &bits, 8);
        } else if (property.signedValue) {
            int64_t scalar = int64_t(bits);
            if (bits & (uint64_t(1) << (property.size * 8 - 1))) scalar -= int64_t(uint64_t(1) << (property.size * 8));
            value = double(scalar);
        } else value = double(bits);
    }
    Require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(), "Nonfinite/out-of-range PLY attribute");
    return value;
}
std::map<std::string, size_t> Indices(const FSplatPLYHeader& header) {
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < header.properties.size(); ++i)
        Require(index.emplace(header.properties[i].name, i).second, "Duplicate PLY property");
    return index;
}
} // namespace

bool FSplatPLYStream::Probe(const std::string& path, FSplatPLYHeader& output,
    std::string& error, const FSplatPLYLimits& limits, const FSplatBuildControl* control) {
    try {
        Poll(control);
        Require(limits.maxHeaderBytes > 0 && limits.maxHeaderBytes <= (16u << 20) &&
            limits.maxRecordBytes > 0 && limits.maxRecordBytes <= (16u << 20) &&
            limits.maxProperties > 0 && limits.maxProperties <= 4096, "Invalid PLY parser limits");
        std::ifstream file(path, std::ios::binary);
        Require(bool(file), "Cannot open PLY");
        file.seekg(0, std::ios::end);
        FSplatPLYHeader header;
        header.limits = limits; header.fileBytes = Offset(file);
        Require(header.fileBytes <= limits.maxFileBytes, "PLY source byte limit exceeded");
        file.seekg(0); Require(bool(file), "Cannot seek PLY header");
        uint64_t offset = 0;
        auto readLine = [&]() { Poll(control); return Line(file, offset, limits.maxHeaderBytes, control, &header.headerIdentity); };
        Require(readLine() == "ply", "Missing PLY magic");
        bool hasFormat = false, hasVertex = false, inVertex = false, ended = false;
        while (!ended) {
            const auto line = readLine();
            std::istringstream words(line); words.imbue(std::locale::classic());
            std::string kind; words >> kind;
            if (kind.empty() || kind == "comment" || kind == "obj_info") continue;
            if (kind == "format") {
                std::string format, version, extra; words >> format >> version;
                Require(!hasFormat && version == "1.0" && !(words >> extra), "Invalid PLY format declaration");
                if (format == "ascii") header.format = ESplatPLYFormat::ASCII;
                else if (format == "binary_little_endian") header.format = ESplatPLYFormat::LittleEndian;
                else if (format == "binary_big_endian") header.format = ESplatPLYFormat::BigEndian;
                else throw std::runtime_error("Unsupported PLY format");
                hasFormat = true;
            } else if (kind == "element") {
                std::string name, count, extra; words >> name >> count;
                Require(!name.empty() && !(words >> extra), "Invalid PLY element");
                const auto amount = Unsigned(count); inVertex = name == "vertex";
                if (inVertex) {
                    Require(!hasVertex, "Duplicate PLY vertex element"); hasVertex = true;
                    header.recordCount = amount;
                    Require(amount <= limits.maxRecords, "PLY vertex limit exceeded");
                } else Require(hasVertex || !amount, "Vertex must be first nonempty PLY element");
            } else if (kind == "property" && inVertex) {
                std::string type, name, extra; words >> type >> name;
                Require(!name.empty() && type != "list" && !(words >> extra) &&
                    header.properties.size() < limits.maxProperties, "Invalid PLY vertex property");
                header.properties.push_back(Property(type, name));
            } else if (kind == "property") continue;
            else if (kind == "end_header") { std::string extra; Require(!(words >> extra), "Malformed end_header"); ended = true; }
            else throw std::runtime_error("Unknown PLY header declaration");
        }
        Require(hasFormat && hasVertex, "Missing PLY format/vertex");
        const auto index = Indices(header);
        const char* required[] = {"x","y","z","f_dc_0","f_dc_1","f_dc_2","opacity","scale_0","scale_1","scale_2","rot_0","rot_1","rot_2","rot_3"};
        for (auto name : required) Require(index.count(name) != 0, std::string("Missing Gaussian property: ") + name);
        uint32_t rest = 0;
        for (const auto& property : header.properties) {
            header.stride = Add(header.stride, property.size);
            if (property.name.rfind("f_rest_", 0) == 0) {
                const auto token = property.name.substr(7); const auto number = Unsigned(token);
                Require(number < 45 && token == std::to_string(number), "Invalid SH index"); ++rest;
            }
        }
        header.shDegree = rest == 0 ? 0 : rest == 9 ? 1 : rest == 24 ? 2 : rest == 45 ? 3 : 4;
        Require(header.shDegree <= 3, "Incomplete SH degree layout");
        for (uint32_t i = 0; i < rest; ++i) Require(index.count("f_rest_" + std::to_string(i)) != 0, "Noncontiguous SH properties");
        Require(header.stride <= limits.maxRecordBytes, "PLY binary stride limit exceeded");
        header.dataOffset = offset;
        Require(offset <= header.fileBytes, "Invalid PLY payload offset");
        if (header.format != ESplatPLYFormat::ASCII)
            Require(Multiply(header.recordCount, header.stride) <= header.fileBytes - offset, "Truncated binary PLY payload");
        else Require(Multiply(header.recordCount, header.properties.size() * 2 - 1) <= header.fileBytes - offset, "Truncated ASCII PLY payload");
        output = std::move(header); error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

uint64_t FSplatPLYStream::ParserWorkingBytes(const FSplatPLYHeader& header, uint32_t capacity) {
    Require(capacity > 0 && capacity <= 65536, "PLY batch capacity must be in [1,65536]");
    // Includes duplicate probe/header state, row scalars, property/index string
    // allocations and stream buffers; consumers additionally admit their own sink.
    return Add(Add(Multiply(capacity, sizeof(FSplatGaussian)), Multiply(header.limits.maxHeaderBytes, 8)),
        Add(Multiply(header.limits.maxRecordBytes, 2), Add(Multiply(header.limits.maxProperties, 1024), 65536)));
}

bool FSplatPLYStream::StreamRecords(const std::string& path, const FSplatPLYHeader& header,
    uint32_t capacity, FSplatBuildControl& control, const FSplatPLYBatchSink& sink,
    std::string& error, const FSplatBuildSourceCursor& resume) {
    try {
        ParserWorkingBytes(header, capacity); Require(bool(sink), "Missing PLY batch sink");
        FSplatPLYHeader checked;
        if (!Probe(path, checked, error, header.limits, &control)) return false;
        Require(checked.headerIdentity == header.headerIdentity && checked.fileBytes == header.fileBytes &&
            checked.recordCount == header.recordCount && checked.dataOffset == header.dataOffset &&
            checked.stride == header.stride && checked.shDegree == header.shDegree && checked.format == header.format,
            "PLY header/source identity changed");
        // Use the freshly parsed metadata; a caller cannot tamper property indices.
        const auto& h = checked;
        std::ifstream file(path, std::ios::binary); Require(bool(file), "Cannot open PLY records");
        const bool ascii = h.format == ESplatPLYFormat::ASCII;
        const auto index = Indices(h);
        uint64_t originalId = resume.nextOriginalId;
        uint64_t offset = resume.byteOffset;
        Require(originalId <= h.recordCount, "PLY resume row exceeds vertex count");
        if (!offset && !originalId) offset = h.dataOffset;
        Require(offset >= h.dataOffset && offset <= h.fileBytes &&
            (originalId ? offset > h.dataOffset : offset == h.dataOffset), "Invalid PLY resume cursor");
        if (!ascii) Require(offset == Add(h.dataOffset, Multiply(originalId, h.stride)), "Misaligned binary PLY resume cursor");
        else if (originalId && offset < h.fileBytes) {
            file.seekg(static_cast<std::streamoff>(offset - 1)); char previous = 0;
            Require(bool(file.get(previous)) && previous == '\n', "ASCII PLY resume is not a complete record boundary");
        }
        Require(offset <= uint64_t(std::numeric_limits<std::streamoff>::max()), "PLY offset cannot be represented");
        file.seekg(static_cast<std::streamoff>(offset)); Require(bool(file), "Cannot seek PLY records");
        std::vector<FSplatGaussian> batch; batch.reserve(capacity);
        std::vector<double> row(h.properties.size());
        const uint32_t perChannel = (h.shDegree + 1) * (h.shDegree + 1) - 1;
        for (; originalId < h.recordCount; ++originalId) {
            Poll(&control);
            const auto line = ascii ? Line(file, offset, h.limits.maxRecordBytes, &control) : std::string{};
            size_t cursor = 0;
            for (size_t p = 0; p < row.size(); ++p) {
                if ((p & 255u) == 0) Poll(&control);
                row[p] = Scalar(file, h.properties[p], h.format == ESplatPLYFormat::BigEndian, ascii ? &line : nullptr, cursor);
            }
            if (ascii) { while (cursor < line.size() && Space(line[cursor])) ++cursor; Require(cursor == line.size(), "Extra ASCII PLY row values"); }
            else offset = Add(offset, h.stride);
            auto value = [&](const std::string& name) { return row[index.at(name)]; };
            FSplatGaussian gaussian; gaussian.originalId = originalId;
            gaussian.position[0] = float(value("x")); gaussian.position[1] = float(value("y")); gaussian.position[2] = float(value("z"));
            for (int axis = 0; axis < 3; ++axis) {
                gaussian.scale[axis] = float(std::exp(value("scale_" + std::to_string(axis))));
                Require(std::isfinite(gaussian.scale[axis]) && gaussian.scale[axis] > 0, "Invalid activated Gaussian scale");
            }
            double quaternion[4], norm = 0;
            for (int axis = 0; axis < 4; ++axis) { quaternion[axis] = value("rot_" + std::to_string(axis)); norm += quaternion[axis] * quaternion[axis]; }
            Require(norm > 1e-30, "Zero Gaussian quaternion"); norm = std::sqrt(norm);
            for (int axis = 0; axis < 4; ++axis) gaussian.rotation[axis] = float(quaternion[axis] / norm);
            const auto opacity = value("opacity");
            gaussian.opacity = float(opacity >= 0 ? 1 / (1 + std::exp(-opacity)) : std::exp(opacity) / (1 + std::exp(opacity)));
            for (int channel = 0; channel < 3; ++channel) {
                gaussian.sh[channel] = float(value("f_dc_" + std::to_string(channel)));
                for (uint32_t coefficient = 0; coefficient < perChannel; ++coefficient)
                    gaussian.sh[3 * (coefficient + 1) + channel] = float(value("f_rest_" + std::to_string(channel * perChannel + coefficient)));
            }
            batch.push_back(gaussian);
            if (batch.size() == capacity || originalId + 1 == h.recordCount) {
                Poll(&control);
                if (!sink(batch, {offset, originalId + 1}, error)) {
                    if (error.empty()) error = "PLY sink rejected batch"; return false;
                }
                batch.clear();
            }
        }
        Poll(&control); error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
} // namespace MonsterRender::Splat
