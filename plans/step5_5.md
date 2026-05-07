# Step 5.5：把 Depth Stencil 迁移到 AllocatedImage

你已经完成 Step 5：

```text
CPU 生成 checkerboard pixels
-> staging buffer
-> VMA AllocatedImage
-> layout transition
-> copyBufferToImage
-> sampler + descriptor
-> HLSL Texture2D.Sample
```

这说明你已经能自己创建一张可采样的 GPU image。

Step 5.5 的目标是：

```text
把 Lab0 的 depth stencil image 也迁移到 VMA AllocatedImage
```

这一轮不会引入新画面效果，但很重要。

因为 depth image 是后续 shadow map、offscreen pass、deferred rendering 里最常见的 image 类型之一。

## 1. 当前问题

你现在的 `setupDepthStencil()` 还是手动 Vulkan 内存管理：

```cpp
vkCreateImage
vkGetImageMemoryRequirements
vkAllocateMemory
vkBindImageMemory
vkCreateImageView
```

这和你已经完成的资源系统不统一。

现在 Lab0 里已经有：

```text
vertex buffer  -> AllocatedBuffer
index buffer   -> AllocatedBuffer
uniform buffer -> AllocatedBuffer
texture image  -> AllocatedTexture / AllocatedImage
depth image    -> 仍然是 VkImage + VkDeviceMemory
```

Step 5.5 完成后会变成：

```text
depth image -> AllocatedImage
```

这一步完成后，`getMemoryTypeIndex()` 在 Lab0 里基本就可以删除了。

## 2. 这一轮的关键坑：不要直接让基类拥有 VMA depth image

这里有一个非常重要的项目结构问题。

`VulkanExampleBase` 里有一个基类成员：

```cpp
struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
} depthStencil{};
```

而且基类析构函数会手动做：

```cpp
vkDestroyImageView(device, depthStencil.view, nullptr);
vkDestroyImage(device, depthStencil.image, nullptr);
vkFreeMemory(device, depthStencil.memory, nullptr);
```

基类的 `windowResize()` 里也会手动销毁并重建：

```cpp
vkDestroyImageView(device, depthStencil.view, nullptr);
vkDestroyImage(device, depthStencil.image, nullptr);
vkFreeMemory(device, depthStencil.memory, nullptr);
setupDepthStencil();
```

所以本轮不要简单地做：

```cpp
depthStencil.image = myAllocatedImage.image;
depthStencil.view = myAllocatedImage.imageView;
```

这样退出或 resize 时，基类会用 `vkDestroyImage + vkFreeMemory` 去销毁 VMA 创建的 image，这是错误的。

本轮推荐的安全方案是：

```text
Lab0 自己持有 AllocatedImage depthImage
Lab0 的 render() 使用 depthImage.image / depthImage.imageView
基类 depthStencil 保持 VK_NULL_HANDLE
```

这样基类析构和 resize 时销毁空句柄，不会碰你的 VMA image。

## 3. 修改 lab0.h：新增 depthImage

在 `VulkanExample` 成员里加：

```cpp
AllocatedImage depthImage;
```

建议放在 texture 附近：

```cpp
AllocatedTexture baseColorTexture;
AllocatedImage depthImage;
```

这里不用 `AllocatedTexture`。

原因是 depth image 不是被普通 fragment shader 采样的颜色纹理。
它现在只是 dynamic rendering 的 depth attachment。

所以它不需要：

```text
sampler
VkDescriptorImageInfo
```

只需要：

```text
VkImage
VkImageView
VmaAllocation
format / extent / layout
```

## 4. 修改 prepare 顺序：VMA allocator 要提前创建

你现在的 `prepare()` 大概是：

```cpp
void VulkanExample::prepare() {
    VulkanExampleBase::prepare();

    createVmaAllocator();
    createSynchronizationPrimitives();
    createCommandBuffers();
    ...
}
```

但 `VulkanExampleBase::prepare()` 内部会调用虚函数：

```cpp
setupDepthStencil();
```

如果 `setupDepthStencil()` 要用 VMA，那么 `allocator` 必须在 `VulkanExampleBase::prepare()` 之前创建。

所以改成：

```cpp
void VulkanExample::prepare() {
    createVmaAllocator();

    VulkanExampleBase::prepare();

    createSynchronizationPrimitives();
    createCommandBuffers();
    createVertexBuffer();
    createUniformBuffers();
    loadTexture();
    createDescriptors();
    createPipeline();
    prepared = true;
}
```

注意：

```text
createVmaAllocator() 依赖 device / physicalDevice / instance
这些在 initVulkan() 后已经有效
所以可以放在 VulkanExampleBase::prepare() 前
```

同时要确保：

```text
不要再在 VulkanExampleBase::prepare() 后第二次 createVmaAllocator()
```

## 5. 增加 depth format 辅助函数

