// 3D模型加载实现
// 负责加载和处理OBJ格式的3D模型
#define TINYOBJLOADER_IMPLEMENTATION
#include "renderer/model.h"
#include "core/vulkan_device.h"
#include <tiny_obj_loader.h>
#include <iostream>
#include <array>
#include <unordered_map>

namespace owengine {

using namespace std;

// 获取顶点绑定描述
// 定义顶点数据的输入布局
VkVertexInputBindingDescription Vertex::getBindingDescription() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 4> Vertex::getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
    
    // 位置
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);
    
    // 法线
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);
    
    // 颜色
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, color);
    
    // 纹理坐标
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(Vertex, texCoord);
    
    return attributeDescriptions;
}

Model::Model(std::shared_ptr<VulkanDevice> device)
    : device(device) {
}

Model::~Model() {
    cleanup();
}

void Model::loadFromObj(const std::string& filename) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str())) {
        throw std::runtime_error(warn + err);
    }
    
    std::unordered_map<Vertex, uint32_t> uniqueVertices{};
    
    // 第一步：加载顶点数据（不包含法线）
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};
            
            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            // 检查是否有纹理坐标
            if (index.texcoord_index >= 0) {
                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };
            } else {
                vertex.texCoord = {0.0f, 0.0f};
            }

            vertex.color = {1.0f, 1.0f, 1.0f};
            vertex.normal = {0.0f, 0.0f, 0.0f};  // 初始化为零向量
            
            if (uniqueVertices.count(vertex) == 0) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }
            
            indices.push_back(uniqueVertices[vertex]);
        }
    }

    // 第二步：计算顶点法线（通过面法线平均）
    // 遍历所有三角形，计算面法线并累加到顶点
    for (size_t i = 0; i < indices.size(); i += 3) {
        // 获取三角形的三个顶点索引
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        
        // 获取三个顶点的位置
        glm::vec3 v0 = vertices[i0].pos;
        glm::vec3 v1 = vertices[i1].pos;
        glm::vec3 v2 = vertices[i2].pos;
        
        // 计算边向量
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        
        // 计算面法线（叉积）
        glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));
        
        // 将面法线累加到三个顶点
        vertices[i0].normal += faceNormal;
        vertices[i1].normal += faceNormal;
        vertices[i2].normal += faceNormal;
    }
    
    // 第三步：归一化所有顶点法线
    for (auto& vertex : vertices) {
        vertex.normal = glm::normalize(vertex.normal);
    }

    std::cout << "Loaded model: " << filename << std::endl;
    std::cout << "Vertices: " << vertices.size() << std::endl;
    std::cout << "Indices: " << indices.size() << std::endl;
}

void Model::cleanup() {
    vertices.clear();
    indices.clear();
}

} // namespace owengine