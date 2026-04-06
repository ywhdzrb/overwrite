# OverWrite 文档

欢迎使用 OverWrite 游戏引擎文档。本文档涵盖了引擎的所有主要模块和 API。

## 项目概览

OverWrite 是一个基于 Vulkan 的开源游戏引擎，采用 ECS（实体组件系统）架构，支持：

- Vulkan 1.3+ 渲染后端
- glTF 2.0 模型加载与动画
- PBR 材质系统
- 多类型光源（方向光、点光源、聚光灯）
- 多人联机（WebSocket）
- 跨平台支持（Linux）

## 文档目录

### 核心模块

- [Vulkan 封装](./core/vulkan.md) - Vulkan API 的封装类
- [相机系统](./core/camera.md) - 相机控制和视锥体剔除
- [输入系统](./core/input.md) - 键盘和鼠标输入处理
- [物理系统](./core/physics.md) - 基础物理模拟

### 渲染模块

- [渲染器](./renderer/renderer.md) - 主渲染器类
- [模型加载](./renderer/model.md) - OBJ 和 glTF 模型加载
- [纹理系统](./renderer/texture.md) - 纹理加载和管理
- [光源系统](./renderer/light.md) - 光源管理和着色器数据
- [渲染器组件](./renderer/renderers.md) - 地板、立方体、天空盒等渲染器

### ECS 架构

- [ECS 概述](./ecs/overview.md) - 实体组件系统架构说明
- [共享组件](./ecs/shared_components.md) - 客户端和服务端共用的组件
- [共享系统](./ecs/shared_systems.md) - 客户端和服务端共用的系统
- [客户端组件](./ecs/client_components.md) - 客户端专用组件
- [客户端系统](./ecs/client_systems.md) - 客户端专用系统

### 网络模块

- [客户端网络](./network/client_network.md) - 客户端网络系统
- [服务端网络](./network/server_network.md) - 服务端 WebSocket 服务器
- [服务器发现](./network/server_discovery.md) - 局域网服务器发现机制

### 工具模块

- [日志系统](./utils/logger.md) - 日志记录工具
- [着色器编译](./utils/shader_compiler.md) - GLSL 到 SPIR-V 编译

## 快速开始

### 构建项目

```bash
./build.sh
```

### 运行项目

```bash
./run.sh
```

## 项目结构

```
overwrite/
├── include/           # 公共头文件
├── src/               # 源文件
│   ├── core/         # 核心模块
│   ├── renderer/     # 渲染模块
│   └── utils/        # 工具模块
├── client/           # 客户端代码
├── server/           # 服务端代码
├── shared/           # 共享代码
├── shaders/          # GLSL 着色器
├── assets/           # 游戏资源
└── docs/             # 文档
```

## 命名空间

项目使用 `vgame` 作为主命名空间，子命名空间包括：

- `vgame::ecs` - ECS 系统
- `vgame::client` - 客户端专用代码
- `vgame::server` - 服务端专用代码
