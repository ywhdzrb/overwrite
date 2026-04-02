#version 450

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

// 光源Uniform Buffer (set = 1, binding = 0)
layout(set = 1, binding = 0, std140) uniform LightUniformBuffer {
    Light lights[16];       // 最多16个光源
    int lightCount;         // 启用的光源数量
    int _pad1;              // 填充
    int _pad2;              // 填充
    int _pad3;              // 填充
    vec3 ambientColor;      // 环境光颜色
    float _pad4;            // 填充
} lightBuffer;

// Push Constants
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 baseColor;
    float metallic;
    float roughness;
    int hasTexture;
    float _pad0;
} pushConstants;

// 计算方向光
vec3 calculateDirectionalLight(Light light, vec3 normal, vec3 viewDir, vec3 albedo) {
    vec3 lightDir = normalize(-light.direction);

    // 漫反射
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.color * diff * albedo;

    // 镜面反射（Blinn-Phong）
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
    vec3 specular = light.color * spec * 0.5;

    return (diffuse + specular) * light.intensity;
}

// 计算点光源
vec3 calculatePointLight(Light light, vec3 normal, vec3 viewDir, vec3 fragPosition, vec3 albedo) {
    vec3 lightDir = normalize(light.position - fragPosition);

    // 漫反射
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.color * diff * albedo;

    // 镜面反射（Blinn-Phong）
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
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

        // 镜面反射（Blinn-Phong）
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
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

    // 采样纹理或使用顶点颜色
    if (pushConstants.hasTexture == 1) {
        vec4 texColor = texture(texSampler, fragTexCoord);
        albedo = texColor.rgb;
    } else {
        albedo = fragColor;
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
            result += calculateDirectionalLight(light, normal, viewDir, albedo);
        } else if (light.type == LIGHT_TYPE_POINT) {
            result += calculatePointLight(light, normal, viewDir, fragPos, albedo);
        } else if (light.type == LIGHT_TYPE_SPOT) {
            result += calculateSpotLight(light, normal, viewDir, fragPos, albedo);
        }
    }

    outColor = vec4(result, 1.0);
}
