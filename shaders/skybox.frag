#version 450

// 使用顶点着色器传递的位置作为纹理坐标
layout(location = 0) in vec3 fragTexCoord;

// 立方体贴图
layout(binding = 0) uniform samplerCube skybox;

// 输出颜色
layout(location = 0) out vec4 outColor;

void main() {
    // 从立方体贴图中采样
    vec3 color = texture(skybox, fragTexCoord).rgb;
    
    // 输出颜色
    outColor = vec4(color, 1.0);
}