cmake_minimum_required(VERSION 3.24)
project(MonsterStudioQtHostVerification LANGUAGES CXX)
enable_testing()
get_filename_component(MONSTER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(MONSTER_STUDIO_PRODUCTION_BUILD "" CACHE PATH "Matching stage-1 production build")
if(NOT MONSTER_STUDIO_PRODUCTION_BUILD)
  message(FATAL_ERROR "Matching production build required")
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Test)
add_executable(StudioQtHostTests StudioQtHostTests.cpp
  "${MONSTER_ROOT}/Tools/MonsterStudio/Qt/NativePreviewWidget.cpp"
  "${MONSTER_ROOT}/Tools/MonsterStudio/Qt/NativePreviewWidget.h"
  "${MONSTER_ROOT}/Tools/MonsterStudio/Qt/HostValidationWindow.cpp"
  "${MONSTER_ROOT}/Tools/MonsterStudio/Qt/HostValidationWindow.h")
set_target_properties(StudioQtHostTests PROPERTIES AUTOMOC ON)
target_compile_features(StudioQtHostTests PRIVATE cxx_std_20)
target_compile_definitions(StudioQtHostTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE MONSTER_STUDIO_QT_TESTING)
target_include_directories(StudioQtHostTests PRIVATE "${MONSTER_ROOT}/Include" "${MONSTER_ROOT}/Tools/MonsterStudio/Qt")
target_compile_options(StudioQtHostTests PRIVATE /utf-8 /W4 /EHsc)
set(production_lib_dir "${MONSTER_STUDIO_PRODUCTION_BUILD}/lib")
if(EXISTS "${production_lib_dir}/Release/MonsterStudioPreview.lib")
  set(production_lib_dir "${production_lib_dir}/Release")
endif()
# MonsterStudioPreview's archive embeds its Core/Splat object-library objects.
target_link_libraries(StudioQtHostTests PRIVATE Qt6::Widgets Qt6::Test
  "${production_lib_dir}/MonsterStudioPreview.lib"
  "${production_lib_dir}/MonsterMemoryRuntime.lib"
  "${production_lib_dir}/MonsterVideoTo3DServices.lib"
  "${MONSTER_ROOT}/3rd-party/vulkan/lib/vulkan-1.lib" ole32 bcrypt shell32 user32 gdi32 uuid)
set(MONSTER_QT_TEST_RUNTIME "" CACHE PATH "New runtime evidence directory (must not already exist)")
set(MONSTER_QT_TEST_PROJECT "" CACHE PATH "Unlocked legitimately validated Completed project; read only")
if(MONSTER_QT_TEST_RUNTIME AND MONSTER_QT_TEST_PROJECT)
  add_test(NAME StudioQtHostTests COMMAND StudioQtHostTests "${MONSTER_QT_TEST_RUNTIME}" "${MONSTER_QT_TEST_PROJECT}")
  set_tests_properties(StudioQtHostTests PROPERTIES TIMEOUT 240 WORKING_DIRECTORY "${MONSTER_ROOT}")
endif()
