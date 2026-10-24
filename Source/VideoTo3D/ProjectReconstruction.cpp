// Copyright Monster Engine. All Rights Reserved.
#include "VideoTo3D/ProjectReconstruction.h"
#include "VideoTo3D/WindowsProcessHost.h"
#include "VideoTo3D/ToolAdmission.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <chrono>
#include <limits>
#include <cstdio>
#include <set>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace MonsterEngine::VideoTo3D {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t kMetadataLimit = 4 * 1024 * 1024;
constexpr std::size_t kEventLimit = 64 * 1024;
constexpr std::uint64_t kEventsLimit = 64ULL * 1024 * 1024;
[[noreturn]] void Bad(const std::string& message) { throw std::runtime_error(message); }
std::string Utf8(const fs::path& p) {
    const auto text = p.u8string(); return {reinterpret_cast<const char*>(text.data()), text.size()};
}
fs::path Path(const std::string& text) {
    if (text.empty() || text.size() > 32768 || text.find('\0') != std::string::npos) Bad("Invalid path text");
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size) Bad("Invalid UTF-8 path");
    std::wstring wide(size, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size) != size) Bad("Cannot decode path");
    return fs::path(wide);
}
bool Uuid(std::string_view s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (s[i] != '-') return false; }
        else if (!(s[i] >= '0' && s[i] <= '9') && !(s[i] >= 'a' && s[i] <= 'f')) return false;
    }
    return true;
}
// Bounded JSON decoder used only for independent local execution records. No
// permissive fallback, duplicate keys, unknown framing or unbounded recursion.
struct Json {
    enum class Kind { Null, Bool, Number, String, Object, Array } kind{Kind::Null};
    std::string text;
    std::map<std::string, Json> object;
    std::vector<Json> array;
    const Json& At(const std::string& key) const {
        if (kind != Kind::Object) Bad("Expected JSON object");
        const auto i = object.find(key); if (i == object.end()) Bad("Missing execution field: " + key); return i->second;
    }
    std::string String() const { if (kind != Kind::String) Bad("Expected JSON string"); return text; }
    std::uint64_t Number() const {
        if (kind != Kind::Number || text.empty()) Bad("Expected unsigned integer");
        std::uint64_t n = 0; const auto [p, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
        if (ec != std::errc{} || p != text.data() + text.size()) Bad("Invalid unsigned integer"); return n;
    }
    bool Bool() const { if (kind != Kind::Bool) Bad("Expected JSON boolean"); return text == "true"; }
};
class Decoder {
    std::string_view s_; std::size_t p_{0}; std::size_t nodes_{0};
    void Space() { while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\r' || s_[p_] == '\n' || s_[p_] == '\t')) ++p_; }
    char Take() { if (p_ == s_.size()) Bad("Truncated JSON"); return s_[p_++]; }
    unsigned Hex() { unsigned n = 0; for (int i = 0; i < 4; ++i) { const char c = Take(); n <<= 4; if (c >= '0' && c <= '9') n += c-'0'; else if (c >= 'a' && c <= 'f') n += c-'a'+10; else if (c >= 'A' && c <= 'F') n += c-'A'+10; else Bad("Invalid JSON escape"); } return n; }
    std::string String() {
        if (Take() != '"') Bad("Expected JSON string"); std::string out;
        for (;;) {
            const auto c = static_cast<unsigned char>(Take()); if (c == '"') break;
            if (c < 32) Bad("JSON control character");
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            switch (Take()) {
            case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = Hex();
                if (cp >= 0xd800 && cp <= 0xdbff) { if (Take() != '\\' || Take() != 'u') Bad("Missing low surrogate"); const auto lo = Hex(); if (lo < 0xdc00 || lo > 0xdfff) Bad("Invalid low surrogate"); cp = 0x10000 + ((cp-0xd800)<<10) + lo-0xdc00; }
                else if (cp >= 0xdc00 && cp <= 0xdfff) Bad("Unexpected low surrogate");
                if (cp < 0x80) out.push_back(static_cast<char>(cp));
                else if (cp < 0x800) { out.push_back(static_cast<char>(0xc0|(cp>>6))); out.push_back(static_cast<char>(0x80|(cp&63))); }
                else if (cp < 0x10000) { out.push_back(static_cast<char>(0xe0|(cp>>12))); out.push_back(static_cast<char>(0x80|((cp>>6)&63))); out.push_back(static_cast<char>(0x80|(cp&63))); }
                else { out.push_back(static_cast<char>(0xf0|(cp>>18))); out.push_back(static_cast<char>(0x80|((cp>>12)&63))); out.push_back(static_cast<char>(0x80|((cp>>6)&63))); out.push_back(static_cast<char>(0x80|(cp&63))); } break;
            }
            default: Bad("Invalid JSON escape");
            }
        }
        if (!out.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, out.data(), static_cast<int>(out.size()), nullptr, 0)) Bad("Invalid UTF-8 JSON");
        return out;
    }
    Json Value(unsigned depth) {
        if (depth > 32 || ++nodes_ > 100000) Bad("JSON nesting/node limit exceeded"); Space(); Json out; const char c = Take();
        if (c == '{') {
            out.kind = Json::Kind::Object; Space(); if (p_ < s_.size() && s_[p_] == '}') { ++p_; return out; }
            for (;;) { Space(); auto key = String(); Space(); if (Take() != ':') Bad("Expected JSON colon"); auto value = Value(depth+1); if (!out.object.emplace(key, std::move(value)).second) Bad("Duplicate JSON key"); Space(); const char next = Take(); if (next == '}') break; if (next != ',') Bad("Expected JSON comma"); }
        } else if (c == '[') {
            out.kind = Json::Kind::Array; Space(); if (p_ < s_.size() && s_[p_] == ']') { ++p_; return out; }
            for (;;) { out.array.push_back(Value(depth+1)); Space(); const char next = Take(); if (next == ']') break; if (next != ',') Bad("Expected JSON comma"); }
        } else if (c == '"') { --p_; out.kind = Json::Kind::String; out.text = String(); }
        else if (c == 't' || c == 'f' || c == 'n') {
            const std::string_view literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
            --p_; if (s_.substr(p_, literal.size()) != literal) Bad("Invalid JSON literal"); p_ += literal.size(); out.kind = c == 'n' ? Json::Kind::Null : Json::Kind::Bool; out.text = literal;
        } else {
            out.kind = Json::Kind::Number; const auto start = --p_;
            if (p_ < s_.size() && s_[p_] == '-') ++p_;
            if (p_ == s_.size() || s_[p_] < '0' || s_[p_] > '9') Bad("Invalid JSON number");
            if (s_[p_] == '0') ++p_; else while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
            if (p_ < s_.size() && s_[p_] == '.') { ++p_; auto a = p_; while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_; if (a == p_) Bad("Invalid JSON fraction"); }
            if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) { ++p_; if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-')) ++p_; auto a = p_; while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_; if (a == p_) Bad("Invalid JSON exponent"); }
            out.text = s_.substr(start, p_-start);
        }
        return out;
    }
public:
    explicit Decoder(std::string_view text) : s_(text) { if (s_.size() > kMetadataLimit) Bad("JSON metadata limit exceeded"); }
    Json Parse() { auto j = Value(0); Space(); if (p_ != s_.size()) Bad("Trailing JSON bytes"); return j; }
};
std::string Q(const std::string& s) { return "\"" + EscapeJson(s) + "\""; }
std::string Encode(const Json& j) {
    switch(j.kind){
    case Json::Kind::Null:return "null";
    case Json::Kind::Bool:case Json::Kind::Number:return j.text;
    case Json::Kind::String:return Q(j.text);
    case Json::Kind::Array:{std::string out="[";for(const auto& value:j.array){if(out.size()>1)out+=",";out+=Encode(value);}return out+"]";}
    case Json::Kind::Object:{std::string out="{";for(const auto& [key,value]:j.object){if(out.size()>1)out+=",";out+=Q(key)+":"+Encode(value);}return out+"}";}
    }Bad("Unknown JSON kind");
}
fs::path LocalPath(const fs::path& path) {
    const auto& name = path.native();
    // Repository leases use GetFinalPathNameByHandleW's extended DOS form.
    // Present an ordinary rooted drive path to tools that do not implement the
    // extended Win32 namespace; never reinterpret device or extended UNC paths.
    if (name.starts_with(L"\\\\?\\")) {
        if (name.size() < 7 || !((name[4] >= L'A' && name[4] <= L'Z') ||
            (name[4] >= L'a' && name[4] <= L'z')) || name[5] != L':' || name[6] != L'\\')
            Bad("Only local extended DOS paths are supported");
        return fs::path(name.substr(4));
    }
    return path;
}
void Safe(const fs::path& p, bool missing = false) {
    const auto local = LocalPath(p);
    if (!local.is_absolute() || local.native().find(L'\0') != std::wstring::npos) Bad("An explicit absolute path is required");
    fs::path current = local.root_path();
    for (const auto& part : local.relative_path()) {
        const auto& name = part.native();
        if (name.empty() || part == L".." || part == L"." || name.back() == L'.' || name.back() == L' ' || name.find(L':') != std::wstring::npos) Bad("Unsafe path component");
        current /= part; const auto attr = GetFileAttributesW(current.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (missing && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) continue;
            Bad("Cannot inspect execution path (Win32 " + std::to_string(error) + ")");
        }
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT) Bad("Reparse points are not accepted in execution paths");
    }
}
fs::path Relative(const fs::path& root, const std::string& text) {
    auto relative = Path(text);
    if (relative.is_absolute() || relative.has_root_path() || text.size() > 4096) Bad("Unsafe execution-relative path");
    for (const auto& c : relative) {
        const auto name = c.native();
        if (name.empty() || name == L".." || name == L"." || name.back() == L'.' || name.back() == L' ' || name.find_first_of(L":*?\"<>|\r\n") != std::wstring::npos) Bad("Unsafe execution-relative component");
    }
    const auto path = root / relative; Safe(path); return path;
}
std::string Read(const fs::path& path, std::size_t limit = kMetadataLimit) {
    Safe(path); const auto size = fs::file_size(path); if (size > limit) Bad("Execution file exceeds byte limit");
    std::ifstream stream(path, std::ios::binary); if (!stream) Bad("Cannot open execution file");
    std::string bytes(static_cast<std::size_t>(size), '\0'); if (size && !stream.read(bytes.data(), static_cast<std::streamsize>(size))) Bad("Cannot read execution file");
    return bytes;
}
void Publish(const fs::path& path, const std::string& bytes, bool replace = false) {
    if (bytes.size()>kMetadataLimit) Bad("Execution publication exceeds byte limit");
    Safe(path.parent_path()); Safe(path, true);
    const auto temporary = path.parent_path() / (L".execution-" + Path(ProjectRepository::NewId()).native() + L".tmp");
    const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) Bad("Cannot create execution metadata");
    DWORD written = 0; bool ok = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    if (ok) ok = FlushFileBuffers(handle) != FALSE;
    if (!CloseHandle(handle)) ok = false;
    if (!ok) Bad("Cannot flush execution metadata; temporary file retained");
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) Bad("Cannot publish execution metadata; previous state retained");
}
void Directory(const fs::path& p) { Safe(p, true); if (!fs::create_directory(p) && !fs::is_directory(p)) Bad("Cannot create execution directory"); Safe(p); }
std::string Hash(const fs::path& path, const std::string& pin = {}) {
    Safe(path); const auto identity = InspectToolIdentity(ToolKind::Brush, path, pin);
    if (identity.status != (pin.empty() ? IdentityStatus::Unpinned : IdentityStatus::Match)) Bad("Identity rejected for " + Utf8(path) + ": " + identity.error);
    return identity.actualSha256;
}
std::string InputJson(const InputManifest& m) {
    const auto& f = m.files.at(0);
    return "{\"id\":"+Q(m.inputId)+",\"revision\":"+std::to_string(m.revision)+",\"relativePath\":"+Q(f.relativePath)+",\"bytes\":"+std::to_string(f.byteCount)+",\"sha256\":"+Q(f.sha256)+"}";
}
void MatchInput(const Json& j, const InputManifest& input) {
    const auto& f = input.files.at(0);
    if (j.At("id").String() != input.inputId || j.At("revision").Number() != input.revision || j.At("relativePath").String() != f.relativePath || j.At("bytes").Number() != f.byteCount || j.At("sha256").String() != f.sha256) Bad("Execution input identity differs from the queue-owned imported input");
}
ExecutionState State(const std::string& s) {
    for (auto state : {ExecutionState::None, ExecutionState::Preparing, ExecutionState::Training, ExecutionState::Pausing, ExecutionState::Saving, ExecutionState::Paused, ExecutionState::Restoring, ExecutionState::Completed, ExecutionState::Failed, ExecutionState::Stopped, ExecutionState::Interrupted}) if (s == ExecutionStateName(state)) return state;
    Bad("Unknown execution state");
}
std::string UtcNow() {
    SYSTEMTIME t{};GetSystemTime(&t);char text[32]{};
    std::snprintf(text,sizeof(text),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);return text;
}
void ValidateUtc(const std::string& text) {
    if(text.size()!=24||text[4]!='-'||text[7]!='-'||text[10]!='T'||text[13]!=':'||text[16]!=':'||text[19]!='.'||text[23]!='Z')Bad("Invalid UTC timestamp");
    for(std::size_t i=0;i<text.size();++i)if(i!=4&&i!=7&&i!=10&&i!=13&&i!=16&&i!=19&&i!=23&&(text[i]<'0'||text[i]>'9'))Bad("Invalid UTC digits");
    auto number=[&](std::size_t offset,std::size_t length){unsigned value=0;for(std::size_t i=offset;i<offset+length;++i)value=value*10+static_cast<unsigned>(text[i]-'0');return value;};
    const auto year=number(0,4),month=number(5,2),day=number(8,2);
    if(!year||month<1||month>12||number(11,2)>23||number(14,2)>59||number(17,2)>59)Bad("Invalid UTC calendar value");
    constexpr unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    const auto maximum=days[month-1]+(month==2&&year%4==0&&(year%100!=0||year%400==0)?1u:0u);
    if(day<1||day>maximum)Bad("Invalid UTC calendar day");
}
void ValidateStartedBoundary(const std::string& boundary) {
    if(boundary!="preparation_start_v1"&&boundary!="execution_start_v1")Bad("Unsupported timing start boundary");
}
std::uint64_t AddTime(std::uint64_t a,std::uint64_t b) {
    if(b>std::numeric_limits<std::uint64_t>::max()-a)Bad("Active time exceeds supported range");return a+b;
}
std::string OptionalNumber(const std::optional<std::uint64_t>& n){return n?std::to_string(*n):"null";}
std::string OptionalText(const std::optional<std::string>& t){return t?Q(*t):"null";}
void ReadObservations(const Json& pointer,ExecutionSnapshot& s) {
    if(const auto it=pointer.object.find("progress");it!=pointer.object.end()){
        const auto& p=it->second;if(p.kind!=Json::Kind::Object||p.object.size()!=5||p.At("schemaVersion").Number()!=1)Bad("Unsupported progress metadata");
        auto number=[&](const char* name)->std::optional<std::uint64_t>{const auto& v=p.At(name);return v.kind==Json::Kind::Null?std::nullopt:std::optional<std::uint64_t>(v.Number());};
        s.progress={number("observedIteration"),number("completedIterations"),number("totalIterations"),number("sampleSequence")};const auto& v=s.progress;
        if((v.totalIterations&&(!*v.totalIterations||*v.totalIterations>10000000))||(v.completedIterations&&v.totalIterations&&*v.completedIterations>*v.totalIterations)||(v.observedIteration&&v.totalIterations&&*v.observedIteration>=*v.totalIterations))Bad("Invalid progress bounds");
        if(s.state!=ExecutionState::Completed&&v.completedIterations&&v.totalIterations&&*v.completedIterations==*v.totalIterations)Bad("Unpublished target progress");
    }
    if(const auto it=pointer.object.find("timing");it!=pointer.object.end()){
        const auto& t=it->second;if(t.kind!=Json::Kind::Object||(t.object.size()!=5&&t.object.size()!=6)||t.At("schemaVersion").Number()!=1)Bad("Unsupported timing metadata");
        if(t.object.size()==6&&!t.object.contains("startedBoundary"))Bad("Unsupported timing metadata field");
        const auto& ms=t.At("activeMilliseconds");if(ms.kind!=Json::Kind::Null)s.timing.activeMilliseconds=ms.Number();
        auto text=[&](const char* name)->std::optional<std::string>{const auto& v=t.At(name);if(v.kind==Json::Kind::Null)return {};auto result=v.String();ValidateUtc(result);return result;};
        s.timing.startedUtc=text("startedUtc");s.timing.completedUtc=text("completedUtc");s.timing.activeTimeComplete=t.At("activeTimeComplete").Bool();
        if(const auto boundary=t.object.find("startedBoundary");boundary!=t.object.end()){
            s.timing.startedBoundary=boundary->second.String();ValidateStartedBoundary(*s.timing.startedBoundary);
            if(!s.timing.startedUtc)Bad("Timing start boundary requires recorded start");
        }
        if(s.timing.activeTimeComplete&&!s.timing.activeMilliseconds)Bad("Complete time requires recorded intervals");if(s.timing.completedUtc&&s.state!=ExecutionState::Completed)Bad("Completion UTC on unpublished execution");
    }
}
fs::path VerifyReference(const fs::path& root, const Json& reference,std::uint64_t byteLimit=kMaxInputFileBytes) {
    const auto path = Relative(root, reference.At("relativePath").String());
    if (reference.At("bytes").Number()>byteLimit||fs::file_size(path) != reference.At("bytes").Number()) Bad("Published execution reference length mismatch");
    Hash(path, reference.At("sha256").String()); return path;
}
// COLMAP binaries are little endian. Bounds are checked before every read/skip;
// no input path from the runner is used to locate the camera files.
struct CameraBytes {
    std::string bytes; std::size_t offset{0};
    template<class T> T Get() {
        if (sizeof(T)>bytes.size()-offset) Bad("Truncated reconstruction camera data");
        T value{}; std::memcpy(&value,bytes.data()+offset,sizeof(T)); offset+=sizeof(T); return value;
    }
    void Skip(std::uint64_t size) { if(size>bytes.size()-offset) Bad("Truncated camera observations"); offset+=static_cast<std::size_t>(size); }
    void End() const { if(offset!=bytes.size()) Bad("Trailing reconstruction camera data"); }
};
std::optional<ReconstructionView> InitialView(const fs::path& execution,const Json& candidate) {
    const auto prepared=candidate.object.find("prepared");
    if(prepared==candidate.object.end())return {}; // Legacy fixture/producer: bounded PLY framing.
    if(prepared->second.At("schemaVersion").Number()!=1)Bad("Unsupported prepared camera schema");
    const auto& inventory=prepared->second.At("datasetInventory");
    if(inventory.kind!=Json::Kind::Array||inventory.array.empty())Bad("Missing prepared camera inventory");
    std::map<std::string,Json> files;
    for(const auto& entry:inventory.array){const auto name=entry.At("path").String();if(!files.emplace(name,entry).second)Bad("Duplicate prepared inventory path");}
    auto verified=[&](const std::string& name,std::size_t limit){
        const auto item=files.find(name);if(item==files.end())Bad("Prepared camera identity missing: "+name);
        const auto path=Relative(execution,"preparation/dataset/"+name);
        if(fs::file_size(path)!=item->second.At("bytes").Number()||fs::file_size(path)>limit)Bad("Prepared camera length rejected");
        Hash(path,item->second.At("sha256").String());
        auto bytes=Read(path,limit);
        Hash(path,item->second.At("sha256").String());
        return CameraBytes{std::move(bytes)};
    };
    auto cameras=verified("sparse/cameras.bin",1024*1024);
    struct Intrinsics{double fov,aspect;};std::map<std::int32_t,Intrinsics> intrinsics;
    const auto cameraCount=cameras.Get<std::uint64_t>();if(!cameraCount||cameraCount>4096)Bad("Camera count outside preview bounds");
    for(std::uint64_t n=0;n<cameraCount;++n){
        const auto id=cameras.Get<std::int32_t>(),model=cameras.Get<std::int32_t>();
        const auto width=cameras.Get<std::uint64_t>(),height=cameras.Get<std::uint64_t>();
        if(id<=0||(model!=0&&model!=1)||!width||!height||width>16384||height>16384)Bad("Unsupported preview camera intrinsics");
        double p[4]{};for(int k=0;k<(model==0?3:4);++k){p[k]=cameras.Get<double>();if(!std::isfinite(p[k]))Bad("Nonfinite camera intrinsics");}
        const double fx=p[0],fy=model==0?p[0]:p[1],cx=model==0?p[1]:p[2],cy=model==0?p[2]:p[3];
        const double fov=2*std::atan(double(height)/(2*fy))*180/3.14159265358979323846;
        // Orbit uses a symmetric FOV. This reconstruction route supports the
        // centered undistorted cameras emitted by our preparation pipeline.
        if(fx<=0||fy<=0||fov<1||fov>170||std::abs(cx-double(width)/2)>1||std::abs(cy-double(height)/2)>1)Bad("Preview requires centered finite pinhole intrinsics");
        if(!intrinsics.emplace(id,Intrinsics{fov,double(width)/double(height)}).second)Bad("Duplicate camera ID");
    }cameras.End();
    auto images=verified("sparse/images.bin",64*1024*1024);
    const auto count=images.Get<std::uint64_t>();if(!count||count>100000)Bad("Image count outside preview bounds");
    struct Pose{std::string name;ReconstructionView view;};std::vector<Pose> poses;poses.reserve(static_cast<std::size_t>(count));std::set<std::int32_t> ids;std::set<std::string> names;
    for(std::uint64_t n=0;n<count;++n){
        const auto id=images.Get<std::int32_t>();if(id<=0||!ids.insert(id).second)Bad("Invalid or duplicate image ID");
        double q[4],t[3];for(double& v:q)v=images.Get<double>();for(double& v:t)v=images.Get<double>();
        double norm=0;for(double v:q){if(!std::isfinite(v))Bad("Nonfinite camera rotation");norm+=v*v;}for(double v:t)if(!std::isfinite(v)||std::abs(v)>1e12)Bad("Invalid camera translation");
        if(std::abs(norm-1)>1e-5)Bad("Nonunit camera quaternion");
        const auto camera=intrinsics.find(images.Get<std::int32_t>());if(camera==intrinsics.end())Bad("Unknown image camera ID");
        std::string name;for(;;){const auto c=images.Get<char>();if(!c)break;if(name.size()>=4096)Bad("Camera image name too long");name+=c;}
        if(name.empty()||name.find_first_of("/\\:")!=std::string::npos||!names.insert(name).second)Bad("Invalid or duplicate camera image name");
        const auto points=images.Get<std::uint64_t>();if(points>100000000)Bad("Camera observation count outside bounds");images.Skip(points*24);
        const double w=q[0],x=q[1],y=q[2],z=q[3];
        const double r[3][3]={{1-2*y*y-2*z*z,2*x*y-2*w*z,2*x*z+2*w*y},{2*x*y+2*w*z,1-2*x*x-2*z*z,2*y*z-2*w*x},{2*x*z-2*w*y,2*y*z+2*w*x,1-2*x*x-2*y*y}};
        ReconstructionView view;view.verticalFOVDegrees=camera->second.fov;view.aspectRatio=camera->second.aspect;
        for(int k=0;k<3;++k){view.eye[k]=-(r[0][k]*t[0]+r[1][k]*t[1]+r[2][k]*t[2]);view.forward[k]=r[2][k];view.up[k]=-r[1][k];}
        view.imageName=name;poses.push_back({std::move(name),std::move(view)});
    }images.End();
    std::sort(poses.begin(),poses.end(),[](const auto& a,const auto& b){return a.name<b.name;});
    auto view=std::move(poses[poses.size()/2].view);
    view.camerasSha256=files.at("sparse/cameras.bin").At("sha256").String();view.imagesSha256=files.at("sparse/images.bin").At("sha256").String();return view;
}
struct Config { Json json; std::string hash; fs::path python; fs::path runner; std::chrono::milliseconds timeout; std::string bytes; };
void ValidateSettings(const Json& settings) {
    const std::map<std::string,std::pair<double,double>> limits={{"extractionFps",{0.1,30}},{"extractionWidth",{64,4096}},{"colmapThreads",{1,32}},{"colmapSeed",{0,2147483647}},{"brushSteps",{1,100000}},{"brushSeed",{0,2147483647}},{"maxResolution",{64,4096}},{"maxSplats",{1,2000000}},{"refineEvery",{1,100000}},{"evalEvery",{1,100000}},{"exportEvery",{1,100000}},{"evalSplitEvery",{2,1000}}};
    if(settings.kind!=Json::Kind::Object||settings.object.size()!=limits.size())Bad("All bounded settings must be explicit; unknown settings rejected");
    for(const auto& [key,range]:limits){const auto& value=settings.At(key);double n=0;
        if(value.kind!=Json::Kind::Number)Bad("Invalid setting: "+key);
        const auto [end,error]=std::from_chars(value.text.data(),value.text.data()+value.text.size(),n);
        if(error!=std::errc{}||end!=value.text.data()+value.text.size()||!std::isfinite(n)||n<range.first||n>range.second)Bad("Setting outside bounds: "+key);
        if(key!="extractionFps")value.Number();
    }
}
Config Admit(const fs::path& path,bool strict=false) {
    const auto before=Hash(path); const auto bytes = Read(path, 1024*1024); Config c{Decoder(bytes).Parse(), Hash(path), {}, {}, {},bytes};
    if (before!=c.hash) Bad("Configuration changed during inspection");
    if (c.json.At("schemaVersion").Number() != 1) Bad("Unsupported reconstruction configuration schema");
    for (const auto& group : {"tools", "helpers"}) {
        const std::vector<std::string> names = std::string_view(group) == "tools" ? std::vector<std::string>{"ffprobe","ffmpeg","colmap","brush","python"} : std::vector<std::string>{"runner","preparation"};
        if(strict&&c.json.At(group).object.size()!=names.size())Bad("Unknown or missing tool/helper roles");
        for (const auto& name : names) {
            const auto& record = c.json.At(group).At(name); const auto p = Path(record.At("path").String()); Hash(p, record.At("sha256").String());
            if (name == "python") c.python = p; if (name == "runner") c.runner = p;
        }
    }
    if(strict){
        ValidateSettings(c.json.At("settings"));
        const std::vector<std::string> names={"wholeRun","ffprobe","ffmpeg","colmapFeature","colmapMatch","colmapMapper","colmapOther","brush","control","retirement"};
        const auto& budgets=c.json.At("budgetsSeconds");if(budgets.object.size()!=names.size())Bad("Unknown or missing stage budgets");
        for(const auto& name:names){const auto& value=budgets.At(name);double n=0;if(value.kind!=Json::Kind::Number)Bad("Invalid stage budget");const auto [end,error]=std::from_chars(value.text.data(),value.text.data()+value.text.size(),n);if(error!=std::errc{}||end!=value.text.data()+value.text.size()||!std::isfinite(n)||n<=0||n>86400)Bad("Stage budget outside bounds");}
    }
    const auto seconds = c.json.At("budgetsSeconds").At("wholeRun").Number();
    if (seconds == 0 || seconds > 86400) Bad("Whole-run budget must be between 1 and 86400 seconds");
    c.timeout = std::chrono::milliseconds(seconds*1000); return c;
}
} // namespace

