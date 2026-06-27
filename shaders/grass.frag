#version 450
// 草丛片段着色器（使用 LightBuffer SSBO + ShadowMap，与主渲染管线共享光照）
//
// 功能：
//   1. 基于高度的渐变绿色，根部深绿→尖端浅绿/黄绿
//   2. 从 LightBuffer SSBO (set=1) 读取光源数据，与主场景完全一致
//   3. 从 ShadowMap (set=2) 读取阴影贴图，支持方向光阴影
//   4. 世界坐标伪随机色调微调，增加视觉差异
//   5. 根部渐变为棕色以融入地面
//
// 光照来源从 push constants 迁移到 descriptor set set=1/set=2，
// 确保草与地形/模型接受完全一致的光照和阴影。
// push constants 中的 lightDir/ambientIntensity 保留但不再用于光照计算。

// ---- 光源结构体（必须与 CPU 端 ShaderLight 和 shader.frag 完全匹配）----
const int LIGHT_TYPE_DIRECTIONAL = 0;
const int LIGHT_TYPE_POINT = 1;
const int LIGHT_TYPE_SPOT = 2;

struct Light {
    int type;
    int enabled;
    int _pad1;
    int _pad2;

    vec3 position;
    float _pad3;

    vec3 direction;
    float _pad4;

    vec3 color;
    float intensity;

    float constant;
    float linear;
    float quadratic;
    float _pad5;

    float innerCutoff;
    float outerCutoff;
    float shadowIntensity;
    float _pad6;
};

// ---- Descriptor Sets（与主渲染管线共享）----

// Set=1: LightBuffer SSBO — 最多 64 个光源
layout(set = 1, binding = 0, std430) buffer LightBuffer {
    Light lights[64];
    vec3 ambientColor;
    int lightCount;
} lightBuffer;

// Set=2: ShadowMap — 方向光阴影贴图
layout(set = 2, binding = 0) uniform sampler2D shadowMap;
layout(set = 2, binding = 1) uniform ShadowUniform {
    mat4 lightVP;
    float shadowIntensity;
} shadowData;

// ---- 来自顶点着色器的输入 ----
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in float fragHeight;

layout(location = 0) out vec4 outColor;

// push constants 与顶点着色器共享同一块内存，必须结构一致
// lightDir/ambientIntensity 保留以维持内存布局兼容，但不再用于光照
layout(push_constant) uniform PushConstants {
    mat4 view;              // 0-63
    mat4 proj;              // 64-127
    vec4 timeParams;        // 128-143
    vec4 playerPos;         // 144-159
    vec4 lightDir;          // 160-175  已弃用（保留兼容）
    vec4 ambientIntensity;  // 176-191  已弃用（保留兼容）
} push;

// Interleaved Gradient Noise：打破深度比较量化色阶的亚像素抖动
float interleavedGradientNoise(vec2 pos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(pos, magic.xy)));
}

// 计算方向光阴影因子（与 shader.frag 的 calcShadow 一致）
float calcShadow(vec3 fragPosWorld) {
    vec4 fls = shadowData.lightVP * vec4(fragPosWorld, 1.0);
    vec3 pc = fls.xyz / fls.w;
    pc.xy = pc.xy * 0.5 + 0.5;

    // 边界平滑过渡
    float edgeFade = 1.0;
    edgeFade *= smoothstep(0.0, 0.05, pc.x) * smoothstep(1.0, 0.95, pc.x);
    edgeFade *= smoothstep(0.0, 0.05, pc.y) * smoothstep(1.0, 0.95, pc.y);
    edgeFade *= smoothstep(0.0, 0.05, pc.z) * smoothstep(1.0, 0.95, pc.z);

    float dither = (interleavedGradientNoise(gl_FragCoord.xy) - 0.5) * 0.0015;
    float bias = 0.004 + dither;

    // 3x3 PCF
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    float shadow = 0.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            float storedDepth = texture(shadowMap, pc.xy + offset).r;
            shadow += (pc.z - bias) > storedDepth ? 0.0 : 1.0;
        }
    }
    shadow /= 9.0;
    return mix(1.0, shadow, edgeFade);
}

// 计算方向光（草无法线，使用向上方向近似）
vec3 calcDirectionalLight(Light light, vec3 albedo, vec3 worldPos) {
    vec3 lightDir = normalize(-light.direction);

    // 漫反射（硬编码法线向上，模拟草地受光特性）
    float diff = max(dot(vec3(0.0, 1.0, 0.0), lightDir), 0.0);
    vec3 diffuse = light.color * diff * albedo;

    // 阴影计算
    float shadowFactor = 1.0;
    if (shadowData.shadowIntensity > 0.001) {
        shadowFactor = calcShadow(worldPos);
        shadowFactor = mix(1.0, shadowFactor, shadowData.shadowIntensity);
    }

    return diffuse * light.intensity * shadowFactor;
}

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

    // === 从 LightBuffer SSBO 获取光照数据 ===
    // 环境光：从 SSBO 读取，与主场景完全一致
    vec3 result = variedColor * lightBuffer.ambientColor;

    // 遍历所有光源（仅处理方向光，点/聚光对草影响可忽略）
    for (int i = 0; i < lightBuffer.lightCount; i++) {
        Light light = lightBuffer.lights[i];
        if (light.enabled == 0) continue;
        if (light.type == LIGHT_TYPE_DIRECTIONAL) {
            result += calcDirectionalLight(light, variedColor, fragWorldPos);
        }
    }

    outColor = vec4(result, 1.0);
}
