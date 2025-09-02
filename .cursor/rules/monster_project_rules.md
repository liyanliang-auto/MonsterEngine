# MonsterRender 渲染引擎 - Cursor 全局规则
# 基于虚幻引擎5 RHI架构和现代渲染引擎最佳实践

## 核心架构原则

### RHI (渲染硬件接口) 分层架构
- 遵循UE5的RHI抽象模式：RHI -> 平台特定实现 (D3D12, Vulkan, OpenGL)
- 严格分离高级渲染逻辑和底层图形API调用
- 为所有RHI组件使用抽象基类 (Device, CommandList, Resources等)
- 实现工厂模式创建平台特定的RHI对象
- 支持延迟资源创建和懒初始化

### 代码组织和结构
- 使用命名空间层次结构：`MonsterRender::RHI::Platform` (例如：`MonsterRender::RHI::D3D12`)
- 分离头文件和实现：公共接口在头文件中，实现细节在.cpp文件中
- 广泛使用前向声明以减少编译依赖
- 将代码组织为逻辑模块：Core、RHI、Renderer、Platform等

### 内存管理
- 使用智能指针 (std::shared_ptr, std::unique_ptr) 进行自动资源管理
- 为不同资源类型实现自定义分配器 (GPU内存、CPU内存等)
- 遵循RAII原则进行所有资源管理
- 为频繁分配/释放的对象使用资源池
- 为共享GPU资源实现引用计数

### 资源管理
- 实现资源状态跟踪 (类似D3D12资源状态)
- 使用资源屏障进行适当的同步
- 支持立即和延迟资源创建
- 实现资源缓存和重用策略
- 使用弱引用避免循环依赖

## 编码标准

### C++ 样式指南
- 类、结构体和枚举值使用PascalCase：`class RHIDevice`, `enum class EPrimitiveType`
- 函数和变量使用camelCase：`createBuffer()`, `vertexCount`
- 常量和宏使用SCREAMING_SNAKE_CASE：`MAX_RENDER_TARGETS`
- 接口类使用'I'前缀：`class IRHIDevice`
- 成员变量使用'm_'前缀：`m_deviceContext`
- 使用清楚表明用途和作用域的描述性名称

### 头文件结构
```cpp
#pragma once

// 引擎包含文件
#include "Core/CoreMinimal.h"
#include "RHI/RHIDefinitions.h"

// 前向声明
class IRHICommandList;
class IRHIResource;

namespace MonsterRender {
namespace RHI {
    class IRHIDevice {
        // 接口定义
    };
}}
```

### 错误处理
- 谨慎使用异常，优先使用错误码或 std::optional/std::expected
- 实现具有不同日志级别的综合日志系统
- 在调试版本中使用断言：`ASTRA_CHECK(condition)`
- 验证所有公共API的输入参数
- 提供包含上下文的详细错误消息

### 平台抽象
- 对平台特定代码使用预处理器宏：`#if PLATFORM_WINDOWS`
- 将所有平台特定功能抽象到接口后面
- 支持不同RHI实现之间的热切换
- 对平台特定服务使用依赖注入

## RHI 组件指导原则

### 命令列表和命令缓冲区
- 实现线程安全的命令列表记录
- 支持并行命令列表生成
- 使用命令列表池进行高效重用
- 在调试版本中实现命令列表验证
- 支持复杂渲染操作的嵌套命令列表

### 资源创建模式
```cpp
// 首选的资源创建模式
class IRHIDevice {
    virtual std::shared_ptr<IRHIBuffer> createBuffer(const BufferDesc& desc) = 0;
    virtual std::shared_ptr<IRHITexture> createTexture(const TextureDesc& desc) = 0;
    virtual std::shared_ptr<IRHIPipelineState> createPipelineState(const PipelineStateDesc& desc) = 0;
};
```

### 着色器管理
- 支持多种着色器语言 (HLSL, GLSL, SPIR-V)
- 实现带缓存的着色器编译管道
- 使用着色器反射进行自动资源绑定
- 支持热重载以实现快速迭代
- 实现针对不同功能组合的着色器变体系统

