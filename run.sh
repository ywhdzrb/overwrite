#!/bin/bash
# 使用默认平台（尝试 X11，因为 Wayland 键盘输入不工作）
unset GLFW_PLATFORM
./build/OverWrite