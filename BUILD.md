# MonsterEngine - CMake 构建指南 (Windows)

本文档提供 MonsterEngine 使用 CMake 在 Windows 平台上的完整构建说明。

---

## 📋 前置要求

### 必需软件

| 软件 | 最低版本 | 推荐版本 | 说明 |
|------|---------|---------|------|
| **CMake** | 3.20 | 3.28+ | 构建系统生成器 |
| **Visual Studio** | 2022 | 2022 | MSVC 编译器 |
| **Vulkan SDK** | 1.3.x | 1.4.x | 图形 API |
| **Windows SDK** | 10.0 | 10.0.22621.0 | Windows 开发工具 |

### 环境变量

确保以下环境变量已正确设置：

```powershell
# 检查 Vulkan SDK
echo $env:VULKAN_SDK
# 应输出: C:\VulkanSDK\1.4.xxx.x

# 检查 CMake
cmake --version
# 应输出: cmake version 3.20 或更高
```

---

## 🚀 快速开始

### 方法 1: 使用 Visual Studio 2022 (推荐)

```powershell
# 1. 创建构建目录
mkdir build
cd build

# 2. 生成 Visual Studio 解决方案
cmake .. -G "Visual Studio 17 2022" -A x64

# 3. 打开生成的解决方案
start MonsterEngine.sln

# 4. 在 Visual Studio 中按 F5 编译并运行
```

### 方法 2: 使用 CMake 命令行

```powershell
# 1. 创建构建目录
mkdir build
cd build

# 2. 配置项目 (Debug)
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug

# 3. 编译项目
cmake --build . --config Debug

# 4. 运行程序
.\bin\Debug\MonsterEngine.exe
```

### 方法 3: 使用 Ninja (更快的编译速度)

```powershell
# 1. 安装 Ninja (如果未安装)
# 下载: https://github.com/ninja-build/ninja/releases

# 2. 创建构建目录
mkdir build
cd build

# 3. 配置项目
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug

# 4. 编译项目
ninja

# 5. 运行程序
.\bin\MonsterEngine.exe
```

---

## 🔧 详细构建步骤

### 步骤 1: 克隆或获取源代码

```powershell
# 如果使用 Git
git clone <repository-url>
cd MonsterEngine

# 或者直接进入项目目录
cd D:\MonsterEngine
```

### 步骤 2: 验证依赖项

```powershell
# 检查 Vulkan SDK
if ($env:VULKAN_SDK) {
    Write-Host "Vulkan SDK: $env:VULKAN_SDK" -ForegroundColor Green
} else {
    Write-Host "ERROR: Vulkan SDK not found!" -ForegroundColor Red
    Write-Host "Please install Vulkan SDK from: https://vulkan.lunarg.com/" -ForegroundColor Yellow
}

# 检查 GLFW
if (Test-Path "3rd-party\glfw-3.4.bin.WIN64\lib-vc2022\glfw3.dll") {
    Write-Host "GLFW: OK" -ForegroundColor Green
} else {
    Write-Host "ERROR: GLFW not found!" -ForegroundColor Red
}

# 检查 ImGui
if (Test-Path "3rd-party\imgui\imgui.cpp") {
    Write-Host "ImGui: OK" -ForegroundColor Green
} else {
    Write-Host "ERROR: ImGui not found!" -ForegroundColor Red
}
```

### 步骤 3: 配置 CMake

```powershell
# 创建并进入构建目录
mkdir build -Force
cd build

# 配置项目 (选择一个生成器)

# 选项 A: Visual Studio 2022
cmake .. -G "Visual Studio 17 2022" -A x64

# 选项 B: Ninja (需要先设置 MSVC 环境)
# 运行 Visual Studio Developer Command Prompt 或:
# & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1"
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug

# 选项 C: NMake
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
```

### 步骤 4: 编译项目

#### 使用 Visual Studio

```powershell
# 打开解决方案
start MonsterEngine.sln

# 或使用命令行编译
cmake --build . --config Debug --parallel 8
```

#### 使用 Ninja

```powershell
ninja -j 8
```

#### 使用 NMake

```powershell
nmake
```

### 步骤 5: 运行程序

```powershell
# Debug 版本
.\bin\Debug\MonsterEngine.exe

# Release 版本
.\bin\Release\MonsterEngine.exe

# 运行测试
.\bin\Debug\MonsterEngine.exe --test-container
```

---

## 🎯 构建配置选项

### Debug vs Release

```powershell
# Debug 构建 (带调试符号)
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug

# Release 构建 (优化性能)
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### 并行编译

```powershell
# 使用 8 个并行任务
cmake --build . --config Debug --parallel 8

# 使用所有 CPU 核心
cmake --build . --config Debug --parallel
```

### 清理构建

```powershell
# 清理构建产物
cmake --build . --target clean

