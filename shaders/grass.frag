#version 450
// 草丛片段着色器（纯程序化颜色版，光照与地形渲染完全一致）
//
// 功能：
//   1. 基于高度的渐变绿色，根部深绿→尖端浅绿/黄绿
//   2. 方向光漫反射 + 环境光，方向/强度从 push constants 获取（昼夜联动）
//   3. 世界坐标伪随机色调微调，增加视觉差异
//   4. 光照模型与 terrain shader.frag 在平地上保持完全一致：
//      - ambientColor = lightManager.getAmbient() = (0.5,0.5,0.5)*ambientIntensity
//      - diffuse = max(dot(up, lightDir), 0) * lightDir.w
//      - final = variedColor * (ambientColor + diffuse)
//
// push constants 必须与顶点着色器和 C++ PushBlock 完全对齐。
// lightDir.xyz = 光照方向（场景→光源，归一化），lightDir.w = 漫反射强度 [0,1]
// ambientIntensity.xyz = 环境光颜色（由 Renderer 传入，与 terrain 共享）

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in float fragHeight;

layout(location = 0) out vec4 outColor;

// 草地纹理采样器（与 terrain 共享同一张草地 BaseColor 贴图，使用世界空间 UV）
layout(binding = 0) uniform sampler2D texSampler;

// push constants 与顶点着色器共享同一块内存，必须结构一致
layout(push_constant) uniform PushConstants {
    mat4 view;              // 0-63
    mat4 proj;              // 64-127
    vec4 timeParams;        // 128-143
    vec4 playerPos;         // 144-159
    vec4 lightDir;          // 160-175  xyz=光照方向, w=漫反射强度
    vec4 ambientIntensity;  // 176-191  xyz=环境光颜色(与terrain一致), w未用
} push;

void main() {
    // 采样草地贴图（世界空间 UV 与 terrain 一致，确保无缝拼合）
    vec2 texUV = fragWorldPos.xz / 4.0;
    vec3 texColor = texture(texSampler, texUV).rgb;

    // 基于高度渐变：基部深绿，尖端黄绿 — 叠加在纹理之上做高度着色
    vec3 baseColor  = vec3(0.22, 0.55, 0.12);
    vec3 tipColor   = vec3(0.50, 0.80, 0.20);
    vec3 heightColor = mix(baseColor, tipColor, fragHeight);

    // 利用世界坐标生成微小的随机色调偏移 (±5%)
    vec3 floorPos = floor(fragWorldPos * 4.0);
    float h = fract(sin(dot(floorPos, vec3(12.9898, 78.233, 45.543))) * 43758.5453);
    float variation = h * 0.10 - 0.05;
    vec3 variedColor = heightColor + variation;

    // 混合纹理颜色 + 程序化颜色：纹理提供地面拼合感，程序化提供高度渐变
    // 根部更接近纹理（融入地面），尖端更多程序化绿色（草叶自身颜色）
    float texBlend = 1.0 - fragHeight * 0.6;
    texBlend = clamp(texBlend, 0.3, 0.8);
    vec3 blendedColor = mix(variedColor, texColor, texBlend);

    // === 光照计算（与 terrain shader.frag 公式一致） ===
    // 环境光：由 Renderer 传入 ambientColor * ambientIntensity（完整 RGB）
    vec3 ambientColor = push.ambientIntensity.xyz;

    // 漫反射：标准 Blinn-Phong 漫反射项（草无法线，使用向上方向近似平地）
    // lightDir.w = 漫反射强度（由 Renderer 传入，白天≈1 夜晚≈0）
    vec3 dir = normalize(push.lightDir.xyz);
    float diff = max(dot(vec3(0.0, 1.0, 0.0), dir), 0.0);
    diff *= push.lightDir.w;

    // 最终颜色 = 环境光 + 漫反射（与 terrain 完全一致的合并方式）
    vec3 finalColor = blendedColor * (ambientColor + diff);

    outColor = vec4(finalColor, 1.0);
}
