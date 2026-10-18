# Production CPU resource builder, generated shaders, and explicit verification hooks.
# The engine compiles its own full PLY source through the normal source inventory;
# it never links the SPLAT_CPU_ONLY variant, avoiding duplicate definitions.
find_package(Threads REQUIRED)
set(SPLAT_CPU_SOURCES ${MONSTER_CPU_SPLAT_SOURCES})
add_library(SplatLODCoreCPU STATIC ${SPLAT_CPU_SOURCES})
target_include_directories(SplatLODCoreCPU PUBLIC "${PROJECT_SOURCE_DIR}/Include")
target_compile_features(SplatLODCoreCPU PUBLIC cxx_std_20)
target_compile_definitions(SplatLODCoreCPU PRIVATE SPLAT_CPU_ONLY)
target_compile_definitions(SplatLODCoreCPU PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(SplatLODCoreCPU PUBLIC Threads::Threads MonsterMemoryRuntime)
if(MSVC)
    target_compile_options(SplatLODCoreCPU PRIVATE /utf-8 /EHsc /W3)
endif()
if(NOT ANDROID)
add_executable(SplatMobileLODBuilder Tools/SplatMobileLODBuilder/main.cpp)
target_link_libraries(SplatMobileLODBuilder PRIVATE SplatLODCoreCPU)
add_executable(SplatLODBuilder Tools/SplatLODBuilder/main.cpp)
target_link_libraries(SplatLODBuilder PRIVATE SplatLODCoreCPU)
if(MSVC)
    target_compile_options(SplatLODBuilder PRIVATE /utf-8 /EHsc /W3)
endif()

endif() # host CLI tools

option(SPLAT_BUILD_SHADERS "Build offline production shaders independently of viewer" ${MONSTER_BUILD_ENGINE})
set(SPLAT_GENERATED_SHADER_DIR "${CMAKE_BINARY_DIR}/splat-shaders" CACHE PATH "Flat generated splat shader directory")
find_program(SPLAT_GLSLC NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" NO_CMAKE_FIND_ROOT_PATH)
set(SPLAT_COMPUTE_SHADERS
    splat_preprocess.comp splat_fusion.comp splat_fusion_split_clear.comp splat_fusion_visible.comp splat_fusion_split_cheap.comp splat_fusion_queue_args.comp splat_fusion_split_exact.comp splat_fusion_split_fresh.comp Sort/splat_prefix_sum.comp Sort/splat_prepare_dispatch.comp Sort/splat_prepare_split_dispatch.comp
    Sort/splat_assign_keys.comp Sort/splat_radix_histogram.comp Sort/splat_radix_offsets.comp
    Sort/splat_radix_scatter.comp Sort/splat_tile_boundaries.comp Render/splat_render.comp)
set(SPLAT_PRESENT_SHADERS splat_present.vert splat_present.frag)
set(SPLAT_SHADER_OUTPUTS)
if(SPLAT_BUILD_SHADERS OR MONSTER_BUILD_ENGINE OR SPLAT_BUILD_GPU_TESTS OR SPLAT_BUILD_INTEGRATION_TESTS)
    if(NOT SPLAT_GLSLC)
        message(FATAL_ERROR "A host glslc is required; set SPLAT_GLSLC")
    endif()
    find_program(SPLAT_SPIRV_VAL NAMES spirv-val HINTS "$ENV{VULKAN_SDK}/Bin" NO_CMAKE_FIND_ROOT_PATH)
    if(NOT SPLAT_SPIRV_VAL)
        message(FATAL_ERROR "A host spirv-val is required; set SPLAT_SPIRV_VAL")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # Includes are traversed recursively at configure; CMAKE_CONFIGURE_DEPENDS catches edits
    # introducing new includes on all generators, including Visual Studio without DEPFILE support.
    function(splat_shader_dependencies source result)
        get_filename_component(source "${source}" ABSOLUTE)
        set(found "${source}")
        set(queue "${source}")
        while(queue)
            list(POP_FRONT queue current)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${current}")
            file(STRINGS "${current}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
            foreach(line IN LISTS includes)
                string(REGEX REPLACE "^[^<\"]*[<\"]([^>\"]+)[>\"].*$" "\\1" include "${line}")
                get_filename_component(parent "${current}" DIRECTORY)
                if(EXISTS "${parent}/${include}")
                    set(dependency "${parent}/${include}")
                elseif(EXISTS "${PROJECT_SOURCE_DIR}/Shaders/Splat/${include}")
                    set(dependency "${PROJECT_SOURCE_DIR}/Shaders/Splat/${include}")
                else()
                    message(FATAL_ERROR "Missing shader include ${include} from ${current}")
                endif()
                get_filename_component(dependency "${dependency}" ABSOLUTE)
                if(NOT dependency IN_LIST found)
                    list(APPEND found "${dependency}")
                    list(APPEND queue "${dependency}")
                endif()
            endforeach()
        endwhile()
        set(${result} "${found}" PARENT_SCOPE)
    endfunction()
    set(shaderInputJson "[")
    set(shaderInputSeparator "")
    set(allShaderDependencies)
    foreach(shader IN LISTS SPLAT_COMPUTE_SHADERS SPLAT_PRESENT_SHADERS)
        get_filename_component(name "${shader}" NAME)
        if(shader IN_LIST SPLAT_COMPUTE_SHADERS)
            get_filename_component(name "${shader}" NAME_WE)
            set(stage -fshader-stage=comp)
        else()
            set(stage)
        endif()
        set(source "${PROJECT_SOURCE_DIR}/Shaders/Splat/${shader}")
        set(output "${SPLAT_GENERATED_SHADER_DIR}/${name}.spv")
        splat_shader_dependencies("${source}" shaderDeps)
        string(APPEND shaderInputJson "${shaderInputSeparator}{\"output\":\"${name}.spv\",\"sources\":[")
        set(sourceSeparator "")
        foreach(dependency IN LISTS shaderDeps)
            file(RELATIVE_PATH logical "${PROJECT_SOURCE_DIR}" "${dependency}")
            file(TO_CMAKE_PATH "${dependency}" dependencyPath)
            string(APPEND shaderInputJson "${sourceSeparator}{\"logical\":\"${logical}\",\"file\":\"${dependencyPath}\"}")
            set(sourceSeparator ",")
        endforeach()
        string(APPEND shaderInputJson "]}")
        set(shaderInputSeparator ",")
        list(APPEND allShaderDependencies ${shaderDeps})
        add_custom_command(OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPLAT_GENERATED_SHADER_DIR}"
            COMMAND "${SPLAT_GLSLC}" --target-env=vulkan1.2 ${stage}
                -I "${PROJECT_SOURCE_DIR}/Shaders/Splat" -o "${output}" "${source}"
            COMMAND "${SPLAT_SPIRV_VAL}" --target-env vulkan1.2 "${output}"
            DEPENDS ${shaderDeps} "${SPLAT_GLSLC}" "${SPLAT_SPIRV_VAL}"
            VERBATIM COMMENT "Compile and validate splat shader ${shader}")
        list(APPEND SPLAT_SHADER_OUTPUTS "${output}")
    endforeach()
    string(APPEND shaderInputJson "]\n")
    set(shaderInputFile "${CMAKE_CURRENT_BINARY_DIR}/splat-shader-inputs.json")
    file(GENERATE OUTPUT "${shaderInputFile}" CONTENT "${shaderInputJson}")
    set(SPLAT_SHADER_MANIFEST "${SPLAT_GENERATED_SHADER_DIR}/shader-manifest.json")
    add_custom_command(OUTPUT "${SPLAT_SHADER_MANIFEST}"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/Tools/Android/shader_manifest.py"
            --shader-dir "${SPLAT_GENERATED_SHADER_DIR}" --output "${SPLAT_SHADER_MANIFEST}"
            --validator "${SPLAT_SPIRV_VAL}" --compiler "${SPLAT_GLSLC}"
            --inputs "${shaderInputFile}"
        DEPENDS ${SPLAT_SHADER_OUTPUTS} ${allShaderDependencies} "${shaderInputFile}"
            "${PROJECT_SOURCE_DIR}/Tools/Android/shader_manifest.py"
        VERBATIM)
    add_custom_target(SplatShaders DEPENDS ${SPLAT_SHADER_OUTPUTS} "${SPLAT_SHADER_MANIFEST}")
endif()
if(TARGET MonsterEngine)
    add_dependencies(MonsterEngine SplatShaders)
    file(TO_CMAKE_PATH "${SPLAT_GENERATED_SHADER_DIR}" shaderDirectory)
    target_compile_definitions(MonsterSplatRuntime PRIVATE "MONSTER_SPLAT_DEFAULT_SHADER_DIR=\"${shaderDirectory}\"")
    target_compile_definitions(MonsterWindowsShell PRIVATE "MONSTER_SPLAT_DEFAULT_SHADER_DIR=\"${shaderDirectory}\"")
endif()

# No verification source is fabricated in coding. An enabled suite requires a
# real main and assertions supplied by the project-level verification stage.
option(SPLAT_BUILD_CPU_TESTS "Build the formal CPU suite" OFF)
option(SPLAT_BUILD_GPU_TESTS "Build the formal real-device GPU suite" OFF)
option(SPLAT_BUILD_INTEGRATION_TESTS "Build the formal integration suite" OFF)
if(SPLAT_BUILD_GPU_TESTS OR SPLAT_BUILD_INTEGRATION_TESTS)
    set(SPLAT_TEST_SHADER_DIR "${CMAKE_BINARY_DIR}/splat-test-shaders")
    set(readbackSource "${PROJECT_SOURCE_DIR}/Source/Tests/3DGSLODStreaming/Shaders/storage_readback.comp")
    set(readbackOutput "${SPLAT_TEST_SHADER_DIR}/storage_readback.spv")
    add_custom_command(OUTPUT "${readbackOutput}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPLAT_TEST_SHADER_DIR}"
        COMMAND "${SPLAT_GLSLC}" --target-env=vulkan1.2 -fshader-stage=comp
            -o "${readbackOutput}" "${readbackSource}"
        DEPENDS "${readbackSource}" VERBATIM)
    set(tightCullingProbeSource "${PROJECT_SOURCE_DIR}/Source/Tests/3DGSLODStreaming/Shaders/tight_culling_probe.comp")
    set(tightCullingProbeOutput "${SPLAT_TEST_SHADER_DIR}/tight_culling_probe.spv")
    add_custom_command(OUTPUT "${tightCullingProbeOutput}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPLAT_TEST_SHADER_DIR}"
        COMMAND "${SPLAT_GLSLC}" --target-env=vulkan1.2 -fshader-stage=comp
            "-I${PROJECT_SOURCE_DIR}/Shaders/Splat"
            -o "${tightCullingProbeOutput}" "${tightCullingProbeSource}"
        DEPENDS "${tightCullingProbeSource}" "${PROJECT_SOURCE_DIR}/Shaders/Splat/splat_tight_culling.glsl"
        VERBATIM)
    add_custom_target(SplatTestShaders DEPENDS "${readbackOutput}" "${tightCullingProbeOutput}")
endif()
set(SPLAT_CPU_TEST_SOURCES "${PROJECT_SOURCE_DIR}/Source/Tests/3DGSLODStreaming/SplatLODCPUTests.cpp" CACHE STRING "CPU suite source list")
set(SPLAT_GPU_TEST_SOURCES "${PROJECT_SOURCE_DIR}/Source/Tests/3DGSLODStreaming/SplatLODGPUTests.cpp" CACHE STRING "GPU suite source list")
set(SPLAT_INTEGRATION_TEST_SOURCES "${PROJECT_SOURCE_DIR}/Source/Tests/3DGSLODStreaming/SplatLODIntegrationTests.cpp" CACHE STRING "Integration suite source list")
function(splat_require_test_sources variable)
    if(NOT ${variable})
        message(FATAL_ERROR "${variable} must name real verification sources")
    endif()
    foreach(source IN LISTS ${variable})
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "Verification source is absent: ${source}; the suite cannot be reported as passing")
        endif()
    endforeach()
endfunction()
function(splat_native_test target sources)
    if(NOT TARGET MonsterEngine)
        message(FATAL_ERROR "${target} requires MONSTER_BUILD_ENGINE=ON and real Vulkan support")
    endif()
    add_executable(${target} ${${sources}})
    monster_compile_settings(${target})
    target_link_libraries(${target} PRIVATE MonsterWindowsShell MonsterCore MonsterVulkan
        MonsterSplatRuntime MonsterMemoryRuntime "${VULKAN_LOCAL_DIR}/lib/vulkan-1.lib"
        "${GLFW_DIR}/lib-vc2022/glfw3dll.lib" opengl32)
    target_include_directories(${target} PRIVATE "${VULKAN_LOCAL_DIR}" "${GLFW_DIR}/Include")
    target_precompile_headers(${target} PRIVATE "${PROJECT_SOURCE_DIR}/Include/Core/MonsterEnginePCH.h")
    target_compile_definitions(${target} PRIVATE "MONSTER_SPLAT_DEFAULT_SHADER_DIR=\"${SPLAT_GENERATED_SHADER_DIR}\"")
    add_dependencies(${target} SplatShaders SplatTestShaders)
    target_compile_definitions(${target} PRIVATE "MONSTER_SPLAT_TEST_SHADER_DIR=\"${SPLAT_TEST_SHADER_DIR}\"")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/3rd-party/glfw-3.4.bin.WIN64/lib-vc2022/glfw3.dll"
            "$<TARGET_FILE_DIR:${target}>" VERBATIM)
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
    set_tests_properties(${target} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 2400)
endfunction()
if(SPLAT_BUILD_CPU_TESTS OR SPLAT_BUILD_GPU_TESTS OR SPLAT_BUILD_INTEGRATION_TESTS)
    enable_testing()
endif()
if(SPLAT_BUILD_CPU_TESTS)
    splat_require_test_sources(SPLAT_CPU_TEST_SOURCES)
    add_executable(SplatLODTestsCPU ${SPLAT_CPU_TEST_SOURCES})
    target_link_libraries(SplatLODTestsCPU PRIVATE SplatLODCoreCPU)
    add_test(NAME SplatLODTestsCPU COMMAND $<TARGET_FILE:SplatLODTestsCPU>)
    set_tests_properties(SplatLODTestsCPU PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endif()
if(SPLAT_BUILD_GPU_TESTS)
    splat_require_test_sources(SPLAT_GPU_TEST_SOURCES)
    splat_native_test(SplatLODTestsGPU SPLAT_GPU_TEST_SOURCES)
endif()
if(SPLAT_BUILD_INTEGRATION_TESTS)
    splat_require_test_sources(SPLAT_INTEGRATION_TEST_SOURCES)
    splat_native_test(SplatLODTestsIntegration SPLAT_INTEGRATION_TEST_SOURCES)
endif()
