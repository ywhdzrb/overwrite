#version 450

/**
 * @file cloud.frag
 * @brief 体积云 Ray Marching — Nubis 风格 + 多层云顶羽化 + 花椰菜细节
 *
 * 核心改进（v3）：
 *   ① 多层随机云顶：3层不同频率ValueNoise累加，云顶变成"毛玻璃"状模糊渐层
 *      不再有单一明确的云顶边界，水平视线时云顶渐出宽度自动扩大
 *   ② 水平线羽化：视线角度越低，顶部渐出区间越宽（从30%→80%层厚），消除地平线接缝
 *   ③ 花椰菜高频凸起：在上半云区叠加高频Worley凸起，产生积云特有的凹凸表面
 *   ④ 增强光照：银边效应(边缘正向散射)+加强粉末糖，提升体积感和层次
 *
 * 基形改为纯ValueNoise FBM（3层互质频率，无Voronoi细胞）
 *   Worley纹理仅用于侵蚀和花椰菜的表面细节，不出现在基形中
 * 保留Nubis核心：
 *   覆盖度：remap减法
 *   侵蚀：remap(密度, 细节×强度, 1, 0, 1)
 *   光照：HG相位 + Beer透射率 + lightMarch单散射
 */

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler3D noiseTexture;

layout(push_constant) uniform PushConstants {
    mat4  invViewProj;
    vec4  cameraPos_cloudMin;     // w = cloudHeightMin
    vec4  cloudMax_time;          // x=cloudHeightMax, y=全局时间, z=风速, w=风向(弧度)
    vec4  params;                 // x=stepCount, y=coverage, z=densityMult, w=薄云层高度
    vec4  sunDir_dayFactor;       // xyz=太阳方向, w=昼夜因子 [0(夜)~1(昼)]
} push;

// ============================================================
// 常数参数
// ============================================================

// 注意：基形用纯ValueNoise FBM（3层频率互质），无需Worley纹理频率常量
const float DETAIL_FREQ   = 0.025;    // 侵蚀细节频率（Worley纹理）
const float CAULI_FREQ    = 0.10;     // 花椰菜高频细节频率（Worley纹理，≈12.5x DETAIL_FREQ）
                                       // 在云的上半部产生小尺度凹凸
const float CAULI_STR     = 0.18;     // 花椰菜强度（0.18，减小颗粒感，保留积云凹凸形态）
const float EROSION_STR   = 0.8;      // 侵蚀强度（越大边缘越破碎）
const float HG_G          = 0.6;      // HG散射非对称因子
const float EXTINCTION    = 0.5;      // 消光系数
const float LIGHT_STEP_SZ = 10.0;     // 光照单步步长（保持10m，避免单步光学厚度过大导致云体过暗）


// ============================================================
// 哈希函数
// ============================================================

/**
 * @brief 3D 哈希（用于 Value Noise）
 */
float hash31(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    // 线性组合版（替代乘法版）：避免任何分量为0时hash退化为0的问题，
    // 以及p.x=p.y=p.z时的方向性相关。使用大质数乘子保证各分量独立。
    return fract(p.x * 127.1 + p.y * 311.7 + p.z * 74.7);
}

/**
 * @brief 2D 哈希（用于抖动反走样）
 */
float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

/**
 * @brief Interleaved Gradient Noise — 打破颜色分层的输出抖动
 *
 * 相比纯白噪声，IGN在频域上能量分布更均匀，人眼不易感知。
 * 在输出前叠加 ±0.5/255 的微小扰动，破坏相邻像素的量化一致性，
 * 从而消除8bit渲染中常见的颜色分层（banding）现象。
 */
float interleavedGradientNoise(vec2 uv) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(uv, magic.xy)));
}

// ============================================================
// 噪声函数
// ============================================================

/**
 * @brief 3D Value Noise — 平滑连续的伪随机噪声
 *
 * 在整数格点上用hash31生成随机值，三线性插值+smoothstep产生连续场。
 */
float valueNoise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    // Hermite平滑
    f = f * f * (3.0 - 2.0 * f);

    float v000 = hash31(i + vec3(0, 0, 0));
    float v100 = hash31(i + vec3(1, 0, 0));
    float v010 = hash31(i + vec3(0, 1, 0));
    float v110 = hash31(i + vec3(1, 1, 0));
    float v001 = hash31(i + vec3(0, 0, 1));
    float v101 = hash31(i + vec3(1, 0, 1));
    float v011 = hash31(i + vec3(0, 1, 1));
    float v111 = hash31(i + vec3(1, 1, 1));

    float xy0 = mix(mix(v000, v100, f.x), mix(v010, v110, f.x), f.y);
    float xy1 = mix(mix(v001, v101, f.x), mix(v011, v111, f.x), f.y);
    return mix(xy0, xy1, f.z);
}

