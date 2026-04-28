@echo off
REM Compile FXAA shaders to SPIR-V
REM Usage: compile_fxaa.bat

echo Compiling FXAA shaders...

glslangValidator -V FXAAPass.vert -o FXAAPass.vert.spv
if %errorlevel% neq 0 (
    echo ERROR: Failed to compile FXAAPass.vert
    exit /b 1
)

glslangValidator -V FXAAPass.frag -o FXAAPass.frag.spv
if %errorlevel% neq 0 (
    echo ERROR: Failed to compile FXAAPass.frag
    exit /b 1
)

echo FXAA shaders compiled successfully!
echo   - FXAAPass.vert.spv
echo   - FXAAPass.frag.spv
