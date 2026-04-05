// GLTF 模型实现
#include "gltf_model.h"
#include "logger.h"
#include <algorithm>
#include <stdexcept>
#include <filesystem>

// 启用 GLM 实验性扩展
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

// STB Image 用于解码嵌入纹理（由 tinygltf.cc 提供 STB_IMAGE_IMPLEMENTATION）
#include "stb_image.h"

namespace vgame {

GLTFModel::GLTFModel(std::shared_ptr<VulkanDevice> device,
                     std::shared_ptr<TextureLoader> textureLoader)
    : device(device),
      textureLoader(textureLoader),
      position(0.0f, 0.0f, 0.0f),
      rotation(0.0f, 0.0f, 0.0f),
      scale(1.0f, 1.0f, 1.0f),
      boundingBox(glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX)) {
    
    Logger::info("GLTFModel initialized");
}

GLTFModel::~GLTFModel() {
    cleanup();
}

bool GLTFModel::loadFromFile(const std::string& filename) {
    Logger::info("Loading GLTF model: " + filename);
    
    // 清理之前的数据
    cleanup();
    
    // 保存文件目录
    size_t lastSlash = filename.find_last_of("/\\");
    directory = (lastSlash != std::string::npos) ? filename.substr(0, lastSlash) : ".";
    
    // 加载 glTF 模型
    if (!loadGLTF(filename)) {
        Logger::error("Failed to load GLTF model: " + filename);
        return false;
    }
    
    // 设置模型名称（使用文件名）
    name = (lastSlash != std::string::npos) ? filename.substr(lastSlash + 1) : filename;
    // 移除扩展名
    size_t lastDot = name.find_last_of('.');
    if (lastDot != std::string::npos) {
        name = name.substr(0, lastDot);
    }
    
    // 处理所有材质
    for (const auto& gltfMaterial : gltfModel.materials) {
        processMaterial(gltfMaterial);
    }
    
    // 添加默认材质（如果没有材质）
    if (materials.empty()) {
        MaterialData defaultMaterial;
        defaultMaterial.baseColor = glm::vec3(1.0f);
        defaultMaterial.metallic = 0.0f;
        defaultMaterial.roughness = 0.5f;
        defaultMaterial.useBaseColorTexture = false;
        defaultMaterial.useNormalTexture = false;
        defaultMaterial.useMetallicRoughnessTexture = false;
        materials.push_back(defaultMaterial);
    }
    
    // 处理所有节点
    for (size_t nodeIndex = 0; nodeIndex < gltfModel.nodes.size(); ++nodeIndex) {
        const auto& gltfNode = gltfModel.nodes[nodeIndex];
        
        NodeData nodeData;
        nodeData.meshIndex = -1;
        nodeData.transform = glm::mat4(1.0f);
        
        // 解析节点变换
        if (!gltfNode.matrix.empty()) {
            // 使用矩阵
            const auto& m = gltfNode.matrix;
            nodeData.transform = glm::mat4(
                m[0], m[1], m[2], m[3],
                m[4], m[5], m[6], m[7],
                m[8], m[9], m[10], m[11],
                m[12], m[13], m[14], m[15]
            );
        } else {
            // 使用 TRS（平移、旋转、缩放）
            glm::mat4 T = glm::mat4(1.0f);
            if (!gltfNode.translation.empty()) {
                T = glm::translate(glm::mat4(1.0f), 
                                   glm::vec3(gltfNode.translation[0], 
                                            gltfNode.translation[1], 
                                            gltfNode.translation[2]));
            }
            
            glm::mat4 R = glm::mat4(1.0f);
            if (!gltfNode.rotation.empty()) {
                glm::quat q(gltfNode.rotation[3], 
                           gltfNode.rotation[0], 
                           gltfNode.rotation[1], 
                           gltfNode.rotation[2]);
                R = glm::mat4_cast(q);
            }
            
            glm::mat4 S = glm::mat4(1.0f);
            if (!gltfNode.scale.empty()) {
                S = glm::scale(glm::mat4(1.0f),
                              glm::vec3(gltfNode.scale[0],
                                       gltfNode.scale[1],
                                       gltfNode.scale[2]));
            }
            
            nodeData.transform = T * R * S;
        }
        
        // 处理网格
        if (gltfNode.mesh >= 0) {
            size_t meshIndex = processMesh(gltfModel.meshes[gltfNode.mesh]);
            nodeData.meshIndex = meshIndex;
        }
        
        // 处理子节点
        for (int childIndex : gltfNode.children) {
            nodeData.children.push_back(static_cast<size_t>(childIndex));
        }
        
        nodes.push_back(nodeData);
    }
    
    // 加载蒙皮数据
    loadSkins();
    
    // 存储节点原始变换（用于动画）
    storeNodeTransforms();
    
    // 加载动画数据
    loadAnimations();
    
    // 计算包围盒
    calculateBoundingBox();
    
    Logger::info("GLTF model loaded successfully: " + name + 
                " (nodes: " + std::to_string(nodes.size()) + 
                ", meshes: " + std::to_string(meshes.size()) + 
                ", materials: " + std::to_string(materials.size()) + 
                ", animations: " + std::to_string(animations.size()) + 
                ", skins: " + std::to_string(skins.size()) + ")");
    
    return true;
}

