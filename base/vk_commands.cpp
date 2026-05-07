#include "vk_commands.h"
#include "VulkanTools.h"

namespace vkutil {
    void immediateSubmit(const ImmediateSubmitContext& context, std::function<void(VkCommandBuffer cmd)>&& function) {
        VkCommandBuffer cmd{ VK_NULL_HANDLE };

        // 为一次短任务临时分配 primary command buffer。
        VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocateInfo.commandPool = context.commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(context.device, &allocateInfo, &cmd));

        // 告诉驱动这份命令只会提交一次，适合初始化上传、布局转换这类临时命令。
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

        // 这里刻意同步等待，保证函数返回后 staging buffer 等临时资源可以安全销毁。
        VK_CHECK_RESULT(vkQueueSubmit(context.queue, 1, &submitInfo, fence));
        VK_CHECK_RESULT(vkWaitForFences(context.device, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

        vkDestroyFence(context.device, fence, nullptr);
        vkFreeCommandBuffers(context.device, context.commandPool, 1, &cmd);
    }
} // namespace vkutil
