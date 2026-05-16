#include "vk_descriptors.h"

namespace vkutil {
    VkDescriptorPoolSize descriptorPoolSize(VkDescriptorType type, uint32_t descriptorCount) {
        VkDescriptorPoolSize descriptorPoolSize {};
        descriptorPoolSize.type = type;
        descriptorPoolSize.descriptorCount = descriptorCount;
        return descriptorPoolSize;
    }

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo(const std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets) {
        VkDescriptorPoolCreateInfo descriptorPoolInfo{};
        descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        descriptorPoolInfo.pPoolSizes = poolSizes.data();
        descriptorPoolInfo.maxSets = maxSets;
        return descriptorPoolInfo;
    }

    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding(VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t binding, uint32_t descriptorCount) {
        VkDescriptorSetLayoutBinding setLayoutBinding {};
        setLayoutBinding.descriptorType = type;
        setLayoutBinding.stageFlags = stageFlags;
        setLayoutBinding.binding = binding;
        setLayoutBinding.descriptorCount = descriptorCount;
        return setLayoutBinding;
    }

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo(const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
        descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCreateInfo.pBindings = bindings.data();
        descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        return descriptorSetLayoutCreateInfo;
    }

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo(VkDescriptorPool descriptorPool, const VkDescriptorSetLayout* pSetLayouts, uint32_t descriptorSetCount) {
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo {};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.pSetLayouts = pSetLayouts;
        descriptorSetAllocateInfo.descriptorSetCount = descriptorSetCount;
        return descriptorSetAllocateInfo;
    }

    VkDescriptorBufferInfo descriptorBufferInfo(VkBuffer buffer, VkDeviceSize range, VkDeviceSize offset) {
        VkDescriptorBufferInfo info {};
        info.buffer = buffer;
        info.offset = offset;
        info.range = range;
        return info;
    }

    VkDescriptorImageInfo descriptorImageInfo(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout) {
        VkDescriptorImageInfo info {};
        info.sampler = sampler;
        info.imageView = imageView;
        info.imageLayout = imageLayout;
        return info;
    }

    VkWriteDescriptorSet writeBufferDescriptorSet(VkDescriptorSet dstSet, VkDescriptorType type, uint32_t binding,
        const VkDescriptorBufferInfo* bufferInfo, uint32_t arrayElement, uint32_t descriptorCount) {
        VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = dstSet;
        write.descriptorType = type;
        write.dstBinding = binding;
        write.pBufferInfo = bufferInfo;

        // 默认参数
        write.dstArrayElement = arrayElement;
        write.descriptorCount = descriptorCount;
        return write;
    }

    VkWriteDescriptorSet writeImageDescriptorSet(VkDescriptorSet dstSet, VkDescriptorType type, uint32_t binding,
        const VkDescriptorImageInfo* imageInfo, uint32_t arrayElement, uint32_t descriptorCount) {
        VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = dstSet;
        write.descriptorType = type;
        write.dstBinding = binding;
        write.pImageInfo = imageInfo;

        // 默认参数
        write.dstArrayElement = arrayElement;
        write.descriptorCount = descriptorCount;
        return write;
    }

    VkWriteDescriptorSet writeUniformBuffer(VkDescriptorSet dstSet, uint32_t binding, const VkDescriptorBufferInfo* bufferInfo) {
        return writeBufferDescriptorSet(dstSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, binding, bufferInfo);
    }

    VkWriteDescriptorSet writeCombinedImageSampler(VkDescriptorSet dstSet, uint32_t binding, const VkDescriptorImageInfo* imageInfo) {
        return writeImageDescriptorSet(dstSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, binding, imageInfo);
    }

    void updateDescriptorSet(VkDevice device, const std::vector<VkWriteDescriptorSet>& writes) {
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}