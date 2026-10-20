#include "VideoTo3D/ProjectPreflight.h"
#include "VideoTo3D/ProjectInput.h"
#include <Windows.h>
#include <Aclapi.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <vector>
using namespace MonsterEngine::VideoTo3D;
namespace fs = std::filesystem;
static void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
static void Code(PreflightCode value, PreflightCode expected) { if(value != expected) throw std::runtime_error(std::string("Expected ") + PreflightCodeName(expected) + ", got " + PreflightCodeName(value)); }
static void Write(const fs::path& path, const std::string& bytes) { std::ofstream f(path,std::ios::binary); f.write(bytes.data(),bytes.size()); Require(bool(f),"fixture write"); }
static std::string Read(const fs::path& path) { std::ifstream f(path,std::ios::binary); Require(bool(f),"fixture read"); return {std::istreambuf_iterator<char>(f),{}}; }
static std::map<std::wstring,std::string> Inventory(const fs::path& root) { std::map<std::wstring,std::string> result; for(const auto& e:fs::recursive_directory_iterator(root)) result[e.path().lexically_relative(root).native()] = e.is_directory()?"DIRECTORY":Read(e.path()); return result; }
static void Put32(std::string& value,size_t offset,unsigned number) { for(unsigned i=0;i<4;++i) value.at(offset+i)=char(number>>(8*i)); }
static SpaceBudget Budget() { return {0,0,0,0,0,0,0,0,0}; }
static const std::vector<std::optional<uint64_t> SpaceBudget::*> fields{&SpaceBudget::mediaCopies,&SpaceBudget::preparedImages,&SpaceBudget::reconstructionDatabase,&SpaceBudget::checkpoints,&SpaceBudget::temporaryExports,&SpaceBudget::lod,&SpaceBudget::newAssets,&SpaceBudget::retainedOverlap,&SpaceBudget::reserve};
static void Fixture(const fs::path& dir, bool input) {
    auto repo=ProjectRepository::Create(dir,"preflight fixture");
    if(input) { const auto source=dir.parent_path()/(dir.filename().native()+L"-source.mp4"); Write(source,"original-source-bytes"); ProjectInput::Import(*repo,InputKind::Video,source); }
    repo.reset(); fs::remove(dir/L".monsterstudio.lock");
    Write(dir/L"project.mstudio.tmp","stale-project-do-not-remove"); Write(dir/L"input.mstudio.tmp","stale-input-do-not-remove");
    fs::create_directories(dir/L"staging"); Write(dir/L"staging"/L"keep.tmp","stale-staging");
}
class DeniedWrite final {
    fs::path dir_; PSECURITY_DESCRIPTOR original_{}; PACL replacement_{};
public:
    explicit DeniedWrite(const fs::path& dir, DWORD permissions=FILE_ADD_FILE):dir_(dir) {
        PACL acl{}; Require(GetNamedSecurityInfoW(const_cast<wchar_t*>(dir.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&original_)==ERROR_SUCCESS,"read original ACL");
        SID_IDENTIFIER_AUTHORITY authority=SECURITY_WORLD_SID_AUTHORITY; PSID everyone{};
        Require(AllocateAndInitializeSid(&authority,1,SECURITY_WORLD_RID,0,0,0,0,0,0,0,&everyone)!=0,"world SID");
        EXPLICIT_ACCESSW entry{}; entry.grfAccessPermissions=permissions; entry.grfAccessMode=DENY_ACCESS; entry.Trustee.TrusteeForm=TRUSTEE_IS_SID; entry.Trustee.ptstrName=static_cast<LPWSTR>(everyone);
        const auto code=SetEntriesInAclW(1,&entry,acl,&replacement_); FreeSid(everyone); Require(code==ERROR_SUCCESS,"compose ACL");
        Require(SetNamedSecurityInfoW(const_cast<wchar_t*>(dir.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,replacement_,nullptr)==ERROR_SUCCESS,"install denied-write ACL");
    }
    void Restore() { if(!original_) return; PACL acl{}; BOOL present{},defaulted{}; Require(GetSecurityDescriptorDacl(original_,&present,&acl,&defaulted)!=0,"original DACL"); SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision{}; Require(GetSecurityDescriptorControl(original_,&control,&revision)!=0,"original control"); Require(SetNamedSecurityInfoW(const_cast<wchar_t*>(dir_.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|((control&SE_DACL_PROTECTED)?PROTECTED_DACL_SECURITY_INFORMATION:UNPROTECTED_DACL_SECURITY_INFORMATION),nullptr,nullptr,acl,nullptr)==ERROR_SUCCESS,"restore ACL"); LocalFree(original_); original_=nullptr; LocalFree(replacement_); replacement_=nullptr; }
    ~DeniedWrite() { try { Restore(); } catch(...) { std::cerr<<"ACL RESTORE FAILED\n"; } }
};
int wmain(int argc,wchar_t** argv) { std::cout << std::unitbuf;
    if(argc==3 && std::wstring(argv[1])==L"--fixture") { try { Fixture(argv[2],true); return 0; } catch(const std::exception& e) { std::cerr << e.what(); return 90; } }
    const auto root=fs::temp_directory_path()/(L"MonsterPreflight-tests-"+std::to_wstring(GetCurrentProcessId())); fs::create_directory(root);
    int pass=0,fail=0;
    auto test=[&](const char* name,const std::function<void(const fs::path&)>& fn) { const auto dir=root/std::to_wstring(pass+fail); fs::create_directory(dir); try { fn(dir); ++pass; std::cout<<"PASS "<<name<<'\n'; } catch(const std::exception& e) { ++fail; std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n'; } };
    test("zero_exact_short_caller_quota",[](const fs::path&) { auto b=Budget(); Code(EvaluateSpaceBudget(b,{0,100,1000}).diagnostic.code,PreflightCode::Ok); b.mediaCopies=10; b.reserve=2; auto r=EvaluateSpaceBudget(b,{12,999,1000}); Code(r.diagnostic.code,PreflightCode::Ok); Require(r.incrementalBytes==10 && r.requiredBytes==12 && r.reserveBytes==2,"account totals"); Code(EvaluateSpaceBudget(b,{11,999,1000}).diagnostic.code,PreflightCode::InsufficientSpace); });
    test("all_fields_required_and_accounted",[](const fs::path&) { for(const auto f:fields) { auto b=Budget(); (b.*f).reset(); Code(EvaluateSpaceBudget(b,{100,100,100}).diagnostic.code,PreflightCode::InvalidBudget); b=Budget(); b.*f=7; Require(EvaluateSpaceBudget(b,{7,7,7}).requiredBytes==7,"field not counted"); } });
    test("all_field_overflow_and_uint64_max",[](const fs::path&) { const auto max=(std::numeric_limits<uint64_t>::max)(); for(const auto f:fields) { auto b=Budget(); b.*f=max; Code(EvaluateSpaceBudget(b,{max,max,max}).diagnostic.code,PreflightCode::Ok); if(f==&SpaceBudget::mediaCopies) b.reserve=1; else b.mediaCopies=1; Code(EvaluateSpaceBudget(b,{max,max,max}).diagnostic.code,PreflightCode::ArithmeticOverflow); } });
    test("real_capacity_sample",[](const fs::path& dir) { auto r=QuerySpacePreflight(dir,Budget()); Code(r.diagnostic.code,PreflightCode::Ok); Require(r.sample.has_value(),"missing sample"); ULARGE_INTEGER a{},t{},f{}; Require(GetDiskFreeSpaceExW(dir.c_str(),&a,&t,&f)!=0,"independent capacity query"); Require(r.sample->volumeTotal==t.QuadPart && r.sample->volumeFree<=r.sample->volumeTotal && r.sample->availableToCaller<=r.sample->volumeTotal,"capacity fields"); });
    test("real_write_full_lifecycle_preserves_existing",[](const fs::path& dir) { Write(dir/L".monster-preflight-existing.tmp","keep"); const auto before=Inventory(dir); const auto r=ProbeDirectoryWrite(dir); Code(r.diagnostic.code,PreflightCode::Ok); Require(r.created&&r.flushed&&r.closed&&r.removed&&r.bytesWritten==4096&&r.residualPath.empty(),"probe lifecycle"); Require(Inventory(dir)==before,"probe changed inventory"); });
    test("real_acl_denied_write_and_restored",[](const fs::path& dir) { { DeniedWrite deny(dir); const auto r=ProbeDirectoryWrite(dir); Code(r.diagnostic.code,PreflightCode::AccessDenied); Require(!r.created&&r.bytesWritten==0&&r.residualPath.empty(),"denied probe created data"); deny.Restore(); } Code(ProbeDirectoryWrite(dir).diagnostic.code,PreflightCode::Ok); });
    test("missing_and_regular_file_destinations",[](const fs::path& dir) { const auto missing=dir/L"missing"; Code(ProbeDirectoryWrite(missing).diagnostic.code,PreflightCode::MissingDestination); Require(!fs::exists(missing),"implicit mkdir"); Write(dir/L"file","keep"); Code(ProbeDirectoryWrite(dir/L"file").diagnostic.code,PreflightCode::NotDirectory); Require(Read(dir/L"file")=="keep","collision overwritten"); Code(InspectProjectCompatibility(dir).diagnostic.code,PreflightCode::MissingProject); });
    test("lexical_unsafe_paths",[](const fs::path& dir) { for(const auto& value:std::vector<fs::path>{L"relative",L"C:ambiguous",L"\\\\.\\C:\\",L"\\\\server\\share",dir/L"..",dir/L"NUL",dir/L"trailing.",dir/std::wstring(256,L'a'),fs::path(L"C:\\"+std::wstring(16385,L'a'))}) Code(ProbeDirectoryWrite(value).diagnostic.code,PreflightCode::UnsupportedPath); });
    test("supported_optional_input_read_only",[](const fs::path& dir) { const auto p=dir/L"project"; Fixture(p,false); const auto before=Inventory(p); const auto r=InspectProjectCompatibility(p); Code(r.diagnostic.code,PreflightCode::Ok); Code(r.input.diagnostic.code,PreflightCode::NoInput); Require(r.project.fullyValidatedStructure&&r.stableObservation&&!r.mediaIntegrityVerified&&!r.inputPresent,"inspection depth"); Require(Inventory(p)==before,"inspection mutation"); });
    test("supported_input_read_only_and_pure_decoders",[](const fs::path& dir) { const auto p=dir/L"project"; Fixture(p,true); const auto before=Inventory(p); auto snapshot=ProjectRepository::DecodeSnapshotBytes(Read(p/L"project.mstudio")); const auto input=ProjectInput::DecodeManifestBytes(Read(p/L"input.mstudio"),snapshot.projectId); Require(input.projectId==snapshot.projectId,"pure decode identity"); const auto r=InspectProjectCompatibility(p); Code(r.diagnostic.code,PreflightCode::Ok); Require(r.project.fullyValidatedStructure&&r.input.fullyValidatedStructure&&r.stableObservation&&!r.mediaIntegrityVerified,"supported depth"); Require(Inventory(p)==before,"supported mutation"); });
    test("unknown_corrupt_identity_preservation",[](const fs::path& dir) { for(int mode=0;mode<6;++mode) { const auto p=dir/std::to_wstring(mode); Fixture(p,true); const auto file=p/(mode<3?L"project.mstudio":L"input.mstudio"); auto bytes=Read(file); const auto expected=(mode==0||mode==3)?PreflightCode::UnsupportedSchema:(mode==5?PreflightCode::IdentityMismatch:PreflightCode::CorruptData); if(mode==0||mode==3) Put32(bytes,8,99); else if(mode==5) bytes[16]=bytes[16]=='a'?'b':'a'; else if(mode==1||mode==4) bytes.resize(10); else bytes.back()=char(255); Write(file,bytes); const auto before=Inventory(p); const auto r=InspectProjectCompatibility(p); Code(r.diagnostic.code,expected); Require(!r.mediaIntegrityVerified&&r.stableObservation,"failure observation"); Require(Inventory(p)==before,"failure mutation"); } });
    test("real_metadata_read_denied_restored",[](const fs::path& dir) { const auto p=dir/L"project"; Fixture(p,true); const auto before=Inventory(p); { DeniedWrite deny(p/L"project.mstudio",FILE_READ_DATA); const auto r=InspectProjectCompatibility(p); Code(r.diagnostic.code,PreflightCode::AccessDenied); Require(!r.stableObservation&&!r.project.fullyValidatedStructure,"denied read claimed validation"); deny.Restore(); } Code(InspectProjectCompatibility(p).diagnostic.code,PreflightCode::Ok); Require(Inventory(p)==before,"denied read mutation"); });
    test("concurrent_metadata_writer_conservative",[](const fs::path& dir) { const auto p=dir/L"project"; Fixture(p,true); const auto before=Inventory(p); HANDLE h=CreateFileW((p/L"project.mstudio").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr); Require(h!=INVALID_HANDLE_VALUE,"writer fixture"); const auto r=InspectProjectCompatibility(p); CloseHandle(h); Code(r.diagnostic.code,PreflightCode::UnstableObservation); Require(!r.stableObservation&&Inventory(p)==before,"conflict claimed stable/mutated"); });
    test("real_long_unicode_write_and_compatibility",[](const fs::path& dir) { auto extended=fs::path(L"\\\\?\\"+dir.native()); for(int i=0;i<5;++i) extended/=L"\u9879\u76ee long unicode component 012345678901234567890123456789"; fs::create_directories(extended); Require(extended.native().size()>300,"not a long path"); Code(ProbeDirectoryWrite(extended).diagnostic.code,PreflightCode::Ok); const auto source=dir/L"seed"; Fixture(source,true); for(const auto& e:fs::recursive_directory_iterator(source)) { const auto target=extended/e.path().lexically_relative(source); if(e.is_directory()) fs::create_directory(target); else fs::copy_file(e.path(),target); } const auto before=Inventory(extended); const auto r=InspectProjectCompatibility(extended); Code(r.diagnostic.code,PreflightCode::Ok); Require(r.input.fullyValidatedStructure&&Inventory(extended)==before,"long read changed bytes"); const auto ordinary=fs::path(extended.native().substr(4)); Code(ProbeDirectoryWrite(ordinary).diagnostic.code,PreflightCode::Ok); });
    std::cout<<"SUMMARY "<<pass<<" passed "<<fail<<" failed\n";
    fs::remove_all(fs::path(L"\\\\?\\"+root.native())); return fail?1:0;
}



