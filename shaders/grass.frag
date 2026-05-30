#version 450
// 草丛片段着色器（纯程序化颜色）
//
// 功能：
//   1. 基于高度的渐变绿色，根部深绿→尖端浅绿/黄绿
//   2. 方向光漫反射 + 环境光，方向/强度从 push constants 获取（昼夜联动）
//   3. 世界坐标伪随机色调微调，增加视觉差异
//   4. 根部渐变为棕色以融入地面
//
// push constants 必须与顶点着色器和 C++ PushBlock 完全对齐。
// lightDir.xyz = 光照方向（场景→光源，归一化），lightDir.w = 漫反射强度 [0,1]
// ambientIntensity.xyz = 环境光颜色（由 Renderer 传入）

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in float fragHeight;

layout(location = 0) out vec4 outColor;

// push constants 与顶点着色器共享同一块内存，必须结构一致
layout(push_constant) uniform PushConstants {
    mat4 view;              // 0-63
    mat4 proj;              // 64-127
    vec4 timeParams;        // 128-143
    vec4 playerPos;         // 144-159
    vec4 lightDir;          // 160-175  xyz=光照方向, w=漫反射强度
    vec4 ambientIntensity;  // 176-191  xyz=环境光颜色, w未用
} push;

void main() {
    // 基于高度渐变：根部深绿/棕，尖端黄绿
    vec3 baseColor  = vec3(0.22, 0.35, 0.10);
    vec3 tipColor   = vec3(0.50, 0.80, 0.20);
    vec3 heightColor = mix(baseColor, tipColor, fragHeight);

    // 利用世界坐标生成微小的随机色调偏移 (±5%)
    vec3 floorPos = floor(fragWorldPos * 4.0);
    float h = fract(sin(dot(floorPos, vec3(12.9898, 78.233, 45.543))) * 43758.5453);
    float variation = h * 0.10 - 0.05;
    vec3 variedColor = heightColor + variation;

    // === 光照计算（与 terrain shader.frag 公式一致） ===
    // 环境光：由 Renderer 传入 ambientColor * ambientIntensity（完整 RGB）
    vec3 ambientColor = push.ambientIntensity.xyz;

    // 漫反射：标准 Blinn-Phong 漫反射项（草无法线，使用向上方向近似平地）
    vec3 dir = normalize(push.lightDir.xyz);
    float diff = max(dot(vec3(0.0, 1.0, 0.0), dir), 0.0);
    diff *= push.lightDir.w;

    // 最终颜色 = 环境光 + 漫反射
    vec3 finalColor = variedColor * (ambientColor + diff);

    outColor = vec4(finalColor, 1.0);
}
