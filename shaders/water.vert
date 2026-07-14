#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 color;            // 水面基础色 rgb + 透明度 a
    vec4 waveParams;       // waveAmp, waveFreq, waveSpeed, time
    vec4 sunDir_intensity; // sunDir.xyz + intensity
} pc;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

void main() {
    vec3 pos = inPosition;

    float waveAmp  = pc.waveParams.x;
    float waveFreq = pc.waveParams.y;
    float waveSpeed = pc.waveParams.z;
    float time     = pc.waveParams.w;

    // 多层正弦波叠加产生自然水面起伏
    float wave1 = sin(pos.x * waveFreq + pos.z * waveFreq * 0.7 + time * waveSpeed) * waveAmp;
    float wave2 = sin(pos.x * waveFreq * 1.8 - pos.z * waveFreq * 1.2 + time * waveSpeed * 1.4) * waveAmp * 0.6;
    float wave3 = sin(pos.x * waveFreq * 0.4 + pos.z * waveFreq * 2.1 + time * waveSpeed * 0.8) * waveAmp * 0.4;
    float wave4 = sin((pos.x + pos.z) * waveFreq * 0.9 + time * waveSpeed * 1.1) * waveAmp * 0.3;
    pos.y += wave1 + wave2 + wave3 + wave4;

    // 计算扰动后的法线（通过偏导数近似）
    float dx = waveFreq * waveAmp * cos(pos.x * waveFreq + inPosition.z * waveFreq * 0.7 + time * waveSpeed);
    float dz = waveFreq * 0.7 * waveAmp * cos(pos.x * waveFreq + inPosition.z * waveFreq * 0.7 + time * waveSpeed);
    dx += waveFreq * 1.8 * waveAmp * 0.6 * cos(pos.x * waveFreq * 1.8 - inPosition.z * waveFreq * 1.2 + time * waveSpeed * 1.4);
    dz -= waveFreq * 1.2 * waveAmp * 0.6 * cos(pos.x * waveFreq * 1.8 - inPosition.z * waveFreq * 1.2 + time * waveSpeed * 1.4);
    dx += waveFreq * 0.4 * waveAmp * 0.4 * cos(inPosition.x * waveFreq * 0.4 + inPosition.z * waveFreq * 2.1 + time * waveSpeed * 0.8);
    dz += waveFreq * 2.1 * waveAmp * 0.4 * cos(inPosition.x * waveFreq * 0.4 + inPosition.z * waveFreq * 2.1 + time * waveSpeed * 0.8);

    vec3 normal = normalize(vec3(-dx, 1.0, -dz));

    vec4 worldPos = pc.model * vec4(pos, 1.0);
    fragPos = worldPos.xyz;
    fragNormal = normal;
    fragTexCoord = inPosition.xz * 0.02; // 世界空间 UV 平铺

    gl_Position = pc.proj * pc.view * worldPos;
    gl_Position.y = -gl_Position.y;
}
