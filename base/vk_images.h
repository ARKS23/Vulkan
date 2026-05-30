#pragma once

#include "vk_resources.h"

namespace vkutil {
    // 创建一个 device-local 2D image，并同时创建匹配的 image view。
    // aspectFlags 决定 view 看到的是 color、depth 还是 depth-stencil 子资源。
    AllocatedImage createAllocatedImage(
        VkDevice device,
        VmaAllocator allocator,
        VkExtent3D extent,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspectFlags
    );

    AllocatedCubeTexture createAllocatedCubeTexture(
        VkDevice device,
        VmaAllocator allocator,
        uint32_t dim,
        uint32_t mipLevels,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageLayout descriptorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    // 按依赖顺序销毁 image view，再销毁 VMA image allocation。
    void destroyAllocatedImage(
        VkDevice device,
        VmaAllocator allocator,
        AllocatedImage& allocatedImage
    );

    void destroyAllocatedCubeTexture(
        VkDevice device,
        VmaAllocator allocator,
        AllocatedCubeTexture& texture
    );

    VkImageSubresourceRange cubeSubresourceRange(
        uint32_t mipLevels,
        uint32_t baseMipLevel = 0,
        uint32_t baseArrayLayer = 0,
        uint32_t layerCount = 6
    );

    // 向已有 command buffer 录入一次 image layout transition。
    // 当前先覆盖 texture upload 所需路径，后续 offscreen/shadow 再按需扩展。
    void cmdTransitionImageLayout(
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspectMask
    );

    // Range-based overload for cubemaps, texture arrays and mipmapped images.
    void cmdTransitionImageLayout(
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageSubresourceRange subresourceRange
    );

    // 录入 buffer -> image 拷贝命令，面向单层、单 mip 的 2D color image。
    // 调用前目标 image 必须已经处于 TRANSFER_DST_OPTIMAL layout。
    // 使用 AllocatedImage::layout 作为 oldLayout，并在录制 barrier 后更新它。
    // 适合单 mip、单 layer 的普通 render target，如 G-Buffer / HDR / SSAO。
    bool cmdTransitionTrackedImageLayout(
        VkCommandBuffer cmd,
        AllocatedImage& image,
        VkImageLayout newLayout,
        VkImageAspectFlags aspectMask
    );

    void cmdCopyBufferToImage(
        VkCommandBuffer cmd,
        VkBuffer buffer,
        VkImage image,
        uint32_t width,
        uint32_t height
    );

    // 从 CPU 像素内存创建一张可采样的 2D texture。
    // 内部会创建 staging buffer、上传像素、转换 layout、创建 sampler，并填好 descriptor。
    AllocatedTexture createTexture2DFromPixels(
        VkDevice device,
        VmaAllocator allocator,
        const ImmediateSubmitContext& submitContext,
        const void* pixels,
        uint32_t width,
        uint32_t height,
        VkFormat format
    );

    // 销毁 createTexture2DFromPixels 创建的 sampler 和 image 资源。
    void destroyTexture(
        VkDevice device,
        VmaAllocator allocator,
        AllocatedTexture& texture
    );
}
