// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/SplatResourceProvider.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace MonsterRender::Splat {
namespace {
bool HashValid(const std::string& hash) {
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
// This inventory protocol intentionally permits only ASCII logical names. No
// general-purpose JSON DOM or unbounded recursive structure is needed here.
class InventoryReader {
public:
    explicit InventoryReader(const std::vector<uint8_t>& bytes) : text(bytes.begin(), bytes.end()) {}
    bool Take(char c) { Space(); if (pos < text.size() && text[pos] == c) { ++pos; return true; } return false; }
    void Need(char c) { if (!Take(c)) throw std::runtime_error("Malformed resource inventory"); }
    bool End() { Space(); return pos == text.size(); }
    std::string String() {
        Need('"'); std::string value;
        while (pos < text.size()) {
            unsigned char c = text[pos++];
            if (c == '"') return value;
            if (c < 32 || c > 126) throw std::runtime_error("Nonportable resource inventory string");
            if (c == '\\') {
                if (pos >= text.size()) break;
                c = text[pos++];
                if (c != '"' && c != '\\' && c != '/') throw std::runtime_error("Unsupported resource inventory escape");
            }
            value += char(c);
            if (value.size() > 4096) throw std::runtime_error("Resource inventory string limit exceeded");
        }
        throw std::runtime_error("Unterminated resource inventory string");
    }
    uint64_t Number() {
        Space(); const size_t start = pos; uint64_t value = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            const unsigned digit = unsigned(text[pos++] - '0');
            if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
                throw std::runtime_error("Resource inventory integer overflow");
            value = value * 10 + digit;
        }
        if (pos == start || (pos - start > 1 && text[start] == '0'))
            throw std::runtime_error("Invalid resource inventory integer");
        return value;
    }
private:
    void Space() { while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' || text[pos] == '\r' || text[pos] == '\t')) ++pos; }
    std::string text;
    size_t pos = 0;
};
const FResourceIdentity* Find(const std::vector<FResourceIdentity>& entries, const std::string& name) {
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.path == name; });
    return found == entries.end() ? nullptr : &*found;
}
}

bool IsResourceLogicalPath(const std::string& path) {
    if (path.empty() || path.size() > 4096 || path.front() == '/' || path.back() == '/') return false;
    size_t start = 0;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i < path.size()) {
            const unsigned char c = path[i];
            if (c < 32 || c > 126 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
        }
        if (i == path.size() || path[i] == '/') {
            const auto component = path.substr(start, i - start);
            if (component.empty() || component == "." || component == ".." || component.back() == '.' || component.back() == ' ') return false;
            start = i + 1;
        }
    }
    return true;
}

bool ValidateResourceInventory(const std::vector<FResourceIdentity>& entries, uint64_t cap, std::string& error) {
    if (!cap || entries.size() > 4096) { error = "Resource inventory policy limit exceeded"; return false; }
    std::set<std::string> names;
    for (const auto& entry : entries) {
        if (!IsResourceLogicalPath(entry.path) || !HashValid(entry.sha256) || entry.bytes > cap || !names.insert(entry.path).second) {
            error = "Invalid, duplicate or oversized resource identity"; return false;
        }
    }
    error.clear(); return true;
}