void GLTFModel::cleanup() {
    meshes.clear();
    materials.clear();
    nodes.clear();
    textureCache.clear();
    
    boundingBox = std::make_pair(glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX));
    
    Logger::info("GLTFModel cleaned up");
}

void GLTFModel::render(VkCommandBuffer commandBuffer,
                       VkPipelineLayout pipelineLayout,
                       const glm::mat4& viewMatrix,
                       const glm::mat4& projectionMatrix,
                       const glm::mat4& modelMatrix) {
    // 渲染所有根节点
    for (size_t i = 0; i < nodes.size(); ++i) {
        // 检查是否是根节点（没有被其他节点引用）
        bool isRoot = true;
        for (const auto& node : nodes) {
            if (std::find(node.children.begin(), node.children.end(), i) != node.children.end()) {
                isRoot = false;
                break;
            }
        }
        
        if (isRoot) {
            renderNode(commandBuffer, pipelineLayout, viewMatrix, projectionMatrix, i, modelMatrix);
        }
    }
}

void GLTFModel::renderNode(VkCommandBuffer commandBuffer,
                          VkPipelineLayout pipelineLayout,
                          const glm::mat4& viewMatrix,
                          const glm::mat4& projectionMatrix,
                          size_t nodeIndex,
                          const glm::mat4& parentMatrix) {
    if (nodeIndex >= nodes.size()) {
        return;
    }
    
    const auto& node = nodes[nodeIndex];
    
    // 计算节点的变换矩阵
    glm::mat4 nodeMatrix = parentMatrix * node.transform;
    
    // 如果节点有网格，渲染网格
    if (node.meshIndex < meshes.size()) {
        const auto& meshData = meshes[node.meshIndex];
        
        if (meshData.mesh) {
            // 设置 push constants
            struct PushConstants {
                glm::mat4 model;
                glm::mat4 view;
                glm::mat4 proj;
                glm::vec3 baseColor;
                float metallic;
                float roughness;
                int hasTexture;
                float _pad0;
            };
            
            PushConstants pushConstants{};
            pushConstants.model = nodeMatrix;
            pushConstants.view = viewMatrix;
            pushConstants.proj = projectionMatrix;
            
            // 获取材质数据
            if (meshData.materialIndex < materials.size()) {
                const auto& material = materials[meshData.materialIndex];
                pushConstants.baseColor = material.baseColor;
                pushConstants.metallic = material.metallic;
                pushConstants.roughness = material.roughness;
                pushConstants.hasTexture = material.useBaseColorTexture ? 1 : 0;
            }
            
            vkCmdPushConstants(commandBuffer, pipelineLayout,
                             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(PushConstants), &pushConstants);
            
            // 绑定并绘制网格
            meshData.mesh->bind(commandBuffer);
            meshData.mesh->draw(commandBuffer);
        }
    }
    
    // 递归渲染子节点
    for (size_t childIndex : node.children) {
        renderNode(commandBuffer, pipelineLayout, viewMatrix, projectionMatrix, childIndex, nodeMatrix);
    }
}

glm::mat4 GLTFModel::getModelMatrix() const {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, scale);
    return model;
}

glm::mat4 GLTFModel::getNodeTransform(size_t nodeIndex, const glm::mat4& parentMatrix) const {
    if (nodeIndex >= nodes.size()) {
        return parentMatrix;
    }
    
    const auto& node = nodes[nodeIndex];
    return parentMatrix * node.transform;
}

