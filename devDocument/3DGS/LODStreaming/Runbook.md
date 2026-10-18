# Build, run and reproduce LOD streaming

Run commands from `D:/code/MonsterEngine`. These are implemented entrypoints for TS `20261005-111243-tmeg`; this coding handoff does not claim a linked build or acceptance run passed. Consult [SceneProtocol](SceneProtocol.md) for file/lifecycle contracts and [DeliveryEvidence](DeliveryEvidence.md) for mandatory gates. Put every build, download, generated scene, config, test helper, log, capture and result under `TestFile/3DGSLODStreaming`. Use a distinct output directory for each run because the recorder truncates files with matching names.

## Build entrypoints

Windows viewer prerequisites are the project's bundled dependencies, MSVC with C++20, CMake >=3.20, a Vulkan device/driver and `glslc` on PATH or configured through VULKAN_SDK/SPLAT_GLSLC. The CPU builder can configure without the viewer using MONSTER_BUILD_ENGINE=OFF. Generated shader files must match the executable and be retained with their hashes.

```powershell
cmake -S . -B TestFile/3DGSLODStreaming/build/cmake -G "Visual Studio 16 2019" -A x64 -DCMAKE_GENERATOR_INSTANCE=D:/software/VS2019
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build TestFile/3DGSLODStreaming/build/cmake --config Release --target MonsterEngine SplatLODBuilder SplatShaders
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
$env:MONSTER_SPLAT_SHADER_DIR = 'D:/code/MonsterEngine/TestFile/3DGSLODStreaming/build/cmake/splat-shaders'
```

CMake targets are MonsterEngine, SplatLODCoreCPU, SplatLODBuilder and SplatShaders. Executables are `build/cmake/bin/Release/MonsterEngine.exe` and `SplatLODBuilder.exe` under the artifact tree. SplatShaders writes a flat `build/cmake/splat-shaders` directory and is an engine dependency. The compile-time default points there; MONSTER_SPLAT_SHADER_DIR explicitly overrides it. Missing glslc fails viewer configuration rather than leaving stale shaders. CPU-only configure/build uses another directory:

```powershell
cmake -S . -B TestFile/3DGSLODStreaming/build/cpu-builder -G "Visual Studio 16 2019" -A x64 -DCMAKE_GENERATOR_INSTANCE=D:/software/VS2019 -DMONSTER_BUILD_ENGINE=OFF
if ($LASTEXITCODE -ne 0) { throw 'CPU builder configure failed' }
cmake --build TestFile/3DGSLODStreaming/build/cpu-builder --config Release --target SplatLODBuilder
if ($LASTEXITCODE -ne 0) { throw 'CPU builder build failed' }
```

The native VS project also includes the new implementation inventory. In a VS2019 developer shell, `msbuild MonsterEngine.vcxproj /p:Configuration=Release /p:Platform=x64` writes engine output to `TestFile/3DGSLODStreaming/build/vs/bin/Release`, objects to `build/vs/obj/Release`, shaders to `build/shaders`, and invokes a CPU-only CMake builder build in `build/native-builder`. Its builder is `build/native-builder/bin/Release/SplatLODBuilder.exe`. `SplatCMakeExe` overrides CMake discovery; `SplatSkipBuilder=true` explicitly skips the post-build builder, so that build cannot establish builder availability. Standalone shader regeneration is implemented by `cmd /c Shaders\Splat\compile_splat.bat TestFile\3DGSLODStreaming\pipeline\shaders`; set MONSTER_SPLAT_SHADER_DIR to that directory when using it.

Retain stdout/stderr and failure exit codes in artifact build logs. Owned-source `/Zs` checks and GLSL compilation are coding evidence only; linked builds and device execution remain separate checks.

## Obtain and verify the representative model

Bicycle is the approved fallback because the inspected Garden mirror lacked the required raw PLY. Exact pinned URLs:

- [Bicycle original PLY](https://huggingface.co/datasets/dylanebert/3dgs/resolve/703d19c2a4a1ac5bda01c4453e74fefd7372d145/bicycle/point_cloud/iteration_30000/point_cloud.ply)
- [Bicycle training cameras](https://huggingface.co/datasets/dylanebert/3dgs/resolve/703d19c2a4a1ac5bda01c4453e74fefd7372d145/bicycle/cameras.json)

Local files already downloaded are `datasets/bicycle/point_cloud_30000.ply` and `datasets/bicycle/cameras.json` below the artifact tree. The camera URL was remotely compared to local bytes: 77,427 bytes and matching SHA256. The mirror supplies no dataset license declaration; retain that provenance limitation and verify upstream conditions before redistribution. Do not add the model or derived large resources to Git. The source filename's 30000 is a training iteration, not a Gaussian count.

The following recovery/download procedure writes only missing files, checks temporary bytes before moving them, then validates existing files as well:

```powershell
$datasetRoot = 'TestFile/3DGSLODStreaming/datasets/bicycle'
New-Item -ItemType Directory -Force -Path $datasetRoot | Out-Null
$datasetRevision = '703d19c2a4a1ac5bda01c4453e74fefd7372d145'
$datasetFiles = @(
    @{ Name='point_cloud_30000.ply'; Remote='bicycle/point_cloud/iteration_30000/point_cloud.ply'; Bytes=1520726124; Hash='64d357cb25bd85f710f8551a18d830f8497277fbd8c5805adfd72ffe9ca78227' },
    @{ Name='cameras.json'; Remote='bicycle/cameras.json'; Bytes=77427; Hash='5f0ebcb6b33e415061a485ed9961fb277cf7e875586f0d82d88b746d9d9fd502' }
)
foreach ($datasetFile in $datasetFiles) {
    $datasetPath = Join-Path $datasetRoot $datasetFile.Name
    if (!(Test-Path -LiteralPath $datasetPath)) {
        $datasetPartial = $datasetPath + '.partial'
        $datasetURL = 'https://huggingface.co/datasets/dylanebert/3dgs/resolve/' + $datasetRevision + '/' + $datasetFile.Remote
        curl.exe --fail --location --retry 2 --output $datasetPartial $datasetURL
        if ($LASTEXITCODE -ne 0) { throw 'Dataset download failed' }
        if ((Get-Item -LiteralPath $datasetPartial).Length -ne $datasetFile.Bytes -or (Get-FileHash -LiteralPath $datasetPartial -Algorithm SHA256).Hash.ToLowerInvariant() -ne $datasetFile.Hash) { throw 'Downloaded dataset size/hash mismatch' }
        Move-Item -LiteralPath $datasetPartial -Destination $datasetPath
    }
    if ((Get-Item -LiteralPath $datasetPath).Length -ne $datasetFile.Bytes -or (Get-FileHash -LiteralPath $datasetPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $datasetFile.Hash) { throw 'Local dataset size/hash mismatch' }
}
```

Verified PLY header: binary_little_endian, 6,131,954 vertices, 62 float properties including normals, position, SH DC plus 45 rest coefficients, opacity, three log-scales and four quaternion components. [dataset-manifest.json](../../../TestFile/3DGSLODStreaming/datasets/bicycle/dataset-manifest.json) and [ply-header.txt](../../../TestFile/3DGSLODStreaming/datasets/bicycle/ply-header.txt) retain the inspection. The production loader still validates the full input during offline construction/original mode.

`python TestFile/3DGSLODStreaming/application/generate_sequences.py` regenerates the three durable `resources/3dgs_sequences/bicycle_lod_near.json`, `bicycle_lod_mid_far.json`, and `bicycle_lod_stress.json` files plus `TestFile/3DGSLODStreaming/application/sequence_derivation.json`. It uses the verified source cameras and preserves source coordinates/vertical FOV. Review any changed sequence and retain hashes; labels use camera-position radius quantiles and require image inspection. Do not substitute synthetic paths or label these Bicycle paths Garden.

## Construct a scene

```powershell
& TestFile/3DGSLODStreaming/build/cmake/bin/Release/SplatLODBuilder.exe --input TestFile/3DGSLODStreaming/datasets/bicycle/point_cloud_30000.ply --output TestFile/3DGSLODStreaming/builder/bicycle --config Tools/SplatLODBuilder/default-config.json --max-input-bytes 8589934592
if ($LASTEXITCODE -ne 0) { throw 'Offline scene construction failed' }
```

`--input` and `--output` are required; `--config` and `--max-input-bytes` are optional. `--help` prints this contract. Output must be a new directory; preserve a failed attempt and select a new path for retry. Successful output is `scene.mgs`, `pages/*.mgsp` and `build-report.json`. `.mgs` is binary with its own checksum footer; there is no `scene.mgs.json`. The report retains source/executable/canonical-config hashes, counts, depth, merge/error policies and an estimated offline working set. Measure actual offline RAM separately. A useful hierarchy and three useful resolutions must be verified, not inferred merely from the file count.

## Runtime configuration and mode matrix

The current builder emits manifest v3 (`MonsterEngine-SplatLOD-4-RMSCenterDepth`), with emitting alpha-area and offline original/proxy descendant center bounds; pages stay v1. The reader accepts unchanged v1 and interim v2 scenes. See [SceneProtocol](SceneProtocol.md) for exact layouts and the approximate RMS/image-impact contract: support controls visibility, centers control safe projection depth, and no error statistic certifies the quality gates. Rebuild an older scene into a NEW output directory with the existing builder command to obtain the new statistics; never overwrite old evidence.

The continuation repair derives the same statistics from all 8,174,025 verified existing source/proxy records, without rebuilding/downloading Bicycle pages. Its explicit v3 artifact is `TestFile/3DGSLODStreaming/continuation-20261006-041033-bxgr/fix1/bicycle-scene-rms/scene-center.mgs`; v1/v2 inputs and failed attempts remain alongside their original reports. The artifact-only `fix1/compile_centers.cmd` and `derive_centers.exe input-v2.mgs NEW-output.mgs` document this one-time center metadata derivation. Production generation remains SplatLODBuilder. Configure a new run with an explicit scene path and distinct output directory. Use the registered quality1/SH3/camera/budget settings; do not increase tolerance to obtain proxy counts.

Default proxy image checks and short diagnostic timing are distinct from the mandatory three60second A-E comparison, full/refined45/.995 quality, startup comparison and E1800sampledseconds. Those gates remain UNVERIFIED until their actual evidence passes. A nonzero proxy count does not establish the25percent performance requirement.

`resources/3dgs_sequences/lod_streaming_defaults.json` now points to the actual `TestFile/3DGSLODStreaming/builder/bicycle/scene.mgs`. Config loads first; explicit CLI arguments override it. Known fields are strictly typed; unknown fields and invalid bounds fail. Default D uses 1920x1080, activeCapacity=1,000,000, sortCapacity=8,000,000, qualityPixels=1, hysteresis=0.2, scene/temporary=512 MiB each, CPU=256 MiB, staging=32 MiB, upload=8 MiB/frame, two reads and two additional retries. These are bounded defaults, not validated real-model quality settings.

| Mode | Actual startup and selection |
|---|---|
| A | Original full PLY path; with scene provided, verifies source hash/count/degree against it |
| B | Pins root and all leaf pages, preflights capacity, then renders all original leaves through active indices |
| C | Pins every manifest page, requires all representations to fit, then uses resident LOD selection |
| D | Starts indexes/coarse pages and asynchronously streams desired refinements with registered ample budgets |
| E | Restricted streaming; fine bytes must be >=2*measured physical source allocation; benchmark requires actual eviction and >=30 minutes |

Source allocation, active Gaussian and sort entry capacities are independent. Bicycle A/B/fullLeaf require active capacity >=6,131,954. B requires leaf plus mandatory coarse slots; C requires all proxy/leaf pages. Physical source, staging and temporary resources must fit their budgets; outputImageBytes is included in temporaryBytes. Original mode A reports deduplicated physical backing separately. No mode may report a truncated list as original-quality success.

Interactive bounded D launch:

```powershell
& TestFile/3DGSLODStreaming/build/cmake/bin/Release/MonsterEngine.exe --splat-config resources/3dgs_sequences/lod_streaming_defaults.json
if ($LASTEXITCODE -ne 0) { throw 'Viewer failed' }
```

The UI exposes loading, LOD quality, freeze, level/bounds outlines, mode/scene/budgets and resident/waiting/active/sort/memory/overflow diagnostics. Apply scene/mode/budget changes through the existing scene switch path; benchmark mode rejects interactive scene replacement. LOD disable/fullLeaf requires enough source and active capacity, otherwise reports bounded degradation; it never silently drops original points.

Implemented flags: `--splat-config`, `--splat-scene`, `--splat-source-ply`, `--splat-mode A|B|C|D|E`, `--splat-benchmark`, `--splat-capture-only`, `--splat-validation`, `--splat-full-leaf`, `--splat-sequence`, `--splat-output`, `--splat-cache-condition`, `--splat-warmup`, `--splat-seconds`, `--splat-rounds`, `--splat-quality`, `--splat-width`, `--splat-height`, `--splat-active-capacity`, `--splat-sort-capacity`, `--splat-sh-degree`, `--splat-capture-times`, `--splat-capture-settle`, and scene/cpu/staging/temporary/upload `--splat-*-mib`. Read concurrency/retries/hysteresis/freeze are JSON fields. SH CLI accepts 0..3; omit it or use JSON displaySHDegree=-1 to match source degree. MiB is 1,048,576 bytes. Capture times are a strictly increasing comma-separated list.

## Release timing and separate captures

Copy defaults to an artifact config with validation=false for timing; `--splat-validation` only enables validation, so do not use it to disable a config's true value. The examples below use explicit capacity starting points; their fit/quality/overflow behavior is UNVERIFIED and must be checked on the registered hardware. Do not lower leaf counts to obtain a run. Adjust registered budgets before acceptance, then retain the exact config/hash across comparisons.

```powershell
New-Item -ItemType Directory -Force -Path TestFile/3DGSLODStreaming/configs | Out-Null
$timingConfig = Get-Content -LiteralPath resources/3dgs_sequences/lod_streaming_defaults.json -Raw | ConvertFrom-Json
$timingConfig.validation = $false
$timingText = $timingConfig | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText([System.IO.Path]::GetFullPath('TestFile/3DGSLODStreaming/configs/performance.json'), $timingText, [System.Text.UTF8Encoding]::new($false))
$viewer = 'TestFile/3DGSLODStreaming/build/cmake/bin/Release/MonsterEngine.exe'
$comparisonArgs = @('--splat-config','TestFile/3DGSLODStreaming/configs/performance.json','--splat-benchmark','--splat-sequence','resources/3dgs_sequences/bicycle_lod_mid_far.json','--splat-width','1920','--splat-height','1080','--splat-warmup','10','--splat-seconds','60','--splat-rounds','3','--splat-cache-condition','unknown','--splat-active-capacity','6131954','--splat-sort-capacity','64000000','--splat-scene-mib','2048','--splat-temporary-mib','4096')
foreach ($comparisonMode in @('A','B','C','D')) {
    & $viewer @comparisonArgs --splat-mode $comparisonMode --splat-output ('TestFile/3DGSLODStreaming/evidence/mid_far/' + $comparisonMode)
    if ($LASTEXITCODE -ne 0) { throw ('Incomplete timing evidence for mode ' + $comparisonMode) }
}
```

The runtime requests IMMEDIATE and disables software caps; record actual mode. MAILBOX fallback has presentation synchronization implications; FIFO rejects timing evidence. One initial >=10s warmup precedes three >=60s consecutive rounds; camera time restarts each round. Warmup begins after required coarse/resident startup readiness, not at process launch. Loading/degraded sample rows remain in primary data. Repeat the registered near path separately. Do not claim the budgets above or selector node-bound sort heuristic produce valid C/D quality until captures demonstrate actual refinement.

Restricted E example uses the default 512 MiB scene cap and a 60-second stress sequence repeated for three 600-second rounds. Runtime enforces fine-data ratio and later actual eviction; confirm root/transition fit and memory peaks:

```powershell
& $viewer --splat-config TestFile/3DGSLODStreaming/configs/performance.json --splat-mode E --splat-benchmark --splat-sequence resources/3dgs_sequences/bicycle_lod_stress.json --splat-output TestFile/3DGSLODStreaming/evidence/restricted_E --splat-warmup 10 --splat-seconds 600 --splat-rounds 3 --splat-cache-condition unknown
if ($LASTEXITCODE -ne 0) { throw 'Restricted E evidence failed or is incomplete' }
```

Capture-only samples exact times and explicitly waits for completion; it is a separate run excluded from timing. For corrected A, B and fully refined D, retain identical camera/SH/background and sufficient fullLeaf capacity. Example for D; run A/B with distinct directories and corresponding modes, omitting fullLeaf for A:

```powershell
& $viewer --splat-config resources/3dgs_sequences/lod_streaming_defaults.json --splat-mode D --splat-capture-only --splat-validation --splat-full-leaf --splat-sequence resources/3dgs_sequences/bicycle_lod_mid_far.json --splat-output TestFile/3DGSLODStreaming/evidence/captures/refined_D --splat-active-capacity 6131954 --splat-sort-capacity 64000000 --splat-scene-mib 2048 --splat-temporary-mib 4096 --splat-capture-times '0,15,30,45,60' --splat-capture-settle 2
if ($LASTEXITCODE -ne 0) { throw 'Capture evidence failed or is incomplete' }
```

Keep the comma-separated capture argument quoted as one string. The config example writes UTF-8 without a byte-order mark, which the strict parser expects. Capture convergence has a bounded wait and fails after 60 seconds; settle time alone does not prove completion. E can capture its bounded degraded fallback. Readback physical backing must fit the remaining temporary budget.

Artifacts are `capture_NNN.rgba`, RGB P6 `.ppm`, and `.json` with exact view/projection/position/focal/SH/time. Pixels are linear shader RGBA8 output before sRGB presentation, black RGB/transparent alpha and exposure 1. Register ROIs and compare declared color space consistently. Full leaves/refined D require PSNR >=45dB and SSIM >=0.995 versus corrected A; default settled LOD requires >=30dB/0.95 with no persistent holes or boundaries. Save full/ROI metrics, error maps and temporal captures under the artifact tree. Image comparison and numerical acceptance tools belong to downstream verification; none is implied by a successful capture exit.

## Interpret telemetry and close resources

Every timing run emits `metadata.json`, `frames.csv`, `gpu_samples.csv` and `gpu_counts.csv`. Raw stage names are Pipeline, Preprocess, Scan, AssignKeys, RadixSort, TileBounds and Render. Hardware GPU samples/count batches preserve original frame IDs. Do not join a delayed count to the current recording or add the total Pipeline interval to its component intervals. `gpu_available`, `count_available`, missing/late counts and skipped samples must remain explicit. Pending joins are bounded at 128 frames; incomplete sampled evidence is UNVERIFIED/nonzero exit.

`RECORDED` means raw evidence completed without the recorder's overflow/missing checks; downstream computes median/p95/p99 and quality. It does not mean acceptance PASS. Record B GPU <=1.05*A; quality-qualified mid/far C median <=0.75*B with no worse p95; sufficient settled D frame p95 <=1.10*C. Retain complete samples before any declared settled subgroup. Normal acceptance overflow must be zero; fallback retaining the old image is not successful current-frame output.

Metadata retains executable/source/manifest/sequence/configuration/shader hashes, CPU/RAM/GPU/driver/Vulkan/timestamp and actual present settings. Preserve build configuration/commit/dirty state, compiler, disk/cache details and page/build-report provenance alongside it. Cold-file-cache needs demonstrated OS-cache control; process restart alone is unknown or warm. Compare firstUsableFrameSeconds/ID and residentTargetReadySeconds separately; populated scenes require an actual completed nonoverflow positive GPU count for first usable evidence. Startup must beat full loading under the same conditions.

See [SceneProtocol](SceneProtocol.md) for per-frame protect/Tick/build/submit/release sequencing. Transfer leases remain charged until vectors are destroyed and ReleaseTransferred is called. Submission tokens must change after actual successful present. Ordinary frames do not wait/read files/counts. Failed submit, scene switch and teardown prove completion, release immediate bindings/present descriptors/source owners and drain deferred destruction before replacement. Capture/teardown waits are explicit exceptions and cannot contaminate timing. Preserve post-shutdown transfer/reservation/page counters, independent memory peaks and global allocator slack for T15/T16.

## Formal verification build hooks

The build provides opt-in suite targets, not fabricated tests. The downstream test generator supplies `Source/Tests/3DGSLODStreaming/SplatLODCPUTests.cpp`, `SplatLODGPUTests.cpp` and `SplatLODIntegrationTests.cpp` (or explicit source lists through SPLAT_CPU_TEST_SOURCES, SPLAT_GPU_TEST_SOURCES and SPLAT_INTEGRATION_TEST_SOURCES). With those real files present:

```powershell
cmake -S . -B TestFile/3DGSLODStreaming/build/verification -G "Visual Studio 16 2019" -A x64 -DCMAKE_GENERATOR_INSTANCE=D:/software/VS2019 -DSPLAT_BUILD_CPU_TESTS=ON -DSPLAT_BUILD_GPU_TESTS=ON -DSPLAT_BUILD_INTEGRATION_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'Verification configure failed' }
cmake --build TestFile/3DGSLODStreaming/build/verification --config Release --target SplatLODTestsCPU SplatLODTestsGPU SplatLODTestsIntegration
if ($LASTEXITCODE -ne 0) { throw 'Verification build failed' }
ctest --test-dir TestFile/3DGSLODStreaming/build/verification -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Formal verification failed' }
```

Missing source lists/files fail configuration. CPU tests link SplatLODCoreCPU; GPU/integration targets require the engine and real Vulkan support. There is no separate SplatBenchmark executable or invented `SplatLODTests --suite` CLI. Use the actual targets/MonsterEngine flags. Missing hardware is UNVERIFIED and blocks mandatory gates. Keep all test fixtures/helpers/results below the artifact tree, with durable formal source in the test source directory. [DeliveryEvidence](DeliveryEvidence.md) maps T01-T16, A-E quality/performance, fault injection and the 30-minute run to required evidence; none is waived by this runbook.
