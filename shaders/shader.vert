#version 450

// 顶点位置属性（location = 0）
layout(location = 0) in vec3 inPosition;
// 顶点法线属性（location = 1）
layout(location = 1) in vec3 inNormal;
// 顶点颜色属性（location = 2）
layout(location = 2) in vec3 inColor;
// 纹理坐标属性（location = 3）
layout(location = 3) in vec2 inTexCoord;

// Uniform 变量
layout(push_constant) uniform PushConstants {
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
} pushConstants;

// 输出到片段着色器
layout(location = 0) out vec3 fragPos;       // 世界空间位置
layout(location = 1) out vec3 fragNormal;    // 法线
layout(location = 2) out vec3 fragColor;     // 颜色
layout(location = 3) out vec2 fragTexCoord;  // 纹理坐标

void main() {
    // 计算世界空间位置
    vec4 worldPos = pushConstants.model * vec4(inPosition, 1.0);

    // ===== 风场顶点偏移 =====
    float windTime = pushConstants.windTime;
    float windStrength = pushConstants.windStrength;

    if (windStrength > 0.001) {
        // 使用世界空间 Y 作为高度基准（树叶的局部 Y 接近 0，不适用）
        // 世界高度越高摆动越大，树干(base)不动，树冠(canopy)摆动
        float worldHeight = worldPos.y;
        float heightFactor = clamp(worldHeight / 10.0, 0.0, 1.0);
        heightFactor = heightFactor * heightFactor * heightFactor;  // 三次曲线，树冠摆动更集中

        // 主风向（西北方向）
        vec3 windDir = normalize(vec3(1.0, 0.0, 0.6));

        // 三频正弦叠加，模拟自然风的复杂运动
        float wind1 = sin(windTime * 1.2 + worldHeight * 1.5 + inPosition.x * 0.3);
        float wind2 = sin(windTime * 2.7 + inPosition.z * 1.5 + worldHeight * 0.8);
        float wind3 = sin(windTime * 0.5 + inPosition.x * 0.8 + inPosition.z * 0.4);
        float windFactor = (wind1 * 0.5 + wind2 * 0.3 + wind3 * 0.2) * heightFactor * windStrength * 1.0;

        // 垂直弹跳，模拟枝叶回弹
        float bob = sin(windTime * 1.8 + worldHeight * 2.5) * heightFactor * windStrength * 0.3;

        worldPos.xyz += windDir * windFactor;
        worldPos.y += bob;
    }
    // ===== 风场结束 =====

    fragPos = worldPos.xyz;
    
    // 应用变换矩阵
    gl_Position = pushConstants.proj * pushConstants.view * worldPos;

    // Vulkan的NDC坐标系Y轴是向下的，需要反转Y轴
    gl_Position.y = -gl_Position.y;

    // 计算法线（使用法线矩阵，处理非均匀缩放）
    mat3 normalMatrix = transpose(inverse(mat3(pushConstants.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    
    // 传递颜色到片段着色器
    fragColor = inColor;
    
    // 传递纹理坐标
    fragTexCoord = inTexCoord;
}