#include "renderer/fsr1_pass.h"
#include "utils/logger.h"
#include <fstream>
#include <cstring>

namespace owengine {
namespace {
std::vector<char> readFile_(const std::string& p) {
    std::ifstream f(p, std::ios::ate | std::ios::binary);
    if (!f.is_open()) return {};
    size_t sz = static_cast<size_t>(f.tellg());
    std::vector<char> b(sz); f.seekg(0); f.read(b.data(), sz); return b;
}
VkShaderModule createMod_(VkDevice d, const std::vector<char>& c) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = c.size(); ci.pCode = reinterpret_cast<const uint32_t*>(c.data());
    VkShaderModule m; vkCreateShaderModule(d, &ci, nullptr, &m); return m;
}
void easuCon(uint32_t con[16], float inW, float inH, float outW, float outH) {
    float rx = outW / inW, ry = outH / inH;
    con[0] = (uint32_t)(inW*0.5f); con[1] = (uint32_t)(inH*0.5f);
    memcpy(con+2, &inW, 4); memcpy(con+3, &inH, 4);
    float rr = 1.0f/rx; memcpy(con+4, &rr, 4); rr = 1.0f/ry; memcpy(con+5, &rr, 4);
    memcpy(con+6, &rx, 4); memcpy(con+7, &ry, 4);
    float z = 0; memcpy(con+8, &z, 4); memcpy(con+9, &z, 4);
    float s = 1.0f; memcpy(con+10, &s, 4); memcpy(con+11, &s, 4);
    con[12] = (uint32_t)outW; con[13] = (uint32_t)outH; con[14] = 0; con[15] = 0;
}
}

Fsr1Pass::Fsr1Pass(std::shared_ptr<VulkanDevice> dev, VkFormat colorFormat, VkExtent2D outExt, float sc)
    : device_(std::move(dev)), colorFormat_(colorFormat), outputExtent_(outExt), scale_(sc) {
    renderExtent_ = {std::max(1u, (uint32_t)(outExt.width*sc)), std::max(1u, (uint32_t)(outExt.height*sc))};
}
Fsr1Pass::~Fsr1Pass() { cleanup(); }

void Fsr1Pass::init() {
    if (initialized_) return;
    createRenderTarget();
    createDescriptorResources();
    createEasuPipeline();
    easuCon(constData_, (float)renderExtent_.width, (float)renderExtent_.height,
            (float)outputExtent_.width, (float)outputExtent_.height);
    void* d; vkMapMemory(device_->getDevice(), constBufferMemory_, 0, sizeof(constData_), 0, &d);
    memcpy(d, constData_, sizeof(constData_)); vkUnmapMemory(device_->getDevice(), constBufferMemory_);
    Logger::info("[Fsr1Pass] OK");
    initialized_ = true;
}

void Fsr1Pass::createRenderTarget() {
    VkDevice dev = device_->getDevice();
    VkImageCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = colorFormat_;
    ci.extent = {renderExtent_.width, renderExtent_.height, 1};
    ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(dev, &ci, nullptr, &colorImage_);
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, colorImage_, &mr);
    VkMemoryAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = device_->findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(dev, &ai, nullptr, &colorMemory_); vkBindImageMemory(dev, colorImage_, colorMemory_, 0);
    VkImageViewCreateInfo iv{}; iv.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image = colorImage_; iv.viewType = VK_IMAGE_VIEW_TYPE_2D; iv.format = colorFormat_;
    iv.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCreateImageView(dev, &iv, nullptr, &colorView_);
}

void Fsr1Pass::createDescriptorResources() {
    VkDevice dev = device_->getDevice();
    // Layout: 0=sampled, 1=sampler, 2=storage(out), 3=uniform
    VkDescriptorSetLayoutBinding b[4] = {};
    b[0] = {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT};
    b[1] = {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT};
    b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT};
    b[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo lc{}; lc.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; lc.bindingCount=4; lc.pBindings=b;
    vkCreateDescriptorSetLayout(dev, &lc, nullptr, &descSetLayout_);

    VkDescriptorPoolSize ps[4] = {};
    ps[0] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_FRAMES};
    ps[1] = {VK_DESCRIPTOR_TYPE_SAMPLER, MAX_FRAMES};
    ps[2] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_FRAMES};
    ps[3] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES};
    VkDescriptorPoolCreateInfo pc{}; pc.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pc.maxSets = MAX_FRAMES; pc.poolSizeCount=4; pc.pPoolSizes=ps;
    vkCreateDescriptorPool(dev, &pc, nullptr, &descPool_);

    VkDescriptorSetAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool=descPool_; ai.descriptorSetCount=MAX_FRAMES;
    VkDescriptorSetLayout layouts[MAX_FRAMES] = {descSetLayout_, descSetLayout_};
    ai.pSetLayouts = layouts;
    vkAllocateDescriptorSets(dev, &ai, descSets_);

    VkSamplerCreateInfo sc{}; sc.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sc.magFilter=VK_FILTER_LINEAR; sc.minFilter=VK_FILTER_LINEAR;
    sc.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sc.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sc.maxLod=1.0f;
    vkCreateSampler(dev, &sc, nullptr, &sampler_);

    VkBufferCreateInfo bci{}; bci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size=sizeof(constData_); bci.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    vkCreateBuffer(dev, &bci, nullptr, &constBuffer_);
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, constBuffer_, &mr);
    VkMemoryAllocateInfo bai{}; bai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bai.allocationSize=mr.size; bai.memoryTypeIndex=device_->findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(dev, &bai, nullptr, &constBufferMemory_); vkBindBufferMemory(dev, constBuffer_, constBufferMemory_, 0);

    // 初始化所有 desc set 的静态绑定（sampled=0, sampler=1, uniform=3）
    for (int f = 0; f < MAX_FRAMES; f++) {
        VkDescriptorImageInfo si{}; si.imageView=colorView_; si.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo sampInfo{}; sampInfo.sampler=sampler_;
        VkDescriptorBufferInfo cbi{}; cbi.buffer=constBuffer_; cbi.range=sizeof(constData_);
        VkWriteDescriptorSet w[3]={{},{},{}};
        w[0].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[0].dstSet=descSets_[f]; w[0].dstBinding=0;
        w[0].descriptorCount=1; w[0].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w[0].pImageInfo=&si;
        w[1].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[1].dstSet=descSets_[f]; w[1].dstBinding=1;
        w[1].descriptorCount=1; w[1].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER; w[1].pImageInfo=&sampInfo;
        w[2].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[2].dstSet=descSets_[f]; w[2].dstBinding=3;
        w[2].descriptorCount=1; w[2].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w[2].pBufferInfo=&cbi;
        vkUpdateDescriptorSets(dev, 3, w, 0, nullptr);
    }
}

