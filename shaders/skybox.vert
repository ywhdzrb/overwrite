#version 450

// 顶点位置属性
layout(location = 0) in vec3 inPosition;

// Uniform 变量
layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
} pushConstants;

// 输出到片段着色器的位置（用作立方体贴图纹理坐标）
layout(location = 0) out vec3 fragTexCoord;

void main() {
    // 天空盒应该始终在相机周围，所以移除 view 矩阵的平移部分
    mat4 viewWithoutTranslation = mat4(
        mat3(pushConstants.view)
    );

    // 应用变换矩阵
    gl_Position = pushConstants.proj * viewWithoutTranslation * vec4(inPosition, 1.0);

    // Vulkan的NDC坐标系Y轴是向下的，需要反转Y轴
    gl_Position.y = -gl_Position.y;

    // 将位置传递给片段着色器作为纹理坐标
    fragTexCoord = inPosition;
}