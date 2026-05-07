#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>

#include <vulkan/vulkan.h>
// #include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

// #include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

// 由 VMA 管理内存的 Buffer 包装。
// 这里只保存资源句柄和分配信息，释放时统一走 vkutil::destroyAllocatedBuffer。
struct AllocatedBuffer {
    VkBuffer handle{ VK_NULL_HANDLE };
    VmaAllocation allocation{ VK_NULL_HANDLE };
    VmaAllocationInfo allocationInfo{};
    VkDeviceSize size{ 0 };
};

// GPU 侧 mesh 数据。
// 目前采用 vertex buffer + index buffer 的经典组合，适合 vkCmdDrawIndexed。
struct GPUMeshBuffers {
    AllocatedBuffer vertexBuffer;
    AllocatedBuffer indexBuffer;
    uint32_t indexCount{ 0 };
    VkIndexType indexType{ VK_INDEX_TYPE_UINT32 };
};

// 由 VMA 管理内存的 Image 包装，并默认持有一个 2D ImageView。
// 保持通用：它既可以表示普通纹理，也可以表示 depth image、shadow map、offscreen render target。
struct AllocatedImage {
    VkImage image{ VK_NULL_HANDLE };
    VkImageView imageView{ VK_NULL_HANDLE };
    VmaAllocation allocation{ VK_NULL_HANDLE };
    VmaAllocationInfo allocationInfo{};
    VkExtent3D extent{};
    VkFormat format{ VK_FORMAT_UNDEFINED };
    VkImageLayout layout{ VK_IMAGE_LAYOUT_UNDEFINED };
};

// 可被 shader 采样的 2D 纹理。
// 在 AllocatedImage 之上补充 sampler 和 descriptor，方便 descriptor set 写入时直接使用。
struct AllocatedTexture {
    AllocatedImage image;
    VkSampler sampler{ VK_NULL_HANDLE };
    VkDescriptorImageInfo descriptor{};
};
