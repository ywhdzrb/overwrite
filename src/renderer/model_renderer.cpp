// OBJ模型渲染器实现
// 用于加载和渲染OBJ格式的3D模型
#include "model_renderer.h"
#include "vulkan_device.h"
#include <stdexcept>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vgame {

ModelRenderer::ModelRenderer(std::shared_ptr<VulkanDevice> device)
    : device(device),
      position(0.0f, 0.0f, 0.0f),
      scale(1.0f, 1.0f, 1.0f),
      rotation(0.0f, 0.0f, 0.0f) {
    model = std::make_unique<Model>(device);
}

ModelRenderer::~ModelRenderer() {
    cleanup();
}

void ModelRenderer::create() {
    // 模型数据在loadModel中加载
}

void ModelRenderer::cleanup() {
    mesh.reset();
}

void ModelRenderer::loadModel(const std::string& filename) {
    // 加载OBJ文件
    model->loadFromObj(filename);

    // 使用Mesh类创建顶点和索引缓冲区（使用设备本地内存，性能更好）
    mesh = std::make_unique<Mesh>(device, model->getVertices(), model->getIndices());
}

void ModelRenderer::setPosition(const glm::vec3& pos) {
    position = pos;
}

void ModelRenderer::setScale(const glm::vec3& s) {
    scale = s;
}

void ModelRenderer::setRotation(float pitch, float yaw, float roll) {
    rotation = glm::vec3(pitch, yaw, roll);
}

void ModelRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                          const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    if (!mesh) {
        return;  // 模型未加载
    }

    // 设置push constants
    PushConstants pushConstants{};

    // 创建模型矩阵（包含位置、旋转和缩放）
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));  // pitch
    model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));  // yaw
    model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));  // roll
    model = glm::scale(model, scale);

    pushConstants.model = model;
    pushConstants.view = viewMatrix;
    pushConstants.proj = projectionMatrix;

    vkCmdPushConstants(commandBuffer, pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushConstants);

    // 绑定并绘制Mesh
    mesh->bind(commandBuffer);
    mesh->draw(commandBuffer);
}

} // namespace vgame