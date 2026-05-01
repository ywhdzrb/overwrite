#version 450
// 草丛实例化顶点着色器（交叉十字面片版）
//
// 功能：
//   1. 实例化渲染：binding 0 = 十字交叉面片顶点（带 UV），binding 1 = 实例数据
//   2. 复合多频风场 + 阵风系统
//   3. 角色交互挤压 + 弹簧恢复动画
//   4. 草尖弯曲随高度二次增长，根部固定
//   5. 传递 UV 坐标到片段着色器

// ---- 顶点输入（binding 0：十字面片几何体） ----
layout(location = 0) in vec3 inPosition;    // 局部坐标
layout(location = 1) in vec2 inUV;          // 纹理坐标

// ---- 实例输入（binding 1：每根草茎独有数据） ----
layout(location = 2) in vec3 instancePosition;
layout(location = 3) in float instanceYaw;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceWindSeed;
layout(location = 6) in float instancePushState;

// ---- Push Constants ----
layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
    vec4 timeParams;    // x=time, y=windStrength, z=playerRadius, w=playerForce
    vec4 playerPos;     // xyz=playerWorldPos
} push;

// ---- 输出到片段着色器 ----
layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out float fragHeight;

// 简单伪随机函数
float hash(float x) {
    return fract(sin(x * 127.1 + 311.7) * 43758.5453);
}

void main() {
    vec3 localPos = inPosition;

    // === 1. 绕 Y 轴旋转 ===
    float c = cos(instanceYaw);
    float s = sin(instanceYaw);
    vec3 rPos;
    rPos.x = localPos.x * c - localPos.z * s;
    rPos.z = localPos.x * s + localPos.z * c;
    rPos.y = localPos.y * instanceScale;

    float heightFrac = localPos.y;

    // === 2. 复合风场 + 阵风系统 ===
    float windTime = push.timeParams.x;
    float windStr  = push.timeParams.y;
    float bendFact = heightFrac * heightFrac;

    float windPhase = hash(instanceWindSeed) * 6.28318;
    float freq1 = 0.6 + hash(instanceWindSeed + 1.0) * 0.8;
    float freq2 = 1.8 + hash(instanceWindSeed + 2.0) * 1.2;
    float freq3 = 3.5 + hash(instanceWindSeed + 3.0) * 2.0;

    float wave1 = sin(windTime * freq1 + windPhase);
    float wave2 = sin(windTime * freq2 + windPhase * 1.7) * 0.35;
    float wave3 = sin(windTime * freq3 + windPhase * 3.1) * 0.15;
    float windOsc = (wave1 + wave2 + wave3) / 1.2;

    float gustRaw = sin(windTime * 0.13 + hash(instanceWindSeed + 4.0) * 1.5) * 0.5 + 0.5;
    float gust = gustRaw * gustRaw * gustRaw;
    float gustFactor = 0.6 + gust * 0.4;

    float windAngle = windTime * 0.2 + hash(instanceWindSeed + 5.0) * 0.5;
    vec2 windDir = vec2(sin(windAngle), cos(windAngle));

    float turbX = sin(windTime * 2.5 + instanceWindSeed * 15.0) * 0.2;
    float turbZ = cos(windTime * 3.1 + instanceWindSeed * 25.0) * 0.2;
    windDir += vec2(turbX, turbZ);
    windDir = normalize(windDir);

    float windMag = windOsc * windStr * gustFactor * bendFact * 0.35;
    vec3 windDisp = vec3(windDir * windMag, 0.0);
    windDisp.y -= abs(windMag) * 0.15;

    vec3 displaced = rPos + windDisp;

    // === 3. 世界坐标 ===
    vec3 worldPos = instancePosition + displaced;

    // === 4. 角色交互挤压 ===
    vec3 toPlayer = worldPos - push.playerPos.xyz;
    float dist = length(toPlayer);
    float playerForce  = push.timeParams.w;

    if (instancePushState > 0.001) {
        // 向外推挤 + 向下压弯（模拟踩踏效果，防止草在脚下变长）
        vec3 pushDir = normalize(toPlayer);
        pushDir.y = -0.5;  // 强制向下分量，草被踩弯而不是向上窜长
        pushDir = normalize(pushDir);

        float pushMag = instancePushState * playerForce * bendFact;
        worldPos += pushDir * pushMag;
    }

    // === 5. 变换到裁切空间 ===
    gl_Position = push.proj * push.view * vec4(worldPos, 1.0);
    gl_Position.y = -gl_Position.y;

    // === 输出到片段着色器 ===
    fragUV        = inUV;
    fragWorldPos  = worldPos;
    fragHeight    = heightFrac;
}
