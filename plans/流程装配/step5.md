# Step 5：抽出 AllocatedImage / vk_images，自己创建 GPU Texture

你已经完成 Step 4：

```text
圆盘有 UV
HLSL 可以采样 Texture2D / SamplerState
descriptor binding 1 是 combined image sampler
vks::Texture2D 成功加载 KTX 并显示纹理
```

这说明“使用纹理”的链路已经打通了。

Step 5 要学习的是另一半：

```text
如何自己创建 image
如何用 staging buffer 上传像素
如何做 image layout transition
如何创建 image view / sampler / descriptor
```

这一轮建议先不要重写 KTX loader。

我们先用 CPU 生成一张 checkerboard 纹理，然后上传到 GPU。这样可以把注意力集中在 Vulkan image 本身，而不是文件格式解析。

最终目标：

```text
用自己的 vkutil::AllocatedTexture 替换 Lab0 里的 vks::Texture2D colorTexture
画面仍然能显示纹理
descriptor / shader 基本不变
```

## 1. 这一轮为什么重要

Vulkan 里很多东西本质上都是 image：

```text
普通 2D texture
depth texture
shadow map
G-buffer normal / albedo / depth
offscreen color target
HDR render target
bloom ping-pong texture
cubemap / IBL environment map
storage image
```

Step 4 让你会“采样一张 texture”。

Step 5 让你开始掌握：

```text
这张 texture 在 GPU 上到底是怎么创建出来的。
```

这一步做完，你后面进 offscreen、shadow mapping、deferred shading 会轻松很多。

## 2. 本轮推荐结构

新增：

```text
base/vk_images.h
base/vk_images.cpp
```

在 `base/vk_types.h` 新增两个结构：

```cpp
struct AllocatedImage {
    VkImage image{ VK_NULL_HANDLE };
    VkImageView imageView{ VK_NULL_HANDLE };
    VmaAllocation allocation{ VK_NULL_HANDLE };
    VkExtent3D extent{};
    VkFormat format{ VK_FORMAT_UNDEFINED };
    VkImageLayout layout{ VK_IMAGE_LAYOUT_UNDEFINED };
};

struct AllocatedTexture {
    AllocatedImage image;
    VkSampler sampler{ VK_NULL_HANDLE };
    VkDescriptorImageInfo descriptor{};
};
```

为什么分两个结构？

```text
AllocatedImage
-> 只表示 GPU image 资源
-> 可以用于 texture、depth image、offscreen render target、shadow map

AllocatedTexture
-> image + sampler + descriptor
-> 专门表示可以被 fragment shader 采样的 2D texture
```

这个分层很重要。
不要把所有 image 都理解成 texture。

## 3. 本轮要实现哪些 helper

建议先实现这些：

```cpp
namespace vkutil {

AllocatedImage createAllocatedImage(
    VkDevice device,
    VmaAllocator allocator,
    VkExtent3D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspectFlags);

void destroyAllocatedImage(
    VkDevice device,
    VmaAllocator allocator,
    AllocatedImage& image);

void cmdTransitionImage(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageAspectFlags aspectMask);

void cmdCopyBufferToImage(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height);

AllocatedTexture createTexture2DFromPixels(
    VkDevice device,
    VmaAllocator allocator,
    const ImmediateSubmitContext& submitContext,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    VkFormat format);

void destroyTexture(
    VkDevice device,
    VmaAllocator allocator,
    AllocatedTexture& texture);

}
```

注意这里有两类函数：

```text
cmdXXX
-> 只录命令，不提交

createTexture2DFromPixels
-> 内部使用 immediateSubmit，一次性完成 transition/copy/transition
```

这种分层以后会很有用。
例如 offscreen pass 里，你可能已经在录主 command buffer，这时就应该调用 `cmdTransitionImage()`，而不是再开一个 `immediateSubmit()`。

## 4. 新建 vk_images.h

新建：

```text
base/vk_images.h
```

建议内容：

```cpp
#pragma once

#include "vk_commands.h"
#include "vk_resources.h"
#include "vk_types.h"

namespace vkutil {

AllocatedImage createAllocatedImage(
    VkDevice device,
    VmaAllocator allocator,
    VkExtent3D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspectFlags);

void destroyAllocatedImage(
    VkDevice device,
    VmaAllocator allocator,
    AllocatedImage& image);

void cmdTransitionImage(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageAspectFlags aspectMask);

void cmdCopyBufferToImage(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height);

AllocatedTexture createTexture2DFromPixels(
    VkDevice device,
    VmaAllocator allocator,
    const ImmediateSubmitContext& submitContext,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    VkFormat format);

void destroyTexture(
    VkDevice device,
    VmaAllocator allocator,
    AllocatedTexture& texture);

}
```

