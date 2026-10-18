# Android release evidence checklist

Updated 2026-10-18. Unchecked items are pending. Build scripts and CI never publish to a store.

- [ ] Record actual source inventory/dirty state and pinned host/JDK/Gradle/SDK/NDK/CMake/Shader tool hashes. Preserve failed commands as well as successful output.
- [ ] Build debug/profile and release for **both** arm64-v8a and x86_64; native compilation/linking and generated shader/assets must complete. Syntax checks, Kotlin-only compilation and an old APK are insufficient.
- [ ] Run `Tools/Android/inspect_native_package.py` on the exact APK/AAB; inspect every embedded native library. Record native/asset/config/shader/APK hashes.
- [ ] For AAB, use a separately checksum-pinned bundletool to generate installation APKs, then inspect their native ELF and ZIP alignment too. AAB ZIP offsets are not APK offsets.
- [ ] Install/run the exact package on the available emulator; record ABI, emulator/translation, Vulkan loader/device/driver identity and page size. Exercise actual compute/present/GPU readback, partial diagnostics, scene import/build/package roundtrip, cancellation/recovery, lease-aware deletion and prescribed lifecycle/switch/error scenarios.
- [ ] Approve quantitative reference image thresholds and compare captured output. Nonblack/pixel smoke does not establish acceptable quality or large-scene support.
- [ ] Run Windows/shared-core regression on matching sources. Report actual exits/assertions, not historical counts.
- [ ] Separately validate actual 16 KiB runtime (`getconf PAGE_SIZE` equals 16384) with native load/render/import/build/recovery. Static ELF and ZIP checks do not close this item.
- [ ] Obtain a physical ARM device for device-specific support, memory and sustained AC17 experiments. No physical phone is available in the current session; emulator results cannot close this item.
- [ ] Replace/approve the provisional **768 MiB** development aggregate policy using real device/memory evidence. It is neither measured RAM nor a universal supported-device threshold. Track conservative WSI/driver envelopes separately from measured allocation information.
- [ ] Complete root engine licensing, exact transitive/native notices, imported-data rights and distribution review described in `THIRD_PARTY_NOTICES.md`.
- [ ] Verify schema/upgrade compatibility on preserved scenes/jobs/settings. Unknown schema or changed checkpoint identity must fail safely; use explicit source rebuild, never implicit destructive migration.
- [ ] Supply release credentials externally; same approved app ID/key, increasing version code, non-debuggable release, privacy-safe user-triggered diagnostics and current foreground-service/notification behavior.
- [ ] Verify signed AAB/APK certificate and store policy/review separately; no signing identity or store approval is established by this source change.

Local release command after supplying `MONSTER_KEYSTORE`, `MONSTER_KEYSTORE_PASSWORD`, `MONSTER_KEY_ALIAS`, `MONSTER_KEY_PASSWORD` in the environment:

```powershell
./Tools/Android/build_android.ps1 -Variant Release -Bundle -RequireSigning -VersionCode 2 -VersionName 0.1.1
```

Omitting signing variables produces an unsigned release artifact for inspection, not a releasable app. Never put credentials in tracked properties, command-line arguments, logs or source. The CI protected `android-build` environment uses secret variables and a temporary keystore; configure `VULKAN_SDK_LINUX_SHA256` to an independently approved hash for the pinned LunarG archive. CI action revisions and tool versions are locked in the workflow; Maven dependency versions are in the version catalog. Mutable remote dependency availability and future SDK changes still require validation; bit-for-bit reproducibility is not claimed.

CI requires authorized environment configuration before it can pass and only uploads build artifacts/evidence. Run/store signing, publication and review remain separate human-owned release actions.
