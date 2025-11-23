# Rotating Textured Cube Demo - MonsterEngine

## Overview

This demo implements a rotating 3D cube with texture mapping using the MonsterEngine's Vulkan RHI (Render Hardware Interface). It closely follows the [LearnOpenGL Coordinate Systems tutorial](https://learnopengl-cn.github.io/01%20Getting%20started/08%20Coordinate%20Systems/) but adapted for Monster Engine's UE5-style architecture.

## Features

- ✅ **3D Cube Rendering** - 36 vertices (6 faces × 2 triangles × 3 vertices)
- ✅ **Texture Mapping** - Two textures blended together (container.jpg + awesomeface.png)
- ✅ **MVP Transformations** - Full Model-View-Projection matrix pipeline
- ✅ **Rotation Animation** - Smooth rotation around arbitrary axis
- ✅ **Depth Testing** - Proper 3D depth management
- ✅ **UE5-Style Architecture** - Following Unreal Engine 5 design patterns
- ✅ **Vulkan Backend** - Modern low-level graphics API

## File Structure

```
MonsterEngine/
├── Include/
│   └── CubeRenderer.h              # Cube renderer header
├── Source/
│   ├── CubeRenderer.cpp            # Cube renderer implementation
│   └── CubeApplication.cpp         # Application entry point
├── Shaders/
│   ├── Cube.vert                   # Vertex shader (GLSL)
│   ├── Cube.frag                   # Fragment shader (GLSL)
│   ├── Cube.vert.spv               # Compiled vertex shader (SPIR-V)
│   └── Cube.frag.spv               # Compiled fragment shader (SPIR-V)
└── resources/
    └── textures/
        ├── container.jpg           # First texture
        └── awesomeface.png         # Second texture
```

## Prerequisites

1. **Visual Studio 2022** with C++20 support
2. **Vulkan SDK** - Download from https://vulkan.lunarg.com/
3. **GLFW** - Already included in `3rd-party/glfw-3.4.bin.WIN64/`
4. **Texture Files** - Place the following in `resources/textures/`:
   - `container.jpg` - Container texture
   - `awesomeface.png` - Awesomeface texture

## Build Instructions

### Step 1: Compile Shaders

Before building the project, compile the GLSL shaders to SPIR-V:

```batch
cd E:\MonsterEngine
compile_cube_shaders.bat
```

This will generate:
- `Shaders/Cube.vert.spv`
- `Shaders/Cube.frag.spv`

### Step 2: Prepare Texture Assets

Copy your texture files to the resources directory:

```
E:\MonsterEngine\resources\textures\container.jpg
E:\MonsterEngine\resources\textures\awesomeface.png
```

**Note:** The current implementation creates placeholder textures. Full texture loading will be implemented in a future update.

### Step 3: Build the Project

Open `MonsterEngine.sln` in Visual Studio 2022 and build:

1. Open Visual Studio 2022
2. File → Open → Project/Solution → Select `MonsterEngine.sln`
3. Select **Debug x64** or **Release x64** configuration
4. Build → Build Solution (Ctrl+Shift+B)

Alternatively, use MSBuild from command line:

```batch
msbuild MonsterEngine.sln /p:Configuration=Debug /p:Platform=x64
```

## Running the Demo

After successful build, run the executable:

```batch
x64\Debug\MonsterEngine.exe
```

**Controls:**
- **ESC** - Exit application
- **Space** - Reserved for future features

## Technical Details

### Architecture Overview

The implementation follows UE5's RHI architecture pattern with three main layers:

```
Application Layer (CubeApplication)
        ↓
RHI Abstraction Layer (IRHIDevice, IRHICommandList)
        ↓
Vulkan Implementation (VulkanDevice, VulkanCommandList)
```

### Key Components

#### 1. CubeRenderer
- **Vertex Structure**: `CubeVertex` with position (vec3) and texture coordinates (vec2)
- **Uniform Buffer**: `CubeUniformBufferObject` containing MVP matrices
- **Matrix Math**: Custom implementation without GLM dependency
- **Animation**: Time-based rotation using `std::chrono`

