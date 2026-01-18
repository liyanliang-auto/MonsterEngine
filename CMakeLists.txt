# ==============================================================================
# MonsterEngine - CMake Build Configuration (Windows)
# ==============================================================================
# Minimum CMake version required
cmake_minimum_required(VERSION 3.20)

# Project definition
project(MonsterEngine 
    VERSION 1.0.0
    DESCRIPTION "Modern 3D Rendering Engine with Vulkan and OpenGL support"
    LANGUAGES CXX C
)

# ==============================================================================
# Build Configuration
# ==============================================================================

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C Standard
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Configuration-specific output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug or Release)" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMake version: ${CMAKE_VERSION}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ==============================================================================
# Find Dependencies
# ==============================================================================

# Vulkan SDK (required)
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")

# ==============================================================================
# Source Files
# ==============================================================================

# Main entry point
set(MAIN_SOURCES
    main.cpp
)

# Core module
file(GLOB_RECURSE CORE_SOURCES 
    "Source/Core/*.cpp"
)

# Remove PCH creation file (will be handled separately)
list(REMOVE_ITEM CORE_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/MonsterEnginePCH.cpp"
)

# Container module
file(GLOB_RECURSE CONTAINER_SOURCES 
    "Source/Containers/*.cpp"
)

# RHI module
file(GLOB_RECURSE RHI_SOURCES 
    "Source/RHI/*.cpp"
)

# Platform module
file(GLOB_RECURSE PLATFORM_SOURCES 
    "Source/Platform/*.cpp"
)

# Renderer module
file(GLOB_RECURSE RENDERER_SOURCES 
    "Source/Renderer/*.cpp"
)

# Engine module
file(GLOB_RECURSE ENGINE_SOURCES 
    "Source/Engine/*.cpp"
)

# Editor module
file(GLOB_RECURSE EDITOR_SOURCES 
    "Source/Editor/*.cpp"
)

# RDG (Render Dependency Graph) module
file(GLOB_RECURSE RDG_SOURCES 
    "Source/RDG/*.cpp"
)

# Test module
file(GLOB_RECURSE TEST_SOURCES 
    "Source/Tests/*.cpp"
)

# ImGui third-party library
file(GLOB IMGUI_SOURCES 
    "3rd-party/imgui/*.cpp"
)

# Combine all sources
set(ALL_SOURCES
    ${MAIN_SOURCES}
    ${CORE_SOURCES}
    ${CONTAINER_SOURCES}
    ${RHI_SOURCES}
    ${PLATFORM_SOURCES}
    ${RENDERER_SOURCES}
    ${ENGINE_SOURCES}
    ${EDITOR_SOURCES}
    ${RDG_SOURCES}
    ${TEST_SOURCES}
    ${IMGUI_SOURCES}
)

# Header files (for IDE project organization)
file(GLOB_RECURSE ALL_HEADERS 
    "Include/*.h"
)

message(STATUS "Total source files: ${CMAKE_CURRENT_LIST_LENGTH}")

# ==============================================================================
# Executable Target
# ==============================================================================

add_executable(MonsterEngine 
    ${ALL_SOURCES}
    ${ALL_HEADERS}
)

# ==============================================================================
# Include Directories
# ==============================================================================

target_include_directories(MonsterEngine PRIVATE
    # Engine headers
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    
    # Third-party libraries
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd-party
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd-party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd-party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd-party/cgltf
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd-party/glfw-3.4.bin.WIN64/Include
    
    # Vulkan SDK
    ${Vulkan_INCLUDE_DIRS}
)

# ==============================================================================
# Precompiled Headers (PCH)
# ==============================================================================

# Set precompiled header for the target
target_precompile_headers(MonsterEngine PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/Include/Core/MonsterEnginePCH.h>"
)

