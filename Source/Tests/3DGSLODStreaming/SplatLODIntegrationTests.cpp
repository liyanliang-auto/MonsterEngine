#include "SplatLODTestSupport.h"
#include "SplatSceneApplication.h"
#include "Renderer/Splat/LOD/SplatLODBuilder.h"
#include "Renderer/Splat/LOD/SplatSceneFormat.h"
#include "Renderer/Splat/SplatCameraSequence.h"
#include <limits>
#include <thread>
#include <cmath>
#include <windows.h>
using namespace SplatVerification;
namespace {
FSplatLODRuntimeOptions Options(){FSplatLODRuntimeOptions o;o.benchmark=true;o.sequencePath="registered-recorder-unit-fixture";o.outputDirectory=Directory("recorder").string();return o;}
struct TimeoutScene: SplatSceneApplication {
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    bool timeout=false;
    TimeoutScene(const FSplatLODRuntimeOptions& o):SplatSceneApplication("",{},o){}
    void onUpdate(float dt) override {SplatSceneApplication::onUpdate(dt);if(std::chrono::steady_clock::now()-start>std::chrono::seconds(12)){timeout=true;requestExit();}}
};
struct SwitchScene: TimeoutScene {
    unsigned frames=0;bool switched=false;std::string scene,source;uint64_t lastToken=0;
    SwitchScene(const FSplatLODRuntimeOptions& o):TimeoutScene(o),scene(o.scenePath),source(o.sourcePLY){}
    void onUpdate(float dt) override {TimeoutScene::onUpdate(dt);if(frames==5&&!switched){Require(QueueSceneSwitch(scene,source,'B'),"valid switch rejected");switched=true;}if(frames==10)onWindowResize(80,64);if(frames>=16)requestExit();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    void onRender() override {SplatSceneApplication::onRender();if(getEngine()){auto t=getEngine()->getRHIDevice()->getLastSubmissionToken();Require(t.value>=lastToken,"submission token regressed across scene switch");lastToken=t.value;}++frames;}
};
struct VisibleUIScene: TimeoutScene {
    unsigned frames=0;bool sawPress=false,sawRelease=false;
    VisibleUIScene(const FSplatLODRuntimeOptions& o):TimeoutScene(o){}
    void onInitialize() override {
        SplatSceneApplication::onInitialize();
        PostMessageW(static_cast<HWND>(getWindow()->getNativeHandle()),WM_KEYDOWN,0x57,0x00110001);
    }
    void onUpdate(float dt) override {
        auto* input=getWindow()->getInputManager();
        sawPress=sawPress||(input->isKeyPressed(EKey::W)&&input->isKeyDown(EKey::W));
        sawRelease=sawRelease||(input->isKeyReleased(EKey::W)&&!input->isKeyDown(EKey::W));
        TimeoutScene::onUpdate(dt);
        if(frames==10)PostMessageW(static_cast<HWND>(getWindow()->getNativeHandle()),WM_KEYUP,0x57,0xc0110001);
        if(++frames>=256)requestExit();
    }
};
struct Fixture {
    fs::path dir=Directory("integration-scene");std::string ply,scene,sequence;
    Fixture(){ply=(dir/"source.ply").string();std::ofstream f(ply);f<<"ply\nformat ascii 1.0\nelement vertex 4\n";for(const char* p:{"x","y","z","f_dc_0","f_dc_1","f_dc_2","opacity","scale_0","scale_1","scale_2","rot_0","rot_1","rot_2","rot_3"})f<<"property float "<<p<<'\n';f<<"end_header\n";for(int i=0;i<4;++i)f<<((i%2)*.4-.2)<<' '<<((i/2)*.4-.2)<<" -5 .3 .5 .1 1 -2 -2 -2 1 0 0 0\n";f.close();std::vector<FSplatGaussian> g;unsigned degree;std::string e;Require(FSplatPLYLoader::LoadCPU(ply,g,degree,e),e);FSplatLODBuildConfig cfg;cfg.maxLeafGaussians=1;cfg.maxPageGaussians=1;Require(FSplatSceneFormat::SHA256File(ply,cfg.sourceSHA256,e),e);cfg.builderHash=std::string(64,'c');FSplatSceneManifest m;FSplatLODBuilder builder;Require(builder.Build(g,degree,cfg,(dir/"scene").string(),m,e),e);scene=(dir/"scene/scene.mgs").string();sequence=(dir/"camera.json").string();FSplatCameraSequence cameras;FSplatCameraKeyframe key;key.State.Yaw=-90;key.State.Pitch=0;key.State.FOV=60;Require(cameras.AddOrReplace(key),"fixture camera first");key.TimeSeconds=1;Require(cameras.AddOrReplace(key),"fixture camera second");Require(cameras.SaveJson(sequence,&e),e);}
    FSplatLODRuntimeOptions Runtime(char mode){auto o=Options();o.mode=mode;o.scenePath=scene;o.sourcePLY=ply;o.sequencePath=sequence;o.captureOnly=true;o.captureTimes={0};o.captureSettleSeconds=.02;o.width=320;o.height=256;o.activeCapacity=8;o.sortCapacity=256;o.streaming.sceneGPUBytes=128ull<<20;o.streaming.stagingBytes=4096;o.streaming.cpuCacheBytes=65536;o.streaming.maxUploadBytesPerFrame=4096;o.streaming.temporaryGPUBytes=16ull<<20;o.validation=true;return o;}
};
}
#include "SplatLODRound3IntegrationTests.h"
#include "SplatRadixIntegrationTests.h"
#include "SplatViewerMeasurementTests.h"
int main(int argc,char** argv){if(argc==3&&std::string(argv[1])=="--bicycle-capture"){char mode=argv[2][0];if(std::string(argv[2]).size()!=1||(mode!='A'&&mode!='C'&&mode!='D'))return 2;InitializeLogging((ArtifactRoot()/"logs/bicycle-engine.log").string().c_str(),false,true,ELogVerbosity::Warning);FSplatLODRuntimeOptions o;o.benchmark=o.captureOnly=o.validation=true;o.mode=mode;o.scenePath="TestFile/3DGSLODStreaming/verification/bicycle-scene/scene.mgs";o.sourcePLY="TestFile/3DGSLODStreaming/datasets/bicycle/point_cloud_30000.ply";o.sequencePath="resources/3dgs_sequences/bicycle_lod_mid_far.json";o.outputDirectory=(ArtifactRoot()/(std::string("bicycle-capture-")+mode)).string();o.width=1920;o.height=1080;o.activeCapacity=6131954;o.sortCapacity=64000000;o.streaming.sceneGPUBytes=2ull<<30;o.streaming.temporaryGPUBytes=4ull<<30;o.captureTimes={0};o.captureSettleSeconds=2;o.cacheCondition="unknown";SplatSceneApplication app("",{},o);app.SetControlUIVisible(false);int result=app.run();app.shutdown();bool failed=app.HasRunFailed();ShutdownLogging();return result||failed?1:0;}fs::create_directories("TestFile/3DGSLODStreaming/verification/logs");InitializeLogging((ArtifactRoot()/"logs/integration-engine.log").string().c_str(),false,true,ELogVerbosity::Warning);Suite suite;
    RunViewerMeasurementTests(suite);
    RunRadixIntegrationTests(suite);
    suite.Test("T15 strict runtime config defaults unknown duplicate and finite validation",[]{FSplatLODRuntimeOptions o;std::string e;Require(FSplatLODRuntimeOptions::LoadConfig("resources/3dgs_sequences/lod_streaming_defaults.json",o,e),e);Require(o.mode=='D'&&o.activeCapacity==1500000&&o.streaming.sceneGPUBytes==(1ull<<30)&&!o.vsync&&o.sortCapacity==16000000&&o.width==1920&&o.height==1080&&!o.projectedFusion&&o.sourcePLY.find("bicycle")!=std::string::npos,"registered defaults");auto path=Directory("config")/"bad.json";for(const char* body:{"{\"unknown\":1}","{\"width\":64,\"width\":128}","{\"warmupSeconds\":1e999}","{\"captureTimes\":[0,0]}","{\"mode\":\"Z\"}"}){std::ofstream f(path);f<<body;f.close();auto previous=o;Require(!FSplatLODRuntimeOptions::LoadConfig(path.string(),o,e),"invalid config accepted");Require(o.width==previous.width&&o.mode==previous.mode,"failed config mutated output");}o=Options();o.mode='E';o.sampleSeconds=60;o.rounds=3;Require(!o.Validate(e),"short E stress accepted");o.sampleSeconds=600;Require(o.Validate(e),e);o.warmupSeconds=0;Require(!o.Validate(e),"performance warmup waived");});
    suite.Test("T15 independent internal render resolution is strict and backwards compatible",[]{
        FSplatLODRuntimeOptions o;std::string e;
        Require(FSplatLODRuntimeOptions::LoadConfig("resources/3dgs_sequences/bicycle_realtime_1080p.json",o,e),e);
        Require(o.width==1920&&o.height==1080&&o.renderWidth==1280&&o.renderHeight==720&&o.activeCapacity==1400000&&!o.vsync,"realtime presentation/render split");
        o.renderHeight=0;Require(!o.Validate(e),"half-specified internal resolution accepted");
        o.renderHeight=720;o.renderWidth=2048;Require(!o.Validate(e),"internal resolution larger than presentation accepted");
        o.renderWidth=o.renderHeight=0;Require(o.Validate(e),e);
    });
    suite.Test("T15 interactive validation and presentation obey explicit config",[]{
        FSplatLODRuntimeOptions o;std::string e;
        Require(FSplatLODRuntimeOptions::LoadConfig("resources/3dgs_sequences/lod_streaming_defaults.json",o,e),e);
        SplatSceneApplication ordinary("",{},o);
        Require(!ordinary.getConfig().enableValidation,"interactive false validation overridden");
        o.validation=true;o.vsync=false;SplatSceneApplication diagnostic("",{},o);
        Require(diagnostic.getConfig().enableValidation&&!diagnostic.getConfig().enableVSync&&!diagnostic.getConfig().windowProperties.vsync,"interactive diagnostic policy ignored");
        o.benchmark=true;o.vsync=true;SplatSceneApplication measured("",{},o);
        Require(measured.getConfig().enableValidation&&!measured.getConfig().enableVSync,"benchmark policy changed");
        auto path=Directory("presentation-config")/"config.json";std::ofstream f(path);f<<"{\"validation\":true,\"vsync\":false}";f.close();
        Require(FSplatLODRuntimeOptions::LoadConfig(path.string(),o,e)&&o.validation&&!o.vsync,e);
    });
    suite.Test("T15 validated UI freeze and budget scene queue",[]{FSplatControlState c;c.LODQualityPixels=std::numeric_limits<float>::infinity();Require(!c.IsValid(),"nonfinite UI quality accepted");c.ClampToValid();Require(c.IsValid(),"UI clamp did not repair finite control");SplatSceneApplication app;Require(app.IsControlUIVisible(),"ordinary UI hidden");app.onKeyPressed(EKey::F1);Require(!app.IsControlUIVisible(),"UI toggle");Require(!app.QueueSceneSwitch("","",'D')&&!app.QueueSceneSwitch("x","",'Z'),"invalid scene switch accepted");auto o=Options();SplatSceneApplication benchmark("",{},o);Require(!benchmark.QueueSceneSwitch("x","",'D'),"benchmark allowed camera/config-changing scene switch");});
    suite.Test("T15 short functional evidence does not relax registered acceptance implicitly",[]{
        auto o=Options();std::string e;o.mode='D';o.warmupSeconds=1;o.sampleSeconds=5;o.rounds=1;
        Require(!o.Validate(e),"short run accepted without explicit functional policy");
        o.functionalSmoke=true;Require(o.Validate(e),e);
        o.mode='E';Require(o.Validate(e),e);
        o.functionalSmoke=false;Require(!o.Validate(e),"functional flag leaked into formal policy");
        auto p=Directory("functional-policy")/"config.json";std::ofstream f(p);f<<"{\"mode\":\"D\",\"functionalSmoke\":true,\"benchmark\":true,\"sequence\":\"fixture\",\"output\":\"TestFile/3DGSLODStreaming/functional-policy\",\"warmupSeconds\":1,\"sampleSeconds\":5,\"rounds\":1}";f.close();
        Require(FSplatLODRuntimeOptions::LoadConfig(p.string(),o,e)&&o.functionalSmoke,e);
    });
    suite.Test("T15 actual Bicycle sequence metadata and valid cameras",[]{for(const char* p:{"resources/3dgs_sequences/bicycle_lod_near.json","resources/3dgs_sequences/bicycle_lod_mid_far.json","resources/3dgs_sequences/bicycle_lod_stress.json"}){FSplatCameraSequence s;std::string e;Require(s.LoadJson(p,&e),e);Require(s.GetDuration()==60&&s.GetKeyframes().size()>=5,"production sequence duration");for(auto& k:s.GetKeyframes())Require(k.State.IsValid(),"invalid real camera transform/FOV");auto b=Read(p);std::string text(b.begin(),b.end());Require(text.find("Bicycle")!=std::string::npos&&text.find("cameraSourceSHA256")!=std::string::npos,"dataset provenance missing");}});
    suite.Test("T13 delayed event batch joins original frame IDs",[]{auto o=Options();FSplatLODBenchmark recorder;std::string e;Require(recorder.Begin(o,{{"fixtureKind","recorder unit input events, not hardware measurements"}},e),e);for(unsigned frame:{101u,102u,103u}){FSplatLODBenchmarkFrame f;f.frame=frame;f.sampled=true;f.degraded=frame==102;Require(recorder.Record(f,e),e);}FSplatPipelineFrameStats resolved;for(unsigned frame:{103u,101u,102u}){resolved.countSamples.push_back({frame,frame+7,false});for(const char* stage:{"Pipeline","Preprocess","Scan","AssignKeys","RadixSort","TileBounds","Render"})resolved.timestampSamples.push_back({frame,stage,double(frame)});}Require(recorder.Resolve(resolved,e),e);Require(recorder.Finish(true,"recorder unit fixture only",e),e);auto bytes=Read(fs::path(o.outputDirectory)/"frames.csv");std::string csv(bytes.begin(),bytes.end());Require(csv.find("101,")!=std::string::npos&&csv.find("102,")!=std::string::npos&&csv.find("103,")!=std::string::npos,"saved frame IDs absent");Require(csv.find("101,101,101,101,101,101,101")!=std::string::npos,"late timestamps joined wrong frame");});
    suite.Test("T13 bounded row ring reports missing measurements",[]{auto o=Options();FSplatLODBenchmark recorder;std::string e;Require(recorder.Begin(o,{},e),e);for(unsigned i=1;i<=130;++i){FSplatLODBenchmarkFrame f;f.frame=i;f.sampled=true;f.loading=true;f.degraded=true;Require(recorder.Record(f,e),e);}Require(!recorder.Finish(true,"missing GPU is explicit",e),"missing GPU falsely recorded complete");auto b=Read(fs::path(o.outputDirectory)/"metadata.json");std::string text(b.begin(),b.end());Require(text.find("UNVERIFIED")!=std::string::npos&&text.find("130")!=std::string::npos,"missing frame counts absent");});
    suite.Test("T13 actual overflow prevents successful evidence",[]{auto o=Options();FSplatLODBenchmark recorder;std::string e;Require(recorder.Begin(o,{},e),e);FSplatLODBenchmarkFrame f;f.frame=1;f.sampled=true;f.pipeline.countSamples={{1,400,true}};for(const char* stage:{"Pipeline","Preprocess","Scan","AssignKeys","RadixSort","TileBounds","Render"})f.pipeline.timestampSamples.push_back({1,stage,1});Require(recorder.Record(f,e),e);Require(!recorder.Finish(true,"overflow fixture",e),"overflow falsely accepted");});
    suite.Test("T14 synthetic actual A B fullleaf D capture equivalence",[]{Fixture fixture;std::vector<uint8_t> reference;for(char mode:{'A','B','D'}){auto o=fixture.Runtime(mode);o.fullLeaf=mode=='D';TimeoutScene app(o);auto result=app.run();app.shutdown();Require(result==0&&!app.timeout&&!app.HasRunFailed(),std::string("actual capture app failed mode ")+mode);auto image=Read(fs::path(o.outputDirectory)/"capture_000.rgba");Require(image.size()==320*256*4&&std::any_of(image.begin(),image.end(),[](uint8_t v){return v>0;}),"capture invalid or transparent");if(reference.empty())reference=image;else{double mse=0;for(unsigned i=0;i<image.size();++i){double delta=(double(image[i])-reference[i])/255;mse+=delta*delta;}mse/=image.size();Require(mse==0||-10*std::log10(mse)>=45,"fullleaf fixture PSNR below 45dB");}auto m=Read(fs::path(o.outputDirectory)/"capture_000.json");std::string meta(m.begin(),m.end());Require(meta.find("\"sequenceSeconds\":0")!=std::string::npos&&meta.find("excludedFromPerformance")!=std::string::npos,"capture timestamp/camera protocol missing");}});
    suite.Test("T15 real scene switch resize and shutdown",[]{Fixture fixture;auto o=fixture.Runtime('D');o.benchmark=o.captureOnly=false;SwitchScene app(o);app.SetControlUIVisible(false);auto result=app.run();app.shutdown();Require(result==0&&app.switched&&app.frames>=16&&app.lastToken>0&&!app.timeout&&!app.HasRunFailed(),"switch/resize lifecycle failed");});
    suite.Test("T15 original interactive scene respects configured GPU budget",[]{
        Fixture fixture;auto o=fixture.Runtime('A');o.benchmark=o.captureOnly=false;
        // This cannot hold the pipeline. Ordinary original-scene startup used
        // to ignore the config and silently use the legacy automatic budget.
        o.streaming.temporaryGPUBytes=1;
        TimeoutScene app(o);app.SetControlUIVisible(false);
        app.run();app.shutdown();
        Require(app.HasRunFailed()&&!app.timeout,"ordinary A silently ignored configured temporary budget");
    });
    suite.Test("T15 visible UI frame uploads and polled input edges",[]{
        Fixture fixture;auto o=fixture.Runtime('D');o.benchmark=o.captureOnly=false;
        VisibleUIScene app(o);Require(app.IsControlUIVisible(),"UI unexpectedly disabled");
        const auto result=app.run();app.shutdown();
        Require(result==0&&!app.timeout&&!app.HasRunFailed()&&app.frames>=256,"visible UI rendering lifecycle failed");
        Require(app.sawPress&&app.sawRelease,"polled input edges cleared before onUpdate");
    });
    RunRound3Integration(suite);
    int result=suite.Finish();ShutdownLogging();return result;
}

