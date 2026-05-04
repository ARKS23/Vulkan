#pragma once

#include "vk_types.h"

namespace vkinit {
    VkFenceCreateInfo fenceCreateInfo(VkFenceCreateFlags flags = 0);
    VkSemaphoreCreateInfo semaphoreCreateInfo(VkSemaphoreCreateFlags flags = 0);

    VkCommandPoolCreateInfo commandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo();
} // namespace vkinit
