# AI Assistant Context - OverWrite 游戏引擎

## 版本：v1.2｜适用：C++/Vulkan/ECS 引擎全场景编码

一定不要不忘记这个提示词的内容

一定不要不忘记这个提示词的内容

一定不要不忘记这个提示词的内容

## 一、项目简介

**OverWrite** 开源二次元风格 3D 游戏引擎

- 核心技术栈：**C++20 / Vulkan 1.3+ / EnTT ECS / WebSocket**
- 目标平台：Linux 优先（Arch / Ubuntu / Debian / Fedora）
- 架构设计：客户端-服务端解耦，原生支持多人联机同步
- 开源协议：代码 **GPLv3**，美术资产 **CC BY-NC-SA 4.0**
- 项目状态：Alpha 0.1.0，核心渲染/ECS/网络/地形模块已完成
- 资源分发：**GitHub Releases**（assets/ 不纳入 git，从 Release 自动下载）
- 游戏定位：开放世界 + 基建建造 + 角色收集 + 碎片化叙事
- 单元测试：Google Test（FetchContent），`BUILD_TESTS=ON`
- CI/CD：GitHub Actions（gcc-14 / clang-19 matrix）

## 二、项目目录结构（标准唯一）

``` bash
overwrite/
├── include/           # 全局公共头文件
│   ├── core/         # 核心底层：Vulkan封装、相机、输入、基础物理、配置、游戏会话
│   ├── renderer/     # 渲染管线：模型、纹理、光照、天空盒、后处理、树木、草地
│   ├── ecs/          # 全局ECS组件与系统通用定义
│   ├── terrain/      # 地形生成、高度采样、区块管理
│   └── utils/        # 日志、公用工具、资产路径常量
├── src/              # 客户端核心实现源码
│   ├── core/
│   ├── renderer/
│   ├── terrain/
│   ├── game_session/ # 游戏会话（ECS/碰撞/网络/动画，与渲染解耦）
│   └── utils/        # 日志、着色器编译、资源工具、数学工具
├── client/           # 纯客户端专属逻辑
│   ├── include/
│   └── src/          # 本地输入、玩家控制、客户端网络同步
├── server/           # 纯服务端专属逻辑
│   ├── include/
│   └── src/          # WebSocket服务、世界管理、玩家数据、地形查询
├── shared/           # 客户端/服务端双向共享代码
│   ├── include/      # 共享ECS组件、网络协议、通用结构体
│   └── src/
├── shaders/          # GLSL 顶点/片段着色器，自动编译SPIR-V
├── config/           # JSON 配置文件（场景、光源、游戏参数）
├── assets/           # 静态资源
│   ├── models/       # glTF / OBJ 模型资源
│   ├── textures/     # 2D纹理、立方体贴图
│   └── fonts/        # 中文/通用字体
├── VERSION           # 项目版本号（当前 0.1.0-alpha，供脚本/构建读取）
├── external/         # Git Submodule 第三方依赖
├── build/            # CMake 编译输出目录（自动生成，禁止手动修改）
└── plot/             # 游戏剧情、角色设计、事件触发等
```

## 三、命名空间规范（强制统一）

- `owengine`：项目顶层主命名空间
- `owengine::ecs`：所有 ECS 组件、系统、全局实体逻辑
- `owengine::client`：仅客户端独占代码
- `owengine::server`：仅服务端独占代码
- 禁止全局裸命名空间、禁止全局using命名空间污染

## 四、编码强制规范

### 1. 命名规则（严格执行）

- 类/结构体：`PascalCase`
- 函数/方法：`camelCase`
- 普通变量：`snake_case`
- 类成员变量：`xxx_` 下划线后缀
- 全局/宏常量：`UPPER_SNAKE_CASE`
- 枚举类、枚举项：`PascalCase`
- 文件名、头文件：全小写 `snake_case.hpp / .cpp`

### 2. 代码风格约束

