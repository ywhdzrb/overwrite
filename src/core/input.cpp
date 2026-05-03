// 输入处理实现
// 处理键盘和鼠标输入
#include "core/input.h"
#include <cstring>
#include <iostream>
#include <cmath>


namespace owengine {

Input* Input::s_instance = nullptr;

Input::Input(GLFWwindow* window)
    : window_(window),
      mouseX_(0.0),
      mouseY_(0.0),
      previousMouseX_(0.0),
      previousMouseY_(0.0),
      previousCallbackX_(0.0),
      previousCallbackY_(0.0),
      scrollX_(0.0),
      scrollY_(0.0),
      cursorCaptured_(false),
      jumpJustPressed_(false),
      smoothMouseX_(0.0),
      smoothMouseY_(0.0) {
    
    // 注册单例实例（ECS InputSystem 会覆盖 glfwSetWindowUserPointer，因此回调中用 s_instance 代替）
    s_instance = this;
    
    // 初始化状态数组
    keys_.fill(false);
    previousKeys_.fill(false);
    mouseButtons_.fill(false);
    previousMouseButtons_.fill(false);
    
    // 设置回调函数
    glfwSetWindowUserPointer(window, this);
    
    // 启用原始输入模式
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    
    glfwSetKeyCallback(window, keyCallback);
    // 保存前一个回调（ImGui 注册的），后续回调函数会自动链式转发
    prevMouseButtonCallback_ = glfwSetMouseButtonCallback(window, mouseButtonCallback);
    prevCursorPosCallback_ = glfwSetCursorPosCallback(window, cursorPosCallback);
    prevScrollCallback_ = glfwSetScrollCallback(window, scrollCallback);
    
    // 获取初始鼠标位置
    glfwGetCursorPos(window_, &mouseX_, &mouseY_);
    previousMouseX_ = mouseX_;
    previousMouseY_ = mouseY_;
}

void Input::update() {
    // 重置滚轮偏移
    scrollX_ = 0.0;
    scrollY_ = 0.0;
    
    // 在捕获模式下，Wayland 返回的值已经是增量
    // 直接使用 mouseX_/mouseY_，然后重置为 0
    if (cursorCaptured_) {
        // 不需要日志输出
    } else {
        // 非捕获模式，使用绝对位置
        checkMouseMovement();
    }
    
    // 检测"刚刚按下"状态（在这一帧开始时）
    jumpJustPressed_ = isKeyJustPressed(GLFW_KEY_SPACE);
    
    // 在帧结束时，将当前状态保存到 previousKeys_（为下一帧做准备）
    updatePreviousStates();
}

void Input::resetJustPressedFlags() noexcept {
    jumpJustPressed_ = false;
}

void Input::checkMouseMovement() noexcept {
    // 在非捕获模式下，手动轮询鼠标位置
    if (!cursorCaptured_) {
        double currentX, currentY;
        glfwGetCursorPos(window_, &currentX, &currentY);
        
        // 更新鼠标位置
        mouseX_ = currentX;
        mouseY_ = currentY;
    }
}

void Input::getRawMouseMovement(double& deltaX, double& deltaY) noexcept {
    if (cursorCaptured_) {
        // 在捕获模式下，mouseX_/mouseY_ 是相对移动量
        // 直接使用，不进行平滑处理
        deltaX = mouseX_;
        deltaY = mouseY_;
        
        // 使用后清零
        mouseX_ = 0.0;
        mouseY_ = 0.0;
    } else {
        // 在非捕获模式下，计算差值
        deltaX = mouseX_ - previousMouseX_;
        deltaY = mouseY_ - previousMouseY_;
    }
}

void Input::updatePreviousStates() noexcept {
    // 保存上一帧的键盘状态
    for (size_t i = 0; i < keys_.size(); i++) {
        previousKeys_[i] = keys_[i];
    }
    
    // 保存上一帧的鼠标按钮状态
    for (size_t i = 0; i < mouseButtons_.size(); i++) {
        previousMouseButtons_[i] = mouseButtons_[i];
    }
}

bool Input::isKeyPressed(int key) const noexcept {
    if (key >= 0 && key < GLFW_KEY_LAST) {
        return keys_[key];
    }
    return false;
}

bool Input::isKeyJustPressed(int key) const noexcept {
    if (key >= 0 && key < GLFW_KEY_LAST) {
        return keys_[key] && !previousKeys_[key];
    }
    return false;
}

bool Input::isKeyJustReleased(int key) const noexcept {
    if (key >= 0 && key < GLFW_KEY_LAST) {
        return !keys_[key] && previousKeys_[key];
    }
    return false;
}

bool Input::isMouseButtonPressed(int button) const noexcept {
    if (button >= 0 && button < 8) {
        return mouseButtons_[button];
    }
    return false;
}

bool Input::isMouseButtonJustPressed(int button) const noexcept {
    if (button >= 0 && button < 8) {
        return mouseButtons_[button] && !previousMouseButtons_[button];
    }
    return false;
}

bool Input::isMouseButtonJustReleased(int button) const noexcept {
    if (button >= 0 && button < 8) {
        return !mouseButtons_[button] && previousMouseButtons_[button];
    }
    return false;
}

void Input::getMousePosition(double& x, double& y) const noexcept {
    x = mouseX_;
    y = mouseY_;
}

void Input::getMouseDelta(double& deltaX, double& deltaY) const noexcept {
    deltaX = mouseX_ - previousMouseX_;
    deltaY = mouseY_ - previousMouseY_;
}

void Input::getScrollDelta(double& deltaX, double& deltaY) const noexcept {
    deltaX = scrollX_;
    deltaY = scrollY_;
}

double Input::consumeScrollY() noexcept {
    double value = scrollY_;
    scrollY_ = 0.0;
    return value;
}

void Input::setCursorCaptured(bool captured) {
    cursorCaptured_ = captured;
    if (captured) {
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        // 在捕获模式下，鼠标位置被固定在中心（通常是 0,0）
        // 重置 previousMouseX/Y 为 0，避免第一帧的大偏移
        previousMouseX_ = 0.0;
        previousMouseY_ = 0.0;
        mouseX_ = 0.0;
        mouseY_ = 0.0;
    } else {
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        // 切换到非捕获模式时，获取当前的绝对位置
        glfwGetCursorPos(window_, &mouseX_, &mouseY_);
        previousMouseX_ = mouseX_;
        previousMouseY_ = mouseY_;
    }
}

// GLFW回调函数实现
void Input::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // 使用 s_instance 而非 glfwGetWindowUserPointer
    // （ECS InputSystem 会覆盖 user pointer，与 scrollCallback 同理）
    if (s_instance) {
        if (key >= 0 && key < GLFW_KEY_LAST) {
            if (action == GLFW_PRESS) {
                s_instance->keys_[key] = true;
            } else if (action == GLFW_RELEASE) {
                s_instance->keys_[key] = false;
            }
            // GLFW_REPEAT 不需要特殊处理
        }
    }
}

