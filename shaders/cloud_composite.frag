#version 450

/**
 * @file cloud_composite.frag
 * @brief 半分辨率云上采样合成片段着色器
 *
 * 将半分辨率渲染的云纹理通过双线性滤波上采样，
 * 利用Alpha混合叠加到全分辨率场景上。
 * 管线状态已配置 SRC_ALPHA + ONE_MINUS_SRC_ALPHA 混合，
 * 因此直接输出即可正确合成。
 */

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D cloudTexture;

void main() {
    // sampler2D 默认线性滤波，自动完成半分辨率→全分辨率上采样
    outColor = texture(cloudTexture, fragUV);

    // 写入固定远平面深度值以参与遮挡测试
    // 合成管线已启用深度测试（LESS），不透明物体深度更小则遮挡此片段
    gl_FragDepth = 0.9999;
}
