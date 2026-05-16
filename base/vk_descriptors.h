#pragma once
#include "vk_types.h"

// 描述符封装
namespace vkutil {
    VkDescriptorPoolSize descriptorPoolSize(VkDescriptorType type, uint32_t descriptorCount);

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo(const std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets);

    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding(
        VkDescriptorType type,
        VkShaderStageFlags stageFlags,
        uint32_t binding,
        uint32_t descriptorCount = 1);

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo(const std::vector<VkDescriptorSetLayoutBinding>& bindings);

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo(
		VkDescriptorPool descriptorPool,
        const VkDescriptorSetLayout* pSetLayouts,
        uint32_t descriptorSetCount);

    VkDescriptorBufferInfo descriptorBufferInfo(
        VkBuffer buffer,
        VkDeviceSize range,
        VkDeviceSize offset = 0);

    VkDescriptorImageInfo descriptorImageInfo(
        VkSampler sampler,
        VkImageView imageView,
        VkImageLayout imageLayout);

    VkWriteDescriptorSet writeBufferDescriptorSet(
        VkDescriptorSet dstSet,
        VkDescriptorType type,
        uint32_t binding,
        const VkDescriptorBufferInfo* bufferInfo,
        uint32_t arrayElement = 0,
        uint32_t descriptorCount = 1);

    VkWriteDescriptorSet writeImageDescriptorSet(
        VkDescriptorSet dstSet,
        VkDescriptorType type,
        uint32_t binding,
        const VkDescriptorImageInfo* imageInfo,
        uint32_t arrayElement = 0,
        uint32_t descriptorCount = 1);

    // 复用上两个函数，减少参数量
    VkWriteDescriptorSet writeUniformBuffer(VkDescriptorSet dstSet, uint32_t binding, const VkDescriptorBufferInfo* bufferInfo);
    VkWriteDescriptorSet writeCombinedImageSampler(VkDescriptorSet dstSet, uint32_t binding, const VkDescriptorImageInfo* imageInfo);

    // 封装原生update
    void updateDescriptorSet(VkDevice device, const std::vector<VkWriteDescriptorSet>& writes);
} // namespace vkutil