// ============================================================
// 工具函数
// ============================================================

/**
 * @brief Remap：将v从[low, high]线性映射到[newLow, newHigh]
 */
float remap(float v, float low, float high, float newLow, float newHigh) {
    return newLow + (v - low) / (high - low) * (newHigh - newLow);
}


// ============================================================
// 光照函数
// ============================================================

/**
 * @brief Henyey-Greenstein 相位函数
 * @param cosTheta 视角与光源夹角余弦
 * @param g 非对称因子（>0前向散射，<0后向散射）
 */
float hgPhase(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / max(pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5), 0.001);
}

// ============================================================
// 云层求交
// ============================================================

/**
 * @brief 射线与水平云层包围盒求交
 */
bool rayBoxIntersect(vec3 origin, vec3 dir, float hMin, float hMax,
                     out float tMin, out float tMax) {
    tMin = (hMin - origin.y) / dir.y;
    tMax = (hMax - origin.y) / dir.y;
    if (tMin > tMax) {
        float tmp = tMin; tMin = tMax; tMax = tmp;
    }
    if (tMax < 0.0) return false;
    tMin = max(tMin, 0.0);
    return (tMax - tMin) > 0.001;
}

// ============================================================
// 云密度模型 — Nubis连续噪声场 + 多层云顶 + 花椰菜高频凸起
// ============================================================

/**
 * @brief 计算云密度 — 四层融合：基形→覆盖→高度→侵蚀→花椰菜
 *
 * v3 关键改进：
 *
 * ① 多层随机云顶（代替原有的单层 localCloudMax）：
 *    用3层不同频率的ValueNoise累加，每层贡献不同的高度偏移。
 *    由于各层频率不同且互质，云顶不再是单一的起伏曲面，
 *    而是"毛玻璃"状的多层次叠层——没有任何一个高度是明确的"边界"。
 *    配合多层渐出，水平视线时云顶平滑融入天空，无可见接缝。
 *
 * ② 水平线羽化（参数 viewHorizon）：
 *    当视线接近水平(horizon→1.0)时，顶部渐出区间从30%层宽扩展到80%+。
 *    水平视线时，云的密度在120~180m高度范围内逐渐衰减到0，
 *    而不是在120m附近突然消失——水平接缝彻底消除。
 *
 * ③ 花椰菜高频凸起（cauliflower）：
 *    在侵蚀之后，用CAULI_FREQ=0.1(≈12.5m周期)的高频Worley噪声
 *    在云的上半部做二次乘法调制。由于频率远高于基形和侵蚀层，
 *    这种调制在云表面产生小尺度的凹凸感，模拟积云顶部的花椰菜状对流结构。
 *    花椰菜强度随高度递增：底部无→中部最强→顶部随渐出减弱。
 *
 * ④ 侵蚀强度与花椰菜解耦：
 *    侵蚀(remap减法)负责中尺度边缘蓬松感；
 *    花椰菜(乘法调制)负责小尺度表面凹凸感。
 *    二者在云中不同高度区间各有侧重，不会互相抵消。
 *
 * @param pos 世界空间采样位置
 * @param windOffset 风偏移量（时间驱动）
 * @param cloudMin 云层底部高度
 * @param cloudMax 云层顶部基础高度
 * @param viewHorizon 视线水平度 [0,1] 0=垂直向上, 1=水平, 控制顶部渐出宽度
 * @return 云密度值（经densityMult缩放）
 */