const char* ReconstructionProfileName(ReconstructionProfile p) noexcept {
    switch(p){case ReconstructionProfile::Low:return "Low";case ReconstructionProfile::Medium:return "Medium";case ReconstructionProfile::High:return "High";case ReconstructionProfile::Advanced:return "Advanced";}return "Unknown";
}
ReconstructionProfileSettings GetReconstructionProfileSettings(ReconstructionProfile p) {
    switch(p){case ReconstructionProfile::Low:return {300,512,25000};case ReconstructionProfile::Medium:return {600,640,50000};case ReconstructionProfile::High:return {1200,960,100000};default:Bad("Unknown fixed reconstruction profile");}
}
fs::path DefaultRuntimeConfigurationPath() {
    PWSTR value=nullptr;const auto result=SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&value);
    if(FAILED(result)||!value){if(value)CoTaskMemFree(value);Bad("Cannot resolve Windows local application data for Studio runtime setup");}
    const fs::path base(value);CoTaskMemFree(value);return base/L"MonsterEngine"/L"MonsterStudio"/L"runtime-config.json";
}
fs::path DiscoverRuntimeConfiguration() {
    fs::path path;
    SetLastError(ERROR_SUCCESS);const auto size=GetEnvironmentVariableW(L"MONSTER_VIDEO_TO3D_CONFIG",nullptr,0);
    if(!size&&GetLastError()!=ERROR_ENVVAR_NOT_FOUND)Bad("MONSTER_VIDEO_TO3D_CONFIG override is empty or cannot be read; remove it or supply a trusted absolute config path");
    if(size){std::wstring text(size,L'\0');const auto actual=GetEnvironmentVariableW(L"MONSTER_VIDEO_TO3D_CONFIG",text.data(),size);if(!actual||actual>=size)Bad("Runtime override changed during discovery");text.resize(actual);path=text;}
    else path=DefaultRuntimeConfigurationPath();
    try{Admit(path,true);}catch(const std::exception& e){Bad("Studio runtime is unavailable or rejected. Use Runtime setup to install a trusted local configuration. "+std::string(e.what()));}return path;
}
fs::path ProvisionRuntimeConfiguration(const fs::path& source) {
    const auto admitted=Admit(source,true);const auto target=DefaultRuntimeConfigurationPath();
    Safe(target.parent_path(),true);std::error_code error;fs::create_directories(target.parent_path(),error);if(error)Bad("Cannot create local runtime settings directory: "+error.message());Safe(target.parent_path());
    if(Admit(source,true).hash!=admitted.hash)Bad("Runtime source changed during setup");
    Publish(target,admitted.bytes,true);return target;
}