void Fsr1Pass::createEasuPipeline() {
    VkDevice dev = device_->getDevice();
    auto code = readFile_("shaders/fsr1_easu.comp.spv");
    if (code.empty()) { Logger::error("[Fsr1Pass] shader not found"); return; }
    VkShaderModule mod = createMod_(dev, code);
    if (!mod) return;
    VkPipelineShaderStageCreateInfo s{}; s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    s.stage=VK_SHADER_STAGE_COMPUTE_BIT; s.module=mod; s.pName="main";
    VkPushConstantRange p{}; p.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; p.size=8;
    VkPipelineLayoutCreateInfo lc{}; lc.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lc.setLayoutCount=1; lc.pSetLayouts=&descSetLayout_; lc.pushConstantRangeCount=1; lc.pPushConstantRanges=&p;
    vkCreatePipelineLayout(dev, &lc, nullptr, &easuLayout_);
    VkComputePipelineCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage=s; ci.layout=easuLayout_;
    vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &ci, nullptr, &easuPipeline_);
    vkDestroyShaderModule(dev, mod, nullptr);
}

void Fsr1Pass::dispatch(VkCommandBuffer cmd) {
    if (!initialized_) return;
    // 过渡 colorImage_ 到 SHADER_READ_ONLY（用 UNDEFINED 兼容任何 render pass 最终布局）
    VkImageMemoryBarrier b{}; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.image=colorImage_; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,0,nullptr,0,nullptr,1,&b);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, easuPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, easuLayout_, 0, 1, &descSets_[currentFrameIndex_], 0, nullptr);
    uint32_t push[2] = {outputExtent_.width, outputExtent_.height};
    vkCmdPushConstants(cmd, easuLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, push);
    vkCmdDispatch(cmd, (outputExtent_.width+31)/32, (outputExtent_.height+7)/8, 1);
    // 恢复: SHADER_READ_ONLY → COLOR_ATTACHMENT_OPTIMAL（render pass 期望）
    b.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; b.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT; b.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,0,nullptr,0,nullptr,1,&b);
}

void Fsr1Pass::updateOutputDescriptor(VkImageView swapchainImgView) {
    if (!initialized_ || swapchainImgView == VK_NULL_HANDLE) return;
    VkDescriptorImageInfo outInfo{}; outInfo.imageView=swapchainImgView; outInfo.imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet w{}; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet=descSets_[currentFrameIndex_]; w.dstBinding=2; w.descriptorCount=1;
    w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w.pImageInfo=&outInfo;
    vkUpdateDescriptorSets(device_->getDevice(), 1, &w, 0, nullptr);
}

void Fsr1Pass::cleanup() {
    if (!initialized_) return;
    VkDevice dev = device_->getDevice();
    if (easuPipeline_) vkDestroyPipeline(dev, easuPipeline_, nullptr);
    if (easuLayout_) vkDestroyPipelineLayout(dev, easuLayout_, nullptr);
    if (descSetLayout_) vkDestroyDescriptorSetLayout(dev, descSetLayout_, nullptr);
    if (sampler_) vkDestroySampler(dev, sampler_, nullptr);
    if (colorView_) vkDestroyImageView(dev, colorView_, nullptr);
    if (colorImage_) vkDestroyImage(dev, colorImage_, nullptr);
    if (colorMemory_) vkFreeMemory(dev, colorMemory_, nullptr);
    if (constBuffer_) vkDestroyBuffer(dev, constBuffer_, nullptr);
    if (constBufferMemory_) vkFreeMemory(dev, constBufferMemory_, nullptr);
    if (descPool_) vkDestroyDescriptorPool(dev, descPool_, nullptr);
    initialized_ = false;
}
} // namespace owengine
