# wind\-monster\-peoject\-rule

---

## trigger: always\_on

# 基于虚幻引擎5 RHI架构和现代渲染引擎最佳实践

---

## 项目概述

MonsterEngine 是一个现代 3D 渲染引擎，采用 Vulkan 作为主要图形后端，架构风格严格参考 Unreal Engine 5。

- **UE5 参考源码**: https://github\.com/EpicGames/UnrealEngine

- **UE5 本地源码路径**: `E:\UnrealEngine`

- **项目解决方案**: `E:\MonsterEngine\MonsterEngine.sln`

- **开发文档路径**: `E:\MonsterEngine\devDocument\`

- **架构设计文档**: `E:\MonsterEngine\devDocument\引擎的架构和设计.md`

---

## 核心架构原则

### RHI \(渲染硬件接口\) 分层架构

- 遵循 UE5 的 RHI 抽象模式：RHI → 平台特定实现 \(Vulkan, OpenGL\)，并预留D3D12和metal扩展

- 严格分离高级渲染逻辑和底层图形 API 调用

- 为所有 RHI 组件使用抽象基类 \(`IRHIDevice`, `IRHICommandList`, `IRHIResource` 等\)

- 实现工厂模式创建平台特定的 RHI 对象

- 支持延迟资源创建和懒初始化

- **新增代码要同时适配和兼容 Vulkan 和 OpenGL**

### 代码组织和结构

- 使用命名空间层次结构：`MonsterRender::RHI::Platform`

- 分离头文件和实现：公共接口在头文件中，实现细节在 \.cpp 文件中

- 广泛使用前向声明以减少编译依赖

- 将代码组织为逻辑模块：Core、RHI、Renderer、Platform 等

---

## 代码风格规范

### 命名约定

|类型|命名规则|示例|
|---|---|---|
|类名|PascalCase|`TriangleRenderer`, `VulkanDevice`|
|接口|`I` 前缀 \+ PascalCase|`IRHIDevice`, `IRHICommandList`|
|成员变量|`m_` 前缀 \+ camelCase|`m_device`, `m_vertexBuffer`|
|函数名|camelCase|`initialize()`, `createVertexBuffer()`|
|模板类型|`T` 前缀 \+ PascalCase|`TArray`, `TSharedPtr`, `TSpan`|
|枚举|`E` 前缀 \+ PascalCase|`ELogLevel`, `EResourceUsage`|
|常量/宏|SCREAMING\_SNAKE\_CASE|`PLATFORM_WINDOWS`, `MAX_RENDER_TARGETS`|

### 类型定义（禁止使用标准库类型）

**整数类型:**

- `int8`, `int16`, `int32`, `int64`

- `uint8`, `uint16`, `uint32`, `uint64`

**浮点类型:**

- `float32`, `float64`

**字符串类型（禁止使用 ****`std::string`****）:**

- `FString`, `FName`, `FText`

- 兼容旧代码: `String`, `StringView`

**容器类型（禁止使用标准库容器）:**

- `TArray<T>`, `TSparseArray<T>`

- `TMap<K,V>`, `TSet<T>`

**智能指针:**

- `TSharedPtr<T>`, `TUniquePtr<T>`, `TWeakPtr<T>`

- 禁止使用C\+\+智能指针；

**其他:**

- `TOptional<T>`, `TFunction<T>`, `TSpan<T>`

### 智能指针使用

```C++
// 创建共享指针
TSharedPtr<MyClass> ptr = MakeShared<MyClass>(args...);

// 创建唯一指针
TUniquePtr<MyClass> ptr = MakeUnique<MyClass>(args...);

// 指针转换
auto derived = StaticCastSharedPtr<DerivedClass>(basePtr);
```

### 命名空间

- 主命名空间: `MonsterRender`

- 容器/核心系统: `MonsterEngine`

- RHI 子命名空间: `MonsterRender::RHI`

- 平台特定: `MonsterRender::RHI::Vulkan`, `MonsterRender::RHI::OpenGL`

---

## 内存管理（严格规范）

### ⚠️ 禁止事项

- **禁止使用 ****`new`****/****`delete`**

- **禁止使用 ****`std::make_shared`****/****`std::make_unique`**

- **禁止使用原始 ****`malloc`****/****`free`**（除了内存管理器FMemoryManager的内部实现外）

### ✅ 必须使用

|用途|使用的管理器|
|---|---|
|系统内存分配|`FMemory::Malloc`, `FMemoryManager`, `FMallocBinned2`|
|GPU 内存分配|`FVulkanMemoryManager`|
|纹理管理|`FTextureStreamingManager`|
|对象创建|`MakeShared<T>()`, `MakeUnique<T>()`|

### 内存管理原则

- 使用智能指针进行自动资源管理

- 为不同资源类型实现自定义分配器

- 遵循 RAII 原则进行所有资源管理

- 为频繁分配/释放的对象使用资源池

- 为共享 GPU 资源实现引用计数

---

## 日志系统

### ⚠️ 禁止使用 `std::cout`，必须使用日志系统

使用 UE5 风格的日志宏（定义在 `LogMacros.h`）:

```C++
// 推荐使用（新代码）
MR_LOG(LogCategory, Verbosity, "Format string: %s", args...);