bool GLTFModel::loadGLTF(const std::string& filename) {
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;
    
    // 判断文件类型（自动检测）
    bool ret = false;
    
    // 首先检查文件扩展名
    bool isGlbByExtension = (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".glb");
    
    // 如果扩展名是 .glb，或者我们怀疑它可能是二进制格式，检查文件头
    bool useBinaryLoader = isGlbByExtension;
    
    if (!isGlbByExtension) {
        // 读取文件前 4 个字节检查 magic number
        FILE* f = fopen(filename.c_str(), "rb");
        if (f) {
            unsigned char header[4];
            size_t bytesRead = fread(header, 1, 4, f);
            fclose(f);
            
            if (bytesRead == 4 && header[0] == 0x67 && header[1] == 0x6C && 
                header[2] == 0x54 && header[3] == 0x46) {
                // 文件开头是 "glTF"，这是二进制格式
                useBinaryLoader = true;
                Logger::info("Detected binary glTF format despite .gltf extension");
            }
        }
    }
    
    if (useBinaryLoader) {
        ret = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filename);
    } else {
        ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filename);
    }
    
    if (!warn.empty()) {
        Logger::warning("GLTF warning: " + warn);
    }
    
    if (!err.empty()) {
        Logger::error("GLTF error: " + err);
    }
    
    if (!ret) {
        Logger::error("Failed to parse glTF file: " + filename);
        return false;
    }
    
    return true;
}

size_t GLTFModel::processMesh(const tinygltf::Mesh& gltfMesh) {
    for (const auto& primitive : gltfMesh.primitives) {
        // 获取顶点数据
        const auto& positionAccessor = gltfModel.accessors[primitive.attributes.at("POSITION")];
        const auto* positionData = getBufferData(positionAccessor);
        const auto& positionBufferView = gltfModel.bufferViews[positionAccessor.bufferView];
        
        // 构建顶点向量
        std::vector<Vertex> vertices;
        vertices.reserve(positionAccessor.count);
        
        for (size_t i = 0; i < positionAccessor.count; ++i) {
            Vertex vertex{};
            
            // 位置
            size_t posStride = positionBufferView.byteStride > 0 ? positionBufferView.byteStride : sizeof(glm::vec3);
            const float* pos = reinterpret_cast<const float*>(positionData + i * posStride);
            vertex.pos = glm::vec3(pos[0], pos[1], pos[2]);
            
            // 法线（如果存在）
            if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
                const auto& normalAccessor = gltfModel.accessors[primitive.attributes.at("NORMAL")];
                const auto* normalData = getBufferData(normalAccessor);
                const auto& normalBufferView = gltfModel.bufferViews[normalAccessor.bufferView];
                size_t normalStride = normalBufferView.byteStride > 0 ? normalBufferView.byteStride : sizeof(glm::vec3);
                const float* norm = reinterpret_cast<const float*>(normalData + i * normalStride);
                vertex.normal = glm::vec3(norm[0], norm[1], norm[2]);
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            
            // UV 坐标（如果存在）
            if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                const auto& texCoordAccessor = gltfModel.accessors[primitive.attributes.at("TEXCOORD_0")];
                const auto* texCoordData = getBufferData(texCoordAccessor);
                const auto& texCoordBufferView = gltfModel.bufferViews[texCoordAccessor.bufferView];
                size_t texCoordStride = texCoordBufferView.byteStride > 0 ? texCoordBufferView.byteStride : sizeof(glm::vec2);
                const float* uv = reinterpret_cast<const float*>(texCoordData + i * texCoordStride);
                vertex.texCoord = glm::vec2(uv[0], uv[1]);
            } else {
                vertex.texCoord = glm::vec2(0.0f, 0.0f);
            }
            
            // 颜色（如果存在）
            if (primitive.attributes.find("COLOR_0") != primitive.attributes.end()) {
                const auto& colorAccessor = gltfModel.accessors[primitive.attributes.at("COLOR_0")];
                const auto* colorData = getBufferData(colorAccessor);
                const auto& colorBufferView = gltfModel.bufferViews[colorAccessor.bufferView];
                if (colorAccessor.type == TINYGLTF_TYPE_VEC4) {
                    const float* col = reinterpret_cast<const float*>(colorData + i * colorAccessor.ByteStride(colorBufferView));
                    vertex.color = glm::vec3(col[0], col[1], col[2]);
                } else {
                    const float* col = reinterpret_cast<const float*>(colorData + i * colorAccessor.ByteStride(colorBufferView));
                    vertex.color = glm::vec3(col[0], col[1], col[2]);
                }
            } else {
                vertex.color = glm::vec3(1.0f);
            }
            
            vertices.push_back(vertex);
        }
        
        // 获取索引数据
        std::vector<uint32_t> indices;
        if (primitive.indices >= 0) {
            const auto& indexAccessor = gltfModel.accessors[primitive.indices];
            const auto* indexData = getBufferData(indexAccessor);
            
            indices.reserve(indexAccessor.count);
            for (size_t i = 0; i < indexAccessor.count; ++i) {
                if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    indices.push_back(static_cast<uint32_t>(indexData[i]));
                } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const uint16_t* data = reinterpret_cast<const uint16_t*>(indexData);
                    indices.push_back(static_cast<uint32_t>(data[i]));
                } else {
                    const uint32_t* data = reinterpret_cast<const uint32_t*>(indexData);
                    indices.push_back(data[i]);
                }
            }
        } else {
            // 如果没有索引，生成线性索引
            indices.reserve(vertices.size());
            for (size_t i = 0; i < vertices.size(); ++i) {
                indices.push_back(static_cast<uint32_t>(i));
            }
        }
        
        // 创建网格
        auto mesh = std::make_unique<Mesh>(device, vertices, indices);
        
        // 计算网格包围盒
        glm::vec3 minBounds(FLT_MAX);
        glm::vec3 maxBounds(-FLT_MAX);
        for (const auto& vertex : vertices) {
            minBounds = glm::min(minBounds, vertex.pos);
            maxBounds = glm::max(maxBounds, vertex.pos);
        }
        
        // 添加到网格列表
        MeshData meshData;
        meshData.mesh = std::move(mesh);
        meshData.materialIndex = primitive.material >= 0 ? static_cast<size_t>(primitive.material) : 0;
        meshData.minBounds = minBounds;
        meshData.maxBounds = maxBounds;
        
        meshes.push_back(std::move(meshData));
    }
    
    return meshes.size() - 1;
}

