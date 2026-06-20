#version 450

/**
 * @file cloud.vert
 * @brief 体积云全屏四边形顶点着色器
 *
 * 使用内置的 gl_VertexIndex 生成覆盖整个屏幕的四边形，
 * 无需外部顶点缓冲。输出 NDC 坐标和 UV 坐标供片段着色器使用。
 */

// 输出到片段着色器的 NDC 位置和 UV
layout(location = 0) out vec2 fragUV;

void main() {
    // gl_VertexIndex: 0, 1, 2, 3 → TRIANGLE_STRIP
    // 0: (-1, -1)  1: (1, -1)  2: (-1, 1)  3: (1, 1)
    float x = float(gl_VertexIndex & 1) * 2.0 - 1.0;
    float y = float(gl_VertexIndex >> 1) * 2.0 - 1.0;

    gl_Position = vec4(x, y, 0.0, 1.0);

    // 传递 UV 坐标（片段着色器重建世界空间射线）
    fragUV = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
}
