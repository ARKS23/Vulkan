#pragma once

#include <functional>
#include <vulkan/vulkan.h>

namespace vkutil {
    // 一次性命令提交所需的最小上下文。
    // commandPool 必须来自 queue 所属的 queue family，否则提交/释放命令会出问题。
    struct ImmediateSubmitContext {
        VkDevice device {VK_NULL_HANDLE};
        VkQueue queue {VK_NULL_HANDLE};
        VkCommandPool commandPool{ VK_NULL_HANDLE };
    };

    // 录制一个一次性 command buffer，提交后同步等待完成，并立即释放。
    // 适合初始化阶段的 copy、layout transition 等短任务；不要放进每帧主渲染循环里频繁调用。
    void immediateSubmit(const ImmediateSubmitContext& context, std::function<void(VkCommandBuffer cmd)>&& function);
}
