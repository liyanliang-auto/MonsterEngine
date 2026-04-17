# Vulkan Descriptor Set 架构修复 - 实现计划

> **面向 AI 代理的工作者：** 使用 superpowers:executing-plans 逐任务实现此计划。

**目标：** 修复 Vulkan RHI descriptor set 绑定架构，从 slot-based 迁移到 (set, binding)-based。

**架构：** 重构 VulkanPendingState 管理 4 个独立 descriptor set，修改 VulkanRHICommandList 传递 (set, binding)。

**技术栈：** C++20, Vulkan 1.3, MonsterEngine RHI

**参考：** `devDocument/2026-04-16-vulkan-descriptor-set-architecture-fix.md`

---

## 任务 1：添加验证函数

**文件：** `Include/RHI/RHIDefinitions.h`

- [ ] 在 namespace RHI 末尾添加 ValidateSetBinding 函数
- [ ] 编译验证
- [ ] Commit: "feat(rhi): Add ValidateSetBinding helper"

---

## 任务 2：修改 VulkanPendingState 头文件

**文件：** `Include/Platform/Vulkan/VulkanPendingState.h`

- [ ] 添加 FDescriptorSetState 结构
- [ ] 修改接口为 (set, binding) 参数
- [ ] 替换成员变量为 m_descriptorSets 数组
- [ ] 添加 updateDescriptorSet 和 bindDescriptorSets 方法
- [ ] Commit: "refactor(vulkan): Update VulkanPendingState header"

---

## 任务 3：实现资源绑定方法

**文件：** `Source/Platform/Vulkan/VulkanPendingState.cpp`

- [ ] 实现 setUniformBuffer(set, binding, ...)
- [ ] 实现 setTexture(set, binding, ...)
- [ ] 实现 setSampler(set, binding, ...)
- [ ] Commit: "refactor(vulkan): Implement set-based binding"

---

## 任务 4：实现更新和绑定逻辑

**文件：** `Source/Platform/Vulkan/VulkanPendingState.cpp`

- [ ] 实现 updateDescriptorSet(set)
- [ ] 实现 bindDescriptorSets(cmdBuffer)
- [ ] 修改 reset() 清空所有 descriptor sets
- [ ] Commit: "feat(vulkan): Implement descriptor set update/bind"

---

## 任务 5：修改 Descriptor Set Key

**文件：** `Include/Platform/Vulkan/VulkanDescriptorSetLayoutCache.h`

- [ ] 修改 FVulkanDescriptorSetKey 添加 SetIndex
- [ ] 修改 FBufferBinding/FImageBinding 使用 Binding
- [ ] 更新 hash 函数
- [ ] Commit: "refactor(vulkan): Update descriptor set key"

---

## 任务 6：修改 UpdateDescriptorSet

**文件：** `Source/Platform/Vulkan/VulkanDescriptorSetLayoutCache.cpp`

- [ ] 修改 UpdateDescriptorSet 使用 binding 而非 slot
- [ ] 添加日志和验证
- [ ] Commit: "fix(vulkan): Use correct binding in UpdateDescriptorSet"

---

## 任务 7：修改 RHICommandList

**文件：** `Source/Platform/Vulkan/VulkanRHICommandList.cpp`

- [ ] 修改 setConstantBuffer 传递 (set, binding)
- [ ] 修改 setShaderResource 传递 (set, binding)
- [ ] 修改 setSampler 传递 (set, binding)
- [ ] 编译验证（应成功）
- [ ] Commit: "fix(vulkan): Pass (set, binding) to PendingState"

---

## 任务 8：单元测试

**文件：** `Source/RHI/VulkanDescriptorSetTest.cpp` (新建)

- [ ] 创建测试文件
- [ ] 实现 TestSlotConversion
- [ ] 实现 TestDescriptorSetState
- [ ] 实现 TestValidateSetBinding
- [ ] 添加到项目和 main.cpp
- [ ] 运行测试验证
- [ ] Commit: "test(vulkan): Add descriptor set unit tests"

---

## 任务 9：渲染验证

- [ ] 运行 CubeSceneApplication
- [ ] 检查日志输出
- [ ] 使用 RenderDoc 捕获验证
- [ ] 创建验证报告
- [ ] Commit: "docs: Add verification report"

---

## 任务 10：文档更新

- [ ] 更新设计文档状态
- [ ] 创建使用示例文档
- [ ] 更新架构文档
- [ ] 创建最终总结
- [ ] Commit: "docs: Update all documentation"

---

## 验证清单

- [ ] 所有单元测试通过
- [ ] CubeSceneApplication 渲染正常
- [ ] RenderDoc 验证 descriptor set 绑定正确
- [ ] 无性能回退
- [ ] 无内存泄漏

---

**预计完成时间：** 2-3 天
**风险等级：** 中（渐进式迁移，可回滚）
