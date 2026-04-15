# Descriptor Set 线性展开方案实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。

**目标：** 实现简单线性展开方案（MAX_BINDINGS_PER_SET = 8），统一 Vulkan 和 OpenGL 的 Shader 源码。

**架构：** 修改 RHI 定义层，实现 Shader 预处理器，更新 Vulkan/OpenGL RHI 层，重新规划 Shader 布局。

**技术栈：** C++20, Vulkan, OpenGL, GLSL, std::regex

**参考规格：** `e:\MonsterEngine\devDocument\2026-04-15-descriptor-set-linear-flatten-design.md`

---

## 阶段 1：核心基础设施

### 任务 1：修改 RHI 定义

**文件：** `Include/RHI/RHIDefinitions.h`

- [ ] 修改 MAX_BINDINGS_PER_SET 为 8
- [ ] 添加 EDescriptorSet 枚举
- [ ] 添加 GetGlobalBinding() 函数
- [ ] Commit

### 任务 2：实现 Shader 预处理器

**文件：** `Include/RHI/ShaderPreprocessor.h`, `Source/RHI/ShaderPreprocessor.cpp`

- [ ] 创建头文件声明
- [ ] 实现 PreprocessShader() 方法
- [ ] 实现正则替换逻辑
- [ ] 添加单元测试
- [ ] Commit

---

## 阶段 2：迁移 CubeLitShadow Shader

### 任务 3：重新规划 Shader 布局

**文件：** `Shaders/CubeLitShadow.vert`, `Shaders/CubeLitShadow.frag`

- [ ] 修改 TransformUBO 为 Set 0, Binding 0
- [ ] 修改 ShadowUBO 为 Set 1, Binding 1
- [ ] 修改 ShadowMap 为 Set 1, Binding 2
- [ ] Commit

### 任务 4：更新 Vulkan RHI

**文件：** `Source/Platform/Vulkan/VulkanRHICommandList.cpp`

- [ ] 在 setConstantBuffer() 添加 slot 转换
- [ ] 在 setShaderResource() 添加 slot 转换
- [ ] Commit

### 任务 5：更新应用层绑定

**文件：** `Source/CubeSceneApplication.cpp`

- [ ] 更新所有 setConstantBuffer 调用
- [ ] 更新所有 setShaderResource 调用
- [ ] Commit

### 任务 6：测试渲染

- [ ] 编译项目
- [ ] 测试 Vulkan 渲染
- [ ] 测试 OpenGL 渲染
- [ ] Commit

---

## 阶段 3：迁移其他 Shader

### 任务 7：迁移 PBR/ImGui/Forward Shader

**文件：** 各 Shader 文件

- [ ] 重新规划所有 Shader 布局
- [ ] 更新应用层绑定代码
- [ ] 测试所有渲染路径
- [ ] Commit

---

## 阶段 4：清理和优化

### 任务 8：清理和文档

- [ ] 删除 _GL Shader 文件
- [ ] 性能测试
- [ ] 更新文档
- [ ] 最终验证
- [ ] Commit

---

**详细实施步骤请参考设计规格文档。**
