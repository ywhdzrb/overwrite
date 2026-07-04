<div align="center">

<h1>OverWrite</h1>

**曾经的你是现在的我？**

<img src="https://img.shields.io/badge/C++-20-00599C?style=flat-square&logo=c%2B%2B" alt="C++20">
<img src="https://img.shields.io/badge/Vulkan-1.3+-AC162C?style=flat-square&logo=vulkan" alt="Vulkan">
<img src="https://img.shields.io/badge/Platform-Linux-FCC624?style=flat-square&logo=linux" alt="Linux">
<img src="https://img.shields.io/badge/License-GPL%20v3-blue?style=flat-square" alt="License">

</div>

---

## 关于《OverWrite》

回忆过去？天真。反反复复，周而复始。

**覆写世界，覆写意识**

### 核心特性

- **你就是你自己** 不是别人，是我自己
- **死亡** 我吗？
- **？** 

---

## 安装

### 依赖

**Arch Linux:**
```bash
sudo pacman -S vulkan-headers vulkan-tools glfw glm cmake gcc
```

**Ubuntu/Debian:**
```bash
sudo apt install vulkan-sdk libglfw3-dev libglm-dev cmake build-essential
```

### 构建 & 运行

```bash
./build.sh              # Release 构建
./build.sh debug        # Debug 构建（Vulkan 验证层）
./build.sh run          # 构建并运行客户端
./build.sh run-server   # 构建并运行服务端
```

### 打包分发

```bash
./build.sh package
```

输出 `overwrite-<VERSION>-linux-x86_64.tar.xz`, 包含二进制、运行库、素材、配置、启动脚本。解压即可运行：

```bash
tar -xf overwrite-<VERSION>-linux-x86_64.tar.xz && cd overwrite-<VERSION>-linux-x86_64 && ./run.sh
```

### 完整命令参考

| 命令 | 说明 |
|---|---|
| `./build.sh` | Release 构建所有目标 |
| `./build.sh debug` | Debug 构建（验证层 + 调试符号 + 警告全开） |
| `./build.sh run` | 构建并启动客户端 |
| `./build.sh run-server` | 构建并启动服务端 |
| `./build.sh clean` | 清空构建目录和着色器缓存 |
| `./build.sh package` | 打包为可分发 tar.xz |
| `./build.sh test` | 构建并运行单元测试 |

---

## 开发工作流

### 提交推送

```bash
git add .
git commit -m "feat: 添加xxx功能"
git push
```

**再见**