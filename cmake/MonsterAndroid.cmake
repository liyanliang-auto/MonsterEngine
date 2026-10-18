# One JNI shared object owns the process runtime and statically linked libc++.
if(NOT ANDROID_STL STREQUAL "c++_static")
    message(FATAL_ERROR "monster_android requires -DANDROID_STL=c++_static")
endif()
if(NOT ANDROID_ABI MATCHES "^(arm64-v8a|x86_64)$")
    message(FATAL_ERROR "Supported Android ABIs: arm64-v8a, x86_64")
endif()
set(MONSTER_ANDROID_ASSET_DIR "${CMAKE_BINARY_DIR}/android-assets" CACHE PATH "Generated APK asset root")
set(MONSTER_ANDROID_ABI_ASSET_DIR "${MONSTER_ANDROID_ASSET_DIR}/${ANDROID_ABI}")
add_library(monster_android SHARED ${MONSTER_ANDROID_SOURCES})
monster_compile_settings(monster_android)
target_include_directories(monster_android PRIVATE "${VULKAN_LOCAL_DIR}" "${PROJECT_SOURCE_DIR}/3rd-party")
target_link_libraries(monster_android PRIVATE MonsterCore MonsterVulkan MonsterSplatRuntime
    MonsterMemoryRuntime android log dl vulkan Threads::Threads)
target_link_options(monster_android PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384" "-Wl,--no-undefined")
# APK assets use logical names; no host shader directory is compiled into Android.
set(MONSTER_ASSET_MANIFEST "${MONSTER_ANDROID_ABI_ASSET_DIR}/asset-manifest.json")
set(androidAssetByproducts
    "${MONSTER_ANDROID_ABI_ASSET_DIR}/scenes/builtin.ply"
    "${MONSTER_ANDROID_ABI_ASSET_DIR}/resources/android/default-viewer.json"
    "${MONSTER_ANDROID_ABI_ASSET_DIR}/resources/android/builtin-scene-license.txt"
    "${MONSTER_ANDROID_ABI_ASSET_DIR}/Shaders/Splat/shader-manifest.json")
foreach(shader IN LISTS SPLAT_SHADER_OUTPUTS)
    get_filename_component(shaderName "${shader}" NAME)
    list(APPEND androidAssetByproducts "${MONSTER_ANDROID_ABI_ASSET_DIR}/Shaders/Splat/${shaderName}")
endforeach()
add_custom_command(OUTPUT "${MONSTER_ASSET_MANIFEST}"
    BYPRODUCTS ${androidAssetByproducts}
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/Tools/Android/prepare_assets.py"
        --source-root "${PROJECT_SOURCE_DIR}" --shader-dir "${SPLAT_GENERATED_SHADER_DIR}"
        --output "${MONSTER_ANDROID_ABI_ASSET_DIR}"
    DEPENDS "${SPLAT_SHADER_MANIFEST}" ${SPLAT_SHADER_OUTPUTS}
        "${PROJECT_SOURCE_DIR}/Tools/Android/prepare_assets.py"
        "${PROJECT_SOURCE_DIR}/Tools/Android/generate_builtin_scene.py"
        "${PROJECT_SOURCE_DIR}/resources/android/default-viewer.json"
    VERBATIM)
add_custom_target(AndroidAssets DEPENDS "${MONSTER_ASSET_MANIFEST}")
add_dependencies(AndroidAssets SplatShaders)
add_dependencies(monster_android AndroidAssets)