size_t GLTFModel::processMaterial(const tinygltf::Material& gltfMaterial) {
    MaterialData material;
    
    // PBR 材质参数
    const auto& pbr = gltfMaterial.pbrMetallicRoughness;
    
    // 基础颜色
    if (!pbr.baseColorFactor.empty()) {
        material.baseColor = glm::vec3(pbr.baseColorFactor[0],
                                      pbr.baseColorFactor[1],
                                      pbr.baseColorFactor[2]);
    } else {
        material.baseColor = glm::vec3(1.0f);
    }
    
    // 金属度
    material.metallic = static_cast<float>(pbr.metallicFactor);
    
    // 粗糙度
    material.roughness = static_cast<float>(pbr.roughnessFactor);
    
    // 基础颜色纹理
    if (pbr.baseColorTexture.index >= 0) {
        material.baseColorTexture = loadTexture(pbr.baseColorTexture.index);
        material.useBaseColorTexture = (material.baseColorTexture != nullptr);
    } else {
        material.useBaseColorTexture = false;
    }
    
    // 法线纹理
    if (gltfMaterial.normalTexture.index >= 0) {
        material.normalTexture = loadTexture(gltfMaterial.normalTexture.index);
        material.useNormalTexture = (material.normalTexture != nullptr);
    } else {
        material.useNormalTexture = false;
    }
    
    // 金属度/粗糙度纹理
    if (pbr.metallicRoughnessTexture.index >= 0) {
        material.metallicRoughnessTexture = loadTexture(pbr.metallicRoughnessTexture.index);
        material.useMetallicRoughnessTexture = (material.metallicRoughnessTexture != nullptr);
    } else {
        material.useMetallicRoughnessTexture = false;
    }
    
    // 输出材质信息用于调试
    Logger::info("Material loaded: baseColor=(" + 
                std::to_string(material.baseColor.r) + ", " +
                std::to_string(material.baseColor.g) + ", " +
                std::to_string(material.baseColor.b) + "), " +
                "metallic=" + std::to_string(material.metallic) + ", " +
                "roughness=" + std::to_string(material.roughness) + ", " +
                "hasTexture=" + (material.useBaseColorTexture ? "yes" : "no"));
    
    materials.push_back(material);
    
    return materials.size() - 1;
}

