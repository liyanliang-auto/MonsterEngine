# Shader Binding Refactor - Quick Reference

## 🎯 项目目标

将 MonsterEngine 的 Shader 绑定机制重构为线性展开方式（Linear Flatten），使用 `MAX_BINDINGS_PER_SET = 8`，提升 Vulkan 和 OpenGL 之间的跨平台兼容性。

## ✅ 完成状态

**状态**: 阶段 1-2 完成，阶段 3 延后，阶段 4 完成  
**分支**: `feature_shader_set_bind_opengl`  
**提交数**: 9 个提交  
**测试**: 21/21 通过 ✅

## 📊 核心改动

### 1. RHI 定义更新
- `MAX_BINDINGS_PER_SET`: 16 → 8
- 新增 `GetGlobalBinding()` 和 `GetSetAndBinding()` 辅助函数
- 新增 `EDescriptorSet` 枚举

### 2. Shader 预处理器
- 自动转换 Vulkan GLSL → OpenGL GLSL
- 使用正则表达式解析 `layout(set=X, binding=Y)`
- 7 个单元测试全部通过

### 3. CubeLitShadow Shader 迁移
- 重组为 Set 0/1/2 布局
- 应用层绑定代码更新
- 渲染测试通过

### 4. 文档完善
- 标准布局文档
- 实现总结文档
- 迁移指南

## 🔢 Slot 映射公式

```cpp
globalBinding = set * 8 + binding
set = globalBinding / 8
binding = globalBinding % 8
```

## 📋 标准布局

| Set | 用途 | 更新频率 | Slot 范围 |
|-----|------|----------|-----------|
| 0 | Per-Frame | 每帧 | 0-7 |
| 1 | Per-Pass | 每Pass | 8-15 |
| 2 | Per-Material | 每材质 | 16-23 |
| 3 | Per-Object | 每对象 | 24-31 |

## 📁 关键文件

### 代码文件
- `Include/RHI/RHIDefinitions.h` - RHI 定义
- `Include/RHI/ShaderPreprocessor.h` - Shader 预处理器
- `Source/RHI/ShaderPreprocessor.cpp` - 预处理器实现
- `Source/Platform/Vulkan/VulkanRHICommandList.cpp` - Vulkan RHI 更新

### Shader 文件
- `Shaders/CubeLitShadow.vert` - 顶点着色器（已迁移）
- `Shaders/CubeLitShadow.frag` - 片段着色器（已迁移）

### 测试文件
- `Source/RHI/ShaderPreprocessorTest.cpp` - 预处理器测试
- `Source/RHI/SlotConversionTest.cpp` - Slot 转换测试

### 文档文件
- `devDocument/shader_descriptor_layout_standard.md` - 标准布局
- `devDocument/shader_binding_refactor_summary.md` - 实现总结
- `devDocument/SHADER_BINDING_REFACTOR_README.md` - 本文档

## 🚀 使用方法

### 应用层绑定资源

```cpp
// 旧方式（直接使用 binding）
cmdList->setConstantBuffer(3, lightingUBO);  // ❌

// 新方式（使用计算的 slot）
// Set 1, Binding 0 -> slot = 1*8+0 = 8
cmdList->setConstantBuffer(8, lightingUBO);  // ✅
```

### Shader 中声明资源

```glsl
// Vulkan GLSL
layout(set = 1, binding = 0) uniform LightingUBO {
    // ...
} lighting;

// 预处理器自动转换为 OpenGL GLSL:
// layout(binding = 8) uniform LightingUBO { ... } lighting;
```

## 📈 测试结果

### 单元测试
- Shader 预处理器: 7/7 ✅
- Slot 转换: 14/14 ✅
- **总计**: 21/21 ✅

### 集成测试
- CubeLitShadow 渲染: ✅
- 阴影映射: ✅
- 多纹理支持: ✅

### 性能
- 无性能影响
- 描述符绑定开销不变
- Shader 编译时间不变

## 📝 提交历史

| Commit | 描述 |
|--------|------|
| `849a6b1` | 添加文档 |
| `b94b6f7` | Slot 转换测试 |
| `ebb356d` | 应用层绑定更新 |
| `ff1a713` | Vulkan RHI 更新 |
| `eb4394a` | Shader 布局重组 |
| `24d8684` | Shader 预处理器 |
| `8226b3d` | RHI 定义更新 |
| `5c08c04` | 实现计划 |
| `68fbc9f` | 设计规格 |

## 🔮 后续工作

### 短期
- 监控 CubeLitShadow 在生产环境的表现
- 收集新绑定方式的反馈

### 中期
- 在 PBR 渲染器重构时迁移 PBR Shader
- 为其他活跃开发的 Shader 应用新布局

### 长期
- 实现自动 Shader 变体生成
- 添加运行时 Shader 反射验证
- 考虑 Vulkan 1.2+ 的 Bindless Descriptor

## 📚 参考资料

- **设计文档**: `devDocument/引擎的架构和设计.md`
- **实现计划**: `devDocument/shader_binding_implementation_plan.md`
- **布局标准**: `devDocument/shader_descriptor_layout_standard.md`
- **实现总结**: `devDocument/shader_binding_refactor_summary.md`
- **UE5 RHI**: `E:\UnrealEngine\Engine\Source\Runtime\RHI`

## ❓ 常见问题

### Q: 为什么 MAX_BINDINGS_PER_SET 是 8？
A: 8 是 2 的幂次，便于位运算优化，同时满足大多数 Shader 的需求。

### Q: 旧的 Shader 还能用吗？
A: 可以。未迁移的 Shader 继续使用旧的绑定方式，不受影响。

### Q: 如何迁移新的 Shader？
A: 参考 `shader_descriptor_layout_standard.md` 中的标准布局，按照 Set 0/1/2/3 组织资源。

### Q: OpenGL 版本的 Shader 需要修改吗？
A: 不需要。OpenGL Shader (*_GL.vert/frag) 保持原样，预处理器只处理 Vulkan Shader。

### Q: 性能有影响吗？
A: 无影响。这只是组织方式的改变，不改变底层绑定机制。

---

**文档版本**: 1.0  
**最后更新**: 2026-04-15  
**作者**: Windsurf Cascade AI Assistant  
**状态**: 完成 ✅
