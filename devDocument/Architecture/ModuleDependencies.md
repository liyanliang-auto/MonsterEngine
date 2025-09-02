# 模块依赖关系

MonsterRender引擎的模块化设计确保了清晰的依赖层次和良好的代码组织。

## 依赖层次图

```
Application Layer (用户代码)
    ↓
Engine Layer (Engine.h/cpp) 
    ↓
Renderer Layer (未来实现)
    ↓
RHI Layer (RHI/*.h/cpp)
    ↓
Platform Layer (Platform/*/*.h/cpp)
    ↓
Core Layer (Core/*.h/cpp)
```

## 核心模块 (Core)

**依赖**: 仅标准库
**被依赖**: 所有其他模块

### 提供的功能
- 基础类型定义 (`CoreTypes.h`)
- 日志系统 (`Log.h`) 
- 断言机制 (`Assert.h`)
- 统一包含 (`CoreMinimal.h`)

### 内部依赖
```cpp
CoreMinimal.h
├── CoreTypes.h
├── Log.h
└── Assert.h (依赖 Log.h)
```

## RHI模块

**依赖**: Core模块
**被依赖**: Engine, Platform实现

### 模块构成
- `RHIDefinitions.h` - 类型和枚举定义
- `IRHIResource.h` - 资源基类 
- `IRHICommandList.h` - 命令列表接口
- `IRHIDevice.h` - 设备接口
- `RHI.h` - 工厂和管理

### 内部依赖关系
```cpp
RHI.h 
└── IRHIDevice.h
    ├── IRHIResource.h
    │   └── RHIDefinitions.h
    └── IRHICommandList.h
        └── IRHIResource.h
```

## Platform模块

**依赖**: Core, RHI模块
**被依赖**: Engine (通过RHI工厂)

### Vulkan后端结构
```cpp
Vulkan/
├── VulkanRHI.h (API加载器)
├── VulkanDevice.h (设备实现)
├── VulkanBuffer.h (缓冲区实现) 
├── VulkanTexture.h (纹理实现)
└── VulkanCommandList.h (命令列表实现)
```

### Platform依赖关系
```cpp
VulkanDevice.h
├── IRHIDevice.h
├── VulkanRHI.h
└── 系统相关头文件 (vulkan/vulkan.h, Windows.h等)
```

## Engine模块

**依赖**: Core, RHI模块
**被依赖**: 用户应用代码

### 职责
- 统一的引擎接口
- 子系统协调管理
- 应用程序生命周期

## 依赖规则

### 允许的依赖
✅ 上层模块可以依赖下层模块
✅ 同层模块间的水平依赖（需文档说明）
✅ 通过接口的依赖倒置

### 禁止的依赖
❌ 下层模块不能依赖上层模块
❌ Core模块不能依赖任何引擎模块
❌ Platform模块不能相互依赖

### 循环依赖解决
使用前向声明和接口分离：

```cpp
// 正确：使用前向声明
class IRHICommandList; // 前向声明

class IRHIDevice {
    virtual TSharedPtr<IRHICommandList> createCommandList() = 0;
};

// 避免：直接包含会造成循环依赖
// #include "IRHICommandList.h"
```

## 编译顺序

1. **Core模块** - 无依赖，最先编译
2. **RHI模块** - 依赖Core
3. **Platform模块** - 依赖Core和RHI
4. **Engine模块** - 依赖所有底层模块
5. **Application** - 依赖Engine

## 模块接口设计

### 清晰的边界
每个模块都有明确定义的公共接口：

```cpp
// RHI模块的公共接口
namespace MonsterRender::RHI {
    // 工厂入口点
    class RHIFactory;
    
    // 核心接口
    class IRHIDevice;
    class IRHIResource;
    class IRHICommandList;
}
```

### 实现细节隐藏
```cpp
// 公共头文件：只暴露接口
class IRHIDevice {
    virtual ~IRHIDevice() = default;
    // ... 纯虚函数
};

// 实现文件：包含具体实现
class VulkanDevice : public IRHIDevice {
    // 实现细节对外部不可见
private:
    VkDevice m_device;
    // ...
};
```

## 测试依赖

### 单元测试结构
```cpp
Tests/
├── Core/           // 测试Core模块
├── RHI/           // 测试RHI接口
├── Vulkan/        // 测试Vulkan实现
└── Integration/   // 集成测试
```

### Mock依赖
```cpp
// 为上层测试提供Mock实现
class MockRHIDevice : public IRHIDevice {
public:
    MOCK_METHOD(TSharedPtr<IRHIBuffer>, createBuffer, (const BufferDesc&));
    // ...
};
```

## 构建系统依赖

### CMake目标依赖
```cmake
# Core库（无依赖）
add_library(MonsterCore STATIC ${CORE_SOURCES})

# RHI库（依赖Core）
add_library(MonsterRHI STATIC ${RHI_SOURCES})
target_link_libraries(MonsterRHI MonsterCore)

# Vulkan后端（依赖Core, RHI, Vulkan）
add_library(MonsterVulkan STATIC ${VULKAN_SOURCES})
target_link_libraries(MonsterVulkan MonsterCore MonsterRHI Vulkan::Vulkan)

# 引擎库（依赖所有模块）
add_library(MonsterEngine STATIC ${ENGINE_SOURCES})
target_link_libraries(MonsterEngine MonsterCore MonsterRHI MonsterVulkan)
```

## 性能影响

### 编译时间优化
- 最小化头文件包含
- 使用前向声明
- PIMPL模式隐藏实现细节

### 运行时性能
- 虚函数调用开销最小化
- 内联关键路径函数
- 模板特化减少抽象层开销

---

## 相关文档
- [总体架构设计](OverallArchitecture.md)
- [设计模式和原则](DesignPatterns.md)