Depth image 的 aspect mask 不能永远写死成：

```cpp
VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
```

有些 `depthFormat` 只有 depth，没有 stencil。

建议在 `lab0.cpp` 文件顶部加两个小 helper：

```cpp
namespace {

bool hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D16_UNORM_S8_UINT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT ||
           format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkImageAspectFlags getDepthAspectMask(VkFormat format) {
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (hasStencilComponent(format)) {
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return aspectMask;
}

}
```

这两个函数后面 shadow map、depth prepass、offscreen depth texture 都会继续有用。

## 6. 重写 setupDepthStencil

把当前 `setupDepthStencil()` 里的：

```cpp
vkCreateImage
vkGetImageMemoryRequirements
vkAllocateMemory
vkBindImageMemory
vkCreateImageView
```

替换成：

```cpp
void VulkanExample::setupDepthStencil() {
    vkutil::destroyAllocatedImage(device, allocator, depthImage);

    const VkImageAspectFlags aspectMask = getDepthAspectMask(depthFormat);

    depthImage = vkutil::createAllocatedImage(
        device,
        allocator,
        VkExtent3D{ width, height, 1 },
        depthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        aspectMask);

    depthImage.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    // 基类 depthStencil 由 VulkanExampleBase 手动销毁。
    // Lab0 的 VMA depth image 不交给基类管理，避免 resize/析构时错误释放。
    depthStencil.image = VK_NULL_HANDLE;
    depthStencil.view = VK_NULL_HANDLE;
    depthStencil.memory = VK_NULL_HANDLE;
}
```

这里 `destroyAllocatedImage()` 放在开头是为了支持 resize。

当窗口大小变化时，基类 `windowResize()` 会再次调用你的 `setupDepthStencil()`。
此时你需要先销毁旧的 `depthImage`，再创建新尺寸的 depth image。

## 7. 修改 render 里的 depth barrier

你现在 render 里应该有：

```cpp
vks::tools::insertImageMemoryBarrier(
    commandBuffer,
    depthStencil.image,
    ...
    VkImageSubresourceRange{
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        0, 1, 0, 1
    });
```

改成：

```cpp
const VkImageAspectFlags depthAspectMask = getDepthAspectMask(depthFormat);

vks::tools::insertImageMemoryBarrier(
    commandBuffer,
    depthImage.image,
    0,
    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
    VkImageSubresourceRange{
        depthAspectMask,
        0, 1, 0, 1
    });
```

这里仍然可以先用项目已有的 `vks::tools::insertImageMemoryBarrier()`。

本轮目标是迁移 depth image 的创建和销毁，不必强行把所有 barrier 都改成 `vkCmdPipelineBarrier2`。

之后做 Step 6/offscreen 时，再统一 transition helper 也可以。

## 8. 修改 dynamic rendering attachment

当前：

```cpp
depthStencilAttachment.imageView = depthStencil.view;
```

改成：

```cpp
depthStencilAttachment.imageView = depthImage.imageView;
```

其他字段可以先保持：

```cpp
depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
depthStencilAttachment.clearValue.depthStencil = { 1.0f, 0 };
```

如果你想更严谨，可以把 image layout 写成 Vulkan 1.3 更通用的：

```cpp
VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL
```

你现在已经在 barrier 里用了 `VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL`。

但为了减少一次改动范围，这轮先保持原写法也可以。

## 9. 修改析构函数

在 `VulkanExample::~VulkanExample()` 里，在 `destroyVmaAllocator()` 前销毁：

```cpp
vkutil::destroyAllocatedImage(device, allocator, depthImage);
```

建议顺序：

```cpp
vkutil::destroyTexture(device, allocator, baseColorTexture);

vkutil::destroyAllocatedImage(device, allocator, depthImage);

vkutil::destroyAllocatedBuffer(allocator, circleMeshBuffers.vertexBuffer);
vkutil::destroyAllocatedBuffer(allocator, circleMeshBuffers.indexBuffer);

for (...) {
    vkutil::destroyAllocatedBuffer(allocator, uniformBuffersV2[i].buffer);
}

destroyVmaAllocator();
```

如果你按上面的 `setupDepthStencil()` 保持基类 `depthStencil` 为空，那么基类析构后续再销毁空句柄是安全的。

## 10. 可以删除 getMemoryTypeIndex

完成后检查：

```powershell
rg -n "getMemoryTypeIndex|vkAllocateMemory|vkBindImageMemory|vkGetImageMemoryRequirements" examples/Lab0
```

如果没有其它地方使用：

```cpp
uint32_t getMemoryTypeIndex(...);
```

就可以从 `lab0.h` 和 `lab0.cpp` 删除。

这一步很有标志性：

```text
Lab0 不再手写 memory type selection
buffer/image 都交给 VMA
```

这就是你引入 VMA 后真正想要达到的状态。

