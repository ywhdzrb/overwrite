#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 color;
    vec4 waveParams;
    vec4 sunDir_intensity;
    vec4 interaction;
} pc;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragFoam;

void main() {
    vec3 pos = inPosition;

    float waveAmp  = pc.waveParams.x;
    float waveFreq = pc.waveParams.y;
    float waveSpeed = pc.waveParams.z;
    float time     = pc.waveParams.w;

    const float steepness = 0.35;

    vec2 d1 = normalize(vec2(1.0, 0.7));
    float f1 = waveFreq;
    float a1 = waveAmp;
    float s1 = d1.x * pos.x * f1 + d1.y * pos.z * f1 + time * waveSpeed;
    float sin1 = sin(s1);
    float cos1 = cos(s1);
    float q1 = steepness / max(f1 * a1 * 4.0, 0.001);

    vec2 d2 = normalize(vec2(1.8, -1.2));
    float f2 = waveFreq * 1.8;
    float a2 = waveAmp * 0.6;
    float s2 = d2.x * pos.x * f2 + d2.y * pos.z * f2 + time * waveSpeed * 1.4;
    float sin2 = sin(s2);
    float cos2 = cos(s2);
    float q2 = steepness / max(f2 * a2 * 4.0, 0.001);

    vec2 d3 = normalize(vec2(0.4, 2.1));
    float f3 = waveFreq * 0.4;
    float a3 = waveAmp * 0.4;
    float s3 = d3.x * pos.x * f3 + d3.y * pos.z * f3 + time * waveSpeed * 0.8;
    float sin3 = sin(s3);
    float cos3 = cos(s3);
    float q3 = steepness / max(f3 * a3 * 4.0, 0.001);

    vec2 d4 = normalize(vec2(0.9, 0.9));
    float f4 = waveFreq * 0.9;
    float a4 = waveAmp * 0.3;
    float s4 = d4.x * pos.x * f4 + d4.y * pos.z * f4 + time * waveSpeed * 1.1;
    float sin4 = sin(s4);
    float cos4 = cos(s4);
    float q4 = steepness / max(f4 * a4 * 4.0, 0.001);

    // Gerstner 水平位移
    pos.x += q1 * a1 * d1.x * cos1 + q2 * a2 * d2.x * cos2
           + q3 * a3 * d3.x * cos3 + q4 * a4 * d4.x * cos4;
    pos.z += q1 * a1 * d1.y * cos1 + q2 * a2 * d2.y * cos2
           + q3 * a3 * d3.y * cos3 + q4 * a4 * d4.y * cos4;

    // Gerstner 垂直位移
    pos.y += a1 * sin1 + a2 * sin2 + a3 * sin3 + a4 * sin4;

    vec4 worldPos = pc.model * vec4(pos, 1.0);

    // 法线扰动
    float dx = f1 * d1.x * a1 * cos1 + f2 * d2.x * a2 * cos2
             + f3 * d3.x * a3 * cos3 + f4 * d4.x * a4 * cos4;
    float dz = f1 * d1.y * a1 * cos1 + f2 * d2.y * a2 * cos2
             + f3 * d3.y * a3 * cos3 + f4 * d4.y * a4 * cos4;

    vec3 normal = normalize(vec3(-dx, 1.0, -dz));

    // 泡沫因子
    float totalHeight = a1 * sin1 + a2 * sin2 + a3 * sin3 + a4 * sin4;
    float maxHeight = a1 + a2 + a3 + a4;
    float heightRatio = totalHeight / max(maxHeight, 0.001);
    float foam = smoothstep(0.35, 0.9, heightRatio);
    float noise = sin(pos.x * 7.3 + pos.z * 11.7 + time * 2.0) * 0.5 + 0.5;
    foam *= smoothstep(0.2, 0.8, noise);

    fragPos = worldPos.xyz;
    fragNormal = normal;
    fragTexCoord = inPosition.xz * 0.02;
    fragFoam = clamp(foam, 0.0, 1.0);

    gl_Position = pc.proj * pc.view * worldPos;
    gl_Position.y = -gl_Position.y;
}
