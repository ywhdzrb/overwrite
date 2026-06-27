#version 450

/**
 * @file shader.frag
 * @brief 主渲染管线片段着色器 — Blinn-Phong 光照 + 方向光阴影映射
 *
 * 阴影映射说明：
 * - 方向光（太阳）使用 ShadowMapper 生成的阴影贴图
 * - set=2 提供阴影贴图采样器（深度比较模式，硬件 PCF 滤波）和光源 VP 矩阵
 * - 光照方向只有方向光计算阴影，点光源和聚光灯保持原有计算方式
 * - 阴影强度由 ShadowUniform.shadowIntensity 控制（0=无阴影，1=全阴影）
 */

// 来自顶点着色器的输入
layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;

// 输出颜色
layout(location = 0) out vec4 outColor;

// 纹理采样器 (set = 0, binding = 0)
layout(set = 0, binding = 0) uniform sampler2D texSampler;

// 光源类型枚举
const int LIGHT_TYPE_DIRECTIONAL = 0;
const int LIGHT_TYPE_POINT = 1;
const int LIGHT_TYPE_SPOT = 2;

// 光源结构体（std140布局，必须与CPU端ShaderLight匹配）
struct Light {
    int type;               // 0=方向光, 1=点光源, 2=聚光灯
    int enabled;            // 是否启用
    int _pad1;              // 填充
    int _pad2;              // 填充

    vec3 position;          // 位置
    float _pad3;            // 填充

    vec3 direction;         // 方向
    float _pad4;            // 填充

    vec3 color;             // 颜色
    float intensity;        // 强度

    float constant;         // 衰减常数项
    float linear;           // 衰减线性项
    float quadratic;        // 衰减二次项
    float _pad5;            // 填充

    float innerCutoff;      // 聚光灯内切角（弧度）
    float outerCutoff;      // 聚光灯外切角（弧度）
    float shadowIntensity;  // 阴影强度
    float _pad6;            // 填充
};

// 光源 Storage Buffer (set = 1, binding = 0) — 支持动态光源数
layout(set = 1, binding = 0, std430) buffer LightBuffer {
    Light lights[64];       // 最大 64 个光源（SSBO 动态上采样）
    vec3 ambientColor;      // 环境光颜色，offset=6144
    int lightCount;         // 启用的光源数量，offset=6156
} lightBuffer;

// 阴影贴图 (set = 2, binding = 0) — R32_SFLOAT 常规采样，手动深度比较
layout(set = 2, binding = 0) uniform sampler2D shadowMap;

// 阴影参数 (set = 2, binding = 1)
layout(set = 2, binding = 1) uniform ShadowUniform {
    mat4 lightVP;           // 光源 VP 矩阵（世界→光源裁剪空间）
    float shadowIntensity;  // 阴影强度（0=无阴影，1=全阴影）
} shadowData;

// Push Constants（scalar布局使vec3对齐到4字节，与C++ glm::vec3兼容）
#extension GL_EXT_scalar_block_layout : enable
layout(push_constant, scalar) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 baseColor;
    float metallic;
    float roughness;
    int hasTexture;
    float _pad0;
    float windTime;      // 累计时间（秒），用于风场动画
    float windStrength;  // 风场强度（0=无风）
    vec3 normalScale;    // 逆缩放因子（CPU 计算，用于法线矩阵）
} pushConstants;

// 从粗糙度计算镜面高光指数：低粗糙度 = 高光泽度 = 更亮更锐利的高光
float calcShininess() {
    return max(2.0, (1.0 - pushConstants.roughness) * 128.0);
}

/**
 * @brief 计算阴影因子（手动深度比较，单纹素采样）
 * @param fragPosWorld 片段世界空间位置
 * @return 阴影因子（1.0=完全照亮，0.0=完全阴影）
 *
 * 将片段的 world 坐标通过 lightVP 变换到光源裁剪空间。
 * xy 从 NDC [-1,1] 映射到 UV [0,1] 用于采样阴影贴图（R32_SFLOAT）。
 * z 直接作为深度比较值（Vulkan 中 depth buffer 存储的是 clip_z，
 * 不需要 OpenGL 的 [-1,1]→[0,1] 映射）。
 */
// Interleaved Gradient Noise：打破深度比较量化色阶的亚像素抖动
// 来源：GPU Gems 3 第 24 章，单周期高效实现
float interleavedGradientNoise(vec2 pos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(pos, magic.xy)));
}

