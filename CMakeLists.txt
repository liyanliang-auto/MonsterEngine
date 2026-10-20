cmake_minimum_required(VERSION 3.20)
project(MonsterEngine VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release")
option(MONSTER_BUILD_ENGINE "Build native viewer/runtime" ON)
include(cmake/MonsterSources.cmake)
find_package(Threads REQUIRED)

function(monster_compile_settings target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/Include")
    target_compile_definitions(${target} PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /EHsc /W3 /MP /FS /wd4819)
        target_compile_definitions(${target} PUBLIC _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
    endif()
endfunction()

# Allocators and their process lease always travel together, including CPU-only tools.
add_library(MonsterMemoryRuntime STATIC ${MONSTER_MEMORY_SOURCES})
monster_compile_settings(MonsterMemoryRuntime)
target_link_libraries(MonsterMemoryRuntime PUBLIC Threads::Threads)
if(ANDROID)
    target_link_libraries(MonsterMemoryRuntime PUBLIC log)
endif()

if(MONSTER_BUILD_ENGINE)
    if(NOT WIN32 AND NOT ANDROID)
        message(FATAL_ERROR "Viewer supported on Windows/Android; CPU tools use MONSTER_BUILD_ENGINE=OFF")
    endif()
    set(VULKAN_LOCAL_DIR "${PROJECT_SOURCE_DIR}/3rd-party/vulkan")
    foreach(pair Core Vulkan SplatRuntime)
        if(pair STREQUAL "Core")
            set(sources ${MONSTER_CORE_SOURCES})
        elseif(pair STREQUAL "Vulkan")
            set(sources ${MONSTER_VULKAN_SOURCES})
        else()
            set(sources ${MONSTER_SPLAT_SOURCES})
        endif()
        add_library(Monster${pair} OBJECT ${sources})
        monster_compile_settings(Monster${pair})
        target_include_directories(Monster${pair} PUBLIC
            "${PROJECT_SOURCE_DIR}/3rd-party" "${VULKAN_LOCAL_DIR}"
            "${VULKAN_LOCAL_DIR}/vulkan" "${VULKAN_LOCAL_DIR}/vk_video")
        target_precompile_headers(Monster${pair} PRIVATE "${PROJECT_SOURCE_DIR}/Include/Core/MonsterEnginePCH.h")
    endforeach()
    target_link_libraries(MonsterCore PUBLIC MonsterMemoryRuntime Threads::Threads)
    target_link_libraries(MonsterVulkan PUBLIC MonsterCore)
    target_link_libraries(MonsterSplatRuntime PUBLIC MonsterCore)
    if(WIN32)
        set(GLFW_DIR "${PROJECT_SOURCE_DIR}/3rd-party/glfw-3.4.bin.WIN64")
        foreach(target MonsterCore MonsterVulkan MonsterSplatRuntime)
            target_include_directories(${target} PRIVATE "${GLFW_DIR}/Include")
        endforeach()
        # Each object is linked once. This preserves all historical Windows production dispatchers.
        add_library(MonsterWindowsShell OBJECT ${MONSTER_WINDOWS_SOURCES})
        monster_compile_settings(MonsterWindowsShell)
        target_include_directories(MonsterWindowsShell PRIVATE
            "${PROJECT_SOURCE_DIR}/3rd-party" "${PROJECT_SOURCE_DIR}/3rd-party/imgui"
            "${PROJECT_SOURCE_DIR}/3rd-party/stb" "${PROJECT_SOURCE_DIR}/3rd-party/cgltf"
            "${GLFW_DIR}/Include" "${VULKAN_LOCAL_DIR}" "${VULKAN_LOCAL_DIR}/vulkan")
        target_precompile_headers(MonsterWindowsShell PRIVATE "${PROJECT_SOURCE_DIR}/Include/Core/MonsterEnginePCH.h")
        set_source_files_properties(3rd-party/imgui/imgui.cpp 3rd-party/imgui/imgui_demo.cpp
            3rd-party/imgui/imgui_draw.cpp 3rd-party/imgui/imgui_tables.cpp
            3rd-party/imgui/imgui_widgets.cpp Source/Engine/Asset/GLTFLoader.cpp
            PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
        # main.cpp stays on the executable so formal suites reuse all other shell objects.
        list(REMOVE_ITEM MONSTER_WINDOWS_SOURCES main.cpp)
        set_property(TARGET MonsterWindowsShell PROPERTY SOURCES "${MONSTER_WINDOWS_SOURCES}")
        add_executable(MonsterEngine main.cpp)
        monster_compile_settings(MonsterEngine)
        target_link_libraries(MonsterEngine PRIVATE MonsterWindowsShell MonsterCore MonsterVulkan
            MonsterSplatRuntime MonsterMemoryRuntime "${VULKAN_LOCAL_DIR}/lib/vulkan-1.lib"
            "${GLFW_DIR}/lib-vc2022/glfw3dll.lib" opengl32)
        target_include_directories(MonsterEngine PRIVATE "${GLFW_DIR}/Include" "${VULKAN_LOCAL_DIR}")
        target_precompile_headers(MonsterEngine PRIVATE "${PROJECT_SOURCE_DIR}/Include/Core/MonsterEnginePCH.h")
        set_property(TARGET MonsterEngine PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
# Copy GLFW DLL to output directory
add_custom_command(TARGET MonsterEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GLFW_DIR}/lib-vc2022/glfw3.dll"
        $<TARGET_FILE_DIR:MonsterEngine>
    COMMENT "Copying glfw3.dll to output directory"
)

# Copy shaders directory (if exists)
# Build the receiver shaders from source; source-tree SPIR-V binaries are not
# required. Refresh the executable's shaders even when only GLSL changed.
if(SPLAT_GLSLC)
    set(MONSTER_GLSLC "${SPLAT_GLSLC}")
else()
    find_program(MONSTER_GLSLC NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" NO_CMAKE_FIND_ROOT_PATH)
endif()
if(NOT MONSTER_GLSLC)
    message(FATAL_ERROR "glslc is required for the Vulkan cube shadow shaders. Install the Vulkan SDK or set MONSTER_GLSLC.")
endif()
set(MONSTER_CUBE_SHADOW_OUTPUTS)
foreach(shader CubeLitShadow.vert CubeLitShadow.frag)
    set(shader_output "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders/${shader}.spv")
    add_custom_command(OUTPUT "${shader_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders"
        COMMAND "${MONSTER_GLSLC}" --target-env=vulkan1.0
            "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/${shader}" -o "${shader_output}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/${shader}"
        VERBATIM)
    list(APPEND MONSTER_CUBE_SHADOW_OUTPUTS "${shader_output}")
endforeach()
add_custom_target(MonsterCubeShadowShaders
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MonsterEngine>/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MONSTER_CUBE_SHADOW_OUTPUTS}
        "$<TARGET_FILE_DIR:MonsterEngine>/Shaders"
    DEPENDS ${MONSTER_CUBE_SHADOW_OUTPUTS}
    VERBATIM)
add_dependencies(MonsterEngine MonsterCubeShadowShaders)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
    add_custom_command(TARGET MonsterEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
            "$<TARGET_FILE_DIR:MonsterEngine>/Shaders"
        COMMENT "Copying shaders to output directory"
    )
    add_custom_command(TARGET MonsterEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MONSTER_CUBE_SHADOW_OUTPUTS}
            "$<TARGET_FILE_DIR:MonsterEngine>/Shaders"
        VERBATIM)
endif()

# Copy resources directory (if exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    add_custom_command(TARGET MonsterEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:MonsterEngine>/resources"
        COMMENT "Copying resources to output directory"
    )
endif()


    endif()
endif()
include(cmake/SplatLODStreaming.cmake)
include(cmake/MonsterVideoTo3D.cmake)
if(ANDROID AND MONSTER_BUILD_ENGINE)
    include(cmake/MonsterAndroid.cmake)
endif()