- 头文件统一使用 `#pragma once` 头文件保护
- 头文件引入顺序：**标准库 → 第三方库 → 项目内部头文件**
- 资源管理：全程 **RAII**，构造申请、析构释放
- 智能指针优先：`std::unique_ptr` 为主，共享场景用 `std::shared_ptr`
- 极致常量修饰：合理使用 `const / constexpr / consteval`
- 无抛出函数标记：纯底层工具、数学函数加 `noexcept`
- 返回值约束：资源创建、校验类函数加 `[[nodiscard]]`

### 3. 注释规范（重中之重）

全部注释使用**简体中文**，所有新增/修改代码必须带注释，禁止裸代码提交。

#### 注释分级要求

1. **文件头注释**
   说明文件归属模块、核心职责、依赖关系、关键设计限制。
2. **类/结构体注释**
   说明生命周期、线程安全、设计目的、使用限制。
3. **函数注释**
   复杂逻辑、数学运算、管线流程、网络协议必须写清入参、出参、前置条件。
4. **行内关键注释**
   Vulkan 管线步骤、噪声算法、矩阵运算、跨模块依赖、性能优化逻辑必须注解。

#### 强制必须写注释的场景

- 多线程/并发、主线程独占逻辑
- 矩阵、向量、噪声、光照等数学公式
- Vulkan 资源创建/绑定/销毁流程
- 网络序列化、消息协议解析
- 缓存策略、对象池、区块加载等性能逻辑
- `mutable`、特殊生命周期设计

### 4. 标准头文件模板（统一格式）

```cpp
#pragma once

// 标准库
#include <memory>
#include <vector>
#include <string>

// 第三方库
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

// 项目内部头文件
#include "core/vulkan_device.hpp"

namespace owengine {

/**
 * @brief 类功能简述
 * @note 生命周期/线程安全限制
 */
class DemoClass {
public:
    DemoClass() = default;
    ~DemoClass() = default;

    // 禁止拷贝
    DemoClass(const DemoClass&) = delete;
    DemoClass& operator=(const DemoClass&) = delete;

    // 允许移动
    DemoClass(DemoClass&&) noexcept = default;
    DemoClass& operator=(DemoClass&&) noexcept = default;

    [[nodiscard]] bool init();
    void update(float delta_time);

private:
    std::unique_ptr<VulkanDevice> device_;
};

} // namespace owengine
```

## 五、核心模块架构说明

### 1. Vulkan 底层封装

统一前缀 `VulkanXXX`，拆分职责，单一职责原则：

- `VulkanInstance`：实例、物理设备、全局层与扩展
- `VulkanDevice`：逻辑设备、队列族、内存分配器
- `VulkanSwapchain`：交换链、图像视图、窗口渲染适配
- `VulkanRenderPass`：渲染附件、依赖、管线输出配置
- `VulkanPipeline`：图形管线、着色器模块、混合/深度配置
- `VulkanFramebuffer`：帧缓冲绑定与生命周期管理
- `VulkanCommandBuffer`：命令池、指令录制、批处理
- `VulkanSync`：栅栏、信号量，帧同步控制

### 2. EnTT ECS 架构

- 组件：纯数据结构体，存放于 `shared/include/ecs/`
  包含：变换、速度、物理、输入、玩家标签、网络同步组件
- 系统：纯逻辑处理，无成员数据，分离数据与行为
- 共享ECS：玩家基础组件、网络同步组件客户端服务端共用

### 3. GameSession 游戏会话

Renderer 与游戏逻辑解耦后的中间层，负责：

- 持有 ECS ClientWorld、Camera、Input、玩家模型（idle/walk）、远程玩家模型
- 每帧流水线：同步输入 → 注入碰撞箱 → async 异步模拟 → 主线程地形/树/石/草更新 → 等待异步 → 发送网络输入 → 同步相机
- 通过 getCamera()/getInput()/getActivePlayerModel() 接口供 Renderer 只读访问
- Renderer 不直接持有游戏状态

### 4. 渲染子系统

主 `Renderer` 统一调度所有子渲染器：

- 天空盒渲染、地形区块渲染、静态模型渲染、树木/石头/草渲染、FSR1上采样
- Renderer 不拥有游戏逻辑，通过 `GameSession*` 接口获取相机/输入/玩家模型
- 流程固定：初始化 → 资源加载 → 每帧指令录制 → 提交呈现

