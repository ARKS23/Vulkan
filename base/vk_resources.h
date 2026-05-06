#pragma once

#include "vk_commands.h"
#include "vk_types.h"

namespace vkutil {
    AllocatedBuffer createAllocatedBuffer(
        VmaAllocator allocator,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VmaAllocationCreateFlags allocationFlags,
        VmaMemoryUsage memoryUsage
    );

    void destroyAllocatedBuffer(
        VmaAllocator allocator,
        AllocatedBuffer& buffer
    );

    AllocatedBuffer createDeviceLocalBuffer(
        VmaAllocator allocator, 
        const ImmediateSubmitContext& submitContext, 
        const void* data, 
        VkDeviceSize size, 
        VkBufferUsageFlags usage
    ); // 封装CPU上传数据到GPU
} // namespace vkutil
