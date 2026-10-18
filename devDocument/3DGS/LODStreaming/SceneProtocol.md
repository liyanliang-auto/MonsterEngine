# Implemented scene and runtime protocol

This describes the current source contract for TS `20261005-111243-tmeg`. Coding artifacts exist; real-device correctness, quality and performance remain UNVERIFIED. The approved development document remains the acceptance contract. [Runbook](Runbook.md) gives production commands; [DeliveryEvidence](DeliveryEvidence.md) records the remaining gates.

## Scene files and conventions

The offline builder writes a new directory containing binary `scene.mgs`, independent `pages/page_<id>.mgsp` files, and `build-report.json`. The manifest is not JSON. Version 1 pages contain explicitly serialized FP32 records in array-of-structures order, not the proposed SoA format. There is no compression or dependence on another page. The current builder assigns one representation owner per page and offset zero per independent file; the reader supports validated nonzero range offsets.

All integers and FP32 bit patterns are little-endian. Scene conventions are `source-world`, quaternion `wxyz`, and `coefficient-major-rgb` SH. No world-coordinate transformation occurs. PLY scales use exp, opacity uses sigmoid, and quaternion normalization happens once in CPU ingestion. Pages contain activated positive scales, opacity in [0,1], unit rotations, position w=1, and scale w=0. Source SH degree is 0..3; active SH floats per Gaussian are `3*(degree+1)^2`. Every page nevertheless serializes all 48 SH floats; unused coefficients must be finite zero. Display SH degree may be lower but does not change source buffer stride.

Leaves retain normalized attributes and `originalId` without additional quantization. Offline ordering may differ from PLY order; identity links leaves back to original rows. Proxy IDs set bit 63 and use deterministic construction IDs. Gaussian centers belong to one leaf. Support bounds encompass source descendants and the node's proxy representations for configured `supportSigma`; these finite bounds do not represent infinite Gaussian tails.

## Manifest byte order

The body begins with U32 magic `0x4d53474d` (bytes `MGSM`), U32 manifest version and U32 SH degree. Default construction emits v3; optional original fusion membership emits v4. Readers retain v1/v2/v3 compatibility. Pages remain v1. Text is U32 byte length followed by bytes, with no terminator. ID arrays are U32 count followed by U32 IDs. The body ends with a 64-byte lowercase ASCII SHA256 hex digest of all preceding body bytes; this is not a raw 32-byte digest.

Fields after the prefix, in order:

1. Text sourceSHA256, builderVersion, builderHash, configHash, buildConfig, endian, coordinates, quaternion, shLayout.
2. F32 supportSigma; U32 rootNode; U64 sourceCount, leafCount, proxyCount, totalDataBytes; U32 node count, page count.
3. For each node in dense ID order: three F32 bounds.min, three F32 bounds.max; children ID array, representation page ID array; U32 gaussianCount; F32 geometricError, appearanceError; U32 level. v2 and later append F64 coverageAreaUpper. v3 and later additionally append three F32 centerBounds.min and three F32 centerBounds.max. v4 then appends U32 original group count; each group is U32 member count (1..4) followed by that many U32 original row ordinals. All floating-point bit patterns are explicitly little-endian; native nodes are never dumped.
4. For each page in dense ID order: U32 id; Text relativePath; U64 offset, byteSize, decodedByteSize; U32 gaussianCount; Text sha256; U32 pinned (0 or 1).

Node IDs are vector indices. Root level is zero; every other node has one parent with the next level. Children number at most eight. Every page has exactly one node owner. Node counts sum their page counts, leafCount equals sourceCount, and totalDataBytes sums encoded page sizes. Nonempty root pages must be pinned. Empty scenes have invalid root `0xffffffff`, no nodes/pages and zero statistics.

## Page layout

