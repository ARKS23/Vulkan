#include "vk_initializers.h"

namespace vkinit {
    VkFenceCreateInfo fenceCreateInfo(VkFenceCreateFlags flags) {
        VkFenceCreateInfo fenceCI{};
        fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCI.pNext = nullptr;
        fenceCI.flags = flags;
        return fenceCI;
    }
    
    VkSemaphoreCreateInfo semaphoreCreateInfo(VkSemaphoreCreateFlags flags) {
        VkSemaphoreCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        info.pNext = nullptr;
        info.flags = flags;
        return info;
    }
    
    VkCommandPoolCreateInfo commandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags) {
        VkCommandPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.pNext = nullptr;
        info.queueFamilyIndex = queueFamilyIndex;
        info.flags = flags;
        return info;
    }
    
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo() {
        VkPipelineLayoutCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.pNext = nullptr;

        info.flags = 0;
        info.setLayoutCount = 0;
        info.pSetLayouts = nullptr;
        info.pushConstantRangeCount = 0;
        info.pPushConstantRanges = nullptr;
        
        return info;
    }
}