float getCloudDensity(vec3 pos, vec3 windOffset,
                      float cloudMin, float cloudMax,
                      float viewHorizon) {
    vec3 p = pos + windOffset;

    // ============================================================
    // 步骤1：基形噪声（同v2，ValueNoise主导 + Worley辅助 + 坐标扰动）
    // ============================================================
    // 多尺度 ValueNoise FBM — 纯连续场，完全无Voronoi细胞结构
    // 3层频率互质(×2.33)，避免谐波叠加产生的周期性
    // Worley纹理只用于侵蚀和花椰菜（作为表面细节），不出现在基形中
    float n1 = valueNoise3D(p * 0.003);           // 333m周期，大尺度主体
    float n2 = valueNoise3D(p * 0.007 + 30.0);    // 143m周期，中尺度细节
    float n3 = valueNoise3D(p * 0.015 + 60.0);    // 67m周期，小尺度修饰
    float baseShape = n1 * 0.55 + n2 * 0.30 + n3 * 0.15;

    // wobble 仅用于侵蚀和花椰菜的 Worley 纹理坐标扰动
    // 基形不需要 wobble（已无 Worley 需要打碎）
    float wobble1 = valueNoise3D(p * 0.001) * 0.4;
    float wobble2 = valueNoise3D(p * 0.003 + 50.0) * 0.3;
    float wobble = wobble1 + wobble2;

    // ============================================================
    // 步骤2：覆盖度 remap（同v2）
    // ============================================================
    float coverage = clamp(push.params.y, 0.0, 1.0);
    // 覆盖阈值微抖：用高频噪声(周期50m)微调每像素的覆盖阈值
    // 避免噪声恰好穿越阈值时产生的方向性切断线（"x轴割裂感"的根因之一）
    float thresholdDither = valueNoise3D(p * 0.02 + 200.0) * 0.02;
    baseShape = remap(baseShape, 1.0 - coverage + thresholdDither, 1.0, 0.0, 1.0);
    baseShape = clamp(baseShape, 0.0, 1.0);
    if (baseShape < 0.001) return 0.0;

    // ============================================================
    // 步骤3：高度剖面 — 多层随机云顶 + 水平线羽化
    // ============================================================
    //
    // ① 多层云顶：3层不同频率的ValueNoise构建模糊云顶
    //    每一层的频率和权重都不同，叠加后产生非周期性的云顶起伏
    //    任何单一高度都不是明确的云顶面
    //
    // ② 水平线羽化：viewHorizon=0(垂直看)时渐出区间窄，
    //    viewHorizon=1(水平看)时渐出区间宽3倍
    //    水平视线时，云顶从更早的高度开始渐出，到更高的高度完全消失
    //
    // ③ 底部sharp：模拟积云冷凝平底（不随视角变化）

    // 第1层：大尺度云顶起伏（~500m周期，权重最大）
    float topVary1 = valueNoise3D(vec3(pos.xz * 0.002, 0.0)) * 25.0;
    // 第2层：中尺度云顶起伏（~200m周期）
    float topVary2 = valueNoise3D(p * 0.005 + 50.0) * 15.0;
    // 第3层：小尺度云顶起伏（~100m周期）
    float topVary3 = valueNoise3D(p * 0.01 + 100.0) * 8.0;

    // 有效云顶高度 = 基础顶部 + 各层贡献
    float localCloudMax = cloudMax + 5.0
                        + topVary1
                        + topVary2 * 0.5
                        + topVary3 * 0.25;

    // 归一化高度（从云底到云顶）
    float nh = (pos.y - cloudMin) / (localCloudMax - cloudMin);

    // 水平视线羽化：viewHorizon=0时正常渐出，=1时渐出宽度+100%
    // topFadeStart: 开始渐出的nh位置（垂直0.35 / 水平0.15）
    // topFadeEnd:   完全消失的nh位置（垂直1.0 / 水平1.5）
    float topFadeStart = 0.30 - viewHorizon * 0.15;
    float topFadeEnd   = 0.90 + viewHorizon * 0.60;

    float bottomFade = smoothstep(0.0, 0.04, nh);
    float topFade    = 1.0 - smoothstep(topFadeStart, topFadeEnd, max(nh, 0.0));

    // 完整高度剖面
    float heightProfile = bottomFade * topFade;
    if (heightProfile < 0.001) return 0.0;

    // 基形 × 高度
    float density = baseShape * heightProfile;
    if (density < 0.001) return 0.0;

    // ============================================================
    // 步骤4：侵蚀 remap（同v2，云心保留、边缘挖空）
    // ============================================================
    vec4 dn = texture(noiseTexture, p * DETAIL_FREQ + wobble);
    float detail = dn.b * 0.6 + dn.a * 0.4;

    // 侵蚀强度在云心最大，在顶部/底部递减
    float hc = clamp(nh, 0.0, 1.0);
    float heightFactor = 1.0 - abs(hc - 0.15) * 1.5;
    heightFactor = clamp(heightFactor * heightFactor, 0.0, 1.0);
    float erosionStr = EROSION_STR * (0.2 + 0.8 * heightFactor);

    // 密度耦合：低密度区减弱侵蚀
    float densityCoupling = smoothstep(0.01, 0.3, density);

    // 侵蚀阈值（软钳制，保证不削到0）
    float erosionThreshold = min(detail * erosionStr * densityCoupling,
                                 density * 0.75);

    // remap减法侵蚀
    density = remap(density, erosionThreshold, 1.0, 0.0, 1.0);
    density = clamp(density, 0.0, 1.0);
    if (density < 0.001) return 0.0;

    // ============================================================
    // 步骤5：花椰菜高频凸起（v3新增！）
    // ============================================================
    //
    // 原理：在云的上半部(0.2~0.8 nh)，用CAULI_FREQ=0.1的高频Worley
    // 噪声对密度做乘法调制。CAULI_FREQ = 0.1 意味着纹素对应~10m空间，
    // 而云的典型水平尺寸~30-50m——每个云朵上有3-5个凸起，模拟
    // 积云顶部的花椰菜状对流形态。
    //
    // 注意：花椰菜是"乘法调制"而非"加法"或"侵蚀"，因为：
    //   - 乘法：密度高的区域凸起强，密度低的区域凸起弱
    //   - 不会产生负密度或超出[0,1]范围
    //   - 与侵蚀解耦，各自在不同尺度上起作用

    // 仅在云的上半部施加花椰菜效果
    float cauliZone = smoothstep(0.15, 0.40, hc)
                    * (1.0 - smoothstep(0.65, 0.95, hc));

    if (cauliZone > 0.001) {
        vec4 cn = texture(noiseTexture, p * CAULI_FREQ + wobble);
        float cauliNoise = cn.r * 0.4 + cn.g * 0.3 + cn.b * 0.2 + cn.a * 0.1;

        // 花椰菜强度 = 基础强度 × 高度区间 × 密度耦合
        float cauliStrength = CAULI_STR * cauliZone * densityCoupling;

        // 乘法调制：[0,1] → cauliMod ∈ [0.7, 1.3] 区间
        float cauliMod = 1.0 + (cauliNoise - 0.5) * cauliStrength;
        density *= cauliMod;
        density = clamp(density, 0.0, 1.0);
    }

    // ============================================================
    // 步骤6：密度倍率
    // ============================================================
    return density * push.params.z;
}