## 11. 编译和运行

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
画面和 Step 5 一样
两个圆盘仍然正确遮挡
validation layer 没有 image destroy / image layout / depth attachment 错误
窗口关闭不报资源释放错误
```

建议额外测试一次：

```text
拖动窗口改变大小
```

因为 Step 5.5 最容易出问题的地方就是 resize。

如果 resize 后崩溃，大概率是：

```text
旧 depthImage 没有在 setupDepthStencil() 开头 destroy
或者基类 depthStencil 被设置成了 VMA image，导致基类错误销毁
```

## 12. 常见错误

如果退出时报 image/free memory 相关 validation 错误：

```text
检查基类 depthStencil.image/view/memory 是否保持 VK_NULL_HANDLE
检查自己的 depthImage 是否只由 vkutil::destroyAllocatedImage 销毁
```

如果 resize 崩溃：

```text
检查 setupDepthStencil() 开头是否 destroyAllocatedImage(depthImage)
检查 createVmaAllocator() 是否已经在 VulkanExampleBase::prepare() 前执行
```

如果 depth 不生效，两个圆盘遮挡异常：

```text
检查 depthStencilAttachment.imageView 是否改成 depthImage.imageView
检查 depth barrier 是否改成 depthImage.image
检查 depthAspectMask 是否包含正确 aspect
检查 pipeline depth test/write 是否仍然开启
```

如果 validation 报 aspect mask 错：

```text
不要无脑使用 DEPTH | STENCIL
用 getDepthAspectMask(depthFormat)
只有 stencil format 才加 VK_IMAGE_ASPECT_STENCIL_BIT
```

## 13. 关于 loadTexture hard code：后续怎么做

你现在的 `loadTexture()` 是 CPU hard code 生成 checkerboard。

这很好，它应该保留，但定位要变成：

```text
debug/procedural texture
```

我的建议路线是：

```text
短期
-> 保留 create checkerboard / solid color 这类 procedural texture
-> 用它们调试 UV、sampler、descriptor、fallback texture

中期
-> 不再回到 vks::Texture2D 作为最终资源类型
-> 复用项目已有 KTX 库和 VulkanTexture.cpp 的加载思路
-> 写自己的 vkutil::loadKtxTexture2D，返回 AllocatedTexture

后期
-> 如果 Games202 实验需要 PNG/JPG/HDR
-> 再引入 stb_image 或 tinygltf 贴图读取链路
-> 但最终仍然走 AllocatedTexture / AllocatedImage
```

也就是说：

```text
资源对象不要退回 vks::Texture2D
文件解析逻辑可以借鉴/复用原项目
GPU 资源创建继续走你的 vk_images
```

## 14. 为什么优先复用 KTX，而不是马上引入新库

这个项目已经有：

```text
external/ktx
base/VulkanTexture.cpp
大量 assets/textures/*.ktx
```

KTX 对 Vulkan 很友好：

```text
可以保存 mipmap
可以保存 GPU-friendly format
可以保存 cubemap / array texture
很多 Vulkan sample 都用 KTX
```

所以中期最推荐做：

```cpp
AllocatedTexture vkutil::loadKtxTexture2D(
    VkDevice device,
    VmaAllocator allocator,
    const ImmediateSubmitContext& submitContext,
    const std::string& filename,
    VkFormat format);
```

内部可以参考：

```text
base/VulkanTexture.cpp
vks::Texture2D::loadFromFile()
```

但最终创建 image、sampler、descriptor 时，使用你自己的：

```text
createAllocatedImage
cmdTransitionImageLayout
cmdCopyBufferToImage
destroyTexture
```

## 15. 什么时候引入 stb_image

项目里目前 `external/stb` 主要是字体相关文件，不是完整的 `stb_image.h` 贴图加载链路。

如果后面你需要：

```text
PNG
JPG
HDR environment map
普通课程资源图片
```

可以引入 `stb_image.h`。

但我建议不要现在引入。

原因是当前阶段你的重点是：

```text
Vulkan image ownership
layout transition
descriptor
offscreen/depth/shadow
```

图片文件格式不是核心。

比较稳的节奏是：

```text
现在
-> procedural checkerboard

下一阶段
-> KTX loader，复用项目资产

做 IBL/HDR 时
-> 再引入 stb_image 或专门 HDR loader
```

这样你不会被文件格式加载细节拖住。

## 16. Step 5.5 完成后下一步

完成后你会拥有：

```text
AllocatedBuffer
AllocatedImage
AllocatedTexture
VMA-managed mesh / uniform / texture / depth
```

下一步就可以进入：

```text
Step 6：Offscreen Pass
```

Step 6 的目标会是：

```text
创建一个 offscreen color AllocatedImage
第一 pass 把圆盘画到 offscreen image
第二 pass 把 offscreen image 采样画回 swapchain
```

那一步会正式把你带进多 pass 图形算法世界。

