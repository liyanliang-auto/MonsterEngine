# Android Vulkan viewer

Updated 2026-10-18, Beijing time. The Android client and shared native implementation now exist. This document describes source/build entrypoints, not a completed runtime acceptance report. No physical phone is available in this development session; emulator results must retain their actual ABI, translation and Vulkan identity.

The supported build targets are `arm64-v8a` and `x86_64`, API 26 minimum / API 36 target. Rendering admission requires Vulkan 1.2, enabled `shaderInt64`, final SPIR-V requirements, queue/format/Surface limits and successful resource admission. Unsupported devices retain diagnostics. A marketing GPU name or a successful APK install cannot establish rendering support.

## Build

Use PowerShell 7, JDK 21, Gradle wrapper 8.13 (checksum in wrapper properties), AGP 8.13.2, Kotlin 2.3.10, AndroidX Core 1.17.0, SDK platform 36, Build Tools 36.1.0, NDK 30.0.16248370 and SDK CMake 3.22.1. `android/gradle/libs.versions.toml` and Gradle scripts are authoritative. No workstation path belongs in product configuration. Set `JAVA_HOME`, `ANDROID_HOME`, `SPLAT_GLSLC`, `SPLAT_SPIRV_VAL` and `Python3_EXECUTABLE` to resolved local tools. Vulkan SDK 1.3.296.0 supplies the baseline host shader tools. The script records executable hashes, so a different tool build remains distinguishable.

From the repository root:

```powershell
./Tools/Android/build_android.ps1 -Variant Debug
./Tools/Android/build_android.ps1 -Variant Profile
./Tools/Android/build_android.ps1 -Variant Release -Bundle
```

Each command uses Gradle's actual `monster_android` CMake target and builds both ABIs. Android sets `MONSTER_BUILD_ENGINE=ON`; `OFF` is the separate host CPU-tools mode. The Android target links shared core/Vulkan/splat modules and static libc++, without the Windows/GLFW/OpenGL/ImGui shell. Generated validated SPIR-V, shader inventory and the procedural scene are prerequisites of asset merging. Native link flags request 16 KiB ELF alignment; packaging uses uncompressed native libraries.

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

`MainActivity` / `ViewerController` own UI and asynchronous operations. `ViewerSurfaceView` sends one Choreographer source; `AndroidRenderHost` owns native rendering on one thread. Surface detach succeeds only after backend/native window retirement; timeout retains the reference and retries. Parsing/building never occurs in the Surface handshake. Shared `FSplatViewerSession` / `FSplatStreamingController` also serve the Windows ordinary D path.

Import uses the system document picker and private durable copies. Small valid Gaussian PLYs can open directly; large PLYs require explicit LOD construction rather than truncation. PLY supports ASCII and both binary endian encodings, SH0-3, activated log-scales/logit-opacity and normalized quaternions. Generic point clouds are not automatically Gaussian scenes. The bundled 49,152-Gaussian procedural SH0 scene is a small rendering demonstration, not a streaming or image-quality oracle.

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