这里 include `vk_resources.h` 是因为 `createTexture2DFromPixels()` 会创建 staging buffer。

## 5. 实现 createAllocatedImage

在：

```text
base/vk_images.cpp
```

先写：

```cpp
#include "vk_images.h"

#include <cstring>

#include "VulkanTools.h"

namespace vkutil {
```

然后实现：

```cpp
AllocatedImage createAllocatedImage(
    VkDevice device,
    VmaAllocator allocator,
    VkExtent3D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspectFlags)
{
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = extent;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    AllocatedImage image{};
    image.extent = extent;
    image.format = format;
    image.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CHECK_RESULT(vmaCreateImage(
        allocator,
        &imageInfo,
        &allocationInfo,
        &image.image,
        &image.allocation,
        nullptr));

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
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
```

这一段对应的 Vulkan 对象关系：

```text
VkImage
-> 真正的 GPU image 资源

VmaAllocation
-> image 背后的 GPU memory

VkImageView
-> shader / framebuffer 访问 image 的视图
```

后面 shadow map 和 offscreen render target 都会复用这个函数。

## 6. 实现 destroyAllocatedImage

```cpp
void destroyAllocatedImage(
    VkDevice device,
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
```

销毁顺序要记住：

```text
先 destroy image view
再 destroy image + allocation
```

因为 image view 依赖 image。

## 7. 实现 cmdTransitionImage

你现在 Lab0 已经使用 Vulkan 1.3，并启用了：

```cpp
enabledFeatures.synchronization2 = VK_TRUE;
```

所以这里建议用 `vkCmdPipelineBarrier2`。

先支持本轮需要的两个 transition：

```text
UNDEFINED -> TRANSFER_DST_OPTIMAL
TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
```

实现：

```cpp
void cmdTransitionImage(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageAspectFlags aspectMask)
{
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

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        imageBarrier.srcAccessMask = 0;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    } else {
        throw std::runtime_error("Unsupported image layout transition");
    }

    VkDependencyInfo dependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}
```

这里先不要追求支持所有 layout。

学习阶段只支持你真的用到的 transition，反而更清楚：

```text
UNDEFINED
-> 新 image 初始状态，不关心旧内容

TRANSFER_DST_OPTIMAL
-> 准备被 vkCmdCopyBufferToImage 写入

SHADER_READ_ONLY_OPTIMAL
-> 准备被 fragment shader 采样
```

## 8. 实现 cmdCopyBufferToImage

```cpp
void cmdCopyBufferToImage(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height)
{
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

    vkCmdCopyBufferToImage(
        cmd,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion);
}
```

这里的前提是：

```text
image 已经 transition 到 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
```

这就是为什么我们要在 copy 前做 layout transition。

## 9. 实现 createTexture2DFromPixels

这个函数把前面的 helper 串起来：

```text
CPU pixels
-> staging buffer
-> AllocatedImage
-> transition UNDEFINED -> TRANSFER_DST
-> copy buffer to image
-> transition TRANSFER_DST -> SHADER_READ_ONLY
-> create sampler
-> fill descriptor
```

实现：

```cpp
AllocatedTexture createTexture2DFromPixels(
    VkDevice device,
    VmaAllocator allocator,
    const ImmediateSubmitContext& submitContext,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    VkFormat format)
{
    const VkDeviceSize uploadSize = static_cast<VkDeviceSize>(width) * height * 4;

    AllocatedBuffer stagingBuffer = createAllocatedBuffer(
        allocator,
        uploadSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT,
        VMA_MEMORY_USAGE_AUTO);

    std::memcpy(stagingBuffer.allocationInfo.pMappedData, pixels, static_cast<size_t>(uploadSize));
    VK_CHECK_RESULT(vmaFlushAllocation(allocator, stagingBuffer.allocation, 0, uploadSize));

    AllocatedTexture texture{};
    texture.image = createAllocatedImage(
        device,
        allocator,
        VkExtent3D{ width, height, 1 },
        format,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    immediateSubmit(submitContext, [&](VkCommandBuffer cmd) {
        cmdTransitionImage(
            cmd,
            texture.image.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        cmdCopyBufferToImage(cmd, stagingBuffer.handle, texture.image.image, width, height);

        cmdTransitionImage(
            cmd,
            texture.image.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
    });

    texture.image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    destroyAllocatedBuffer(allocator, stagingBuffer);

    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &texture.sampler));

    texture.descriptor.sampler = texture.sampler;
    texture.descriptor.imageView = texture.image.imageView;
    texture.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    return texture;
}
```

