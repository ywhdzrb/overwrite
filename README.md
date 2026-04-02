<div align="center">

<img src="assets/logo/logo.png" alt="OverWrite Logo" width="480">

**基于 Vulkan 的现代 3D 游戏引擎框架**

<img src="https://img.shields.io/badge/C++-20-00599C?style=flat-square&logo=c%2B%2B" alt="C++20">
<img src="https://img.shields.io/badge/Vulkan-1.3+-AC162C?style=flat-square&logo=vulkan" alt="Vulkan">
<img src="https://img.shields.io/badge/Platform-Linux-FCC624?style=flat-square&logo=linux" alt="Linux">
<img src="https://img.shields.io/badge/License-GPL%20v3-blue?style=flat-square" alt="License">
<img src="https://img.shields.io/badge/License-CC%20BY--NC--ND%204.0-lightgrey.svg?style=flat-square" alt="License">


</div>

---

## ✨ 特性

- 🎮 **高性能渲染** - 基于 Vulkan 1.3+ 的现代图形管线
- 🌍 **3D 模型加载** - 支持 OBJ 和 GLTF 格式
- 💡 **动态光源系统** - 方向光、点光源、聚光灯，CPU 实时控制
- 🔧 **开发者工具** - ESC 开启开发者面板，实时调参
- 🎯 **物理系统** - 重力模拟、碰撞检测
- 🖼️ **MSAA 抗锯齿** - 动态多重采样，最高支持 64x
- 🇨🇳 **中文支持** - ImGui 中文字体渲染

## 🚀 快速开始

### 依赖

**Arch Linux:**

```bash
sudo pacman -S vulkan-headers vulkan-tools glfw glm cmake gcc
```

**Ubuntu/Debian:**

```bash
sudo apt install vulkan-sdk libglfw3-dev libglm-dev cmake build-essential
```

### 构建

```bash
./build.sh
```

或手动构建：

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### 运行

```bash
./run.sh
```

## 🎮 操作

| 按键 | 功能 |
|------|------|
| `WASD` | 移动 |
| `Mouse` | 视角控制 |
| `Space` | 跳跃 |
| `Shift` | 加速 |
| `R` | 切换自由视角 |
| `ESC` | 开发者模式 |

## 🛠️ 开发者面板

按 `ESC` 进入开发者模式，可实时调整：

- **性能统计** - FPS、帧时间、时间缩放
- **相机控制** - 位置、速度、灵敏度
- **光源管理** - 颜色、强度、位置、衰减参数
- **模型控制** - 位置、旋转、缩放
- **物理设置** - 重力、地面高度、跳跃力
- **渲染设置** - MSAA 级别、线框模式

## 📁 项目结构

```
OverWrite/
├── include/           # 头文件
│   ├── vulkan_*.h     # Vulkan 核心组件
│   ├── renderer.h     # 主渲染器
│   ├── camera.h       # 相机系统
│   └── ...
├── src/
│   ├── core/          # Vulkan 核心实现
│   ├── renderer/      # 渲染系统
│   └── utils/         # 工具函数
├── shaders/           # GLSL 着色器
├── assets/            # 模型和纹理
└── external/          # 第三方库
```

## 📜 许可证

本项目采用双重许可：

### 源代码

[GNU General Public License v3.0](LICENSE)

适用范围：
- `src/` 目录下所有源代码文件
- `include/` 目录下所有头文件
- `shaders/` 目录下所有着色器源码
- `CMakeLists.txt` 构建脚本

您有权：
- ✅ 自由使用、研究、修改源代码
- ✅ 分发原始或修改后的代码
- ✅ 将代码用于商业用途

义务：
- ⚠️ 分发时必须附带 GPL v3 许可证副本
- ⚠️ 修改后的文件必须注明修改内容
- ⚠️ 分发二进制产品时必须提供源代码

### 艺术内容

[CC BY-NC-ND 4.0](https://creativecommons.org/licenses/by-nc-nd/4.0/)

适用范围：
- `assets/models/` 目录下所有 3D 模型文件
- `assets/textures/` 目录下所有纹理图片

您有权：
- ✅ 复制和分发原素材

限制：
- ❌ 不得用于商业用途
- ❌ 不得修改后再分发
- ⚠️ 使用时需注明原作者

---

<div align="center">

*用 ❤️ 构建*

</div>
