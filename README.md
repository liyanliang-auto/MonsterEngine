# MonsterRender Engine

基于虚幻引擎5 (UE5) RHI架构的现代渲染引擎，使用C++20开发。

## 项目架构

MonsterRender采用模块化架构，遵循UE5的设计模式：

```
MonsterEngine/
├── Include/                    # 头文件
│   ├── Core/                  # 核心系统
│   │   ├── CoreMinimal.h      # 核心最小包含
│   │   ├── CoreTypes.h        # 基础类型定义
│   │   ├── Log.h              # 日志系统
│   │   └── Assert.h           # 断言系统
│   ├── RHI/                   # 渲染硬件接口
│   │   ├── RHI.h              # RHI工厂
│   │   ├── RHIDefinitions.h   # RHI类型定义
│   │   ├── IRHIDevice.h       # 设备接口
│   │   ├── IRHIResource.h     # 资源接口
│   │   └── IRHICommandList.h  # 命令列表接口
│   ├── Platform/              # 平台特定实现
│   │   └── Vulkan/            # Vulkan后端
│   │       ├── VulkanRHI.h    # Vulkan基础
│   │       └── VulkanDevice.h # Vulkan设备
│   └── Engine.h               # 主引擎类
├── Source/                    # 源文件
│   ├── Engine.cpp
│   ├── RHI/
│   │   ├── IRHIResource.cpp
│   │   └── RHI.cpp
│   └── Platform/
│       └── Vulkan/
│           └── VulkanAPI.cpp
└── main.cpp                   # 应用程序入口
```

## 核心特性

### 已实现功能
- ✅ **核心系统**: 日志、断言、类型系统
- ✅ **RHI抽象层**: 设备、资源、命令列表接口
- ✅ **Vulkan后端**: API加载器和基础框架
- ✅ **工厂模式**: 支持多RHI后端切换
- ✅ **现代C++**: 使用C++20标准和智能指针

### 计划功能
- ⏳ **Vulkan设备实现**: 完整的设备创建和管理
- 📋 **资源管理**: 缓冲区、纹理、管道状态
- 📋 **着色器系统**: 编译、缓存和反射
- 📋 **基础渲染器**: 简单三角形渲染
- 📋 **窗口系统**: 跨平台窗口管理

## 构建要求

### 必需软件
- **Visual Studio 2019/2022** (Windows)
- **Vulkan SDK** 1.3.x 或更高版本
- **C++20** 支持

### Vulkan SDK 安装
1. 下载并安装 [Vulkan SDK](https://vulkan.lunarg.com/)
2. 确保设置了 `VULKAN_SDK` 环境变量
3. 验证安装: 运行 `vulkaninfo` 命令

## 构建步骤

### Windows (Visual Studio)
1. 打开 `MonsterEngine.sln`
2. 选择 **x64** 平台配置
3. 选择 **Debug** 或 **Release** 配置
4. 构建解决方案 (Ctrl+Shift+B)

### 命令行构建
```bash
# 使用开发者命令提示符
msbuild MonsterEngine.sln /p:Configuration=Debug /p:Platform=x64
```

## 运行

编译成功后，运行 `Debug/MonsterEngine.exe` 或 `Release/MonsterEngine.exe`

预期输出：
```
[HH:MM:SS] [INFO] Starting MonsterRender Engine Application
[HH:MM:SS] [INFO] MonsterRender Engine created  
[HH:MM:SS] [INFO] Initializing MonsterRender Engine...
[HH:MM:SS] [INFO] Available RHI backends:
[HH:MM:SS] [INFO]   - Vulkan
[HH:MM:SS] [INFO] Creating RHI device...
[HH:MM:SS] [INFO] Auto-selected RHI backend: Vulkan
```

## 代码规范

### 命名约定
- **类/结构**: PascalCase (`class RHIDevice`)
- **函数/变量**: camelCase (`createBuffer()`, `vertexCount`)
- **常量/宏**: SCREAMING_SNAKE_CASE (`MAX_RENDER_TARGETS`)
- **接口类**: 'I'前缀 (`class IRHIDevice`)
- **成员变量**: 'm_'前缀 (`m_deviceContext`)

### 类型别名
```cpp
// 智能指针
TSharedPtr<T>    // std::shared_ptr<T>
TUniquePtr<T>    // std::unique_ptr<T>
TWeakPtr<T>      // std::weak_ptr<T>

// 容器
TArray<T>        // std::vector<T>
TMap<K,V>        // std::unordered_map<K,V>
TSpan<T>         // std::span<T>
```

### 内存管理
- 使用RAII原则管理所有资源
- 智能指针管理对象生命周期
- 避免裸指针，除非用于观察目的

## 架构设计

### RHI分层架构
```
High-Level Renderer
       ↓
   RHI Interface (IRHIDevice, IRHICommandList, etc.)
       ↓
Platform Implementation (VulkanDevice, D3D12Device, etc.)
       ↓
   Graphics API (Vulkan, D3D12, OpenGL, etc.)
```

### 支持的后端
- **Vulkan** ✅ (主要开发中)
- **Direct3D 12** 📋 (计划支持)
- **Direct3D 11** 📋 (计划支持)
- **OpenGL** 📋 (计划支持)
- **Metal** 📋 (计划支持)

## 开发指南

### 添加新的RHI后端
1. 在 `Include/Platform/NewBackend/` 创建头文件
2. 在 `Source/Platform/NewBackend/` 创建实现
3. 在 `RHI.cpp` 中添加工厂支持
4. 更新项目文件包含新文件

### 调试配置
- 启用Vulkan验证层: `enableValidation = true`
- 启用调试标记: `enableDebugMarkers = true`
- 日志级别: `Logger::getInstance().setMinLevel(ELogLevel::Debug)`

## 许可证

待定

## 贡献

待定

---
**注意**: 这是一个教育和学习项目，参考虚幻引擎5的架构设计。
