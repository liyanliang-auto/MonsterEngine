@echo off
REM ============================================================================
REM Deferred Rendering Shader Compile Script
REM Compiles 4 GLSL files into SPIR-V (.spv) for Vulkan backend.
REM
REM Dependency: Vulkan SDK's glslc (must be on PATH)
REM Usage: Double-click to run, or invoke from command line
REM ============================================================================

setlocal enabledelayedexpansion

REM Switch to this script's directory
cd /d "%~dp0"

echo.
echo ==========================================================
echo  Compiling Deferred Rendering Shaders (GLSL -^> SPIR-V)
echo ==========================================================
echo.

set SHADERS=GeometryPass.vert GeometryPass.frag LightingPass.vert LightingPass.frag
set HAS_ERROR=0

for %%S in (%SHADERS%) do (
    echo [Compile] %%S
    glslc "%%S" -o "%%S.spv"
    if !ERRORLEVEL! NEQ 0 (
        echo   [FAILED] %%S
        set HAS_ERROR=1
    ) else (
        echo   [OK]     %%S.spv
    )
    echo.
)

if %HAS_ERROR% NEQ 0 (
    echo ==========================================================
    echo  Shader compilation FAILED! Check errors above.
    echo ==========================================================
    exit /b 1
)

echo ==========================================================
echo  All Deferred shaders compiled successfully!
echo ==========================================================
echo.
echo Generated files:
echo   GeometryPass.vert.spv
echo   GeometryPass.frag.spv
echo   LightingPass.vert.spv
echo   LightingPass.frag.spv
echo.

endlocal
