# Delivery evidence and mandatory gates

Status at J3-runbook coding, TS `20261005-111243-tmeg`: implementation handoffs and owned-source syntax/shader checks exist. The linked runtime has not established T01-T16, real-model image quality, performance, fault behavior or 30-minute stability. Those gates are **UNVERIFIED** and block final delivery. A coding-stage PASS or recorder `RECORDED` is not acceptance PASS.

## Dataset identity

The user approved Bicycle as fallback because the inspected Garden mirror provided reduced `.splat` rather than the required original PLY. The representative model is Bicycle; no Bicycle capture or statistic should be labeled Garden. [Dataset manifest](../../../TestFile/3DGSLODStreaming/datasets/bicycle/dataset-manifest.json) records the pinned source:

| Property | Recorded value |
|---|---|
| Repository | https://huggingface.co/datasets/dylanebert/3dgs |
| Revision | 703d19c2a4a1ac5bda01c4453e74fefd7372d145 |
| Remote PLY | bicycle/point_cloud/iteration_30000/point_cloud.ply |
| Local PLY | TestFile/3DGSLODStreaming/datasets/bicycle/point_cloud_30000.ply |
| Bytes / vertices | 1,520,726,124 / 6,131,954 |
| SHA256 | 64d357cb25bd85f710f8551a18d830f8497277fbd8c5805adfd72ffe9ca78227 |
| Header | binary_little_endian, float Gaussian properties, degree-3 SH |
| Camera SHA256 | 5f0ebcb6b33e415061a485ed9961fb277cf7e875586f0d82d88b746d9d9fd502 |

Download/header/hash inspection is recorded; renderer and acceptance behavior remain UNVERIFIED. The mirror has no dataset-card/license declaration. Upstream usage conditions require verification before delivery or redistribution; no license is invented, and large source/derived resources must not be added to Git or redistributed on the basis of this mirror. Source and camera provenance must accompany any later result. Existing `bonsai_30k.ply` remains a regression model; `30000`, `30k` and `7k` in filenames refer to training iterations, not vertex counts.

[Camera derivation](../../../TestFile/3DGSLODStreaming/application/sequence_derivation.json) and its [generator](../../../TestFile/3DGSLODStreaming/application/generate_sequences.py) connect production `bicycle_lod_near.json`, `bicycle_lod_mid_far.json` and `bicycle_lod_stress.json` to downloaded cameras. Source camera-to-world rotation column 2 is forward, negative column 1 is engine up and column 0 is right; PLY world coordinates stay unchanged. Maximum basis reconstruction error is 3.3306690738754696e-16. Preserve training vertical FOV and record the change from 4946x3286 training aspect to registered 1920x1080. Radius-quantile near/mid/far names require later image inspection.

## Existing coding evidence

| Area | Evidence | Interpretation |
|---|---|---|
| Format/PLY and builder | specs/20261005-111243-tmeg/coding-manifest-J0-contracts.json and coding-manifest-J1-builder.json; TestFile/3DGSLODStreaming/builder/compile.log | Source/compile handoffs; no determinism or proxy-image PASS |
| RHI | [CodingNotes](../../../TestFile/3DGSLODStreaming/rhi/CodingNotes.md), compile.log | Syntax evidence; real completion/timestamp correctness still unverified |
| Streamer | [CodingNotes](../../../TestFile/3DGSLODStreaming/streamer/CodingNotes.md), compile.log | Syntax evidence; injected I/O/cancellation/lease behavior still unverified |
| GPU pool | [CodingNotes](../../../TestFile/3DGSLODStreaming/gpu-pool/CodingNotes.md), compile.log | Exact allocation/lifecycle implementation; real budget/fence tests pending |
| Pipeline | [CodingNotes](../../../TestFile/3DGSLODStreaming/pipeline/CodingNotes.md), compile.log, shader_compile.log | Owned C++ and GLSL compilation; GPU oracle/image/performance tests pending |
| Application | [CodingNotes](../../../TestFile/3DGSLODStreaming/application/CodingNotes.md), compile.log | CLI/capture/telemetry implementation; linked execution pending |

