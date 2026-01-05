@echo off
REM ============================================================================
REM compile_pbr_shaders.bat - Compile PBR shaders to SPIR-V
REM ============================================================================
REM Copyright Monster Engine. All Rights Reserved.
REM
REM Requires Vulkan SDK with glslc compiler
REM ============================================================================

echo Compiling PBR shaders...

REM Set paths
set GLSLC=%VULKAN_SDK%\Bin\glslc.exe
set SHADER_DIR=%~dp0

REM Check if glslc exists
if not exist "%GLSLC%" (
    echo ERROR: glslc not found at %GLSLC%
    echo Please ensure Vulkan SDK is installed and VULKAN_SDK environment variable is set.
    exit /b 1
)

REM Compile vertex shader
echo Compiling PBR.vert...
"%GLSLC%" -fshader-stage=vertex "%SHADER_DIR%PBR.vert" -o "%SHADER_DIR%PBR.vert.spv"
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to compile PBR.vert
    exit /b 1
)

REM Compile fragment shader
echo Compiling PBR.frag...
"%GLSLC%" -fshader-stage=fragment "%SHADER_DIR%PBR.frag" -o "%SHADER_DIR%PBR.frag.spv"
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to compile PBR.frag
    exit /b 1
)

echo.
echo PBR shader compilation successful!
echo Output files:
echo   - PBR.vert.spv
echo   - PBR.frag.spv
echo.

exit /b 0
