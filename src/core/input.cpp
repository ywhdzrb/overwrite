// 输入处理实现
// 处理键盘和鼠标输入
#include "input.h"
#include <cstring>

namespace vgame {

Input::Input(GLFWwindow* window)
    : window(window),
      mouseX(0.0),
      mouseY(0.0),
      previousMouseX(0.0),
      previousMouseY(0.0),
      scrollX(0.0),
      scrollY(0.0),
      cursorCaptured(false) {
    
    // 初始化状态数组
    keys.fill(false);
    previousKeys.fill(false);
    mouseButtons.fill(false);
    previousMouseButtons.fill(false);
    
    // 设置回调函数
    glfwSetWindowUserPointer(window, this);
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
    // 保存上一帧的状态
    updatePreviousStates();
    
    // 重置滚轮偏移
    scrollX = 0.0;
    scrollY = 0.0;
    
    // 更新鼠标位置
    previousMouseX = mouseX;
    previousMouseY = mouseY;
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
        // 捕获后立即重置鼠标位置，避免第一帧的大偏移
        glfwGetCursorPos(window, &mouseX, &mouseY);
        previousMouseX = mouseX;
        previousMouseY = mouseY;
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

// GLFW回调函数实现
void Input::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input && key >= 0 && key < GLFW_KEY_LAST) {
        if (action == GLFW_PRESS) {
            input->keys[key] = true;
        } else if (action == GLFW_RELEASE) {
            input->keys[key] = false;
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
        input->mouseX = xpos;
        input->mouseY = ypos;
    }
}

void Input::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input) {
        input->scrollX = xoffset;
        input->scrollY = yoffset;
    }
}

} // namespace vgame