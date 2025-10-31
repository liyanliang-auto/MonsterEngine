# GLFW Setup Instructions

## Download and Install GLFW

1. **Download GLFW**
   - Go to https://www.glfw.org/download.html
   - Download "Windows pre-compiled binaries" (64-bit)
   - Extract to a folder like `C:\Libraries\glfw-3.3.8.bin.WIN64`

2. **Set Environment Variable**
   - Open "Environment Variables" in Windows
   - Add new system variable:
     - Variable name: `GLFW_DIR`
     - Variable value: `C:\Libraries\glfw-3.3.8.bin.WIN64` (your GLFW path)

3. **Project Configuration**
   - The project is already configured to use:
   - Include directory: `$(GLFW_DIR)\include`
   - Library directory: `$(GLFW_DIR)\lib-vc2022`
   - Library file: `glfw3.lib`

## Alternative: Manual Setup

If environment variables don't work:

1. **Edit MonsterEngine.vcxproj directly**
   - Replace `$(GLFW_DIR)\include` with your actual GLFW include path
   - Replace `$(GLFW_DIR)\lib-vc2022` with your actual GLFW library path

2. **Example paths**:
   ```xml
   <AdditionalIncludeDirectories>$(ProjectDir)Include;$(VULKAN_SDK)\Include;C:\Libraries\glfw-3.3.8.bin.WIN64\include</AdditionalIncludeDirectories>
   <AdditionalLibraryDirectories>$(VULKAN_SDK)\Lib;C:\Libraries\glfw-3.3.8.bin.WIN64\lib-vc2022</AdditionalLibraryDirectories>
   ```

## Verification

After setup, the following should compile without errors:
```cpp
#include <GLFW/glfw3.h>
```

If you still get "cannot open include file" errors:
1. Check that GLFW_DIR environment variable is set correctly
2. Restart Visual Studio after setting environment variables
3. Verify the GLFW folder structure matches the expected layout