std::shared_ptr<Texture> GLTFModel::loadTexture(int textureIndex) {
    if (textureIndex < 0 || textureIndex >= static_cast<int>(gltfModel.textures.size())) {
        return nullptr;
    }
    
    // 检查缓存
    auto it = textureCache.find(textureIndex);
    if (it != textureCache.end()) {
        return it->second;
    }
    
    const auto& gltfTexture = gltfModel.textures[textureIndex];
    const auto& gltfImage = gltfModel.images[gltfTexture.source];
    
    std::shared_ptr<Texture> texture = nullptr;
    
    if (!gltfImage.uri.empty()) {
        // 外部纹理文件
        std::string texturePath = directory + "/" + gltfImage.uri;
        texture = textureLoader->loadTexture(texturePath, true);
        Logger::info("Loading external texture: " + texturePath);
    } else {
        // 嵌入式纹理（buffer view）
        Logger::info("Loading embedded texture (index: " + std::to_string(gltfTexture.source) + ")");
        
        if (gltfImage.bufferView >= 0) {
            const auto& bufferView = gltfModel.bufferViews[gltfImage.bufferView];
            const auto& buffer = gltfModel.buffers[bufferView.buffer];
            
            // 获取图像数据
            const unsigned char* imageData = buffer.data.data() + bufferView.byteOffset;
            size_t dataSize = bufferView.byteLength;
            
            // 使用 stb_image 从内存中解码
            int width, height, channels;
            unsigned char* decodedData = stbi_load_from_memory(imageData, static_cast<int>(dataSize), 
                                                                &width, &height, &channels, 4);  // 强制 4 通道（RGBA）
            
            if (decodedData) {
                Logger::info("Embedded texture decoded: " + std::to_string(width) + "x" + 
                            std::to_string(height) + ", channels: " + std::to_string(channels));
                
                // 创建纹理
                size_t textureSize = width * height * 4;  // RGBA = 4 bytes per pixel
                texture = textureLoader->loadTextureFromMemory(decodedData, textureSize, width, height, 4);
                
                // 释放解码后的数据
                stbi_image_free(decodedData);
            } else {
                Logger::error("Failed to decode embedded texture: " + std::string(stbi_failure_reason()));
            }
        } else {
            Logger::error("Embedded texture has no buffer view");
        }
    }
    
    if (texture) {
        textureCache[textureIndex] = texture;
        Logger::info("Texture loaded successfully (index: " + std::to_string(textureIndex) + ")");
    } else {
        Logger::warning("Failed to load texture (index: " + std::to_string(textureIndex) + ")");
    }
    
    return texture;
}

const unsigned char* GLTFModel::getBufferData(const tinygltf::Accessor& accessor) const {
    const auto& bufferView = gltfModel.bufferViews[accessor.bufferView];
    const auto& buffer = gltfModel.buffers[bufferView.buffer];
    
    return buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
}

void GLTFModel::calculateBoundingBox() {
    boundingBox = std::make_pair(glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX));
    
    // 遍历所有根节点
    for (size_t i = 0; i < nodes.size(); ++i) {
        bool isRoot = true;
        for (const auto& node : nodes) {
            if (std::find(node.children.begin(), node.children.end(), i) != node.children.end()) {
                isRoot = false;
                break;
            }
        }
        
        if (isRoot) {
            calculateNodeBoundingBox(i, glm::mat4(1.0f));
        }
    }
    
    Logger::info("Model bounding box: min(" + 
                std::to_string(boundingBox.first.x) + ", " + 
                std::to_string(boundingBox.first.y) + ", " + 
                std::to_string(boundingBox.first.z) + "), max(" +
                std::to_string(boundingBox.second.x) + ", " +
                std::to_string(boundingBox.second.y) + ", " +
                std::to_string(boundingBox.second.z) + ")");
}

void GLTFModel::calculateNodeBoundingBox(size_t nodeIndex, const glm::mat4& parentMatrix) {
    if (nodeIndex >= nodes.size()) {
        return;
    }
    
    const auto& node = nodes[nodeIndex];
    glm::mat4 nodeMatrix = parentMatrix * node.transform;
    
    // 如果节点有网格，更新包围盒
    if (node.meshIndex < meshes.size()) {
        const auto& meshData = meshes[node.meshIndex];
        
        // 转换包围盒顶点到世界空间
        glm::vec3 corners[8] = {
            glm::vec3(meshData.minBounds.x, meshData.minBounds.y, meshData.minBounds.z),
            glm::vec3(meshData.maxBounds.x, meshData.minBounds.y, meshData.minBounds.z),
            glm::vec3(meshData.minBounds.x, meshData.maxBounds.y, meshData.minBounds.z),
            glm::vec3(meshData.maxBounds.x, meshData.maxBounds.y, meshData.minBounds.z),
            glm::vec3(meshData.minBounds.x, meshData.minBounds.y, meshData.maxBounds.z),
            glm::vec3(meshData.maxBounds.x, meshData.minBounds.y, meshData.maxBounds.z),
            glm::vec3(meshData.minBounds.x, meshData.maxBounds.y, meshData.maxBounds.z),
            glm::vec3(meshData.maxBounds.x, meshData.maxBounds.y, meshData.maxBounds.z),
        };
        
        for (const auto& corner : corners) {
            glm::vec4 worldPos = nodeMatrix * glm::vec4(corner, 1.0f);
            glm::vec3 worldPos3 = glm::vec3(worldPos.x, worldPos.y, worldPos.z);
            
            boundingBox.first = glm::min(boundingBox.first, worldPos3);
            boundingBox.second = glm::max(boundingBox.second, worldPos3);
        }
    }
    
    // 递归处理子节点
    for (size_t childIndex : node.children) {
        calculateNodeBoundingBox(childIndex, nodeMatrix);
    }
}

std::shared_ptr<Texture> GLTFModel::getFirstTexture() const {
    // 返回第一个材质的 baseColor 纹理
    if (!materials.empty()) {
        const auto& material = materials[0];
        if (material.useBaseColorTexture) {
            return material.baseColorTexture;
        }
    }
    return nullptr;
}

