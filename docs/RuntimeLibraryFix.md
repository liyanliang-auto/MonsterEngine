# 运行时库冲突修复指南

## 问题描述

编译时出现警告：
```
LINK : warning LNK4098: 默认库"MSVCRT"与其他库的使用冲突；请使用 /NODEFAULTLIB:library
```

## 问题原因

这个警告是由于不同的库使用了不兼容的运行时库版本：

- **Debug配置** 应该使用：`MSVCRTD` (Debug版本的多线程DLL运行时库)
- **Release配置** 应该使用：`MSVCRT` (Release版本的多线程DLL运行时库)

当项目中的某些库链接了错误版本的运行时库时，就会出现这个冲突。

## 解决方案

### 1. 明确指定运行时库类型

在项目文件 (`MonsterEngine.vcxproj`) 中为每个配置明确指定运行时库：

#### Debug配置
```xml
<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
  <ClCompile>
    <RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>
  </ClCompile>
</ItemDefinitionGroup>
```

#### Release配置
```xml
<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
  <ClCompile>
    <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
  </ClCompile>
</ItemDefinitionGroup>
```

### 2. 使用正确的GLFW库版本

GLFW提供了多个库文件，我们选择多线程静态库版本：

- 从 `glfw3.lib` 改为 `glfw3_mt.lib`
- `glfw3_mt.lib` 是多线程版本，与我们的运行时库设置兼容

```xml
<AdditionalDependencies>vulkan-1.lib;glfw3_mt.lib;%(AdditionalDependencies)</AdditionalDependencies>
```

## 运行时库类型说明

| 设置 | 描述 | 使用场景 |
|------|------|----------|
| `MultiThreadedDebugDLL` | 多线程Debug DLL (/MDd) | Debug配置 |
| `MultiThreadedDLL` | 多线程Release DLL (/MD) | Release配置 |
| `MultiThreadedDebug` | 多线程Debug静态 (/MTd) | 静态链接Debug |
| `MultiThreaded` | 多线程Release静态 (/MT) | 静态链接Release |

## GLFW库文件说明

| 文件名 | 描述 | 推荐使用 |
|--------|------|----------|
| `glfw3.lib` | 基础静态库 | ❌ |
| `glfw3_mt.lib` | 多线程静态库 | ✅ 推荐 |
| `glfw3dll.lib` | 动态库导入库 | 需要DLL |
| `glfw3.dll` | 动态链接库 | 与glfw3dll.lib配合 |

## 验证修复

修复后重新编译，应该不再出现 LNK4098 警告。

## 最佳实践

1. **一致性**: 确保项目中所有库使用相同类型的运行时库
2. **配置匹配**: Debug配置使用Debug运行时库，Release配置使用Release运行时库
3. **库选择**: 优先选择与项目配置匹配的第三方库版本
4. **文档记录**: 在项目文档中记录使用的库版本和配置

## 故障排除

如果仍然出现运行时库冲突：

1. 检查所有第三方库的运行时库依赖
2. 使用 `dumpbin /dependents library.lib` 查看库依赖
3. 考虑使用 `/NODEFAULTLIB` 排除特定库
4. 确保Vulkan SDK库与项目配置兼容

## 相关链接

- [Microsoft文档: /MD, /MT, /LD](https://docs.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library)
- [GLFW文档: Building applications](https://www.glfw.org/docs/latest/build_guide.html)
