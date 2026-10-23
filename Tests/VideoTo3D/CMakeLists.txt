cmake_minimum_required(VERSION 3.20)
project(VideoTo3DServiceVerification LANGUAGES CXX)
enable_testing()
get_filename_component(MONSTER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_executable(VideoTo3DServicesTests VideoTo3DServicesTests.cpp
    "${MONSTER_ROOT}/Source/VideoTo3D/WindowsProcessHost.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ToolAdmission.cpp")
target_compile_features(VideoTo3DServicesTests PRIVATE cxx_std_20)
target_compile_definitions(VideoTo3DServicesTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(VideoTo3DServicesTests PRIVATE "${MONSTER_ROOT}/Include")
target_link_libraries(VideoTo3DServicesTests PRIVATE bcrypt)
if(MSVC)
    target_compile_options(VideoTo3DServicesTests PRIVATE /utf-8 /W4)
endif()
add_test(NAME VideoTo3DServicesTests COMMAND VideoTo3DServicesTests)

add_executable(VideoTo3DProjectTests VideoTo3DProjectTests.cpp
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectRepository.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ReconstructionQueue.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectInput.cpp")
target_compile_features(VideoTo3DProjectTests PRIVATE cxx_std_20)
target_compile_definitions(VideoTo3DProjectTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_include_directories(VideoTo3DProjectTests PRIVATE "${MONSTER_ROOT}/Include")
target_link_libraries(VideoTo3DProjectTests PRIVATE ole32 bcrypt)
if(MSVC)
    target_compile_options(VideoTo3DProjectTests PRIVATE /utf-8 /W4)
endif()
add_test(NAME VideoTo3DProjectTests COMMAND VideoTo3DProjectTests)

add_executable(VideoTo3DInputTests VideoTo3DInputTests.cpp
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectRepository.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectInput.cpp")
target_compile_features(VideoTo3DInputTests PRIVATE cxx_std_20)
target_compile_definitions(VideoTo3DInputTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE MONSTER_VIDEO_TO_3D_INPUT_TEST_HOOK)
target_include_directories(VideoTo3DInputTests PRIVATE "${MONSTER_ROOT}/Include")
target_link_libraries(VideoTo3DInputTests PRIVATE ole32 bcrypt)
if(MSVC)
    target_compile_options(VideoTo3DInputTests PRIVATE /utf-8 /W4)
endif()
add_test(NAME VideoTo3DInputTests COMMAND VideoTo3DInputTests)

add_executable(VideoTo3DPreflightTests VideoTo3DPreflightTests.cpp
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectRepository.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectInput.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/PreflightWindowsPath.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectPreflight.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectCompatibility.cpp")
target_compile_features(VideoTo3DPreflightTests PRIVATE cxx_std_20)
target_compile_definitions(VideoTo3DPreflightTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_include_directories(VideoTo3DPreflightTests PRIVATE "${MONSTER_ROOT}/Include")
target_link_libraries(VideoTo3DPreflightTests PRIVATE ole32 bcrypt advapi32)
if(MSVC)
    target_compile_options(VideoTo3DPreflightTests PRIVATE /utf-8 /W4)
endif()
add_test(NAME VideoTo3DPreflightTests COMMAND VideoTo3DPreflightTests)

# Real native Studio/controller integration; private seam exists only in this target.
enable_language(RC)
add_executable(StudioImportTests StudioImportTests.cpp
    "${MONSTER_ROOT}/Tools/MonsterStudio/StudioWindow.cpp"
    "${MONSTER_ROOT}/Tools/MonsterStudio/MonsterStudio.rc"
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectRepository.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ReconstructionQueue.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectInput.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectPreflight.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/PreflightWindowsPath.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ProjectReconstruction.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/WindowsProcessHost.cpp"
    "${MONSTER_ROOT}/Source/VideoTo3D/ToolAdmission.cpp")
target_compile_features(StudioImportTests PRIVATE cxx_std_20)
target_compile_definitions(StudioImportTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE MONSTER_STUDIO_TESTING)
target_include_directories(StudioImportTests PRIVATE "${MONSTER_ROOT}/Include" "${MONSTER_ROOT}/Tools/MonsterStudio")
target_link_libraries(StudioImportTests PRIVATE ole32 bcrypt shell32 comctl32 user32 gdi32 uuid advapi32)
if(MSVC)
    target_compile_options(StudioImportTests PRIVATE /utf-8 /W4 /EHsc)
endif()
# CTest callers must explicitly configure a fresh runtime path and local LEGO source.
set(MONSTER_STUDIO_TEST_RUNTIME "" CACHE PATH "New absolute Studio test runtime directory")
if(MONSTER_STUDIO_TEST_RUNTIME)
    add_test(NAME StudioImportTests COMMAND StudioImportTests --runtime "${MONSTER_STUDIO_TEST_RUNTIME}")
    set_tests_properties(StudioImportTests PROPERTIES TIMEOUT 180)
endif()


add_executable(VideoTo3DReconstructionTests VideoTo3DReconstructionTests.cpp
 "${MONSTER_ROOT}/Source/VideoTo3D/ProjectReconstruction.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/ProjectRepository.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/ReconstructionQueue.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/ProjectInput.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/WindowsProcessHost.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/ToolAdmission.cpp")
target_compile_features(VideoTo3DReconstructionTests PRIVATE cxx_std_20)
target_compile_definitions(VideoTo3DReconstructionTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_include_directories(VideoTo3DReconstructionTests PRIVATE "${MONSTER_ROOT}/Include")
target_link_libraries(VideoTo3DReconstructionTests PRIVATE ole32 bcrypt)
if(MSVC)
 target_compile_options(VideoTo3DReconstructionTests PRIVATE /utf-8 /W4 /EHsc)
endif()
set(MONSTER_RECONSTRUCTION_TEST_RUNTIME "" CACHE PATH "Fresh reconstruction unit runtime")
if(MONSTER_RECONSTRUCTION_TEST_RUNTIME)
 add_test(NAME VideoTo3DReconstructionTests COMMAND VideoTo3DReconstructionTests "${MONSTER_RECONSTRUCTION_TEST_RUNTIME}")
endif()
add_executable(StudioReconstructionTests StudioReconstructionTests.cpp
 "${MONSTER_ROOT}/Tools/MonsterStudio/StudioWindow.cpp"
 "${MONSTER_ROOT}/Tools/MonsterStudio/MonsterStudio.rc"
 "${MONSTER_ROOT}/Source/VideoTo3D/ProjectRepository.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/ReconstructionQueue.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/ProjectInput.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/ProjectPreflight.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/PreflightWindowsPath.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/ProjectReconstruction.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/WindowsProcessHost.cpp"
 "${MONSTER_ROOT}/Source/VideoTo3D/ToolAdmission.cpp")
target_compile_features(StudioReconstructionTests PRIVATE cxx_std_20)
target_compile_definitions(StudioReconstructionTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE MONSTER_STUDIO_TESTING)
target_include_directories(StudioReconstructionTests PRIVATE "${MONSTER_ROOT}/Include" "${MONSTER_ROOT}/Tools/MonsterStudio")
target_link_libraries(StudioReconstructionTests PRIVATE ole32 bcrypt shell32 comctl32 user32 gdi32 uuid advapi32)
if(MSVC)
 target_compile_options(StudioReconstructionTests PRIVATE /utf-8 /W4 /EHsc)
endif()

# Link the exact freshly built production renderer; no fake GPU implementation.
set(MONSTER_STUDIO_PRODUCTION_BUILD "" CACHE PATH "Matching production build containing MonsterStudioPreview")
if(MONSTER_STUDIO_PRODUCTION_BUILD)
    add_executable(VideoTo3DPreviewMetadataTests VideoTo3DPreviewMetadataTests.cpp)
    target_compile_features(VideoTo3DPreviewMetadataTests PRIVATE cxx_std_20)
    target_compile_definitions(VideoTo3DPreviewMetadataTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_include_directories(VideoTo3DPreviewMetadataTests PRIVATE "${MONSTER_ROOT}/Include")
    target_link_libraries(VideoTo3DPreviewMetadataTests PRIVATE
        "${MONSTER_STUDIO_PRODUCTION_BUILD}/lib/Release/MonsterVideoTo3DServices.lib" ole32 bcrypt)
    if(MSVC)
        target_compile_options(VideoTo3DPreviewMetadataTests PRIVATE /utf-8 /W4 /EHsc)
    endif()
    set(preview_libs
        "${MONSTER_STUDIO_PRODUCTION_BUILD}/lib/Release/MonsterStudioPreview.lib"
        "${MONSTER_STUDIO_PRODUCTION_BUILD}/MonsterSplatRuntime.dir/Release/MonsterSplatRuntime.lib"
        "${MONSTER_STUDIO_PRODUCTION_BUILD}/MonsterCore.dir/Release/MonsterCore.lib"
        "${MONSTER_STUDIO_PRODUCTION_BUILD}/lib/Release/MonsterMemoryRuntime.lib"
        "${MONSTER_ROOT}/3rd-party/vulkan/lib/vulkan-1.lib")
    foreach(studio_test StudioImportTests StudioReconstructionTests)
        target_link_libraries(${studio_test} PRIVATE ${preview_libs})
    endforeach()
    add_executable(StudioPreviewTests StudioPreviewTests.cpp
        "${MONSTER_ROOT}/Tools/MonsterStudio/StudioWindow.cpp"
        "${MONSTER_ROOT}/Tools/MonsterStudio/MonsterStudio.rc")
    target_compile_features(StudioPreviewTests PRIVATE cxx_std_20)
    target_compile_definitions(StudioPreviewTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE MONSTER_STUDIO_TESTING)
    target_include_directories(StudioPreviewTests PRIVATE "${MONSTER_ROOT}/Include" "${MONSTER_ROOT}/Tools/MonsterStudio")
    target_link_libraries(StudioPreviewTests PRIVATE ${preview_libs}
        "${MONSTER_STUDIO_PRODUCTION_BUILD}/lib/Release/MonsterVideoTo3DServices.lib"
        ole32 bcrypt shell32 comctl32 user32 gdi32 uuid advapi32 d3d11 dxgi)
    if(MSVC)
        target_compile_options(StudioPreviewTests PRIVATE /utf-8 /W4 /EHsc)
    endif()
endif()
if(MONSTER_STUDIO_PRODUCTION_BUILD)
 add_executable(StudioPresetTests StudioPresetTests.cpp "${MONSTER_ROOT}/Tools/MonsterStudio/StudioWindow.cpp" "${MONSTER_ROOT}/Tools/MonsterStudio/MonsterStudio.rc")
 target_compile_features(StudioPresetTests PRIVATE cxx_std_20)
 target_compile_definitions(StudioPresetTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE MONSTER_STUDIO_TESTING)
 target_include_directories(StudioPresetTests PRIVATE "${MONSTER_ROOT}/Include" "${MONSTER_ROOT}/Tools/MonsterStudio")
 target_link_libraries(StudioPresetTests PRIVATE ${preview_libs} "${MONSTER_STUDIO_PRODUCTION_BUILD}/lib/Release/MonsterVideoTo3DServices.lib" ole32 bcrypt shell32 comctl32 user32 gdi32 uuid advapi32 d3d11 dxgi)
 if(MSVC)
 target_compile_options(StudioPresetTests PRIVATE /utf-8 /W4 /EHsc)
 endif()
endif()
if(MONSTER_STUDIO_PRODUCTION_BUILD)
 add_executable(VideoTo3DPresetPublicationTests VideoTo3DPresetPublicationTests.cpp)
 target_compile_features(VideoTo3DPresetPublicationTests PRIVATE cxx_std_20)
 target_compile_definitions(VideoTo3DPresetPublicationTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
 target_include_directories(VideoTo3DPresetPublicationTests PRIVATE "${MONSTER_ROOT}/Include")
 target_link_libraries(VideoTo3DPresetPublicationTests PRIVATE "${MONSTER_STUDIO_PRODUCTION_BUILD}/lib/Release/MonsterVideoTo3DServices.lib" ole32 shell32 uuid bcrypt)
 target_compile_options(VideoTo3DPresetPublicationTests PRIVATE /utf-8 /EHsc)
endif()