// ==================== 动画系统实现 ====================

void GLTFModel::loadAnimations() {
    animations.clear();
    
    for (const auto& gltfAnimation : gltfModel.animations) {
        Animation animation;
        animation.name = gltfAnimation.name;
        
        // 加载采样器
        for (size_t s = 0; s < gltfAnimation.samplers.size(); ++s) {
            const auto& gltfSampler = gltfAnimation.samplers[s];
            AnimationSampler sampler;
            sampler.interpolation = gltfSampler.interpolation;
            
            // 调试：检查采样器索引
            Logger::info("  glTF Sampler: input=" + std::to_string(gltfSampler.input) + 
                        ", output=" + std::to_string(gltfSampler.output) + 
                        ", accessors count=" + std::to_string(gltfModel.accessors.size()));
            
            // 检查索引是否有效
            if (gltfSampler.input < 0 || gltfSampler.input >= static_cast<int>(gltfModel.accessors.size())) {
                Logger::warning("  Invalid input accessor index!");
                continue;
            }
            if (gltfSampler.output < 0 || gltfSampler.output >= static_cast<int>(gltfModel.accessors.size())) {
                Logger::warning("  Invalid output accessor index!");
                continue;
            }
            
            // 加载输入时间戳
            const auto& inputAccessor = gltfModel.accessors[gltfSampler.input];
            const auto* inputData = getBufferData(inputAccessor);
            sampler.inputs.reserve(inputAccessor.count);
            
            for (size_t i = 0; i < inputAccessor.count; ++i) {
                float time;
                if (inputAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                    time = reinterpret_cast<const float*>(inputData)[i];
                } else {
                    time = static_cast<float>(reinterpret_cast<const double*>(inputData)[i]);
                }
                sampler.inputs.push_back(time);
            }
            
            // 加载输出值
            const auto& outputAccessor = gltfModel.accessors[gltfSampler.output];
            const auto* outputData = getBufferData(outputAccessor);
            sampler.outputs.reserve(outputAccessor.count);
            
            for (size_t i = 0; i < outputAccessor.count; ++i) {
                glm::vec4 value(0.0f);
                if (outputAccessor.type == TINYGLTF_TYPE_VEC3) {
                    const float* v = reinterpret_cast<const float*>(outputData) + i * 3;
                    value = glm::vec4(v[0], v[1], v[2], 1.0f);
                } else if (outputAccessor.type == TINYGLTF_TYPE_VEC4) {
                    const float* v = reinterpret_cast<const float*>(outputData) + i * 4;
                    value = glm::vec4(v[0], v[1], v[2], v[3]);
                }
                sampler.outputs.push_back(value);
            }
            
            animation.samplers.push_back(std::move(sampler));
        }
        
        // 加载通道
        for (const auto& gltfChannel : gltfAnimation.channels) {
            AnimationChannel channel;
            channel.nodeIndex = gltfChannel.target_node;
            channel.samplerIndex = gltfChannel.sampler;
            
            // 解析目标路径
            if (gltfChannel.target_path == "translation") {
                channel.path = AnimationPath::Translation;
            } else if (gltfChannel.target_path == "rotation") {
                channel.path = AnimationPath::Rotation;
            } else if (gltfChannel.target_path == "scale") {
                channel.path = AnimationPath::Scale;
            } else if (gltfChannel.target_path == "weights") {
                channel.path = AnimationPath::Weights;
            }
            
            animation.channels.push_back(std::move(channel));
        }
        
        // 计算动画持续时间
        for (const auto& sampler : animation.samplers) {
            if (!sampler.inputs.empty()) {
                float maxTime = sampler.inputs.back();
                if (maxTime > animation.duration) {
                    animation.duration = maxTime;
                }
            }
        }
        
        animations.push_back(std::move(animation));
    }
    
    if (!animations.empty()) {
        Logger::info("Loaded " + std::to_string(animations.size()) + " animations");
        for (size_t i = 0; i < animations.size(); ++i) {
            Logger::info("  Animation[" + std::to_string(i) + "]: " + 
                        (animations[i].name.empty() ? "<unnamed>" : animations[i].name) +
                        " (duration: " + std::to_string(animations[i].duration) + "s, " +
                        std::to_string(animations[i].channels.size()) + " channels)");
        }
    }
}