#### 2. Vertex Data

36 vertices defining 6 faces of the cube:

```cpp
struct CubeVertex {
    float position[3];      // Position (x, y, z)
    float texCoord[2];      // Texture coordinates (u, v)
};
```

#### 3. MVP Matrices

```
Vertex Position → Model Matrix → View Matrix → Projection Matrix → Clip Space
```

- **Model**: Rotation transform (animated)
- **View**: Camera at (0, 0, -3) looking at origin
- **Projection**: Perspective with 45° FOV

#### 4. Shaders

**Vertex Shader (`Cube.vert`):**
- Input: Position + Texture coordinates
- Uniform: MVP matrices
- Output: Transformed position + texture coordinates

**Fragment Shader (`Cube.frag`):**
- Input: Texture coordinates
- Uniform: Two texture samplers
- Output: Blended color (80% texture1 + 20% texture2)

### Performance Characteristics

- **Vertices**: 36 (no index buffer optimization yet)
- **Draw Calls**: 1 per frame
- **Depth Testing**: Enabled (D32_FLOAT)
- **Back-face Culling**: Enabled
- **Blending**: Disabled (opaque rendering)

## Code Standards

The implementation follows the project's coding standards:

- **Naming Convention**: 
  - Classes/Structs: `PascalCase`
  - Functions/Variables: `camelCase`
  - Member variables: `m_` prefix
  - Constants: `SCREAMING_SNAKE_CASE`

- **Comments**: English, with detailed Doxygen-style documentation

- **Memory Management**: 
  - Smart pointers (`TSharedPtr`, `TUniquePtr`)
  - RAII principle throughout

## Future Enhancements

### Phase 1: Texture Loading (Priority: HIGH)
- [ ] Implement STB image loader
- [ ] Load actual texture files from disk
- [ ] Generate mipmaps
- [ ] Support multiple texture formats

### Phase 2: Advanced Features
- [ ] Index buffer for cube (reduce from 36 to 8 vertices)
- [ ] Camera controls (WASD + mouse look)
- [ ] Multiple cubes with different positions
- [ ] Lighting system (Phong/PBR)
- [ ] Normal mapping

### Phase 3: Optimization
- [ ] Frustum culling
- [ ] Instanced rendering for multiple cubes
- [ ] GPU-driven rendering
- [ ] Bindless textures

## Troubleshooting

### Shader Compilation Fails

**Error**: `glslc not found`

**Solution**: Install Vulkan SDK and ensure it's in PATH:
```
set PATH=%PATH%;C:\VulkanSDK\1.x.xxx.x\Bin
```

### Build Errors

**Error**: `Cannot open include file 'vulkan/vulkan.h'`

**Solution**: Set VULKAN_SDK environment variable:
```
set VULKAN_SDK=C:\VulkanSDK\1.x.xxx.x
```

### Runtime Errors

**Error**: `Failed to load shader: Shaders/Cube.vert.spv`

**Solution**: Run `compile_cube_shaders.bat` to generate SPIR-V files.

**Error**: `Failed to create pipeline state`

**Solution**: Check validation layers output in console for detailed error messages.

## References

- [LearnOpenGL - Coordinate Systems](https://learnopengl-cn.github.io/01%20Getting%20started/08%20Coordinate%20Systems/)
- [Vulkan Tutorial](https://vulkan-tutorial.com/)
- [Unreal Engine 5 Source Code](https://github.com/EpicGames/UnrealEngine)
- [Vulkan Specification](https://www.khronos.org/vulkan/)

## License

This demo is part of the MonsterEngine project. See main repository for license information.

## Contact

For questions or issues, please refer to the main MonsterEngine documentation:
`E:\MonsterEngine\devDocument\引擎的架构和设计.md`

---

**Last Updated**: 2025-01-XX  
**MonsterEngine Version**: 1.0  
**Vulkan Version**: 1.3+
