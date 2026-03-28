#version 450

// 顶点位置属性（location = 0）
layout(location = 0) in vec3 inPosition;
// 顶点颜色属性（location = 1）
layout(location = 1) in vec3 inColor;

// Uniform 变量
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
} pushConstants;

// 输出到片段着色器的颜色
layout(location = 0) out vec3 fragColor;

void main() {
    // 应用变换矩阵
    gl_Position = pushConstants.proj * pushConstants.view * pushConstants.model * vec4(inPosition, 1.0);
    
    // 传递颜色到片段着色器
    fragColor = inColor;
}