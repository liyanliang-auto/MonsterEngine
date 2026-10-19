# Android Vulkan viewer

Updated 2026-10-19, Beijing time. The Android client and shared native implementation now exist. This document describes source/build entrypoints, not a completed runtime acceptance report. A Xiaomi physical tablet is now available; its rendering and lifecycle acceptance require current matching evidence. Historical emulator results retain their original ABI, translation and Vulkan identity.

The supported build targets are `arm64-v8a` and `x86_64`, API 26 minimum / API 36 target. The shared device requires Vulkan 1.1; Android admission follows the selected final SPIR-V manifest plus queue/format/Surface limits and resource admission. Sort keys use two 32-bit words with lossless tile/depth ordering and do not require `shaderInt64`. Generated production shaders target Vulkan 1.1 / SPIR-V 1.3. Headless admission leaves presentation explicitly unverified until real attachment. Unsupported devices retain diagnostics. A marketing GPU name or a successful APK install cannot establish rendering support.

## Build

Use PowerShell 7, JDK 21, Gradle wrapper 8.13 (checksum in wrapper properties), AGP 8.13.2, Kotlin 2.3.10, AndroidX Core 1.17.0, SDK platform 36, Build Tools 36.1.0, NDK 30.0.16248370 and SDK CMake 3.22.1. `android/gradle/libs.versions.toml` and Gradle scripts are authoritative. No workstation path belongs in product configuration. Set `JAVA_HOME`, `ANDROID_HOME`, `SPLAT_GLSLC`, `SPLAT_SPIRV_VAL` and `Python3_EXECUTABLE` to resolved local tools. Vulkan SDK 1.3.296.0 supplies the baseline host shader tools. The script records executable hashes, so a different tool build remains distinguishable.

From the repository root:

```powershell
./Tools/Android/build_android.ps1 -Variant Debug
./Tools/Android/build_android.ps1 -Variant Profile
./Tools/Android/build_android.ps1 -Variant Release -Bundle
```

Build the host `SplatMobileLODBuilder` target first, then set `SPLAT_MOBILE_LOD_BUILDER` to its actual executable or pass `-LodBuilder <absolute-host-executable>` to the build script. The complete local `resources/point_cloud/bonsai_30k.ply` must match the identity in `resources/android/default-viewer.json`. The script derives and validates bounded LOD offline before Gradle; source/config/tool identity selects the reusable cache under `build/android-builtin-lod` (override with `MONSTER_ANDROID_BUILTIN_LOD_DIR`). Direct Gradle/CMake builds require that prepared cache. The raw 309 MB PLY is preserved outside the APK. Pages retain all 1,244,819 normalized source records; the filename does not establish its count or provenance.

Each command uses Gradle's actual `monster_android` CMake target and builds both ABIs. Android sets `MONSTER_BUILD_ENGINE=ON`; `OFF` is the separate host CPU-tools mode. The Android target links shared core/Vulkan/splat modules and static libc++, without the Windows/GLFW/OpenGL/ImGui shell. Validated SPIR-V, shader inventory and the derived LOD are prerequisites of asset merging. Native link flags request 16 KiB ELF alignment; packaging uses uncompressed native libraries.

The optional FPS overlay defaults off. After asset preparation, Gradle property `-PMONSTER_ANDROID_FPS_OVERLAY=ON` enables both native and Kotlin switches. It samples progressing submitted-frame observations every 1.5 seconds and clears continuity on pause, Surface/scene changes or stalled frames. Its value estimates CPU submit/present-return cadence, not GPU time or display refresh rate. Disabled builds schedule no dedicated FPS polling.

The packaged fresh-default quality request prioritizes detail: longest internal edge 1920, at most 2073600 pixels, up to 1000000 active Gaussians and 4500000 sort entries, a 224 MiB source GPU allowance, 24 MiB CPU streaming with one read worker, 0.1-pixel LOD threshold without hysteresis, and complete SH3 in FP32 with fusion/color compression disabled. CPU admission still checks manifest/workspace and minimum worker progress; sort storage and diagnostic screenshots share the aggregate account. Existing saved requests remain authoritative. The visible `最高画质` action explicitly saves this request while retaining the selected scene. These are bounded requests, not promised effective counts or full-leaf residency; the unchanged 768 MiB aggregate account and physical allocation preflight can refuse admission, and critical memory/thermal pressure still pauses rendering. Runtime image and budget acceptance require matching evidence.