void GLTFModel::loadSkins() {
    skins.clear();
    
    for (const auto& gltfSkin : gltfModel.skins) {
        Skin skin;
        skin.name = gltfSkin.name;
        skin.skeletonRoot = gltfSkin.skeleton;
        
        // 加载关节
        for (size_t i = 0; i < gltfSkin.joints.size(); ++i) {
            Joint joint;
            joint.nodeIndex = gltfSkin.joints[i];
            
            // 获取逆绑定矩阵
            if (gltfSkin.inverseBindMatrices >= 0) {
                const auto& accessor = gltfModel.accessors[gltfSkin.inverseBindMatrices];
                const auto* data = getBufferData(accessor);
                const float* matrixData = reinterpret_cast<const float*>(data) + i * 16;
                joint.inverseBindMatrix = glm::mat4(
                    matrixData[0], matrixData[1], matrixData[2], matrixData[3],
                    matrixData[4], matrixData[5], matrixData[6], matrixData[7],
                    matrixData[8], matrixData[9], matrixData[10], matrixData[11],
                    matrixData[12], matrixData[13], matrixData[14], matrixData[15]
                );
            } else {
                joint.inverseBindMatrix = glm::mat4(1.0f);
            }
            
            skin.joints.push_back(std::move(joint));
            
            // 更新节点的关节索引
            if (joint.nodeIndex >= 0 && joint.nodeIndex < static_cast<int>(nodes.size())) {
                nodes[joint.nodeIndex].jointIndex = static_cast<int>(i);
                nodes[joint.nodeIndex].inverseBindMatrix = joint.inverseBindMatrix;
            }
        }
        
        skins.push_back(std::move(skin));
    }
    
    if (!skins.empty()) {
        Logger::info("Loaded " + std::to_string(skins.size()) + " skins");
    }
}

void GLTFModel::storeNodeTransforms() {
    nodeTransforms.resize(nodes.size());
    
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        
        // 从变换矩阵分解 TRS
        glm::vec3 scale, translation, skew;
        glm::quat rotation;
        glm::vec4 perspective;
        if (glm::decompose(node.transform, scale, rotation, translation, skew, perspective)) {
            nodeTransforms[i].translation = translation;
            nodeTransforms[i].rotation = rotation;
            nodeTransforms[i].scale = scale;
            nodeTransforms[i].hasTranslation = true;
            nodeTransforms[i].hasRotation = true;
            nodeTransforms[i].hasScale = true;
        }
    }
}

const std::string& GLTFModel::getAnimationName(size_t index) const {
    static const std::string empty;
    if (index < animations.size()) {
        return animations[index].name;
    }
    return empty;
}

void GLTFModel::playAnimation(size_t index, bool loop, float speed) {
    if (index < animations.size()) {
        animationState.animationIndex = static_cast<int>(index);
        animationState.currentTime = 0.0f;
        animationState.playing = true;
        animationState.loop = loop;
        animationState.speed = speed;
        
        Logger::info("Playing animation: " + 
                    (animations[index].name.empty() ? std::to_string(index) : animations[index].name));
    }
}

bool GLTFModel::playAnimation(const std::string& name, bool loop, float speed) {
    for (size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].name == name) {
            playAnimation(i, loop, speed);
            return true;
        }
    }
    Logger::warning("Animation not found: " + name);
    return false;
}

void GLTFModel::stopAnimation() {
    animationState.playing = false;
    animationState.currentTime = 0.0f;
    playAllAnimationsMode = false;
    
    // 恢复节点原始变换
    for (size_t i = 0; i < nodes.size() && i < nodeTransforms.size(); ++i) {
        const auto& transform = nodeTransforms[i];
        glm::mat4 T = glm::translate(glm::mat4(1.0f), transform.translation);
        glm::mat4 R = glm::mat4_cast(transform.rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), transform.scale);
        nodes[i].transform = T * R * S;
    }
}

void GLTFModel::playAllAnimations(bool loop, float speed) {
    if (animations.empty()) return;
    
    playAllAnimationsMode = true;
    animationState.playing = true;
    animationState.currentTime = 0.0f;
    animationState.loop = loop;
    animationState.speed = speed;
    animationState.animationIndex = 0;  // 用于时间同步
    
    Logger::info("Playing all " + std::to_string(animations.size()) + " animations simultaneously");
}

void GLTFModel::pauseAnimation() {
    animationState.playing = false;
}

void GLTFModel::resumeAnimation() {
    if (animationState.animationIndex >= 0) {
        animationState.playing = true;
    }
}

