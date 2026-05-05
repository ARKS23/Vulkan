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

struct AllocatedBuffer {
    VkBuffer handle{ VK_NULL_HANDLE };          // buffer句柄
    VmaAllocation allocation{ VK_NULL_HANDLE }; // vma分配句柄
    VmaAllocationInfo allocationInfo{};         // 分配信息，包含内存类型、大小、映射指针等
    VkDeviceSize size{ 0 };                     // buffer大小，单位字节
};

struct GPUMeshBuffers {
    AllocatedBuffer vertexBuffer; // 顶点缓冲
    AllocatedBuffer indexBuffer;  // 索引缓冲
    uint32_t indexCount{ 0 };    // 索引数量
    VkIndexType indexType{ VK_INDEX_TYPE_UINT32 }; // 索引类型，默认为32位无符号整数
};