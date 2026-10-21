// Copyright Monster Engine. All Rights Reserved.
#include "Platform/Android/AndroidJNI.h"
#include "Profiling/MonsterProfiler.h"
#if PLATFORM_ANDROID
#include "Platform/Android/AndroidBuildJobHost.h"
#include "Platform/Android/AndroidRenderHost.h"
#include "Platform/Android/AndroidResourceProvider.h"
#include "Core/RuntimeLease.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include "Renderer/Splat/LOD/SplatSceneValidation.h"
#include "Renderer/Splat/LOD/SplatStreamingLODBuilder.h"
#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string_view>

namespace MonsterRender::Splat {
namespace {
namespace fs = std::filesystem;
constexpr uint64_t DefaultAggregatePolicy = 768ull << 20;
constexpr size_t CommandBytes = 65536, OutputBytes = 16u << 20;
constexpr uint64_t RenderGeneration = 0xa11d0001ull;
constexpr uint64_t TicketGeneration = 0xc11d0001ull;
JavaVM* VM = nullptr;
void Need(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
bool HashValid(const std::string& hash) { return hash.size()==64 && hash.find_first_not_of("0123456789abcdef")==std::string::npos; }

// Private bounded JSON transport. Reject duplicate keys, ambiguous integers,
// malformed Unicode, unknown command fields and excessive nesting/allocations.
void UTF8(std::string& out,uint32_t c) {
    Need(c<=0x10ffff && (c<0xd800||c>0xdfff),"Invalid Unicode scalar");
    if(c<128)out+=char(c);
    else if(c<2048){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}
    else if(c<65536){out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
    else{out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
}
struct Json {
    enum Kind { Null, Boolean, Number, Text, Array, Object } kind=Null;
    std::string text;
    std::vector<Json> array;
    std::map<std::string,Json> object;
    Json()=default;
    Json(bool b):kind(Boolean),text(b?"true":"false"){}
    Json(uint64_t n):kind(Number),text(std::to_string(n)){}
    Json(uint32_t n):Json(uint64_t(n)){}
    Json(int32_t n):kind(Number),text(std::to_string(n)){}
    Json(double n):kind(Number),text(std::to_string(n)){Need(std::isfinite(n),"Nonfinite JSON number");}
    Json(const char* s):kind(Text),text(s){}
    Json(std::string s):kind(Text),text(std::move(s)){}
    static Json Obj(){Json x;x.kind=Object;return x;}
    static Json Arr(){Json x;x.kind=Array;return x;}
    Json& operator[](const std::string& key){Need(kind==Object,"Expected object");return object[key];}
    bool has(const std::string& key)const{return kind==Object&&object.count(key);}
    const Json& at(const std::string& key)const{Need(kind==Object&&object.count(key),"Missing JSON field: "+key);return object.at(key);}
    std::string str()const{Need(kind==Text,"Expected string");return text;}
    bool boolean()const{Need(kind==Boolean,"Expected boolean");return text=="true";}
    uint64_t u64()const {
        uint64_t n=0;auto r=std::from_chars(text.data(),text.data()+text.size(),n);
        Need(kind==Number&&r.ec==std::errc{}&&r.ptr==text.data()+text.size()&&n<=INT64_MAX,"Expected signed-Long-compatible nonnegative integer");return n;
    }
    uint32_t u32()const{const auto n=u64();Need(n<=UINT32_MAX,"u32 overflow");return uint32_t(n);}
    int32_t i32()const{int32_t n=0;auto r=std::from_chars(text.data(),text.data()+text.size(),n);Need(kind==Number&&r.ec==std::errc{}&&r.ptr==text.data()+text.size(),"Expected i32");return n;}
    float number()const{Need(kind==Number,"Expected number");size_t used=0;double n=std::stod(text,&used);Need(used==text.size()&&std::isfinite(n)&&std::abs(n)<=std::numeric_limits<float>::max(),"Invalid finite float");return float(n);}
};
class Parser {
    const std::string& s;size_t p=0,nodes=0,cap;
    void space(){while(p<s.size()&&(s[p]==' '||s[p]=='\n'||s[p]=='\t'||s[p]=='\r'))++p;}
    char get(){Need(p<s.size(),"Truncated JSON");return s[p++];}
    uint32_t hex(){uint32_t n=0;for(int i=0;i<4;++i){char c=get();uint32_t d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99;Need(d<16,"Bad Unicode escape");n=n*16+d;}return n;}
    std::string string(){
        Need(get()=='"',"Expected JSON string");std::string out;
        for(;;){unsigned char c=get();if(c=='"')break;Need(c>=32,"Control character in JSON");
            if(c=='\\'){char e=get();switch(e){
                case '"':case '\\':case '/':out+=e;break;
                case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
                case 'u':{uint32_t code=hex();if(code>=0xd800&&code<=0xdbff){Need(get()=='\\'&&get()=='u',"Missing low surrogate");auto low=hex();Need(low>=0xdc00&&low<=0xdfff,"Invalid low surrogate");code=0x10000+((code-0xd800)<<10)+(low-0xdc00);}UTF8(out,code);break;}
                default:Need(false,"Invalid JSON escape");
            }}else out+=char(c);
            Need(out.size()<=cap,"JSON string limit");
        }return out;
    }
    Json value(unsigned depth){
        Need(depth<=20&&++nodes<=65536,"JSON complexity limit");space();Need(p<s.size(),"Missing JSON value");char c=s[p];
        if(c=='"')return Json(string());
        if(c=='{'||c=='['){++p;Json out=c=='{'?Json::Obj():Json::Arr();space();char end=c=='{'?'}':']';
            if(p<s.size()&&s[p]==end){++p;return out;}
            for(;;){space();if(c=='{'){auto key=string();space();Need(get()==':',"Missing colon");auto item=value(depth+1);Need(out.object.emplace(std::move(key),std::move(item)).second,"Duplicate JSON key");}
                else out.array.push_back(value(depth+1));space();char sep=get();if(sep==end)break;Need(sep==',',"Invalid JSON separator");}
            return out;
        }
        for(const char* literal:{"true","false","null"}){std::string token=literal;if(s.compare(p,token.size(),token)==0){p+=token.size();if(token=="null")return {};return Json(token=="true");}}
        size_t start=p;if(c=='-')++p;Need(p<s.size()&&s[p]>='0'&&s[p]<='9',"Invalid JSON number");
        if(s[p]=='0')++p;else while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;
        if(p<s.size()&&s[p]=='.'){++p;size_t begin=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;Need(p>begin,"Invalid fraction");}
        if(p<s.size()&&(s[p]=='e'||s[p]=='E')){++p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;size_t begin=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;Need(p>begin,"Invalid exponent");}
        Need(p-start<=128,"JSON numeric limit");Json out;out.kind=Json::Number;out.text=s.substr(start,p-start);return out;
    }
public:
    explicit Parser(const std::string& text,size_t maximum=OutputBytes):s(text),cap(maximum){Need(s.size()<=cap,"JSON byte limit");}
    Json parse(){auto out=value(0);space();Need(p==s.size(),"Trailing JSON data");return out;}
};
std::string Quote(const std::string& s){
    static const char hex[]="0123456789abcdef";std::string out="\"";
    for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}return out+'"';
}
std::string Dump(const Json& j){
    if(j.kind==Json::Null)return "null";if(j.kind==Json::Text)return Quote(j.text);
    if(j.kind==Json::Boolean||j.kind==Json::Number)return j.text;
    std::string out=j.kind==Json::Object?"{":"[";bool first=true;
    if(j.kind==Json::Object)for(const auto& [key,item]:j.object){if(!first)out+=',';first=false;out+=Quote(key)+':'+Dump(item);Need(out.size()<=OutputBytes,"Output JSON limit");}
    else for(const auto& item:j.array){if(!first)out+=',';first=false;out+=Dump(item);Need(out.size()<=OutputBytes,"Output JSON limit");}
    return out+(j.kind==Json::Object?"}":"]");
}
void Known(const Json& j,std::initializer_list<const char*> names){
    Need(j.kind==Json::Object,"Expected object");std::set<std::string> allowed;for(auto name:names)allowed.insert(name);
    for(const auto& [key,value]:j.object){(void)value;Need(allowed.count(key),"Unknown JSON field: "+key);}
}
void Version(const Json& j){Need(j.at("schemaVersion").u32()==1,"Unsupported schema version");}
Json Result(bool ok,const std::string& code,const std::string& message={},Json payload=Json::Obj()){
    Json j=Json::Obj();j["schemaVersion"]=uint32_t(1);j["ok"]=ok;j["code"]=code;j["message"]=message.substr(0,4096);j["payload"]=std::move(payload);return j;
}
Json Status(const FViewerStatus& s){Json j=Json::Obj();j["ok"]=s.ok;j["code"]=s.code;j["message"]="omitted_for_privacy";return j;}
Json Result(const FViewerStatus& s,Json payload=Json::Obj()){return Result(s.ok,s.code,s.message,std::move(payload));}
std::string SHA(const std::string& s){return FSplatSceneFormat::SHA256(std::vector<uint8_t>(s.begin(),s.end()));}
void JavaOK(JNIEnv* env){if(env->ExceptionCheck()){env->ExceptionClear();throw std::runtime_error("Java operation failed");}}
std::string String(JNIEnv* env,jstring s,size_t cap=CommandBytes){
    Need(s,"Null string");auto count=env->GetStringLength(s);JavaOK(env);Need(size_t(count)<=cap,"Input string limit");
    const jchar* chars=env->GetStringChars(s,nullptr);JavaOK(env);Need(chars,"String access failed");std::string out;
    try {for(jsize i=0;i<count;++i){uint32_t c=chars[i];if(c>=0xd800&&c<=0xdbff){Need(i+1<count,"Unpaired surrogate");auto low=chars[++i];Need(low>=0xdc00&&low<=0xdfff,"Unpaired surrogate");c=0x10000+((c-0xd800)<<10)+(low-0xdc00);}UTF8(out,c);Need(out.size()<=cap,"UTF8 input limit");}}
    catch(...){env->ReleaseStringChars(s,chars);throw;}env->ReleaseStringChars(s,chars);return out;
}
jstring JString(JNIEnv* env,const std::string& s){
    Need(s.size()<=OutputBytes,"Output byte limit");std::vector<jchar> out;out.reserve(s.size());
    for(size_t i=0;i<s.size();){uint32_t c=uint8_t(s[i++]);unsigned n=0;uint32_t minimum=0;
        if(c>=0xf0){Need(c<=0xf4,"Invalid UTF8");c&=7;n=3;minimum=65536;}else if(c>=0xe0){c&=15;n=2;minimum=2048;}else if(c>=0xc0){c&=31;n=1;minimum=128;}else Need(c<128,"Invalid UTF8");
        while(n--){Need(i<s.size()&&(uint8_t(s[i])&0xc0)==0x80,"Invalid UTF8 continuation");c=(c<<6)|(uint8_t(s[i++])&63);}
        Need(c>=minimum&&c<=0x10ffff&&(c<0xd800||c>0xdfff),"Invalid UTF8 scalar");
        if(c<65536)out.push_back(jchar(c));else{c-=65536;out.push_back(jchar(0xd800+(c>>10)));out.push_back(jchar(0xdc00+(c&1023)));}
    }return env->NewString(out.data(),jsize(out.size()));
}
template<class F> jstring StringBoundary(JNIEnv* env,F&& f) noexcept{
    try{MonsterEngine::Profiling::Scope responseScope("jni.string_response");return JString(env,Dump(f()));}
    catch(const std::exception& e){env->ExceptionClear();try{return JString(env,Dump(Result(false,"native_error",e.what())));}catch(...){return env->NewStringUTF("{\"schemaVersion\":1,\"ok\":false,\"code\":\"native_error\",\"message\":\"Failure\",\"payload\":{}}");}}
    catch(...){env->ExceptionClear();return env->NewStringUTF("{\"schemaVersion\":1,\"ok\":false,\"code\":\"native_error\",\"message\":\"Unexpected native exception\",\"payload\":{}}");}
}
template<class F> void VoidBoundary(JNIEnv* env,F&& f) noexcept{
    try{f();}catch(const std::exception& e){env->ExceptionClear();auto cls=env->FindClass("java/lang/IllegalStateException");if(cls)env->ThrowNew(cls,e.what());}
    catch(...){env->ExceptionClear();auto cls=env->FindClass("java/lang/IllegalStateException");if(cls)env->ThrowNew(cls,"Unexpected native exception");}
}
struct Env {
    JNIEnv* value=nullptr;bool attached=false;
    Env(){Need(VM,"JavaVM unavailable");jint result=VM->GetEnv(reinterpret_cast<void**>(&value),JNI_VERSION_1_6);if(result==JNI_EDETACHED){Need(VM->AttachCurrentThread(&value,nullptr)==JNI_OK,"Worker JVM attach failed");attached=true;}else Need(result==JNI_OK,"Worker JVM access failed");}
    ~Env(){if(attached)VM->DetachCurrentThread();}
};
struct LocalFrame {
    JNIEnv* env;
    explicit LocalFrame(JNIEnv* e,jint count=48):env(e){Need(env->PushLocalFrame(count)==0,"JNI local frame allocation failed");}
    ~LocalFrame(){env->PopLocalFrame(nullptr);}
};
struct Global {
    JavaVM* vm; jobject ref;
    Global(JNIEnv* env,jobject value):vm(VM),ref(env->NewGlobalRef(value)){JavaOK(env);Need(ref,"Global reference allocation failed");}
    ~Global(){if(!ref||!vm)return;JNIEnv* env=nullptr;bool attached=false;auto state=vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6);if(state==JNI_EDETACHED){if(vm->AttachCurrentThread(&env,nullptr)!=JNI_OK)return;attached=true;}else if(state!=JNI_OK)return;env->DeleteGlobalRef(ref);if(attached)vm->DetachCurrentThread();}
};
void WorkerOnly(JNIEnv* env){
    LocalFrame frame(env);auto cls=env->FindClass("android/os/Looper");JavaOK(env);
    auto main=env->GetStaticMethodID(cls,"getMainLooper","()Landroid/os/Looper;");auto mine=env->GetStaticMethodID(cls,"myLooper","()Landroid/os/Looper;");JavaOK(env);
    auto a=env->CallStaticObjectMethod(cls,main);JavaOK(env);auto b=env->CallStaticObjectMethod(cls,mine);JavaOK(env);
    Need(!a||!b||!env->IsSameObject(a,b),"Worker thread required");
}
struct Window {
    ANativeWindow* value=nullptr;
    Window(JNIEnv* env,jobject surface){if(surface){value=ANativeWindow_fromSurface(env,surface);JavaOK(env);Need(value,"Invalid Surface");}}
    ~Window(){if(value)ANativeWindow_release(value);}
};

// The wrapper, not just a JNI registry entry, owns the Java AssetManager pin.
// Host resources and scene pins therefore retain it through owner/workers.
class Provider final:public IResourceProvider {
public:
    std::shared_ptr<Global> assets;
    FAndroidResourceProvider provider;
    Provider(JNIEnv* env,jobject manager):assets(std::make_shared<Global>(env,manager)){}
    bool ReadBytes(const std::string& name,std::vector<uint8_t>& out,std::string& error)override{return provider.ReadBytes(name,out,error);}
    bool ResolveValidatedFile(const std::string& name,std::string& out,std::string& error)override{return provider.ResolveValidatedFile(name,out,error);}
};
struct Ticket { std::optional<FSplatBudgetCoordinator::FReservation> lease;uint64_t bytes=0;ESplatBudgetComponent component; };
struct Entry {
    FProcessRuntimeLease runtime=FProcessRuntimeLease::Acquire();
    std::mutex mutex;
    std::shared_ptr<Provider> resources;
    std::optional<FSplatBudgetCoordinator::FReservation> metadata;
    std::shared_ptr<FAndroidRenderHost> host;
    uint64_t hostId=0;
    std::string root;
    FSceneDescriptor builtin;
    FViewerConfig requested;
    std::map<uint64_t,Ticket> tickets;
    bool invalidated=false;
    ~Entry(){if(hostId)FAndroidRenderHost::Destroy(hostId);}
};
struct BuildEntry { FSplatBoundedBuildConfig config;std::string root; };
struct PrivateRootPin {std::string spelling;dev_t device=0;ino_t inode=0;};
struct Process {
    std::mutex mutex;
    std::shared_ptr<FSplatBudgetCoordinator> budget;
    std::optional<FSplatBudgetCoordinator::FReservation> bridgeMetadata;
    std::string root,limitSource,lastError;
    // One process may legitimately see the same app-private directory through
    // /data/data and /data/user/0 spellings, and instrumentation isolates owners
    // under distinct app-private roots. Each admitted root is verified app-owned;
    // identical inodes are unified to the first spelling so path comparisons agree.
    std::vector<PrivateRootPin> roots;
    FAndroidProbeOptions probe;
    uint64_t nextRender=1,nextTicket=1;
    std::map<uint64_t,std::shared_ptr<Entry>> renderers;
    std::map<uint64_t,std::shared_ptr<Entry>> retiredOwners;
    std::map<uint64_t,BuildEntry> builders;
};
Process& State(){static Process value;return value;}
std::shared_ptr<FSplatBudgetCoordinator> Account(uint64_t requested=0){
    auto& p=State();std::lock_guard lock(p.mutex);
    if(p.budget){Need(!requested||p.budget->Snapshot().limitBytes==requested,"budget_policy_mismatch: restart process after retiring owners to change aggregate policy");return p.budget;}
    auto budget=std::make_shared<FSplatBudgetCoordinator>(requested?requested:DefaultAggregatePolicy);std::string error;
    auto metadata=budget->TryReserve(ESplatBudgetComponent::Metadata,4ull<<20,error);Need(bool(metadata),error);
    p.budget=budget;p.bridgeMetadata=std::move(metadata);p.limitSource=requested?"requested_policy":"development_policy";return budget;
}
fs::path PrivateRoot(const std::string& candidate){
    Need(!candidate.empty()&&candidate.size()<=4096&&candidate.find('\0')==std::string::npos,"Invalid private files root");
    auto path=fs::canonical(fs::path(candidate));struct stat st{};
    // Android may create filesDir as 0771 with the application's own primary group.
    Need(::lstat(path.c_str(),&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==::getuid()&&!(st.st_mode&0002)&&(!(st.st_mode&0020)||st.st_gid==::getgid()),"Files root must be app-owned private storage");
    auto& p=State();std::lock_guard lock(p.mutex);
    for(const auto& pin:p.roots)if(pin.device==st.st_dev&&pin.inode==st.st_ino)return fs::path(pin.spelling);
    Need(p.roots.size()<16,"Private root registry exhausted");
    p.roots.push_back(PrivateRootPin{path.string(),st.st_dev,st.st_ino});
    if(p.root.empty())p.root=path.string();
    return path;
}
fs::path RootFor(const std::string& path){
    std::vector<PrivateRootPin> roots;std::string root;
    {auto& p=State();std::lock_guard lock(p.mutex);roots=p.roots;root=p.root;}
    auto at=path.find("/files/");
    if(at!=std::string::npos){
        const auto candidate=path.substr(0,at+6);
        Need(candidate.starts_with("/data/user/")||candidate.starts_with("/data/user_de/")||candidate.starts_with("/data/data/"),"Unsupported private root ancestry");
        return PrivateRoot(candidate);
    }
    // Non-files paths (for example a rejected /proc source) must still be checked
    // against an admitted root so the diagnostic is a real confinement decision.
    for(const auto& pin:roots)if(path.starts_with(pin.spelling+"/")||path==pin.spelling)return fs::path(pin.spelling);
    if(!root.empty()){struct stat st{};if(::stat(root.c_str(),&st)==0)return fs::path(root);}
    for(const auto& pin:roots){struct stat st{};if(::stat(pin.spelling.c_str(),&st)==0)return fs::path(pin.spelling);}
    throw std::runtime_error("Private files root has not been initialized");
}
fs::file_status InspectPrivatePath(const fs::path& path){
    std::error_code ec;
    const auto status=fs::symlink_status(path,ec);
    // libc++ reports ENOENT as well as not_found for an absent entry. Other
    // failures (including ENOTDIR and EACCES) must never admit a new leaf.
    Need(!ec||(status.type()==fs::file_type::not_found&&ec==std::errc::no_such_file_or_directory),
        "Unable to inspect private path component (filesystem error "+std::to_string(ec.value())+")");
    Need(status.type()!=fs::file_type::none,"Unknown private path component status");
    return status;
}
bool SameDirectory(const fs::path& left,const fs::path& right){
    std::error_code ec;const bool equal=fs::equivalent(left,right,ec);
    return !ec&&equal;
}
fs::path Confined(const fs::path& root,const std::string& path,bool mayNotExist=false){
    Need(!path.empty()&&path.size()<=4096&&path.find('\0')==std::string::npos,"Invalid private path");
    auto raw=fs::path(path);Need(raw.is_absolute(),"Private path must be absolute");
    for(const auto& part:raw)Need(part!="..","Traversal in private path");
    const auto normalized=raw.lexically_normal();
    const auto canonicalRoot=fs::canonical(root);
    // Every admitted root is Android app-private storage below /data. A path
    // outside this ancestry cannot be confined; reject it before any stat so an
    // unrelated EACCES (for example /proc) is never reported as a path failure.
    const auto text=normalized.generic_string();
    Need(text.starts_with("/data/user/")||text.starts_with("/data/user_de/")||text.starts_with("/data/data/"),
        "Path escapes private files root");

    // Android exposes the same app-private directory through aliases such as
    // /data/data/<package>/files and /data/user/0/<package>/files. MIUI may serve
    // both spellings from separate bind mounts, so canonical strings differ while
    // the device/inode identity is equal. Locate the supplied spelling of the root
    // by identity, then return the path in the pinned spelling so later ownership
    // comparisons remain stable.
    std::error_code ec;
    const auto finalStatus=InspectPrivatePath(normalized);
    fs::path resolved;
    if(fs::exists(finalStatus)){
        resolved=fs::canonical(normalized,ec);
        Need(!ec,"Unable to resolve private path");
    }else{
        Need(mayNotExist,"Private path does not exist");
        const auto parent=normalized.parent_path();
        const auto canonicalParent=fs::canonical(parent,ec);
        Need(!ec,"Private path parent does not exist");
        Need(fs::is_directory(canonicalParent,ec)&&!ec,"Private path parent must be an existing directory");
        resolved=canonicalParent/normalized.filename();
    }

    // Links above the root boundary may be platform aliases; links below it are
    // always rejected, even when they resolve back inside the root.
    fs::path aliasRoot;
    auto probe=normalized.root_path();
    for(const auto& part:normalized.relative_path()){
        probe/=part;
        const auto status=InspectPrivatePath(probe);
        if(!fs::exists(status))break;
        if(probe==root){aliasRoot=probe;break;}
        const auto probeCanonical=fs::canonical(probe,ec);
        if(ec){ec.clear();continue;}
        if(probeCanonical==canonicalRoot||SameDirectory(probe,canonicalRoot)){aliasRoot=probe;break;}
    }
    Need(!aliasRoot.empty(),"Path escapes private files root");
    const auto tail=normalized.lexically_relative(aliasRoot);
    Need(!tail.empty(),"Path escapes private files root");
    fs::path confined=root;
    for(const auto& part:tail){
        Need(part!=".."&&part!="."&&!part.empty(),"Private path escapes private files root");
        confined/=part;
    }
    confined=confined.lexically_normal();
    Need(confined!=root,"Path escapes private files root");
    auto cursor=aliasRoot;
    for(const auto& part:tail){
        cursor/=part;
        const auto status=InspectPrivatePath(cursor);
        Need(!fs::is_symlink(status),"Private path contains symlink");
        if(!fs::exists(status))Need(mayNotExist&&cursor==normalized,"Private path does not exist or missing ancestor");
    }
    if(mayNotExist&&!fs::exists(finalStatus))return confined;
    Need(resolved!=canonicalRoot&&confined!=root,"Path escapes private files root");
    return confined;
}
std::shared_ptr<Entry> Lookup(uint64_t token,bool allowRetired=false){
    Need((token>>32)==RenderGeneration&&uint32_t(token),"invalid_handle");
    auto& p=State();std::lock_guard lock(p.mutex);auto it=p.renderers.find(token);if(it!=p.renderers.end())return it->second;
    if(allowRetired){auto old=p.retiredOwners.find(token);if(old!=p.retiredOwners.end())return old->second;}
    throw std::runtime_error("invalid_handle");
}
std::shared_ptr<FAndroidRenderHost> Host(const std::shared_ptr<Entry>& entry){
    std::lock_guard lock(entry->mutex);Need(!entry->invalidated&&bool(entry->host),"invalid_handle");return entry->host;
}
const char* Components[]={"CPUCache","DecodedReady","SourceGPU","StagingGPU","TemporaryGPU","Swapchain","Metadata","Builder","Java","Native"};
Json Budget(const FSplatBudgetSnapshot& s){
    Json j=Json::Obj();j["limitBytes"]=s.limitBytes;j["reservedBytes"]=s.reservedBytes;j["peakBytes"]=s.peakBytes;j["exclusiveBuild"]=s.exclusiveBuild;j["components"]=Json::Obj();
    for(size_t i=0;i<size_t(ESplatBudgetComponent::Count);++i)j["components"][Components[i]]=s.components[i];
    {auto& p=State();std::lock_guard lock(p.mutex);j["limitSource"]=p.limitSource;}j["meaning"]="reservation_policy_not_measured_process_RAM";return j;
}
void Read(const Json& j,const char* key,uint64_t& out){if(j.has(key))out=j.at(key).u64();}
void Read(const Json& j,const char* key,uint32_t& out){if(j.has(key))out=j.at(key).u32();}
void Read(const Json& j,const char* key,int32_t& out){if(j.has(key))out=j.at(key).i32();}
void Read(const Json& j,const char* key,float& out){if(j.has(key))out=j.at(key).number();}
void Read(const Json& j,const char* key,bool& out){if(j.has(key))out=j.at(key).boolean();}
void Read(const Json& j,const char* key,std::string& out){if(j.has(key)){out=j.at(key).str();Need(out.size()<=4096&&out.find('\0')==std::string::npos,"Invalid text field");}}
#define READ(object,member) Read(object,#member,config.runtime.member)
FViewerConfig DecodeConfig(const Json& j){
    Version(j);Known(j,{"schemaVersion","runtime","maximumInternalEdge","maximumInternalPixels","generation","configHash","shaderIdentity","aggregateBudgetBytes"});
    FViewerConfig config;config.generation=j.at("generation").u64();
    Read(j,"maximumInternalEdge",config.maximumInternalEdge);Read(j,"maximumInternalPixels",config.maximumInternalPixels);
    Read(j,"configHash",config.configHash);Read(j,"shaderIdentity",config.shaderIdentity);
    Need(config.generation&&config.maximumInternalEdge&&config.maximumInternalEdge<=16384&&config.maximumInternalPixels&&config.maximumInternalPixels<=16384ull*16384,"Invalid viewer limits");
    const auto& r=j.at("runtime");
    Known(r,{"mode","width","height","renderWidth","renderHeight","activeCapacity","sortCapacity","tightTileCulling","shStorageMode","shStorageDegree","displaySHDegree","projectionColorCompression","rasterSharedMemoryMode","radixSortBlocksPerWorkgroup","radixSortPhaseTiming","projectedFusion","vsync","benchmark","validation","streaming","lod"});
    if(r.has("mode")){auto mode=r.at("mode").str();Need(mode.size()==1&&mode[0]>='A'&&mode[0]<='E',"Invalid viewer mode");config.runtime.mode=mode[0];}
    READ(r,width);READ(r,height);READ(r,renderWidth);READ(r,renderHeight);READ(r,activeCapacity);READ(r,sortCapacity);READ(r,tightTileCulling);
    READ(r,shStorageMode);READ(r,shStorageDegree);READ(r,displaySHDegree);READ(r,projectionColorCompression);READ(r,rasterSharedMemoryMode);
    READ(r,radixSortBlocksPerWorkgroup);READ(r,radixSortPhaseTiming);READ(r,projectedFusion);READ(r,vsync);READ(r,benchmark);READ(r,validation);
    if(r.has("streaming")){
        const auto& s=r.at("streaming");Known(s,{"sceneGPUBytes","cpuCacheBytes","stagingBytes","temporaryGPUBytes","maxUploadBytesPerFrame","maxConcurrentReads","retries","retryDelayMilliseconds"});
#define STREAM(name) Read(s,#name,config.runtime.streaming.name)
        STREAM(sceneGPUBytes);STREAM(cpuCacheBytes);STREAM(stagingBytes);STREAM(temporaryGPUBytes);STREAM(maxUploadBytesPerFrame);STREAM(maxConcurrentReads);STREAM(retries);STREAM(retryDelayMilliseconds);
#undef STREAM
    }
    if(r.has("lod")){
        const auto& l=r.at("lod");Known(l,{"qualityPixels","hysteresis","activeBudget","sortBudget","sourceBudget","freeze"});
#define LOD(name) Read(l,#name,config.runtime.lod.name)
        LOD(qualityPixels);LOD(hysteresis);LOD(activeBudget);LOD(sortBudget);LOD(sourceBudget);LOD(freeze);
#undef LOD
    }
    Need(!config.runtime.projectedFusion&&!config.runtime.projectionColorCompression&&config.runtime.shStorageMode==0&&config.runtime.radixSortBlocksPerWorkgroup==32,"Mobile baseline requires FP32, fusion/color-compression off and radix32");
    Need(!config.runtime.benchmark,"Desktop benchmark mode is not an Android client command");
    Need(config.configHash.empty()||HashValid(config.configHash),"Invalid requested config hash");
    Need(config.shaderIdentity.empty()||HashValid(config.shaderIdentity),"Invalid requested shader identity");
    std::string error;Need(config.runtime.Validate(error),error);
    // Identity is derived here; user text cannot stand in for canonical policy.
    auto canonical=j;canonical.object.erase("configHash");canonical.object.erase("shaderIdentity");canonical.object.erase("aggregateBudgetBytes");
    config.configHash=SHA(Dump(canonical));return config;
}
#undef READ
Json EncodeConfig(const FViewerConfig& c){
    Json j=Json::Obj(),r=Json::Obj(),s=Json::Obj(),l=Json::Obj();j["schemaVersion"]=uint32_t(1);
    j["maximumInternalEdge"]=c.maximumInternalEdge;j["maximumInternalPixels"]=c.maximumInternalPixels;j["generation"]=c.generation;j["configHash"]=c.configHash;j["shaderIdentity"]=c.shaderIdentity;
    r["mode"]=std::string(1,c.runtime.mode);
#define R(name) r[#name]=c.runtime.name
    R(width);R(height);R(renderWidth);R(renderHeight);R(activeCapacity);R(sortCapacity);R(tightTileCulling);R(shStorageMode);R(shStorageDegree);R(displaySHDegree);
    R(projectionColorCompression);R(rasterSharedMemoryMode);R(radixSortBlocksPerWorkgroup);R(radixSortPhaseTiming);R(projectedFusion);R(vsync);R(benchmark);R(validation);
#undef R
#define S(name) s[#name]=c.runtime.streaming.name
    S(sceneGPUBytes);S(cpuCacheBytes);S(stagingBytes);S(temporaryGPUBytes);S(maxUploadBytesPerFrame);S(maxConcurrentReads);S(retries);S(retryDelayMilliseconds);
#undef S
#define L(name) l[#name]=c.runtime.lod.name
    L(qualityPixels);L(hysteresis);L(activeBudget);L(sortBudget);L(sourceBudget);L(freeze);
#undef L
    r["streaming"]=std::move(s);r["lod"]=std::move(l);j["runtime"]=std::move(r);return j;
}
FSplatBoundedBuildConfig DecodeBuild(const Json& j){
    Version(j);Known(j,{"schemaVersion","workingMemoryBytes","metadataBytes","diskReserveBytes","maxDiskBytes","maxSourceBytes","maxSourceRecords","maxManifestBytes",
        "leafCapacity","pageCapacity","maxProxyCount","rootProxyCount","batchCapacity","maxDepth","maxNodes","maxPages","workerCount","pollIntervalRecords","maxHeaderBytes","maxRecordBytes",
        "supportSigma","proxyReduction","geometricErrorScale","appearanceWeight","maxAppearanceError","builderVersion","parserVersion","builderHash","retainCancelledWork"});
    FSplatBoundedBuildConfig c;
    // Kotlin's typed model always supplies every policy value; omission fails.
#define FIELD(name) Need(j.has(#name),"Missing build policy: " #name);Read(j,#name,c.name)
    FIELD(schemaVersion);FIELD(workingMemoryBytes);FIELD(metadataBytes);FIELD(diskReserveBytes);FIELD(maxDiskBytes);FIELD(maxSourceBytes);FIELD(maxSourceRecords);FIELD(maxManifestBytes);
    FIELD(leafCapacity);FIELD(pageCapacity);FIELD(maxProxyCount);FIELD(rootProxyCount);FIELD(batchCapacity);FIELD(maxDepth);FIELD(maxNodes);FIELD(maxPages);FIELD(workerCount);
    FIELD(pollIntervalRecords);FIELD(maxHeaderBytes);FIELD(maxRecordBytes);FIELD(supportSigma);FIELD(proxyReduction);FIELD(geometricErrorScale);FIELD(appearanceWeight);FIELD(maxAppearanceError);
    FIELD(builderVersion);FIELD(parserVersion);FIELD(builderHash);FIELD(retainCancelledWork);
#undef FIELD
    std::string error;Need(FSplatStreamingLODBuilder::ValidateConfig(c,error,false),error);return c;
}
Json Descriptor(const FSceneDescriptor& d){
    Json j=Json::Obj();j["schemaVersion"]=uint32_t(1);j["sceneId"]=d.sceneId;j["directory"]=d.directory;j["sourcePLY"]=d.sourcePLY;
    j["manifestPath"]=d.manifestPath;j["contentHash"]=d.contentHash;j["generation"]=d.generation;j["kind"]=d.kind==EViewerSceneKind::PLY?"PLY":"LOD";return j;
}
FSceneDescriptor DecodeScene(const Json& j,const std::shared_ptr<Entry>& e){
    Version(j);Known(j,{"schemaVersion","sceneId","directory","sourcePLY","manifestPath","contentHash","generation","kind"});
    FSceneDescriptor d;d.sceneId=j.at("sceneId").str();d.directory=j.at("directory").str();d.sourcePLY=j.at("sourcePLY").str();d.manifestPath=j.at("manifestPath").str();
    d.contentHash=j.at("contentHash").str();d.generation=j.at("generation").u64();auto kind=j.at("kind").str();
    Need(!d.sceneId.empty()&&d.sceneId.size()<=512&&d.generation&&HashValid(d.contentHash)&&(kind=="PLY"||kind=="LOD"),"Invalid scene identity");
    d.kind=kind=="PLY"?EViewerSceneKind::PLY:EViewerSceneKind::LOD;
    const auto directory=Confined(fs::path(e->root),d.directory);
    if(d.sceneId==e->builtin.sceneId){
        Need(directory==fs::path(e->builtin.directory)&&d.sourcePLY==e->builtin.sourcePLY&&d.manifestPath==e->builtin.manifestPath&&d.contentHash==e->builtin.contentHash&&d.kind==e->builtin.kind,"Builtin descriptor mismatch");
        d.initialCamera=e->builtin.initialCamera;
    }else{
        Need(d.sceneId.find_first_not_of("0123456789abcdef-")==std::string::npos&&d.sceneId.size()==36,"Invalid library scene UUID");
        Need(directory==fs::path(e->root)/"scene-store"/"published"/d.sceneId,"Scene must be a published library entry");
        if(d.kind==EViewerSceneKind::PLY)Need(d.sourcePLY==(directory/"source.ply").string()&&d.manifestPath.empty(),"Invalid PLY descriptor path");
        else Need(d.manifestPath==(directory/"scene.mgs").string()&&d.sourcePLY.empty(),"Invalid LOD descriptor path");
    }
    Confined(fs::path(e->root),d.kind==EViewerSceneKind::PLY?d.sourcePLY:d.manifestPath);return d;
}
Json Progress(const FSplatBuildProgress& p){
    static const char* names[]={"Created","Copying","Normalizing","Partitioning","BuildingLeaves","Reducing","Validating","Publishing","Completed","Paused","Cancelled","Failed","WaitingForResources"};
    Need(uint32_t(p.stage)<std::size(names),"Unknown build progress stage");Json j=Json::Obj();j["schemaVersion"]=uint32_t(1);j["stage"]=names[uint32_t(p.stage)];
#define P(name) j[#name]=p.name
    P(processedBytes);P(processedRecords);P(totalBytes);P(totalRecords);P(workingBytes);P(metadataBytes);P(peakWorkingBytes);P(diskBytes);P(checkpointGeneration);
#undef P
    j["status"]=p.status.substr(0,4096);j["error"]=p.error.substr(0,4096);return j;
}
Json BuildPayload(uint64_t token,const FAndroidBuildJobSnapshot& s){
    Json payload=Json::Obj();payload["progress"]=Progress(s.progress);payload["builderHash"]=s.builderHash;payload["running"]=s.running;payload["retired"]=s.retired;
    Json report=Json::Obj();report["schemaVersion"]=uint32_t(1);report["kind"]="build_report";report["progress"]=Progress(s.progress);
    report["progress"].object.erase("status");report["progress"].object.erase("error");report["hasError"]=!s.progress.error.empty();report["detail"]="omitted_for_privacy";
    report["builderHash"]=HashValid(s.builderHash)?Json(s.builderHash):Json();report["sourceIdentityAvailable"]=false;report["sourceSHA256"]=Json();report["sourceBytes"]=Json();report["sourceRecords"]=Json();
    report["sourceIdentityReason"]="retained_by_durable_job_store_and_native_journal";
    {auto& p=State();std::lock_guard lock(p.mutex);auto it=p.builders.find(token);if(it!=p.builders.end()){auto config=it->second.config;config.builderHash=s.builderHash;report["builderVersion"]=config.builderVersion;report["parserVersion"]=config.parserVersion;report["configHash"]=HashValid(s.builderHash)?Json(SHA(FSplatStreamingLODBuilder::CanonicalConfig(config))):Json();}}
    payload["buildReport"]=std::move(report);return payload;
}

// Exact loaded object identity is measured on nativeRunBuild's worker. A
// dladdr APK pseudo-path is opened as its exact ZipFile entry, not as a file.
struct CloseJava {
    JNIEnv* env; jobject object; jmethodID close;
    ~CloseJava(){if(object&&close){env->CallVoidMethod(object,close);if(env->ExceptionCheck())env->ExceptionClear();}}
};
bool LoadedBinaryHash(const FSplatBuildControl& control,std::string& hash,std::string& error){
    try{
        Env worker;auto* env=worker.value;WorkerOnly(env);LocalFrame locals(env,48);
        Dl_info info{};Need(::dladdr(reinterpret_cast<void*>(&JNI_OnLoad),&info)!=0&&info.dli_fname,"Loaded library identity unavailable");
        std::string path=info.dli_fname;Need(path.size()<=4096,"Loaded native path limit");
        auto mdClass=env->FindClass("java/security/MessageDigest");JavaOK(env);
        auto get=env->GetStaticMethodID(mdClass,"getInstance","(Ljava/lang/String;)Ljava/security/MessageDigest;");JavaOK(env);
        auto digest=env->CallStaticObjectMethod(mdClass,get,JString(env,"SHA-256"));JavaOK(env);Need(digest,"SHA256 unavailable");
        auto update=env->GetMethodID(mdClass,"update","([BII)V");auto finish=env->GetMethodID(mdClass,"digest","()[B");JavaOK(env);
        auto buffer=env->NewByteArray(65536);JavaOK(env);Need(buffer,"Hash buffer allocation failed");
        uint64_t total=0;auto poll=[&]{auto signal=control.Poll();Need(signal==ESplatBuildSignal::Continue,signal==ESplatBuildSignal::Cancel?"cancelled":"paused");};
        auto at=path.find("!/");
        if(at!=std::string::npos){
            const auto apk=path.substr(0,at),entryName=path.substr(at+2);
            Need(apk.ends_with(".apk")&&entryName.starts_with("lib/")&&entryName.ends_with("/libmonster_android.so")&&entryName.find("..")==std::string::npos,"Unsupported loaded APK library identity");
            auto zipClass=env->FindClass("java/util/zip/ZipFile");auto entryClass=env->FindClass("java/util/zip/ZipEntry");auto streamClass=env->FindClass("java/io/InputStream");JavaOK(env);
            auto ctor=env->GetMethodID(zipClass,"<init>","(Ljava/lang/String;)V");auto zipClose=env->GetMethodID(zipClass,"close","()V");
            auto getEntry=env->GetMethodID(zipClass,"getEntry","(Ljava/lang/String;)Ljava/util/zip/ZipEntry;");auto getInput=env->GetMethodID(zipClass,"getInputStream","(Ljava/util/zip/ZipEntry;)Ljava/io/InputStream;");
            auto getSize=env->GetMethodID(entryClass,"getSize","()J");auto read=env->GetMethodID(streamClass,"read","([B)I");auto streamClose=env->GetMethodID(streamClass,"close","()V");JavaOK(env);
            auto zip=env->NewObject(zipClass,ctor,JString(env,apk));JavaOK(env);Need(zip,"APK open failed");CloseJava closeZip{env,zip,zipClose};
            auto entry=env->CallObjectMethod(zip,getEntry,JString(env,entryName));JavaOK(env);Need(entry,"Loaded APK entry missing");
            jlong expected=env->CallLongMethod(entry,getSize);JavaOK(env);Need(expected>0&&expected<=jlong(512ull<<20),"Loaded APK entry identity limit");
            auto input=env->CallObjectMethod(zip,getInput,entry);JavaOK(env);Need(input,"Loaded APK entry open failed");CloseJava closeInput{env,input,streamClose};
            for(;;){poll();jint count=env->CallIntMethod(input,read,buffer);JavaOK(env);if(count<0)break;Need(count>0&&count<=65536&&total<=uint64_t(expected)&&uint64_t(count)<=uint64_t(expected)-total,"Loaded APK entry read/size mismatch");total+=uint64_t(count);env->CallVoidMethod(digest,update,buffer,0,count);JavaOK(env);}
            Need(total==uint64_t(expected),"Loaded APK entry truncated");
        }else{
            Need(fs::path(path).is_absolute(),"Loaded library path is not absolute");std::ifstream input(fs::path(path),std::ios::binary);Need(bool(input),"Loaded library open failed");
            std::array<jbyte,65536> bytes;
            while(input){poll();input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());auto count=input.gcount();Need(count>=0&&total<=(512ull<<20)-uint64_t(count),"Loaded binary identity limit");total+=uint64_t(count);
                if(count){env->SetByteArrayRegion(buffer,0,jsize(count),bytes.data());JavaOK(env);env->CallVoidMethod(digest,update,buffer,0,jint(count));JavaOK(env);}}
            Need(input.eof()&&total>0,"Loaded native binary read failed");
        }
        poll();auto bytes=static_cast<jbyteArray>(env->CallObjectMethod(digest,finish));JavaOK(env);Need(bytes&&env->GetArrayLength(bytes)==32,"Invalid SHA256 result");
        std::array<jbyte,32> raw{};env->GetByteArrayRegion(bytes,0,32,raw.data());JavaOK(env);static const char hex[]="0123456789abcdef";hash.clear();for(auto b:raw){auto u=uint8_t(b);hash+=hex[u>>4];hash+=hex[u&15];}
        error.clear();return true;
    }catch(const std::exception& e){hash.clear();error=e.what();return false;}
}
FAndroidProbeIdentity Identity(){
    FAndroidProbeIdentity i;i.application="MonsterEngine";
#if defined(__aarch64__)
    i.abi="arm64-v8a";
#elif defined(__x86_64__)
    i.abi="x86_64";
#endif
    return i; // Emulator/translation/build hashes remain unknown until measured.
}
FAndroidProbeOptions PrepareProbe(const std::shared_ptr<Provider>& provider){
    FAndroidProbeOptions options;options.identity=Identity();std::vector<uint8_t> bytes;std::string error;
    Need(provider->ReadBytes("Shaders/Splat/shader-manifest.json",bytes,error),error);Need(bytes.size()<=OutputBytes,"Shader manifest limit");
    options.shaderManifestJSON.assign(bytes.begin(),bytes.end());return options;
}
void RecordError(const std::string& error)noexcept{
    try{auto& p=State();std::lock_guard lock(p.mutex);p.lastError=error.substr(0,4096);}catch(...){}
}
// Consume only on the same creation worker; raw detail never enters public reports.
thread_local std::array<char,4097> RendererCreationError{};
thread_local std::array<char,81> RendererCreationPhase{};
thread_local FSplatBudgetSnapshot RendererCreationBudget;
thread_local uint64_t RendererCreationOwners=0,RendererRetiringOwners=0;
void RecordRendererCreationError(const char* phase,const char* message)noexcept{
    const auto copy=[](auto& output,const char* input){size_t at=0;while(input&&*input&&at+1<output.size()){const auto c=static_cast<unsigned char>(*input++);output[at++]=c>=32&&c<127?char(c):'?';}output[at]='\0';};
    copy(RendererCreationPhase,phase);copy(RendererCreationError,message);
}
jstring TakeRendererCreationError(JNIEnv* env,jobject){
    return StringBoundary(env,[&]{
        WorkerOnly(env);Json payload=Json::Obj();payload["phase"]=RendererCreationPhase.data();
        payload["budgetBeforeAdmission"]=Budget(RendererCreationBudget);payload["activeRendererOwners"]=RendererCreationOwners;payload["retiringRendererOwners"]=RendererRetiringOwners;
        std::string message=RendererCreationError.data();RendererCreationError[0]='\0';RendererCreationPhase[0]='\0';
        return Result(false,"native_creation_rejected",message.empty()?"No creation diagnostic on this worker":message,std::move(payload));
    });
}
jlong Create(JNIEnv* env,jobject,jobject assets,jstring files,jstring configuration)noexcept{
    RendererCreationError[0]='\0';RendererCreationPhase[0]='\0';RendererCreationBudget={};RendererCreationOwners=RendererRetiringOwners=0;
    const char* phase="worker_or_request";
    try{
        WorkerOnly(env);Need(assets,"AssetManager required");
        auto text=String(env,configuration);auto json=Parser(text,CommandBytes).parse();auto config=DecodeConfig(json);
        uint64_t limit=0;if(json.has("aggregateBudgetBytes")){limit=json.at("aggregateBudgetBytes").u64();Need(limit>0,"Aggregate budget must be positive");}
        auto budget=Account(limit);auto runtime=FProcessRuntimeLease::Acquire();std::string startupError;
        phase="startup_reservation";RendererCreationBudget=budget->Snapshot();
        {auto& p=State();std::lock_guard lock(p.mutex);RendererCreationOwners=p.renderers.size();RendererRetiringOwners=p.retiredOwners.size();}
        // Covers provider's bounded asset buffers and semantic validator peak;
        // this is an admission envelope, never a claim of observed RSS.
        auto startup=budget->TryReserve(ESplatBudgetComponent::Metadata,320ull<<20,startupError);Need(bool(startup),startupError);
        phase="private_root_and_asset_inventory";
        auto root=PrivateRoot(String(env,files,4096));
        auto provider=std::make_shared<Provider>(env,assets);std::string error;auto manager=AAssetManager_fromJava(env,assets);JavaOK(env);
        Need(manager&&provider->provider.Initialize(manager,root.string(),error),error);
        auto options=PrepareProbe(provider);auto requirements=FAndroidCapabilities::ParseShaderRequirements(options.shaderManifestJSON,{},error);
        Need(requirements.valid,error);Need(config.shaderIdentity.empty()||config.shaderIdentity==requirements.shaderIdentity,"Requested shader inventory mismatch");
        config.shaderIdentity=requirements.shaderIdentity;options.identity.configSHA256=config.configHash;
        // Materialize only inventory-verified files, preserving the manifest's
        // relative page layout inside the private provider cache. No runtime
        // construction and no direct-PLY admission exception is involved.
        std::vector<uint8_t> defaultBytes;Need(provider->ReadBytes("resources/android/default-viewer.json",defaultBytes,error),error);
        Need(defaultBytes.size()<=CommandBytes,"Builtin configuration limit");
        auto defaults=Parser(std::string(defaultBytes.begin(),defaultBytes.end()),CommandBytes).parse();Version(defaults);
        phase="builtin_camera_metadata";
        const auto& builtin=defaults.at("builtinScene");const auto logical=builtin.at("asset").str();
        const auto& camera=builtin.at("camera");Known(camera,{"eye","target","up","verticalFovDegrees","nearPlane","farPlane","rollDegrees"});
        FViewerOrbitCameraPreset cameraPreset;
        for(const auto* name:{"eye","target","up"})Need(camera.at(name).kind==Json::Array&&camera.at(name).array.size()==3,"Invalid builtin camera vector");
        for(int axis=0;axis<3;++axis){
            cameraPreset.eye[axis]=camera.at("eye").array[axis].number();cameraPreset.target[axis]=camera.at("target").array[axis].number();
            Need(camera.at("up").array[axis].number()==(axis==1?1.0f:0.0f),"Builtin orbit requires upright Y camera");
        }
        cameraPreset.verticalFOVDegrees=camera.at("verticalFovDegrees").number();cameraPreset.nearPlane=camera.at("nearPlane").number();cameraPreset.farPlane=camera.at("farPlane").number();
        if(camera.has("rollDegrees"))cameraPreset.rollDegrees=camera.at("rollDegrees").number();
        Need(FSplatViewerSession::IsValidOrbitCameraPreset(cameraPreset),"Invalid builtin orbit camera pose");
        Need(logical=="scenes/bonsai-lod/scene.mgs","Unsupported builtin LOD asset");
        const auto& source=builtin.at("source");const auto sourceHash=source.at("sha256").str();
        Need(HashValid(sourceHash)&&source.at("records").u64()>0,"Invalid builtin source identity");
        std::string builtinPath;Need(provider->ResolveValidatedFile(logical,builtinPath,error),error);
        phase="builtin_page_materialization";
        const auto sceneDirectory=fs::path(builtinPath).parent_path();
        const auto logicalDirectory=fs::path(logical).parent_path();
        // Use the verified bounded package index to materialize files first.
        // Semantic validation then preflights index/workspace caps before any
        // full manifest object is allocated.
        std::vector<uint8_t> indexBytes;Need(provider->ReadBytes("scenes/builtin-lod.json",indexBytes,error),error);
        Need(indexBytes.size()<=OutputBytes,"Builtin LOD package index limit");
        auto index=Parser(std::string(indexBytes.begin(),indexBytes.end()),OutputBytes).parse();Version(index);
        const auto& packageFiles=index.at("files");Need(packageFiles.kind==Json::Array&&packageFiles.array.size()<=100001,"Builtin LOD package file limit");
        std::set<std::string> materialized;
        for(const auto& file:packageFiles.array){
            const auto name=file.at("path").str();const fs::path relative(name);
            Need(!relative.is_absolute()&&relative.generic_string()==name&&name.find('\\')==std::string::npos&&
                name.find(':')==std::string::npos,"Invalid builtin page path");
            for(const auto& component:relative)Need(component!=".."&&component!="."&&!component.empty(),"Invalid builtin page component");
            Need(name=="scene.mgs"||(name.starts_with("pages/")&&name.ends_with(".mgsp")),"Unexpected builtin LOD file");
            Need(materialized.insert(name).second,"Duplicate builtin LOD file");
            std::string pagePath;Need(provider->ResolveValidatedFile((logicalDirectory/relative).generic_string(),pagePath,error),error);
            Need(Confined(root,pagePath)==sceneDirectory/relative,"Builtin page escaped manifest directory");
        }
        phase="builtin_native_validation";
        FSplatSceneManifest manifest;Need(FSplatSceneFormat::LoadManifest(builtinPath,manifest,error),error);
        std::string manifestHash;Need(FSplatSceneFormat::SHA256File(builtinPath,manifestHash,error),error);
        const auto& packageValidation=index.at("validation");
        uint64_t encodedBytes=0;for(const auto& page:manifest.pages)encodedBytes+=page.byteSize;
        // The signed APK inventory already binds every page hash to the host
        // builder's validation output. Re-check those identities and the manifest
        // hash instead of decoding 1.2M gaussians on every owner creation; fall
        // back to the full native validation when the package index is incomplete.
        const bool indexed=packageValidation.has("ok")&&packageValidation.at("ok").boolean()&&
            packageValidation.has("contentSHA256")&&packageValidation.has("pageCount")&&
            packageValidation.has("encodedPageBytes")&&packageValidation.has("sourceRecords")&&
            packageValidation.at("contentSHA256").str()==manifestHash&&
            packageValidation.at("pageCount").u64()==manifest.pages.size()&&
            packageValidation.at("encodedPageBytes").u64()==encodedBytes&&
            packageValidation.at("sourceRecords").u64()==manifest.sourceCount&&
            manifest.sourceCount==source.at("records").u64()&&manifest.leafCount==manifest.sourceCount;
        FSplatSceneValidationResult validation;
        if(indexed){validation.contentSHA256=manifestHash;validation.pageCount=uint32_t(manifest.pages.size());
            validation.sourceRecords=manifest.sourceCount;validation.encodedPageBytes=encodedBytes;validation.shDegree=manifest.shDegree;}
        else Need(FSplatSceneValidation::Validate(builtinPath,{},validation,error),error);
        Need(manifest.version==3&&manifest.builderVersion==SplatBoundedBuilderVersion&&manifest.sourceSHA256==sourceHash&&
            manifest.builderHash==index.at("builderSHA256").str()&&validation.contentSHA256==packageValidation.at("contentSHA256").str()&&
            manifest.sourceCount==source.at("records").u64()&&manifest.leafCount==manifest.sourceCount,"Builtin LOD source identity mismatch");
        for(const auto& page:manifest.pages)Need(materialized.contains(page.relativePath),"Builtin page absent from package index");
        auto entry=std::make_shared<Entry>();entry->resources=provider;entry->root=root.string();entry->requested=config;
        entry->metadata=budget->TryReserve(ESplatBudgetComponent::Metadata,8ull<<20,error);Need(bool(entry->metadata),error);
        entry->builtin.sceneId="builtin-"+validation.contentSHA256;entry->builtin.directory=sceneDirectory.string();
        entry->builtin.manifestPath=builtinPath;entry->builtin.contentHash=validation.contentSHA256;entry->builtin.generation=1;entry->builtin.kind=EViewerSceneKind::LOD;
        entry->builtin.initialCamera=cameraPreset;
        FAndroidRenderHostCreateInfo info;info.resources=provider;info.budget=budget;info.config=config;info.probe=options;
        phase="native_render_owner";
        entry->hostId=FAndroidRenderHost::Create(std::move(info),error);Need(entry->hostId,error);
        entry->host=FAndroidRenderHost::Acquire(entry->hostId);Need(bool(entry->host),"Host registry creation failed");
        auto& p=State();std::lock_guard lock(p.mutex);Need(p.renderers.size()<16&&p.retiredOwners.size()<128&&p.nextRender<=UINT32_MAX,"Render registry exhausted");
        uint64_t token=(RenderGeneration<<32)|p.nextRender++;p.renderers.emplace(token,entry);p.probe=std::move(options);p.lastError.clear();return jlong(token);
    }catch(const std::exception& e){env->ExceptionClear();RecordRendererCreationError(phase,e.what());RecordError(e.what());return 0;}
    catch(...){env->ExceptionClear();RecordRendererCreationError(phase,"Unexpected native creation failure");RecordError("Unexpected native creation failure");return 0;}
}
jstring Probe(JNIEnv* env,jobject,jobject surface){
    return StringBoundary(env,[&]{
        FAndroidProbeOptions options;std::string lastError;
        {auto& p=State();std::lock_guard lock(p.mutex);options=p.probe;lastError=p.lastError;}
        if(options.identity.application.empty())options.identity=Identity();
        Window window(env,surface);options.nativeWindow=window.value;
        auto report=FAndroidCapabilities::Probe(options);Json payload=Parser(report.json).parse();payload["bridgeLastErrorAvailable"]=!lastError.empty();
        // Error text remains private; public reports expose stage/availability.
        return Result(true,"probe_observed","",std::move(payload));
    });
}
jstring Attach(JNIEnv* env,jobject,jlong token,jobject surface,jlong generation,jint width,jint height,jint displayWidth,jint displayHeight){
    return StringBoundary(env,[&]{
        Need(generation>0&&width>0&&height>0&&width<=16384&&height<=16384&&
            displayWidth>0&&displayHeight>0&&displayWidth<=16384&&displayHeight<=16384,"Invalid Surface extent/generation");
        auto e=Lookup(uint64_t(token));auto host=Host(e);Window window(env,surface);
        auto status=host->Attach(window.value,uint64_t(generation),uint32_t(width),uint32_t(height),uint32_t(displayWidth),uint32_t(displayHeight));
        // Queued is an admission ACK only. Readiness is observed independently.
        Json payload=Json::Obj();payload["attachment"]="queued";payload["drawableObserved"]=false;payload["surfaceGeneration"]=uint64_t(generation);return Result(status,std::move(payload));
    });
}
jstring Detach(JNIEnv* env,jobject,jlong token,jlong generation){
    return StringBoundary(env,[&]{Need(generation>0,"Invalid Surface generation");auto host=Host(Lookup(uint64_t(token)));return Result(host->Detach(uint64_t(generation),std::chrono::milliseconds(100)));});
}
void Resume(JNIEnv* env,jobject,jlong token,jboolean resumed){VoidBoundary(env,[&]{auto s=Host(Lookup(uint64_t(token)))->SetResumed(resumed);Need(s.ok,s.code);});}
void Frame(JNIEnv* env,jobject,jlong token,jlong timestamp){MonsterEngine::Profiling::Scope boundaryScope("jni.frame_boundary",0,0,0,uint64_t(timestamp));VoidBoundary(env,[&]{Need(timestamp>0,"Invalid frame timestamp");auto s=Host(Lookup(uint64_t(token)))->RequestFrame(uint64_t(timestamp));Need(s.ok,s.code);});}
void Input(JNIEnv* env,jobject,jlong token,jstring command){
    VoidBoundary(env,[&]{
        auto j=Parser(String(env,command),CommandBytes).parse();Version(j);Known(j,{"schemaVersion","phase","pointerId","orbitX","orbitY","panX","panY","pinchScale","reset","surfaceGeneration"});
        FCameraInputState input;auto phase=j.at("phase").str();
        Need(phase=="Begin"||phase=="Move"||phase=="End"||phase=="Cancel","Invalid pointer phase");
        input.phase=phase=="Begin"?EViewerPointerPhase::Begin:phase=="End"?EViewerPointerPhase::End:phase=="Cancel"?EViewerPointerPhase::Cancel:EViewerPointerPhase::Move;
        input.pointerId=j.at("pointerId").i32();input.orbitX=j.at("orbitX").number();input.orbitY=j.at("orbitY").number();input.panX=j.at("panX").number();input.panY=j.at("panY").number();input.pinchScale=j.at("pinchScale").number();
        input.reset=j.at("reset").boolean();input.surfaceGeneration=j.at("surfaceGeneration").u64();Need(input.pinchScale>0&&input.surfaceGeneration,"Invalid camera input");
        auto status=Host(Lookup(uint64_t(token)))->CameraInput(input);Need(status.ok,status.code);
    });
}
struct ScenePin {std::shared_ptr<Provider> resources;FSceneDescriptor descriptor;};
jstring OpenScene(JNIEnv* env,jobject,jlong token,jstring descriptor){
    return StringBoundary(env,[&]{
        WorkerOnly(env);auto entry=Lookup(uint64_t(token));auto host=Host(entry);auto j=Parser(String(env,descriptor),CommandBytes).parse();auto scene=DecodeScene(j,entry);
        // Kotlin's SceneRepository lease is held by the UI through actual release
        // ACK. Native pins immutable descriptor/provider across queued parsing.
        auto pin=std::make_shared<ScenePin>();{std::lock_guard lock(entry->mutex);pin->resources=entry->resources;}pin->descriptor=scene;
        return Result(host->OpenScene(std::move(scene),std::move(pin)));
    });
}
uint64_t DecimalToken(const Json& j){
    auto s=j.str();Need(!s.empty()&&s.size()<=20&&s[0]!='0',"Invalid reservation token");uint64_t n=0;auto r=std::from_chars(s.data(),s.data()+s.size(),n);
    Need(r.ec==std::errc{}&&r.ptr==s.data()+s.size()&&(n>>32)==TicketGeneration&&uint32_t(n),"Invalid reservation token");return n;
}
Json BudgetControl(uint64_t owner,const Json& command){
    const auto action=command.at("action").str();auto e=Lookup(owner,action=="release");auto budget=Account();
    if(action=="reserve"){
        Known(command,{"action","bytes","component"});auto bytes=command.at("bytes").u64();auto name=command.at("component").str();
        auto found=std::find_if(std::begin(Components),std::end(Components),[&](const char* value){return name==value;});Need(found!=std::end(Components),"Unknown budget component");
        auto component=ESplatBudgetComponent(found-std::begin(Components));
        // Dedicated cache/GPU/builder owners charge themselves. Managed bridge
        // admits only generic managed/native/metadata backing to avoid doubles.
        Need(component==ESplatBudgetComponent::Java||component==ESplatBudgetComponent::Native||component==ESplatBudgetComponent::Metadata,"Dedicated budget component is owned by native subsystem");
        uint64_t id;{auto& p=State();std::lock_guard lock(p.mutex);Need(p.nextTicket<=UINT32_MAX,"Reservation IDs exhausted");id=(TicketGeneration<<32)|p.nextTicket++;}
        std::lock_guard lock(e->mutex);Need(!e->invalidated&&e->tickets.size()<64,"Reservation owner retired or ticket limit reached");
        std::string error;auto lease=budget->TryReserve(component,bytes,error);if(!lease)return Result(false,"budget_exceeded",error);
        Ticket ticket;ticket.bytes=bytes;ticket.component=component;ticket.lease=std::move(lease);e->tickets.emplace(id,std::move(ticket));
        Json payload=Json::Obj();payload["reservationId"]=std::to_string(id);payload["bytes"]=bytes;payload["component"]=name;return Result(true,"reserved","",std::move(payload));
    }
    if(action=="release"){
        Known(command,{"action","reservationId"});auto id=DecimalToken(command.at("reservationId"));std::lock_guard lock(e->mutex);
        auto found=e->tickets.find(id);Need(found!=e->tickets.end(),"Reservation token does not belong to this owner");
        if(!found->second.lease)return Result(true,"already_released");
        found->second.lease.reset();
        if(e->invalidated&&std::none_of(e->tickets.begin(),e->tickets.end(),[](const auto& item){return bool(item.second.lease);})){
            // Keep only bounded token tombstones for idempotent explicit release;
            // provider/runtime ownership is no longer needed after all backing
            // tickets and actual host retirement have completed.
            e->resources.reset();e->metadata.reset();e->runtime.Reset();
        }return Result(true,"released");
    }
    Known(command,{"action"});auto host=Host(e);
    if(action=="requestExclusiveBuild"){
        auto retired=host->ReleaseScene(std::chrono::milliseconds(2000));if(!retired.ok)return Result(retired);
        std::string error;if(!budget->RequestExclusiveBuild(error))return Result(false,"waiting_for_resources",error);
        return Result(true,"exclusive_build_admitted","",Budget(budget->Snapshot()));
    }
    if(action=="releaseExclusiveBuild"){
        budget->ReleaseExclusiveBuild();auto snapshot=budget->Snapshot();
        return Result(!snapshot.exclusiveBuild,snapshot.exclusiveBuild?"builder_ownership_retained":"exclusive_build_released","",Budget(snapshot));
    }
    return Result(false,"unknown_budget_control");
}
jstring Settings(JNIEnv* env,jobject,jlong token,jstring command){
    return StringBoundary(env,[&]{
        auto j=Parser(String(env,command),CommandBytes).parse();Version(j);
        if(j.has("budgetControl")){Known(j,{"schemaVersion","budgetControl"});WorkerOnly(env);return BudgetControl(uint64_t(token),j.at("budgetControl"));}
        auto e=Lookup(uint64_t(token));auto host=Host(e);
        if(j.has("profilingControl")){
            Known(j,{"schemaVersion","profilingControl"});auto c=j.at("profilingControl");Known(c,{"enabled","clear"});
            const bool enabled=c.at("enabled").boolean();
            if(c.has("clear")&&c.at("clear").boolean()){
                MonsterEngine::Profiling::SetEnabled(false);
                MonsterEngine::Profiling::Clear();
            }
            MonsterEngine::Profiling::SetEnabled(enabled);
            Json payload=Json::Obj();payload["profilingEnabled"]=MonsterEngine::Profiling::IsEnabled();
            return Result(true,"profiling_configured","",std::move(payload));
        }
        if(j.has("viewerControl")){
            Known(j,{"schemaVersion","viewerControl"});auto c=j.at("viewerControl");Known(c,{"action"});
            Need(c.at("action").str()=="releaseScene","Unknown viewer control");WorkerOnly(env);return Result(host->ReleaseScene(std::chrono::milliseconds(2000)));
        }
        auto config=DecodeConfig(j);if(j.has("aggregateBudgetBytes"))Account(j.at("aggregateBudgetBytes").u64());
        {std::lock_guard lock(e->mutex);config.shaderIdentity=e->requested.shaderIdentity;}auto status=host->ApplySettings(config);
        if(status.ok){std::lock_guard lock(e->mutex);e->requested=config;}return Result(status);
    });
}
void Trim(JNIEnv* env,jobject,jlong token,jint level){VoidBoundary(env,[&]{auto s=Host(Lookup(uint64_t(token)))->MemoryPressure(level);Need(s.ok,s.code);});}
jstring Retry(JNIEnv* env,jobject,jlong token){return StringBoundary(env,[&]{return Result(Host(Lookup(uint64_t(token)))->Retry());});}
FViewerStatus RetireRenderer(uint64_t token){
        if(!token)return {true,"already_retired",{}};Need((token>>32)==RenderGeneration&&uint32_t(token),"invalid_handle");std::shared_ptr<Entry> e;
        {auto& p=State();std::lock_guard lock(p.mutex);auto it=p.renderers.find(token);
            if(it==p.renderers.end())return p.retiredOwners.contains(token)?FViewerStatus{false,"renderer_retirement_pending",{}}:FViewerStatus{true,"already_retired",{}};
            e=std::move(it->second);p.renderers.erase(it);p.retiredOwners.emplace(token,e);}
        uint64_t hostId=0;
        {std::lock_guard lock(e->mutex);e->invalidated=true;hostId=e->hostId;e->hostId=0;}
        // Removed admission before owner join; never hold a registry/job lock.
        if(hostId)Need(FAndroidRenderHost::Destroy(hostId),"Renderer host retirement not acknowledged");
        bool liveTickets=false;
        {std::lock_guard lock(e->mutex);e->host.reset();for(const auto& [id,ticket]:e->tickets){(void)id;liveTickets|=bool(ticket.lease);}}
        if(!liveTickets){auto& p=State();std::lock_guard lock(p.mutex);p.retiredOwners.erase(token);}
        // Explicit tickets remain reachable via budget release on retired owner.
        // No GC/handle disappearance is proof that managed backing retired.
        return {true,"renderer_retired",{}};
}
void Destroy(JNIEnv* env,jobject,jlong token){
    VoidBoundary(env,[&]{const auto status=RetireRenderer(uint64_t(token));Need(status.ok,status.code);});
}
jstring RetireOwner(JNIEnv* env,jobject,jlong token){
    return StringBoundary(env,[&]{WorkerOnly(env);auto status=RetireRenderer(uint64_t(token));
        Json payload=Json::Obj();payload["budgetAfterRetirement"]=Budget(Account()->Snapshot());return Result(status,std::move(payload));});
}
Json Diagnostics(const std::shared_ptr<Entry>& e,const std::shared_ptr<const FAndroidRenderHostSnapshot>& s){
    MonsterEngine::Profiling::Scope snapshotScope("jni.snapshot");
    Json payload=Json::Obj();payload["schemaVersion"]=uint32_t(1);payload["status"]=Status(s->status);
    payload["profilingEnabled"]=MonsterEngine::Profiling::IsEnabled();
    payload["profiling"]=Json::Obj();payload["profiling"]["schemaVersion"]=uint32_t(1);
    payload["profiling"]["enabled"]=MonsterEngine::Profiling::IsEnabled();
    payload["profiling"]["rawJSONField"]="profilingRawJSON";
    if(MonsterEngine::Profiling::IsEnabled()){
        MonsterEngine::Profiling::Scope exportScope("diagnostics.profiling_export");
        payload["profilingRawJSON"]=MonsterEngine::Profiling::ExportJson();
    }
    payload["diagnostics"]=s->diagnosticsJSON.empty()?Json::Obj():Parser(s->diagnosticsJSON).parse();
    payload["diagnostics"]["available"]=!s->diagnosticsJSON.empty();
    if(s->diagnosticsJSON.empty())payload["diagnostics"]["reason"]="no_session_observation";
    payload["measurementSchemaVersion"]=uint32_t(2);
    payload["framesCSV"]=s->framesCSV;payload["capabilities"]=s->capabilities.json.empty()?Json::Obj():Parser(s->capabilities.json).parse();
    payload["budget"]=Budget(Account()->Snapshot());payload["surfaceGeneration"]=s->surfaceGeneration;payload["resumed"]=s->resumed;payload["windowInUse"]=s->windowInUse;
    {auto& p=State();std::lock_guard lock(p.mutex);payload["bridgeLastErrorAvailable"]=!p.lastError.empty();}
    payload["manualRetryRequired"]=s->manualRetryRequired;payload["pressureRetired"]=s->pressureRetired;payload["presentationReservationEstimate"]=s->presentationReservationEstimate;
    payload["driverPresentationBackingMeasured"]=s->driverPresentationBackingMeasured;
    {std::lock_guard lock(e->mutex);payload["requestedConfig"]=EncodeConfig(e->requested);}
    if(s->viewer&&s->viewer->effectiveConfig)payload["effectiveConfig"]=EncodeConfig(*s->viewer->effectiveConfig);
    else payload["effectiveConfig"]=Json();
    return payload;
}
void WriteExport(const fs::path& root,const fs::path& path,const std::vector<uint8_t>& bytes){
    Confined(root,path.string(),true);auto directory=path.parent_path();fs::create_directories(directory);Confined(root,directory.string());
    auto temporary=path;temporary+=".tmp";int fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);Need(fd>=0,"Export temporary open failed");
    try{size_t at=0;while(at<bytes.size()){ssize_t n=::write(fd,bytes.data()+at,std::min<size_t>(65536,bytes.size()-at));Need(n>0,"Export write failed");at+=size_t(n);}Need(::fsync(fd)==0,"Export sync failed");::close(fd);fd=-1;
        Need(::rename(temporary.c_str(),path.c_str())==0,"Export publication failed");int dir=::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);Need(dir>=0,"Export directory open failed");int result=::fsync(dir);::close(dir);Need(result==0,"Export directory sync failed");
    }catch(...){if(fd>=0)::close(fd);::unlink(temporary.c_str());throw;}
}
jstring Snapshot(JNIEnv* env,jobject,jlong token,jstring command){
    return StringBoundary(env,[&]{
        auto j=Parser(String(env,command),CommandBytes).parse();Version(j);Known(j,{"schemaVersion","kind","includePrivatePaths"});if(j.has("includePrivatePaths"))Need(!j.at("includePrivatePaths").boolean(),"Raw private-path diagnostics are not supported");
        auto e=Lookup(uint64_t(token));auto host=Host(e);auto kind=j.at("kind").str();
        if(kind=="builtinScene"){Json payload=Json::Obj();payload["descriptor"]=Descriptor(e->builtin);return Result(true,"builtin_ready","",std::move(payload));}
        if(kind=="budget")return Result(true,"budget_observed","",Budget(Account()->Snapshot()));
        if(kind=="diagnostics"||kind=="framesCSV")return Result(true,"snapshot_observed","",Diagnostics(e,host->Snapshot()));
        if(kind=="screenshot")return Result(host->RequestScreenshot());
        if(kind=="screenshotStatus"){
            WorkerOnly(env);FViewerScreenshot image;if(!host->TakeScreenshot(image)){Json payload=Json::Obj();payload["status"]="pending";payload["gpuCompleted"]=false;return Result(false,"pending","",std::move(payload));}
            if(!image.status.ok)return Result(image.status);
            Need(!image.png.empty()&&image.png.size()<=(128ull<<20),"Screenshot output limit");
            static std::atomic<uint64_t> sequence{1};auto id=sequence.fetch_add(1);Need(id,"Screenshot export sequence exhausted");
            auto path=fs::path(e->root)/"exports"/("screenshot-"+std::to_string(uint64_t(::getpid()))+"-"+std::to_string(id)+".png");
            WriteExport(fs::path(e->root),path,image.png);Json payload=Json::Obj();payload["status"]="completed";payload["gpuCompleted"]=true;payload["path"]=path.string();
            payload["metadata"]=Parser(image.metadataJSON).parse();payload["metadata"]["privateExportPathIncluded"]=false;
            auto metadata=Dump(payload.at("metadata"));WriteExport(fs::path(e->root),fs::path(path.string()+".json"),std::vector<uint8_t>(metadata.begin(),metadata.end()));
            return Result(true,"screenshot_completed","",std::move(payload));
        }
        return Result(false,"unknown_snapshot_kind");
    });
}
jstring Validate(JNIEnv* env,jobject,jstring input){
    return StringBoundary(env,[&]{
        WorkerOnly(env);auto runtime=FProcessRuntimeLease::Acquire();auto raw=String(env,input,4096);auto root=RootFor(raw);auto path=Confined(root,raw);auto budget=Account();std::string error;
        auto lease=budget->TryReserve(ESplatBudgetComponent::Metadata,256ull<<20,error);if(!lease)return Result(false,"waiting_for_resources",error);
        FSplatSceneValidationResult observed;bool ok=FSplatSceneValidation::Validate(path.string(),{},observed,error);
        Json payload=Json::Obj();if(ok){payload["contentHash"]=observed.contentSHA256;payload["sourceRecords"]=observed.sourceRecords;payload["pageCount"]=observed.pageCount;payload["encodedPageBytes"]=observed.encodedPageBytes;payload["shDegree"]=observed.shDegree;}
        return Result(ok,ok?"validated":"validation_failed",error,std::move(payload));
    });
}
// Creation errors belong to the calling build worker, never to the process's
// latest renderer/monitor failure. Fixed storage keeps the noexcept catch safe.
thread_local std::array<char,4097> BuildCreationError{};
void RecordBuildCreationError(const char* phase,const char* message)noexcept{
    size_t used=0;
    for(const char* text:{phase,": ",message}){
        if(!text)continue;
        while(*text&&used+1<BuildCreationError.size()){
            const auto c=static_cast<unsigned char>(*text++);
            BuildCreationError[used++]=c>=32&&c<127?char(c):'?';
        }
    }
    BuildCreationError[used]='\0';
}
jstring TakeBuildCreationError(JNIEnv* env,jobject){
    return StringBoundary(env,[&]{
        std::string message=BuildCreationError.data();BuildCreationError[0]='\0';
        return Result(false,"native_creation_rejected",message.empty()?"No creation diagnostic on this worker":message);
    });
}
jlong CreateBuild(JNIEnv* env,jobject,jstring source,jstring journal,jstring output,jstring command)noexcept{
    BuildCreationError[0]='\0';
    const char* phase="worker_or_request";
    try{
        WorkerOnly(env);auto src=String(env,source,4096),job=String(env,journal,4096),out=String(env,output,4096);
        phase="private_root";auto root=RootFor(src);
        phase="stable_source";auto sourcePath=Confined(root,src);
        phase="journal_path";auto jobPath=Confined(root,job,true);
        phase="output_path";auto outputPath=Confined(root,out,true);
        phase="build_path_ownership";
        Need(sourcePath.parent_path()==root/"scene-store"/"build-inputs","Build source must be stable private import");
        Need(jobPath.filename()=="journal"&&jobPath.parent_path().parent_path()==root/"build-jobs","Invalid durable journal owner");
        Need(outputPath.parent_path()==root/"scene-store"/"staging","Invalid private native output");
        phase="build_policy";auto config=DecodeBuild(Parser(String(env,command),CommandBytes).parse());FAndroidBuildJobRequest request;
        request.stableSource=sourcePath.string();request.jobDirectory=jobPath.string();request.outputDirectory=outputPath.string();request.config=config;request.resolveBinaryIdentity=LoadedBinaryHash;
        phase="native_build_owner";std::string error;auto token=FAndroidBuildJobHost::Create(std::move(request),*Account(),error);Need(token,error);
        phase="build_registry";
        try{auto& p=State();std::lock_guard lock(p.mutex);Need(p.builders.size()<32,"Build bridge registry exhausted");p.builders.emplace(token,BuildEntry{config,root.string()});}
        catch(...){FAndroidBuildJobHost::Destroy(token);throw;}return jlong(token);
    }catch(const std::exception& e){env->ExceptionClear();RecordBuildCreationError(phase,e.what());return 0;}
    catch(...){env->ExceptionClear();RecordBuildCreationError(phase,"Unexpected native build creation failure");return 0;}
}
void CheckBuild(uint64_t token){auto& p=State();std::lock_guard lock(p.mutex);Need(p.builders.count(token),"invalid_handle");}
jstring RunBuild(JNIEnv* env,jobject,jlong handle){
    return StringBoundary(env,[&]{WorkerOnly(env);auto token=uint64_t(handle);CheckBuild(token);auto result=FAndroidBuildJobHost::Run(token);return Result(result.ok,result.code,result.message,BuildPayload(token,result.snapshot));});
}
jstring BuildControl(JNIEnv* env,jobject,jlong handle,jstring action){
    return StringBoundary(env,[&]{auto token=uint64_t(handle);CheckBuild(token);auto command=String(env,action,16);std::string error;
        auto ok=FAndroidBuildJobHost::Control(token,command,error);return Result(ok,ok?"control_signalled":"control_rejected",error);});
}
jstring BuildSnapshot(JNIEnv* env,jobject,jlong handle){
    return StringBoundary(env,[&]{auto token=uint64_t(handle);CheckBuild(token);std::string error;FAndroidBuildJobSnapshot snapshot;
        if(!FAndroidBuildJobHost::Snapshot(token,snapshot,error))return Result(false,"invalid_handle",error);return Result(true,"build_snapshot","",BuildPayload(token,snapshot));});
}
void DestroyBuild(JNIEnv* env,jobject,jlong handle){
    VoidBoundary(env,[&]{if(!handle)return;auto token=uint64_t(handle);{auto& p=State();std::lock_guard lock(p.mutex);auto it=p.builders.find(token);if(it==p.builders.end())return;p.builders.erase(it);}FAndroidBuildJobHost::Destroy(token);});
}

// One registration table, exactly matching J04 NativeBridge object methods.
#define METHOD(name,signature,function) {const_cast<char*>(name),const_cast<char*>(signature),reinterpret_cast<void*>(function)}
JNINativeMethod Methods[]={
    METHOD("nativeProbe","(Landroid/view/Surface;)Ljava/lang/String;",Probe),
    METHOD("nativeCreate","(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/lang/String;)J",Create),
    METHOD("nativeTakeRendererCreationError","()Ljava/lang/String;",TakeRendererCreationError),
    METHOD("nativeAttach","(JLandroid/view/Surface;JIIII)Ljava/lang/String;",Attach),
    METHOD("nativeDetach","(JJ)Ljava/lang/String;",Detach),
    METHOD("nativeResume","(JZ)V",Resume),
    METHOD("nativeFrame","(JJ)V",Frame),
    METHOD("nativeInput","(JLjava/lang/String;)V",Input),
    METHOD("nativeOpenScene","(JLjava/lang/String;)Ljava/lang/String;",OpenScene),
    METHOD("nativeSettings","(JLjava/lang/String;)Ljava/lang/String;",Settings),
    METHOD("nativeTrim","(JI)V",Trim),
    METHOD("nativeRetry","(J)Ljava/lang/String;",Retry),
    METHOD("nativeSnapshot","(JLjava/lang/String;)Ljava/lang/String;",Snapshot),
    METHOD("nativeDestroy","(J)V",Destroy),
    METHOD("nativeRetireOwner","(J)Ljava/lang/String;",RetireOwner),
    METHOD("nativeValidateScene","(Ljava/lang/String;)Ljava/lang/String;",Validate),
    METHOD("nativeCreateBuild","(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)J",CreateBuild),
    METHOD("nativeTakeBuildCreationError","()Ljava/lang/String;",TakeBuildCreationError),
    METHOD("nativeRunBuild","(J)Ljava/lang/String;",RunBuild),
    METHOD("nativeBuildControl","(JLjava/lang/String;)Ljava/lang/String;",BuildControl),
    METHOD("nativeBuildSnapshot","(J)Ljava/lang/String;",BuildSnapshot),
    METHOD("nativeDestroyBuild","(J)V",DestroyBuild)
};
#undef METHOD
} // namespace
} // namespace MonsterRender::Splat

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm,void*) {
    using namespace MonsterRender::Splat;
    try{
        JNIEnv* env=nullptr;if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK)return JNI_ERR;
        VM=vm;auto bridge=env->FindClass("com/monsterengine/viewer/NativeBridge");if(!bridge){env->ExceptionClear();VM=nullptr;return JNI_ERR;}
        jint registered=env->RegisterNatives(bridge,Methods,jint(std::size(Methods)));env->DeleteLocalRef(bridge);
        if(registered!=0||env->ExceptionCheck()){env->ExceptionClear();VM=nullptr;return JNI_ERR;}return JNI_VERSION_1_6;
    }catch(...){VM=nullptr;return JNI_ERR;}
}
extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*,void*) {
    using namespace MonsterRender::Splat;
    try{
        std::map<uint64_t,std::shared_ptr<Entry>> owners,retired;std::map<uint64_t,BuildEntry> builds;
        {auto& p=State();std::lock_guard lock(p.mutex);owners.swap(p.renderers);retired.swap(p.retiredOwners);builds.swap(p.builders);}
        for(const auto& [token,build]:builds){(void)build;FAndroidBuildJobHost::Destroy(token);}
        // VM unload implies no further managed calls. Owners retire outside locks;
        // their providers/global refs remain pinned through native owner join.
        owners.clear();retired.clear();
    }catch(...){}
}
#endif