本轮先假设：

```text
format = VK_FORMAT_R8G8B8A8_UNORM
每个像素 4 bytes
mipLevels = 1
2D color texture
```

不要一开始就把它做成万能函数。
等你真的需要 HDR、depth、mipmap、cubemap，再扩展。

## 10. 实现 destroyTexture

```cpp
void destroyTexture(
    VkDevice device,
    VmaAllocator allocator,
    AllocatedTexture& texture)
{
    if (texture.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, texture.sampler, nullptr);
        texture.sampler = VK_NULL_HANDLE;
    }

    destroyAllocatedImage(device, allocator, texture.image);
    texture.descriptor = {};
}
```

销毁顺序：

```text
sampler
image view
image allocation
```

sampler 不依赖 image，但放前面销毁很清楚。

## 11. CMake 注意事项

你现在 `base/CMakeLists.txt` 有 glob：

```cmake
file(GLOB BASE_SRC "*.cpp" "*.hpp" "*.h" "../external/imgui/*.cpp")
```

理论上新增 `vk_images.cpp/.h` 后重新 configure 就能进工程。

如果编译时出现：

```text
unresolved external symbol vkutil::createTexture2DFromPixels
```

说明 `vk_images.cpp` 没有被编进 `base`。

可以在 `base/CMakeLists.txt` 的 `list(APPEND BASE_SRC ...)` 中显式追加：

```cmake
"${CMAKE_CURRENT_SOURCE_DIR}/vk_images.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/vk_images.h"
```

然后重新 configure/build。

## 12. 修改 Lab0 类型

当前 `lab0.h` 里是：

```cpp
vks::Texture2D colorTexture;
```

改成：

```cpp
AllocatedTexture colorTexture;
```

然后 include：

```cpp
#include "vk_images.h"
```

如果你之前只为了 `vks::Texture2D` include 了：

```cpp
#include "VulkanTexture.h"
```

这轮可以删掉。

但如果别处还用，就先保留也没关系。

## 13. 修改析构

当前析构里是：

```cpp
colorTexture.destroy();
```

改成：

```cpp
vkutil::destroyTexture(device, allocator, colorTexture);
```

注意顺序仍然是：

```text
destroyTexture
destroyAllocatedBuffer vertex/index/uniform
destroyVmaAllocator
```

只要 texture 在 allocator 销毁前销毁就行。

## 14. 用 CPU 生成 checkerboard texture

当前 `loadTexture()` 是：

```cpp
void VulkanExample::loadTexture() {
    colorTexture.loadFromFile(
        getAssetPath() + "textures/metalplate01_rgba.ktx",
        VK_FORMAT_R8G8B8A8_UNORM,
        vulkanDevice,
        queue);
}
```

改成：

```cpp
void VulkanExample::loadTexture() {
    constexpr uint32_t textureWidth = 256;
    constexpr uint32_t textureHeight = 256;
    constexpr uint32_t channelCount = 4;
    constexpr uint32_t checkerSize = 32;

    std::vector<uint8_t> pixels(textureWidth * textureHeight * channelCount);

    for (uint32_t y = 0; y < textureHeight; y++) {
        for (uint32_t x = 0; x < textureWidth; x++) {
            const bool checker =
                ((x / checkerSize) + (y / checkerSize)) % 2 == 0;

            const size_t pixelIndex =
                static_cast<size_t>(y * textureWidth + x) * channelCount;

            pixels[pixelIndex + 0] = checker ? 230 : 35;
            pixels[pixelIndex + 1] = checker ? 210 : 45;
            pixels[pixelIndex + 2] = checker ? 90 : 120;
            pixels[pixelIndex + 3] = 255;
        }
    }

    vkutil::ImmediateSubmitContext submitContext{
        device,
        queue,
        commandPool
    };

    colorTexture = vkutil::createTexture2DFromPixels(
        device,
        allocator,
        submitContext,
        pixels.data(),
        textureWidth,
        textureHeight,
        VK_FORMAT_R8G8B8A8_UNORM);
}
```

