#pragma once

#include <functional>
#include <vulkan/vulkan.h>

namespace vkutil {
    struct ImmediateSubmitContext {
        VkDevice device {VK_NULL_HANDLE};
        VkQueue queue {VK_NULL_HANDLE};
        VkCommandPool commandPool{ VK_NULL_HANDLE };
    };

    void immediateSubmit(const ImmediateSubmitContext& context, std::function<void(VkCommandBuffer cmd)>&& function);
}