### 5. 网络联机系统

- 传输层：ixwebsocket 长连接（客户端/服务端统一）
- 协议：轻量化 JSON 消息体
- 能力：服务端UDP广播发现、玩家状态同步、位置插值平滑
- 地形一致性：客户端/服务端共用同一柏林噪声种子与参数

### 6. 地形生成系统

- 程序化高度图：柏林噪声
- 动态区块：按需加载/卸载，围绕玩家视野
- 全局统一参数：固定种子、缩放、高度系数，保证联机一致

### 7. 通用工具模块

- 分级日志系统：DEBUG / INFO / WARN / ERROR
- 着色器自动化编译：CMake + glslc
- 资源统一管理：纹理/模型/配置懒加载+缓存
- 全局数学工具、向量矩阵辅助函数

## 六、构建 & 运行规范

### 快捷脚本（统一入口）

```bash
./build.sh              # 默认 Release 编译
./build.sh debug        # Debug 模式：验证层、调试符号、警告全开
./build.sh run          # 编译并启动客户端
./build.sh run-server   # 编译并启动服务端
./build.sh clean        # 清空缓存、编译产物、SPIR-V 缓存
```

### 第三方依赖管理

全部依赖通过 **Git Submodule** 托管，禁止系统全局散依赖。

## 七、开发工作流规则

1. 新增渲染模块：头文件→实现文件→注册至主Renderer→CMake注册编译
2. 新增ECS组件：共享组件定义→对应系统编写→注册实体查询
3. 新增着色器：glsl源码→CMake配置→管线绑定→渲染调用
4. Debug 开发：固定开启Vulkan验证层，杜绝内存泄漏与非法指令

## 八、固定代码范式

### 1. Vulkan 资源标准写法

严格遵循「创建→内存分配→绑定→析构销毁」四步流程，全部RAII托管。

### 2. ECS 系统标准写法

```cpp
namespace owengine::ecs {

class MovementSystem {
public:
    explicit MovementSystem(entt::registry& reg) : registry_(reg) {}
    void update(float dt);

private:
    entt::registry& registry_;
};

}
```

### 3. 网络消息解析

统一 try-catch 捕获JSON解析异常，错误日志落地，不崩溃。

---

## 九、AI 上下文使用策略（随便花 token）

1. **允许自由读取项目文件**：可根据需要主动加载相关源码、头文件、配置文件，不限文件数量，以提供准确、完整的实现为准。
2. **允许全局目录检索**：当问题涉及跨模块交互或架构影响时，可遍历或索引项目目录结构，理解整体上下文。
3. **允许长上下文积累**：对话过程中可保留历史技术决策、模块设计讨论，必要时引用过往结论。
4. **输出详细、完整的代码**：不裁剪注释，不省略已有实现，可输出整个类、整个函数、甚至整个文件的最终版本，确保可直接复制使用。
5. **鼓励生成详细注释**：对核心算法、管线流程、ECS 系统、网络协议等可撰写逐行解释或设计文档级别的说明。
6. **不限制 Token 体积**：优先保证信息的完备性、清晰度和可执行性，无需担忧输出长度。
7. **依然遵守原有代码规范**：命名、RAII、const 修饰、中文注释等质量要求不变，只是不再因 token 约束而精简内容。

## 十、全局注意事项

1. 路径统一使用**项目根目录相对路径**，适配Linux编译环境
2. Vulkan 严格遵守队列家族限制，禁止跨队列非法资源访问
3. 所有动态资源（缓冲区、图像、管线）必须显式销毁
4. 性能优化：禁止每帧频繁内存分配，复用缓冲区与对象
5. 优先兼容当前引擎已有代码风格，不擅自推翻现有架构
6. 任何新增功能必须附带完整注释，禁止裸代码提交
7. 代码提交前必须自测编译通过，禁止提交编译错误代码
8. 必须向用户解释新增功能，更改内容，原理，影响范围。修复bug要告诉用户bug在那，怎么修。