这里用 `std::vector<uint8_t>`，不要用 `uint32_t` 直接写颜色。

原因是：

```text
uint8_t RGBA 顺序最直观
不需要思考小端机器上的 uint32_t 字节顺序
```

## 15. Descriptor 代码基本不用改

你 Step 4 已经写了：

```cpp
VkDescriptorImageInfo imageInfo = colorTexture.descriptor;
```

如果 `AllocatedTexture` 也有：

```cpp
VkDescriptorImageInfo descriptor;
```

那么 descriptor 写入代码不用改。

这就是为什么我们让 `AllocatedTexture` 保持和 `vks::Texture2D` 类似的 descriptor 成员。

这种兼容设计很小，但很舒服。

## 16. Shader 不需要改

Step 5 不改变 shader interface。

你的 HLSL 仍然是：

```hlsl
Texture2D baseColorTexture : register(t1);
SamplerState baseColorSampler : register(s1);
```

descriptor binding 仍然是：

```text
binding 1 -> VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
```

所以：

```text
不需要重新编译 shader
```

除非你顺手改了 shader 内容。

## 17. 编译和运行

编译：

```powershell
cmake --build build --config Debug --target lab0 -j 32
```

运行：

```powershell
build\bin\Debug\lab0.exe -v -vl
```

成功标准：

```text
圆盘显示 checkerboard 纹理
push constant 仍然可以影响颜色
validation layer 没有 image layout / descriptor 错误
关闭程序无资源销毁错误
```

如果出现 LNK2019：

```text
检查 vk_images.cpp 是否进了 base target
重新 CMake configure
必要时显式加入 base/CMakeLists.txt
```

如果画面黑：

```text
检查 createTexture2DFromPixels 是否设置 descriptor.imageLayout = SHADER_READ_ONLY_OPTIMAL
检查 transition 是否做了 TRANSFER_DST -> SHADER_READ_ONLY
检查 descriptor binding 1 是否仍然写入 colorTexture.descriptor
检查 shader 是否仍然采样 register(t1/s1)
```

如果 validation 报 layout 错误：

```text
检查 vkCmdCopyBufferToImage 时 image layout 是否是 TRANSFER_DST_OPTIMAL
检查 shader 采样前 image layout 是否是 SHADER_READ_ONLY_OPTIMAL
检查 VkDescriptorImageInfo.imageLayout 是否也是 SHADER_READ_ONLY_OPTIMAL
```

如果 validation 报 synchronization2：

```text
确认 Lab0 仍然启用了 enabledFeatures.synchronization2 = VK_TRUE
确认 apiVersion = VK_API_VERSION_1_3
```

你当前 Lab0 已经这么做了，所以一般不会有问题。

## 18. 本轮完成后你应该能讲清楚的链路

完成 Step 5 后，你应该能从头讲清楚：

```text
CPU pixels
-> staging AllocatedBuffer
-> device local AllocatedImage
-> UNDEFINED -> TRANSFER_DST_OPTIMAL
-> vkCmdCopyBufferToImage
-> TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
-> VkImageView
-> VkSampler
-> VkDescriptorImageInfo
-> descriptor set binding 1
-> HLSL Texture2D.Sample
```

这是 Vulkan texture upload 的核心闭环。

你会发现，Step 4 的 `vks::Texture2D` 不再是黑盒了。
它内部做的事情，你现在已经能复刻一个最小版本。

## 19. Step 5 之后再做什么

Step 5 完成后有两个自然方向：

```text
Step 5.5
-> 把 depthStencil 也迁移到 AllocatedImage
-> 这样 getMemoryTypeIndex() 可以继续减少存在感

Step 6
-> 做 offscreen color image
-> 第一 pass 画到自己的 image
-> 第二 pass 把这个 image 画回 swapchain
```

我更建议先做：

```text
Step 5.5：depthStencil -> AllocatedImage
```

原因是：

```text
它能复用 createAllocatedImage / destroyAllocatedImage
但还不引入多 pass
```

等 depth image 也走你的 VMA image helper 后，再做 offscreen pass 会非常自然。