The integer version selects the layout. v2 requires builder tag `MonsterEngine-SplatLOD-3-RMSFootprint`; v3 requires `MonsterEngine-SplatLOD-4-RMSCenterDepth`; v4 requires `MonsterEngine-SplatLOD-5-OriginalFusionGroups`. v4 extends v3 error/bounds semantics without changing them. v1 preserves legacy `MonsterEngine-SplatLOD-1` semantics and the interim `MonsterEngine-SplatLOD-2-RMS` convention. Version/policy mismatches and unknown versions fail validation. Existing artifacts are preserved, with no silent reinterpretation or in-place upgrade.

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | U32 magic `0x5053474d`, bytes `MGSP` |
| 4 | 4 | U32 version=1 |
| 8 | 4 | U32 scene SH degree |
| 12 | 4 | U32 Gaussian count |
| 16 | 4 | U32 disk record stride=252 |
| 20 | 4 | U32 reserved=0 |
| 24 | count*252 | Gaussian records |

Each record contains position[4] at offset 0, scale[4] at 16, rotation[4] at 32, opacity at 48, sh[48] at 52, and U64 originalId at 244. The disk stride is 252 bytes. The current CPU `FSplatGaussian` occupies 256 bytes because its uint64 alignment introduces padding; native structs are never dumped to disk. `decodedByteSize` records count*sizeof(FSplatGaussian). Manifest `sha256` hashes the entire encoded page including its 24-byte header. Encoded byteSize must equal `24 + count*252`.

The parser caps the manifest body and individual pages at 256 MiB, each index at 2,000,000 entries and text at 65,536 bytes. It rejects unknown versions/conventions, malformed hashes, truncation, trailing manifest data, invalid counts/offset arithmetic, nonfinite attributes, invalid activated values, and tree/statistics mismatches before publishing results. Relative paths reject roots, drive prefixes and dot traversal; streamer canonicalization also rejects paths escaping the scene directory. File length is checked before each bounded range read. Checksum validation precedes successful decoded delivery.

## Construction and error limits

Defaults: maxLeafGaussians=8192, maxPageGaussians=8192, maxDepth=16, supportSigma=3, proxyReduction=0.25, geometricErrorScale=1, appearanceWeight=1 and maxAppearanceError=0.1. Stable eight-bucket spatial subdivision terminates at leaf count, depth, or duplicate/degenerate centers; maxDepth is validated at <=64. Pages split oversized representations independently. ConfigHash hashes canonical numeric configuration; builderHash hashes the actual executable; sourceSHA256 hashes the original PLY.

Parents reduce child representations using stable local Morton neighborhoods and covariance moment matching. Merge weights approximate opacity times ellipsoid surface area. Covariance includes both within-Gaussian covariance and displacement covariance; a symmetric eigensolver restores scale/rotation. SH is weighted, and merged opacity approximates area-normalized mass capped at one. This does not preserve alpha compositing exactly. `maxAppearanceError` normalizes merge cost; it is not a hard rejection threshold. Geometric displacement/covariance error and SH/opacity appearance error accumulate through merges and are monotonicized across levels. They guide selection and do not certify PSNR or SSIM. Three useful resolutions, proxy quality, and real refinement require measured verification.

The builder refuses an existing output directory and writes the valid manifest last. A failed build can retain partial files in its new directory; an absent or invalid manifest is not a published scene. Preserve failure logs and use a new output directory for retries. The offline working set includes the full normalized source, indices, proxy arrays and page temporaries; `offlineWorstCaseWorkingBytesEstimate` is an estimate, not a measured peak or the runtime CPU budget.

## Selection, ownership and completion

For RMS-tagged scenes, geometricError is a world-unit, mass-weighted Gaussian transport RMS bound: center displacement plus the Frobenius difference of symmetric covariance square roots under a common-standard-normal coupling, accumulated by triangle inequality through merges. appearanceError is world-unit SH-coefficient RMS and activated-opacity difference weighted by child finite-support radius. Both are monotonicized across hierarchy levels. Morton quantization uses center bounds independently of support outliers. Legacy v1 appearance retains its dimensionless meaning and is converted with the support radius.