The builtin initial/reset orbit is read from inventory-verified packaged camera metadata. Native validation checks finite eye/target vectors, a nonsingular distance/orientation, FOV, near/far planes and roll. Only the canonical builtin descriptor receives this internal preset; user scene JSON schemas stay unchanged. Roll rotates the orthonormal camera basis rather than flipping the shared projection or presentation path. Leaving the preset scene restores the generic orbit, and ordinary Windows/user-scene camera behavior is retained.

The TextureView producer retains the requested edge/pixel clamp while each Surface generation also carries the visible view dimensions to the native owner. Attach, surface/device recovery and manual retry restore this metadata; retirement clears it. Shared presentation reports actual compute (`renderWidth`/`renderHeight`), WSI (`presentationWidth`/`presentationHeight`) and visible view (`displayWidth`/`displayHeight`) dimensions. When Android magnifies an otherwise native-size producer, a bounded four-neighbor contrast correction compensates some compositor filtering without allocating a larger framebuffer. Its maximum gain is `0.22 * clamp(2 * (displayScale - 1), 0, 1)`; existing native upscaling does not receive another correction. Absent/invalid display metadata and no magnification retain zero extra gain. This cannot reconstruct missing source detail; candidate visual benefit and cost require matched MuMu captures, not source inspection alone. The shared fragment push constant is 8 bytes: attachment sRGB decode selector followed by compositor gain; both shader and native code must be rebuilt together.

Private diagnostic exports preserve typed presentation format/color space, boolean sRGB policy, scale/gain and the exact `srgb_encoded_rgb` compute-screenshot label. The owner samples existing Vulkan memory-manager statistics at most once per second: `vulkanManagedLiveBytes` is live pooled suballocations plus dedicated committed bytes; `vulkanPooledUnusedBytes` is unused pool backing; `vulkanManagedBackingBytes` is their overflow-checked sum. Unobserved values are null. These observations include host-visible memory and omit opaque WSI/driver/fallback allocations; they are neither total physical VRAM nor the aggregate admission budget. Android process PSS/RSS must be measured separately. A missing GPU system counter is unavailable, not zero.

The current builtin scene shown on MuMu contains a piano and bicycle despite its local source filename. The user confirmed optimizing this current scene. Keep builtin IDs, model bytes and saved settings intact; model provenance/redistribution remains unresolved. The pre-existing Windows visible UI Integration failure is retained under the user's explicit Android-continuation exception; it is not an Android optimization PASS or a green full-project baseline.

For optional workstation Maven mirrors, pass `-InitScript <local-init.gradle>` explicitly. This is a local dependency-access override; it is not a product repository setting. Do not commit SDK paths or signing secrets to Gradle properties.

Outputs are under `android/app/build/outputs/`; `android/build/reports/delivery/` contains command outcomes, source/tool/package identities and actual embedded `.so`/asset hashes. A successful script exit means build and static package inspection passed. An AAB needs separately generated APK inspection; an AAB ZIP is not an installed APK.

```powershell
python Tools/Android/inspect_native_package.py android/app/build/outputs/apk/debug/app-debug.apk --output android/build/reports/native-package.json
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.monsterengine.viewer.debug/com.monsterengine.viewer.MainActivity
adb shell getconf PAGE_SIZE
```

Resolve the actual `adb` and select the emulator explicitly when more than one device is connected. The package inspector checks every native ELF's ABI/PT_LOAD alignment and each APK native entry's actual local-header payload offset. These checks do not prove an actual 16 KiB process ran. On a 16 KiB system, record `PAGE_SIZE=16384`, install, load native code and exercise rendering/import/build/recovery with that same artifact identity.

## Application and recovery

`MainActivity` / `ViewerController` own UI and asynchronous operations. The existing `ViewerSurfaceView` class now uses TextureView composition, keeping Activity drawing independent of the producer's first buffer. Choreographer supplies one frame source; `AndroidRenderHost` owns native rendering on one thread. Surface/SurfaceTexture release waits for acknowledged native retirement; timeout retains ownership. The real renderer owns the window's Vulkan Surface, without a separate probe Surface on that window. A bounded lifecycle status observation also runs with FPS disabled; it is distinct from FPS sampling. Parsing/building never occurs in the Surface handshake. Shared `FSplatViewerSession` / `FSplatStreamingController` also serve the Windows ordinary D path.

