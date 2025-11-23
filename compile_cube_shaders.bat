@echo off
REM Compile Cube Shaders to SPIR-V
REM Requires Vulkan SDK to be installed

echo ================================================
echo Compiling Cube Shaders to SPIR-V
echo ================================================

REM Check if glslc is available
where glslc >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: glslc not found. Please install Vulkan SDK.
    echo Download from: https://vulkan.lunarg.com/
    pause
    exit /b 1
)

REM Create output directory if it doesn't exist
if not exist "Shaders" mkdir Shaders

echo.
echo Compiling Cube.vert...
glslc -fshader-stage=vert Shaders\Cube.vert -o Shaders\Cube.vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile Cube.vert
    pause
    exit /b 1
)
echo SUCCESS: Cube.vert compiled to Cube.vert.spv

echo.
echo Compiling Cube.frag...
glslc -fshader-stage=frag Shaders\Cube.frag -o Shaders\Cube.frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile Cube.frag
    pause
    exit /b 1
)
echo SUCCESS: Cube.frag compiled to Cube.frag.spv

echo.
echo ================================================
echo All cube shaders compiled successfully!
echo ================================================
echo.

pause
