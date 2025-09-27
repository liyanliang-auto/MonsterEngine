@echo off
echo Compiling Triangle Shaders...

REM Compile vertex shader
glslc Triangle.vert -o Triangle.vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile vertex shader
    pause
    exit /b 1
)

REM Compile fragment shader
glslc Triangle.frag -o Triangle.frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile fragment shader
    pause
    exit /b 1
)

echo Shader compilation completed successfully!
echo Generated files:
echo   Triangle.vert.spv
echo   Triangle.frag.spv

pause
