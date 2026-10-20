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
    "${MONSTER_ROOT}/Source/VideoTo3D/ReconstructionQueue.cpp")
target_compile_features(VideoTo3DProjectTests PRIVATE cxx_std_20)
target_compile_definitions(VideoTo3DProjectTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_include_directories(VideoTo3DProjectTests PRIVATE "${MONSTER_ROOT}/Include")
target_link_libraries(VideoTo3DProjectTests PRIVATE ole32)
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
