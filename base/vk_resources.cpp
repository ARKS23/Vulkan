#include "vk_resources.h"
#include "VulkanTools.h"

#include <cstring>

namespace vkutil {
    AllocatedBuffer createAllocatedBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags allocationFlags, VmaMemoryUsage memoryUsage) {
        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = memoryUsage;
        allocationInfo.flags = allocationFlags;

        AllocatedBuffer buffer{};
        buffer.size = size;
        VK_CHECK_RESULT(vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &buffer.handle, &buffer.allocation, &buffer.allocationInfo));
        return buffer;
    }

    void destroyAllocatedBuffer( VmaAllocator allocator, AllocatedBuffer& buffer) {
        if (buffer.handle != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
            buffer.handle = VK_NULL_HANDLE;
            buffer.allocation = VK_NULL_HANDLE;
            buffer.allocationInfo = {};
            buffer.size = 0;
        }
    }

    AllocatedBuffer createDeviceLocalBuffer(VmaAllocator allocator, const ImmediateSubmitContext& submitContext, const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
        AllocatedBuffer stagingBuffer = createAllocatedBuffer(allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO);

        std::memcpy(stagingBuffer.allocationInfo.pMappedData, data, static_cast<size_t>(size));
        vmaFlushAllocation(allocator, stagingBuffer.allocation, 0, size);

        AllocatedBuffer deviceBuffer = createAllocatedBuffer(allocator, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0, VMA_MEMORY_USAGE_AUTO);
            immediateSubmit(submitContext, [&](VkCommandBuffer cmd) {
            VkBufferCopy copyRegion{};
            copyRegion.size = size;
            vkCmdCopyBuffer(cmd, stagingBuffer.handle, deviceBuffer.handle, 1, &copyRegion);
        });

        destroyAllocatedBuffer(allocator, stagingBuffer);

        return deviceBuffer;
    }
}