<div align="center">

<img src="assets/logo/logo.png" alt="OverWrite Logo" width="480">

**开源 · 社区驱动 · 二次元风格**

**开放世界 × 基建建造 × 角色收集 × 碎片化叙事**

<img src="https://img.shields.io/badge/C++-20-00599C?style=flat-square&logo=c%2B%2B" alt="C++20">
<img src="https://img.shields.io/badge/Vulkan-1.3+-AC162C?style=flat-square&logo=vulkan" alt="Vulkan">
<img src="https://img.shields.io/badge/Platform-Linux-FCC624?style=flat-square&logo=linux" alt="Linux">
<img src="https://img.shields.io/badge/License-GPL%20v3-blue?style=flat-square" alt="License">
<img src="https://img.shields.io/badge/艺术内容-CC%20BY--NC--ND%204.0-lightgrey?style=flat-square" alt="Art License">

</div>

---

## 关于《OverWrite》

世界已被覆写三次。第一次文明、第二次文明、第三次文明——都已化为废墟。现在，是第四次文明的开端。

你是由旧世界碎片聚合而成的意识体——**"回声"**。在废墟之上探索、建造、记录，在这个被反复覆写的世界中留下属于你的印记。

**这不是一个商业产品，而是一场关于共建、记忆与覆写的实验。**

---

## 核心特性

### 开放世界探索

- 自由探索废墟世界，发现前三个文明留下的痕迹
- 碎片化剧情散落各地，无强制主线，按你自己的节奏拼凑故事
- NPC 拥有独立生活轨迹，会记住你的互动

### 基建系统

- 复杂的自动化流水线，从废墟中提取可用资源
- 跨维度物流、多步加工、精密规划
- 生产抽卡券和建造材料的终极工厂

### 自由建造

- 类似《我的世界》的建造自由度
- 建造房屋、城市、工厂——在覆写之上留下印记
- 其他玩家可拜访参观你的世界

### 角色与配饰

- **三类角色**：剧情角色（建立关系）、贡献角色（开源贡献解锁）、基建角色（硬核流水线奖励）
- **配饰系统**："文明的遗物"，每件配饰有 3-5 层覆写，记录不同文明的故事
- 纯外观收集，无数值养成，不影响平衡

### 社区驱动

- 游戏内容来自玩家提议，高质量 Issue 可被采纳为剧情/活动
- 贡献者获得专属角色和配饰（含贡献者 ID 刻印）
- Wiki 由玩家共同维护，更新日志感谢每一位贡献者

---

## 经济系统

**非营利 · 无氪金 · 无内购**

| 抽卡资源获取途径 | 效率 | 特点 |
|------------------|------|------|
| 开源贡献（代码/Wiki/视频/Issue） | ~20 资源/小时 | 一次性爆发奖励 |
| 游戏内流水线生产 | 15-20 资源/小时 | 持续稳定，可自动化 |

- 抽卡产出**纯外观**，无任何数值加成
- 每位玩家独立卡池，互不影响
- 保留随机性，无保底机制

---

## 技术栈

| 层级 | 选型 |
|------|------|
| 客户端语言 | C++20 |
| 渲染引擎 | Vulkan 1.3+ |
| ECS 框架 | EnTT |
| 窗口/输入 | GLFW |
| 物理引擎 | Jolt Physics |
| UI 系统 | Dear ImGui |
| 音频 | miniaudio |
| 序列化 | protobuf |
| 网络 | Boost.Asio |
| 构建系统 | CMake + vcpkg |

---

## 快速开始

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

---

## 操作指南

| 按键 | 功能 |
|------|------|
| `WASD` | 移动 |
| `Mouse` | 视角控制 |
| `Space` | 跳跃 |
| `Shift` | 加速 |
| `R` | 切换自由视角 |
| `ESC` | 开发者模式 |

---

## 开发者面板

按 `ESC` 进入开发者模式，可实时调整：

- **性能统计** - FPS、帧时间、时间缩放
- **相机控制** - 位置、速度、灵敏度
- **光源管理** - 颜色、强度、位置、衰减参数
- **模型控制** - 位置、旋转、缩放
- **物理设置** - 重力、地面高度、跳跃力
- **渲染设置** - MSAA 级别、线框模式

---

## 开发路线图

### 第一阶段：核心引擎 (当前)

- [x] Vulkan 渲染管线
- [x] 相机系统（第一人称视角）
- [x] 基础物理系统
- [x] 3D 模型加载（OBJ/GLTF）
- [x] 动态光源系统
- [x] MSAA 抗锯齿
- [x] 开发者工具面板
- [x] 中文字体支持
- [ ] ECS 架构重构（EnTT）

### 第二阶段：玩家与世界

- [ ] 角色控制器完善
- [ ] 地形系统（无限世界生成）
- [ ] 方块/建筑放置系统
- [ ] 资源采集机制
- [ ] 存档系统

### 第三阶段：基建系统

- [ ] 流水线基础框架
- [ ] 物品传输系统
- [ ] 多步加工链
- [ ] 自动化逻辑
- [ ] 抽卡券生产线

### 第四阶段：角色与剧情

- [ ] 角色系统框架
- [ ] NPC AI 与行为树
- [ ] 碎片化剧情系统
- [ ] 配饰系统
- [ ] 对话系统

### 第五阶段：网络与社交

- [ ] 网络通信模块
- [ ] 账号系统
- [ ] 拜访他人世界
- [ ] 成就与展示
- [ ] 离线模式支持

### 第六阶段：社区集成

- [ ] GitHub OAuth 登录
- [ ] 贡献统计与奖励
- [ ] Wiki 集成
- [ ] Issue 提议系统
- [ ] 贡献者专属内容

### 第七阶段：打磨与发布

- [ ] 性能优化
- [ ] UI/UX 美化
- [ ] 音效与音乐
- [ ] 本地化支持
- [ ] 正式发布

---

## 项目结构

```
OverWrite/
├── include/           # 头文件
│   ├── vulkan_*.h     # Vulkan 核心组件
│   ├── renderer.h     # 主渲染器
│   ├── camera.h       # 相机系统
│   └── ...
├── src/
│   ├── core/          # 核心系统实现
│   ├── renderer/      # 渲染系统
│   └── utils/         # 工具函数
├── shaders/           # GLSL 着色器
├── assets/            # 模型和纹理
│   ├── models/        # 3D 模型
│   ├── textures/      # 纹理图片
│   └── logo/          # 项目 Logo
└── external/          # 第三方库
```

---

## 许可证

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

## 贡献指南

欢迎所有形式的贡献：

- **代码** - 提交 Pull Request
- **Wiki** - 编写文档和攻略
- **创意** - 在 Issues 中提交剧情/活动提议
- **反馈** - 报告 Bug 或提出建议
- **视频** - 制作游戏相关内容

贡献者将获得：
- 抽卡资源奖励
- 专属配饰/角色（含贡献者 ID 刻印）
- 更新日志特别鸣谢

---

<div align="center">

**玩家不是消费者，而是世界的居民和共同作者。**

**代码与创意在开源中不断覆写，每一次贡献都会留下痕迹。**

*"再怎么写也是在同一张纸上。"*

</div>