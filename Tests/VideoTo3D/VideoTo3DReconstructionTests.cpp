#include "VideoTo3D/ProjectReconstruction.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <Windows.h>
#include <chrono>
using namespace MonsterEngine::VideoTo3D;
namespace fs=std::filesystem;
void Require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int wmain(int argc,wchar_t**argv){
 if(argc!=2)return 2; const fs::path root=argv[1];
 if(fs::exists(root))return 3; fs::create_directories(root);
 int passed=0,failed=0;
 auto test=[&](const char*n,auto fn){try{fn();++passed;std::cout<<"PASS "<<n<<"\n";}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<n<<": "<<e.what()<<"\n";}};
 test("inactive_controls_reject",[&]{auto q=ReconstructionQueue::Create(root/L"inactive","test");ProjectReconstruction s(*q,{});Require(!s.RequestPause()&&!s.RequestForceStop()&&!s.RequestPauseAtIteration(101),"inactive control accepted");});
 test("no_input_rejected_without_execution",[&]{auto q=ReconstructionQueue::Create(root/L"noinput","test");ProjectReconstruction s(*q,{});auto r=s.RunNew();Require(r.state==ExecutionState::Failed&&!r.active&&!fs::exists(q->Directory()/L"executions"),"no-input executed");});
 test("actual_preparation_timestamp_consumed_once_on_admission_failure",[&]{auto q=ReconstructionQueue::Create(root/L"preparation-time","test");ProjectReconstruction s(*q,{});const std::string captured="2026-10-24T01:02:03.004Z";s.SetPreparationTiming({1234,captured});auto first=s.RunNew();Require(first.state==ExecutionState::Failed&&first.timing.startedUtc==captured&&first.timing.startedBoundary=="preparation_start_v1"&&first.timing.activeMilliseconds>=1234,"actual preparation boundary lost on failed admission");auto next=s.RunNew();Require(next.timing.startedUtc!=captured&&next.timing.startedBoundary=="execution_start_v1"&&next.timing.activeMilliseconds<1234,"preparation timing leaked into later execution");s.SetPreparationTiming({1234,captured});s.SetPreparationActiveMilliseconds(2345);auto durationOnly=s.RunNew();Require(durationOnly.timing.startedUtc!=captured&&durationOnly.timing.startedBoundary=="execution_start_v1"&&durationOnly.timing.activeMilliseconds>=2345,"duration-only setter retained stale UTC");});
 test("preparation_timestamp_rejects_invalid_calendar_zone_and_overflow",[&]{auto q=ReconstructionQueue::Create(root/L"invalid-preparation-time","test");ProjectReconstruction s(*q,{});for(const auto&value:{"","invalid","2026-02-30T01:02:03.004Z","2026-10-24T01:02:03.004","2026-10-24T01:02:03.004+08:00"}){bool rejected=false;try{s.SetPreparationTiming({0,value});}catch(...){rejected=true;}Require(rejected,"invalid preparation UTC accepted");}bool rejected=false;try{s.SetPreparationTiming({~std::uint64_t{0},"2026-10-24T01:02:03.004Z"});}catch(...){rejected=true;}Require(rejected,"overflow prefix accepted");});
 test("queue_physical_path_inspect_with_owned_unicode_input",[&]{auto p=root/L"owned";auto q=ReconstructionQueue::Create(p,"test");auto src=root/L"LEGO_\u539f\u59cb\u89c6\u9891.mov";{std::ofstream f(src,std::ios::binary);f<<"owned bytes";}q->ImportVideo(src);auto before=q->InspectInput();ProjectReconstruction s(*q,{});auto r=s.Inspect();Require(r.state==ExecutionState::None&&!r.active,"fresh owned project must inspect None through physical lease path");Require(q->InspectInput().inputId==before.inputId,"input changed");});

 wchar_t configsValue[32768]{};
 Require(GetEnvironmentVariableW(L"MONSTER_RECONSTRUCTION_FIXTURE_CONFIGS",configsValue,32768)>0,"local fixture config directory required");
 const fs::path configs=configsValue;
 auto read=[](const fs::path&p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};
 auto scenario=[&](const std::string&mode){
   auto dir=root/fs::path(mode);fs::create_directory(dir);
   auto q=ReconstructionQueue::Create(dir/L"project","native isolated reconstruction");
   auto source=dir/L"\u539f\u89c6\u9891.mov";{std::ofstream f(source);f<<"isolated native process fixture";}
   const auto in=q->ImportVideo(source);const auto projectBefore=read(q->Directory()/L"project.mstudio"),inputBefore=read(q->Directory()/L"input.mstudio");
   ProjectReconstruction service(*q,configs/(mode+".json"));service.SetPreparationTiming({321,"2026-10-24T01:02:03.004Z"});bool ready=false,pending=false,failedSave=false,untrusted=false,force=false;
   auto result=service.RunNew([&](const ExecutionSnapshot&o){
     if(o.stage=="helper-live"&&mode=="prepare"&&!force){Require(!service.RequestPause(),"pause available during preparation");force=service.RequestForceStop();Require(force,"force prep rejected");Require(!service.RequestForceStop(),"duplicate force accepted");}
     if(o.pauseAvailable&&!ready&&(mode=="savefailed"||mode=="wrong")){
       ready=true;bool rejected=false;try{service.RunNew();}catch(...){rejected=true;}Require(rejected,"duplicate Start accepted");
       rejected=false;try{service.Resume();}catch(...){rejected=true;}Require(rejected,"duplicate Resume accepted");
       Require(service.RequestPause(),"ready pause rejected");pending=true;Require(!service.RequestPause(),"duplicate pause accepted");Require(!service.RequestPauseAtIteration(101),"conflicting scheduled pause accepted");
     }
     if(mode=="savefailed"&&pending&&o.state==ExecutionState::Training&&!o.controlPending&&o.message.find("deliberate save failure")!=std::string::npos){Require(o.active&&o.pauseAvailable,"save failure discarded active controller");failedSave=true;force=service.RequestForceStop();Require(force,"force after save failure rejected");}
     if(o.stage=="control-error"&&!force){Require(o.active&&!o.pauseAvailable&&!o.controlPending,"untrusted event destroyed controller");untrusted=true;force=service.RequestForceStop();Require(force,"force after protocol error rejected");}
   });
   Require(read(q->Directory()/L"project.mstudio")==projectBefore&&read(q->Directory()/L"input.mstudio")==inputBefore,"failure/stop changed protected manifests");
   Require(read(source)=="isolated native process fixture","source changed");Require(q->InspectInput().inputId==in.inputId,"owned input changed");
   Require(!result.active&&result.state!=ExecutionState::Paused&&result.state!=ExecutionState::Completed,"false terminal success");
   Require(result.timing.startedUtc=="2026-10-24T01:02:03.004Z"&&result.timing.startedBoundary=="preparation_start_v1"&&result.timing.activeMilliseconds>=321,"process fixture lost actual preparation UTC/prefix");
   if(mode=="savefailed")Require(ready&&pending&&failedSave&&result.state==ExecutionState::Stopped,"save failure path missing");
   if(mode=="wrong"||mode=="stale")Require(untrusted&&result.state==ExecutionState::Stopped,"protocol error path missing");
   if(mode=="timeout")Require(result.state==ExecutionState::Failed&&result.message.find("deadline")!=std::string::npos,"timeout not reported");
   if(mode=="toolmismatch")Require(result.state==ExecutionState::Failed&&result.directory.empty(),"mismatched tool launched");
   if(!result.directory.empty()){
     auto persisted=service.ListExecutions();Require(persisted.size()==1&&persisted.front().timing.startedUtc=="2026-10-24T01:02:03.004Z"&&persisted.front().timing.startedBoundary=="preparation_start_v1","persisted timing changed actual supplied UTC or provenance");
     auto host=read(result.directory/L"attempt-1"/L"host-result.json");Require(host.find("\"treeRetired\":true")!=std::string::npos,"tree retirement missing");
     auto pidText=read(result.directory/L"attempt-1"/L"descendant.pid");if(!pidText.empty()){auto h=OpenProcess(SYNCHRONIZE,FALSE,std::stoul(pidText));if(h){Require(WaitForSingleObject(h,0)==WAIT_OBJECT_0,"descendant still active");CloseHandle(h);}}
   }
 };
 for(const auto& mode:{"prepare","savefailed","wrong","stale","nonzero","timeout","toolmismatch"})test(mode,[&]{scenario(mode);});
 test("tampered_owned_input_preserves_manifests",[&]{auto dir=root/L"tampered";fs::create_directory(dir);auto q=ReconstructionQueue::Create(dir/L"project","tampered");auto src=dir/L"source.mov";{std::ofstream f(src);f<<"original";}auto in=q->ImportVideo(src);auto owned=q->Directory()/fs::u8path(in.files[0].relativePath);{std::ofstream f(owned);f<<"changed";}auto a=read(q->Directory()/L"project.mstudio"),b=read(q->Directory()/L"input.mstudio");ProjectReconstruction svc(*q,configs/L"nonzero.json");auto result=svc.RunNew();Require(result.state==ExecutionState::Failed&&result.directory.empty(),"tampered input launched");Require(read(q->Directory()/L"project.mstudio")==a&&read(q->Directory()/L"input.mstudio")==b,"tamper failure rewrote manifests");});
 std::cout<<"SUMMARY "<<passed<<" passed "<<failed<<" failed\n";return failed?1:0;
}
