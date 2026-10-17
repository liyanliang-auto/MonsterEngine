# P02 Bicycle SH storage comparison

This is one SH32 -> SH16 timing pair followed by a separate validation-enabled
linear capture pair. Use the same final Release executable and deployed SPIR-V
directory for all four runs. The base config fixes A/full resident, SH3,
1920x1080, camera t=0, frozen selection, tight2, radix32, batch64=1, no fusion
and no VSync; warmup 3 seconds, sample 8 seconds, one round. Captures settle
3 seconds at the same camera. Only `shStorageMode` changes within each pair.

Run from the repository root with PowerShell:

```powershell
& ./devDocument/3DGS/GPUResidentCompression/tools/run_comparison.ps1 -Executable 'D:/path/to/Release/MonsterEngine.exe' -ShaderDirectory 'D:/path/to/Release/Shaders/Splat' -OutputRoot './artifacts/p02-final-comparison'
python ./devDocument/3DGS/GPUResidentCompression/tools/analyze_comparison.py './artifacts/p02-final-comparison/run-manifest.json' --output './devDocument/3DGS/GPUResidentCompression/comparison-summary.json'
```

Substitute the actual Release executable/shader paths. The existing `scene.mgs`
path in comparison-config.json is intentionally reused from P01/P05. An explicit
`-Config` can point to a copy with corrected absolute asset paths. The analyzer
enforces all fixed settings and compares every other config field except the
mode and timing/capture-specific output fields. OutputRoot must be new; a failed
or timed-out run remains FAILED with partial evidence and must never be relabeled
as successful. The launcher starts background processes hidden, saves commands,
timestamps, exit status and SHA256 hashes, and restores the shader environment.

The quality criterion is preregistered in run-manifest.json before execution:
normalized linear RGB MAE <= 1/255, PSNR >= 40 dB and maximum absolute error
<= 16/255, plus no visible corruption. It is an engineering criterion, not a
user tolerance. Never tune thresholds after inspecting results. Report RGB,
RGBA and alpha separately. Infinite PSNR is encoded as null + PSNRInfinite=true
to retain valid JSON. Raw captures are linear RGBA8_UNORM before sRGB present;
they cannot establish prequantization floating-point error.

The analyzer preserves mean/median/p95/sample counts for Preprocess, Raster,
WholeFrame GPU and frame interval; FPS is 1000 / mean frame interval. GPU stages
overlap WholeFrame and must not be summed. It checks raw hashes, actual runtime
identities, sample eligibility, stage joins and active counts. Tight2 has minor
pre-existing sort-count fluctuations, so sort ranges are reported without forcing
their equality. All active counts must match. Logs containing validation/VUID
or explicit error/fatal markers fail analysis; inspect complete logs as well.

Memory output separates logical SH/source bytes, actual RHI reserved/backing
bytes, queried SH requirement/alignment, upload/staging counters and sampled
engine allocator ranges. Driver/process VRAM remains explicitly unavailable.
Conversion and initial-upload CPU times overlap; do not add them. Neither is
a standalone GPU upload measurement. A single short pair has no confidence
interval and supports no cross-device or sustained-performance conclusion.

Native window evidence is a separate regression step using the computer-use
skill. Capture both complete real Bicycle windows including their live Render
FPS title bars. Never synthesize title text or crop away the native border.
Display-only or interrupted sessions are not timing samples. Label title FPS
as a short-window reading separately from paired FPS. Numeric analyzer PASS
does not satisfy visual inspection: record the real window paths, matched raw
capture views and visible-corruption judgment in the development report.

## Bicycle P03/P02 protocol

`bicycle-p03-p02-protocol.json` is the preregistered comparison contract for the SH degree and physical storage experiment. Run `run_bicycle_p03_p02.ps1` with the same executable, compiled shader directory, Bicycle scene, camera sequence, resolution, culling and sorting settings for every variant. The runner creates separate timing and capture directories for `baseline`, `p03`, and `p02`; it hashes executable, shaders and scene inputs before and after the run.

P03 changes only `shStorageDegree` (SH3 baseline to the selected SH0-SH3 degree) and leaves `projectionColorCompression=false`. P02 runs after P03 on that selected degree with `projectionColorCompression=true`; projection center and conic quantization are forced to zero and are deferred. The analyzer reports frame timing/FPS, GPU metadata and actual logical/backing byte counters when the runtime emits them. It compares matching linear RGBA8 captures using maximum absolute error, mean absolute error and changed-pixel count. Missing captures or GPU counters are `UNAVAILABLE`, never inferred.

Native screenshots are presentation evidence, separate from timed GPU samples. Use `record_window_evidence.py` with an unmodified full application window and a title containing `FPS: <value>`; a title without a numeric FPS is rejected. The resulting JSON records the screenshot hash, capture format, title FPS and method so the Chinese report can link the exact evidence.