### 管道状态管理
- 缓存管道状态对象以供重用
- 使用基于哈希的查找进行快速管道状态检索
- 支持异步管道状态编译
- 实现管道状态验证
- 将相关状态分组为逻辑束

### 同步和线程
- 实现GPU-CPU同步原语
- 尽可能使用无锁数据结构
- 支持多线程命令列表生成
- 实现用于并行任务的工作窃取作业系统
- 对无锁算法使用原子操作

## 渲染架构

### 帧结构
- 实现双重/三重缓冲以实现流畅的帧传递
- 使用环形缓冲区进行动态资源分配
- 支持基于GPU时间线的同步
- 实现帧节拍和自适应质量系统
- 使用结构化方法进行帧图执行

### 资源绑定
- 实现描述符集/表管理 (类似D3D12/Vulkan)
- 在支持的地方使用无绑定渲染技术
- 实现自动资源转换
- 支持持久映射缓冲区
- 使用具有类型安全的结构化缓冲区绑定

### 调试和性能分析
- 集成GPU性能分析标记
- 支持RenderDoc和PIX集成
- 实现全面的调试可视化
- 在调试版本中使用GPU验证层
- 提供详细的性能指标和瓶颈分析

## 性能考虑

### GPU性能
- 通过适当的资源管理最小化GPU停顿
- 使用GPU驱动的渲染技术
- 实现实例化和批处理策略
- 支持异步计算以进行并行GPU工作
- 使用保守的资源屏障

### CPU性能
- 在渲染热路径中最小化CPU开销
- 对并行CPU工作使用作业系统
- 实现高效的剔除和排序算法
- 缓存昂贵的计算
- 使用面向数据的设计原则

### 内存性能
- 在热路径中最小化内存分配
- 对不同分配模式使用内存池
- 实现高效的GPU内存管理
- 支持内存预算管理
- 对大型资源使用压缩

## 测试和验证

### 单元测试
- 为所有RHI组件编写全面的单元测试
- 使用模拟对象进行无GPU测试
- 测试错误条件和边缘情况
- 实现自动化回归测试
- 对复杂场景使用基于属性的测试

### 集成测试
- 在多个图形API和驱动程序上测试
- 验证跨平台兼容性
- 使用不同GPU厂商进行测试
- 进行高资源使用的压力测试
- 在多线程场景中验证线程安全性

## 文档标准

### 代码文档
- 对所有公共API使用Doxygen风格的注释
- 记录前置条件、后置条件和不变式
- 为复杂API提供使用示例
- 记录性能特征和权衡
- 维护架构决策记录 (ADRs)

### API设计
- 保持公共接口简洁且专注
- 全面使用const正确性
- 在适用的地方提供同步和异步变体
- 对复杂对象创建使用构建器模式
- 为命令记录实现流畅接口

## 现代C++特性

### 语言特性 (C++20/23)
- 使用概念进行模板约束
- 利用协程进行异步操作
- 在构建系统支持的地方使用模块
- 应用范围和视图进行数据处理
- 使用结构化绑定使代码更清洁

### 标准库使用
- 优先使用std::span而不是原始指针和大小
- 对非拥有字符串参数使用std::string_view
- 利用std::variant实现类型安全的联合
- 对可选参数使用std::optional
- 对所有时间相关操作应用std::chrono

## 构建系统和依赖项

### CMake配置
- 支持多种构建配置 (Debug, Release, RelWithDebInfo)
- 实现适当的依赖管理
- 支持不同平台的交叉编译
- 使用现代CMake实践 (目标、属性)
- 与包管理器集成 (vcpkg, Conan)

### 第三方库
- 最小化外部依赖
- 尽可能使用仅头文件库
- 将第三方API包装在引擎特定的抽象中
- 维护与不同库版本的兼容性
- 记录所有外部依赖及其用途

本文档作为使用UE5启发的架构和现代C++最佳实践开发MonsterRender引擎的基础指南。
