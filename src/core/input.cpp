// 输入处理实现
// 处理键盘和鼠标输入
#include "input.h"
#include <cstring>
#include <iostream>
#include <cmath>


namespace owengine {

Input::Input(GLFWwindow* window)
    : window(window),
      mouseX(0.0),
      mouseY(0.0),
      previousMouseX(0.0),
      previousMouseY(0.0),
      previousCallbackX(0.0),
      previousCallbackY(0.0),
      scrollX(0.0),
      scrollY(0.0),
      cursorCaptured(false),
      jumpJustPressed(false),
      smoothMouseX(0.0),
      smoothMouseY(0.0) {
    
    // 初始化状态数组
    keys.fill(false);
    previousKeys.fill(false);
    mouseButtons.fill(false);
    previousMouseButtons.fill(false);
    
    // 设置回调函数
    glfwSetWindowUserPointer(window, this);
    
    // 启用原始输入模式
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    
    // 获取初始鼠标位置
    glfwGetCursorPos(window, &mouseX, &mouseY);
    previousMouseX = mouseX;
    previousMouseY = mouseY;
}

void Input::update() {
    // 重置滚轮偏移
    scrollX = 0.0;
    scrollY = 0.0;
    
    // 在捕获模式下，Wayland 返回的值已经是增量
    // 直接使用 mouseX/mouseY，然后重置为 0
    if (cursorCaptured) {
        // 不需要日志输出
    } else {
        // 非捕获模式，使用绝对位置
        checkMouseMovement();
    }
    
    // 检测"刚刚按下"状态（在这一帧开始时）
    jumpJustPressed = isKeyJustPressed(GLFW_KEY_SPACE);
    
    // 在帧结束时，将当前状态保存到 previousKeys（为下一帧做准备）
    updatePreviousStates();
}

void Input::resetJustPressedFlags() {
    jumpJustPressed = false;
}

void Input::checkMouseMovement() {
    // 在非捕获模式下，手动轮询鼠标位置
    if (!cursorCaptured) {
        double currentX, currentY;
        glfwGetCursorPos(window, &currentX, &currentY);
        
        // 计算相对移动
        double deltaX = currentX - previousMouseX;
        double deltaY = currentY - previousMouseY;
        
        // 更新鼠标位置
        mouseX = currentX;
        mouseY = currentY;
    }
}

void Input::getRawMouseMovement(double& deltaX, double& deltaY) {
    if (cursorCaptured) {
        // 在捕获模式下，mouseX/mouseY 是相对移动量
        // 直接使用，不进行平滑处理
        deltaX = mouseX;
        deltaY = mouseY;
        
        // 使用后清零
        mouseX = 0.0;
        mouseY = 0.0;
    } else {
        // 在非捕获模式下，计算差值
        deltaX = mouseX - previousMouseX;
        deltaY = mouseY - previousMouseY;
    }
}

void Input::updatePreviousStates() {
    // 保存上一帧的键盘状态
    for (size_t i = 0; i < keys.size(); i++) {
        previousKeys[i] = keys[i];
    }
    
    // 保存上一帧的鼠标按钮状态
    for (size_t i = 0; i < mouseButtons.size(); i++) {
        previousMouseButtons[i] = mouseButtons[i];
    }
}

bool Input::isKeyPressed(int key) const {
    if (key >= 0 && key < GLFW_KEY_LAST) {
        return keys[key];
    }
    return false;
}

bool Input::isKeyJustPressed(int key) const {
    if (key >= 0 && key < GLFW_KEY_LAST) {
        return keys[key] && !previousKeys[key];
    }
    return false;
}

bool Input::isKeyJustReleased(int key) const {
    if (key >= 0 && key < GLFW_KEY_LAST) {
        return !keys[key] && previousKeys[key];
    }
    return false;
}

bool Input::isMouseButtonPressed(int button) const {
    if (button >= 0 && button < 8) {
        return mouseButtons[button];
    }
    return false;
}

bool Input::isMouseButtonJustPressed(int button) const {
    if (button >= 0 && button < 8) {
        return mouseButtons[button] && !previousMouseButtons[button];
    }
    return false;
}

bool Input::isMouseButtonJustReleased(int button) const {
    if (button >= 0 && button < 8) {
        return !mouseButtons[button] && previousMouseButtons[button];
    }
    return false;
}

void Input::getMousePosition(double& x, double& y) const {
    x = mouseX;
    y = mouseY;
}

void Input::getMouseDelta(double& deltaX, double& deltaY) const {
    deltaX = mouseX - previousMouseX;
    deltaY = mouseY - previousMouseY;
}

void Input::getScrollDelta(double& deltaX, double& deltaY) const {
    deltaX = scrollX;
    deltaY = scrollY;
}

void Input::setCursorCaptured(bool captured) {
    cursorCaptured = captured;
    if (captured) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        // 在捕获模式下，鼠标位置被固定在中心（通常是 0,0）
        // 重置 previousMouseX/Y 为 0，避免第一帧的大偏移
        previousMouseX = 0.0;
        previousMouseY = 0.0;
        mouseX = 0.0;
        mouseY = 0.0;
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        // 切换到非捕获模式时，获取当前的绝对位置
        glfwGetCursorPos(window, &mouseX, &mouseY);
        previousMouseX = mouseX;
        previousMouseY = mouseY;
    }
}

// GLFW回调函数实现
void Input::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input) {
        if (key >= 0 && key < GLFW_KEY_LAST) {
            if (action == GLFW_PRESS) {
                input->keys[key] = true;
            } else if (action == GLFW_RELEASE) {
                input->keys[key] = false;
            }
            // GLFW_REPEAT 不需要特殊处理
        }
    }
}

void Input::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input && button >= 0 && button < 8) {
        if (action == GLFW_PRESS) {
            input->mouseButtons[button] = true;
        } else if (action == GLFW_RELEASE) {
            input->mouseButtons[button] = false;
        }
    }
}

void Input::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input) {
        if (input->isCursorCaptured()) {
            // 在捕获模式下，Wayland 返回的是相对于中心的位置（会累积）
            // 计算相对于上一帧位置的增量
            double deltaX = xpos - input->previousMouseX;
            double deltaY = ypos - input->previousMouseY;
            
            // 累积增量到 mouseX/mouseY
            input->mouseX += deltaX;
            input->mouseY += deltaY;
            
            // 更新上一帧的位置
            input->previousMouseX = xpos;
            input->previousMouseY = ypos;
        } else {
            // 非捕获模式，使用绝对位置
            input->mouseX = xpos;
            input->mouseY = ypos;
            input->previousMouseX = xpos;
            input->previousMouseY = ypos;
        }
    }
}

void Input::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input) {
        input->scrollX = xoffset;
        input->scrollY = yoffset;
    }
}

} // namespace owengine