coverageAreaUpper is the deterministic sum of `opacity*max(sx*sy,sx*sz,sy*sz)` over the actual node representation, finite and nonnegative in world squared. It bounds orthographic projected Gaussian alpha-area for any direction, including anisotropy; it does not certify composited descendant image error. v3 centerBounds contains every current and descendant original/proxy center, is nested across children, and stays within conservative support bounds. It is constructed offline, so startup need not read all payload pages.

RMS image impact uses focal/depth times combined world error, weighted by the square root of emitting occupancy over the viewport. Occupancy uses the smaller of the support rectangle and finite Gaussian alpha integral times coverageAreaUpper and projection scale. No independent-noise assumption or overlap-density discount is applied. This is an approximation, not a PSNR/SSIM guarantee; correlated thin structures and material boundaries retain the unchanged full-image, foreground and named ROI gates. Shrinking GaussianScale never shrinks the center-error bound; at least unit scale protects center displacement.

`FSplatLODSelector::Select` returns desired and actual cuts separately. Support bounds always determine conservative visibility and screen rectangles. v3 uses nearest descendant center depth for projection/clip safety, matching shader center projection: mixed front/back or near-crossing center ranges force refinement, while support alone crossing near no longer vetoes safe centers. v1/v2 retain conservative nearest-support depth. Hysteresis is unchanged. All immediate child representations, including multipage/invisible siblings needed for replacement, must be ready before the parent is replaced. An unavailable target retains a ready ancestor; unready coarsening preserves a complete resident descendant frontier where budgets permit. Actual cuts exclude ancestor/descendant overlap.

Source slots, active Gaussians, actual Gaussian-tile entries, sort capacity and temporary GPU bytes are separate. Active sourceIndices map compact preprocess input to persistent source slots. Node-support tile-area multiplied by Gaussian count is only a scheduling hint and does not veto quality refinement. Same-frame GPU count/overflow checks enforce the actual capacity. Root-only visibility or a small nonzero proxy count does not establish the C performance gate.

`FSplatPageStreamer::Open` validates index/budgets without reading page payloads. Workers perform prioritized, deduplicated, bounded range reads/checksums/decode. CPU charges include metadata, in-flight encoded/decoded reservations, optional encoded cache, ready results, external leases and bounded terminal failures. Retry count/backoff are bounded; tombstones prevent exhausted failures restarting every frame. New scene/request generations cancel older work; Windows overlapped reads and injected delays are cancellation-aware. `SetFaultInjector` supplies verification seams without render-thread I/O.

`DrainReady` transfers ownership but keeps its CPU charge. The consumer must destroy both payload vectors before `ReleaseTransferred(pageId,generation)`. The pool moves accepted results, bounds pending payloads and exposes released identities through `DrainReleasedTransfers` only after storage destruction. Failed/rejected/stale results require the same release sequence. Cancellation/shutdown do not erase externally retained leases; reopening requires closure and a newer generation.

ProtectSelection precedes Tick. The pool protects roots, actual cuts and complete immediate sibling groups currently being requested, including ready siblings. It admits their union only if source slots fit; ideal distant desired descendants do not reserve slots. Refinement lacking transition capacity is rejected while coverage remains usable. The fixed five source slabs share Gaussian offsets; generations change on slot assignment. BuildActiveSet validates completed mappings and retains frame pins.

Recording does not make an upload GPUReady. Capture the last token before actual submit/present and compare it with the nonzero token after successful submission; only a changed token marks pool/pipeline references. Nonblocking real fence completion governs readiness and staging reclamation. Active/root/transition/in-flight/unsubmitted pages cannot be evicted. Failed submissions never mark readiness; retained staging can be re-recorded after the abandoned command buffer is reset. Ordinary frames neither wait for counts nor call device idle. Scene switch, teardown and explicit capture prove completion before releasing bindings, descriptors, source resources or deferred destruction.

## Optional current-view projected fusion (continuation implementation)

This capability is explicit and remains under real-model verification. `projectedFusion` defaults to false; its independently exposed `fusionRelativeRGBAError` defaults to 0.4. A, B and `fullLeaf` always bypass fusion. C/D/E can enable it only with a v4 manifest. These defaults and successful captures do not establish the mandatory performance or long-streaming gates.

