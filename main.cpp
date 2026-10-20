#include "Core/Application.h"
#include "Core/Logging/Logging.h"
#include "Core/HAL/FMemoryManager.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "CubeSceneApplication.h"
#include "SplatSceneApplication.h"
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <sstream>
// CubeSceneRendererTest - now using MonsterEngine::Renderer namespace
#include "Tests/CubeSceneRendererTest.h"
#include "Tests/CubeSceneRendererTestApp.h"

// ImGui Test Application Forward Declaration
MonsterEngine::TUniquePtr<MonsterRender::Application> createImGuiTestApplication();

// Test Suite Forward Declarations
namespace MonsterRender {
    namespace MemorySystemTest {
        void runAllTests();  // Forward declaration
    }
    namespace FMemorySystemTest {
        void runFMemoryTests();  // Forward declaration
    }
    namespace TextureStreamingSystemTest {
        void runTextureStreamingTests();  // Forward declaration
    }
    namespace VirtualTextureSystemTest {
        void RunAllTests();  // Forward declaration
        void RunRealWorldScenarioTests();  // Forward declaration
    }
    namespace VulkanMemoryManagerTest {
        void RunBasicTests();  // Forward declaration
        void RunScenarioTests();  // Forward declaration
        void RunAllTests();  // Forward declaration
    }
    namespace VulkanResourceManagerTest {
        void RunBasicTests();  // Forward declaration
        void RunScenarioTests();  // Forward declaration
        void RunAllTests();  // Forward declaration
    }
}

// Logging System Test Forward Declaration
void RunLoggingSystemTests();

// Math Library Test Forward Declaration
void RunMathLibraryTests();

// Container Test Forward Declaration
// Implementation in Source/Tests/ContainerTest.cpp
void RunContainerTests();

// Smart Pointer Test Forward Declaration
// Implementation in Source/Tests/SmartPointerTest.cpp
void RunSmartPointerTests();

// Shader Preprocessor Test Forward Declaration
// Implementation in Source/RHI/ShaderPreprocessorTest.cpp
void RunShaderPreprocessorTests();
void RunSlotConversionTests();

// Parallel Rendering Test Forward Declaration
// Implementation in Source/TestMain.cpp
int RunAllParallelRenderingTests();

// Z-Fighting Test Forward Declaration
// Implementation in Source/Tests/ZFightingTest.cpp
namespace MonsterEngine {
    namespace ZFightingTest {
        void RunAllTests();
    }
}

// Entry point following UE5's application architecture
int RunSplatCameraSequenceTests();

// Legacy suites report assertion failures through logging rather than return values.
class FLegacyTestFailureOutput final : public MonsterRender::FOutputDevice {
public:
    void Serialize(const char* message, MonsterRender::ELogVerbosity::Type,
                   const char*) override {
        if (message && (strstr(message, "[FAIL]") || strstr(message, "FAILED:") ||
                        strstr(message, " test(s) failed"))) {
            Failed.store(true, std::memory_order_relaxed);
        }
    }
    std::atomic<bool> Failed{false};
};

