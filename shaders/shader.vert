#version 450

// 顶点位置属性（location = 0）
layout(location = 0) in vec3 inPosition;
// 顶点法线属性（location = 1）
layout(location = 1) in vec3 inNormal;
// 顶点颜色属性（location = 2）
layout(location = 2) in vec3 inColor;
// 纹理坐标属性（location = 3）
layout(location = 3) in vec2 inTexCoord;

// Uniform 变量
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
} pushConstants;

// 输出到片段着色器
layout(location = 0) out vec3 fragPos;       // 世界空间位置
layout(location = 1) out vec3 fragNormal;    // 法线
layout(location = 2) out vec3 fragColor;     // 颜色

void main() {
    // 计算世界空间位置
    vec4 worldPos = pushConstants.model * vec4(inPosition, 1.0);
    fragPos = worldPos.xyz;
    
    // 应用变换矩阵
    gl_Position = pushConstants.proj * pushConstants.view * worldPos;

    // Vulkan的NDC坐标系Y轴是向下的，需要反转Y轴
    gl_Position.y = -gl_Position.y;

    // 计算法线（使用法线矩阵，处理非均匀缩放）
    mat3 normalMatrix = transpose(inverse(mat3(pushConstants.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    
    // 传递颜色到片段着色器
    fragColor = inColor;
}