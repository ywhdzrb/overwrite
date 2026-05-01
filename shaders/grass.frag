#version 450
// 草丛片段着色器（纯程序化颜色版）
//
// 功能：
//   1. 基于高度的渐变绿色，根部深绿→尖端浅绿/黄绿
//   2. 简单方向光漫反射 + 环境光
//   3. 世界坐标伪随机色调微调，增加视觉差异

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in float fragHeight;

layout(location = 0) out vec4 outColor;

void main() {
    // 基于高度渐变：基部深绿，尖端黄绿
    vec3 baseColor  = vec3(0.22, 0.55, 0.12);
    vec3 tipColor   = vec3(0.50, 0.80, 0.20);
    vec3 heightColor = mix(baseColor, tipColor, fragHeight);

    // 利用世界坐标生成微小的随机色调偏移 (±5%)
    vec3 floorPos = floor(fragWorldPos * 4.0);
    float h = fract(sin(dot(floorPos, vec3(12.9898, 78.233, 45.543))) * 43758.5453);
    float variation = h * 0.10 - 0.05;
    vec3 variedColor = heightColor + variation;

    // 简单方向光漫反射
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.3));
    float diff = max(dot(vec3(0.0, 1.0, 0.0), lightDir), 0.0) * 0.5 + 0.5;

    vec3 finalColor = variedColor * diff;

    outColor = vec4(finalColor, 1.0);
}
