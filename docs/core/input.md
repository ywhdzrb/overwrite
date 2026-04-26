# 输入系统

输入系统处理键盘和鼠标输入，提供按键状态查询和鼠标移动检测。

## 类概览

| 类名 | 文件 | 描述 |
|------|------|------|
| `Input` | `include/core/input.h` | 输入管理类 |

---

## Input

管理键盘和鼠标输入状态。

### 类方法

#### 构造

```cpp
Input(GLFWwindow* window);
~Input() = default;
```

#### 更新

```cpp
// 每帧调用，更新输入状态
void update();

// 帧结束时调用，重置"刚刚按下"标志
void resetJustPressedFlags();
```

#### 键盘状态

```cpp
// 检查按键是否被按下
bool isKeyPressed(int key) const;

// 检查按键是否刚刚被按下（仅在当前帧）
bool isKeyJustPressed(int key) const;

// 检查按键是否刚刚被释放
bool isKeyJustReleased(int key) const;
```

#### 鼠标状态

```cpp
// 检查鼠标按钮是否被按下
bool isMouseButtonPressed(int button) const;

// 检查鼠标按钮是否刚刚被按下
bool isMouseButtonJustPressed(int button) const;

// 检查鼠标按钮是否刚刚被释放
bool isMouseButtonJustReleased(int button) const;
```

#### 鼠标位置

```cpp
// 获取鼠标位置
void getMousePosition(double& x, double& y) const;

// 获取鼠标移动偏移
void getMouseDelta(double& deltaX, double& deltaY) const;

// 获取鼠标滚轮偏移
void getScrollDelta(double& deltaX, double& deltaY) const;
```

#### 鼠标捕获

```cpp
// 捕获/释放鼠标（FPS 模式）
void setCursorCaptured(bool captured);

// 检查鼠标是否被捕获
bool isCursorCaptured() const;

// 获取原始鼠标移动（在禁用模式下）
void getRawMouseMovement(double& deltaX, double& deltaY);
```

#### 便捷方法

```cpp
// 常用的按键状态
bool isForwardPressed() const;   // W 或 上箭头
bool isBackPressed() const;      // S 或 下箭头
bool isLeftPressed() const;      // A 或 左箭头
bool isRightPressed() const;     // D 或 右箭头
bool isJumpPressed() const;      // 空格
bool isJumpJustPressed() const;  // 空格刚刚按下
bool isSprintPressed() const;    // 左 Shift
bool isFreeCameraJustPressed() const;  // R 键
```

---

## 使用示例

### 初始化

```cpp
// 创建输入管理器
auto input = std::make_unique<Input>(window);

// 捕获鼠标（FPS 游戏）
input->setCursorCaptured(true);
```

### 主循环

```cpp
void mainLoop() {
    // 更新输入状态
    input->update();
    
    // 处理移动输入
    if (input->isForwardPressed()) {
        player.moveForward();
    }
    if (input->isBackPressed()) {
        player.moveBackward();
    }
    if (input->isLeftPressed()) {
        player.moveLeft();
    }
    if (input->isRightPressed()) {
        player.moveRight();
    }
    
    // 处理跳跃
    if (input->isJumpJustPressed() && player.isGrounded()) {
        player.jump();
    }
    
    // 处理鼠标移动
    double deltaX, deltaY;
    input->getMouseDelta(deltaX, deltaY);
    camera.processMouseMovement(deltaX, deltaY);
    
    // 处理冲刺
    float speed = input->isSprintPressed() ? sprintSpeed : walkSpeed;
    
    // 帧结束时重置状态
    input->resetJustPressedFlags();
}
```

### 鼠标捕获

```cpp
// 进入 FPS 模式
void enterFPSMode() {
    input->setCursorCaptured(true);
}

// 退出 FPS 模式（显示菜单）
void exitFPSMode() {
    input->setCursorCaptured(false);
}

// ESC 键切换
if (input->isKeyJustPressed(GLFW_KEY_ESCAPE)) {
    if (input->isCursorCaptured()) {
        exitFPSMode();
    } else {
        enterFPSMode();
    }
}
```

### 滚轮处理

```cpp
double scrollX, scrollY;
input->getScrollDelta(scrollX, scrollY);

if (scrollY > 0) {
    // 放大
    camera.zoomIn();
} else if (scrollY < 0) {
    // 缩小
    camera.zoomOut();
}
```

---

## 按键常量

使用 GLFW 定义的按键常量：

```cpp
// 字母键
GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D
GLFW_KEY_Q, GLFW_KEY_E, GLFW_KEY_R, GLFW_KEY_F

// 方向键
GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_LEFT, GLFW_KEY_RIGHT

// 特殊键
GLFW_KEY_SPACE, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_LEFT_CONTROL
GLFW_KEY_ESCAPE, GLFW_KEY_ENTER, GLFW_KEY_TAB

// 鼠标按钮
GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT, GLFW_MOUSE_BUTTON_MIDDLE
```

---

## 实现细节

### 状态跟踪

输入系统跟踪三种状态：

1. **当前帧状态**：当前帧按键是否被按下
2. **上一帧状态**：上一帧按键是否被按下
3. **刚刚按下/释放**：通过比较当前帧和上一帧状态计算

### 回调函数

输入系统使用 GLFW 回调来处理输入事件：

```cpp
// 内部回调（自动设置）
static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
```

### 鼠标移动计算

```cpp
void update() {
    // 更新上一帧状态
    updatePreviousStates();
    
    // 检查鼠标移动（用于禁用光标模式）
    checkMouseMovement();
}
```

---

## 注意事项

1. **每帧调用 update()**：必须在每帧开始时调用
2. **帧结束重置**：调用 `resetJustPressedFlags()` 确保"刚刚按下"状态只持续一帧
3. **鼠标捕获**：捕获鼠标时，光标会隐藏并锁定在窗口中心
4. **线程安全**：输入回调在主线程执行，不要在其他线程调用输入方法
