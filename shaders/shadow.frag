#version 450

/**
 * @file shadow.frag
 * @brief 阴影贴图深度渲染片段着色器
 *
 * 将 gl_FragCoord.z（深度值 0-1）写入 R32_SFLOAT 颜色附件。
 * 主着色器通过常规 sampler2D 采样此值并手动进行深度比较。
 */

layout(location = 0) out float outDepth;

void main() {
    // 直接输出深度值（Vulkan 光栅化自动将 clip_z 映射到 [0,1]）
    // 深度偏移由主着色器 calcShadow 中的 bias 统一处理
    outDepth = gl_FragCoord.z;
}
