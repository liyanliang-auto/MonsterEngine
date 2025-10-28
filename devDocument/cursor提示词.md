



#### 1. 帮我实现：

- 完善Descriptor Set分配

  - 实现VulkanDescriptorSetAllocator环形缓冲区

  - 真正调用vkUpdateDescriptorSets + vkCmdBindDescriptorSets

  - 参考：UE5的FVulkanDescriptorSetRingBuffer

- 

  注意:


2.所有代码实现参考UE5，UE5源码github链接：@https://github.com/EpicGames/UnrealEngine ；

3.当前引擎的现有实现见开发文档：开发文档路径：E:\MonsterEngine\devDocument\引擎的架构和设计.md

3.保证在visual studio2022中可以编译和链接通过；

3.更新开发文档E:\MonsterEngine\devDocument\引擎的架构和设计.md：

-   （1）把本次的代码实现整理成文档内容，补充到文档E:\MonsterEngine\devDocument\引擎的架构和设计.md的最后
-    （2）画出类UML图、线程图、代码架构图、代码流程图，并做好排版，补充到文档E:\MonsterEngine\devDocument\引擎的架构和设计.md的最后
-   ​    3）并提出下一步开发计划，补充到文档E:\MonsterEngine\devDocument\引擎的架构和设计.md的最后

4.引擎的开发文档见：E:\MonsterEngine\devDocument\引擎的架构和设计.md：，我的终极目标是实现一个像UE5一样的现代渲染引擎。

5.文件结构参照UE5;