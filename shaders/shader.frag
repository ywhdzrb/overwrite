#version 450

// 简化光照着色器 - 只有环境光

// 来自顶点着色器的输入
layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;

// 输出颜色
layout(location = 0) out vec4 outColor;

// 纹理采样器
layout(set = 0, binding = 0) uniform sampler2D texSampler;

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
    
    // 光源数据
    vec3 lightPos;
    float lightIntensity;
    vec3 lightColor;
    float _pad;
    vec3 ambientColor;
    float _pad2;
} pushConstants;

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
    
    // 硬编码光源参数（临时方案，避免pushConstants传递问题）
    vec3 lightPos = vec3(0.0, 0.0, 3.0);
    vec3 lightColor = vec3(1.0, 1.0, 1.0);
    float lightIntensity = 1.0;
    vec3 ambientColor = vec3(0.5, 0.5, 0.5);
    
    // 环境光
    vec3 result = ambientColor * albedo;
    
    // 漫反射
    vec3 lightDir = normalize(lightPos - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = lightColor * diff * albedo;
    
    // 镜面反射（Blinn-Phong）
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
    vec3 specular = lightColor * spec * 0.5;
    
    // 组合光照
    result += (diffuse + specular) * lightIntensity;
    
    outColor = vec4(result, 1.0);
}