Build integration evidence belongs under `TestFile/3DGSLODStreaming/build`; formal source is supplied by the downstream verification stage. Generated builds, fixtures, tests scripts, downloads, intermediate resources, screenshots, logs, CSV/JSON, metrics and temporary files all stay below `TestFile/3DGSLODStreaming`. Workflow manifests/handoffs stay below `specs/20261005-111243-tmeg`. These directories contain baseline/user artifacts; do not delete or overwrite them indiscriminately.

## Correctness matrix

All rows below are currently UNVERIFIED. A missing device, unavailable fixture, failed build or incomplete evidence is not PASS. Each formal test must report meaningful failures and nonzero exit codes.

| Gate | Required evidence/assertions |
|---|---|
| T01 | PLY degree 0..3, property order, activation, malformed/nonfinite parameters; match known fixture |
| T02 | Repeat build determinism; empty, duplicate positions, degenerate bounds and bounded depth |
| T03 | Unique source leaf identity, full coverage, conservative finite support including elongated ellipsoids |
| T04 | Known covariance moment match, positive covariance, scale/rotation recovery and appearance/opacity behavior |
| T05 | Manifest/page round-trip, checksums, version/truncation/offset/overflow rejection before unsafe allocation |
| T06 | Error/FOV/resolution/near-plane/hysteresis selection; complete mutually exclusive cut |
| T07 | Multipage parent and partial-child readiness; safe downgrade and insufficient transition reserve |
| T08 | Prioritized deduplicated reads, bounded retries, faults/delays/cancel/old generations and CPU leases |
| T09 | Fake completion plus real Vulkan fences; root/transition/frame pins, no premature slot reuse and stale generations |
| T10 | Active 0/1/non-workgroup-multiple, sourceIdx distinct from activeIdx, correct SH source stride |
| T11 | Inclusive scan matches CPU oracle including nonzero final element; matching assigned entry count |
| T12 | Full 64-bit eight-pass radix keys, boundary/depth/zero/one/tail cases and tile ranges |
| T13 | Same-frame GPU count/indirect args, budget/residency invalidation, exact/overflow capacity safety |
| T14 | Corrected original versus all leaves/refined streaming, transparent background, block edges and offscreen ellipsoids |
| T15 | Camera motion/rapid reversal/jump, resize/switch/exit; no missing coverage, stale image or ownership races |
| T16 | Restricted real load/evict roaming for 30 minutes; bounded memory/lifecycle closure, no crash/device loss/validation errors |

Fault runs must deliberately cover retry exhaustion, checksum/range errors, cancelled late completions, rejected allocations, stalled/failed submission, descriptor reuse, staging exhaustion and sort overflow. Preserve real device validation logs separately from performance runs. Normal acceptance paths require zero overflow; an overflow frame preserving previous output is safe fallback evidence, not a complete current rendering result.

## Registered A-E measurement and thresholds

Modes use the same verified source and registered cameras: A corrected original PLY; B all leaves fully resident; C all pages resident with LOD selection; D ample disk streaming; E restricted disk streaming. C must actually refine and meet image quality: reduced Gaussian count or a visible root is not a performance result. Source/active/sort/staging/cache/temporary budgets are recorded separately; B/C/fullLeaf cannot silently discard original leaves to fit.

Release performance protocol: registered 1920x1080, fixed camera path advancing by elapsed time, no software cap/VSync, actual present mode recorded, >=10 seconds initial warmup and three >=60-second sample rounds. Camera time restarts per round. Preregister mid/far path and quality settings before comparison. Keep near results separate. Correctness/validation and capture runs are separate from timing. Label OS-cache state `unknown`, `warm-file-cache` or demonstrated `cold-file-cache`; process restart alone does not prove cold disk.

