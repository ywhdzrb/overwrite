#version 450

// 来自顶点着色器的输入
layout(location = 0) in vec3 fragPos;       // 世界空间位置
layout(location = 1) in vec3 fragNormal;    // 法线
layout(location = 2) in vec3 fragColor;     // 颜色

// 输出颜色
layout(location = 0) out vec4 outColor;

// 光照参数
struct Light {
    vec3 position;  // 光源位置（世界空间）
    vec3 color;     // 光源颜色
    float intensity; // 光源强度
};

struct Material {
    vec3 ambient;   // 环境光反射系数
    vec3 diffuse;   // 漫反射系数
    vec3 specular;  // 镜面反射系数
    float shininess; // 光泽度
};

void main() {
    // 光源设置
    Light light;
    light.position = vec3(5.0, 10.0, 5.0);  // 光源在右上方
    light.color = vec3(1.0, 1.0, 1.0);     // 白光
    light.intensity = 1.0;                   // 光源强度
    
    // 材质设置
    Material material;
    material.ambient = fragColor * 0.1;      // 环境光为物体颜色的 10%
    material.diffuse = fragColor;            // 漫反射使用物体颜色
    material.specular = vec3(0.5);           // 镜面反射（白色）
    material.shininess = 32.0;               // 光泽度
    
    // 归一化法线
    vec3 normal = normalize(fragNormal);
    
    // 计算光照方向（从表面到光源）
    vec3 lightDir = normalize(light.position - fragPos);
    
    // 1. 环境光
    vec3 ambient = light.color * material.ambient * light.intensity;
    
    // 2. 漫反射（Lambert）
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.color * (diff * material.diffuse) * light.intensity;
    
    // 3. 镜面反射（Phong）
    // 观察方向（假设相机在世界原点）
    vec3 viewDir = normalize(-fragPos);
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specular = light.color * (spec * material.specular) * light.intensity;
    
    // 合成最终颜色
    vec3 result = ambient + diffuse + specular;
    
    // 输出
    outColor = vec4(result, 1.0);
}