# Android dependency and data notices

Inventory date: 2026-10-18. This inventory is a delivery checklist, not a grant of rights for the whole engine. No root engine LICENSE was found in this checkout; obtain the owner's distribution license before external release. Preserve source copyright headers and obtain complete license/NOTICE texts for the exact distributed binaries. Missing notice bundles remain a release blocker.

| Component | Use and license evidence |
|---|---|
| MonsterEngine engine/source | Repository copyright headers; overall distribution terms still require owner confirmation |
| Procedural built-in scene, generator and default config | Original mathematical data; permissive grant and provenance in `resources/android/builtin-scene-license.txt`, included in generated APK assets |
| Vulkan-Headers | Bundled headers identify Khronos Apache-2.0 via SPDX; obtain/carry complete upstream Apache license and any required NOTICE for exact revision |
| Android NDK libc++ / libc++abi / compiler runtime | Static C++ runtime contributes code to `.so`; LLVM Apache-2.0 WITH LLVM-exception and exact NDK component notices must accompany distribution |
| AndroidX Core 1.17.0 and resolved transitive AndroidX libraries | Apache-2.0; retain the resolved dependency/license inventory and complete notices |
| Kotlin standard library 2.3.10 | Apache-2.0; Gradle resolution report determines actual packaged transitive artifacts |
| Gradle 8.13 / AGP 8.13.2 / SDK 36 / Build Tools 36.1.0 / NDK 30.0.16248370 / CMake 3.22.1 | Build tools; retain exact versions, hashes and their applicable license terms; SDK components have separate Android SDK terms |
| glslc/shaderc and SPIRV-Tools | Host-only compilation/validation; SDK distribution licenses apply; generated SPIR-V identity is recorded separately |
| GLFW / Dear ImGui / stb / cgltf | Existing Windows shell dependencies; Android source inventory excludes their client implementations. GLFW zlib-style notice and ImGui MIT notice are in bundled LICENSE files; stb dual public-domain/MIT and cgltf MIT terms are embedded in headers. Do not describe these as Android-linked without inspecting the actual link map |
| Vulkan loader/driver on device | Device/platform component; not a bundled Windows `vulkan-1.lib` or private phone driver |

The procedural scene incorporates no downloaded model, photographs, trained dataset or private mesh. A user-imported PLY/scene package retains its original data license; import/build/export does not grant redistribution rights. Do not include private benchmark datasets or device identifiers in public artifacts.

Before release, collect exact resolved runtime dependencies with `android/gradlew :app:dependencies --configuration releaseRuntimeClasspath`, inspect link/package contents, and assemble required full license texts. This document does not assert that this legal/distribution review has occurred.