void Input::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    // 链式转发给前一个回调（ImGui 的鼠标按钮回调）
    if (s_instance && s_instance->prevMouseButtonCallback_) {
        s_instance->prevMouseButtonCallback_(window, button, action, mods);
    }
    // ECS InputSystem 会覆盖 glfwGetWindowUserPointer，因此使用 s_instance
    if (s_instance && button >= 0 && button < 8) {
        if (action == GLFW_PRESS) {
            s_instance->mouseButtons_[button] = true;
        } else if (action == GLFW_RELEASE) {
            s_instance->mouseButtons_[button] = false;
        }
    }
}

void Input::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    // 链式转发给前一个回调（ImGui 的光标位置回调）
    if (s_instance && s_instance->prevCursorPosCallback_) {
        s_instance->prevCursorPosCallback_(window, xpos, ypos);
    }
    // ECS InputSystem 会覆盖 glfwGetWindowUserPointer，因此使用 s_instance
    if (s_instance) {
        if (s_instance->isCursorCaptured()) {
            // 在捕获模式下，Wayland 返回的是相对于中心的位置（会累积）
            // 计算相对于上一帧位置的增量
            double deltaX = xpos - s_instance->previousMouseX_;
            double deltaY = ypos - s_instance->previousMouseY_;
            
            // 累积增量到 mouseX_/mouseY_
            s_instance->mouseX_ += deltaX;
            s_instance->mouseY_ += deltaY;
            
            // 更新上一帧的位置
            s_instance->previousMouseX_ = xpos;
            s_instance->previousMouseY_ = ypos;
        } else {
            // 非捕获模式，使用绝对位置
            s_instance->mouseX_ = xpos;
            s_instance->mouseY_ = ypos;
            s_instance->previousMouseX_ = xpos;
            s_instance->previousMouseY_ = ypos;
        }
    }
}

void Input::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    // 注意：ECS InputSystem 会覆盖 glfwGetWindowUserPointer，因此使用 s_instance 代替
    if (s_instance) {
        s_instance->scrollX_ += xoffset;
        s_instance->scrollY_ += yoffset;
        // 链式转发给前一个回调（ImGui 的滚轮回调），确保 ImGui 窗口能滚动
        if (s_instance->prevScrollCallback_) {
            s_instance->prevScrollCallback_(window, xoffset, yoffset);
        }
    }
}

} // namespace owengine