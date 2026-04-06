---
name: monster-engine-skills
description: MonsterEngine 3D rendering engine development guide - UE5-style architecture with Vulkan/OpenGL backend, parallel rendering, RDG system, and modern C++ best practices
---

# MonsterEngine Development Skills

## Overview

MonsterEngine is a modern 3D rendering engine following Unreal Engine 5 architecture patterns. This skill provides comprehensive guidance for developing, debugging, and maintaining the engine.

## Complete Rule Set Reference

This skill document summarizes key development guidelines and best practices.
For the complete, authoritative rule set (38 detailed rules), see:
- **Project Rules**: E:\MonsterEngine\.windsurf\rules\wind-monster-peoject-rule.md

## Project Information

- **Project Root**: E:\MonsterEngine\
- **Solution File**: E:\MonsterEngine\MonsterEngine.sln
- **Documentation**: E:\MonsterEngine\devDocument\
- **Architecture Doc**: E:\MonsterEngine\devDocument\引擎的架构和设计.md
- **Log File**: E:\MonsterEngine\MonsterEngine.log
- **User Rules**: E:\MonsterEngine\.windsurf\rules\wind-monster-peoject-rule.md

## Reference Codebases

- **UE5 Source**: E:\UnrealEngine (GitHub: https://github.com/EpicGames/UnrealEngine)
- **Google Filament**: E:\githubCode\Filament-V2 (GitHub: https://github.com/google/filament)
- **LearnOpenGL**: E:\10.github\LearnOpenGL

## Core Development Principles

### 1. Architecture Style - Follow UE5 Patterns Strictly

**Reference**: UE5 source code at E:\UnrealEngine

- **RHI (Render Hardware Interface)**: Abstract layer for graphics APIs
- **Scene Rendering**: FSceneRenderer pattern for high-level rendering
- **Parallel Rendering**: Command list pooling with FTaskGraph
- **RDG (Render Dependency Graph)**: Resource management and scheduling
- **Component System**: Actor-Component architecture
- **Scene Proxy Pattern**: Separation of game thread and render thread data
- **File Structure**: Follow UE5 directory organization (Rule 24)
- **File Naming**: Follow UE5 conventions (Rule 21)

### 2. Graphics API Support - Multi-Backend

- **Primary**: Vulkan
- **Secondary**: OpenGL
- **Reserved**: D3D12, Metal
- **Platforms**: Windows (primary), Android (reserved), Linux/macOS (reserved)
- **Rule**: All new rendering code MUST support both Vulkan and OpenGL (Rule 2)
- **Pattern**: Use RHI abstraction to hide platform differences (Rule 3, 20)

### 3. Naming Conventions (Camel Case - Rule 18)

| Type | Convention | Example |
|------|-----------|---------|
| Classes | PascalCase | FSceneRenderer, VulkanDevice |
| Interfaces | I prefix | IRHIDevice, IRHICommandList |
| Member Variables | m_ prefix + camelCase | m_device, m_vertexBuffer |
| Public Functions | camelCase | initialize(), createBuffer() |
| Private/Protected | _ prefix + camelCase | _updateLightBuffer(), _allocateMemory() |
| Template Types | T prefix | TArray, TSharedPtr, TMap |
| Enums | E prefix | ELogLevel, EResourceUsage |
| Constants/Macros | SCREAMING_SNAKE_CASE | MAX_RENDER_TARGETS |

### 4. Type System - Engine Types Only (Rules 4, 5, 6)

**❌ FORBIDDEN - Standard Library Types**:
`cpp
std::string, std::wstring          // Use FString, FName, FText
std::vector, std::map, std::set    // Use TArray, TMap, TSet
std::shared_ptr, std::unique_ptr   // Use TSharedPtr, TUniquePtr
new/delete, malloc/free            // Use MakeShared, MakeUnique, FMemory
`

**✅ REQUIRED - Engine Types**:
`cpp
// Integers
int8, int16, int32, int64
uint8, uint16, uint32, uint64

// Floats
float32, float64

// Strings (Rule 4)
FString, FName, FText
String, StringView  // Legacy compatibility

// Containers (Rule 5)
TArray<T>, TMap<K,V>, TSet<T>, TSparseArray<T>

// Smart Pointers (Rule 6)
TSharedPtr<T>, TUniquePtr<T>, TWeakPtr<T>

// Utilities
TOptional<T>, TFunction<T>, TSpan<T>
`

### 5. Memory Management - Strict Rules (Rule 7)

`cpp
// ❌ FORBIDDEN
MyClass* obj = new MyClass();
delete obj;
auto ptr = std::make_shared<MyClass>();
void* mem = malloc(size);

// ✅ REQUIRED
TSharedPtr<MyClass> obj = MakeShared<MyClass>(args...);
TUniquePtr<MyClass> obj = MakeUnique<MyClass>(args...);
void* mem = FMemory::Malloc(size);
FMemory::Free(mem);
`

**Memory Systems (Rule 7)**:
| Purpose | System |
|---------|--------|
| System Memory | FMemory::Malloc, FMemoryManager, FMallocBinned2 |
| GPU Memory | FVulkanMemoryManager |
| Textures | FTextureStreamingManager |
| Objects | MakeShared<T>(), MakeUnique<T>() |

**Critical (Rule 28)**: Always check for memory leaks and bounds violations!

### 6. Logging System - Printf-Style Only (Rule 16)

`cpp
// ✅ CORRECT - printf-style formatting
MR_LOG(LogCategory, Verbosity, "Format: %s, value: %d", str, value);
MR_LOG(LogRHI, Log, "Buffer created: size=%llu bytes", bufferSize);

// ❌ WRONG - string concatenation
MR_LOG_DEBUG("Message: " + std::to_string(value));

// Verbosity levels
VeryVerbose, Verbose, Log, Warning, Error, Fatal
`

**Define Log Categories**:
`cpp
DEFINE_LOG_CATEGORY_STATIC(LogMyModule, Log, All);
`

### 7. Code Documentation - English Only (Rules 10, 32)

`cpp
/**
 * Creates a vertex buffer with the specified description.
 * 
 * This function allocates GPU memory using FVulkanMemoryManager
 * and initializes the buffer with default values.
 * 
 * @param desc Buffer description containing size, usage, and flags
 * @return Shared pointer to the created buffer, nullptr on failure
 */
TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc);
`

**Rules**:
- All comments in English (Rule 10, 32)
- All log messages in English (Rule 10)
- Chinese only for analysis in chat (Rule 1, 13)
- UTF-8 encoding for all files (Rule 32)
- No markdown unless requested (Rule 11)

### 8. Code Formatting (Rule 12, 19)

- **Alignment**: Code must be properly formatted and aligned
- **Standards**: Follow "Code Complete 2nd Edition" (《代码大全（第2版）》)
- **Consistency**: Maintain consistent style throughout
- **Readability**: Prioritize code clarity

### 9. Thread Safety - Critical for Parallel Rendering (Rule 29)

`cpp
// Use mutex for shared resources
std::mutex m_poolMutex;
{
    std::lock_guard<std::mutex> lock(m_poolMutex);
    // Access shared resource
}

// Use atomic for counters
std::atomic<uint32> m_activeCount{0};
m_activeCount.fetch_add(1, std::memory_order_relaxed);

// Use FTaskGraph for parallel tasks
auto task = FTaskGraph::QueueTask([this]() {
    // Parallel work
});
task->Wait();
`

**Critical**: Always validate thread safety for new code!

### 10. RenderDoc Integration - Debugging Support (Rule 30)

`cpp
// Add debug markers
cmdList->setMarker("PassName");
cmdList->beginDebugLabel("SectionName");
// ... rendering code ...
cmdList->endDebugLabel();
`

**Capture Command**:
`powershell
& "C:\Program Files\RenderDoc\renderdoccmd.exe" capture 
  --working-dir "E:\MonsterEngine" 
  "E:\MonsterEngine\x64\Debug\MonsterEngine.exe" --cube-scene
`

**Rule 30**: Ensure all new code supports RenderDoc capture!

### 11. Include Strategy (Rule 17, 31)

`cpp
// Prefer forward declarations to minimize compile time
class IRHIDevice;
class IRHICommandList;

// Include only when necessary
#include "RHI/RHIDefinitions.h"
`

**Rule 17**: Use forward declarations to avoid long compilation times
**Rule 31**: Pay attention to include structure to keep builds fast

### 12. Type Casting (Rule 15)

`cpp
// Use dynamic_cast for polymorphic types
FPrimitiveSceneProxy* baseProxy = component->GetSceneProxy();
FCubeSceneProxy* cubeProxy = dynamic_cast<FCubeSceneProxy*>(baseProxy);
if (cubeProxy) {
    // Use derived class
}
`

### 13. Path Management (Rule 33)

`cpp
// ✅ Use relative paths for portability
"../Assets/Textures/albedo.png"
"Shaders/vertex.glsl"

// ❌ Avoid absolute paths
"E:/MonsterEngine/Assets/..."  // Not portable!
`

**Rule 33**: Ensure project can run from any directory

## Development Workflow

### Phase 1: Analysis (中文 - Rules 1, 13)
- 在会话框中使用中文分析需求
- 讨论架构设计和实现方案
- 确定修改范围和影响 (Rule 9)
- 规划开发步骤

### Phase 2: Implementation (English - Rules 10, 32)
- All code comments in English
- All log messages in English
- Follow UE5 naming conventions (Rule 21)
- Use engine-defined types only (Rules 4, 5, 6)
- Ensure thread safety (Rule 29)
- Add RenderDoc markers (Rule 30)
- Update related code (Rule 9)
- Proper code formatting (Rule 12)

### Phase 3: Build & Test (Rule 34, 35)
`powershell
# Build in Visual Studio 2022
& "E:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" 
  MonsterEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal

# Run tests
.\x64\Debug\MonsterEngine.exe --test-container

# Run application
.\x64\Debug\MonsterEngine.exe --cube-scene
`

**Rule 35**: Add new files to VS2022 project and ensure compilation

### Phase 4: Verification Checklist
- [ ] Compilation successful (no errors) (Rule 34)
- [ ] No memory leaks (Rule 28)
- [ ] No memory corruption (Rule 28)
- [ ] Thread-safe (no data races) (Rule 29)
- [ ] RenderDoc captures successfully (Rule 30)
- [ ] Vulkan validation layers pass
- [ ] OpenGL debug output clean
- [ ] Cross-platform compatible (Rules 2, 3, 20)
- [ ] Performance acceptable
- [ ] Related code updated (Rule 9)

### Phase 5: Documentation (Rules 22, 23)
- Update TODO list with completed tasks (Rule 22)
- Summarize changes in Chinese (Rule 22)
- Propose next development steps (Rule 23)
- Update architecture docs if needed (Rule 14)

## Common Code Patterns

### RHI Resource Creation
`cpp
RHI::BufferDesc bufferDesc;
bufferDesc.size = dataSize;
bufferDesc.usage = RHI::EResourceUsage::VertexBuffer;
bufferDesc.debugName = "MyVertexBuffer";

TSharedPtr<RHI::IRHIBuffer> buffer = m_device->createBuffer(bufferDesc);
if (!buffer) {
    MR_LOG(LogRHI, Error, "Failed to create vertex buffer");
    return false;
}
`

### Command List Recording
`cpp
auto* cmdList = FRHICommandListPool::AllocateCommandList(
    FRHICommandListPool::ECommandListType::Graphics,
    RHI::ERecordingThread::Any
);

cmdList->begin();
cmdList->setMarker("MyRenderPass");
cmdList->setPipelineState(pso);
cmdList->setVertexBuffers(0, vertexBuffers);
cmdList->draw(vertexCount);
cmdList->end();

FRHICommandListPool::RecycleCommandList(cmdList);
`

### Parallel Task Execution
`cpp
auto task = FTaskGraph::QueueTask([this]() {
    MR_LOG(LogRenderer, Verbose, "Executing parallel task");
    // Task implementation
});

task->Wait();
`

### Smart Pointer Usage
`cpp
// Shared ownership
TSharedPtr<MyClass> shared = MakeShared<MyClass>(args...);
TWeakPtr<MyClass> weak = shared;

// Unique ownership
TUniquePtr<MyClass> unique = MakeUnique<MyClass>(args...);
MyClass* raw = unique.Get();
`

## File Organization (Rule 24)

### Directory Structure
`
MonsterEngine/
├── Include/                 # Public headers
│   ├── Core/               # Core systems
│   ├── Containers/         # Container types
│   ├── RHI/                # Render Hardware Interface
│   ├── Platform/           # Platform implementations
│   │   ├── Vulkan/
│   │   └── OpenGL/
│   ├── Renderer/           # High-level rendering
│   ├── Engine/             # Engine systems
│   └── Math/               # Math library
├── Source/                  # Implementation files
├── Shaders/                 # GLSL/SPIR-V shaders
├── 3rd-party/              # Third-party libraries
├── devDocument/            # Development documentation
└── .windsurf/              # IDE configuration
    ├── rules/              # Project rules
    └── skills/             # Development skills
`

## Matrix and Vector Conventions (Rules 25, 26, 27)

**Row-Major, Row-Vector (UE5 Style)**:
`cpp
// Vector * Matrix multiplication
FVector transformed = position * modelMatrix;

// Matrix concatenation
FMatrix combined = worldMatrix * viewMatrix * projMatrix;

// Shader (GLSL): Row-vector style
// vec4 pos = input.position * ModelViewProjection;
`

**Depth Convention**:
- **Current**: Non-Reverse Z (0=near, 1=far) (Rule 26)
- **Future**: Reverse Z (1=near, 0=far) - UE5 style migration planned

## Error Handling

`cpp
// Use return values for success/failure
bool initialize() {
    if (!createResources()) {
        MR_LOG(LogEngine, Error, "Failed to create resources");
        return false;
    }
    return true;
}

// Use TOptional for optional values
TOptional<int32> findIndex(const FString& name) {
    if (found) return index;
    return {};
}

// Use assertions in debug
check(pointer != nullptr);
checkf(size > 0, "Size must be positive, got %d", size);
ensure(condition);  // Non-fatal
`

## Performance Best Practices

### CPU Optimization
- Minimize allocations in hot paths
- Use object pooling (command lists, etc.)
- Leverage parallel task execution
- Cache expensive computations
- Use data-oriented design

### GPU Optimization
- Batch draw calls
- Use instancing
- Minimize state changes
- Implement GPU-driven rendering
- Use async compute

### Memory Optimization (Rule 28)
- Use memory pools
- Implement efficient GPU memory management
- Support memory budgets
- Profile regularly
- **Check for leaks and bounds violations!**

## Visual Studio 2022 Integration (Rules 34, 35)

### Adding New Files
1. Create header: Include/ModuleName/ClassName.h
2. Create source: Source/ModuleName/ClassName.cpp
3. Add to .vcxproj file
4. Create filter in Solution Explorer
5. Build and verify compilation

### Build Configuration
- **Debug**: Full symbols, no optimization, validation layers
- **Release**: Optimized, minimal symbols, no validation
- **Platform**: x64 (Android support reserved)

## Common Pitfalls to Avoid

1. **Namespace Conflicts**: Use full paths or move to different namespace
2. **Include Cycles**: Use forward declarations (Rule 17)
3. **Memory Leaks**: Always use smart pointers (Rule 7, 28)
4. **Thread Safety**: Protect all shared resources (Rule 29)
5. **Platform Assumptions**: Test Vulkan AND OpenGL (Rule 2)
6. **String Concatenation in Logs**: Use printf-style (Rule 16)
7. **Hardcoded Paths**: Use relative paths (Rule 33)
8. **Missing RenderDoc Markers**: Add debug labels (Rule 30)
9. **Ignoring Warnings**: Fix all warnings
10. **Poor Error Messages**: Provide context
11. **Slow Compilation**: Use forward declarations (Rule 17, 31)
12. **Unrelated Code**: Update related code (Rule 9)

## Task Completion Template (Rules 22, 23)

After completing any task, provide:

### 📋 本次修改总结 (Rule 22)
- 修改的文件列表
- 关键变更说明
- 架构决策说明
- 完成的任务清单

### 🎯 下一步开发计划 (Rule 23)
- 立即下一步任务
- 中期目标
- 长期路线图

### ⚠️ 待完善事项
- 已知限制
- TODO 项目
- 未来优化

## Reference Documentation (Rules 36, 37, 38)

- **UE5 RHI**: E:\UnrealEngine\Engine\Source\Runtime\RHI\
- **UE5 Renderer**: E:\UnrealEngine\Engine\Source\Runtime\Renderer\
- **Filament Backend**: E:\githubCode\Filament-V2\filament\backend\
- **Project Docs**: E:\MonsterEngine\devDocument\
- **User Rules**: E:\MonsterEngine\.windsurf\rules\wind-monster-peoject-rule.md

## Key Principles Summary (38 Rules)

1. **Architecture**: Strictly follow UE5 patterns (Rules 21, 24, 36, 38)
2. **Types**: Use engine types only, no STL (Rules 4, 5, 6)
3. **Memory**: Smart pointers and FMemory system (Rules 7, 28)
4. **Threading**: Ensure thread safety everywhere (Rule 29)
5. **Platform**: Support Vulkan + OpenGL (Rules 2, 3, 20)
6. **Debugging**: RenderDoc markers everywhere (Rule 30)
7. **Language**: 分析用中文，代码用英文 (Rules 1, 10, 13, 32)
8. **Formatting**: Proper alignment and Code Complete standards (Rules 12, 19)
9. **Logging**: Printf-style only (Rule 16)
10. **Includes**: Forward declarations preferred (Rules 17, 31)
11. **Casting**: Use dynamic_cast (Rule 15)
12. **Paths**: Relative paths for portability (Rule 33)
13. **Build**: VS2022 compilation required (Rules 34, 35)
14. **Updates**: Modify related code (Rule 9)
15. **Documentation**: Summarize and plan (Rules 22, 23)

## Conclusion

MonsterEngine development requires:
- Deep understanding of UE5 architecture
- Strict adherence to coding standards
- Careful memory and thread management
- Comprehensive debugging support
- Cross-platform compatibility
- Continuous reference to UE5 and Filament source code

**Remember**: 严格遵循UE5风格，确保代码质量和性能！参考《代码大全》编写高质量代码！
**Remember**: 每一次代码开发完成，都要执行编译，并保证工程编译通过：& "E:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" E:\MonsterEngine\MonsterEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal 2>&
**Remember**: 每一次代码开发完成，都要执行："E:\MonsterEngine\x64\Debug\MonsterEngine.exe" 保证程序可以运行起来；