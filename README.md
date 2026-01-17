# MonsterEngine

[![Windows](https://img.shields.io/badge/platform-Windows-blue.svg)](https://www.microsoft.com/windows)
[![Android](https://img.shields.io/badge/platform-Android-green.svg)](https://www.android.com)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/API-Vulkan-red.svg)](https://www.vulkan.org)
[![OpenGL](https://img.shields.io/badge/API-OpenGL-orange.svg)](https://www.opengl.org)

MonsterEngine is a modern 3D rendering engine built with Vulkan and OpenGL, inspired by Unreal Engine 5 architecture. It is designed to be cross-platform, high-performance, and feature-rich for real-time physically-based rendering.

**Reference**: [Unreal Engine 5](https://github.com/EpicGames/UnrealEngine) | [Google Filament](https://github.com/google/filament)

---

## Download

MonsterEngine is currently in active development. To use the engine, please build from source following the [build instructions](#building).

---

## Documentation

- **[Engine Architecture](devDocument/引擎的架构和设计.md)** - In-depth explanation of the engine architecture, RHI design, and implementation details
- **[Shader Documentation](Shaders/README.md)** - Shader system overview and shader authoring guide
- **Development Rules** - Coding standards and best practices (`.windsurf/rules/`)

---

## Features

### APIs

- **Native C++ API** for Windows and Android
- **RHI Abstraction Layer** - Unified rendering interface across multiple backends

### Backends

- **Vulkan 1.0+** for Windows and Android (primary backend)
- **OpenGL 4.5+** for Windows
- **OpenGL ES 3.0+** for Android
- Planned: **Direct3D 12** for Windows, **Metal** for macOS/iOS

### Core Architecture

- **RHI (Render Hardware Interface)** - UE5-style hardware abstraction layer
  - Platform-agnostic rendering commands
  - Automatic resource lifetime management with RAII
  - Modern command list architecture
  - Efficient pipeline state management
- **Cross-platform Support** - Windows, Android (Linux and macOS planned)
- **Modern C++20** - Using latest language features and best practices
- **Custom Memory Management** - FMemory, FMemoryManager, FMallocBinned2
- **Custom Container System** - TArray, TMap, TSet, FString, FName, FText
- **Smart Pointer System** - TSharedPtr, TUniquePtr, TWeakPtr

### Rendering

- **Physically-Based Rendering (PBR)**
  - Cook-Torrance microfacet specular BRDF
  - Lambertian diffuse BRDF
  - Metallic-roughness workflow
  - Image-based lighting (IBL) support
  - Clear coat materials
  - Normal mapping & ambient occlusion
- **Forward Rendering Pipeline**
  - Clustered forward rendering
  - Multi-pass rendering support
  - Depth pre-pass optimization
- **Lighting System**
  - Directional lights with cascaded shadow maps
  - Point lights and spot lights
  - Ambient lighting
  - Dynamic light management
  - Per-pixel lighting calculations
- **Shadow Rendering**
  - Cascaded shadow maps (CSM)
  - Shadow projection and filtering
  - Configurable shadow quality
- **Material System**
  - PBR material parameters (metallic, roughness, reflectance)
  - Texture support (base color, metallic-roughness, normal, occlusion, emissive)
  - Material instancing and caching
  - Default material fallbacks
- **Texture System**
  - 2D texture support with mipmap generation
  - Texture streaming manager
  - Virtual texture system (planned)
  - Default texture management (white, black, normal, etc.)
- **Descriptor Set Management**
  - Per-frame, per-material, per-object descriptor sets
  - Efficient descriptor pooling and caching
  - Automatic descriptor updates

### Scene Management

- **Scene Graph** - UE5-style scene representation
  - Primitive scene proxies
  - Light scene info and proxies
  - Scene visibility computation
- **Camera System**
  - Camera manager with view target blending
  - FPS camera controller
  - Camera modifiers and effects
  - Orthographic and perspective projection
- **Culling System**
  - Frustum culling
  - Occlusion culling (planned)
  - Octree spatial partitioning
- **Render Queue**
  - Mesh batch collection
  - Render command sorting
  - Multi-pass rendering support

### Shader System

- **Shader Manager** - Centralized shader compilation and caching
  - GLSL shader compilation to SPIR-V
  - Shader hot-reload support
  - Bytecode caching for fast startup
- **Shader Library**
  - PBR shading models
  - BRDF implementations
  - Lighting calculations
  - Shadow mapping utilities
  - Common shader utilities

### Platform Layer

- **Window System** - GLFW-based cross-platform windowing
  - Native surface integration for Vulkan
  - Event-driven input handling
  - Multiple window support
- **Input System**
  - Keyboard and mouse input
  - Event callbacks and polling
  - Input mapping and binding
- **Memory Management**
  - Vulkan memory manager (VMA-style)
  - GPU resource tracking
  - Memory budget management

### Editor & Tools

- **ImGui Integration** - Immediate mode GUI for debugging
  - Render statistics display
  - Material parameter editing
  - Scene hierarchy viewer
- **Shader Compiler** - Offline shader compilation tools
- **Asset Pipeline** (planned)
  - Mesh import (glTF 2.0)
  - Texture import and compression
  - Material authoring

---

## Rendering with MonsterEngine

### Windows

Create an `Engine`, initialize the RHI device, and set up your renderer:

```cpp
#include "Engine.h"
#include "RHI/RHI.h"
#include "Renderer/PBR/PBRRenderer.h"

using namespace MonsterEngine;
using namespace MonsterRender::RHI;

// Create engine
auto engine = MakeUnique<Engine>();

// Initialize RHI
RHICreateInfo rhiInfo;
rhiInfo.preferredBackend = ERHIBackend::Vulkan;
rhiInfo.enableValidation = true;
rhiInfo.applicationName = "My Application";
rhiInfo.windowWidth = 1920;
rhiInfo.windowHeight = 1080;

if (!engine->initialize(rhiInfo)) {
    // Handle error
    return false;
}

// Get RHI device
IRHIDevice* device = engine->getRHIDevice();

// Create PBR renderer
auto pbrRenderer = MakeShared<Renderer::FPBRRenderer>();
if (!pbrRenderer->initialize(device)) {
    // Handle error
    return false;
}
```

To render a frame, set up the view and render objects:

```cpp
// Begin frame
pbrRenderer->beginFrame(frameIndex);

// Set camera view
pbrRenderer->setViewMatrices(viewMatrix, projectionMatrix, cameraPosition);
pbrRenderer->setViewport(width, height);

// Set lighting
pbrRenderer->setDirectionalLight(
    Math::FVector(0.0, -1.0, -0.5),  // direction
    Math::FVector(1.0, 1.0, 0.9),     // color
    1.0f);                             // intensity

// Update per-frame buffers
pbrRenderer->updatePerFrameBuffers();

// Get command list
IRHICommandList* cmdList = device->getImmediateCommandList();
cmdList->begin();

// Bind per-frame descriptor set
pbrRenderer->bindPerFrameDescriptorSet(cmdList);

// Set pipeline state
cmdList->setPipelineState(pipelineState);

// Draw objects with materials
pbrRenderer->drawObject(cmdList, material, modelMatrix, 
                        vertexBuffer, indexBuffer, 
                        vertexCount, indexCount);

cmdList->end();

// Present
device->present();

// End frame
pbrRenderer->endFrame();
```

### Creating PBR Materials

Materials are created with the `FPBRMaterial` class:

```cpp
#include "Renderer/PBR/PBRMaterial.h"
#include "Engine/Texture/Texture2D.h"

using namespace MonsterEngine::Renderer;

// Create material
auto material = FPBRMaterial::createMetallic(
    device, 
    descriptorManager,
    Math::FVector3f(0.8f, 0.8f, 0.8f),  // base color
    0.3f);                               // roughness

// Set material parameters
material->setMetallic(1.0f);
material->setRoughness(0.2f);
material->setReflectance(0.5f);

// Load and set textures
auto baseColorTex = FTexture2D::createFromFile(device, "textures/albedo.png");
material->setBaseColorTexture(baseColorTex.get());

auto normalTex = FTexture2D::createFromFile(device, "textures/normal.png");
material->setNormalTexture(normalTex.get());

// Update GPU resources
material->updateGPUResources();
```

### Scene Management

Build a scene with primitives and lights:

```cpp
#include "Renderer/Scene.h"
#include "Engine/Actor.h"

using namespace MonsterEngine::Renderer;

// Create scene
auto scene = MakeShared<FScene>();

// Add primitive to scene
FPrimitiveSceneProxy* proxy = CreateMeshProxy(mesh, material);
scene->AddPrimitive(proxy);

// Add directional light
FLightSceneProxy* lightProxy = CreateDirectionalLightProxy(
    Math::FVector(0, -1, -0.5),  // direction
    Math::FVector(1, 1, 0.9),     // color
    1.0f);                        // intensity
scene->AddLight(lightProxy);

// Update scene
scene->UpdateAllPrimitiveSceneInfos();
```

---

## Building

### Prerequisites

**Windows:**
- Visual Studio 2022
- Vulkan SDK (latest from [LunarG](https://vulkan.lunarg.com/))
- GLFW 3.3+
- CMake 3.20+ (optional, for future cross-platform builds)

**Android:**
- Android Studio
- Android NDK r21+
- Vulkan SDK for Android

### Environment Setup

Set the following environment variables:

```batch
set VULKAN_SDK=C:\VulkanSDK\1.3.xxx.x
set GLFW_DIR=C:\Libraries\glfw-3.3.x
```

### Build Steps

**Windows:**

1. Clone the repository:
   ```bash
   git clone https://github.com/yourusername/MonsterEngine.git
   cd MonsterEngine
   ```

2. Open `MonsterEngine.sln` in Visual Studio 2022

3. Set configuration to `Debug x64` or `Release x64`

4. Build solution (Ctrl+Shift+B)

5. Run the application:
   ```bash
   .\x64\Debug\MonsterEngine.exe
   ```

**Compile Shaders:**

```bash
cd Shaders
compile_shaders.bat
```

This will compile all GLSL shaders to SPIR-V bytecode.

---

## Directory Structure

This repository contains the core MonsterEngine, supporting libraries, and tools.

- `Include/` - Public header files
  - `Core/` - Core engine systems (Log, Memory, Assert, Application)
  - `Containers/` - Custom container types (TArray, TMap, FString, FName)
  - `Math/` - Math library (Vector, Matrix, Quaternion)
  - `RHI/` - Render Hardware Interface (abstract rendering API)
  - `Platform/` - Platform-specific implementations
    - `Vulkan/` - Vulkan backend implementation
    - `OpenGL/` - OpenGL backend implementation
    - `GLFW/` - GLFW window system
  - `Renderer/` - High-level rendering systems
    - `PBR/` - Physically-based rendering
    - `Scene/` - Scene management
  - `Engine/` - Engine-level systems
    - `Camera/` - Camera management
    - `Shader/` - Shader management
    - `Texture/` - Texture management
    - `Material/` - Material system
  - `Editor/` - Editor and tools
    - `ImGui/` - ImGui integration
- `Source/` - Implementation files (mirrors Include structure)
- `Shaders/` - Shader source files
  - `Common/` - Common shader utilities (BRDF, lighting, shadows)
  - `PBR/` - PBR shaders
  - `Forward/` - Forward rendering passes
  - `Material/` - Material shaders
  - `Lighting/` - Lighting shaders
- `3rd-party/` - Third-party libraries
  - `imgui/` - Dear ImGui
  - `stb/` - STB image library
  - `cgltf/` - glTF loader
- `devDocument/` - Development documentation
- `Tests/` - Unit tests and integration tests

---

## Examples

### Triangle Rendering

```cpp
#include "TriangleRenderer.h"

class TriangleApp : public MonsterRender::Application {
    TUniquePtr<TriangleRenderer> m_renderer;
    
    void onInitialize() override {
        m_renderer = MakeUnique<TriangleRenderer>();
        m_renderer->initialize(getEngine()->getRHIDevice());
    }
    
    void onRender() override {
        auto* cmdList = getEngine()->getRHIDevice()->getImmediateCommandList();
        cmdList->begin();
        m_renderer->render(cmdList);
        cmdList->end();
        getEngine()->getRHIDevice()->present();
    }
};
```

### PBR Cube Scene

```cpp
#include "CubeSceneApplication.h"

// Run PBR cube scene with lighting and shadows
auto app = MakeUnique<CubeSceneApplication>();
app->run();
```

---

## Performance Considerations

### GPU Performance

- **Descriptor Set Pooling** - Efficient descriptor allocation and reuse
- **Command Buffer Pooling** - Minimize command buffer allocation overhead
- **Pipeline State Caching** - Cache pipeline states for fast lookup
- **Resource State Tracking** - Minimize unnecessary resource transitions

### CPU Performance

- **Multi-threaded Command Recording** - Parallel command list generation (planned)
- **Job System** - Task-based parallelism for CPU work (planned)
- **Memory Pooling** - Custom allocators for frequent allocations
- **SIMD Math** - Vectorized math operations (planned)

### Memory Performance

- **GPU Memory Manager** - VMA-style memory allocation
- **Texture Streaming** - Load textures on demand
- **Resource Budgets** - Track and manage memory usage
- **Smart Pointers** - Automatic resource cleanup

---

## Debugging

### RenderDoc Integration

Capture frames with RenderDoc:

```bash
"C:\Program Files\RenderDoc\renderdoccmd.exe" capture --working-dir "E:\MonsterEngine" "E:\MonsterEngine\x64\Debug\MonsterEngine.exe" --cube-scene
```

### Validation Layers

Enable Vulkan validation layers in debug builds:

```cpp
RHICreateInfo rhiInfo;
rhiInfo.enableValidation = true;  // Enable validation
rhiInfo.enableDebugMarkers = true; // Enable debug markers
```

### Logging

Use the logging system for debugging:

```cpp
MR_LOG(LogRenderer, Log, "Rendering frame %u", frameIndex);
MR_LOG(LogRenderer, Warning, "Material not found: %s", *materialName.ToString());
MR_LOG(LogRenderer, Error, "Failed to create pipeline state");
```

Log levels: `VeryVerbose`, `Verbose`, `Log`, `Warning`, `Error`, `Fatal`

---

## Contributing

1. Follow the [coding standards](.windsurf/rules/wind-monster-peoject-rule.md)
2. All code must compile warning-free in Visual Studio 2022
3. Test on both Vulkan and OpenGL backends
4. Document public APIs with Doxygen-style comments
5. Follow UE5 naming conventions and architecture patterns
6. Use English for all code comments and logs
7. Ensure thread safety for multi-threaded code
8. Test with RenderDoc to verify correct rendering

---

## License

This project is for educational and development purposes. Please respect all third-party library licenses.

---

## Dependencies

- **Vulkan SDK** - Graphics API ([Apache 2.0 License](https://www.apache.org/licenses/LICENSE-2.0))
- **GLFW** - Window and input library ([zlib License](https://www.glfw.org/license.html))
- **Dear ImGui** - Immediate mode GUI ([MIT License](https://github.com/ocornut/imgui/blob/master/LICENSE.txt))
- **STB Image** - Image loading ([Public Domain](https://github.com/nothings/stb))
- **cgltf** - glTF loader ([MIT License](https://github.com/jkuhlmann/cgltf))

---

## Acknowledgments

- **Unreal Engine 5** - Architecture and design inspiration
- **Google Filament** - PBR rendering and material system reference
- **Vulkan Tutorial** - Vulkan API learning resource
- **LearnOpenGL** - Graphics programming tutorials