// 支持的 Verbosity: VeryVerbose, Verbose, Log, Warning, Error, Fatal

// 兼容旧 API（不推荐用于新代码）
MR_LOG_INFO("message");
MR_LOG_WARNING("message");
MR_LOG_ERROR("message");
MR_LOG_DEBUG("message");
```

**日志规范:**

- `MR_LOG_DEBUG` 中的字符串必须使用英文

- 日志文件路径: `E:\MonsterEngine\MonsterEngine.log`

---

## 头文件结构

```C++
#pragma once

// Engine includes
#include "Core/CoreMinimal.h"
#include "RHI/RHIDefinitions.h"

// Forward declarations
class IRHICommandList;
class IRHIResource;

namespace MonsterRender {
namespace RHI {

    /**
     * Brief class description (Doxygen style)
     * 
     * Detailed description of the class functionality.
     */
    class IRHIDevice {
    public:
        /**
         * Function description
         * @param paramName Parameter description
         * @return Return value description
         */
        virtual bool initialize(const DeviceDesc& desc) = 0;
        
    private:
        Type* m_memberVariable = nullptr;
        TSharedPtr<Type> m_sharedMember;
    };

}} // namespace MonsterRender::RHI
```

---

## 错误处理

- 谨慎使用异常，优先使用错误码或 `TOptional`

- 使用返回值 `bool` 表示操作成功/失败

- 错误时记录日志 `MR_LOG_ERROR(...)`

- 在调试版本中使用断言: `check()`, `checkf()`, `ensure()`

- 验证所有公共 API 的输入参数

- 提供包含上下文的详细错误消息

---

## RHI 组件模式

### 资源创建模式

```C++
// 创建资源描述
RHI::BufferDesc bufferDesc;
bufferDesc.size = dataSize;
bufferDesc.usage = RHI::EResourceUsage::VertexBuffer;
bufferDesc.debugName = "Descriptive Name";

// 创建资源
TSharedPtr<RHI::IRHIBuffer> buffer = m_device->createBuffer(bufferDesc);
if (!buffer) {
    MR_LOG_ERROR("Failed to create buffer");
    return false;
}
```

### 渲染代码模式

```C++
void Renderer::render(RHI::IRHICommandList* cmdList) {
    if (!cmdList || !m_pipelineState) {
        MR_LOG_ERROR("Invalid render state");
        return;
    }
    
    cmdList->setPipelineState(m_pipelineState);
    cmdList->setVertexBuffers(0, vertexBuffers);
    cmdList->setViewport(viewport);
    cmdList->setScissorRect(scissor);
    cmdList->draw(vertexCount);
}
```

### 命令列表和命令缓冲区

- 实现线程安全的命令列表记录

- 支持并行命令列表生成

- 使用命令列表池进行高效重用

- 在调试版本中实现命令列表验证

### 管道状态管理

- 缓存管道状态对象以供重用

- 使用基于哈希的查找进行快速管道状态检索

- 支持异步管道状态编译

- 实现管道状态验证

支持的平台：

新增的代码需要同时支持Vulkan,和OpenGL；

---

## 文件组织

### 目录结构

```Plaintext
MonsterEngine/
├── Include/                 # Public headers
│   ├── Core/               # Core systems (Log, Memory, Assert)
│   ├── Containers/         # Container types (TArray, TMap)
│   ├── RHI/                # Render Hardware Interface
│   ├── Platform/           # Platform-specific implementations
│   │   ├── Vulkan/
│   │   └── OpenGL/
│   └── Math/               # Math library
├── Source/                  # Implementation files
├── Shaders/                 # GLSL/SPIR-V shaders
├── 3rd-party/              # Third-party libraries
└── devDocument/            # Development documentation
```

### 头文件/源文件对应

- 头文件: `Include/ModuleName/ClassName.h`

- 源文件: `Source/ModuleName/ClassName.cpp`

---

## 构建系统

### Visual Studio 2022 配置

- 解决方案: `E:\MonsterEngine\MonsterEngine.sln`

- 配置: Debug/Release x64

- 依赖: Vulkan SDK, GLFW

### 编译命令

```PowerShell
# 编译项目
& "E:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" MonsterEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal 2>&1 | Select-Object -Last 5
```

### 运行程序

```PowerShell
# 运行测试
.\x64\Debug\MonsterEngine.exe --test-container 2>&1

