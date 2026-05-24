#version 450

// 使用顶点着色器传递的位置作为方向向量
layout(location = 0) in vec3 fragTexCoord;

// 输出颜色
layout(location = 0) out vec4 outColor;

// 通过 push constants 接收太阳方向
// 与 vertex shader 共享同一个 push constant 块（view/proj 在 vertex 中使用，sunDir 在 fragment 中使用）
layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
    vec4 sunDir;
} push;

void main() {
    // 归一化方向向量
    vec3 dir = normalize(fragTexCoord);

    // ===== 1. 动态天空颜色（基于太阳仰角） =====
    float elevation = push.sunDir.y;

    // 白天因子：0=夜晚，1=正午
    float dayMix = smoothstep(-0.15, 0.35, elevation);
    // 黄昏因子：太阳在地平线附近时最大，用于暖色调叠加
    float duskMix = exp(-elevation * elevation * 30.0) * 0.7;

    // 各时段天空颜色
    vec3 nightTop    = vec3(0.02, 0.02, 0.06);  // 深夜深蓝黑
    vec3 nightMid    = vec3(0.03, 0.03, 0.08);  // 深夜暗灰蓝
    vec3 nightBottom = vec3(0.05, 0.05, 0.10);  // 深夜暗灰

    vec3 dayTop    = vec3(0.12, 0.20, 0.50);    // 白天深蓝
    vec3 dayMid    = vec3(0.55, 0.65, 0.80);    // 白天浅蓝灰
    vec3 dayBottom = vec3(0.70, 0.70, 0.75);    // 白天暖灰地面

    vec3 duskTop    = vec3(0.50, 0.25, 0.12);   // 黄昏暖橙
    vec3 duskMid    = vec3(0.95, 0.60, 0.35);   // 黄昏橙粉
    vec3 duskBottom = vec3(0.75, 0.50, 0.40);   // 黄昏暖褐

    // 基础混合：夜晚 ↔ 白天
    vec3 skyTop    = mix(nightTop,    dayTop,    dayMix);
    vec3 skyMid    = mix(nightMid,    dayMid,    dayMix);
    vec3 skyBottom = mix(nightBottom, dayBottom, dayMix);

    // 黄昏暖色叠加（仅在 dayMix 较低时明显）
    skyTop    = mix(skyTop,    duskTop,    duskMix * (1.0 - dayMix));
    skyMid    = mix(skyMid,    duskMid,    duskMix * (1.0 - dayMix));
    skyBottom = mix(skyBottom, duskBottom, duskMix * (1.0 - dayMix));

    // 组装三段式渐变色
    vec3 color;
    if (dir.y > 0.0) {
        // 上半球：顶部→地平线
        float u = pow(dir.y, 0.55);
        color = mix(skyMid, skyTop, u);
    } else {
        // 下半球：地平线→底部
        float u = pow(-dir.y, 0.8);
        color = mix(skyMid, skyBottom, u);
    }

    // 地平线附近薄雾
    float fogFactor = 1.0 - abs(dir.y);
    color = mix(color, skyMid, fogFactor * 0.15);

    // ===== 2. 太阳 =====
    // 太阳方向由 push constants 传入（从 GameConfig JSON 加载）
    vec3 sunDir = normalize(push.sunDir.xyz);

    // 视线与太阳方向的夹角余弦
    float cosAngle = dot(dir, sunDir);

    // --- 太阳日冕（外层暖色辉光）---
    // pow 提升到 25，角半径从 27° 收窄到约 12°
    float corona = max(cosAngle, 0.0);
    corona = pow(corona, 25.0);
    vec3 coronaColor = vec3(1.0, 0.70, 0.30) * corona * 0.8;

    // --- 太阳光晕（中层亮暖色）---
    // pow 提升到 120，角半径从 15° 收窄到约 6°
    float glow = max(cosAngle, 0.0);
    glow = pow(glow, 120.0);
    vec3 glowColor = vec3(1.0, 0.85, 0.50) * glow * 1.5;

    // --- 太阳盘面（高亮白/黄色实心小圆点）---
    float disc = max(cosAngle, 0.0);
    // smoothstep 阈值收紧：约 0.14° 视角
    disc = smoothstep(0.999997, 1.0, disc);
    vec3 discColor = vec3(1.0, 0.95, 0.80) * disc * 8.0;

    // 累加所有太阳层次
    vec3 sunTotal = coronaColor + glowColor + discColor;

    // 太阳在低于地平线时淡出（y≈0 附近截断）
    float sunVisibility = smoothstep(-0.05, 0.05, sunDir.y);
    sunTotal *= sunVisibility;

    // 将太阳合成到天空颜色（使用加法混合，保留天空底色）
    color += sunTotal;

    // 输出最终颜色
    outColor = vec4(color, 1.0);
}