# 或删除整个构建目录
cd ..
Remove-Item -Recurse -Force build
```

---

## 📂 构建输出结构

```
build/
├── bin/
│   ├── Debug/
│   │   ├── MonsterEngine.exe       # 可执行文件
│   │   ├── glfw3.dll                # GLFW 动态库
│   │   ├── Shaders/                 # 着色器文件 (自动复制)
│   │   └── resources/               # 资源文件 (自动复制)
│   └── Release/
│       └── MonsterEngine.exe
├── lib/                             # 静态库 (如果有)
└── CMakeFiles/                      # CMake 内部文件
```

---

## 🐛 常见问题和解决方案

### 问题 1: Vulkan SDK 未找到

**错误信息**:
```
CMake Error: Could NOT find Vulkan (missing: Vulkan_INCLUDE_DIR)
```

**解决方案**:
```powershell
# 1. 确认 Vulkan SDK 已安装
# 2. 设置环境变量
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.304.0"

# 3. 重新运行 CMake
cmake .. -G "Visual Studio 17 2022" -A x64
```

### 问题 2: GLFW DLL 未找到

**错误信息**:
```
The program can't start because glfw3.dll is missing
```

**解决方案**:
```powershell
# DLL 应该自动复制，如果没有，手动复制:
Copy-Item "3rd-party\glfw-3.4.bin.WIN64\lib-vc2022\glfw3.dll" "build\bin\Debug\"
```

### 问题 3: 预编译头错误

**错误信息**:
```
fatal error C1083: Cannot open precompiled header file
```

**解决方案**:
```powershell
# 1. 清理构建目录
Remove-Item -Recurse -Force build

# 2. 重新配置和编译
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Debug
```

### 问题 4: 编译速度慢

**解决方案**:
```powershell
# 使用 Ninja 生成器
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
ninja -j 16

# 或在 Visual Studio 中启用并行编译
cmake --build . --config Debug --parallel 16
```

### 问题 5: CMake 版本过低

**错误信息**:
```
CMake 3.15 or higher is required. You are running version 3.10
```

**解决方案**:
```powershell
# 下载并安装最新版 CMake
# https://cmake.org/download/

# 或使用 Chocolatey
choco install cmake --installargs 'ADD_CMAKE_TO_PATH=System'
```

---

## 🔍 调试技巧

### 查看详细编译信息

```powershell
# CMake 详细输出
cmake .. -G "Visual Studio 17 2022" -A x64 --debug-output

# 编译详细输出
cmake --build . --config Debug --verbose
```

### 查看 CMake 缓存

```powershell
# 查看所有 CMake 变量
cmake -L ..

# 查看高级变量
cmake -LA ..
```

### 使用 CMake GUI

```powershell
# 打开 CMake GUI
cmake-gui ..
```

---

## 📊 性能优化

### 编译时间优化

```powershell
# 1. 使用 Ninja (比 MSBuild 快 30-50%)
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release
ninja

# 2. 启用预编译头 (已默认启用)
# 预编译头可以减少 40-60% 的编译时间

# 3. 使用 ccache (可选)
# 安装: choco install ccache
cmake .. -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
```

### 运行时性能

```powershell
# Release 构建启用了以下优化:
# - /O2: 最大优化
# - /Ob2: 内联函数展开
# - /Oi: 内部函数
# - /GL: 全程序优化
# - /LTCG: 链接时代码生成
```

---

## 🧪 测试

### 运行单元测试

```powershell
# 运行所有测试
.\bin\Debug\MonsterEngine.exe --test-all

# 运行特定测试
.\bin\Debug\MonsterEngine.exe --test-container
.\bin\Debug\MonsterEngine.exe --test-memory
.\bin\Debug\MonsterEngine.exe --test-vulkan
```

### 使用 RenderDoc 调试

```powershell
# 使用 RenderDoc 捕获一帧
& "C:\Program Files\RenderDoc\renderdoccmd.exe" capture `
    --working-dir "D:\MonsterEngine\build\bin\Debug" `
    ".\bin\Debug\MonsterEngine.exe" --cube-scene
```

---

## 📚 其他资源

- **CMake 官方文档**: https://cmake.org/documentation/
- **Vulkan SDK 文档**: https://vulkan.lunarg.com/doc/sdk
- **Visual Studio CMake 支持**: https://docs.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio

---

## 🆘 获取帮助

如果遇到问题:

1. 检查本文档的"常见问题"部分
2. 查看 CMake 输出的错误信息
3. 检查 `MonsterEngine.log` 日志文件
4. 查看项目 README.md

---

**构建成功后，您应该看到类似以下输出:**

```
========================================
MonsterEngine Build Configuration
========================================
Build type:        Debug
C++ Standard:      C++20
Compiler:          MSVC
Vulkan SDK:        C:/VulkanSDK/1.4.304.0/Include
Output directory:  D:/MonsterEngine/build/bin
========================================

[100%] Built target MonsterEngine
```

祝您构建顺利！🎉
