# MonsterRender 引擎开发文档

欢迎来到MonsterRender渲染引擎开发文档。本文档集合提供了引擎架构、API参考和开发指南的详细信息。

## 文档结构

### 📁 [Architecture](Architecture/) - 架构设计
- [总体架构设计](Architecture/OverallArchitecture.md)
- [模块依赖关系](Architecture/ModuleDependencies.md)
- [设计模式和原则](Architecture/DesignPatterns.md)

### 📁 [Core](Core/) - 核心系统
- [核心类型系统](Core/CoreTypes.md)
- [日志系统](Core/LoggingSystem.md)
- [断言和调试](Core/AssertionSystem.md)
- [内存管理](Core/MemoryManagement.md)

### 📁 [RHI](RHI/) - 渲染硬件接口
- [RHI概述](RHI/RHIOverview.md)
- [设备接口](RHI/DeviceInterface.md)
- [资源管理](RHI/ResourceManagement.md)
- [命令列表](RHI/CommandList.md)
- [后端工厂](RHI/BackendFactory.md)

### 📁 [Vulkan](Vulkan/) - Vulkan实现
- [Vulkan后端概述](Vulkan/VulkanOverview.md)
- [API加载器](Vulkan/APILoader.md)
- [设备初始化](Vulkan/DeviceInitialization.md)
- [资源实现](Vulkan/ResourceImplementation.md)

### 📁 [Examples](Examples/) - 示例代码
- [基础引擎使用](Examples/BasicEngineUsage.md)
- [创建简单三角形](Examples/RenderTriangle.md)
- [自定义RHI后端](Examples/CustomBackend.md)

## 快速开始

1. 阅读 [总体架构设计](Architecture/OverallArchitecture.md) 了解引擎整体结构
2. 查看 [RHI概述](RHI/RHIOverview.md) 理解渲染抽象层
3. 参考 [基础引擎使用](Examples/BasicEngineUsage.md) 开始编码

## 文档约定

### 代码示例
所有代码示例都使用C++20语法，并遵循引擎的编码规范：

```cpp
// 类使用PascalCase
class RHIDevice {
    // 成员变量使用m_前缀
    String m_deviceName;
    
    // 方法使用camelCase
    bool initialize(const CreateInfo& info);
};
```

### 符号说明
- 🚀 **新特性** - 最近添加的功能
- ⚠️ **注意** - 重要的注意事项
- 🔧 **待完成** - 计划中但尚未实现的功能
- 📖 **参考** - 相关文档链接

## 版本历史

| 版本 | 日期 | 主要更改 |
|------|------|----------|
| 0.1.0 | 2024-01 | 初始架构和RHI接口设计 |

## 贡献指南

参与引擎开发时请：
1. 遵循已定义的架构模式
2. 保持代码风格一致性
3. 为新功能添加相应文档
4. 编写单元测试

---

> **注意**: MonsterRender是一个学习和研究项目，参考虚幻引擎5的架构设计。所有设计决策都基于现代渲染引擎的最佳实践。
