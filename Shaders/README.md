# MonsterEngine 着色器编译指南

## 概述

此目录包含MonsterEngine的着色器源文件和编译脚本。当前实现支持HLSL和GLSL着色器，并编译为SPIR-V字节码供Vulkan后端使用。

## 文件结构

```
Shaders/
├── Triangle.hlsl          # HLSL着色器源码（顶点和像素着色器）
├── Triangle.vert          # GLSL顶点着色器
├── Triangle.frag          # GLSL片段着色器
├── compile_shaders.bat    # Windows编译脚本
└── README.md             # 本文档
```

## 编译要求

### Windows
- **Vulkan SDK**: 安装最新的Vulkan SDK
- **glslc编译器**: 包含在Vulkan SDK中
- 确保`glslc.exe`在系统PATH中

### Linux
```bash
# Ubuntu/Debian
sudo apt install vulkan-tools

# 或者安装完整的Vulkan SDK
# 从 https://vulkan.lunarg.com/ 下载
```

## 编译步骤

### Windows
1. 打开命令提示符
2. 导航到`Shaders`目录
3. 运行编译脚本：
   ```cmd
   compile_shaders.bat
   ```

### Linux
```bash
cd Shaders/

# 编译顶点着色器
glslc Triangle.vert -o Triangle.vert.spv

# 编译片段着色器  
glslc Triangle.frag -o Triangle.frag.spv
```

### 手动编译（高级用户）
```bash
# 从HLSL编译（需要DXC编译器）
dxc -spirv -T vs_6_0 -E VSMain Triangle.hlsl -Fo Triangle.vert.spv
dxc -spirv -T ps_6_0 -E PSMain Triangle.hlsl -Fo Triangle.frag.spv

# 验证SPIR-V字节码
spirv-val Triangle.vert.spv
spirv-val Triangle.frag.spv
```

## 着色器描述

### Triangle.vert (顶点着色器)
- **输入**: 顶点位置 (vec3) 和颜色 (vec3)
- **输出**: 变换后的位置和插值颜色
- **功能**: 将顶点从模型空间变换到裁剪空间

### Triangle.frag (片段着色器)  
- **输入**: 插值后的顶点颜色
- **输出**: 最终像素颜色
- **功能**: 输出插值后的颜色值

## 集成到引擎

编译后的SPIR-V字节码被硬编码到`TriangleRenderer.cpp`中：

```cpp
// 在createShaders()函数中
TArray<uint8> vertexShaderCode = {
    // SPIR-V字节码...
};
```

## 未来改进

1. **动态加载**: 从文件加载SPIR-V字节码
2. **热重载**: 支持着色器运行时重新编译
3. **着色器变体**: 支持预处理器宏和多个着色器变体
4. **优化**: 启用着色器优化选项
5. **调试**: 添加调试信息和符号

## 故障排除

### 常见问题

1. **glslc未找到**
   - 确保Vulkan SDK已正确安装
   - 检查PATH环境变量是否包含Vulkan SDK的bin目录

2. **编译错误**
   - 检查着色器语法
   - 确保GLSL版本兼容性（当前使用#version 450）

3. **SPIR-V验证失败**
   - 使用`spirv-val`工具验证生成的字节码
   - 检查着色器输入/输出匹配

### 调试工具

- **spirv-dis**: 反汇编SPIR-V字节码
- **spirv-opt**: 优化SPIR-V字节码  
- **spirv-val**: 验证SPIR-V字节码
- **RenderDoc**: 图形调试器

## 参考资料

- [Vulkan GLSL规范](https://www.khronos.org/registry/vulkan/)
- [SPIR-V规范](https://www.khronos.org/registry/spir-v/)
- [glslc用户指南](https://github.com/google/shaderc/tree/main/glslc)
