# MonsterRender Engine

A modern 3D rendering engine built with Vulkan and inspired by Unreal Engine 5 architecture.

## Features

### Core Architecture
- **RHI (Render Hardware Interface)** - Abstract rendering API layer supporting multiple backends
- **Cross-platform Window System** - GLFW-based windowing with native surface integration  
- **Modern C++20** - Using latest language features and best practices
- **UE5-inspired Design** - Following Unreal Engine 5 architectural patterns

### Rendering
- **Vulkan Backend** - Primary rendering backend with full Vulkan API support
- **Dynamic Function Loading** - Runtime Vulkan function loading for optimal performance
- **Surface Integration** - Native window surface creation for all supported platforms
- **Command List Architecture** - Modern command buffer abstraction

### Window & Input System
- **Cross-platform Windowing** - Support for Windows, Linux, and Android
- **Event-driven Input** - Comprehensive keyboard and mouse input handling
- **Callback System** - UE5-style delegate pattern for event handling
- **Multiple Input Modes** - Support for immediate and event-based input queries

### Application Framework  
- **Application Class** - High-level application lifecycle management
- **Main Loop** - Integrated render loop with timing and FPS tracking
- **Event Processing** - Unified window and input event processing
- **Resource Management** - RAII-based automatic resource cleanup

## Building

### Prerequisites

1. **Visual Studio 2022** (Windows)
2. **Vulkan SDK** - Latest version from LunarG
3. **GLFW** - Window and input library

### Environment Setup

Set the following environment variables:
- `VULKAN_SDK` - Path to Vulkan SDK installation
- `GLFW_DIR` - Path to GLFW installation

### Build Steps

1. Clone the repository
2. Open `MonsterEngine.sln` in Visual Studio 2022
3. Set configuration to `Debug x64` or `Release x64`
4. Build solution (Ctrl+Shift+B)

### Project Structure

```
MonsterEngine/
├── Include/                 # Public headers
│   ├── Core/               # Core engine systems
│   ├── Platform/           # Platform-specific implementations
│   └── RHI/               # Render Hardware Interface
├── Source/                 # Implementation files
│   ├── Core/              # Core systems implementation
│   ├── Platform/          # Platform implementations
│   └── RHI/               # RHI implementation
└── main.cpp               # Application entry point
```

## Usage

### Basic Application

```cpp
#include "Core/Application.h"

class MyApplication : public MonsterRender::Application {
public:
    void onInitialize() override {
        // Initialize your application
    }
    
    void onUpdate(float32 deltaTime) override {
        // Update game logic
    }
    
    void onRender() override {
        // Render frame
    }
    
    void onKeyPressed(EKey key) override {
        if (key == EKey::Escape) {
            requestExit();
        }
    }
};

// Required: Application creation function
TUniquePtr<MonsterRender::Application> MonsterRender::createApplication() {
    return MakeUnique<MyApplication>();
}
```

### Window Management

```cpp
// Window configuration
WindowProperties props;
props.title = "My Game";
props.width = 1920;
props.height = 1080;
props.fullscreen = false;
props.resizable = true;

// Create window through factory
auto window = WindowFactory::createWindow(props);
```

### Input Handling

```cpp
// In your application class
void onKeyPressed(EKey key) override {
    switch (key) {
        case EKey::W: moveForward(); break;
        case EKey::S: moveBackward(); break;
        case EKey::A: moveLeft(); break; 
        case EKey::D: moveRight(); break;
    }
}

void onMouseMoved(const MousePosition& position) override {
    updateCamera(position);
}
```

### RHI Usage

```cpp
// Get RHI device
auto* device = getEngine()->getRHIDevice();

// Create render resources
auto vertexBuffer = device->createBuffer(bufferDesc);
auto shader = device->createShader(shaderDesc);
auto pipelineState = device->createPipelineState(pipelineDesc);

// Record commands
auto* cmdList = device->getImmediateCommandList();
cmdList->begin();
cmdList->setPipelineState(pipelineState);
cmdList->setVertexBuffers(0, {vertexBuffer});
cmdList->draw(vertexCount);
cmdList->end();

// Present frame
device->present();
```

## Architecture

### RHI Layer
The Render Hardware Interface provides a unified API across different graphics backends:

- **Abstract Interfaces** - Platform-agnostic rendering commands
- **Vulkan Implementation** - Full Vulkan backend with optimal performance  
- **Resource Management** - Automatic lifetime management with RAII
- **Command Lists** - Modern command buffer abstraction
- **Pipeline States** - Efficient state management

### Platform Layer
Cross-platform abstraction for system-specific functionality:

- **Window System** - GLFW-based windowing with native integration
- **Input Management** - Unified input across all platforms
- **Surface Creation** - Platform-specific Vulkan surface creation
- **Event Processing** - Native event loop integration

### Core Systems
Fundamental engine systems and utilities:

- **Application Framework** - High-level application lifecycle
- **Logging System** - Comprehensive logging with multiple levels  
- **Type System** - UE5-style type definitions and containers
- **Memory Management** - Smart pointer based resource management

## Contributing

1. Follow the existing code style and architecture patterns
2. All code should compile warning-free
3. Test on multiple platforms where possible
4. Document public APIs with Doxygen-style comments
5. Follow UE5 naming conventions and patterns

## License

This project is for educational and development purposes. Please respect all third-party library licenses.

## Dependencies

- **Vulkan SDK** - Graphics API (Apache 2.0 License)
- **GLFW** - Window and input library (zlib License)
- **C++ Standard Library** - C++20 features