#include "VideoTo3D/ProjectReconstruction.h"
#include "VideoTo3D/ToolAdmission.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <windows.h>
using namespace MonsterEngine::VideoTo3D;namespace fs=std::filesystem;
void Need(bool value,const char*message){if(!value)throw std::runtime_error(message);}
void Write(const fs::path&p,const std::string&bytes){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(bytes.data(),bytes.size());Need(bool(f),"fixture write");}
std::string Read(const fs::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void Replace(std::string&s,const std::string&a,const std::string&b){size_t offset=0;while((offset=s.find(a,offset))!=std::string::npos){s.replace(offset,a.size(),b);offset+=b.size();}}
std::string Quote(const std::string&s){std::string out="\"";for(auto c:s){if(c=='\\'||c=='\"')out+='\\';out+=c;}return out+'\"';}
std::string Hash(const fs::path&p){auto id=InspectToolIdentity(ToolKind::Brush,p,{});Need(id.status==IdentityStatus::Unpinned,"fixture hash");return id.actualSha256;}
template<class T>void Put(std::string&bytes,T v){bytes.append(reinterpret_cast<const char*>(&v),sizeof(v));}
std::string Cameras(int model=1,double focal=500,double cx=320,int count=1,bool trailing=false){std::string b;Put(b,uint64_t(count));for(int n=0;n<count;++n){Put(b,int32_t(1));Put(b,int32_t(model));Put(b,uint64_t(640));Put(b,uint64_t(480));Put(b,focal);if(model!=0)Put(b,focal);Put(b,cx);Put(b,240.0);}if(trailing)b+='x';return b;}
std::string Images(double qw=1,int camera=1,const std::string&name="frame.png",int count=1){std::string b;Put(b,uint64_t(count));for(int n=0;n<count;++n){Put(b,int32_t(1));for(double d:{qw,0.,0.,0.,0.,0.,2.})Put(b,d);Put(b,int32_t(camera));b+=name;b+='\0';Put(b,uint64_t(0));}return b;}
struct Fixture{
 fs::path project,execution,attempt,output,cameras,images;std::unique_ptr<ReconstructionQueue>q;InputManifest input;std::string id,preparedMode="valid",cameraBytes=Cameras(),imageBytes=Images(),pointerOverride,candidateOverride;
 Fixture(const fs::path&root){fs::create_directories(root);project=root/L"p";q=ReconstructionQueue::Create(project,"synthetic sealed metadata parser fixture");Write(root/L"video.mov","synthetic owned bytes");input=q->ImportVideo(root/L"video.mov");id=ProjectRepository::NewId();execution=q->Directory()/L"executions"/fs::u8path(id);attempt=execution/L"attempt-1";output=attempt/L"output"/L"model.ply";cameras=execution/L"preparation"/L"dataset"/L"sparse"/L"cameras.bin";images=execution/L"preparation"/L"dataset"/L"sparse"/L"images.bin";Write(output,"synthetic output identity only, never GPU evidence");}
 std::string Ref(const fs::path&p,const std::string&path){return "{\"path\":"+Quote(path)+",\"bytes\":"+std::to_string(fs::file_size(p))+",\"sha256\":"+Quote(Hash(p))+"}";}
 void Seal(){Write(cameras,cameraBytes);Write(images,imageBytes);auto f=input.files[0];std::string in="{\"id\":"+Quote(input.inputId)+",\"revision\":"+std::to_string(input.revision)+",\"relativePath\":"+Quote(f.relativePath)+",\"bytes\":"+std::to_string(f.byteCount)+",\"sha256\":"+Quote(f.sha256)+"}";
 std::string inventory=Ref(cameras,"sparse/cameras.bin")+","+Ref(images,"sparse/images.bin");if(preparedMode=="duplicate")inventory+=","+Ref(cameras,"sparse/cameras.bin");if(preparedMode=="missing-camera")inventory=Ref(images,"sparse/images.bin");std::string prepared=preparedMode=="absent"?"":",\"prepared\":{\"schemaVersion\":"+std::string(preparedMode=="schema"?"2":"1")+",\"datasetPath\":\"C:/ignored-untrusted-fixture-path\",\"datasetInventory\":["+inventory+"]}";
 std::string base="\"schemaVersion\":1,\"projectId\":"+Quote(input.projectId)+",\"executionId\":"+Quote(id)+",\"generation\":1,\"configSha256\":"+Quote(std::string(64,'a'))+",\"input\":"+in;
 std::string candidate="{"+base+",\"outcome\":\"Completed\",\"output\":{\"relativePath\":\"attempt-1/output/model.ply\",\"bytes\":"+std::to_string(fs::file_size(output))+",\"sha256\":"+Quote(Hash(output))+"}"+prepared+"}";if(!candidateOverride.empty())candidate=candidateOverride;Write(attempt/L"result-candidate.json",candidate);
 Write(attempt/L"host-result.json","{\"schemaVersion\":1,\"launched\":true,\"exited\":true,\"treeRetired\":true,\"exitCode\":0,\"win32Error\":0,\"timedOut\":false,\"cancelled\":false}");
 auto hash=Hash(attempt/L"result-candidate.json");Write(attempt/L"native-seal.json","{\"schemaVersion\":1,\"treeRetired\":true,\"state\":\"Completed\",\"candidateSha256\":"+Quote(hash)+",\"hostResultSha256\":"+Quote(Hash(attempt/L"host-result.json"))+"}");std::string pointer="{"+base+",\"state\":\"Completed\",\"message\":\"synthetic test seal, not actual reconstruction\",\"candidatePath\":\"attempt-1/result-candidate.json\",\"candidateSha256\":"+Quote(hash)+"}";if(!pointerOverride.empty())pointer=pointerOverride;Write(execution/L"native-state-1.json",pointer);Write(q->Directory()/L"executions"/L"latest.json",pointer);}
 ExecutionSnapshot Inspect(){ProjectReconstruction service(*q,{});return service.Inspect();}
};
int wmain(int argc,wchar_t**argv){if(argc!=2)return 2;fs::path root=argv[1];if(fs::exists(root))return 3;fs::create_directories(root);int passed=0,failed=0;auto test=[&](const char*name,auto body){try{Fixture f(root/fs::u8path(name));body(f);++passed;std::cout<<"PASS "<<name<<std::endl;}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<std::endl;}};
 test("validated_centered_camera_generation_hash_and_world_basis",[](Fixture&f){f.Seal();auto s=f.Inspect();Need(s.state==ExecutionState::Completed&&s.initialView.has_value(),"valid camera rejected");Need(s.resultGeneration==1&&s.outputSha256==Hash(f.output),"result lineage not exposed");auto v=*s.initialView;Need(v.eye[2]==-2&&v.forward[2]==1&&v.up[1]==-1,"COLMAP world/basis transformed incorrectly");Need(v.imageName=="frame.png"&&v.camerasSha256==Hash(f.cameras)&&v.imagesSha256==Hash(f.images),"camera identity lost");});
 test("legacy_missing_prepared_uses_explicit_optional_fallback",[](Fixture&f){f.preparedMode="absent";f.Seal();auto s=f.Inspect();Need(s.state==ExecutionState::Completed&&!s.initialView,"legacy fallback rejected");});
 test("simple_pinhole_supported",[](Fixture&f){f.cameraBytes=Cameras(0);f.Seal();Need(f.Inspect().initialView.has_value(),"simple pinhole rejected");});
 auto reject=[&](const char*name,std::function<void(Fixture&)>before,std::function<void(Fixture&)>after={}){test(name,[&](Fixture&f){before(f);f.Seal();if(after)after(f);bool rejected=false;try{f.Inspect();}catch(const std::exception&e){Need(std::string(e.what()).find("Win32")==std::string::npos,"fixture path failed before semantic assertion");rejected=true;}Need(rejected,"unsafe or malformed metadata accepted");});};
 reject("unsupported_distorted_camera",[](Fixture&f){f.cameraBytes=Cameras(2);});
 reject("nonfinite_intrinsics",[](Fixture&f){f.cameraBytes=Cameras(1,std::numeric_limits<double>::quiet_NaN());});
 reject("offcenter_intrinsics",[](Fixture&f){f.cameraBytes=Cameras(1,500,100);});
 reject("camera_count_zero",[](Fixture&f){f.cameraBytes=Cameras(1,500,320,0);});
 reject("camera_duplicate_id",[](Fixture&f){f.cameraBytes=Cameras(1,500,320,2);});
 reject("camera_trailing_bytes",[](Fixture&f){f.cameraBytes=Cameras(1,500,320,1,true);});
 reject("camera_truncated_bytes",[](Fixture&f){f.cameraBytes.resize(16);});
 reject("image_nonunit_quaternion",[](Fixture&f){f.imageBytes=Images(.2);});
 reject("image_nonfinite_quaternion",[](Fixture&f){f.imageBytes=Images(std::numeric_limits<double>::infinity());});
 reject("image_foreign_camera",[](Fixture&f){f.imageBytes=Images(1,2);});
 reject("image_unsafe_name",[](Fixture&f){f.imageBytes=Images(1,1,"../foreign.png");});
 reject("image_duplicate_id",[](Fixture&f){f.imageBytes=Images(1,1,"frame.png",2);});
 reject("image_truncated_bytes",[](Fixture&f){f.imageBytes.resize(15);});
 reject("prepared_schema_unsupported",[](Fixture&f){f.preparedMode="schema";});
 reject("prepared_duplicate_inventory",[](Fixture&f){f.preparedMode="duplicate";});
 reject("prepared_missing_camera_inventory",[](Fixture&f){f.preparedMode="missing-camera";});
 reject("camera_hash_tamper_rejected",[](Fixture&){},[](Fixture&f){auto b=f.cameraBytes;b.back()^=1;Write(f.cameras,b);});
 reject("output_hash_tamper_rejected",[](Fixture&){},[](Fixture&f){auto b=Read(f.output);b.back()^=1;Write(f.output,b);});
 reject("candidate_hash_tamper_rejected",[](Fixture&){},[](Fixture&f){Write(f.attempt/L"result-candidate.json","{}");});
 reject("stale_pointer_generation_rejected",[](Fixture&){},[](Fixture&f){auto path=f.q->Directory()/L"executions"/L"latest.json";std::ifstream in(path);std::string b((std::istreambuf_iterator<char>(in)),{});auto at=b.find("\"generation\":1");b.replace(at,14,"\"generation\":2");Write(path,b);});
 test("paused_resume_fixture",[](Fixture&f){wchar_t configs[32768]{};Need(GetEnvironmentVariableW(L"MONSTER_RECONSTRUCTION_FIXTURE_CONFIGS",configs,32768)>0,"fixture configurations required");auto config=fs::path(configs)/L"prepare.json";f.Seal();auto candidate=Read(f.attempt/L"result-candidate.json");Replace(candidate,"Completed","Paused");Replace(candidate,std::string(64,'a'),Hash(config));Replace(candidate,"\"output\":","\"checkpointId\":\"synthetic-control-fixture\",\"resumeContract\":");Write(f.attempt/L"result-candidate.json",candidate);auto hash=Hash(f.attempt/L"result-candidate.json");auto seal=Read(f.attempt/L"native-seal.json");auto oldHash=Hash(f.q->Directory()/L"input.mstudio");(void)oldHash;auto pointer=Read(f.q->Directory()/L"executions"/L"latest.json");auto oldCandidateHash=Hash(f.attempt/L"result-candidate.json");(void)oldCandidateHash;
 auto oldSeal=Read(f.attempt/L"native-seal.json");auto hashAt=oldSeal.find("\"candidateSha256\":\"")+19;auto originalHash=oldSeal.substr(hashAt,64);Replace(seal,originalHash,hash);Replace(seal,"Completed","Paused");Write(f.attempt/L"native-seal.json",seal);Replace(pointer,originalHash,hash);Replace(pointer,"Completed","Paused");Replace(pointer,std::string(64,'a'),Hash(config));Write(f.execution/L"native-state-1.json",pointer);Write(f.q->Directory()/L"executions"/L"latest.json",pointer);auto s=f.Inspect();Need(s.state==ExecutionState::Paused&&s.resumeAvailable,"synthetic paused fixture not admitted");});
 test("ui_valid_fixture",[](Fixture&f){wchar_t source[32768]{};Need(GetEnvironmentVariableW(L"MONSTER_STUDIO_PREVIEW_ANALYTIC",source,32768)>0,"analytic source fixture required");Write(f.output,Read(source));f.Seal();auto s=f.Inspect();Need(s.state==ExecutionState::Completed&&s.initialView&&s.outputSha256==Hash(f.output),"valid UI synthetic fixture failed");});
 test("prepared_file_missing_does_not_use_legacy_fallback",[](Fixture&f){f.Seal();fs::rename(f.cameras,f.cameras.parent_path()/L"missing-held.bin");bool rejected=false;try{f.Inspect();}catch(const std::exception&){rejected=true;}Need(rejected,"present prepared inventory silently fell back after missing camera");});
 std::cout<<"SUMMARY "<<passed<<" passed "<<failed<<" failed\n";return failed?1:0;
}




