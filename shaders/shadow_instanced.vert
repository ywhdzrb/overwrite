#version 450

/**
 * @file shadow_instanced.vert
 * @brief 实例化阴影贴图深度渲染顶点着色器（树木专用）
 *
 * 与 shadow.vert 管线结构兼容（相同 push constant 范围和描述符集布局），
 * 但多一个实例化顶点输入（binding 1），用于传入每棵树的模型矩阵。
 * 着色器计算公式：gl_Position = proj * view * treeModel * nodeTransform * vertex
 *   - proj/view: 光源投影和视图矩阵（push constant）
 *   - treeModel: 每棵树独立的位置/旋转/缩放矩阵（实例化输入）
 *   - nodeTransform: 树模型内部的节点层级变换（push constant model 字段）
 */

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

// 实例化数据：每棵树的模型矩阵（binding 1, VK_VERTEX_INPUT_RATE_INSTANCE）
layout(location = 4) in vec4 inInstRow0;
layout(location = 5) in vec4 inInstRow1;
layout(location = 6) in vec4 inInstRow2;
layout(location = 7) in vec4 inInstRow3;

#extension GL_EXT_scalar_block_layout : enable
layout(push_constant, scalar) uniform PushConstants {
    mat4 model;          // 节点层级变换（per-mesh node transform）
    mat4 view;           // 光源视图矩阵
    mat4 proj;           // 光源投影矩阵
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
    // 从实例化输入读取每棵树的模型矩阵
    mat4 treeModel = mat4(inInstRow0, inInstRow1, inInstRow2, inInstRow3);
    // 预乘：proj * view * treeModel * nodeTransform * vertex
    gl_Position = pushConstants.proj * pushConstants.view * treeModel * pushConstants.model * vec4(inPosition, 1.0);
}
