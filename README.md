## OverWrite

世界已经毁灭了三次。

第一次，耗尽了脚下的土地。第二次，迷失在编织的幻影里。第三次，被自己造出的东西反噬。

每一次毁灭，都不是终结。旧世界的废墟被掩埋，新的文明在废墟上生长，然后再次崩塌——像是有人在一张纸上反复书写，又反复擦去。

现在是第四次文明的黎明。你从一片废墟中醒来，不记得自己的名字，不记得自己来自哪里。身边只有一块停止的怀表，和一株从石缝里探出头的白花。

有人说你是“回声”，是前三次文明残留下来的碎片拼凑而成的意识体。也有人说你是“时间”，因为你身上叠着太多不属于这个时代的痕迹。

这个世界不会催你。废墟、荒原、旧文明的遗骸——它们都还在那里，等你自己决定要不要走进去。

**这是一个关于覆写与记忆的世界。而你，是它留下的最后一道痕迹。**

---

### 关于这个项目

《OverWrite》不是一款商业游戏。它是一个开源的、由社区共建的实验体。无论是代码、剧情、配饰还是玩法，都可以由玩家共同参与塑造。

你可以在废墟里搭流水线、盖房子、收集旧文明的遗物，也可以什么都不做，就坐在某个山顶看日落。没有强制任务，没有每日签到——你来了，你留下点什么，然后继续往前。

---

### 构建与运行

#### 依赖

- Vulkan 1.3+
- GLFW 3.3+
- GLM
- CMake 3.15+
- C++20 编译器

**Arch Linux:**

```bash
sudo pacman -S vulkan-headers vulkan-tools glfw glm cmake gcc
```

**Ubuntu/Debian:**

```bash
sudo apt install vulkan-sdk libglfw3-dev libglm-dev cmake build-essential
```

#### 构建

```bash
./build.sh              # Release
./build.sh debug        # Debug（Vulkan 验证层）
./build.sh run          # 构建并启动客户端
./build.sh run-server   # 构建并启动服务端
./build.sh clean        # 清理构建产物
./build.sh package      # 打包为 tar.xz
./build.sh test         # 运行测试
```

#### 打包分发

```bash
./build.sh package
```

生成 `overwrite-<VERSION>-linux-x86_64.tar.xz`，解压即可运行：

```bash
tar -xf overwrite-<VERSION>-linux-x86_64.tar.xz
cd overwrite-<VERSION>-linux-x86_64
./run.sh
```

---

### 开发工作流

```bash
git add .
git commit -m "feat: 描述改动内容"
git push
```

欢迎提交 PR 和 Issue。

---

### 许可证

- **代码**：GPLv3
- **美术资产（模型、贴图、音频等）**：CC BY-NC-SA 4.0

详见 `LICENSE-CODE` 和 `LICENSE-ART`。
