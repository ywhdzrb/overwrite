#version 450

// Uniform 变量
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
} pushConstants;

// 输出到片段着色器的颜色
layout(location = 0) out vec4 outColor;

// 暂时不使用顶点缓冲区，生成固定顶点
// 顶点索引：0, 1, 2 对应三角形三个顶点
void main() {
    vec3 position;
    vec3 color;
    
    // 基于顶点索引生成三角形
    if (gl_VertexIndex == 0) {
        position = vec3(0.0, -0.5, 0.0);
        color = vec3(1.0, 0.0, 0.0); // 红色
    } else if (gl_VertexIndex == 1) {
        position = vec3(0.5, 0.5, 0.0);
        color = vec3(0.0, 1.0, 0.0); // 绿色
    } else {
        position = vec3(-0.5, 0.5, 0.0);
        color = vec3(0.0, 0.0, 1.0); // 蓝色
    }
    
    // 应用变换矩阵
    gl_Position = pushConstants.proj * pushConstants.view * pushConstants.model * vec4(position, 1.0);
    
    // 传递颜色到片段着色器
    outColor = vec4(color, 1.0);
}