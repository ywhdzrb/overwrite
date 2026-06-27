#version 450

/**
 * @file shadow.vert
 * @brief 阴影贴图深度渲染顶点着色器
 *
 * 只输出顶点位置到深度缓冲，不输出颜色或法线。
 * 使用与主着色器相同的顶点输入布局和 push constants 结构（240 字节），
 * 以便复用现有的渲染函数（terrain/model/tree/stone 等无需修改）。
 * model/view/proj 传入的是光源空间的变换矩阵（lightVP × modelMatrix）。
 */

// 顶点输入 — 与主管线 VertexFormat::POSITION_COLOR 布局完全一致
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

// Push constants — 与主着色器大小一致（240 字节），只使用 model/view/proj
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
    float windTime;
    float windStrength;
    vec3 normalScale;
} pushConstants;

void main() {
    // 从模型空间 → 光源裁剪空间
    // 注意：gl_Position.y 不需要取反。Vulkan 中 viewport 变换已自动处理 Y 轴映射，
    // NDC y=-1（场景底部）→ 视口 y=0（图像顶部），与纹理坐标 V=0（图像顶部）一致。
    gl_Position = pushConstants.proj * pushConstants.view * pushConstants.model * vec4(inPosition, 1.0);
}