# 运行主程序
.\x64\Debug\MonsterEngine.exe 2>&1
```

### 新增文件规范

- 新增文件必须添加到 VS2022 项目

- 创建对应的文件夹筛选器（Filter）

- 保证工程编译通过

- 编译错误请查看日志: `E:\MonsterEngine\MonsterEngine.log`

---

## 注释规范

### 必须使用英文注释

- 类和公共函数使用 Doxygen 风格 `/** */`

- 实现细节使用 `//` 单行注释

- 函数内的代码也应该添加详细注释

- 专业词汇和关键词使用英文

- 中文仅用于开发文档

### 示例

```C++
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
```

---

## 现代 C\+\+ 特性 \(C\+\+20\)

### 推荐使用

- 使用概念 \(Concepts\) 进行模板约束

- 利用协程进行异步操作

- 使用 `std::span` 代替原始指针和大小

- 对非拥有字符串参数使用 `std::string_view`

- 利用 `std::variant` 实现类型安全的联合

- 对可选参数使用 `TOptional`

- 使用结构化绑定使代码更清洁

---

## 性能考虑

### GPU 性能

- 通过适当的资源管理最小化 GPU 停顿

- 使用 GPU 驱动的渲染技术

- 实现实例化和批处理策略

- 支持异步计算以进行并行 GPU 工作

- 使用保守的资源屏障

### CPU 性能

- 在渲染热路径中最小化 CPU 开销

- 对并行 CPU 工作使用作业系统

- 实现高效的剔除和排序算法

- 缓存昂贵的计算

- 使用面向数据的设计原则

### 内存性能

- 在热路径中最小化内存分配

- 对不同分配模式使用内存池

- 实现高效的 GPU 内存管理

- 支持内存预算管理

---

## 测试规范

### 单元测试

- 为所有 RHI 组件编写全面的单元测试

- 使用模拟对象进行无 GPU 测试

- 测试错误条件和边缘情况

- 实现自动化回归测试

### 集成测试

- 在多个图形 API 和驱动程序上测试

- 验证跨平台兼容性

- 使用不同 GPU 厂商进行测试

- 在多线程场景中验证线程安全性

---

## 调试和性能分析

- 集成 GPU 性能分析标记

- 支持 RenderDoc 和 PIX 集成，

- 使用 RenderDoc 来捕获一帧的执行命令：

```Plaintext
& "C:\Program Files\RenderDoc\renderdoccmd.exe" capture --working-dir "E:\MonsterEngine" "E:\MonsterEngine\x64\Debug\MonsterEngine.exe" --cube-scene 2>&1
```

- 实现全面的调试可视化

- 在调试版本中使用 GPU 验证层

- 提供详细的性能指标和瓶颈分析

---

## 工作流程规范

### Agent 分析过程

- 在会话窗口使用中文进行分析讨论

- 代码注释和日志使用英文

- 若不是明确要求，不要生成 markdown 文档

### 开发计划

- 每次完成任务后提出下一步开发计划

- 参考架构文档: `E:\MonsterEngine\devDocument\引擎的架构和设计.md`

---

## 快速参考

|禁止|使用|
|---|---|
|`new`/`delete`|`MakeShared<T>()`, `MakeUnique<T>()`|
|`std::cout`|`MR_LOG(...)`|
|`std::string`|`FString`, `String`|
|`std::vector`|`TArray<T>`|
|`std::map`|`TMap<K,V>`|
|`std::set`|`TSet<T>`|
|原始 `malloc`/`free`|`FMemory::Malloc`, `FVulkanMemoryManager`|

---

本文档作为使用 UE5 启发的架构和现代 C\+\+ 最佳实践开发 MonsterEngine 引擎的基础指南。

