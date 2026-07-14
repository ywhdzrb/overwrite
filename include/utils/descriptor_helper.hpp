#pragma once

/**
 * @file descriptor_helper.hpp
 * @brief 描述符集公共辅助函数 — 消除各处重复的 Allocate+Write 模式
 *
 * 归属模块：utils
 * 核心职责：提供创建 COMBINED_IMAGE_SAMPLER 描述符集的统一入口
 * 使用方式：descriptor_helper::createTextureDescriptorSet(device, pool, layout, texture, name)
 */

#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include "utils/logger.hpp"
#include "renderer/texture.hpp"

namespace owengine::descriptor_helper {

/**
 * @brief 创建纹理描述符集（单纹理写入 binding=0 和 binding=1）
 * @param device Vulkan 设备句柄
 * @param pool 描述符池
 * @param layout 描述符集布局
 * @param texture 纹理对象（含 imageView 和 sampler）
 * @param debugName 调试用名称（仅日志）
 * @return 分配并写入后的 VkDescriptorSet，失败返回 VK_NULL_HANDLE
 */
// 内部：分配描述符集，返回 imageInfo 数组供调用方填充
[[nodiscard]] inline VkDescriptorSet allocateDescriptorSet(
    VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout,
    const std::string& debugName) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        Logger::warning("[DescriptorHelper] 分配描述符集失败: " + debugName);
        return VK_NULL_HANDLE;
    }
    return descriptorSet;
}

// 内部：用指定纹理信息写入描述符集的两个 binding
inline void writeDescriptorSetTwoBindings(VkDevice device, VkDescriptorSet set,
                                           const VkDescriptorImageInfo& info0,
                                           const VkDescriptorImageInfo& info1) {
    VkWriteDescriptorSet descWrites[2]{};
    descWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrites[0].dstSet = set;
    descWrites[0].dstBinding = 0;
    descWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descWrites[0].descriptorCount = 1;
    descWrites[0].pImageInfo = &info0;

    descWrites[1] = descWrites[0];
    descWrites[1].dstBinding = 1;
    descWrites[1].pImageInfo = &info1;

    vkUpdateDescriptorSets(device, 2, descWrites, 0, nullptr);
}

/**
 * @brief 创建纹理描述符集（双 binding 绑定不同纹理）
 * @param device Vulkan 设备句柄
 * @param pool 描述符池
 * @param layout 描述符集布局
 * @param texture0 binding=0 的纹理（草地/默认）
 * @param texture1 binding=1 的纹理（海底/第二纹理），为空时复用 texture0
 * @param debugName 调试用名称（仅日志）
 * @return 分配并写入后的 VkDescriptorSet，失败返回 VK_NULL_HANDLE
 */
[[nodiscard]] inline VkDescriptorSet createTextureDescriptorSet(
    VkDevice device,
    VkDescriptorPool pool,
    VkDescriptorSetLayout layout,
    std::shared_ptr<Texture> texture0,
    std::shared_ptr<Texture> texture1,
    const std::string& debugName = "texture") {

    auto tex = texture0 ? texture0 : texture1;
    if (!tex) {
        Logger::warning("[DescriptorHelper] 纹理均为空: " + debugName);
        return VK_NULL_HANDLE;
    }

    VkDescriptorSet ds = allocateDescriptorSet(device, pool, layout, debugName);
    if (ds == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    auto makeImg = [](std::shared_ptr<Texture> t, std::shared_ptr<Texture> fallback) -> VkDescriptorImageInfo {
        auto src = t ? t : fallback;
        VkDescriptorImageInfo info{};
        info.sampler = src->getSampler();
        info.imageView = src->getImageView();
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return info;
    };

    VkDescriptorImageInfo img0 = makeImg(texture0, tex);
    VkDescriptorImageInfo img1 = makeImg(texture1, tex);

    writeDescriptorSetTwoBindings(device, ds, img0, img1);
    return ds;
}

/**
 * @brief 创建纹理描述符集（单纹理写入两个 binding）
 */
[[nodiscard]] inline VkDescriptorSet createTextureDescriptorSet(
    VkDevice device,
    VkDescriptorPool pool,
    VkDescriptorSetLayout layout,
    std::shared_ptr<Texture> texture,
    const std::string& debugName = "texture") {
    return createTextureDescriptorSet(device, pool, layout, texture, texture, debugName);
}

} // namespace owengine::descriptor_helper
