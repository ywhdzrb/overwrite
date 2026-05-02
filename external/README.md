# 第三方依赖（Git Submodules）

全部依赖通过 Git Submodule 管理，禁止系统全局散依赖。
初始化：`git submodule update --init --recursive`

## 依赖清单

| 目录 | 库 | 用途 | 许可证 |
|---|---|---|---|
| `entt/` | [EnTT](https://github.com/skypjack/entt) | ECS 框架（核心架构） | MIT |
| `imgui/` | [Dear ImGui](https://github.com/ocornut/imgui) | 调试 HUD / 开发面板 | MIT |
| `ixwebsocket/` | [IXWebSocket](https://github.com/machinezone/IXWebSocket) | WebSocket 客户端 + 服务端 | BSD-3 |
| `nlohmann_json/` | [JSON for Modern C++](https://github.com/nlohmann/json) | JSON 序列化/反序列化（配置 + 网络协议） | MIT |
| `tinygltf/` | [TinyGLTF](https://github.com/syoyo/tinygltf) | glTF 2.0 模型加载 | MIT |
| `tinyobjloader/` | [TinyOBJLoader](https://github.com/tinyobjloader/tinyobjloader) | OBJ 模型加载（回退格式） | MIT |

## 注意事项

- **ixwebsocket**：同时用于客户端（`network_system.cpp`）和服务端（`websocket_server.cpp`），`USE_TLS=OFF` 编译（系统中通过 `libssl-dev` 提供 OpenSSL 符号）。
- **EnTT**：仅需头文件，通过 `single_include/` 引入。
- **nlohmann_json**：仅需头文件，通过 `include/` 引入。
- **FSR1**（AMD FidelityFX Super Resolution 1.0）：非 submodule，直接内嵌源码实现（`src/renderer/fsr1_pass.cpp/.h`，着色器 `shaders/fsr1_easu.comp` / `fsr1_rcas.comp`），许可证遵循 AMD GPUOpen MIT 协议。

## 版本锁定

各 submodule 指向经过测试的稳定版本，更新前请确认不影响现有功能。
