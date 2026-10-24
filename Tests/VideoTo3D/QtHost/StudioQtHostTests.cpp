#include "HostValidationWindow.h"
#include "VideoTo3D/ReconstructionQueue.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QTest>
#include <QWheelEvent>
#include <fstream>
#include <cstdio>
#include <atomic>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace MonsterStudioQt;
using namespace MonsterEngine::VideoTo3D;
namespace fs = std::filesystem;
namespace MonsterStudioQt {
// One target-wide test guard covers the shell, viewport, AUTOMOC and this access
// seam. Production targets contain neither this friend nor the worker gate.
struct HostValidationWindowTestAccess {
 static void Gate(HostValidationWindow& w,std::function<void()> callback){std::lock_guard lock(w.mutex_);w.afterInspectionLeaseAcquiredForTest_=std::move(callback);}
 static uint64_t Heartbeat(const HostValidationWindow&w){return w.heartbeat_;}
 static bool Closing(const HostValidationWindow&w){return w.closing_;}
 static bool WorkerDone(HostValidationWindow&w){std::lock_guard lock(w.mutex_);return w.workerDone_;}
 static bool WorkerJoined(const HostValidationWindow&w){return !w.worker_.joinable();}
 static uint64_t Requests(const HostValidationWindow&w){return w.requestId_;}
 static std::optional<StudioPreviewRequest> CompletedInspection(HostValidationWindow&w){std::lock_guard lock(w.mutex_);for(const auto&c:w.completions_)if(c.operation==HostValidationWindow::Operation::Open&&c.error.empty())return c.request;return {};}
 static StudioPreviewStatus Owner(const NativePreviewWidget&v){return v.owner_?v.owner_->Snapshot():StudioPreviewStatus{};}
};
}
using Access=MonsterStudioQt::HostValidationWindowTestAccess;
struct InspectionGate {
 std::mutex mutex;std::condition_variable wake;std::atomic<bool> entered{false};bool released=false;
 void Wait(){std::unique_lock lock(mutex);entered=true;wake.notify_all();if(!wake.wait_for(lock,std::chrono::seconds(30),[&]{return released;}))throw std::runtime_error("Inspection test gate timed out");}
 void Release(){std::lock_guard lock(mutex);released=true;wake.notify_all();}
};
struct ReleaseGateOnExit {std::shared_ptr<InspectionGate> gate;~ReleaseGateOnExit(){gate->Release();}};
static fs::path root, project;
static int pass=0, fail=0;
static QJsonArray outcomes;
static void Need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static QString Text(const fs::path&p){return QString::fromStdWString(p.wstring());}
static std::string Read(const fs::path&p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static void Until(const std::function<bool()>& f,int timeout=30000){QElapsedTimer t;t.start();while(!f()){Need(t.elapsed()<timeout,"Timed out awaiting observable state");QTest::qWait(10);}}
static void WriteJSON(const fs::path&p,const QJsonObject&o){QFile f(Text(p));Need(f.open(QIODevice::WriteOnly),"Open evidence file");Need(f.write(QJsonDocument(o).toJson())>0,"Write evidence file");}
static QJsonObject State(NativePreviewWidget&v){auto s=v.Snapshot();RECT r{};GetClientRect(reinterpret_cast<HWND>(v.winId()),&r);return {{"state",int(s.state)},{"requestId",QString::number(s.identity.requestId)},{"executionId",QString::fromStdString(s.identity.executionId)},{"contentHash",QString::fromStdString(s.identity.contentHash)},{"frames",QString::number(s.submittedFrames)},{"device",QString::fromStdString(s.deviceName)},{"deviceCreations",QString::number(s.deviceCreations)},{"deviceDestructions",QString::number(s.deviceDestructions)},{"sessionShutdowns",QString::number(s.sessionShutdowns)},{"reservedBytesAfterRetire",QString::number(s.reservedBytesAfterRetire)},{"hwnd",QString::number(s.childWindow)},{"physicalWidth",int(s.width)},{"physicalHeight",int(s.height)},{"clientWidth",int(r.right)},{"clientHeight",int(r.bottom)},{"logicalWidth",v.width()},{"logicalHeight",v.height()},{"devicePixelRatio",v.devicePixelRatioF()},{"actualDpi",int(GetDpiForWindow(reinterpret_cast<HWND>(v.winId())))},{"error",QString::fromStdString(s.error)}};}
struct Window{
 HostValidationWindow w;
 Window(){w.show();Until([&]{return w.findChild<QPushButton*>("loadProject")->isEnabled();});}
 NativePreviewWidget& View(){return *w.findChild<NativePreviewWidget*>("nativePreview");}
 void Open(const fs::path&p){auto*line=w.findChild<QLineEdit*>("projectDirectory");line->setText(Text(p));QTest::mouseClick(w.findChild<QPushButton*>("loadProject"),Qt::LeftButton);}
 void Ready(){Until([&]{auto s=View().Snapshot();Need(s.state!=StudioPreviewState::Failed,s.error);return s.state==StudioPreviewState::Ready&&s.submittedFrames>3;},60000);}
 void Idle(){Until([&]{return w.findChild<QPushButton*>("loadProject")->isEnabled();});}
 void Close(){if(!w.isVisible())return;w.close();Until([&]{return !w.isVisible();});Need(View().IsStopped(),"Window closed before owner acknowledgement");auto s=View().Snapshot();Need(s.deviceCreations==s.deviceDestructions&&s.reservedBytesAfterRetire==0,"Close leaked Vulkan owner/account");}
 ~Window(){Close();}
};
static void Test(const char*name,const std::function<void()>&f){try{f();++pass;outcomes.append(QJsonObject{{"name",name},{"passed",true}});std::cout<<"PASS "<<name<<std::endl;}catch(const std::exception&e){++fail;outcomes.append(QJsonObject{{"name",name},{"passed",false},{"error",e.what()}});std::cout<<"FAIL "<<name<<": "<<e.what()<<std::endl;}}
static QImage Capture(NativePreviewWidget&v,const char*name,uint64_t id){auto png=root/(std::string(name)+".png"),meta=root/(std::string(name)+".json");Need(v.RequestCapture(id,png,meta),"Owner capture rejected");Until([&]{auto s=v.Snapshot();return s.captureId==id&&!s.capturePending;});Need(v.Snapshot().captureSucceeded,v.Snapshot().captureError);QImage image(Text(png));Need(!image.isNull(),"Owner PNG undecodable");return image.convertToFormat(QImage::Format_RGBA8888);}
static double Difference(const QImage&a,const QImage&b){Need(a.size()==b.size(),"Image extents changed");double total=0;for(int y=0;y<a.height();++y){auto*x=a.constScanLine(y);auto*z=b.constScanLine(y);for(int i=0;i<a.width()*4;++i)total+=std::abs(int(x[i])-int(z[i]));}return total/(a.width()*a.height()*4);}
int main(int argc,char**argv){QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString& message){auto bytes=message.toUtf8();std::fprintf(stderr,"%s\n",bytes.constData());std::fflush(stderr);});
 if(argc==2&&std::string(argv[1])=="--guard-reparent"){QWidget a,b;NativePreviewWidget v(&a);a.show();v.show();Need(v.StartOwner(),"Owner start");v.setParent(&b);return 90;}
 if(argc!=3)return 2;root=fs::path(QString::fromLocal8Bit(argv[1]).toStdWString());project=fs::path(QString::fromLocal8Bit(argv[2]).toStdWString());Need(!fs::exists(root),"Use fresh evidence runtime");fs::create_directories(root);
 Test("inspection_preserves_active_planning_and_continuous_lease",[]{auto p=root/"planning";auto q=ReconstructionQueue::Create(p,"inspection fixture");q->AddPlannedRun("immutable request");auto snap=q->Snapshot();auto r=snap.runs[0];q->ApplyEvent({snap.projectId,r.runId,r.stageId,r.generation,1,RunEventKind::Started});auto bytes=Read(p/"project.mstudio");q.reset();q=ReconstructionQueue::OpenForInspection(p);Need(Read(p/"project.mstudio")==bytes,"Inspection changed active plan bytes");Need(q->Snapshot().runs[0].state==RunState::Running,"Inspection recovered active plan");bool excluded=false;try{auto other=ReconstructionQueue::Open(p);}catch(const ProjectError&e){excluded=e.Code()==ProjectErrorCode::OwnershipConflict;}Need(excluded,"Inspection released continuous lease");Need(Read(p/"project.mstudio")==bytes,"Competing open changed plan");q.reset();q=ReconstructionQueue::Open(p);Need(q->Snapshot().runs[0].state==RunState::Interrupted,"Existing Open recovery regressed");});
 Test("idle_close_before_owner_start",[]{HostValidationWindow w;w.show();w.close();Until([&]{return !w.isVisible();});Need(w.findChild<NativePreviewWidget*>()->IsStopped(),"Idle close left owner");});
 Test("invalid_missing_raw_ply_and_locked_projects_visible_failure",[]{Window w;for(const auto&p:{root/"missing",project/"executions"/"latest.json"}){w.Open(p);w.Idle();Need(w.View().Snapshot().state!=StudioPreviewState::Ready,"Invalid path fake Ready");Need(w.w.findChild<QLabel*>("hostStatus")->text().startsWith(QString::fromUtf8("失败")),"Failure not visible");}auto lock=ReconstructionQueue::OpenForInspection(project);w.Open(project);w.Idle();Need(w.View().Snapshot().state!=StudioPreviewState::Ready,"Locked project Ready");Need(w.w.findChild<QLabel*>("hostStatus")->text().startsWith(QString::fromUtf8("失败")),"Lease error invisible");});
 Test("active_plan_unverified_result_rejected_without_mutation",[]{auto p=root/"unverified";auto q=ReconstructionQueue::Create(p,"unverified plan");q->AddPlannedRun("immutable request");auto s=q->Snapshot();auto r=s.runs[0];q->ApplyEvent({s.projectId,r.runId,r.stageId,r.generation,1,RunEventKind::Started});auto before=Read(p/"project.mstudio");q.reset();Window w;w.Open(p);w.Idle();Need(w.View().Snapshot().state!=StudioPreviewState::Ready,"Unverified result Ready");Need(Read(p/"project.mstudio")==before,"Host inspection recovered active record");});
 Test("real_validated_scene_input_resize_hide_restore_retire_reload",[]{auto latest=Read(project/"executions"/"latest.json"),plan=Read(project/"project.mstudio"),input=Read(project/"input.mstudio");Window w;w.Open(project);w.Ready();auto&v=w.View();auto initial=v.Snapshot();Need(initial.deviceCreations==1&&!initial.deviceName.empty()&&!initial.identity.contentHash.empty(),"No real identified GPU");WriteJSON(root/"initial-status.json",State(v));RECT bounds{};GetClientRect(reinterpret_cast<HWND>(v.winId()),&bounds);Need(initial.width==unsigned(bounds.right)&&initial.height==unsigned(bounds.bottom),"Logical size used for native extent");auto base=Capture(v,"initial",1);auto center=v.rect().center();QTest::mousePress(&v,Qt::LeftButton,Qt::NoModifier,center);QMouseEvent move(QEvent::MouseMove,QPointF(center+QPoint(130,45)),QPointF(v.mapToGlobal(center+QPoint(130,45))),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&v,&move);QTest::mouseRelease(&v,Qt::LeftButton,Qt::NoModifier,center+QPoint(130,45));QTest::qWait(200);auto orbit=Capture(v,"orbit",2);Need(Difference(base,orbit)>.1,"Orbit did not change rendered scene");QWheelEvent wheel(QPointF(center),QPointF(v.mapToGlobal(center)),{},QPoint(0,240),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&v,&wheel);QTest::qWait(200);auto zoom=Capture(v,"zoom",3);Need(Difference(orbit,zoom)>.1,"Wheel did not change rendered scene");QTest::mouseClick(w.w.findChild<QPushButton*>("resetView"),Qt::LeftButton);QTest::qWait(200);auto reset=Capture(v,"reset",4);double resetDifference=Difference(base,reset);Need(resetDifference<.1,"Reset did not restore validated camera");WriteJSON(root/"input-pixels.json",{{"orbitMeanAbsoluteDifference",Difference(base,orbit)},{"zoomMeanAbsoluteDifference",Difference(orbit,zoom)},{"resetMeanAbsoluteDifference",resetDifference}});
 w.w.resize(970,690);Until([&]{RECT r{};GetClientRect(reinterpret_cast<HWND>(v.winId()),&r);auto s=v.Snapshot();return s.width==unsigned(r.right)&&s.height==unsigned(r.bottom)&&s.submittedFrames>initial.submittedFrames+5;});WriteJSON(root/"resized-status.json",State(v));w.w.showMinimized();QTest::qWait(250);auto minimized=v.Snapshot().submittedFrames;QTest::qWait(200);Need(v.Snapshot().submittedFrames<=minimized+1,"Minimized rendering did not stop");w.w.showNormal();Until([&]{return v.Snapshot().submittedFrames>minimized+2;});w.w.hide();QTest::qWait(200);auto hidden=v.Snapshot().submittedFrames;QTest::qWait(200);Need(v.Snapshot().submittedFrames<=hidden+1,"Hidden rendering did not stop");w.w.show();Until([&]{return v.Snapshot().submittedFrames>hidden+2;});WriteJSON(root/"restored-status.json",State(v));
 for(int i=0;i<3;++i){QTest::mouseClick(w.w.findChild<QPushButton*>("retirePreview"),Qt::LeftButton);w.Idle();auto s=v.Snapshot();Need(s.state==StudioPreviewState::Unloaded&&s.deviceCreations==s.deviceDestructions&&s.reservedBytesAfterRetire==0,"Retirement leaked account/device");WriteJSON(root/("retired-"+std::to_string(i)+".json"),State(v));{auto q=ReconstructionQueue::OpenForInspection(project);}w.Open(project);w.Ready();Need(v.Snapshot().identity.requestId>initial.identity.requestId,"Request identity reused");}
 w.Close();WriteJSON(root/"closed-status.json",State(v));Need(Read(project/"executions"/"latest.json")==latest&&Read(project/"project.mstudio")==plan&&Read(project/"input.mstudio")==input,"Viewing mutated project identities");});
 Test("close_before_open_dispatch_immediate_cancel",[]{for(int i=0;i<3;++i){Window w;w.Open(project);w.Close();QTest::qWait(100);Need(!w.w.isVisible(),"Immediate cancellation reopened window");auto q=ReconstructionQueue::OpenForInspection(project);}});
 Test("close_during_real_inspection_retains_hwnd_and_lease_until_late_completion",[]{
  Window w;
  auto gate=std::make_shared<InspectionGate>();
  ReleaseGateOnExit release{gate}; // Unblocks worker before Window cleanup, including assertion failures.
  Access::Gate(w.w,[gate]{gate->Wait();});
  w.Open(project);
  Until([&]{return gate->entered.load();});
  const auto hwnd=reinterpret_cast<HWND>(w.View().winId());
  bool excluded=false;try{auto other=ReconstructionQueue::OpenForInspection(project);}catch(const ProjectError&e){excluded=e.Code()==ProjectErrorCode::OwnershipConflict;}
  Need(excluded,"In-flight inspection did not retain acquired lease");
  const auto heartbeat=Access::Heartbeat(w.w);
  w.w.close();
  QTest::qWait(180);
  Need(Access::Closing(w.w)&&w.w.isVisible(),"Close acknowledged while inspection still gated");
  Need(IsWindow(hwnd)&&!Access::WorkerDone(w.w),"HWND or service worker retired before inspection release");
  Need(Access::Heartbeat(w.w)>heartbeat+2,"GUI event loop blocked while inspection held lease");
  Need(Access::Requests(w.w)==0,"Inspection gate unexpectedly issued preview request");
  WriteJSON(root/"inspection-close-gated.json",{{"gateEntered",true},{"competingLeaseRejected",excluded},{"closing",true},{"windowStillVisible",w.w.isVisible()},{"borrowedHwndStillValid",bool(IsWindow(hwnd))},{"workerStillActive",!Access::WorkerDone(w.w)},{"heartbeatBefore",QString::number(heartbeat)},{"heartbeatAfter",QString::number(Access::Heartbeat(w.w))},{"previewRequests",QString::number(Access::Requests(w.w))}});
  gate->Release();
  // Do not pump GUI while observing the real Inspect result queued by the
  // worker. This makes its late completion observable before Poll discards it.
  QElapsedTimer wait;wait.start();std::optional<StudioPreviewRequest> late;
  while(!(late=Access::CompletedInspection(w.w))){Need(wait.elapsed()<30000,"Real late inspection did not complete successfully");Sleep(1);}
  Need(fs::equivalent(late->identity.projectDirectory,project)&&!late->identity.executionId.empty()&&!late->identity.contentHash.empty()&&late->initialView.has_value(),"Late completion did not contain validated actual result/camera");
  Until([&]{return !w.w.isVisible();});
  Need(Access::WorkerDone(w.w)&&Access::WorkerJoined(w.w)&&w.View().IsStopped(),"Closed before worker join and owner stop");
  QTest::qWait(100);
  Need(!w.w.isVisible()&&Access::Requests(w.w)==0&&w.View().Snapshot().identity.requestId==0,"Late inspection reopened/loaded the closing window");
  auto stopped=Access::Owner(w.View());Need(stopped.state==StudioPreviewState::Stopped&&stopped.deviceCreations==stopped.deviceDestructions&&stopped.reservedBytesAfterRetire==0,"In-flight inspection close leaked owner/account");
  auto q=ReconstructionQueue::OpenForInspection(project);
  WriteJSON(root/"inspection-close-completed.json",{{"actualInspectionCompleted",true},{"executionId",QString::fromStdString(late->identity.executionId)},{"contentHash",QString::fromStdString(late->identity.contentHash)},{"validatedCamera",late->initialView.has_value()},{"lateCompletionIgnored",Access::Requests(w.w)==0},{"workerJoined",Access::WorkerJoined(w.w)},{"leaseReacquired",bool(q)},{"owner",State(w.View())}});
 });
 Test("close_during_observed_native_owner_loading_stops_before_hwnd_release",[]{
  Window w;bool stoppedSawValidHwnd=false;const auto hwnd=reinterpret_cast<HWND>(w.View().winId());
  QObject::connect(&w.View(),&NativePreviewWidget::stopped,&w.w,[&]{auto s=Access::Owner(w.View());stoppedSawValidHwnd=IsWindow(hwnd)&&s.state==StudioPreviewState::Stopped;});
  w.Open(project);StudioPreviewStatus loading;QElapsedTimer wait;wait.start();
  for(;;){loading=Access::Owner(w.View());auto expected=w.View().Snapshot().identity;
   if(loading.state==StudioPreviewState::Loading&&loading.identity.requestId>0&&loading.identity.requestId==expected.requestId&&loading.identity.executionId==expected.executionId&&loading.identity.contentHash==expected.contentHash&&fs::equivalent(loading.identity.projectDirectory,project))break;
   Need(loading.state!=StudioPreviewState::Failed,loading.error);Need(wait.elapsed()<30000,"Never observed actual native owner Loading with current identity");QCoreApplication::processEvents(QEventLoop::AllEvents,1);Sleep(1);
  }
  Need(!loading.identity.executionId.empty()&&!loading.identity.contentHash.empty(),"Unvalidated loading identity");
  WriteJSON(root/"native-loading-close-observed.json",{{"observation","actual StudioPreviewHost::Snapshot, not optimistic widget state"},{"state",int(loading.state)},{"requestId",QString::number(loading.identity.requestId)},{"executionId",QString::fromStdString(loading.identity.executionId)},{"contentHash",QString::fromStdString(loading.identity.contentHash)},{"borrowedHwndStillValid",bool(IsWindow(hwnd))},{"submittedFrames",QString::number(loading.submittedFrames)},{"deviceCreations",QString::number(loading.deviceCreations)}});
  w.w.close();Need(IsWindow(hwnd),"Close destroyed borrowed HWND before acknowledgement");
  Until([&]{return !w.w.isVisible();});
  const auto stopped=Access::Owner(w.View());
  Need(stoppedSawValidHwnd&&stopped.state==StudioPreviewState::Stopped,"Owner stop did not precede native surface release");
  Need(stopped.deviceCreations==stopped.deviceDestructions&&stopped.reservedBytesAfterRetire==0,"Native-loading close leaked device/account");
  Need(Access::WorkerDone(w.w)&&Access::WorkerJoined(w.w),"Native-loading close preceded worker join");
  QTest::qWait(100);Need(!w.w.isVisible()&&w.View().IsStopped(),"Late preparation reopened window");
  auto q=ReconstructionQueue::OpenForInspection(project);
  auto record=State(w.View());record.insert("ownerStopObservedWhileHwndValid",stoppedSawValidHwnd);record.insert("workerJoined",Access::WorkerJoined(w.w));record.insert("leaseReacquired",bool(q));WriteJSON(root/"native-loading-close-stopped.json",record);
 });
 Test("native_live_reparent_guard_fails_before_hwnd_replacement",[]{QProcess child;child.start(QCoreApplication::applicationFilePath(),{"--guard-reparent"});Need(child.waitForStarted(),"Guard child start");Need(child.waitForFinished(10000),"Guard child did not exit");Need(child.exitStatus()==QProcess::CrashExit||child.exitCode()!=90,"Live parent replacement was accepted");QFile log(Text(root/"guard-reparent.log"));Need(log.open(QIODevice::WriteOnly),"Guard log");auto error=child.readAllStandardError();log.write(error);Need(error.contains("Reparenting a live NativePreviewWidget is unsupported"),"Expected guard not observed");});
 WriteJSON(root/"test-results.json",{{"passed",pass},{"failed",fail},{"tests",outcomes},{"coverage","n/a"},{"dpiScope","Actual current system DPI only; remaining four-scale full-PRD checks pending"}});std::cout<<"SUMMARY "<<pass<<" passed "<<fail<<" failed"<<std::endl;return fail?1:0;}