float calcShadow(vec3 fragPosWorld) {
    vec4 fls = shadowData.lightVP * vec4(fragPosWorld, 1.0);
    vec3 pc = fls.xyz / fls.w;
    // xy 从 NDC [-1,1] 映射到纹理 UV [0,1]
    // 注意：Vulkan 中 viewport 的 Y 轴自动映射使 NDC y=-1→图像顶部→UV.y=0，
    // 与纹理坐标 V=0（图像顶部）一致，因此不需要额外翻转 Y。
    pc.xy = pc.xy * 0.5 + 0.5;

    // 边界平滑过渡：UV 接近 0/1 时用 smoothstep 淡出阴影，避免物体进出阴影贴图边界时瞬间消失
    float edgeFade = 1.0;
    edgeFade *= smoothstep(0.0, 0.05, pc.x) * smoothstep(1.0, 0.95, pc.x);
    edgeFade *= smoothstep(0.0, 0.05, pc.y) * smoothstep(1.0, 0.95, pc.y);
    edgeFade *= smoothstep(0.0, 0.05, pc.z) * smoothstep(1.0, 0.95, pc.z);

    // 深度偏移：缓解表面自阴影（阴影粉刺）
    // 使用 Interleaved Gradient Noise 对 bias 添加亚像素级随机抖动，
    // 将深度比较的量化色阶转化为不易感知的高频噪声。
    float dither = (interleavedGradientNoise(gl_FragCoord.xy) - 0.5) * 0.0015;
    float bias = 0.004 + dither;

    // 3x3 PCF：边缘锐利，同时保留足够采样消除单点锯齿
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
    // 将边界淡出和 PCF 结果混合，离开贴图范围的区域逐渐变为完全照亮
    return mix(1.0, shadow, edgeFade);
}

// 计算方向光（含阴影）
vec3 calculateDirectionalLight(Light light, vec3 normal, vec3 viewDir, vec3 albedo, vec3 worldPos) {
    vec3 lightDir = normalize(-light.direction);

    // 漫反射
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.color * diff * albedo;

    // 镜面反射（Blinn-Phong，粗糙度控制高光锐度）
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float shininess = calcShininess();
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    vec3 specular = light.color * spec * 0.5;

    // 阴影计算（仅方向光）
    float shadowFactor = 1.0;
    if (shadowData.shadowIntensity > 0.001) {
        shadowFactor = calcShadow(worldPos);
        // 阴影强度混合：shadowIntensity=0 时无阴影，=1 时完全阴影
        shadowFactor = mix(1.0, shadowFactor, shadowData.shadowIntensity);
    }

    return (diffuse + specular) * light.intensity * shadowFactor;
}

// 计算点光源
vec3 calculatePointLight(Light light, vec3 normal, vec3 viewDir, vec3 fragPosition, vec3 albedo) {
    vec3 lightDir = normalize(light.position - fragPosition);

    // 漫反射
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.color * diff * albedo;

    // 镜面反射（Blinn-Phong，粗糙度控制高光锐度）
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float shininess = calcShininess();
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    vec3 specular = light.color * spec * 0.5;

    // 衰减
    float distance = length(light.position - fragPosition);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * distance * distance);

    return (diffuse + specular) * light.intensity * attenuation;
}

// 计算聚光灯
vec3 calculateSpotLight(Light light, vec3 normal, vec3 viewDir, vec3 fragPosition, vec3 albedo) {
    vec3 lightDir = normalize(light.position - fragPosition);

    // 检查是否在聚光灯锥形范围内
    float theta = dot(lightDir, normalize(-light.direction));

    if (theta > light.outerCutoff) {
        // 漫反射
        float diff = max(dot(normal, lightDir), 0.0);
        vec3 diffuse = light.color * diff * albedo;

        // 镜面反射（Blinn-Phong，粗糙度控制高光锐度）
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float shininess = calcShininess();
        float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
        vec3 specular = light.color * spec * 0.5;

        // 衰减
        float distance = length(light.position - fragPosition);
        float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * distance * distance);

        // 聚光灯边缘柔和过渡
        float epsilon = light.innerCutoff - light.outerCutoff;
        float intensity = clamp((theta - light.outerCutoff) / epsilon, 0.0, 1.0);

        return (diffuse + specular) * light.intensity * attenuation * intensity;
    }

    return vec3(0.0);
}

void main() {
    vec3 albedo;
    float alpha = 1.0;

    // 采样纹理或使用顶点颜色
    if (pushConstants.hasTexture == 1) {
        vec4 texColor = texture(texSampler, fragTexCoord);
        albedo = texColor.rgb;
        alpha = texColor.a;
    } else {
        albedo = fragColor;
    }

    // 丢弃完全透明的像素
    if (alpha < 0.1) {
        discard;
    }

    // 归一化法线
    vec3 normal = normalize(fragNormal);

    // 观察方向
    vec3 viewDir = normalize(-fragPos);

    // 环境光
    vec3 result = lightBuffer.ambientColor * albedo;

    // 遍历所有光源
    for (int i = 0; i < lightBuffer.lightCount; i++) {
        Light light = lightBuffer.lights[i];

        if (light.enabled == 0) continue;

        if (light.type == LIGHT_TYPE_DIRECTIONAL) {
            // 方向光传入世界位置用于阴影计算
            result += calculateDirectionalLight(light, normal, viewDir, albedo, fragPos);
        } else if (light.type == LIGHT_TYPE_POINT) {
            result += calculatePointLight(light, normal, viewDir, fragPos, albedo);
        } else if (light.type == LIGHT_TYPE_SPOT) {
            result += calculateSpotLight(light, normal, viewDir, fragPos, albedo);
        }
    }

    outColor = vec4(result, alpha);
}