| Mandatory gate | Threshold | Current status |
|---|---|---|
| Full-leaf/refined equivalence | B and fully refined D versus corrected A: PSNR >=45 dB and SSIM >=0.995; no systematic visible difference | UNVERIFIED |
| Default settled LOD | PSNR >=30 dB and SSIM >=0.95; no persistent holes/boundaries or frequent oscillation | UNVERIFIED |
| B integration overhead | Median hot GPU time <=1.05*A | UNVERIFIED |
| C LOD gain | Preregistered quality-qualified mid/far path: median GPU <=0.75*B and p95 not worse | UNVERIFIED |
| D streaming overhead | Sufficient-budget settled p95 frame time <=1.10*C | UNVERIFIED |
| Restricted E | Fine-level data >=2*effective physical scene budget, actual load+evict, >=1800 sampled seconds | UNVERIFIED |
| Memory/stability | Independent hard caps; no persistent growth, crash/device loss/new validation errors/coverage loss/premature reuse | UNVERIFIED |
| Startup | First actual usable coarse frame faster than full loading under the same hardware/cache condition; record convergence | UNVERIFIED |
| Dynamic scheduling | No normal-frame synchronous count readback or disk read; real asynchronous hardware GPU intervals | UNVERIFIED |
| Overflow | Normal acceptance overflow=0; injected overflow preserves previous valid output safely | UNVERIFIED |

Use fixed linear shader-output color space, black background/transparent alpha, exposure 1, identical display SH degree and actual camera metadata. Lossless RGBA8/PPM captures contain quantized linear output; analyze that declared representation consistently. Register ROIs for near/far detail, fine foliage, materials and block boundaries. Save full-image and ROI PSNR/SSIM, maximum/quantile differences, signed/absolute error images, and temporal sequences. Empty/black regions cannot dominate metrics and hide detail loss. Fully refined D uses explicit sufficient budgets and fullLeaf, not an assumption that a low quality threshold guarantees refinement.

## Raw evidence and lifecycle closure

Each run retains `metadata.json`, `frames.csv`, `gpu_samples.csv` and `gpu_counts.csv` in its own output directory. Original frame IDs join asynchronously completed GPU stages/counts. Keep hardware interval stage names, timestamp availability, count availability, late/missing data and skipped telemetry explicit. Missing sampled GPU/count data invalidates performance evidence and propagates nonzero exit. `RUNNING`, `FAILED`, `UNVERIFIED` and `RECORDED` are recorder statuses; `RECORDED` says complete raw data exists, not that the numerical gates passed.

Metadata must include hardware/driver/Vulkan/CPU/RAM, actual present mode and implications, build/executable identity, source/manifest/builder/configuration/shader/sequence hashes, registered render settings and cache condition. Retain source revision, compiler/build configuration/commit and dirty state, disk details and externally measured total driver memory when evaluating acceptance. The runtime's allocator scope is backend pools, not total VRAM. Report logical resident bytes, dedicated scene reservations/backing, staging live/reservation/backing, CPU cache/inflight/ready/transferred/metadata charges, temporary bytes (including output image), and capture readback separately. Close transfers and page pins across repeated switch/exit cycles and record peaks plus post-teardown counts.

Capture-only writes `capture_NNN.rgba`, `.ppm` and `.json` with exact camera/SH/time metadata after an explicit completion wait. It is excluded from performance. For startup, distinguish coarse readiness, resident target readiness and the first actual nonoverflow GPU count frame; populated scenes need a positive count, while empty scenes legitimately have zero. Loading and degraded sampled frames stay in primary CSV; any settled-only subgroup must state its rule and accompany complete raw samples.

The remaining delivery package must add actual linked-build logs, formal T01-T16 results, verified usage conditions, offline builder output and measured memory, A-E rounds/summary/captures/errors, real 30-minute memory/lifecycle curves, startup comparison and reproducible validation logs. Record PASS/FAIL/UNVERIFIED for every mandatory gate with supporting paths. Do not waive a gate, lower a threshold or treat absence of evidence as successful delivery.
