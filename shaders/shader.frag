#version 450

// 使用顶点着色器传递的颜色
layout(location = 0) in vec3 inColor;

// 输出颜色
layout(location = 0) out vec4 outColor;

void main() {
    // 使用顶点着色器传递的颜色，添加不透明度
    outColor = vec4(inColor, 1.0);
}