bool ParseResourceInventory(const std::vector<uint8_t>& bytes, std::vector<FResourceIdentity>& entries, std::string& error) {
    entries.clear();
    try {
        if (bytes.empty() || bytes.size() > (4u << 20)) throw std::runtime_error("Resource inventory size limit exceeded");
        InventoryReader reader(bytes); std::vector<FResourceIdentity> parsed;
        bool schema = false, assets = false;
        reader.Need('{');
        do {
            const auto key = reader.String(); reader.Need(':');
            if (key == "schemaVersion" && !schema) {
                if (reader.Number() != 1) throw std::runtime_error("Unsupported resource inventory schema");
                schema = true;
            } else if (key == "assets" && !assets) {
                assets = true; reader.Need('[');
                if (!reader.Take(']')) {
                    do {
                        if (parsed.size() >= 4096) throw std::runtime_error("Resource inventory entry limit exceeded");
                        FResourceIdentity entry; unsigned fields = 0; reader.Need('{');
                        do {
                            const auto field = reader.String(); reader.Need(':');
                            if (field == "path" && !(fields & 1)) { entry.path = reader.String(); fields |= 1; }
                            else if (field == "bytes" && !(fields & 2)) { entry.bytes = reader.Number(); fields |= 2; }
                            else if (field == "sha256" && !(fields & 4)) { entry.sha256 = reader.String(); fields |= 4; }
                            else throw std::runtime_error("Unknown or duplicate resource identity field");
                        } while (reader.Take(','));
                        reader.Need('}');
                        if (fields != 7) throw std::runtime_error("Incomplete resource identity");
                        parsed.push_back(std::move(entry));
                    } while (reader.Take(','));
                    reader.Need(']');
                }
            } else throw std::runtime_error("Unknown or duplicate inventory field");
        } while (reader.Take(','));
        reader.Need('}');
        if (!schema || !assets || parsed.empty() || !reader.End()) throw std::runtime_error("Incomplete resource inventory");
        if (!ValidateResourceInventory(parsed, std::numeric_limits<uint64_t>::max(), error)) return false;
        entries = std::move(parsed); error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool ValidateResourceBytes(const FResourceIdentity& identity, const std::vector<uint8_t>& bytes, std::string& error) {
    if (!IsResourceLogicalPath(identity.path) || !HashValid(identity.sha256) || bytes.size() != identity.bytes || FSplatSceneFormat::SHA256(bytes) != identity.sha256) {
        error = "Resource identity mismatch"; return false;
    }
    error.clear(); return true;
}

FFileResourceProvider::FFileResourceProvider(std::string base, std::vector<FResourceIdentity> identities, uint64_t cap)
    : root(std::move(base)), inventory(std::move(identities)), maximumResourceBytes(cap) {}

bool FFileResourceProvider::Resolve(const std::string& logical, std::string& path, std::string& error) const {
    path.clear();
    try {
        if (!IsResourceLogicalPath(logical) || root.empty()) throw std::runtime_error("Invalid resource path or root");
        if (!ValidateResourceInventory(inventory, maximumResourceBytes, error)) return false;
        const auto* identity = Find(inventory, logical);
        if (!inventory.empty() && !identity) throw std::runtime_error("Resource is not in inventory");
        const auto base = std::filesystem::canonical(std::filesystem::u8path(root));
        if (!std::filesystem::is_directory(base)) throw std::runtime_error("Resource root is not a directory");
        const auto candidate = std::filesystem::canonical(base / std::filesystem::u8path(logical));
        auto parent = base.begin(), child = candidate.begin();
        for (; parent != base.end(); ++parent, ++child)
            if (child == candidate.end() || *parent != *child) throw std::runtime_error("Resource escapes root");
        if (!std::filesystem::is_regular_file(candidate)) throw std::runtime_error("Resource is not a regular file");
        const auto size = std::filesystem::file_size(candidate);
        if (size > maximumResourceBytes || size > uint64_t(std::numeric_limits<std::streamsize>::max()) || size > uint64_t(std::numeric_limits<size_t>::max()) || (identity && size != identity->bytes))
            throw std::runtime_error("Resource size exceeds admission or identity");
        const auto utf8 = candidate.u8string(); path.assign(utf8.begin(), utf8.end());
        error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool FFileResourceProvider::ReadBytes(const std::string& logical, std::vector<uint8_t>& bytes, std::string& error) {
    bytes.clear();
    try {
        std::string path;
        if (!Resolve(logical, path, error)) return false;
        std::ifstream input(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Cannot open resource");
        const auto size = input.tellg();
        if (size < 0 || uint64_t(size) > maximumResourceBytes || uint64_t(size) > uint64_t(std::numeric_limits<size_t>::max()) || uint64_t(size) > uint64_t(std::numeric_limits<std::streamsize>::max())) throw std::runtime_error("Invalid resource length");
        std::vector<uint8_t> result(static_cast<size_t>(size)); input.seekg(0);
        if (!input || (size && !input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size))) || input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Resource read failed or file changed");
        if (const auto* identity = Find(inventory, logical); identity && !ValidateResourceBytes(*identity, result, error)) return false;
        bytes = std::move(result); error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool FFileResourceProvider::ResolveValidatedFile(const std::string& logical, std::string& path, std::string& error) {
    path.clear(); std::vector<uint8_t> bytes;
    if (!ReadBytes(logical, bytes, error)) return false;
    return Resolve(logical, path, error);
}
} // namespace MonsterRender::Splat