int main(int argc, char** argv) {
    using namespace MonsterRender;
    bool splatEvidenceRun=false;
    for(int i=1;i<argc;++i)if(strcmp(argv[i],"--splat-benchmark")==0||strcmp(argv[i],"--splat-capture-only")==0||strcmp(argv[i],"--splat-scene")==0||strcmp(argv[i],"--splat-config")==0||strcmp(argv[i],"--splat-calibrated-capture")==0)splatEvidenceRun=true;
    
    // ========================================================================
    // Initialize Logging System FIRST - before any other initialization
    // ========================================================================
    InitializeLogging(
        splatEvidenceRun?"TestFile/3DGSLODStreaming/application/MonsterEngine.log":"log/MonsterEngine.log",
        true,                        // Enable console output
        true,                        // Enable debug output (OutputDebugString on Windows)
        true                         // Enable file output
    );
    
    // ========================================================================
    // Keep normal runs at Error; opt in to camera and pipeline diagnostics with
    // MONSTER_SPLAT_DIAG=1 when reproducing a splat rendering problem.
    // ========================================================================
    // Opt in to the splat diagnostics while reproducing camera-dependent issues.
    char* splatDiag = nullptr;
    size_t splatDiagLength = 0;
    _dupenv_s(&splatDiag, &splatDiagLength, "MONSTER_SPLAT_DIAG");
    SetGlobalLogVerbosity(splatDiag ? ELogVerbosity::Log : ELogVerbosity::Error);
    std::free(splatDiag);

    MR_LOG(LogInit, Log, "Starting MonsterRender Engine");
    MR_LOG(LogInit, Log, "Command line arguments: %d", argc);
    
    // Check for test mode flag
    bool runMemoryTests = false;
    bool runTextureTests = false;
    bool runVirtualTextureTests = false;
    bool runVulkanMemoryTests = false;
    bool runVulkanResourceTests = false;
    bool runLoggingTests = false;
    bool runMathTests = false;
    bool runContainerTests = false;
    bool runSmartPointerTests = false;
    bool runParallelRenderingTests = false;  // Run parallel rendering tests
    bool runParallelIntegrationExample = false;  // Run parallel rendering integration example
    bool runAllTests = false;
    bool runCubeScene = false;  // Run CubeSceneApplication with lighting
    bool runPBRScene = false;
    bool cubeShowShadows = true;
    bool cubeShowcase = false;
    uint32 cubeFrameLimit = 0;
    String cubeCaptureOutput;
    bool runCubeSceneTest = false;  // Run CubeSceneRendererTest (pipeline integration test)
    bool useDeferredRendering = false;  // Enable deferred rendering in CubeSceneApplication
    bool runZFightingTests = false;  // Run Z-Fighting test suite
    bool runSplatScene = true;  // Run SplatSceneApplication (3DGS render)
    String splatModelPath = "resources/point_cloud/bicycle/point_cloud_30000.ply";
    SplatSceneApplication::FStartupExportOptions splatExport;
    Splat::FSplatLODRuntimeOptions splatRuntime;
    bool splatLODRequested = false;
    // Default showcase uses the bounded Bicycle scene built from the original PLY.
    // Explicit CLI runs keep their own model/config and camera settings.
    if (argc == 1 || (argc == 2 && (strcmp(argv[1], "--splat") == 0 || strcmp(argv[1], "-splat") == 0))) {
        std::string error;
        if (!Splat::FSplatLODRuntimeOptions::LoadConfig("resources/3dgs_sequences/lod_streaming_defaults.json", splatRuntime, error)) {
            std::fprintf(stderr, "Cannot load Bicycle showcase defaults: %s\n", error.c_str());
            ShutdownLogging(); return 2;
        }
        splatLODRequested = true;
    }
    // A calibrated request is a complete isolated invocation.
    for(int i=1;i<argc;++i)if(strcmp(argv[i],"--splat-calibrated-capture")==0){
        std::string error;
        if(argc!=3||i!=1||!Splat::FSplatLODRuntimeOptions::LoadCalibratedCaptureConfig(argv[2],splatRuntime,error)){
            std::fprintf(stderr,"Invalid calibrated capture: %s\n",error.empty()?"Use only --splat-calibrated-capture <config.json>":error.c_str());ShutdownLogging();return 2;
        }
        splatLODRequested=true;
    }
    for(int i=1;i<argc;++i)if(strcmp(argv[i],"--splat-config")==0){
        std::string error;
        if(i+1>=argc||!Splat::FSplatLODRuntimeOptions::LoadConfig(argv[++i],splatRuntime,error)){
            std::fprintf(stderr,"Invalid splat config: %s\n",error.c_str());ShutdownLogging();return 2;
        }
        splatLODRequested=true;
    }
    if(!splatRuntime.sourcePLY.empty())splatModelPath=splatRuntime.sourcePLY;
    auto unsignedOption=[](const char* text)->uint64_t{size_t consumed=0;if(!text||!*text||*text=='-')throw std::runtime_error("Expected unsigned option");const auto n=std::stoull(text,&consumed);if(consumed!=std::strlen(text))throw std::runtime_error("Malformed integer option");return n;};
    auto realOption=[](const char* text)->double{size_t consumed=0;const auto n=std::stod(text,&consumed);if(consumed!=std::strlen(text)||!std::isfinite(n))throw std::runtime_error("Malformed numeric option");return n;};
    bool splatControlUIVisible = true;
    try {
    for (int i = 1; i < argc; ++i) {
        auto argument=[&](){if(i+1>=argc)throw std::runtime_error(std::string("Missing argument for ")+argv[i]);return argv[++i];};
        if (strcmp(argv[i], "--test-memory") == 0 || strcmp(argv[i], "-tm") == 0) {
            runMemoryTests = true;
        }
        else if (strcmp(argv[i], "--test-texture") == 0 || strcmp(argv[i], "-tt") == 0) {
            runTextureTests = true;
        }
        else if (strcmp(argv[i], "--test-virtual-texture") == 0 || strcmp(argv[i], "-tvt") == 0) {
            runVirtualTextureTests = true;
        }
        else if (strcmp(argv[i], "--test-vulkan-memory") == 0 || strcmp(argv[i], "-tvm") == 0) {
            runVulkanMemoryTests = true;
        }
        else if (strcmp(argv[i], "--test-vulkan-resource") == 0 || strcmp(argv[i], "-tvr") == 0) {
            runVulkanResourceTests = true;
        }
        else if (strcmp(argv[i], "--test-logging") == 0 || strcmp(argv[i], "-tl") == 0) {
            runLoggingTests = true;
        }
        else if (strcmp(argv[i], "--test-math") == 0 || strcmp(argv[i], "-tmath") == 0) {
            runMathTests = true;
        }
        else if (strcmp(argv[i], "--test-container") == 0 || strcmp(argv[i], "-tc") == 0) {
            runContainerTests = true;
        }
        else if (strcmp(argv[i], "--test-smartptr") == 0 || strcmp(argv[i], "-tsp") == 0) {
            runSmartPointerTests = true;
        }
        else if (strcmp(argv[i], "--test-parallel") == 0 || strcmp(argv[i], "-tp") == 0) {
            runParallelRenderingTests = true;
        }
        else if (strcmp(argv[i], "--parallel-rendering-integration") == 0 || strcmp(argv[i], "-pri") == 0) {
            runParallelIntegrationExample = true;
        }
        else if (strcmp(argv[i], "--test-all") == 0 || strcmp(argv[i], "-ta") == 0) {
            runAllTests = true;
        }
        else if (strcmp(argv[i], "--renderPBR") == 0 || strcmp(argv[i], "--pbr-scene") == 0) {
            runPBRScene = true;
            runCubeScene = true;
            runSplatScene = false;
        }
        else if (strcmp(argv[i], "--cube-scene") == 0 || strcmp(argv[i], "-cs") == 0) {
            runCubeScene = true;
            runSplatScene = false;
        }
        else if (strcmp(argv[i], "--cube-showcase") == 0) {
            runCubeScene = true;
            runSplatScene = false;
            cubeShowcase = true;
        }
        else if (strcmp(argv[i], "--cube-no-shadows") == 0) {
            runCubeScene = true;
            runSplatScene = false;
            cubeShowShadows = false;
            cubeShowcase = true;
        }
        else if (strcmp(argv[i], "--cube-frames") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--cube-frames requires a positive frame count\n");
                ShutdownLogging(); return 2;
            }
            char* end = nullptr;
            const char* value = argv[++i];
            const unsigned long frames = std::strtoul(value, &end, 10);
            if (!*value || *value == '-' || *end || frames == 0 || frames > 1000000) {
                std::fprintf(stderr, "Invalid --cube-frames value\n");
                ShutdownLogging(); return 2;
            }
            cubeFrameLimit = static_cast<uint32>(frames);
            runCubeScene = true;
            runSplatScene = false;
            cubeShowcase = true;
        }
        else if (strcmp(argv[i], "--cube-capture") == 0) {
            if (i + 1 >= argc || !*argv[i + 1] || argv[i + 1][0] == '-') {
                std::fprintf(stderr, "--cube-capture requires a PPM output path\n");
                ShutdownLogging(); return 2;
            }
            cubeCaptureOutput = argv[++i];
            runCubeScene = true;
            runSplatScene = false;
            cubeShowcase = true;
        }
        else if (strcmp(argv[i], "--cube-scene-test") == 0 || strcmp(argv[i], "-cst") == 0) {
            runCubeSceneTest = true;
        }
        else if (strcmp(argv[i], "--deferred") == 0 || strcmp(argv[i], "-def") == 0) {
            useDeferredRendering = true;
        }
        else if (strcmp(argv[i], "--test-zfighting") == 0 || strcmp(argv[i], "-tzf") == 0) {
            runZFightingTests = true;
        }
        else if (strcmp(argv[i], "--splat") == 0 || strcmp(argv[i], "-splat") == 0) {
            runSplatScene = true;
            // Optional: next arg is model path
            if (i + 1 < argc && argv[i + 1][0] != '-')
                splatRuntime.sourcePLY = splatModelPath = argv[++i];
        }
        else if (strcmp(argv[i], "--test-splat-controls") == 0) {
            const int result = RunSplatCameraSequenceTests();
            ShutdownLogging();
            return result;
        }
        else if (strcmp(argv[i], "--splat-no-ui") == 0) {
            splatControlUIVisible = false;
        }
        else if (strcmp(argv[i], "--splat-sequence") == 0 && i + 1 < argc) {
            splatRuntime.sequencePath = splatExport.sequencePath = argv[++i];
        }
        else if(strcmp(argv[i],"--splat-calibrated-capture")==0){argument();}
        else if(strcmp(argv[i],"--splat-config")==0){argument();}
        else if(strcmp(argv[i],"--splat-scene")==0){splatRuntime.scenePath=argument();splatLODRequested=true;}
        else if(strcmp(argv[i],"--splat-source-ply")==0){splatRuntime.sourcePLY=splatModelPath=argument();}
        else if(strcmp(argv[i],"--splat-mode")==0){const std::string mode=argument();if(mode.size()!=1)throw std::runtime_error("Mode requires one letter");splatRuntime.mode=mode[0];splatLODRequested=true;}
        else if(strcmp(argv[i],"--splat-benchmark")==0){splatRuntime.benchmark=true;}
        else if(strcmp(argv[i],"--splat-capture-only")==0){splatRuntime.benchmark=true;splatRuntime.captureOnly=true;}
        else if(strcmp(argv[i],"--splat-validation")==0){splatRuntime.validation=true;}
        else if(strcmp(argv[i],"--splat-full-leaf")==0){splatRuntime.fullLeaf=true;}
        else if(strcmp(argv[i],"--splat-output")==0){splatRuntime.outputDirectory=argument();}
        else if(strcmp(argv[i],"--splat-cache-condition")==0){splatRuntime.cacheCondition=argument();}
        else if(strcmp(argv[i],"--splat-warmup")==0){splatRuntime.warmupSeconds=realOption(argument());}
        else if(strcmp(argv[i],"--splat-seconds")==0){splatRuntime.sampleSeconds=realOption(argument());}
        else if(strcmp(argv[i],"--splat-quality")==0){splatRuntime.lod.qualityPixels=float(realOption(argument()));}
        else if(strcmp(argv[i],"--splat-capture-settle")==0){splatRuntime.captureSettleSeconds=realOption(argument());}
        else if(strcmp(argv[i],"--splat-rounds")==0){const auto n=unsignedOption(argument());if(n>100)throw std::runtime_error("Rounds exceed 100");splatRuntime.rounds=uint32_t(n);}
        else if(strcmp(argv[i],"--splat-width")==0){const auto n=unsignedOption(argument());if(n>16384)throw std::runtime_error("Width exceeds 16384");splatRuntime.width=uint32_t(n);}
        else if(strcmp(argv[i],"--splat-height")==0){const auto n=unsignedOption(argument());if(n>16384)throw std::runtime_error("Height exceeds 16384");splatRuntime.height=uint32_t(n);}
        else if(strcmp(argv[i],"--splat-render-width")==0){const auto n=unsignedOption(argument());if(n>16384)throw std::runtime_error("Render width exceeds 16384");splatRuntime.renderWidth=uint32_t(n);}
        else if(strcmp(argv[i],"--splat-render-height")==0){const auto n=unsignedOption(argument());if(n>16384)throw std::runtime_error("Render height exceeds 16384");splatRuntime.renderHeight=uint32_t(n);}
        else if(strcmp(argv[i],"--splat-active-capacity")==0){const auto n=unsignedOption(argument());if(n>UINT32_MAX)throw std::runtime_error("Active capacity exceeds uint32");splatRuntime.activeCapacity=uint32_t(n);}
        else if(strcmp(argv[i],"--splat-sort-capacity")==0){const auto n=unsignedOption(argument());if(n>UINT32_MAX)throw std::runtime_error("Sort capacity exceeds uint32");splatRuntime.sortCapacity=uint32_t(n);}
        else if(strcmp(argv[i],"--splat-sh-degree")==0){const auto n=unsignedOption(argument());if(n>3)throw std::runtime_error("SH degree exceeds 3");splatRuntime.displaySHDegree=int32_t(n);}
        else if(strcmp(argv[i],"--splat-capture-times")==0){std::istringstream times(argument());std::string item;splatRuntime.captureTimes.clear();while(std::getline(times,item,','))splatRuntime.captureTimes.push_back(realOption(item.c_str()));}
        else if(strcmp(argv[i],"--splat-scene-mib")==0||strcmp(argv[i],"--splat-cpu-mib")==0||strcmp(argv[i],"--splat-staging-mib")==0||strcmp(argv[i],"--splat-temporary-mib")==0||strcmp(argv[i],"--splat-upload-mib")==0){
            const std::string option=argv[i];const auto n=unsignedOption(argument());if(n>(1ull<<28))throw std::runtime_error("Budget exceeds supported bytes");const uint64_t bytes=n<<20;
            if(option=="--splat-scene-mib")splatRuntime.streaming.sceneGPUBytes=bytes;else if(option=="--splat-cpu-mib")splatRuntime.streaming.cpuCacheBytes=bytes;else if(option=="--splat-staging-mib")splatRuntime.streaming.stagingBytes=bytes;else if(option=="--splat-temporary-mib")splatRuntime.streaming.temporaryGPUBytes=bytes;else splatRuntime.streaming.maxUploadBytesPerFrame=bytes;
        }
        else if (strcmp(argv[i], "--splat-export") == 0 && i + 1 < argc) {
            splatExport.videoPath = argv[++i];
        }
        else if (strcmp(argv[i], "--splat-ffmpeg") == 0 && i + 1 < argc) {
            splatExport.ffmpegPath = argv[++i];
        }
        else if (strcmp(argv[i], "--splat-export-fps") == 0 && i + 1 < argc) {
            const int fps = atoi(argv[++i]);
            splatExport.fps = fps > 0 ? static_cast<uint32>(fps) : 0;
        }
        else if (strcmp(argv[i], "--imgui-test") == 0 || strcmp(argv[i], "-imgui") == 0) {
            // Run ImGui test application directly
            MR_LOG(LogInit, Log, "Running ImGui Test Application...");
            auto imguiApp = createImGuiTestApplication();
            if (imguiApp) {
                int32 exitCode = imguiApp->run();
                imguiApp->shutdown();
                imguiApp.reset();
                ShutdownLogging();
                return exitCode;
            }
            MR_LOG(LogInit, Error, "Failed to create ImGui test application");
            ShutdownLogging();
            return -1;
        }
    }
    }catch(const std::exception& e){std::fprintf(stderr,"Invalid splat option: %s\n",e.what());ShutdownLogging();return 2;}
    if(splatLODRequested||splatRuntime.benchmark){
        std::string error;
        if(!splatRuntime.Validate(error)||(splatRuntime.mode!='A'&&splatRuntime.scenePath.empty())){
            std::fprintf(stderr,"Invalid splat runtime: %s\n",error.empty()?"Modes B-E require --splat-scene":error.c_str());ShutdownLogging();return 2;
        }
    }
    // SplatShaderPath uses the current build's generated directory unless the
    // caller explicitly overrides it; never select a stale experiment package.
    
    // Default behavior: run parallel rendering integration example by default
    // If no command line arguments provided, keep runParallelIntegrationExample = true
    if (argc == 1) {
        // No arguments, run parallel rendering integration example by default
        runParallelIntegrationExample = false;
    }
    
    // Run logging tests separately (can run standalone)
    if (runLoggingTests) {
        RunLoggingSystemTests();
        //return 0;
    }
    
    // Run math library tests
    if (runMathTests) {
        printf("\n");
        printf("==========================================");
        printf("  Math Library Tests");
        printf("==========================================");
        printf("\n");
        
        RunMathLibraryTests();
        ShutdownLogging();
        return 0;
    }
    
    // Run container tests
    if (runContainerTests) {
        printf("Starting container tests...\n");
        fflush(stdout);
        printf("\n");
        RunContainerTests();
        printf("Container tests completed.\n");
        fflush(stdout);
        ShutdownLogging();
        return 0;
    }
    
    // Run smart pointer tests
    if (runSmartPointerTests) {
        RunSmartPointerTests();
        ShutdownLogging();
        return 0;
    }
    
    // Run Z-Fighting tests
    if (runZFightingTests) {
        MonsterEngine::ZFightingTest::RunAllTests();
        ShutdownLogging();
        return 0;
    }
    
    // Run parallel rendering integration example
    if (runParallelIntegrationExample) {
        printf("\n");
        printf("==========================================\n");
        printf("  Parallel Rendering Integration Example\n");
        printf("==========================================\n");
        printf("\n");
        
        // Call RunParallelRenderingTestMain to initialize Vulkan device and run example
        extern int RunParallelRenderingTestMain(int argc, char** argv);
        int exitCode = RunParallelRenderingTestMain(argc, argv);
        
        printf("\n");
        ShutdownLogging();
        return exitCode;
    }
    
    // Run parallel rendering tests
    if (runParallelRenderingTests) {
        printf("\n");
        printf("==========================================\n");
        printf("  Parallel Rendering System Tests\n");
        printf("==========================================\n");
        printf("\n");
        
        int exitCode = RunAllParallelRenderingTests();
        
        printf("\n");
        ShutdownLogging();
        return exitCode;
    }
    
    // Run tests if requested
    if (runMemoryTests || runTextureTests || runVirtualTextureTests || 
        runVulkanMemoryTests || runVulkanResourceTests || runMathTests || runContainerTests || runAllTests) {
        SetGlobalLogVerbosity(ELogVerbosity::Log);
        FLegacyTestFailureOutput testFailures;
        GLog->AddOutputDevice(&testFailures);
        printf("\n");
        printf("==========================================");
        printf("  MonsterEngine Test Suite");
        printf("==========================================");
        printf("\n");
        
        // Initialize FMemoryManager before any test that uses FMemory::Malloc
        FMemoryManager::Get().Initialize();
        
        // Run ShaderPreprocessor tests first (no dependencies)
        printf("======================================\n");
        printf("  Shader Preprocessor Tests\n");
        printf("======================================\n");
        printf("\n");
        
        RunShaderPreprocessorTests();
        printf("\n");
        
        // Run Slot Conversion tests
        printf("======================================\n");
        printf("  Slot Conversion Tests\n");
        printf("======================================\n");
        printf("\n");
        
        RunSlotConversionTests();
        printf("\n");
        
        // Run memory system tests
        if (runMemoryTests || runAllTests) {
            printf("======================================");
            printf("  Memory System Tests");
            printf("======================================");
            printf("");
            
            MemorySystemTest::runAllTests();
            
            printf("\n");
            printf("======================================");
            printf("  FMemory System Tests");
            printf("======================================");
            printf("");
            
            FMemorySystemTest::runFMemoryTests();
        }
        
        // Run texture streaming tests
        if (runTextureTests || runAllTests) {
            printf("\n\n");
            printf("======================================");
            printf("  Texture Streaming System Tests");
            printf("======================================");
            printf("");
            
            TextureStreamingSystemTest::runTextureStreamingTests();
        }
        
        // Run virtual texture system tests
        if (runVirtualTextureTests || runAllTests) {
            printf("\n\n");
            printf("==========================================");
            printf("  Virtual Texture System Tests");
            printf("==========================================");
            printf("");
            
            // Run basic tests
            VirtualTextureSystemTest::RunAllTests();
            
            // Run real-world scenario tests
            printf("\n");
            VirtualTextureSystemTest::RunRealWorldScenarioTests();
        }
        
        // Run Vulkan memory manager tests
        if (runVulkanMemoryTests || runAllTests) {
            printf("\n\n");
            printf("==========================================");
            printf("  Vulkan Memory Manager Tests");
            printf("==========================================");
            printf("");
            
            // Run all Vulkan memory tests
            VulkanMemoryManagerTest::RunAllTests();
        }
        
        // Run Vulkan resource manager tests
        if (runVulkanResourceTests || runAllTests) {
            printf("\n\n");
            printf("==========================================");
            printf("  Vulkan Resource Manager Tests");
            printf("==========================================");
            printf("");
            
            // Run all Vulkan resource management tests
            VulkanResourceManagerTest::RunAllTests();
        }
        
        printf("\n");
        printf("==========================================");
        printf("  All Tests Completed");
        printf("==========================================");
        printf("\n");
        
        // Exit after tests
        GLog->Flush();
        GLog->RemoveOutputDevice(&testFailures);
        const bool failed = testFailures.Failed.load(std::memory_order_relaxed);
        ShutdownLogging();
        return failed ? 1 : 0;
    }
    
    // CubeSceneRendererTest - uses MonsterEngine::Renderer namespace
    // Run as Application with proper RHI device initialization
    if (runCubeSceneTest) {
        MR_LOG(LogInit, Log, "Running CubeSceneRendererTestApp...");
        auto cubeSceneTestApp = CreateCubeSceneRendererTestApp();
        if (cubeSceneTestApp) {
            int32 exitCode = cubeSceneTestApp->run();
            cubeSceneTestApp->shutdown();
            cubeSceneTestApp.reset();
            ShutdownLogging();
            return exitCode;
        }
        MR_LOG(LogInit, Error, "Failed to create CubeSceneRendererTestApp");
        ShutdownLogging();
        return -1;
    }
    
    // SplatSceneApplication — 3DGS render
    if (runSplatScene) {
        MR_LOG(LogInit, Log, "Running SplatSceneApplication with model: %s", splatModelPath.c_str());
        auto splatApp = MakeUnique<SplatSceneApplication>(splatModelPath, splatExport, splatRuntime);
        if (splatApp) {
            splatApp->SetControlUIVisible(splatControlUIVisible&&!splatRuntime.benchmark);
            int32 exitCode = splatApp->run();
            splatApp->shutdown();
            if (splatApp->HasRunFailed()) exitCode = 1;
            splatApp.reset();
            ShutdownLogging();
            return exitCode;
        }
        MR_LOG(LogInit, Error, "Failed to create SplatSceneApplication");
        ShutdownLogging();
        return -1;
    }

    // Create application instance
    // Default: Run CubeSceneApplication (rotating cube demo)
    TUniquePtr<Application> app;
    MR_LOG(LogInit, Log, "Running CubeSceneApplication (default demo)...");
    auto cubeApp = MakeUnique<CubeSceneApplication>();
    if (!cubeApp) {
        printf("Failed to create application");
        return -1;
    }
    
    // Enable deferred rendering if requested
    if (runPBRScene) {
        cubeApp->configurePBRShowcase(cubeFrameLimit);
    }
    else if (runCubeScene || cubeShowcase) {
        cubeApp->configureShowcase(cubeShowShadows, cubeFrameLimit, cubeShowcase);
    }
    if (!cubeCaptureOutput.empty()) cubeApp->setCaptureOutput(cubeCaptureOutput);
    if (useDeferredRendering) {
        MR_LOG(LogInit, Log, "Deferred Rendering enabled via command line");
        cubeApp->setUseDeferredRendering(true);
    }
    
    auto* cubeRunState = cubeApp.Get();
    app = std::move(cubeApp);
    
    // Run the application
    int32 exitCode = app->run();
    if (cubeRunState->hasCaptureFailed()) exitCode = 1;
    
    // Shutdown before destroying to ensure proper cleanup order
    app->shutdown();
    
    // Cleanup is handled by RAII
    app.reset();
    
    MR_LOG(LogExit, Log, "MonsterRender Engine shutting down with exit code: %d", exitCode);
    
    // Shutdown logging system LAST - after all other cleanup
    ShutdownLogging();
    
    return exitCode;
}