void GLTFModel::updateAnimation(float deltaTime) {
    if (!animationState.playing) {
        return;
    }
    
    // 更新时间
    animationState.currentTime += deltaTime * animationState.speed;
    
    // 如果是播放所有动画模式
    if (playAllAnimationsMode) {
        // 找到最长动画时长
        float maxDuration = 0.0f;
        for (const auto& anim : animations) {
            if (anim.duration > maxDuration) {
                maxDuration = anim.duration;
            }
        }
        
        // 检查是否播放完成
        if (animationState.currentTime >= maxDuration) {
            if (animationState.loop) {
                animationState.currentTime = std::fmod(animationState.currentTime, maxDuration);
            } else {
                animationState.currentTime = maxDuration;
                animationState.playing = false;
            }
        }
        
        // 应用所有动画
        for (size_t i = 0; i < animations.size(); ++i) {
            // 对每个动画使用模运算使其循环
            float animTime = animationState.currentTime;
            if (animations[i].duration > 0.0f) {
                animTime = std::fmod(animTime, animations[i].duration);
            }
            applyAnimation(static_cast<int>(i), animTime);
        }
        return;
    }
    
    // 单个动画模式
    if (animationState.animationIndex < 0 ||
        animationState.animationIndex >= static_cast<int>(animations.size())) {
        return;
    }
    
    const auto& animation = animations[animationState.animationIndex];
    
    // 检查是否播放完成
    if (animationState.currentTime >= animation.duration) {
        if (animationState.loop) {
            animationState.currentTime = std::fmod(animationState.currentTime, animation.duration);
        } else {
            animationState.currentTime = animation.duration;
            animationState.playing = false;
        }
    }
    
    // 应用动画
    applyAnimation(animationState.animationIndex, animationState.currentTime);
}

std::pair<size_t, float> GLTFModel::findKeyframeIndex(const std::vector<float>& times, float time) const {
    if (times.empty()) {
        return {0, 0.0f};
    }
    
    // 找到当前时间所在的关键帧区间
    size_t index = 0;
    for (size_t i = 0; i < times.size() - 1; ++i) {
        if (time >= times[i] && time < times[i + 1]) {
            index = i;
            break;
        }
        if (i == times.size() - 2) {
            index = i;
        }
    }
    
    // 计算插值因子
    float factor = 0.0f;
    if (index < times.size() - 1) {
        float startTime = times[index];
        float endTime = times[index + 1];
        float duration = endTime - startTime;
        if (duration > 0.0f) {
            factor = (time - startTime) / duration;
        }
    }
    
    return {index, std::clamp(factor, 0.0f, 1.0f)};
}

glm::vec4 GLTFModel::sampleAnimationChannel(const AnimationSampler& sampler, float time) const {
    if (sampler.inputs.empty() || sampler.outputs.empty()) {
        return glm::vec4(0.0f);
    }
    
    auto [index, factor] = findKeyframeIndex(sampler.inputs, time);
    
    if (sampler.interpolation == "STEP") {
        return sampler.outputs[index];
    }
    
    // LINEAR 插值
    if (index < sampler.outputs.size() - 1) {
        const glm::vec4& v0 = sampler.outputs[index];
        const glm::vec4& v1 = sampler.outputs[index + 1];
        
        // 对于旋转使用球面线性插值
        if (sampler.interpolation == "LINEAR") {
            return glm::mix(v0, v1, factor);
        }
    }
    
    return sampler.outputs[index];
}

void GLTFModel::applyAnimation(int animationIndex, float time) {
    if (animationIndex < 0 || animationIndex >= static_cast<int>(animations.size())) {
        return;
    }
    
    const auto& animation = animations[animationIndex];
    
    for (const auto& channel : animation.channels) {
        if (channel.nodeIndex < 0 || channel.nodeIndex >= static_cast<int>(nodes.size())) {
            continue;
        }
        
        const auto& sampler = animation.samplers[channel.samplerIndex];
        glm::vec4 value = sampleAnimationChannel(sampler, time);
        
        auto& node = nodes[channel.nodeIndex];
        auto& transform = nodeTransforms[channel.nodeIndex];
        
        // 应用通道值到节点变换
        switch (channel.path) {
            case AnimationPath::Translation: {
                transform.translation = glm::vec3(value.x, value.y, value.z);
                transform.hasTranslation = true;
                break;
            }
            case AnimationPath::Rotation: {
                // glTF 四元数格式: (x, y, z, w)，GLM 格式: (w, x, y, z)
                transform.rotation = glm::quat(value.w, value.x, value.y, value.z);
                transform.hasRotation = true;
                break;
            }
            case AnimationPath::Scale: {
                transform.scale = glm::vec3(value.x, value.y, value.z);
                transform.hasScale = true;
                break;
            }
            case AnimationPath::Weights:
                // 权重动画暂不支持
                break;
        }
        
        // 重建变换矩阵
        glm::mat4 T = glm::translate(glm::mat4(1.0f), transform.translation);
        glm::mat4 R = glm::mat4_cast(transform.rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), transform.scale);
        node.transform = T * R * S;
    }
}

} // namespace vgame
