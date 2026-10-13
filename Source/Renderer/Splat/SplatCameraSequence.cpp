// Copyright Monster Engine. All Rights Reserved.
#include "Renderer/Splat/SplatCameraSequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace MonsterRender::Splat
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double Rad = Pi / 180.0;
constexpr double Deg = 180.0 / Pi;

void Error(std::string* out, const std::string& message) { if (out) *out = message; }
double ClampTime(double t, double lo, double hi) { return std::clamp(std::isfinite(t) ? t : lo, lo, hi); }
float Mix(float a, float b, double t) { return static_cast<float>(a + (static_cast<double>(b) - a) * t); }
double Ease(double t) { return t * t * (3.0 - 2.0 * t); }

struct Vec3 { double x, y, z; };
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 operator*(Vec3 a, double b) { return {a.x*b,a.y*b,a.z*b}; }
double Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
Vec3 Normalize(Vec3 v) { const double l=std::sqrt(Dot(v,v)); return l>1e-12 ? v*(1.0/l) : Vec3{0,1,0}; }
Vec3 Position(const FSplatControlState& s) { return {s.PositionX,s.PositionY,s.PositionZ}; }

struct Quat { double w,x,y,z; };
Quat operator*(Quat a, Quat b)
{
    return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
        a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
Quat Axis(Vec3 v, double angle)
{
    const double s=std::sin(angle*0.5);
    return {std::cos(angle*0.5),v.x*s,v.y*s,v.z*s};
}
Quat Orientation(const FSplatControlState& s)
{
    // Camera convention: yaw -90 looks down -Z; pitch raises the view.
    return Axis({0,1,0},-s.Yaw*Rad) * Axis({0,0,1},s.Pitch*Rad) * Axis({1,0,0},s.Roll*Rad);
}
Vec3 Rotate(Quat q, Vec3 v)
{
    const Vec3 u{q.x,q.y,q.z};
    return v + Cross(u,v)*(2.0*q.w) + Cross(u,Cross(u,v))*2.0;
}
Quat Slerp(Quat a, Quat b, double t)
{
    double dot=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z;
    if (dot<0) { b={-b.w,-b.x,-b.y,-b.z}; dot=-dot; }
    if (dot>0.9995)
    {
        Quat q{a.w+(b.w-a.w)*t,a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
        const double inv=1.0/std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
        return {q.w*inv,q.x*inv,q.y*inv,q.z*inv};
    }
    const double theta=std::acos(std::clamp(dot,-1.0,1.0));
    const double sinTheta=std::sin(theta);
    const double wa=std::sin((1.0-t)*theta)/sinTheta;
    const double wb=std::sin(t*theta)/sinTheta;
    return {a.w*wa+b.w*wb,a.x*wa+b.x*wb,a.y*wa+b.y*wb,a.z*wa+b.z*wb};
}
void ExtractAngles(Quat q, FSplatControlState& s)
{
    const Vec3 front=Normalize(Rotate(q,{1,0,0}));
    const Vec3 up=Normalize(Rotate(q,{0,1,0}));
    s.Yaw=static_cast<float>(std::atan2(front.z,front.x)*Deg);
    s.Pitch=static_cast<float>(std::asin(std::clamp(front.y,-1.0,1.0))*Deg);
    const Vec3 right0=Normalize(Cross(front,{0,1,0}));
    const Vec3 up0=Normalize(Cross(right0,front));
    s.Roll=static_cast<float>(std::atan2(Dot(up,right0),Dot(up,up0))*Deg);
}
Vec3 PositionTangent(const std::vector<FSplatCameraKeyframe>& keys, size_t i)
{
    if (i==0) return (Position(keys[1].State)-Position(keys[0].State))*(1.0/(keys[1].TimeSeconds-keys[0].TimeSeconds));
    if (i+1==keys.size()) return (Position(keys[i].State)-Position(keys[i-1].State))*(1.0/(keys[i].TimeSeconds-keys[i-1].TimeSeconds));
    const double dt0=keys[i].TimeSeconds-keys[i-1].TimeSeconds;
    const double dt1=keys[i+1].TimeSeconds-keys[i].TimeSeconds;
    const Vec3 left=(Position(keys[i].State)-Position(keys[i-1].State))*(1.0/dt0);
    const Vec3 right=(Position(keys[i+1].State)-Position(keys[i].State))*(1.0/dt1);
    return (left*dt1+right*dt0)*(1.0/(dt0+dt1));
}

// Small strict JSON reader for the versioned sequence format; no external JSON dependency.
struct Json
{
    enum class Kind { Null, Number, Boolean, String, Array, Object } kind=Kind::Null;
    double number=0;
    bool boolean=false;
    std::string string;
    std::vector<Json> array;
    std::map<std::string,Json> object;
    const Json& At(const char* key) const
    {
        const auto it=object.find(key);
        if (kind!=Kind::Object || it==object.end()) throw std::runtime_error(std::string("missing field: ")+key);
        return it->second;
    }
    double Number() const { if (kind!=Kind::Number || !std::isfinite(number)) throw std::runtime_error("expected finite number"); return number; }
    bool Bool() const { if (kind!=Kind::Boolean) throw std::runtime_error("expected boolean"); return boolean; }
    const std::string& String() const { if (kind!=Kind::String) throw std::runtime_error("expected string"); return string; }
};
class JsonReader
{
public:
    explicit JsonReader(const std::string& source) : m_source(source) {}
    Json Parse() { Json value=Value(0); Space(); if (m_pos!=m_source.size()) throw std::runtime_error("trailing JSON content"); return value; }
private:
    const std::string& m_source;
    size_t m_pos=0;
    void Space() { while (m_pos<m_source.size() && (m_source[m_pos]==' ' || m_source[m_pos]=='\n' || m_source[m_pos]=='\r' || m_source[m_pos]=='\t')) ++m_pos; }
    bool Eat(char c) { Space(); if (m_pos<m_source.size() && m_source[m_pos]==c) { ++m_pos; return true; } return false; }
    void Need(char c) { if (!Eat(c)) throw std::runtime_error(std::string("expected '")+c+"'"); }
    std::string String()
    {
        Need('"'); std::string result;
        while (m_pos<m_source.size())
        {
            const char c=m_source[m_pos++];
            if (c=='"') return result;
            if (static_cast<unsigned char>(c)<0x20) throw std::runtime_error("invalid JSON string");
            if (c=='\\')
            {
                if (m_pos==m_source.size()) throw std::runtime_error("incomplete JSON escape");
                const char e=m_source[m_pos++];
                switch (e) { case '"': result+='"'; break; case '\\': result+='\\'; break;
                    case '/': result+='/'; break; case 'b': result+='\b'; break; case 'f': result+='\f'; break;
                    case 'n': result+='\n'; break; case 'r': result+='\r'; break; case 't': result+='\t'; break;
                    default: throw std::runtime_error("unsupported JSON escape"); }
            }
            else result+=c;
        }
        throw std::runtime_error("unterminated JSON string");
    }
    Json Value(unsigned depth)
    {
        if (depth>12) throw std::runtime_error("JSON nesting too deep");
        Space(); if (m_pos==m_source.size()) throw std::runtime_error("unexpected end of JSON");
        Json j; const char c=m_source[m_pos];
        if (c=='{')
        {
            j.kind=Json::Kind::Object; ++m_pos;
            if (Eat('}')) return j;
            do { const std::string name=String(); Need(':'); auto [it,inserted]=j.object.emplace(name,Value(depth+1)); if (!inserted) throw std::runtime_error("duplicate JSON field"); if (Eat('}')) return j; Need(','); } while (true);
        }
        if (c=='[')
        {
            j.kind=Json::Kind::Array; ++m_pos;
            if (Eat(']')) return j;
            do { j.array.push_back(Value(depth+1)); if (Eat(']')) return j; Need(','); } while (true);
        }
        if (c=='"') { j.kind=Json::Kind::String; j.string=String(); return j; }
        if (m_source.compare(m_pos,4,"true")==0) { m_pos+=4; j.kind=Json::Kind::Boolean; j.boolean=true; return j; }
        if (m_source.compare(m_pos,5,"false")==0) { m_pos+=5; j.kind=Json::Kind::Boolean; return j; }
        if (m_source.compare(m_pos,4,"null")==0) { m_pos+=4; return j; }
        const size_t start=m_pos;
        if (c=='-') ++m_pos;
        if (m_pos==m_source.size() || m_source[m_pos]<'0' || m_source[m_pos]>'9') throw std::runtime_error("invalid JSON number");
        if (m_source[m_pos]=='0') ++m_pos;
        else while (m_pos<m_source.size() && m_source[m_pos]>='0' && m_source[m_pos]<='9') ++m_pos;
        if (m_pos<m_source.size() && m_source[m_pos]=='.') { ++m_pos; const size_t digits=m_pos; while (m_pos<m_source.size() && m_source[m_pos]>='0' && m_source[m_pos]<='9') ++m_pos; if (m_pos==digits) throw std::runtime_error("invalid JSON decimal"); }
        if (m_pos<m_source.size() && (m_source[m_pos]=='e' || m_source[m_pos]=='E')) { ++m_pos; if (m_pos<m_source.size() && (m_source[m_pos]=='+' || m_source[m_pos]=='-')) ++m_pos; const size_t digits=m_pos; while (m_pos<m_source.size() && m_source[m_pos]>='0' && m_source[m_pos]<='9') ++m_pos; if (m_pos==digits) throw std::runtime_error("invalid JSON exponent"); }
        j.kind=Json::Kind::Number; j.number=std::stod(m_source.substr(start,m_pos-start)); return j;
    }
};
float FloatField(const Json& j, const char* name)
{
    const double n=j.At(name).Number();
    if (n<-std::numeric_limits<float>::max() || n>std::numeric_limits<float>::max()) throw std::runtime_error(std::string("float out of range: ")+name);
    return static_cast<float>(n);
}
} // namespace

bool FSplatCameraSequence::AddOrReplace(const FSplatCameraKeyframe& keyframe, std::string* error)
{
    if (!std::isfinite(keyframe.TimeSeconds) || keyframe.TimeSeconds<0 || !keyframe.State.IsValid())
    { Error(error,"invalid keyframe time or state"); return false; }
    const auto it=std::lower_bound(m_keyframes.begin(),m_keyframes.end(),keyframe.TimeSeconds,
        [](const FSplatCameraKeyframe& k,double t) { return k.TimeSeconds<t; });
    if (it!=m_keyframes.end() && it->TimeSeconds==keyframe.TimeSeconds) *it=keyframe;
    else m_keyframes.insert(it,keyframe);
    return true;
}
bool FSplatCameraSequence::Remove(size_t index)
{
    if (index>=m_keyframes.size()) return false;
    m_keyframes.erase(m_keyframes.begin()+static_cast<std::ptrdiff_t>(index));
    Seek(m_currentTime); return true;
}
bool FSplatCameraSequence::SetKeyframeTime(size_t index,double timeSeconds,std::string* error)
{
    if (index>=m_keyframes.size() || !std::isfinite(timeSeconds) || timeSeconds<0)
    { Error(error,"invalid keyframe index or time"); return false; }
    for (size_t i=0;i<m_keyframes.size();++i)
        if (i!=index && m_keyframes[i].TimeSeconds==timeSeconds)
        { Error(error,"keyframe time already exists"); return false; }
    const auto key=m_keyframes[index];
    m_keyframes.erase(m_keyframes.begin()+static_cast<std::ptrdiff_t>(index));
    auto changed=key; changed.TimeSeconds=timeSeconds;
    return AddOrReplace(changed,error);
}
void FSplatCameraSequence::Clear() { m_keyframes.clear(); Stop(); }
FSplatControlState FSplatCameraSequence::Sample(double timeSeconds) const
{
    const auto& keys=ActiveKeyframes();
    if (keys.empty()) return {};
    if (!std::isfinite(timeSeconds) || timeSeconds<=keys.front().TimeSeconds) return keys.front().State;
    if (timeSeconds>=keys.back().TimeSeconds) return keys.back().State;
    const auto upper=std::upper_bound(keys.begin(),keys.end(),timeSeconds,
        [](double t,const FSplatCameraKeyframe& k) { return t<k.TimeSeconds; });
    const size_t b=static_cast<size_t>(upper-keys.begin()); const size_t a=b-1;
    const auto& ka=keys[a]; const auto& kb=keys[b];
    const double dt=kb.TimeSeconds-ka.TimeSeconds;
    const double t=(timeSeconds-ka.TimeSeconds)/dt, eased=Ease(t);
    FSplatControlState s=ka.State;
    const Vec3 p0=Position(ka.State), p1=Position(kb.State);
    Vec3 p;
    if (m_interpolation==EPositionInterpolation::Linear || keys.size()==2) p=p0+(p1-p0)*t;
    else
    {
        const Vec3 m0=PositionTangent(keys,a), m1=PositionTangent(keys,b);
        const double t2=t*t,t3=t2*t;
        p=p0*(2*t3-3*t2+1)+m0*(dt*(t3-2*t2+t))+p1*(-2*t3+3*t2)+m1*(dt*(t3-t2));
    }
    s.PositionX=static_cast<float>(p.x); s.PositionY=static_cast<float>(p.y); s.PositionZ=static_cast<float>(p.z);
    ExtractAngles(Slerp(Orientation(ka.State),Orientation(kb.State),eased),s);
    s.FOV=Mix(ka.State.FOV,kb.State.FOV,eased);
    s.NearPlane=Mix(ka.State.NearPlane,kb.State.NearPlane,eased);
    s.FarPlane=Mix(ka.State.FarPlane,kb.State.FarPlane,eased);
    s.GaussianScale=Mix(ka.State.GaussianScale,kb.State.GaussianScale,eased);
    s.ClampToValid();
    return s;
}
bool FSplatCameraSequence::Play()
{
    if (m_playbackKeyframes.empty())
    {
        if (m_keyframes.size()<2) return false;
        m_playbackKeyframes=m_keyframes;
    }
    if (m_currentTime>=GetDuration()) m_currentTime=m_playbackKeyframes.front().TimeSeconds;
    m_playing=true; return true;
}
void FSplatCameraSequence::Stop() { m_playing=false; m_playbackKeyframes.clear(); m_currentTime=m_keyframes.empty()?0:m_keyframes.front().TimeSeconds; }
void FSplatCameraSequence::Seek(double timeSeconds)
{
    const auto& keys=ActiveKeyframes();
    m_currentTime=keys.empty()?0:ClampTime(timeSeconds,keys.front().TimeSeconds,GetDuration());
}
void FSplatCameraSequence::Tick(double deltaSeconds)
{
    if (!m_playing || !std::isfinite(deltaSeconds) || deltaSeconds<=0 || m_playbackKeyframes.size()<2) return;
    const double start=m_playbackKeyframes.front().TimeSeconds, end=GetDuration();
    const double next=m_currentTime+deltaSeconds;
    if (!std::isfinite(next)) { m_currentTime=end; m_playing=false; return; }
    if (next<end) { m_currentTime=next; return; }
    if (m_looping && end>start) m_currentTime=start+std::fmod(next-start,end-start);
    else { m_currentTime=end; m_playing=false; }
}

bool FSplatCameraSequence::SaveJson(const std::string& path,std::string* error) const
{
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if (!out) { Error(error,"cannot open sequence file for writing"); return false; }
    out << std::setprecision(17) << "{\n  \"version\": 1,\n  \"timeUnit\": \"seconds\",\n  \"fpsSuggestion\": 30,\n  \"keyframes\": [\n";
    for (size_t i=0;i<m_keyframes.size();++i)
    {
        const auto& k=m_keyframes[i]; const auto& s=k.State;
        out << "    {\"time\": " << k.TimeSeconds << ", \"positionX\": " << s.PositionX
            << ", \"positionY\": " << s.PositionY << ", \"positionZ\": " << s.PositionZ
            << ", \"yaw\": " << s.Yaw << ", \"pitch\": " << s.Pitch << ", \"roll\": " << s.Roll
            << ", \"fov\": " << s.FOV << ", \"near\": " << s.NearPlane << ", \"far\": " << s.FarPlane
            << ", \"distanceCulling\": " << (s.bDistanceCulling?"true":"false")
            << ", \"wireframe\": " << (s.bWireframe?"true":"false")
            << ", \"gaussianScale\": " << s.GaussianScale << "}" << (i+1<m_keyframes.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    if (!out) { Error(error,"failed writing sequence file"); return false; }
    return true;
}
bool FSplatCameraSequence::LoadJson(const std::string& path,std::string* error)
{
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if (!in) { Error(error,"cannot open sequence file"); return false; }
    const auto size=in.tellg();
    if (size<0 || size>1024*1024) { Error(error,"sequence file exceeds 1 MiB"); return false; }
    in.seekg(0); const std::string source((std::istreambuf_iterator<char>(in)),{});
    try
    {
        const Json root=JsonReader(source).Parse();
        if (root.At("version").Number()!=1 || root.At("timeUnit").String()!="seconds") throw std::runtime_error("unsupported sequence version or time unit");
        const Json& frames=root.At("keyframes");
        if (frames.kind!=Json::Kind::Array || frames.array.size()>10000) throw std::runtime_error("invalid keyframe array");
        std::vector<FSplatCameraKeyframe> parsed; parsed.reserve(frames.array.size());
        for (const Json& j:frames.array)
        {
            FSplatCameraKeyframe k; k.TimeSeconds=j.At("time").Number(); auto& s=k.State;
            s.PositionX=FloatField(j,"positionX"); s.PositionY=FloatField(j,"positionY"); s.PositionZ=FloatField(j,"positionZ");
            s.Yaw=FloatField(j,"yaw"); s.Pitch=FloatField(j,"pitch"); s.Roll=FloatField(j,"roll");
            s.FOV=FloatField(j,"fov"); s.NearPlane=FloatField(j,"near"); s.FarPlane=FloatField(j,"far");
            s.GaussianScale=FloatField(j,"gaussianScale");
            s.bDistanceCulling=j.At("distanceCulling").Bool(); s.bWireframe=j.At("wireframe").Bool();
            if (!std::isfinite(k.TimeSeconds) || k.TimeSeconds<0 || !s.IsValid() || (!parsed.empty() && k.TimeSeconds<=parsed.back().TimeSeconds))
                throw std::runtime_error("invalid or unsorted keyframe");
            parsed.push_back(k);
        }
        m_keyframes=std::move(parsed); Stop(); return true;
    }
    catch (const std::exception& e) { Error(error,e.what()); return false; }
}
} // namespace MonsterRender::Splat