# Disable PCH for specific files
set_source_files_properties(
    # ImGui sources (third-party code)
    ${IMGUI_SOURCES}
    
    # GLTFLoader (has specific requirements)
    "Source/Engine/Asset/GLTFLoader.cpp"
    
    PROPERTIES 
        SKIP_PRECOMPILE_HEADERS ON
        COMPILE_FLAGS ""
)

message(STATUS "Precompiled headers configured")

# ==============================================================================
# Compiler Options
# ==============================================================================

if(MSVC)
    target_compile_options(MonsterEngine PRIVATE
        /W3                     # Warning level 3
        /MP                     # Multi-processor compilation
        /FS                     # Force synchronous PDB writes
        /wd4819                 # Disable warning 4819 (code page issue)
        
        # Debug configuration
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Debug>:/Zi>
        $<$<CONFIG:Debug>:/Od>
        
        # Release configuration
        $<$<CONFIG:Release>:/MD>
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Ob2>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/GL>
    )
    
    # Preprocessor definitions
    target_compile_definitions(MonsterEngine PRIVATE
        # Platform
        PLATFORM_WINDOWS=1
        WIN32
        _CONSOLE
        UNICODE
        _UNICODE
        
        # Configuration-specific
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
    )
    
    # Linker options
    target_link_options(MonsterEngine PRIVATE
        /SUBSYSTEM:CONSOLE
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:Debug>:/INCREMENTAL>
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
    
    message(STATUS "MSVC compiler options configured")
endif()

# ==============================================================================
# Link Libraries
# ==============================================================================

# GLFW library directories
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rd-party/glfw-3.4.bin.WIN64")
target_link_directories(MonsterEngine PRIVATE 
    "${GLFW_DIR}/lib-vc2022"
)

# Link libraries
target_link_libraries(MonsterEngine PRIVATE
    # Vulkan
    Vulkan::Vulkan
    
    # GLFW
    glfw3dll
    
    # Windows system libraries
    opengl32
)

message(STATUS "Libraries linked successfully")

# ==============================================================================
# Post-Build Commands
# ==============================================================================

# Copy GLFW DLL to output directory
add_custom_command(TARGET MonsterEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GLFW_DIR}/lib-vc2022/glfw3.dll"
        $<TARGET_FILE_DIR:MonsterEngine>
    COMMENT "Copying glfw3.dll to output directory"
)

# Copy shaders directory (if exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
    add_custom_command(TARGET MonsterEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
            "$<TARGET_FILE_DIR:MonsterEngine>/Shaders"
        COMMENT "Copying shaders to output directory"
    )
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

# ==============================================================================
# IDE Organization (Visual Studio Filters)
# ==============================================================================

# Group source files by module
source_group("Source Files\\Core" FILES ${CORE_SOURCES})
source_group("Source Files\\Containers" FILES ${CONTAINER_SOURCES})
source_group("Source Files\\RHI" FILES ${RHI_SOURCES})
source_group("Source Files\\Platform" FILES ${PLATFORM_SOURCES})
source_group("Source Files\\Renderer" FILES ${RENDERER_SOURCES})
source_group("Source Files\\Engine" FILES ${ENGINE_SOURCES})
source_group("Source Files\\Editor" FILES ${EDITOR_SOURCES})
source_group("Source Files\\RDG" FILES ${RDG_SOURCES})
source_group("Source Files\\Tests" FILES ${TEST_SOURCES})
source_group("Source Files\\ThirdParty\\ImGui" FILES ${IMGUI_SOURCES})
source_group("Source Files" FILES ${MAIN_SOURCES})

# Group header files
source_group("Header Files" FILES ${ALL_HEADERS})

# ==============================================================================
# Build Summary
# ==============================================================================

message(STATUS "")
message(STATUS "========================================")
message(STATUS "MonsterEngine Build Configuration")
message(STATUS "========================================")
message(STATUS "Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:      C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:          ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Vulkan SDK:        ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Output directory:  ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "========================================")
message(STATUS "")
