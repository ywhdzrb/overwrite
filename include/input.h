#ifndef INPUT_H
#define INPUT_H

#include <GLFW/glfw3.h>
#include <memory>
#include <array>

namespace vgame {

class Input {
public:
    Input(GLFWwindow* window);
    ~Input() = default;

    // 更新输入状态
    void update();
    
    // 键盘状态
    bool isKeyPressed(int key) const;
    bool isKeyJustPressed(int key) const;
    bool isKeyJustReleased(int key) const;
    
    // 鼠标状态
    bool isMouseButtonPressed(int button) const;
    bool isMouseButtonJustPressed(int button) const;
    bool isMouseButtonJustReleased(int button) const;
    
    // 获取鼠标位置
    void getMousePosition(double& x, double& y) const;
    
    // 获取鼠标移动偏移
    void getMouseDelta(double& deltaX, double& deltaY) const;
    
    // 获取鼠标滚轮偏移
    void getScrollDelta(double& deltaX, double& deltaY) const;
    
    // 捕获/释放鼠标
    void setCursorCaptured(bool captured);
    bool isCursorCaptured() const { return cursorCaptured; }
    
    // 获取原始鼠标移动（在禁用模式下）
    void getRawMouseMovement(double& deltaX, double& deltaY);
    
    // 重置"刚刚按下"标志（在帧结束时调用）
    void resetJustPressedFlags();
    
    // 便捷方法：常用的按键状态
    bool isForwardPressed() const { return keys[GLFW_KEY_W] || keys[GLFW_KEY_UP]; }
    bool isBackPressed() const { return keys[GLFW_KEY_S] || keys[GLFW_KEY_DOWN]; }
    bool isLeftPressed() const { return keys[GLFW_KEY_A] || keys[GLFW_KEY_LEFT]; }
    bool isRightPressed() const { return keys[GLFW_KEY_D] || keys[GLFW_KEY_RIGHT]; }
    bool isJumpPressed() const { return keys[GLFW_KEY_SPACE]; }
    bool isJumpJustPressed() const { return jumpJustPressed; }
    bool isSprintPressed() const { return keys[GLFW_KEY_LEFT_SHIFT]; }

private:
    // 更新上一帧的状态
    void updatePreviousStates();
    
    // 手动检查鼠标移动（用于禁用模式）
    void checkMouseMovement();
    
    // GLFW回调函数声明
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

private:
    GLFWwindow* window;
    
    // 键盘状态
    std::array<bool, GLFW_KEY_LAST> keys;
    std::array<bool, GLFW_KEY_LAST> previousKeys;
    
    // 鼠标按钮状态
    std::array<bool, 8> mouseButtons;
    std::array<bool, 8> previousMouseButtons;
    
// 鼠标移动
    double mouseX;
    double mouseY;
    double previousMouseX;
    double previousMouseY;
    
    // 上一帧的回调位置（用于计算增量）
    double previousCallbackX;
    double previousCallbackY;
    
    // 鼠标滚轮
    double scrollX;
    double scrollY;
    
    // 鼠标捕获状态
    bool cursorCaptured;
    
    // "刚刚按下"标志
    bool jumpJustPressed;
    
    // 平滑后的鼠标移动
    double smoothMouseX;
    double smoothMouseY;
};

} // namespace vgame

#endif // INPUT_H