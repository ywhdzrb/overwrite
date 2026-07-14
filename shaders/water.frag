#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 color;            // 水面基础色 rgb + 透明度 a
    vec4 waveParams;       // waveAmp, waveFreq, waveSpeed, time
    vec4 sunDir_intensity; // sunDir.xyz + intensity
} pc;

void main() {
    vec3 waterColor = pc.color.rgb;
    float alpha = pc.color.a;

    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(-fragPos);

    // 太阳方向与强度
    vec3 sunDir = normalize(pc.sunDir_intensity.xyz);
    float sunIntensity = pc.sunDir_intensity.w;

    // 菲涅尔效应：视线与法线夹角越浅，反射越强
    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(normal, viewDir), 0.0), 4.0);

    // 漫反射（来自太阳）
    float diffuse = max(dot(normal, sunDir), 0.0) * 0.6 + 0.4;

    // 镜面高光（太阳反射闪烁）
    vec3 halfway = normalize(sunDir + viewDir);
    float spec = pow(max(dot(normal, halfway), 0.0), 128.0);
    vec3 specColor = vec3(1.0, 0.95, 0.8) * spec * sunIntensity * 0.8;

    // 环境光反射（模拟天空反射颜色，与海拔有关）
    vec3 skyReflect = mix(
        vec3(0.02, 0.05, 0.15), // 深色（向下看水面）
        vec3(0.12, 0.25, 0.50), // 亮色（向上看水面，反射天空）
        max(normal.y, 0.0)
    );

    // 合成最终颜色
    vec3 finalColor = waterColor * diffuse * 0.6
                    + skyReflect * 0.4
                    + specColor;

    // 深度衰减：深水处颜色变暗
    float depthFade = 1.0;

    // 雾化（远处变淡）
    float dist = length(fragPos);
    float fog = clamp((dist - 50.0) / 300.0, 0.0, 0.6);

    finalColor = mix(finalColor, vec3(0.4, 0.5, 0.6), fog);

    // 动态透明度：垂直看时透明（看到海底），倾斜时反射增强
    float viewAngle = max(dot(normal, viewDir), 0.0);
    float dynamicAlpha = mix(0.65, 0.25, viewAngle);
    // 远处雾化降低透明度
    float finalAlpha = alpha * dynamicAlpha * (1.0 - fog * 0.5);

    outColor = vec4(finalColor, finalAlpha);
}
