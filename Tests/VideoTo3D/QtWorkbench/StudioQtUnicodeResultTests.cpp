// Real controller/native owner; read-only existing Completed project, no mocks,
// Start/Resume/training or hardcoded private project path.
#include "WorkbenchWindow.h"
#include "WorkbenchController.h"
#include "NativePreviewWidget.h"
#include "VideoTo3D/ProjectReconstruction.h"
#include <QApplication>
#include <QTest>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QScreen>
#include <QPixmap>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QStackedWidget>
#include <ShlObj.h>
#include <filesystem>
#include <iostream>
using namespace MonsterStudioQt;using namespace MonsterEngine::VideoTo3D;namespace fs=std::filesystem;
static QString Text(const fs::path&p){return QString::fromStdWString(p.wstring());}
static void Need(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static QByteArray Read(const fs::path&p){QFile f(Text(p));Need(f.open(QIODevice::ReadOnly),"Read protected file");return f.readAll();}
static void Write(const fs::path&p,const QByteArray&b){fs::create_directories(p.parent_path());QFile f(Text(p));Need(f.open(QIODevice::WriteOnly)&&f.write(b)==b.size(),"Write evidence");}
static QString Hash(const fs::path&p){QFile f(Text(p));Need(f.open(QIODevice::ReadOnly),"Hash protected file");QCryptographicHash hash(QCryptographicHash::Sha256);Need(hash.addData(&f),"Hash input");return QString::fromLatin1(hash.result().toHex());}
static QJsonArray Inventory(const fs::path&p){std::vector<fs::path> paths;for(auto&e:fs::recursive_directory_iterator(p))if(e.is_regular_file())paths.push_back(e.path());std::sort(paths.begin(),paths.end());QJsonArray result;for(auto&f:paths)result.append(QJsonObject{{"path",Text(f.lexically_relative(p))},{"bytes",double(fs::file_size(f))},{"sha256",Hash(f)}});return result;}
static void Json(const fs::path&p,const QJsonObject&o){Write(p,QJsonDocument(o).toJson());}
static void Until(const std::function<bool()>&predicate,int ms=60000){QElapsedTimer t;t.start();while(!predicate()){Need(t.elapsed()<ms,"Real GPU/controller boundary timeout");QTest::qWait(10);}}
struct SettingsRestore{fs::path file;bool existed{};QByteArray bytes;SettingsRestore(const fs::path&out){PWSTR path=nullptr;Need(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path)),"LocalAppData");file=fs::path(path)/L"MonsterEngine/MonsterStudio/gui-settings-v1.json";CoTaskMemFree(path);existed=fs::exists(file);if(existed){bytes=Read(file);Write(out/L"gui-settings-before.bin",bytes);}}~SettingsRestore(){if(existed)Write(file,bytes);else{std::error_code ec;fs::remove(file,ec);}}};
int wmain(int argc,wchar_t**argv){if(argc!=3)return 2;char name[]="StudioQtUnicodeResultTests";char*args[]={name,nullptr};int qtargc=1;QApplication app(qtargc,args);app.setQuitOnLastWindowClosed(false);const fs::path out=argv[1],project=argv[2];Need(!fs::exists(out),"Fresh evidence directory required");fs::create_directories(out);SettingsRestore restore(out);const auto before=Inventory(project);Write(out/L"project-before.json",QJsonDocument(before).toJson());ExecutionSnapshot selected;{auto queue=ReconstructionQueue::OpenForInspection(project);ProjectReconstruction service(*queue,{});selected=service.Inspect();Need(selected.state==ExecutionState::Completed&&selected.initialView&&selected.resultGeneration&&!selected.output.empty(),"Original project is not a validated Completed result");Need(Hash(selected.output)==QString::fromStdString(selected.outputSha256),"Actual published output SHA mismatch");}
WorkbenchWindow w;WorkbenchController controller(w);QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[&]{if(auto*box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){Write(out/L"observed-error-dialog.txt",box->text().toUtf8());box->accept();}});dismiss.start(10);w.show();Until([&]{for(auto*edit:w.findChildren<QLineEdit*>())if(edit->accessibleName()==QStringLiteral("项目名称"))return edit->isEnabled();return false;});int code=0;QJsonObject report;
try{
 w.openProjectRequested(Text(project));Until([&]{auto s=w.Preview()->Snapshot();Need(s.state!=StudioPreviewState::Failed,"Real Unicode output Vulkan load failed");return s.state==StudioPreviewState::Ready&&s.submittedFrames>=8;});auto first=w.Preview()->Snapshot();Need(first.identity.executionId==selected.executionId&&first.identity.contentHash==selected.outputSha256&&first.identity.resultGeneration==selected.resultGeneration&&fs::equivalent(first.identity.projectDirectory,project),"Normal controller loaded foreign execution/output");Need(w.findChild<QStackedWidget*>()->currentIndex()==1,"Normal completed open did not use same-window viewer");const auto top=reinterpret_cast<HWND>(w.winId()),child=reinterpret_cast<HWND>(w.Preview()->winId()),parent=GetParent(child);Need(w.Preview()->RequestCapture(1,out/L"unicode-high-gpu.png",out/L"unicode-high-gpu.json"),"Native GPU capture rejected");Until([&]{auto s=w.Preview()->Snapshot();return s.captureId==1&&!s.capturePending;});Need(w.Preview()->Snapshot().captureSucceeded,"Actual GPU capture failed");QTest::qWait(100);Need(w.screen()->grabWindow(w.winId()).save(Text(out/L"unicode-high-window.png")),"Actual window-pixel capture failed");QJsonArray labels;for(auto*l:w.findChildren<QLabel*>())if(l->isVisible())labels.append(l->text());
 w.retryPreviewRequested();Until([&]{auto s=w.Preview()->Snapshot();Need(s.state!=StudioPreviewState::Failed,"Reload failed");return s.state==StudioPreviewState::Ready&&s.identity.requestId>first.identity.requestId&&s.submittedFrames>=8;});auto reloaded=w.Preview()->Snapshot();Need(reinterpret_cast<HWND>(w.winId())==top&&reinterpret_cast<HWND>(w.Preview()->winId())==child&&GetParent(child)==parent,"Reload changed borrowed HWND or parent");Need(reloaded.deviceCreations>=first.deviceCreations&&reloaded.deviceDestructions>=1&&reloaded.retiredToken>first.retiredToken,"Reload lacked actual native retirement");
 report={{"passed",true},{"helperExecutable",Text(fs::path(argv[0]))},{"helperNotDeployedProduct",true},{"pid",int(GetCurrentProcessId())},{"project",Text(project)},{"executionId",QString::fromStdString(selected.executionId)},{"output",Text(selected.output)},{"outputSha256",QString::fromStdString(selected.outputSha256)},{"resultGeneration",double(selected.resultGeneration)},{"cameraValidatedByRealService",selected.initialView.has_value()},{"deviceName",QString::fromStdString(first.deviceName)},{"readyFrames",double(first.submittedFrames)},{"reloadFrames",double(reloaded.submittedFrames)},{"firstRequestId",double(first.identity.requestId)},{"reloadRequestId",double(reloaded.identity.requestId)},{"sameWindow",true},{"childHwndStable",true},{"dpi",int(GetDpiForWindow(top))},{"windowLogicalWidth",w.width()},{"windowLogicalHeight",w.height()},{"deviceCreationsBeforeClose",double(reloaded.deviceCreations)},{"deviceDestructionsBeforeClose",double(reloaded.deviceDestructions)},{"visibleLabels",labels}};
}catch(const std::exception&e){code=1;report={{"passed",false},{"error",e.what()}};}
w.close();Until([&]{return !w.isVisible();});auto stopped=w.Preview()->Snapshot();Need(w.Preview()->IsStopped()&&stopped.deviceCreations==stopped.deviceDestructions&&stopped.reservedBytesAfterRetire==0,"Close preceded actual owner/device release");{auto reacquired=ReconstructionQueue::OpenForInspection(project);Need(bool(reacquired),"Lease not released");}const auto after=Inventory(project);Need(before==after,"Read-only original project changed");Write(out/L"project-after.json",QJsonDocument(after).toJson());report["projectInventoryUnchanged"]=true;report["ownerStopped"]=true;report["deviceCreations"]=double(stopped.deviceCreations);report["deviceDestructions"]=double(stopped.deviceDestructions);report["reservedBytesAfterRetire"]=double(stopped.reservedBytesAfterRetire);report["leaseReacquired"]=true;Json(out/L"test-results.json",report);std::cout<<(code?"FAIL":"PASS")<<" normal_unicode_completed_controller_gpu_reload_close"<<std::endl;return code;}

