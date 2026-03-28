#version 450

// 使用顶点着色器传递的颜色
layout(location = 0) in vec4 inColor;

// 输出颜色
layout(location = 0) out vec4 outColor;

void main() {
    // 使用顶点着色器传递的颜色
    outColor = inColor;
}