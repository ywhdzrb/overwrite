#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragFoam;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 color;            // 水面基础色 rgb + 透明度 a
    vec4 waveParams;       // waveAmp, waveFreq, waveSpeed, time
    vec4 sunDir_intensity; // sunDir.xyz + intensity
    vec4 interaction;      // xz=交互点位置, y=交互半径, z=交互强度, w=预留
} pc;

void main() {
    vec3 waterColor = pc.color.rgb;
    float alpha = pc.color.a;
    float time = pc.waveParams.w;

    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(-fragPos);

    // 小波纹微扰动法线
    float micro1 = sin(fragTexCoord.x * 18.0 + fragTexCoord.y * 14.0 + time * 2.5) * 0.04;
    float micro2 = cos(fragTexCoord.x * 24.0 - fragTexCoord.y * 20.0 + time * 2.0) * 0.03;
    float micro = micro1 + micro2;
    vec3 detailNormal = normalize(normal + vec3(micro, 0.0, micro * 0.8));

    vec3 sunDir = normalize(pc.sunDir_intensity.xyz);
    float sunIntensity = pc.sunDir_intensity.w;

    // 昼夜亮度因子
    float ambientFactor = sunIntensity * 0.96 + 0.04;
    ambientFactor = clamp(ambientFactor, 0.0, 1.0);

    // 折射
    float eta = 1.0 / 1.33;
    vec3 refrDir = refract(-viewDir, detailNormal, eta);
    float viewAngle = max(dot(detailNormal, viewDir), 0.0);
    float refractStrength = pow(1.0 - viewAngle, 2.0) * 3.0;
    vec3 refrPos = fragPos + refrDir * max(refractStrength, 0.0);

    // === 表面液体模拟：基于色散关系的物理涟漪 ===
    // 参考文献：Airy wave theory (1841), Stokes (1847)
    // 深水重力波色散关系: ω² = gk, 群速度 cg = ½cp
    // 圆形波远场解: h(r,t) = A·cos(kr-ωt)·exp(-αr)/√r
    vec2 interactPos = pc.interaction.xy;
    float interactionStrength = pc.interaction.w;
    float rippleFoam = 0.0;

    if (interactionStrength > 0.01) {
        vec2 delta = fragPos.xz - interactPos;
        float r = length(delta);
        float rSafe = max(r, 0.5);
        vec2 dir = delta / rSafe;

        // 物理参数
        const float g = 9.81;      // 重力加速度 m/s²
        const float nu = 0.04;     // 粘性阻尼系数

        // 4 层波分量叠加，利用色散产生真实涟漪扩散
        // 波数范围 0.5~1.7 rad/m，对应波长 3.7~12.6m
        float h = 0.0;    // 水面高度位移
        float dh = 0.0;   // 径向梯度（用于法线计算）

        // 展开循环避免 GLSL 循环性能问题
        // 第1分量：k=0.5, 波长≈12.6m, 传播快
        float k1 = 0.5; float w1 = sqrt(g * k1);
        float a1 = 0.15 / sqrt(rSafe) * exp(-nu * r);
        float p1 = k1 * r - w1 * time;
        h  += a1 * sin(p1);
        dh += a1 * k1 * cos(p1);

        // 第2分量：k=0.9, 波长≈7.0m
        float k2 = 0.9; float w2 = sqrt(g * k2);
        float a2 = 0.12 / sqrt(rSafe) * exp(-nu * r);
        float p2 = k2 * r - w2 * time;
        h  += a2 * sin(p2);
        dh += a2 * k2 * cos(p2);

        // 第3分量：k=1.3, 波长≈4.8m
        float k3 = 1.3; float w3 = sqrt(g * k3);
        float a3 = 0.10 / sqrt(rSafe) * exp(-nu * r);
        float p3 = k3 * r - w3 * time;
        h  += a3 * sin(p3);
        dh += a3 * k3 * cos(p3);

        // 第4分量：k=1.7, 波长≈3.7m, 传播慢
        float k4 = 1.7; float w4 = sqrt(g * k4);
        float a4 = 0.08 / sqrt(rSafe) * exp(-nu * r);
        float p4 = k4 * r - w4 * time;
        h  += a4 * sin(p4);
        dh += a4 * k4 * cos(p4);

        // 法线扰动：径向梯度，方向向外
        vec3 rippleNormal = vec3(-dir.x * dh, 0.0, -dir.y * dh);
        detailNormal = normalize(detailNormal + rippleNormal * interactionStrength);

        // 涟漪泡沫：波峰处（h > 阈值）产生泡沫
        rippleFoam = smoothstep(0.06, 0.18, max(h, 0.0)) * interactionStrength;
    }

    // === 水深模拟 ===
    float d1 = sin(fragPos.x * 0.05 + fragPos.z * 0.07) * 0.5 + 0.5;
    float d2 = sin(fragPos.x * 0.12 - fragPos.z * 0.09 + 2.1) * 0.5 + 0.5;
    float d3 = cos(fragPos.x * 0.03 + fragPos.z * 0.04 + 1.7) * 0.5 + 0.5;
    float d4 = sin((fragPos.x + fragPos.z) * 0.08 + 3.2) * 0.5 + 0.5;
    float waterDepth = d1 * 0.35 + d2 * 0.30 + d3 * 0.20 + d4 * 0.15;

    vec3 shallowCol = vec3(0.12, 0.40, 0.22);
    vec3 deepCol    = vec3(0.01, 0.03, 0.12);
    vec3 depthColor = mix(shallowCol, deepCol, waterDepth);
    waterColor = depthColor;

    // 漫反射
    float diffuse = max(dot(detailNormal, sunDir), 0.0) * 0.6 + 0.4 * ambientFactor;

    // 镜面高光
    vec3 halfway = normalize(sunDir + viewDir);
    float spec = pow(max(dot(detailNormal, halfway), 0.0), 128.0);
    vec3 specColor = vec3(1.0, 0.95, 0.8) * spec * sunIntensity * 0.8;

    // 环境反射
    float upness = max(detailNormal.y, 0.0);
    vec3 skyReflect = mix(
        vec3(0.02, 0.05, 0.15),
        vec3(0.50, 0.60, 0.75),
        upness
    );

    // 焦散
    vec2 causticUV = refrPos.xz * 0.06 + time * vec2(0.05, 0.04);
    float caustic = sin(causticUV.x * 2.3 + causticUV.y * 1.7)
                  * cos(causticUV.x * 1.9 - causticUV.y * 2.1)
                  * sin((causticUV.x + causticUV.y) * 1.3 + time * 0.5);
    caustic = pow(max(caustic, 0.0), 2.0) * 0.12;

    // === 浪花效果：波峰泡沫 + 涟漪泡沫 ===
    float waveCrest = 1.0 - dot(normalize(fragNormal), vec3(0.0, 1.0, 0.0));
    float crestMask = smoothstep(0.12, 0.40, waveCrest);

    vec2 foamUV = fragPos.xz * 0.25 + time * vec2(0.04, 0.03);
    float f1 = sin(foamUV.x * 4.3 + foamUV.y * 3.7) * cos(foamUV.y * 5.1 - foamUV.x * 2.9);
    float f2 = sin(foamUV.x * 7.1 - foamUV.y * 6.3 + 1.8) * cos(foamUV.x * 3.7 + foamUV.y * 8.2);
    float f3 = sin((foamUV.x + foamUV.y) * 5.5 + time * 0.6);
    float foamTexture = f1 * 0.4 + f2 * 0.35 + f3 * 0.25;
    foamTexture = foamTexture * 0.5 + 0.5;
    foamTexture = smoothstep(0.35, 0.75, foamTexture);

    float foamFade = sin(fragPos.x * 0.3 + fragPos.z * 0.2 + time * 1.5) * 0.5 + 0.5;
    foamFade = smoothstep(0.2, 0.8, foamFade);

    float foamAmount = crestMask * foamTexture * foamFade * 0.8 + fragFoam * 0.2;
    foamAmount = clamp(foamAmount, 0.0, 1.0);

    // 涟漪泡沫叠加
    foamAmount = max(foamAmount, rippleFoam);

    vec3 foamColor = vec3(0.92, 0.95, 1.0);

    // 合成
    vec3 finalColor = waterColor * diffuse * 0.6
                    + skyReflect * 0.4
                    + specColor
                    + vec3(0.4, 0.6, 0.5) * caustic;
    finalColor = mix(finalColor, foamColor, foamAmount);

    // 雾化
    float distFog = length(refrPos);
    float fog = clamp((distFog - 50.0) / 300.0, 0.0, 0.6);
    finalColor = mix(finalColor, vec3(0.4, 0.5, 0.6), fog);

    // 水深最终强调
    float depthBright = mix(1.3, 0.5, waterDepth);
    finalColor *= depthBright;

    // 昼夜统一调制
    finalColor *= ambientFactor;

    // 透明度
    float depthAlpha = waterDepth * 0.60;
    float viewAlpha = mix(0.55, 0.10, viewAngle);
    float finalAlpha = alpha * max(viewAlpha, depthAlpha);
    finalAlpha = max(finalAlpha, foamAmount);
    finalAlpha *= (1.0 - fog * 0.5);

    outColor = vec4(finalColor, finalAlpha);
}