A v4 group exists only on an immediate-leaf-parent node. Its ordinals index the concatenation of child IDs in stored order, each child's page IDs in stored order, and rows in each page. Groups exactly partition that concatenation, without duplicate or missing rows. No member references a child proxy. Original FP32 payloads and IDs remain exact. Higher-level spatial coarse representations remain available as an explicitly degraded, budget-bounded fallback. Original groups are only rearranged when every referenced leaf belongs to the actual ready cut; partial groups remain exact singleton originals. All member pages therefore use the existing active-page generation and real submission-completion pins.

Each frame first projects the actual original Gaussian covariance/opacity and current SH color through the existing preprocess shader, including its 0.3 pixel filter and current GaussianScale/clipping settings. Within a <=4-member group, order kernels by depth, expand ordered premultiplied RGBA alpha composition into signed Gaussian products, fit a pair's alpha zeroth/first/second moments, and test the whole candidate composition against the immutable original group with continuous per-channel relative L2. Greedily admit the least-error pair only within the explicit error tolerance; rejected and culled/near-mixed members remain originals. Group-relative screen coordinates protect float precision. Fitted opacity is capped at 0.99, color follows integrated premultiplied RGB, and depth stays within the contributing depth interval. This continuous kernel error is approximate: pixel cutoffs, opacity caps, external depth interleaving, correlated material edges and temporal decisions still require unchanged image/ROI gates.

Fusion writes existing projected buffers in place and zeros unused lanes. The active bound still denotes original input extent; the inclusive tile scan publishes the actual same-frame tile work for all indirect consumers. Capacity overflow retains the previous valid image. Completed `source_active`, `rendered_primitives`, `fused_primitives`, `fusion_applied` and `max_fusion_rgba_l2` samples preserve original frame IDs; a false fusion flag means primitive telemetry is unavailable, not zero rendered output. Preprocess hardware time includes fusion; WholeFrame and the existing six component stages remain measured as before.

Two completion-protected group SSBOs and the GPU counters use dedicated backing, exact physical preflight and the existing temporary cap. CPU credit explicitly reserves the three manifest membership owners and maximum descriptor/permutation working allowance before page-cache admission. This allowance is reported in metadata/CSV and reduces streamer cache credit; it is a CPU reservation, not a measured malloc or driver allocation. RSS remains a separate mandatory stress measurement. Membership augmentation reads verified leaf pages once offline and writes a new v4 manifest with unchanged page hardlinks/copies; startup reads its index without loading all leaf payloads. No camera-specific attributes are stored.

## Independent physical budgets

Vulkan preflight queries unbound buffer/image memory requirements without allocating device memory. Streaming source/staging and pipeline temporary resources request dedicated backing; measured reservations are checked after allocation. Unsupported exact allocation/completion capabilities fail initialization. No guessed alignment headroom is treated as a hard budget.

| Budget/account | Runtime meaning |
|---|---|
| sceneGPUBytes | Physical five-slab source reservation cap, including root/transition space |
| cpuCacheBytes | Charged payload/cache/lease/metadata cap; pool pending bytes are a subset |
| stagingBytes | Physical staging reservation cap; live chunk bytes are separate occupancy |
| temporaryGPUBytes | Physical preprocess/index/count/sort/render resource cap |
| outputImageBytes | Included subset of temporaryBytes, never an extra additive account |
| maxUploadBytesPerFrame | Newly submitted/replayed upload byte limit per frame |

`FBufferAllocationInfo` supplies reservationBytes, backingBytes and opaque backingId. Deduplicate shared backing IDs, especially on original PLY mode A. Logical residentBytes does not measure allocation. Global backend allocator pools and their retained slack, driver overhead and swapchain resources are separate telemetry; they must not be relabeled as source/staging/temporary reservations. Explicit capture readback must fit the remaining temporary budget. See pool/pipeline/application CodingNotes under `TestFile/3DGSLODStreaming` for exact consumer sequencing.
