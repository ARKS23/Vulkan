#pragma once

#include "vk_commands.h"
#include "vk_types.h"

namespace vkutil {
    // 创建一个由 VMA 管理内存的 buffer，并返回句柄与分配信息。
    // 如果需要 CPU 持久映射写入，可以在 allocationFlags 中传入 VMA_ALLOCATION_CREATE_MAPPED_BIT。
    AllocatedBuffer createAllocatedBuffer(
        VmaAllocator allocator,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VmaAllocationCreateFlags allocationFlags,
        VmaMemoryUsage memoryUsage
    );

    // 销毁 createAllocatedBuffer 创建的 buffer，并把包装结构清空，避免悬空句柄被重复使用。
    void destroyAllocatedBuffer(
        VmaAllocator allocator,
        AllocatedBuffer& buffer
    );

    // 通过临时 staging buffer 把 CPU 数据上传到 device-local buffer。
    // usage 描述最终 GPU 用途，例如 VERTEX_BUFFER 或 INDEX_BUFFER，函数内部会自动补上 TRANSFER_DST。
    AllocatedBuffer createDeviceLocalBuffer(
        VmaAllocator allocator,
        const ImmediateSubmitContext& submitContext,
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags usage
    );
} // namespace vkutil