// ============================================================
// 光照步进
// ============================================================

/**
 * @brief 从采样点沿太阳方向步进，计算太阳透射率
 *
 * 使用与主步进相同的密度函数（含多层云顶和花椰菜），保证光照与密度一致。
 * viewHorizon 继承自主射线的值，确保云顶边界一致性。
 */
float lightMarch(vec3 origin, vec3 sunDir, vec3 windOffset,
                 float cloudMin, float cloudMax, float viewHorizon,
                 int lightSteps) {
    float totalDensity = 0.0;
    vec3 pos = origin;
    for (int i = 0; i < lightSteps; i++) {
        pos += sunDir * LIGHT_STEP_SZ;
        totalDensity += getCloudDensity(pos, windOffset, cloudMin, cloudMax, viewHorizon)
                      * LIGHT_STEP_SZ;
    }
    return exp(-totalDensity * EXTINCTION);
}

// ============================================================
// 主函数
// ============================================================

void main() {
    // ========== 1. 重建世界空间射线 ==========
    vec2 ndc = fragUV * 2.0 - 1.0;
    ndc.y = -ndc.y;
    vec4 clipPos = vec4(ndc, 1.0, 1.0);
    vec4 worldPos = push.invViewProj * clipPos;
    vec3 camPos = push.cameraPos_cloudMin.xyz;
    vec3 worldDir = normalize(worldPos.xyz / worldPos.w - camPos);

    // ========== 2. 计算视线水平度（用于云顶羽化） ==========
    // viewHorizon = 0: 视线垂直（看向天顶或地面）
    // viewHorizon = 1: 视线水平（看向地平线）
    // 水平视线时，云顶部渐出区间自动扩大，消除地平线接缝
    float viewHorizon = 1.0 - abs(normalize(worldDir).y);

    // ========== 3. 云层包围盒求交 ==========
    float cloudMin = push.cameraPos_cloudMin.w;
    float cloudMax = push.cloudMax_time.x;

    // 扩展云层包围盒顶部高度：
    //   - 多层云顶可达 cloudMax + 5 + 25 + 15*0.5 + 8*0.25 ≈ cloudMax + 48m
    //   - 水平视线渐出到 nh=1.5 (≈ 1.5 * 48 = 72m 以上)
    //   合计需要 +72m 余量
    float extCloudMax = cloudMax + 100.0;

    float tMin, tMax;
    if (!rayBoxIntersect(camPos, worldDir, cloudMin, extCloudMax, tMin, tMax)) {
        discard;
    }

    // ========== 4. 限制总步进距离 + 距离衰减 ==========
    float maxRayDist = 300.0;   // 总视野缩短到300m，云在堆叠之前就被衰减消失
    tMin = max(tMin, 0.0);
    tMax = min(tMax, tMin + maxRayDist);
    if (tMax - tMin < 0.01) discard;

    // ========== 5. Ray Marching 参数 ==========
    float time = push.cloudMax_time.y;
    float windSpeed = push.cloudMax_time.z;
    float windAngle = push.cloudMax_time.w;
    vec2 windDir = vec2(cos(windAngle), sin(windAngle));
    vec3 windOffset = time * windSpeed * vec3(windDir.x, 0.0, windDir.y);

    // ========== 5b. 从 params.x 解码主步进次数和光照步进次数 ==========
    // params.x = stepCount + lightSteps * 0.001（见 C++ CloudSystem::render 编码）
    float rawPC = push.params.x;
    int stepCount = int(rawPC);
    int lightSteps = int(round((rawPC - float(stepCount)) * 1000.0 + 0.001));
    lightSteps = max(min(lightSteps, 8), 1);
    stepCount = max(min(stepCount, 128), 8);
    float stepSize = (tMax - tMin) / float(stepCount);

    // 抖动反走样：改用 Interleaved Gradient Noise 替代 hash21
    // IGN 频谱能量集中在高频，人眼不易感知为噪声，同时在消除色带上比白噪声更有效
    float jitter = interleavedGradientNoise(gl_FragCoord.xy) * 0.30;
    vec3 pos = camPos + worldDir * (tMin + stepSize * jitter);

    float transmittance = 1.0;
    vec3 accumulatedColor = vec3(0.0);

    vec3 sunDir = normalize(push.sunDir_dayFactor.xyz);
    float dayFactor = push.sunDir_dayFactor.w;

    // 隔步光照复用：每2步计算一次 lightMarch，中间步进复用上次结果
    // 光照在云中是缓变函数，间隔~20m的两次采样差异很小，肉眼不可察觉
    float lastLightTrans = 1.0;
    int lightStepCounter = 0;

    // 薄云层高度（params.w < 0 时禁用）
    float thinCloudHeight = push.params.w;
    bool thinCloudActive = thinCloudHeight > cloudMin + 20.0;

    // ========== 6. Ray Marching 主循环 ==========
    for (int i = 0; i < stepCount; i++) {
        // 提前结束：超出云层范围
        if (pos.y < cloudMin - 2.0 || pos.y > extCloudMax + 2.0) {
            pos += worldDir * stepSize;
            continue;
        }
        // 提前终止：透明度趋近0
        if (transmittance < 0.005) break;

        float density = getCloudDensity(pos, windOffset, cloudMin, cloudMax, viewHorizon);

        // 距离衰减：从 180m 开始平滑淡出到 300m 完全消失
        // 300m以内云层已经过多个细胞——在此之后密度贡献全是层叠伪影，直接切掉
        float rayDist = length(pos - camPos);
        density *= 1.0 - smoothstep(180.0, maxRayDist, rayDist);

        // ========== 薄云层（高空卷云） ==========
        // 在params.w设定的高度上，叠加一层稀疏的卷云-like薄云
        // 使用独立的噪声采样，不受主密度模型覆盖/侵蚀影响
        // ① 提前检查垂直距离：仅当在薄云层 ±30m 范围内才计算
        if (thinCloudActive && abs(pos.y - thinCloudHeight) < 30.0 && density < 0.95) {
            vec3 tp = pos + windOffset * 0.3;
            float tn1 = valueNoise3D(tp * 0.002 + 200.0);
            float tn2 = valueNoise3D(tp * 0.005 + 300.0);
            float thinBase = tn1 * 0.6 + tn2 * 0.4;

            float thinThresh = 1.0 - clamp(push.params.y * 0.7, 0.0, 1.0);
            thinBase = remap(thinBase, thinThresh, 1.0, 0.0, 1.0);
            thinBase = clamp(thinBase, 0.0, 1.0);

            if (thinBase > 0.01) {
                float thick = mix(30.0, 60.0, dayFactor);
                float thinNh = (pos.y - thinCloudHeight) / thick;
                float thinProfile = smoothstep(0.0, 0.05, thinNh)
                                 * (1.0 - smoothstep(0.6, 1.0, thinNh));
                if (thinProfile > 0.01) {
                    float thinDensity = thinBase * thinProfile * 0.25
                                      * (0.5 + 0.5 * dayFactor);
                    // 薄云层与主云层取max（不叠加，保持薄云的半透明特性）
                    density = max(density, thinDensity);
                }
            }
        }

        if (density > 0.001) {
            // 体积深度感：视线仰角调制消光系数
            float viewUp = dot(normalize(worldDir), vec3(0.0, 1.0, 0.0));
            float extScale = 1.0 + 0.25 * viewUp;
            float sampleTrans = exp(-density * stepSize * EXTINCTION * extScale);

            // 隔步光照复用：每2次密度采样才做一次光照步进
            // lightStepCounter 仅在有密度的步进中递增，保证光照始终在最近的有效位置计算
            bool doLightMarch_ = (lightStepCounter % 2 == 0);
            if (doLightMarch_) {
                lastLightTrans = lightMarch(pos, sunDir, windOffset,
                                            cloudMin, cloudMax, viewHorizon,
                                            lightSteps);
            }
            lightStepCounter++;
            float lightTrans = lastLightTrans;

            // HG相位函数
            float cosAngle = dot(worldDir, sunDir);
            float phase = hgPhase(cosAngle, HG_G);

            // 银边效应（silver lining）：云边缘的额外正向散射亮部
            // 原理：在密度低的云边缘，光更容易穿透并在表面附近被散射回来
            // 实现：密度越低，phase增强越多
            float silverLining = exp(-density * 6.0) * 0.3;
            phase += silverLining * pow(max(cosAngle, 0.0), 2.0);

            // 粉末糖效应（powder sugar）：云边缘的额外散射亮部
            float powder = 1.0 - exp(-4.0 * density * stepSize);

            // 昼夜颜色插值：颜色值匹配实际天空盒 skybox.frag 的配色
            // 白天: dayMid=(0.55,0.65,0.80); 夜晚: nightMid=(0.03,0.03,0.08)
            vec3 sunColor = mix(vec3(0.08, 0.08, 0.25), vec3(1.0, 0.85, 0.5), dayFactor);
            vec3 ambientColor = mix(vec3(0.03, 0.03, 0.08), vec3(0.50, 0.60, 0.80), dayFactor);
            vec3 scatterColor = mix(vec3(0.04, 0.04, 0.10), vec3(0.58, 0.65, 0.80), dayFactor);

            // 太阳强度随昼夜变化：夜间直射光大幅减弱但保留月光成分
            float sunIntensity = dayFactor * 0.9 + 0.1;

            // 光照分量
            // 环境光和散射光底数提升：阴影下环境光 0.35(原0.2)、散射 0.5(原0.3)
            // 使云体暗部更亮但不影响亮部
            vec3 directLight   = sunColor * sunIntensity * lightTrans * phase;
            vec3 ambientLight  = ambientColor * (0.35 + 0.65 * lightTrans);
            vec3 scatterLight  = scatterColor * (1.0 - lightTrans) * 0.5;

            vec3 sampleColor = (directLight + ambientLight + scatterLight) * powder;

            // 前置混合
            accumulatedColor += transmittance * (1.0 - sampleTrans) * sampleColor;
            transmittance *= sampleTrans;
        }

        pos += worldDir * stepSize;
    }

    gl_FragDepth = 0.9999;
    // 输出前空间抖动：用IGN打破8bit颜色分层
    // 幅度 ±1.5/255 ≈ ±0.6%，对亮部不可感知，但在暗部过渡区足以打断量化一致性
    float dither = (interleavedGradientNoise(gl_FragCoord.xy) - 0.5) * 1.5 / 255.0;
    // 同时抖动alpha通道，防止透明度色阶
    float alphaDither = (interleavedGradientNoise(gl_FragCoord.xy + 100.0) - 0.5) * 1.0 / 255.0;
    outColor = vec4(accumulatedColor + dither, 1.0 - transmittance + alphaDither);
}
