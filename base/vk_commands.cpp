#include "vk_commands.h"
#include "VulkanTools.h"

namespace vkutil {
    void immediateSubmit(const ImmediateSubmitContext& context, std::function<void(VkCommandBuffer cmd)>&& function) {
        VkCommandBuffer cmd{ VK_NULL_HANDLE };
        VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocateInfo.commandPool = context.commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(context.device, &allocateInfo, &cmd));

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK_RESULT(vkBeginCommandBuffer(cmd, &beginInfo));
        function(cmd);
        VK_CHECK_RESULT(vkEndCommandBuffer(cmd));

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence fence{ VK_NULL_HANDLE };
        VK_CHECK_RESULT(vkCreateFence(context.device, &fenceInfo, nullptr, &fence));

        VK_CHECK_RESULT(vkQueueSubmit(context.queue, 1, &submitInfo, fence));
        VK_CHECK_RESULT(vkWaitForFences(context.device, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

        vkDestroyFence(context.device, fence, nullptr);
        vkFreeCommandBuffers(context.device, context.commandPool, 1, &cmd);
    }
} // namespace vkutil
