<div align="center">

<img src="assets/logo/logo.png" alt="OverWrite Logo" width="480">

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

### 构建

```bash
./build.sh
```

### 运行

```bash
./run.sh
# 或者
./build.sh run
```

---

## 操作

| 按键 | 功能 |
|------|------|
| `WASD` | 移动 |
| `Mouse` | 视角控制 |
| `Space` | 上升（飞行） |
| `Shift` | 下降 |
| `R` | 切换自由视角 |
| `ESC` | 开发者模式 |

---

## 许可证

本项目采用双重许可：

- **源代码**: [GPL v3](LICENSE) - 适用 `src/`、`include/`、`shaders/` 目录
- **美术资产**: [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) - 适用 `assets/` 目录

---

<div align="center">

**玩家不是消费者，而是世界的居民和共同作者。**

</div>
