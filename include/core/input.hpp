#pragma once

/**
 * @file input.h
 * @brief 输入模块 — 键盘/鼠标状态管理、按键按下/释放/刚刚按下检测
 *
 * 归属模块：core
 * 核心职责：封装 GLFW 输入回调，提供按键/鼠标/滚轮状态查询接口
 * 依赖关系：GLFW
 * 关键设计：帧同步设计 — update() 在每帧开头调用，resetJustPressedFlags() 在帧末调用
 */

#include <array>
#include <memory>

#include <GLFW/glfw3.h>

namespace owengine {

class Input {
public:
    Input(GLFWwindow* window);
    ~Input() = default;

    // 更新输入状态
    void update();
    
    // 键盘状态
    [[nodiscard]] bool isKeyPressed(int key) const noexcept;
    [[nodiscard]] bool isKeyJustPressed(int key) const noexcept;
    [[nodiscard]] bool isKeyJustReleased(int key) const noexcept;
    
    // 鼠标状态
    [[nodiscard]] bool isMouseButtonPressed(int button) const noexcept;
    [[nodiscard]] bool isMouseButtonJustPressed(int button) const noexcept;
    [[nodiscard]] bool isMouseButtonJustReleased(int button) const noexcept;
    
    // 获取鼠标位置
    void getMousePosition(double& x, double& y) const noexcept;
    
    // 获取鼠标移动偏移
    void getMouseDelta(double& deltaX, double& deltaY) const noexcept;
    
    // 获取鼠标滚轮偏移
    void getScrollDelta(double& deltaX, double& deltaY) const noexcept;
    
    // 获取并重置滚轮 Y 偏移（用于热键切换等单次消费场景）
    [[nodiscard]] double consumeScrollY() noexcept;
    
    // 捕获/释放鼠标
    void setCursorCaptured(bool captured);
    [[nodiscard]] bool isCursorCaptured() const noexcept { return cursorCaptured_; }
    
    // 获取原始鼠标移动（在禁用模式下）
    void getRawMouseMovement(double& deltaX, double& deltaY) noexcept;
    
    // 重置"刚刚按下"标志（在帧结束时调用）
    void resetJustPressedFlags() noexcept;
    
    // 便捷方法：常用的按键状态
    [[nodiscard]] bool isForwardPressed() const noexcept { return keys_[GLFW_KEY_W] || keys_[GLFW_KEY_UP]; }
    [[nodiscard]] bool isBackPressed() const noexcept { return keys_[GLFW_KEY_S] || keys_[GLFW_KEY_DOWN]; }
    [[nodiscard]] bool isLeftPressed() const noexcept { return keys_[GLFW_KEY_A] || keys_[GLFW_KEY_LEFT]; }
    [[nodiscard]] bool isRightPressed() const noexcept { return keys_[GLFW_KEY_D] || keys_[GLFW_KEY_RIGHT]; }
    [[nodiscard]] bool isJumpPressed() const noexcept { return keys_[GLFW_KEY_SPACE]; }
    [[nodiscard]] bool isJumpJustPressed() const noexcept { return jumpJustPressed_; }
    [[nodiscard]] bool isSprintPressed() const noexcept { return keys_[GLFW_KEY_LEFT_SHIFT]; }
    [[nodiscard]] bool isFreeCameraJustPressed() const noexcept { 
        return isKeyJustPressed(GLFW_KEY_R); 
    }

private:
    // 更新上一帧的状态
    void updatePreviousStates() noexcept;
    
    // 手动检查鼠标移动（用于禁用模式）
    void checkMouseMovement() noexcept;
    
    // GLFW回调函数声明
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    /** @brief 单例实例指针，替代 glfwGetWindowUserPointer（ECS InputSystem 会覆盖 user pointer） */
    static Input* s_instance;

private:
    GLFWwindow* window_;
    
    // 键盘状态
    std::array<bool, GLFW_KEY_LAST> keys_;
    std::array<bool, GLFW_KEY_LAST> previousKeys_;
    
    // 鼠标按钮状态
    std::array<bool, 8> mouseButtons_;
    std::array<bool, 8> previousMouseButtons_;
    
    // 鼠标移动
    double mouseX_;
    double mouseY_;
    double previousMouseX_;
    double previousMouseY_;
    
    // 上一帧的回调位置（用于计算增量）
    double previousCallbackX_;
    double previousCallbackY_;
    
    // 鼠标滚轮
    double scrollX_;
    double scrollY_;
    
    // 前一个回调（由 ImGui 注册，Input 创建后自动链式转发）
    GLFWmousebuttonfun prevMouseButtonCallback_ = nullptr;
    GLFWcursorposfun prevCursorPosCallback_ = nullptr;
    GLFWscrollfun prevScrollCallback_ = nullptr;
    
    // 鼠标捕获状态
    bool cursorCaptured_;
    
    // "刚刚按下"标志
    bool jumpJustPressed_;
    
    // 平滑后的鼠标移动
    double smoothMouseX_;
    double smoothMouseY_;
};

} // namespace owengine
