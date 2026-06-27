<div align="center">

<h1>OverWrite</h1>

**开源 · 社区驱动 · 二次元风格**

**开放世界 × 基建建造 × 角色收集 × 碎片化叙事**

<img src="https://img.shields.io/badge/C++-20-00599C?style=flat-square&logo=c%2B%2B" alt="C++20">
<img src="https://img.shields.io/badge/Vulkan-1.3+-AC162C?style=flat-square&logo=vulkan" alt="Vulkan">
<img src="https://img.shields.io/badge/Platform-Linux-FCC624?style=flat-square&logo=linux" alt="Linux">
<img src="https://img.shields.io/badge/License-GPL%20v3-blue?style=flat-square" alt="License">

</div>

---

## 关于《OverWrite》

世界已被覆写三次。第一次文明、第二次文明、第三次文明——都已化为废墟。现在，是第四次文明的开端。

你是由旧世界碎片聚合而成的意识体——**"回声"**。在废墟之上探索、建造、记录，在这个被反复覆写的世界中留下属于你的印记。

**这不是一个商业产品，而是一场关于共建、记忆与覆写的实验。**

### 核心特性

- **开放世界探索** - 自由探索废墟世界，碎片化剧情散落各地
- **基建系统** - 复杂的自动化流水线，从废墟中提取资源
- **自由建造** - 类似《我的世界》的建造自由度
- **角色与配饰** - 纯外观收集，无数值养成
- **社区驱动** - 游戏内容来自玩家提议，贡献者获得专属奖励

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
