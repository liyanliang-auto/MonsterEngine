#include "StudioWindow.h"
#include "resource.h"
#include <Windows.h>
#include <objbase.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace MonsterEngine::VideoTo3D;namespace fs=std::filesystem;
void Need(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);}}}
void Until(const std::function<bool()>&f,DWORD ms=30000){auto end=GetTickCount64()+ms;while(!f()){Pump();Need(GetTickCount64()<end,"native reconstruction message-loop timeout");MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}Pump();}
std::string Read(const fs::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
fs::path Env(const wchar_t*n){wchar_t v[32768]{};Need(GetEnvironmentVariableW(n,v,32768)>0,"local test environment required");return v;}
struct StudioWindowTestAccess{
 using Op=StudioWindow::Operation;
 static void Send(StudioWindow&w,Op op,const fs::path&p={}){w.Enqueue({op,++w.generation_,p,"reconstruction fixture"});}
 static bool Busy(StudioWindow&w){return w.busy_;}
 static auto Execution(StudioWindow&w){return w.execution_;}
 static bool Closing(StudioWindow&w){return w.closing_;}
 static auto Service(StudioWindow&w){return w.Service();}
 static bool Done(StudioWindow&w){std::lock_guard l(w.mutex_);return w.workerDone_;}
 static void Config(StudioWindow&w,const fs::path&p){w.configPath_=p;}
 static auto Generation(StudioWindow&w){return w.generation_;}
 static size_t Pending(StudioWindow&w){std::lock_guard l(w.mutex_);return w.completions_.size();}
};using A=StudioWindowTestAccess;
struct Window{
 StudioWindow w{GetModuleHandleW(nullptr)};HWND h{};
 Window(){Need(w.Create(),"create native Studio");h=w.Handle();}
 void Wait(){Until([&]{return !A::Busy(w);});}
 void Open(const fs::path&p){A::Send(w,A::Op::Open,p);Wait();}
 void Close(){if(IsWindow(h)){SendMessageW(h,WM_CLOSE,0,0);Until([&]{return !IsWindow(h);},180000);}Need(A::Done(w),"HWND destroyed before worker retirement");}
 ~Window(){if(IsWindow(h)){SendMessageW(h,WM_COMMAND,MAKEWPARAM(IDC_FORCE_STOP,BN_CLICKED),0);SendMessageW(h,WM_CLOSE,0,0);for(int i=0;i<2000&&IsWindow(h);++i){Pump();Sleep(5);}}}
};
void Click(Window&v,WORD id){SendMessageW(v.h,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);}
int wmain(int argc,wchar_t**argv){
 if(argc!=2)return 2;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);fs::path root=argv[1];if(fs::exists(root))return 3;fs::create_directories(root);
 int passed=0,failed=0;auto test=[&](const char*n,auto fn){try{fn();++passed;std::cout<<"PASS "<<n<<std::endl;}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<n<<": "<<e.what()<<std::endl;}};
 test("native_compact760_control_rectangles",[&]{Window v;SetWindowPos(v.h,nullptr,0,0,1060,760,SWP_NOMOVE|SWP_NOZORDER);Pump();RECT c{};GetClientRect(v.h,&c);for(auto id:{IDC_CONFIG,IDC_START_RECONSTRUCTION,IDC_PAUSE_RECONSTRUCTION,IDC_RESUME_RECONSTRUCTION,IDC_FORCE_STOP,IDC_EXECUTION_INFO,IDC_INPUT_INFO,IDC_ADD,IDC_QUEUE,IDC_UP,IDC_DOWN,IDC_CANCEL,IDC_STATUS}){RECT r{};GetWindowRect(GetDlgItem(v.h,id),&r);MapWindowPoints(nullptr,v.h,reinterpret_cast<POINT*>(&r),2);Need(r.left>=0&&r.top>=0&&r.right<=c.right&&r.bottom<=c.bottom,"control outside 760 client rectangle");}RECT actions{},footer{};GetWindowRect(GetDlgItem(v.h,IDC_CANCEL),&actions);GetWindowRect(GetDlgItem(v.h,IDC_STATUS),&footer);Need(actions.bottom<=footer.top,"actions overlap status footer");v.Close();});
 const auto configs=Env(L"MONSTER_RECONSTRUCTION_FIXTURE_CONFIGS");
 auto seam=[&](const std::wstring&mode){auto d=root/mode;fs::create_directory(d);auto q=ReconstructionQueue::Create(d/L"project","native GUI fixture");auto src=d/L"source.mov";{std::ofstream f(src);f<<"protected GUI video";}q->ImportVideo(src);q->AddPlannedRun("plan-only text");q.reset();auto before=Read(d/L"project"/L"project.mstudio"),input=Read(d/L"project"/L"input.mstudio");Window v;v.Open(d/L"project");A::Config(v.w,configs/(mode+L".json"));Click(v,IDC_START_RECONSTRUCTION);auto generation=A::Generation(v.w);Click(v,IDC_START_RECONSTRUCTION);Click(v,IDC_RESUME_RECONSTRUCTION);Click(v,IDC_ADD);Need(A::Generation(v.w)==generation,"duplicate queued native commands changed generation");
 Until([&]{auto s=A::Service(v.w);return s&&s->Snapshot().active&&s->Snapshot().stage=="helper-live"&&(mode==L"prepare"||s->Snapshot().pauseAvailable);});
 if(mode==L"prepare"){SendMessageW(v.h,WM_CLOSE,0,0);Until([&]{return !IsWindow(v.h);});Need(A::Done(v.w),"preparation close did not join worker");}
 else {SendMessageW(v.h,WM_CLOSE,0,0);Need(IsWindow(v.h),"training close destroyed HWND immediately");Until([&]{auto s=A::Service(v.w);return s&&s->Snapshot().active&&!A::Closing(v.w)&&((mode==L"wrong"&&s->Snapshot().stage=="control-error")||(mode==L"savefailed"&&s->Snapshot().state==ExecutionState::Training&&!s->Snapshot().controlPending));});Need(IsWindow(v.h)&&IsWindowEnabled(GetDlgItem(v.h,IDC_FORCE_STOP)),"save failure did not keep active HWND and force control");auto s=A::Service(v.w);Need(s->Snapshot().active,"failed-save controller retired unexpectedly");Click(v,IDC_FORCE_STOP);Click(v,IDC_FORCE_STOP);v.Wait();Need(A::Execution(v.w).state==ExecutionState::Stopped,"explicit force not Stopped");v.Close();}
 Need(Read(d/L"project"/L"project.mstudio")==before&&Read(d/L"project"/L"input.mstudio")==input,"GUI lifecycle changed manifests");auto reopened=ReconstructionQueue::Open(d/L"project");Need(reopened->Snapshot().runs.size()==1,"plan changed");};
 test("native_duplicate_start_resume_mutation_and_close_preparation",[&]{seam(L"prepare");});
 test("native_close_savefailed_keeps_window_until_explicit_force",[&]{seam(L"savefailed");});
 test("native_close_wrong_ack_keeps_window_until_explicit_force",[&]{seam(L"wrong");});
 test("native_pending_result_close_joins_before_release",[&]{auto d=root/L"pending";fs::create_directory(d);auto q=ReconstructionQueue::Create(d/L"project","pending");auto src=d/L"source.mov";{std::ofstream f(src);f<<"pending input";}q->ImportVideo(src);q.reset();Window v;v.Open(d/L"project");A::Config(v.w,configs/L"nonzero.json");Click(v,IDC_START_RECONSTRUCTION);auto deadline=GetTickCount64()+10000;while(A::Pending(v.w)==0&&GetTickCount64()<deadline)Sleep(5);Need(A::Pending(v.w)>0,"no pending completion");SendMessageW(v.h,WM_CLOSE,0,0);Until([&]{return !IsWindow(v.h);});Need(A::Done(v.w),"pending result close detached worker");auto reopened=ReconstructionQueue::Open(d/L"project");});
 test("real_Brush_restore_training_close_safepause_reopen",[&]{const auto p=Env(L"MONSTER_REAL_RECONSTRUCTION_PROJECT"),config=Env(L"MONSTER_REAL_RECONSTRUCTION_CONFIG");const auto project=Read(p/L"project.mstudio"),input=Read(p/L"input.mstudio");{Window v;v.Open(p);Need(A::Execution(v.w).state==ExecutionState::Paused&&A::Execution(v.w).resumeAvailable,"real fixture must reopen validated paused");A::Config(v.w,config);Click(v,IDC_RESUME_RECONSTRUCTION);const auto generation=A::Generation(v.w);Click(v,IDC_RESUME_RECONSTRUCTION);Click(v,IDC_START_RECONSTRUCTION);Need(A::Generation(v.w)==generation,"duplicate resume changed generation");Until([&]{auto s=A::Service(v.w);return s&&s->Snapshot().active&&s->Snapshot().state==ExecutionState::Training&&s->Snapshot().pauseAvailable;},180000);SendMessageW(v.h,WM_CLOSE,0,0);Need(IsWindow(v.h),"real training HWND destroyed before safe save");Until([&]{return !IsWindow(v.h);},180000);Need(A::Done(v.w),"real close worker not retired");}Need(Read(p/L"project.mstudio")==project&&Read(p/L"input.mstudio")==input,"real training close changed manifests");{Window reopened;reopened.Open(p);Need(A::Execution(reopened.w).state==ExecutionState::Paused&&A::Execution(reopened.w).resumeAvailable,"safe close not persistently resumable");reopened.Close();}});
 std::cout<<"SUMMARY "<<passed<<" passed "<<failed<<" failed\n";CoUninitialize();return failed?1:0;
}
