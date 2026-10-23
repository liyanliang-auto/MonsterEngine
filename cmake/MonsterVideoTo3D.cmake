# Isolated Windows product services; never linked into renderer or Android targets.
if(WIN32 AND NOT ANDROID)
    add_library(MonsterVideoTo3DServices STATIC
        "${PROJECT_SOURCE_DIR}/Source/VideoTo3D/WindowsProcessHost.cpp"
        "${PROJECT_SOURCE_DIR}/Source/VideoTo3D/ToolAdmission.cpp"
        "${PROJECT_SOURCE_DIR}/Source/VideoTo3D/ProjectRepository.cpp"
        "${PROJECT_SOURCE_DIR}/Source/VideoTo3D/ProjectInput.cpp"
        "${PROJECT_SOURCE_DIR}/Source/VideoTo3D/PreflightWindowsPath.cpp"
        "${PROJECT_SOURCE_DIR}/Source/VideoTo3D/ProjectPreflight.cpp"
        "${PROJECT_SOURCE_DIR}/Source/VideoTo3D/ProjectCompatibility.cpp"
        "${PROJECT_SOURCE_DIR}/Source/VideoTo3D/ProjectReconstruction.cpp"
        "${PROJECT_SOURCE_DIR}/Source/VideoTo3D/ReconstructionQueue.cpp")
    monster_compile_settings(MonsterVideoTo3DServices)
    target_link_libraries(MonsterVideoTo3DServices PRIVATE bcrypt PUBLIC ole32 shell32 uuid)

    add_executable(MonsterVideoTo3DInput "${PROJECT_SOURCE_DIR}/Tools/VideoTo3DInput/main.cpp")
    monster_compile_settings(MonsterVideoTo3DInput)
    target_link_libraries(MonsterVideoTo3DInput PRIVATE MonsterVideoTo3DServices)

    add_executable(MonsterVideoTo3DPreflight "${PROJECT_SOURCE_DIR}/Tools/VideoTo3DPreflight/main.cpp")
    monster_compile_settings(MonsterVideoTo3DPreflight)
    target_link_libraries(MonsterVideoTo3DPreflight PRIVATE MonsterVideoTo3DServices)

    add_executable(MonsterVideoTo3DReconstruction "${PROJECT_SOURCE_DIR}/Tools/VideoTo3DReconstruction/main.cpp")
    monster_compile_settings(MonsterVideoTo3DReconstruction)
    target_link_libraries(MonsterVideoTo3DReconstruction PRIVATE MonsterVideoTo3DServices)

    if(MONSTER_BUILD_ENGINE)
        enable_language(RC)
        add_library(MonsterStudioPreview STATIC
            "${PROJECT_SOURCE_DIR}/Tools/MonsterStudio/StudioPreviewHost.cpp"
            "${PROJECT_SOURCE_DIR}/Tools/MonsterStudio/StudioPreviewHost.h"
            ${MONSTER_STUDIO_VULKAN_SOURCES})
        monster_compile_settings(MonsterStudioPreview)
        target_include_directories(MonsterStudioPreview PUBLIC "${PROJECT_SOURCE_DIR}/Tools/MonsterStudio"
            "${PROJECT_SOURCE_DIR}/3rd-party" "${VULKAN_LOCAL_DIR}" "${VULKAN_LOCAL_DIR}/vulkan"
            "${VULKAN_LOCAL_DIR}/vk_video")
        target_precompile_headers(MonsterStudioPreview PRIVATE "${PROJECT_SOURCE_DIR}/Include/Core/MonsterEnginePCH.h")
        target_link_libraries(MonsterStudioPreview PUBLIC MonsterCore MonsterSplatRuntime MonsterMemoryRuntime
            "${VULKAN_LOCAL_DIR}/lib/vulkan-1.lib" user32 gdi32)
        add_dependencies(MonsterStudioPreview SplatShaders)
        add_executable(MonsterStudio WIN32
            "${PROJECT_SOURCE_DIR}/Tools/MonsterStudio/main.cpp"
            "${PROJECT_SOURCE_DIR}/Tools/MonsterStudio/StudioWindow.cpp"
            "${PROJECT_SOURCE_DIR}/Tools/MonsterStudio/StudioWindow.h"
            "${PROJECT_SOURCE_DIR}/Tools/MonsterStudio/resource.h"
            "${PROJECT_SOURCE_DIR}/Tools/MonsterStudio/MonsterStudio.rc"
            "${PROJECT_SOURCE_DIR}/Tools/MonsterStudio/MonsterStudio.manifest")
        monster_compile_settings(MonsterStudio)
        target_link_libraries(MonsterStudio PRIVATE MonsterVideoTo3DServices MonsterStudioPreview
            shell32 comctl32 user32 gdi32 uuid)
        set_property(TARGET MonsterStudio PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_dependencies(MonsterStudio SplatShaders)
    endif()

    set(admission_descriptor "${PROJECT_SOURCE_DIR}/Tools/VideoTo3D/brush-admission.json")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${admission_descriptor}")
    file(READ "${admission_descriptor}" admission_json)
    string(JSON admission_schema GET "${admission_json}" schemaVersion)
    string(JSON admission_revision GET "${admission_json}" revision)
    string(JSON admission_pause GET "${admission_json}" capabilities safePause status)
    string(JSON admission_restore GET "${admission_json}" capabilities fullCheckpointRestore status)
    string(JSON admission_probe GET "${admission_json}" cliProbe arguments 0)
    string(JSON admission_audit_status GET "${admission_json}" auditStatus)
    if(NOT admission_schema EQUAL 1 OR NOT admission_revision STREQUAL "3edecbb2fe79d3e2c87eeab85b15e0b1dd10d486")
        message(FATAL_ERROR "VideoTo3D admission descriptor requires a reviewed schema/revision update")
    endif()
    if(NOT admission_pause STREQUAL "MISSING" OR NOT admission_restore STREQUAL "MISSING"
        OR NOT admission_probe STREQUAL "--help" OR NOT admission_audit_status STREQUAL "SUPPORTED_BY_SOURCE")
        message(FATAL_ERROR "VideoTo3D audit capability changes require CLI admission review")
    endif()
    file(SHA256 "${admission_descriptor}" admission_sha256)
    execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE admission_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE admission_git_result)
    if(NOT admission_git_result EQUAL 0)
        set(admission_head "UNKNOWN")
    endif()
    string(TIMESTAMP admission_configured_utc "%Y-%m-%dT%H:%M:%SZ" UTC)
    add_executable(MonsterVideoTo3DAdmission "${PROJECT_SOURCE_DIR}/Tools/VideoTo3DAdmission/main.cpp")
    monster_compile_settings(MonsterVideoTo3DAdmission)
    target_link_libraries(MonsterVideoTo3DAdmission PRIVATE MonsterVideoTo3DServices)
    target_compile_definitions(MonsterVideoTo3DAdmission PRIVATE
        MONSTER_ADMISSION_AUDIT_SHA256="${admission_sha256}"
        MONSTER_ADMISSION_SOURCE_HEAD="${admission_head}"
        MONSTER_ADMISSION_CONFIGURED_UTC="${admission_configured_utc}"
        MONSTER_ADMISSION_BUILD_CONFIG="$<CONFIG>"
        MONSTER_ADMISSION_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
endif()
