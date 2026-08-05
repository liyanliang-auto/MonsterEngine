@echo off
REM ============================================================================
REM compile_splat.bat - Compile ALL 3DGS splat compute shaders (GLSL -> SPIR-V)
REM
REM Prerequisites: Vulkan SDK (glslc must be in PATH or VULKAN_SDK set)
REM NOTE: The CMake POST_BUILD step only COPIES the Shaders directory to the
REM       output folder; it does NOT compile GLSL. You MUST run this script
REM       after editing any .comp file, otherwise the stale .spv is used.
REM ============================================================================

setlocal enabledelayedexpansion

REM Determine project root (parent of this script's directory)
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."

REM Detect glslc
where glslc >nul 2>&1
if %errorlevel% equ 0 (
    set "GLSLC=glslc"
    goto :found
)

REM Try Vulkan SDK path
if defined VULKAN_SDK (
    set "GLSLC=%VULKAN_SDK%\Bin\glslc.exe"
    if exist "!GLSLC!" goto :found
)

echo [ERROR] glslc not found. Please install Vulkan SDK or add glslc to PATH.
exit /b 1

:found
echo Using glslc: %GLSLC%
echo.

REM ---- Compile every splat shader ----
call :compile "%SCRIPT_DIR%splat_preprocess.comp"      "%SCRIPT_DIR%compiled\splat_preprocess.spv"
call :compile "%SCRIPT_DIR%Sort\splat_assign_keys.comp" "%SCRIPT_DIR%Sort\compiled\splat_assign_keys.spv"
call :compile "%SCRIPT_DIR%Sort\splat_prefix_sum.comp"  "%SCRIPT_DIR%Sort\compiled\splat_prefix_sum.spv"
call :compile "%SCRIPT_DIR%Sort\splat_radix_histogram.comp" "%SCRIPT_DIR%Sort\compiled\splat_radix_histogram.spv"
call :compile "%SCRIPT_DIR%Sort\splat_radix_scatter.comp"  "%SCRIPT_DIR%Sort\compiled\splat_radix_scatter.spv"
call :compile "%SCRIPT_DIR%Sort\splat_tile_boundaries.comp" "%SCRIPT_DIR%Sort\compiled\splat_tile_boundaries.spv"
call :compile "%SCRIPT_DIR%Render\splat_render.comp"     "%SCRIPT_DIR%Render\compiled\splat_render.spv"

echo.
echo All splat shaders compiled successfully.
exit /b 0

REM ----------------------------------------------------------------------------
REM :compile SRC DST
REM   Compiles one compute shader, creating the output directory if needed.
REM ----------------------------------------------------------------------------
:compile
set "SRC=%~1"
set "DST=%~2"

REM Ensure output directory exists
for %%I in ("%DST%") do if not exist "%%~dpI" mkdir "%%~dpI"

"%GLSLC%" ^
    -fshader-stage=comp ^
    --target-env=vulkan1.2 ^
    -g ^
    -I "%SCRIPT_DIR%" ^
    -o "%DST%" ^
    "%SRC%"

if %errorlevel% neq 0 (
    echo [ERROR] Failed to compile %SRC%
    exit /b 1
)
echo [OK] %~nx1
goto :eof
