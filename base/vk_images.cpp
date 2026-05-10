#include "vk_images.h"

#include "VulkanTools.h"

#include <cstring>
#include <stdexcept>

namespace vkutil {
    AllocatedImage createAllocatedImage(VkDevice device,
            VmaAllocator allocator,
            VkExtent3D extent,
            VkFormat format,
            VkImageUsageFlags usage,
            VkImageAspectFlags aspectFlags)
    {
        // 创建最常用的 2D optimal-tiled image，内存放在 device-local，适合 GPU 访问。
        VkImageCreateInfo imageCI{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageCI.imageType = VK_IMAGE_TYPE_2D;
        imageCI.format = format;
        imageCI.extent = extent;
        imageCI.mipLevels = 1;
        imageCI.arrayLayers = 1;
        imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCI.usage = usage;
        imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocCI{};
        allocCI.usage = VMA_MEMORY_USAGE_AUTO;
        allocCI.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        AllocatedImage image {};
        image.extent = extent;
        image.format = format;
        image.layout = VK_IMAGE_LAYOUT_UNDEFINED;

        VK_CHECK_RESULT(vmaCreateImage(allocator, &imageCI, &allocCI, &image.image, &image.allocation, &image.allocationInfo));

        // 大多数 image 创建后都会立刻被 descriptor 或 render attachment 使用，因此这里顺手创建默认 view。
        VkImageViewCreateInfo viewInfo {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &image.imageView));

        return image;
    }
    
    void destroyAllocatedImage(VkDevice device,
            VmaAllocator allocator,
            AllocatedImage& image)
    {
        if (image.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, image.imageView, nullptr);
            image.imageView = VK_NULL_HANDLE;
        }

        if (image.image != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, image.image, image.allocation);
            image.image = VK_NULL_HANDLE;
            image.allocation = VK_NULL_HANDLE;
        }

        image.extent = {};
        image.format = VK_FORMAT_UNDEFINED;
        image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    
    void cmdTransitionImageLayout(VkCommandBuffer cmd,
            VkImage image,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            VkImageAspectFlags aspectMask)
    {
        // 使用 synchronization2，让 stage/access 的对应关系更显式，后续扩展 offscreen/shadow 会更清楚。
        VkImageMemoryBarrier2 imageBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        imageBarrier.oldLayout = oldLayout;
        imageBarrier.newLayout = newLayout;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = aspectMask;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;

        // UNDEFINED -> TRANSFER_DST_OPTIMAL
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            imageBarrier.srcAccessMask = 0;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }
        // TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }
        // UNDEFINED -> ATTACHMENT_OPTIMAL
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL) {
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            imageBarrier.srcAccessMask = 0;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        // SHADER_READ_ONLY_OPTIMAL -> ATTACHMENT_OPTIMAL
        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL) {
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        // ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
        else if (oldLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }
        // ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
        else if (oldLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            imageBarrier.dstAccessMask = 0;
        }
        // DEPTH_STENCIL_READ_ONLY_OPTIMAL -> DEPTH_ATTACHMENT_OPTIMAL
        else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        // DEPTH_ATTACHMENT_OPTIMAL -> DEPTH_STENCIL_READ_ONLY_OPTIMAL
        else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }
        // UNDEFINED -> DEPTH_ATTACHMENT_OPTIMAL
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            imageBarrier.srcAccessMask = 0;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        else {
            throw std::runtime_error("unsupported layout transition!");
        }

        VkDependencyInfo dependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &imageBarrier;

        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }
    
    void cmdCopyBufferToImage(VkCommandBuffer cmd,
            VkBuffer buffer,
            VkImage image,
            uint32_t width,
            uint32_t height)
    {
        // 像素数据是紧密排列的；rowLength/imageHeight 为 0 表示按 imageExtent 自动推导行宽。
        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;

        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;

        copyRegion.imageOffset = { 0, 0, 0 };
        copyRegion.imageExtent = { width, height, 1 };

        vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    }
    
    AllocatedTexture createTexture2DFromPixels(VkDevice device,
            VmaAllocator allocator,
            const ImmediateSubmitContext& submitContext,
            const void* pixels,
            uint32_t width,
            uint32_t height,
            VkFormat format)
    {
        // 当前版本先假设输入是 RGBA8 类数据：每像素 4 字节，且只有一个 mip。
        const VkDeviceSize uploadSize = static_cast<VkDeviceSize>(width) * height * 4;

        // 先把 CPU 像素写入 mapped staging buffer，再由 GPU copy 到真正的 image。
        AllocatedBuffer stagingBuffer = createAllocatedBuffer(
            allocator, 
            uploadSize, 
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 
            VMA_MEMORY_USAGE_AUTO
        );
        std::memcpy(stagingBuffer.allocationInfo.pMappedData, pixels, static_cast<size_t>(uploadSize));
        VK_CHECK_RESULT(vmaFlushAllocation(allocator, stagingBuffer.allocation, 0, uploadSize));

        AllocatedTexture texture {};
        texture.image = createAllocatedImage(device, allocator, { width, height, 1 }, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

        // 用一次同步提交完成 layout transition 和 copy，结束后 staging buffer 就可以立即释放。
        vkutil::immediateSubmit(submitContext, [&](VkCommandBuffer cmd) {
            cmdTransitionImageLayout(cmd, texture.image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            cmdCopyBufferToImage(cmd, stagingBuffer.handle, texture.image.image, width, height);
            cmdTransitionImageLayout(cmd, texture.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        });
        texture.image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        destroyAllocatedBuffer(allocator, stagingBuffer);

        VkSamplerCreateInfo samplerCI{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerCI.magFilter = VK_FILTER_LINEAR;
        samplerCI.minFilter = VK_FILTER_LINEAR;
        samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerCI.mipLodBias = 0.0f;
        samplerCI.minLod = 0.0f;
        samplerCI.maxLod = 0.0f;
        samplerCI.anisotropyEnable = VK_FALSE;
        samplerCI.maxAnisotropy = 1.0f;
        samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

        VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &texture.sampler));

        // 缓存 descriptor 信息，让 sample 侧写 descriptor set 时只关心“绑定哪张纹理”。
        texture.descriptor.sampler = texture.sampler;
        texture.descriptor.imageView = texture.image.imageView;
        texture.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        return texture;
    }
    
    void destroyTexture(VkDevice device,
            VmaAllocator allocator,
            AllocatedTexture& texture)
    {
        // sampler 和 image 是两个独立 Vulkan 对象，但 ownership 都归这个 texture wrapper。
        if (texture.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, texture.sampler, nullptr);
            texture.sampler = VK_NULL_HANDLE;
        }

        destroyAllocatedImage(device, allocator, texture.image);
        texture.descriptor = {};
    }
}
