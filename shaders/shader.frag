#version 450

// 从顶点着色器接收的输入
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

// 输出颜色
layout(location = 0) out vec4 outColor;

void main() {
    // 简单的颜色输出，可以在这里添加纹理采样
    outColor = vec4(fragColor, 1.0);
    
    // 如果有纹理，可以这样采样：
    // outColor = texture(textureSampler, fragTexCoord);
}