const char* ExecutionStateName(ExecutionState state) noexcept {
    switch (state) {
    case ExecutionState::None: return "None"; case ExecutionState::Preparing: return "Preparing"; case ExecutionState::Training: return "Training"; case ExecutionState::Pausing: return "Pausing"; case ExecutionState::Saving: return "Saving"; case ExecutionState::Paused: return "Paused"; case ExecutionState::Restoring: return "Restoring"; case ExecutionState::Completed: return "Completed"; case ExecutionState::Failed: return "Failed"; case ExecutionState::Stopped: return "Stopped"; case ExecutionState::Interrupted: return "Interrupted";
    } return "Unknown";
}

struct ProjectReconstruction::Impl {
    ReconstructionQueue& queue; fs::path configPath;
    std::optional<ReconstructionProfile> profile;
    std::string configurationJSON;
    mutable std::mutex mutex; ExecutionSnapshot snapshot;
    std::atomic_bool cancel{false};
    fs::path controls; std::uint64_t generation{0};
    std::uint64_t configuredSteps{0};
    std::optional<std::uint64_t> preparationPrefix;
    std::optional<std::string> preparationStartedUtc;
    bool timingRunning{false};
    std::chrono::steady_clock::time_point intervalStart{};
    std::uint64_t intervalBase{0};
    bool forcePending{false}; std::string checkpoint;
    std::string pauseRequest;
    explicit Impl(ReconstructionQueue& q, fs::path config, std::optional<ReconstructionProfile> selected={}) : queue(q), configPath(std::move(config)),profile(selected) {}
    ExecutionSnapshot Copy() const {
        std::lock_guard lock(mutex);auto copy=snapshot;
        if(timingRunning)copy.timing.activeMilliseconds=AddTime(intervalBase,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-intervalStart).count()));return copy;
    }
    void FinishTiming(ExecutionSnapshot& s) {
        std::lock_guard lock(mutex);if(timingRunning){s.timing.activeMilliseconds=AddTime(intervalBase,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-intervalStart).count()));timingRunning=false;snapshot.timing=s.timing;}
    }
    bool Pause(std::uint64_t nextIteration, bool scheduled) {
        std::lock_guard lock(mutex); auto& s=snapshot;
        if (!s.active || !s.pauseAvailable || s.controlPending || forcePending || controls.empty() || (scheduled && (nextIteration<1 || nextIteration>configuredSteps))) return false;
        const auto id=ProjectRepository::NewId();
        try { Publish(controls/Path(id+".json"),"{\"schemaVersion\":1,\"executionId\":"+Q(s.executionId)+",\"generation\":"+std::to_string(generation)+",\"requestId\":"+Q(id)+",\"command\":\"PauseAndSave\""+(scheduled?",\"atNextIteration\":"+std::to_string(nextIteration):"")+"}"); }
        catch (const std::exception& e) { s.message=std::string("Cannot publish pause request: ")+e.what(); return false; }
        pauseRequest=id;
        s.controlPending=true; s.pauseAvailable=false; s.state=ExecutionState::Pausing; s.message="Pause requested; waiting for durable save acknowledgement"; return true;
    }
    void Emit(const ExecutionObserver& observer) { if (observer) observer(Copy()); }
    InputManifest Input() { auto m = queue.InspectInput(); if (m.projectId != queue.Snapshot().projectId || m.kind != InputKind::Video || m.files.size() != 1) Bad("Reconstruction requires exactly one queue-owned imported video"); return m; }
    fs::path Root() const { return LocalPath(queue.Directory()) / L"executions"; }
    fs::path Pointer() const { return Root() / L"latest.json"; }
    // This pointer is separate from the immutable attempts and never changes v1 files.
    void Persist(const ExecutionSnapshot& s, const InputManifest& input, const std::string& configHash, const std::string& candidatePath = {}, const std::string& candidateHash = {}) {
        const std::string bytes = "{\"schemaVersion\":"+std::string(configurationJSON.empty()?"1":"2")+",\"projectId\":"+Q(queue.Snapshot().projectId)+",\"executionId\":"+Q(s.executionId)+",\"generation\":"+std::to_string(generation)+",\"input\":"+InputJson(input)+",\"configSha256\":"+Q(configHash)+",\"state\":"+Q(ExecutionStateName(s.state))+",\"message\":"+Q(s.message)+",\"candidatePath\":"+Q(candidatePath)+",\"candidateSha256\":"+Q(candidateHash)+(configurationJSON.empty()?"":",\"configuration\":"+configurationJSON)+
            ",\"progress\":{\"schemaVersion\":1,\"observedIteration\":"+OptionalNumber(s.progress.observedIteration)+",\"completedIterations\":"+OptionalNumber(s.progress.completedIterations)+",\"totalIterations\":"+OptionalNumber(s.progress.totalIterations)+",\"sampleSequence\":"+OptionalNumber(s.progress.sampleSequence)+"},\"timing\":{\"schemaVersion\":1,\"activeMilliseconds\":"+OptionalNumber(s.timing.activeMilliseconds)+",\"startedUtc\":"+OptionalText(s.timing.startedUtc)+",\"completedUtc\":"+OptionalText(s.timing.completedUtc)+",\"activeTimeComplete\":"+(s.timing.activeTimeComplete?"true":"false")+(s.timing.startedBoundary?",\"startedBoundary\":"+Q(*s.timing.startedBoundary):"")+"}}";
        Publish(s.directory / (L"native-state-"+std::to_wstring(generation)+L".json"), bytes, true);
        Publish(Pointer(), bytes, true);
    }
    Json Candidate(const Json& pointer, const InputManifest& input, const fs::path& directory) {
        const auto candidatePath = Relative(directory, pointer.At("candidatePath").String()); if(fs::file_size(candidatePath)>kMetadataLimit)Bad("Candidate metadata exceeds byte limit");Hash(candidatePath, pointer.At("candidateSha256").String());
        if (candidatePath.lexically_normal()!=(directory/(L"attempt-"+std::to_wstring(pointer.At("generation").Number()))/L"result-candidate.json").lexically_normal()) Bad("Candidate is outside its exact execution attempt");
        const auto candidate = Decoder(Read(candidatePath)).Parse();
        if (candidate.At("schemaVersion").Number() != 1 || candidate.At("projectId").String() != queue.Snapshot().projectId || candidate.At("executionId").String() != pointer.At("executionId").String() || candidate.At("generation").Number() != pointer.At("generation").Number() || candidate.At("configSha256").String() != pointer.At("configSha256").String()) Bad("Execution candidate identity mismatch");
        MatchInput(candidate.At("input"), input);
        const auto attempt=candidatePath.parent_path();
        const auto seal=Decoder(Read(attempt/L"native-seal.json")).Parse();
        if (seal.At("schemaVersion").Number()!=1 || !seal.At("treeRetired").Bool() || seal.At("state").String()!=pointer.At("state").String() || seal.At("candidateSha256").String()!=pointer.At("candidateSha256").String()) Bad("Invalid native execution seal");
        if(fs::file_size(attempt/L"host-result.json")>kMetadataLimit)Bad("Host metadata exceeds byte limit");
        Hash(attempt/L"host-result.json",seal.At("hostResultSha256").String());
        const auto host=Decoder(Read(attempt/L"host-result.json")).Parse();
        if (host.At("schemaVersion").Number()!=1 || !host.At("launched").Bool() || !host.At("exited").Bool() || !host.At("treeRetired").Bool() || host.At("exitCode").Number()!=0 || host.At("win32Error").Number()!=0 || host.At("timedOut").Bool() || host.At("cancelled").Bool()) Bad("Native execution seal lacks successful exit/retirement");
        return candidate;
    }
    // Only immediate execution/native-state entries are enumerated; attempts,
    // logs, datasets and outputs are never recursively scanned.
    std::map<std::string,fs::path> HistoryPointers() const {
        std::map<std::string,fs::path> result;
        Safe(Root(),true);if(!fs::exists(Root()))return result;
        std::size_t entries=0,files=0;std::uint64_t metadataBytes=0;
        for(const auto& entry:fs::directory_iterator(Root())){
            if(++entries>2048)Bad("Execution history directory-entry limit exceeded");
            const auto id=Utf8(entry.path().filename());if(!Uuid(id))continue;
            Safe(entry.path());if(!entry.is_directory())Bad("Execution UUID entry is not a directory");
            if(result.size()>=1024)Bad("Execution history exceeds 1024 entries");
            std::uint64_t newest=0;fs::path chosen;
            for(const auto& record:fs::directory_iterator(entry.path())){
                if(++files>16384)Bad("Execution history file-entry limit exceeded");
                const auto name=Utf8(record.path().filename());
                if(!name.starts_with("native-state-")||!name.ends_with(".json"))continue;
                const auto digits=std::string_view(name).substr(13,name.size()-18);std::uint64_t value=0;
                const auto [end,ec]=std::from_chars(digits.data(),digits.data()+digits.size(),value);
                if(ec!=std::errc{}||end!=digits.data()+digits.size()||!value||value>100000||std::to_string(value)!=digits)Bad("Invalid native state generation filename");
                Safe(record.path());if(!record.is_regular_file())Bad("Native state is not a regular file");
                const auto size=record.file_size();if(size>kMetadataLimit||size>64ULL*1024*1024-metadataBytes)Bad("Execution history metadata byte limit exceeded");metadataBytes+=size;
                if(value>newest){newest=value;chosen=record.path();}
            }
            if(!chosen.empty())result.emplace(id,chosen);
        }
        // A rejected resume deliberately restores its previous safe pointer.
        // Honor that authoritative latest record without rewriting any history.
        Safe(Pointer(),true);if(fs::exists(Pointer())){
            const auto latest=Decoder(Read(Pointer())).Parse();const auto id=latest.At("executionId").String();
            if(!Uuid(id)||latest.At("projectId").String()!=queue.Snapshot().projectId)Bad("Invalid latest history identity");
            if(result.contains(id))result[id]=Pointer();
        }
        return result;
    }
    ExecutionSnapshot HistorySummary(const std::string& id,const fs::path& path) const {
        const auto p=Decoder(Read(path)).Parse();const auto schema=p.At("schemaVersion").Number();
        if((schema!=1&&schema!=2)||p.At("projectId").String()!=queue.Snapshot().projectId||p.At("executionId").String()!=id)Bad("Foreign or unsupported history metadata");
        const auto g=p.At("generation").Number();if(!g||g>100000)Bad("Invalid history generation");
        if(path!=Pointer()&&path.filename()!=fs::path(L"native-state-"+std::to_wstring(g)+L".json"))Bad("History filename/generation mismatch");
        ExecutionSnapshot s;s.executionId=id;s.directory=Root()/Path(id);s.state=State(p.At("state").String());s.message=p.At("message").String();ReadObservations(p,s);
        if(schema==2){const auto& c=p.At("configuration");if(c.At("schemaVersion").Number()!=1)Bad("Unsupported history configuration");s.profileName=c.At("profile").String();}
        if(s.state==ExecutionState::Preparing||s.state==ExecutionState::Training||s.state==ExecutionState::Restoring||s.state==ExecutionState::Pausing||s.state==ExecutionState::Saving){s.state=ExecutionState::Interrupted;s.timing.activeTimeComplete=false;}
        return s; // Deliberately no output, generation, camera or resumability claim.
    }
    ExecutionSnapshot InspectOwned(const fs::path& selectedPointer={}) {
        ExecutionSnapshot s;std::optional<InputManifest> currentInput;if(selectedPointer.empty())currentInput=Input();
        Safe(Root(), true); if (!fs::exists(Root())) return s;
        const auto pointerPath=selectedPointer.empty()?Pointer():selectedPointer;
        Safe(pointerPath,true);if(!fs::exists(pointerPath))return s;
        const auto pointer=Decoder(Read(pointerPath)).Parse();
        const auto schema=pointer.At("schemaVersion").Number();
        if ((schema!=1&&schema!=2) || pointer.At("projectId").String() != queue.Snapshot().projectId) Bad("Unsupported or foreign execution metadata");
        InputManifest input;
        if(selectedPointer.empty())input=*currentInput;
        else {
            // Historical imports remain owned media generations even after the
            // current input pointer changes. Verify the selected sealed identity.
            const auto& recorded=pointer.At("input");input.projectId=queue.Snapshot().projectId;input.inputId=recorded.At("id").String();input.revision=recorded.At("revision").Number();
            if(!Uuid(input.inputId)||!input.revision)Bad("Invalid historical input identity");
            const auto relative=Path(recorded.At("relativePath").String());std::vector<fs::path> components;for(const auto& component:relative)components.push_back(component);
            if(components.size()!=3||components[0]!=L"media"||components[1]!=Path(input.inputId))Bad("Historical media is outside its owned input generation");
            const auto media=Relative(LocalPath(queue.Directory()),recorded.At("relativePath").String());const auto bytes=recorded.At("bytes").Number();
            if(bytes>kMaxInputFileBytes||fs::file_size(media)!=bytes)Bad("Historical input size mismatch");Hash(media,recorded.At("sha256").String());
            input.files.push_back({recorded.At("relativePath").String(),bytes,recorded.At("sha256").String()});
        }
        MatchInput(pointer.At("input"), input); s.executionId = pointer.At("executionId").String(); if (!Uuid(s.executionId)) Bad("Invalid execution UUID");
        s.directory = Root()/Path(s.executionId); Safe(s.directory); generation = pointer.At("generation").Number(); if (!generation || generation > 100000) Bad("Invalid execution generation");
        configurationJSON.clear();s.configurationSha256=pointer.At("configSha256").String();
        if(schema==2){
            const auto& reference=pointer.At("configuration");
            if(reference.kind!=Json::Kind::Object||reference.object.size()!=6||reference.At("schemaVersion").Number()!=1||reference.At("relativePath").String()!="resolved-config.json"||reference.At("sha256").String()!=s.configurationSha256)Bad("Invalid owned execution configuration reference");
            s.configurationPath=VerifyReference(s.directory,reference,1024*1024);
            const auto owned=Decoder(Read(s.configurationPath,1024*1024)).Parse();
            if(owned.At("schemaVersion").Number()!=1)Bad("Unsupported owned configuration schema");
            ValidateSettings(owned.At("settings"));
            if(Encode(owned.At("settings"))!=Encode(reference.At("settings")))Bad("Owned configuration settings differ from metadata");
            s.profileName=reference.At("profile").String();
            const auto& settings=owned.At("settings");s.effectiveSettings={settings.At("brushSteps").Number(),settings.At("maxResolution").Number(),settings.At("maxSplats").Number()};
            bool known=false;for(auto p:{ReconstructionProfile::Low,ReconstructionProfile::Medium,ReconstructionProfile::High,ReconstructionProfile::Advanced})if(s.profileName==ReconstructionProfileName(p)){
                known=true;if(p!=ReconstructionProfile::Advanced){const auto fixed=GetReconstructionProfileSettings(p);if(fixed.brushSteps!=s.effectiveSettings.brushSteps||fixed.maxResolution!=s.effectiveSettings.maxResolution||fixed.maxSplats!=s.effectiveSettings.maxSplats)Bad("Owned profile/settings mismatch");}break;
            }if(!known)Bad("Unknown owned reconstruction profile");configurationJSON=Encode(reference);
        }else if(pointer.object.contains("configuration"))Bad("Legacy execution cannot contain an owned configuration reference");
        s.state = State(pointer.At("state").String()); s.message = pointer.At("message").String(); ReadObservations(pointer,s); if(schema==2&&s.progress.totalIterations&&*s.progress.totalIterations!=s.effectiveSettings.brushSteps)Bad("Progress target differs from owned configuration");checkpoint.clear();
        if (s.state == ExecutionState::Preparing || s.state == ExecutionState::Training || s.state == ExecutionState::Pausing || s.state == ExecutionState::Saving || s.state == ExecutionState::Restoring) { s.timing.activeTimeComplete=false; s.state = ExecutionState::Interrupted; s.message = "Unconfirmed active execution found; no saved checkpoint is inferred"; }
        if (s.state == ExecutionState::Paused || s.state == ExecutionState::Completed) {
            const auto candidate = Candidate(pointer,input,s.directory);
            if (candidate.At("outcome").String() != ExecutionStateName(s.state)) Bad("Execution seal/candidate outcome mismatch");
            if (s.state == ExecutionState::Paused) {
                checkpoint = candidate.At("checkpointId").String(); if (checkpoint.empty() || checkpoint.size() > 256) Bad("Invalid checkpoint identity");
                VerifyReference(s.directory, candidate.At("resumeContract"),kMetadataLimit); s.resumeAvailable = true;
            } else {
                if(!candidate.At("output").At("validationPassed").Bool())Bad("Output validation was not successful");
                s.output = VerifyReference(s.directory, candidate.At("output"));
                std::ifstream ply(s.output,std::ios::binary);char magic[4]{};if(!ply.read(magic,4)||std::string_view(magic,4)!="ply\n")Bad("Output is not a PLY file");
                s.outputSha256=candidate.At("output").At("sha256").String();
                s.resultGeneration=generation;s.initialView=InitialView(s.directory,candidate);
            }
        }
        return s;
    }
    ExecutionSnapshot Execute(bool resume, const ExecutionObserver& observer) {
        const auto started=std::chrono::steady_clock::now();const auto startedUtc=UtcNow();
        ExecutionTiming newTiming;
        {
            std::lock_guard lock(mutex);
            if (snapshot.active) Bad("A reconstruction operation is already active");
            if(!resume){
                newTiming.activeMilliseconds=preparationPrefix.value_or(0);
                newTiming.startedUtc=preparationStartedUtc.value_or(startedUtc);
                newTiming.startedBoundary=preparationStartedUtc?"preparation_start_v1":"execution_start_v1";
                newTiming.activeTimeComplete=true;
                snapshot=ExecutionSnapshot{};snapshot.timing=newTiming;
                intervalStart=started;intervalBase=*newTiming.activeMilliseconds;timingRunning=true;
            }
            // Take the pending preparation once before any fallible admission.
            // Resume discards it rather than contaminating the original start.
            preparationPrefix.reset();preparationStartedUtc.reset();
            snapshot.active = true; snapshot.resumeAvailable = false; snapshot.pauseAvailable = false; snapshot.controlPending = false;
            cancel.store(false); forcePending = false; pauseRequest.clear();
        }
        InputManifest input; Config config; bool persistent = false; bool hadPause = false; ExecutionSnapshot old;
        fs::path attempt; std::string candidateRelative, priorPausedBytes;
        try {
            if (resume) {
                old=InspectOwned();hadPause=old.state==ExecutionState::Paused&&old.resumeAvailable;if(!hadPause)Bad("No verified persisted paused execution is available");
                std::lock_guard lock(mutex);snapshot=old;snapshot.active=true;snapshot.resumeAvailable=false;intervalStart=started;intervalBase=old.timing.activeMilliseconds.value_or(0);timingRunning=true;
            }
            input = Input();
            // Owned Resume is resolved first, without consulting future-start
            // dropdown, Advanced path, environment or persistent runtime defaults.
            fs::path executionConfig=resume&&!old.configurationPath.empty()?old.configurationPath:configPath;
            if(executionConfig.empty()&&profile)executionConfig=DiscoverRuntimeConfiguration();
            config = Admit(executionConfig,profile.has_value()||(resume&&!old.configurationPath.empty()));
            ExecutionSnapshot current;
            if (resume) {
                priorPausedBytes=Read(Pointer()); const auto pointer = Decoder(priorPausedBytes).Parse();
                if (pointer.At("configSha256").String() != config.hash) Bad("Resume configuration bytes changed; previous checkpoint retained");
                current = old; current.state = ExecutionState::Restoring;
                do { if (++generation>100000) Bad("Execution attempt generation limit exceeded"); }
                while (fs::exists(current.directory/(L"attempt-"+std::to_wstring(generation))));
            } else {
                configurationJSON.clear();
                if(profile&&std::string_view(ReconstructionProfileName(*profile))=="Unknown")Bad("Unknown reconstruction profile");
                generation = 1; current.executionId = ProjectRepository::NewId(); Directory(Root()); current.directory = Root()/Path(current.executionId); Directory(current.directory); current.state = ExecutionState::Preparing;
                if(profile){
                    auto resolved=config.json;
                    if(*profile!=ReconstructionProfile::Advanced){
                        const auto fixed=GetReconstructionProfileSettings(*profile);
                        auto& settings=resolved.object.at("settings");
                        for(const auto& [key,value]:std::map<std::string,std::uint64_t>{{"brushSteps",fixed.brushSteps},{"maxResolution",fixed.maxResolution},{"maxSplats",fixed.maxSplats}}){settings.object.at(key).text=std::to_string(value);}
                    }
                    const auto bytes=*profile==ReconstructionProfile::Advanced?config.bytes:Encode(resolved)+"\n";
                    executionConfig=current.directory/L"resolved-config.json";
                    Publish(executionConfig,bytes); // Exclusive: never replace an execution's admitted settings.
                    config=Admit(executionConfig,true);
                    current.configurationPath=executionConfig;current.configurationSha256=config.hash;current.profileName=ReconstructionProfileName(*profile);
                    const auto& settings=config.json.At("settings");current.effectiveSettings={settings.At("brushSteps").Number(),settings.At("maxResolution").Number(),settings.At("maxSplats").Number()};
                    configurationJSON="{\"schemaVersion\":1,\"profile\":"+Q(current.profileName)+",\"relativePath\":\"resolved-config.json\",\"bytes\":"+std::to_string(bytes.size())+",\"sha256\":"+Q(config.hash)+",\"settings\":"+Encode(settings)+"}";
                }
            }
            current.timing.completedUtc.reset();
            if(!resume)current.timing=newTiming;
            {std::lock_guard lock(mutex);intervalStart=started;intervalBase=current.timing.activeMilliseconds.value_or(0);timingRunning=true;}
            current.progress.totalIterations=config.json.At("settings").At("brushSteps").Number();
            current.active = true; current.resumeAvailable = false; current.output.clear(); current.message = resume ? "Revalidating checkpoint and launching a fresh process" : "Admitting pinned tools and preparing owned video";
            attempt = current.directory/(L"attempt-"+std::to_wstring(generation)); Safe(attempt,true);
            if (!fs::create_directory(attempt)) Bad("Execution attempt already exists; prior evidence retained"); Safe(attempt);
            const auto controlPath = attempt/L"controls"; Directory(controlPath);
            { std::lock_guard lock(mutex); snapshot = current; controls = controlPath; configuredSteps=config.json.At("settings").At("brushSteps").Number(); if (!configuredSteps || configuredSteps>10000000) Bad("Invalid configured Brush iteration limit"); }
            const std::string request = "{\"schemaVersion\":1,\"action\":"+Q(resume?"resume":"start")+",\"executionId\":"+Q(current.executionId)+",\"generation\":"+std::to_string(generation)+",\"projectId\":"+Q(input.projectId)+",\"projectDirectory\":"+Q(Utf8(LocalPath(queue.Directory())))+",\"input\":"+InputJson(input)+",\"executionDirectory\":"+Q(Utf8(current.directory))+",\"configPath\":"+Q(Utf8(executionConfig))+",\"configSha256\":"+Q(config.hash)+",\"controlDirectory\":"+Q(Utf8(controlPath))+(resume ? ",\"resumeCheckpoint\":"+Q(checkpoint) : "")+"}";
            Publish(attempt/L"request.json", request);
            if (resume) Publish(attempt/L"previous-paused-state.json",priorPausedBytes);
            Persist(Copy(),input,config.hash);
            persistent = true; Emit(observer);
            std::uint64_t offset = 0, sequence = 0; std::optional<std::uint64_t> observedInAttempt;std::string pending, observationFailure; bool completedCandidate = false, pausedCandidate = false, restoredValidated = !resume, restored = !resume;
            auto poll = [&] {
                const auto eventPath = attempt/L"events.jsonl"; Safe(eventPath,true); if (!fs::exists(eventPath)) return;
                const auto size = fs::file_size(eventPath); if (size < offset || size > kEventsLimit) Bad("Runner event stream truncated or exceeded limit");
                if (size-offset > kMetadataLimit) Bad("Runner event backlog exceeded limit");
                std::ifstream stream(eventPath,std::ios::binary); if (!stream) Bad("Cannot read runner events"); stream.seekg(static_cast<std::streamoff>(offset));
                std::string bytes(static_cast<std::size_t>(size-offset),'\0'); if (!bytes.empty() && !stream.read(bytes.data(),static_cast<std::streamsize>(bytes.size()))) Bad("Cannot read runner events"); offset=size; pending += bytes;
                for (;;) {
                    const auto newline = pending.find('\n'); if (newline == std::string::npos) break;
                    if (newline > kEventLimit) Bad("Runner event line exceeded limit");
                    const auto event = Decoder(std::string_view(pending.data(),newline)).Parse(); pending.erase(0,newline+1);
                    if (event.At("schemaVersion").Number()!=1 || event.At("executionId").String()!=current.executionId || event.At("generation").Number()!=generation || event.At("sequence").Number()<=sequence) Bad("Stale, foreign or unordered runner event");
                    sequence=event.At("sequence").Number(); const auto kind=event.At("kind").String();
                    {
                        std::lock_guard lock(mutex); snapshot.message=event.At("message").String();
                        if (kind=="StageStarted") snapshot.stage=event.At("stage").String();
                        else if (kind=="BrushReady") { if (!resume || restored) { snapshot.state=ExecutionState::Training; snapshot.pauseAvailable=!snapshot.controlPending && !forcePending; } }
                        else if (kind=="RestoreValidated") restoredValidated=true;
                        else if (kind=="Restored") { if (!restoredValidated) Bad("Restore observed without validation"); restored=true; snapshot.state=ExecutionState::Training; }
                        else if (kind=="Sample") {
                            if(!restored)Bad("Training sample before restored acknowledgement");
                            const bool hasIteration=event.object.contains("iteration"),hasSample=event.object.contains("sampleSequence");
                            if(hasIteration!=hasSample)Bad("Incomplete structured sample");
                            if(hasIteration){
                            const auto iteration=event.At("iteration").Number(),sample=event.At("sampleSequence").Number();
                            if(iteration>=configuredSteps||sample!=iteration||(observedInAttempt&&iteration<=*observedInAttempt))Bad("Invalid pre-step sample progress");
                            observedInAttempt=iteration;snapshot.progress.observedIteration=iteration;snapshot.progress.completedIterations=iteration;snapshot.progress.totalIterations=configuredSteps;snapshot.progress.sampleSequence=sample;
                            }
                            if(!snapshot.controlPending)snapshot.state=ExecutionState::Training;
                        }
                        else if (kind=="Saving") { if (!snapshot.controlPending || pauseRequest.empty() || event.At("requestId").String()!=pauseRequest) Bad("Unexpected saving observation without matching native request"); snapshot.state=ExecutionState::Saving; }
                        else if (kind=="SaveFailed") { const auto id=event.At("requestId").String(); if (!id.empty() && id!=pauseRequest) Bad("Foreign save failure request identity"); snapshot.controlPending=false; snapshot.pauseAvailable=!forcePending; snapshot.state=ExecutionState::Training; }
                        else if (kind=="PausedCandidate") { if (!snapshot.controlPending || forcePending || pauseRequest.empty() || event.At("requestId").String()!=pauseRequest) Bad("Unexpected paused candidate"); pausedCandidate=true; snapshot.pauseAvailable=false; }
                        else if (kind=="CompletedCandidate") { completedCandidate=true; snapshot.pauseAvailable=false; }
                        else if (kind=="Failed") snapshot.pauseAvailable=false;
                        else if (kind!="StageCompleted") Bad("Unknown runner event kind");
                    }
                    Emit(observer);
                }
                if (pending.size()>kEventLimit) Bad("Runner event line exceeded limit");
            };
            const auto observeSafely=[&] {
                if (!observationFailure.empty()) return;
                try { poll(); }
                catch (const std::exception& e) {
                    observationFailure=e.what();
                    { std::lock_guard lock(mutex); snapshot.message="Untrusted runner observation: "+observationFailure+"; active controller retained; use Force stop"; snapshot.stage="control-error"; snapshot.pauseAvailable=false; snapshot.controlPending=false; if (snapshot.state==ExecutionState::Pausing || snapshot.state==ExecutionState::Saving) snapshot.state=ExecutionState::Training; }
                    Emit(observer);
                }
            };
            ProcessRequest process; process.executable=config.python; process.arguments={L"-I",L"-B",config.runner.native(),L"--request",(attempt/L"request.json").native()}; process.workingDirectory=attempt; process.timeout=config.timeout; process.observeTick=observeSafely; process.maxCapturedBytesPerStream=65536;
            // Recheck after writing the request. This is an observation boundary,
            // not a lifetime executable/DLL lock or hostile mutation guarantee.
            const auto readmitted=Admit(executionConfig,profile.has_value()||!current.configurationPath.empty()); if (readmitted.hash!=config.hash) Bad("Configuration changed during admission");
            const auto result=WindowsProcessHost::Run(process,&cancel);
            Publish(attempt/L"host-stdout.bin",result.stdoutBytes); Publish(attempt/L"host-stderr.bin",result.stderrBytes);
            Publish(attempt/L"host-result.json","{\"schemaVersion\":1,\"launched\":"+std::string(result.launched?"true":"false")+",\"exited\":"+(result.exited?"true":"false")+",\"treeRetired\":"+(result.treeRetired?"true":"false")+",\"exitCode\":"+std::to_string(result.exitCode)+",\"cancelled\":"+(result.cancelled?"true":"false")+",\"timedOut\":"+(result.timedOut?"true":"false")+",\"win32Error\":"+std::to_string(result.win32Error)+",\"error\":"+Q(result.error)+",\"stdoutTotalBytes\":"+std::to_string(result.stdoutTotalBytes)+",\"stderrTotalBytes\":"+std::to_string(result.stderrTotalBytes)+",\"stdoutTruncated\":"+(result.stdoutTruncated?"true":"false")+",\"stderrTruncated\":"+(result.stderrTruncated?"true":"false")+",\"stdoutPrefixSha256\":"+Q(Hash(attempt/L"host-stdout.bin"))+",\"stderrPrefixSha256\":"+Q(Hash(attempt/L"host-stderr.bin"))+"}");
            observeSafely();
            if (!result.cancelled && !observationFailure.empty()) Bad("Runner observation validation failed: "+observationFailure);
            if (!result.cancelled && !pending.empty()) Bad("Runner left an incomplete JSONL event");
            auto terminal=Copy(); terminal.active=false; terminal.pauseAvailable=false; terminal.controlPending=false; terminal.resumeAvailable=false;
            if (!result.treeRetired) Bad("Execution tree retirement was not confirmed: "+result.error);
            if (result.cancelled) { terminal.state=ExecutionState::Stopped; terminal.message="Force stopped; unsaved work is not a checkpoint"; }
            else if (result.timedOut) { terminal.state=ExecutionState::Failed; terminal.message="Whole-run deadline exceeded; isolated process tree retired"; }
            else if (!result.launched || !result.exited || result.win32Error) { terminal.state=ExecutionState::Failed; terminal.message="Runner launch/host failure: "+result.error; }
            else {
                const auto candidatePath=attempt/L"result-candidate.json"; const auto candidate=Decoder(Read(candidatePath)).Parse();
                if (candidate.At("schemaVersion").Number()!=1 || candidate.At("projectId").String()!=input.projectId || candidate.At("executionId").String()!=current.executionId || candidate.At("generation").Number()!=generation || candidate.At("configSha256").String()!=config.hash) Bad("Final runner candidate identity mismatch");
                MatchInput(candidate.At("input"), input);
                const auto currentInput=Input(); MatchInput(candidate.At("input"),currentInput);
                const auto outcome=candidate.At("outcome").String();
                if (!result.launched || !result.exited || result.win32Error || result.exitCode!=0) {
                    terminal.state=ExecutionState::Failed; terminal.message="Runner failed (exit "+std::to_string(result.exitCode)+"): "+(result.error.empty()?terminal.message:result.error);
                } else if (outcome=="Paused" && pausedCandidate && !completedCandidate) {
                    if (!candidate.At("checkpoint").At("validated").Bool()) Bad("Checkpoint validation was not successful");
                    checkpoint=candidate.At("checkpointId").String(); if (checkpoint.empty() || checkpoint.size()>256) Bad("Invalid committed checkpoint ID");
                    VerifyReference(current.directory,candidate.At("resumeContract"),kMetadataLimit); terminal.state=ExecutionState::Paused; terminal.resumeAvailable=true; terminal.message="Durable checkpoint validated; runner exited and entire tree retired";
                } else if (outcome=="Completed" && completedCandidate && restored) {
                    terminal.output=VerifyReference(current.directory,candidate.At("output"));
                    if (!candidate.At("output").At("validationPassed").Bool()) Bad("Output validation was not successful");
                    std::ifstream ply(terminal.output,std::ios::binary); char magic[4]{}; if (!ply.read(magic,4) || std::string_view(magic,4)!="ply\n") Bad("Output is not a PLY file");
                    terminal.state=ExecutionState::Completed; terminal.message="Validated PLY published after runner exit and tree retirement";
                } else if (outcome=="Failed" || outcome=="Stopped") { terminal.state=ExecutionState::Failed; }
                else Bad("Runner terminal candidate lacks matching trusted observations");
                candidateRelative=Utf8(fs::relative(candidatePath,current.directory));
                // host-result is immutable, and this seal binds that exact observation.
                Publish(attempt/L"native-seal.json","{\"schemaVersion\":1,\"candidateSha256\":"+Q(Hash(candidatePath))+",\"hostResultSha256\":"+Q(Hash(attempt/L"host-result.json"))+",\"treeRetired\":true,\"state\":"+Q(ExecutionStateName(terminal.state))+"}");
            }
            if(terminal.state==ExecutionState::Completed){
                terminal.outputSha256=Hash(terminal.output);terminal.resultGeneration=generation;
                const auto sealed=Decoder(Read(attempt/L"result-candidate.json")).Parse();terminal.initialView=InitialView(current.directory,sealed);
                terminal.progress.completedIterations=configuredSteps;
            }
            FinishTiming(terminal);
            if(terminal.state==ExecutionState::Completed)terminal.timing.completedUtc=UtcNow();
            if (resume && terminal.state!=ExecutionState::Paused && terminal.state!=ExecutionState::Completed) {
                terminal.message += "; previous paused checkpoint retained";
                Publish(Pointer(),priorPausedBytes,true);
            } else Persist(terminal,input,config.hash,candidateRelative,candidateRelative.empty()?"":Hash(attempt/L"result-candidate.json"));
            { std::lock_guard lock(mutex); snapshot=terminal; controls.clear(); }
            Emit(observer); return terminal;
        } catch (const std::exception& e) {
            auto failed=Copy(); failed.active=false; failed.pauseAvailable=false; failed.controlPending=false; failed.resumeAvailable=false; failed.state=ExecutionState::Failed; failed.message=e.what();
            FinishTiming(failed);
            if (hadPause) failed.message += "; previous paused execution/checkpoint retained";
            if (persistent && resume && !priorPausedBytes.empty()) { try { Publish(Pointer(),priorPausedBytes,true); } catch (const std::exception& saved) { failed.message += "; previous pause pointer could not be restored: "+std::string(saved.what()); } }
            if (persistent && !resume) { try { Persist(failed,input,config.hash); } catch (const std::exception& saved) { failed.message += "; metadata publication failed: "+std::string(saved.what()); } }
            { std::lock_guard lock(mutex); snapshot=failed; controls.clear(); }
            Emit(observer); return failed;
        }
    }
};
ProjectReconstruction::ProjectReconstruction(ReconstructionQueue& queue,const fs::path& configPath) : impl_(std::make_unique<Impl>(queue,configPath)) {}
ProjectReconstruction::ProjectReconstruction(ReconstructionQueue& queue,const fs::path& configPath,ReconstructionProfile profile) : impl_(std::make_unique<Impl>(queue,configPath,profile)) {}
ProjectReconstruction::~ProjectReconstruction() = default;
ExecutionSnapshot ProjectReconstruction::Snapshot() const { return impl_->Copy(); }
ExecutionSnapshot ProjectReconstruction::Inspect() {
    { std::lock_guard lock(impl_->mutex); if (impl_->snapshot.active) Bad("Cannot inspect while execution is active"); }
    auto observed=impl_->InspectOwned(); { std::lock_guard lock(impl_->mutex); impl_->snapshot=observed; } return observed;
}
std::vector<ExecutionSnapshot> ProjectReconstruction::ListExecutions() {
    {std::lock_guard lock(impl_->mutex);if(impl_->snapshot.active)Bad("Cannot enumerate history while execution is active");}
    std::vector<ExecutionSnapshot> results;
    for(const auto& [id,path]:impl_->HistoryPointers())results.push_back(impl_->HistorySummary(id,path));
    std::stable_sort(results.begin(),results.end(),[](const auto& a,const auto& b){
        if(a.timing.completedUtc!=b.timing.completedUtc)return a.timing.completedUtc>b.timing.completedUtc;
        if(a.timing.startedUtc!=b.timing.startedUtc)return a.timing.startedUtc>b.timing.startedUtc;
        return a.executionId>b.executionId;
    });return results;
}
ExecutionSnapshot ProjectReconstruction::InspectExecution(const std::string& executionId) {
    if(!Uuid(executionId))Bad("Invalid selected execution UUID");
    {std::lock_guard lock(impl_->mutex);if(impl_->snapshot.active)Bad("Cannot inspect history while execution is active");}
    const auto pointers=impl_->HistoryPointers();const auto found=pointers.find(executionId);
    if(found==pointers.end())Bad("Selected execution is not in this project history");
    impl_->HistorySummary(executionId,found->second); // Enforce filename/metadata identity before full verification.
    Impl inspection(impl_->queue,impl_->configPath,impl_->profile);
    return inspection.InspectOwned(found->second);
}
void ProjectReconstruction::SetPreparationActiveMilliseconds(std::uint64_t milliseconds) {
    std::lock_guard lock(impl_->mutex);if(impl_->snapshot.active)Bad("Cannot change preparation timing during execution");if(milliseconds>std::numeric_limits<std::uint64_t>::max()/2)Bad("Preparation duration exceeds supported range");impl_->preparationPrefix=milliseconds;impl_->preparationStartedUtc.reset();
}
void ProjectReconstruction::SetPreparationTiming(const PreparationTiming& timing) {
    std::lock_guard lock(impl_->mutex);
    if(impl_->snapshot.active)Bad("Cannot change preparation timing during execution");
    if(timing.activeMilliseconds>std::numeric_limits<std::uint64_t>::max()/2)Bad("Preparation duration exceeds supported range");
    ValidateUtc(timing.startedUtc);
    impl_->preparationPrefix=timing.activeMilliseconds;impl_->preparationStartedUtc=timing.startedUtc;
}
ExecutionSnapshot ProjectReconstruction::RunNew(const ExecutionObserver& observer) { return impl_->Execute(false,observer); }
ExecutionSnapshot ProjectReconstruction::Resume(const ExecutionObserver& observer) { return impl_->Execute(true,observer); }
bool ProjectReconstruction::RequestPause() {
    return impl_->Pause(0,false);
}
bool ProjectReconstruction::RequestPauseAtIteration(std::uint64_t nextIteration) { return impl_->Pause(nextIteration,true); }
bool ProjectReconstruction::RequestForceStop() {
    std::lock_guard lock(impl_->mutex); auto& s=impl_->snapshot;
    if (!s.active || impl_->forcePending) return false;
    impl_->forcePending=true; s.controlPending=true; s.pauseAvailable=false; s.message="Force stop requested; waiting for isolated tree retirement";
    if (!impl_->controls.empty()) {
        const auto id=ProjectRepository::NewId();
        try { Publish(impl_->controls/Path(id+".json"),"{\"schemaVersion\":1,\"executionId\":"+Q(s.executionId)+",\"generation\":"+std::to_string(impl_->generation)+",\"requestId\":"+Q(id)+",\"command\":\"ForceStop\"}"); }
        catch (const std::exception& e) { s.message += "; control file publication failed: "+std::string(e.what()); }
    }
    // Native Job cancellation is authoritative even if the helper cannot read a control file.
    impl_->cancel.store(true); return true;
}
} // namespace MonsterEngine::VideoTo3D