Viewer file work, initialization and native retirement use one process-owned serial I/O executor. During Activity recreation, the old controller's queued work and acknowledged native retirement finish before the successor starts creation. UI callbacks enqueue and return; no UI join, budget increase or automatic creation retry hides an overlap. `NativeBridge.destroy` consumes the worker-only `nativeRetireOwner` result after actual owner shutdown, rather than treating a void JNI dispatch as retirement. A retirement failure retains the old handle/filesystem lease and blocks replacement with a restart-required error. Creation failure remains an initialization error, preserving selected scene/settings instead of reporting the scene missing. Bounded native creation phase/reason/account observations and last ownership event are stored only in private `viewer-initialization-error.json` / `viewer-owner-lifecycle.json`; raw private details are not included in public exports. The process executor intentionally survives individual controllers and is not shut down by Activity destruction.

Import uses the system document picker and private durable copies. Small valid Gaussian PLYs can open directly; large PLYs require explicit LOD construction rather than truncation. PLY supports ASCII and both binary endian encodings, SH0-3, activated log-scales/logit-opacity and normalized quaternions. Generic point clouds are not automatically Gaussian scenes. The configured builtin is an offline LOD derived from the exact external PLY; source and redistribution permission remain unverified, as recorded in the packaged `builtin-scene-license.txt`. Do not publish it until rights are resolved. Startup does not implicitly build LOD or relax the 32 MiB/100000-record direct-PLY and 64 MiB per-resource limits. Saved scene selection takes precedence over the default; unsupported settings are preserved and reported rather than silently replaced.

Background construction uses a user-started `dataSync` foreground service, notification progress and explicit pause/cancel/resume. Process/service interruption leaves only committed checkpoint work resumable. Resume is user-triggered from a visible app/notification path; no boot/background restart bypasses system policy. Device loss permits one automatic retry; further failure and critical memory/thermal pressure require explicit user retry/reopening. Deletion defers while scene leases remain live. Diagnostics and screenshots export only on user action through temporary FileProvider access.

The default aggregate account is **768 MiB, a provisional development admission policy**, not measured available RAM or a certified device threshold. Java/native/CPU/GPU reservations share it. Opaque WSI/driver overhead includes explicitly estimated envelopes; fixed GPU page slabs remain charged until actual retirement. A 256 MiB builder workspace is a controlled workspace cap, not whole-process RSS. Quality/capacity changes must pass admission; thermal quality changes and Swappy are optional measured gates, not baseline claims.

## Compatibility and upgrades

JNI/settings/scene descriptors, job UI metadata, asset inventory and diagnostic envelopes use schema 1 and reject unsupported versions. Native journal schema 1 additionally binds source/parser/builder/config/binary identities. `MonsterEngine-MobileLOD-1` writes manifest v3 / page v1 with fixed partition/reduction policies and configuration SHA256. Legacy manifests v1-5 remain subject to their exact existing builder-policy checks; acceptance by a decoder does not enable Android experimental fusion. Android defaults remain FP32 storage, original raster, radix32 and fusion off.

No automatic migration of unknown settings/checkpoints/packages is promised. Preserve original inputs/exported scenes, install only with compatible application ID/signing identity and increasing version code, then revalidate data. Rebuild incompatible unfinished jobs from the stable original source using the new binary. Changing signing keys or uninstalling can prevent upgrades or delete private state; do not use uninstall as a migration strategy. ZIP scene packages use the bounded native scene validator before publication; archive checksums alone are insufficient.

## Verification boundary

| Item | Current claim |
|---|---|
| Source integration and individual syntax/Kotlin checks | Coding evidence only; not a full linked APK or runtime result |
| Two-ABI Gradle/CI entrypoints | Implemented; inspect current verification logs for actual build exits |
| ELF/APK ZIP 16 KiB inspection | Implemented static tooling; no claim of 16 KiB runtime |
| Emulator Vulkan/image/lifecycle/build recovery | Requires matching actual run evidence |
| Physical ARM phone, 16 KiB runtime, AC17 sustained performance | Pending; no physical phone available |
| Reference image thresholds / full streaming acceptance | Pending global acceptance; pixel smoke is insufficient |
| Store signing/review/distribution | Pending external credentials, rights and review; no publication performed |

See [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the authoritative plan/spec under `devDocument/3DGS/`. Evidence must include source inventory, shader/config/native/APK hashes and observed device/driver/ABI/page size. Do not copy old test counts or performance into a new build report.
