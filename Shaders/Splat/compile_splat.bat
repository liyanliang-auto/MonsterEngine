@echo off
setlocal enabledelayedexpansion
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
REM Generated output stays outside source. The runtime uses MONSTER_SPLAT_SHADER_DIR.
set "OUTPUT_DIR=%~1"
if not defined OUTPUT_DIR set "OUTPUT_DIR=%PROJECT_ROOT%\TestFile\3DGSLODStreaming\pipeline\shaders"
where glslc >nul 2>&1
if %errorlevel% equ 0 (
    set "GLSLC=glslc"
) else (
    if not defined VULKAN_SDK exit /b 1
    set "GLSLC=%VULKAN_SDK%\Bin\glslc.exe"
    if not exist "!GLSLC!" exit /b 1
)
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if not exist "%OUTPUT_DIR%" exit /b 1
for %%S in (splat_preprocess.comp splat_fusion.comp splat_fusion_split_clear.comp splat_fusion_visible.comp splat_fusion_split_cheap.comp splat_fusion_queue_args.comp splat_fusion_split_exact.comp splat_fusion_split_fresh.comp Sort\splat_prefix_sum.comp Sort\splat_prepare_dispatch.comp Sort\splat_prepare_split_dispatch.comp Sort\splat_assign_keys.comp Sort\splat_radix_histogram.comp Sort\splat_radix_offsets.comp Sort\splat_radix_scatter.comp Sort\splat_tile_boundaries.comp Render\splat_render.comp) do (
    "%GLSLC%" --target-env=vulkan1.1 -fshader-stage=comp -I "%SCRIPT_DIR%." -o "%OUTPUT_DIR%\%%~nS.spv" "%SCRIPT_DIR%%%S"
    if errorlevel 1 exit /b 1
    echo [OK] %%S
)
for %%S in (splat_present.vert splat_present.frag) do (
    "%GLSLC%" --target-env=vulkan1.1 -o "%OUTPUT_DIR%\%%S.spv" "%SCRIPT_DIR%%%S"
    if errorlevel 1 exit /b 1
    echo [OK] %%S
)
echo Set MONSTER_SPLAT_SHADER_DIR=%OUTPUT_DIR%
exit /b 0
