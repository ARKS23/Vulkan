# Step 6：Offscreen Pass，正式进入多 Pass 渲染

你已经完成了：

```text
Step 1  -> GPU mesh buffer
Step 2  -> VMA uniform buffer
Step 3  -> push constant
Step 4  -> texture sampling
Step 5  -> 自己创建 AllocatedImage / AllocatedTexture
Step 5.5 -> depth image 迁移到 AllocatedImage，并通过 resize 测试
```

现在可以进入 Step 6：Offscreen Pass。

这一轮的目标是：

```text
第一 pass：把圆盘场景画到自己创建的 offscreen color image
第二 pass：把 offscreen color image 当作 texture，画回 swapchain
```

这一步非常关键。

因为从现在开始，你不再只是“把物体画到屏幕上”，而是在组织图形算法的输入输出：

```text
pass A 的输出 image
-> 作为 pass B 的输入 texture
```

后面的 shadow mapping、deferred shading、SSAO、SSR、Bloom、TAA、IBL 预计算，都建立在这个思想上。

## 1. Step 6 最小效果

第一版不要急着做复杂后处理。

只要实现：

```text
scene pass:
    draw textured circles -> offscreenColor

present pass:
    sample offscreenColor -> swapchain
```

成功后，画面应该和 Step 5.5 看起来几乎一样。

这反而是好事。

因为这说明：

```text
渲染目标从 swapchain 切换到 offscreen image
再从 offscreen image 采样回 swapchain
```

这条链路已经打通了。

等链路稳定后，再加 grayscale、blur、edge detect 这类 post-process 就很轻松。

## 2. 本轮需要新增哪些资源

在 `lab0.h` 里新增 offscreen 资源：

```cpp
AllocatedTexture offscreenColor;

VkDescriptorSetLayout blitDescriptorSetLayout{ VK_NULL_HANDLE };
VkDescriptorSet blitDescriptorSet{ VK_NULL_HANDLE };

VkPipelineLayout blitPipelineLayout{ VK_NULL_HANDLE };
VkPipeline blitPipeline{ VK_NULL_HANDLE };
```

为什么 offscreen color 用 `AllocatedTexture`？

```text
它既是第一 pass 的 color attachment
又是第二 pass 的 sampled texture
```

所以它需要：

```text
AllocatedImage
VkSampler
VkDescriptorImageInfo
```

正好符合 `AllocatedTexture`。

## 3. 新增函数规划

建议在 `lab0.h` 里声明：

```cpp
void createOffscreenResources();
void destroyOffscreenResources();
void createBlitDescriptors();
void updateBlitDescriptor();
void createBlitPipeline();
void drawScene(VkCommandBuffer commandBuffer);
void windowResized() override;
```

职责如下：

```text
createOffscreenResources
-> 创建 offscreen color image、sampler、descriptor info

destroyOffscreenResources
-> 销毁 offscreen sampler/image

createBlitDescriptors
-> 创建第二 pass 使用的 descriptor set layout 和 descriptor set

updateBlitDescriptor
-> 让 blitDescriptorSet 指向当前 offscreenColor

createBlitPipeline
-> 创建 fullscreen triangle pipeline

drawScene
-> 把当前 render() 里“绑定 scene pipeline 并画两个圆盘”的代码抽出来

windowResized
-> resize 时重建 offscreen image，并更新 blit descriptor
```

这一步最重要的不是代码量，而是把“pass 的职责”切开。

## 4. 创建 offscreen color image

`createOffscreenResources()` 可以先写成：

```cpp
void VulkanExample::createOffscreenResources() {
    destroyOffscreenResources();

    offscreenColor.image = vkutil::createAllocatedImage(
        device,
        allocator,
        VkExtent3D{ width, height, 1 },
        swapChain.colorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo samplerCI{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = 0.0f;
    samplerCI.maxAnisotropy = 1.0f;
    samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &offscreenColor.sampler));

    offscreenColor.descriptor.sampler = offscreenColor.sampler;
    offscreenColor.descriptor.imageView = offscreenColor.image.imageView;
    offscreenColor.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
```

注意 usage：

```cpp
VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
```

这表示：

```text
第一 pass 会把它当作 color attachment 写入
第二 pass 会把它当作 texture 采样
```

`CLAMP_TO_EDGE` 比 `REPEAT` 更适合 screen texture。

## 5. 销毁 offscreen resources

```cpp
void VulkanExample::destroyOffscreenResources() {
    vkutil::destroyTexture(device, allocator, offscreenColor);
}
```

这里复用 `destroyTexture()` 就够了。

因为 `AllocatedTexture` 里正好有：

```text
sampler
AllocatedImage
descriptor
```

## 6. 扩展 image layout transition helper

你现在的 `vkutil::cmdTransitionImageLayout()` 主要支持 texture upload：

```text
UNDEFINED -> TRANSFER_DST_OPTIMAL
TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
```

Step 6 需要新增：

```text
UNDEFINED -> ATTACHMENT_OPTIMAL
SHADER_READ_ONLY_OPTIMAL -> ATTACHMENT_OPTIMAL
ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
```

建议在 `base/vk_images.cpp` 里继续扩展这个函数。

新增分支可以按这个思路写：

```cpp
else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
         newLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL) {
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    imageBarrier.srcAccessMask = 0;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
}
else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
         newLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL) {
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    imageBarrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
}
else if (oldLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL &&
         newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
}
else if (oldLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL &&
         newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    imageBarrier.dstAccessMask = 0;
}
```

这个 helper 后面会继续演化。

现在不要追求万能，只覆盖当前真的用到的 pass transition。

## 7. 创建 fullscreen blit shader

新增 HLSL：

```text
shaders/hlsl/lab0/fullscreen.vert
shaders/hlsl/lab0/fullscreen.frag
```

`fullscreen.vert`：

```hlsl
struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

VSOutput main(uint vertexIndex : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };

    float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };

    VSOutput output;
    output.Pos = float4(positions[vertexIndex], 0.0, 1.0);
    output.UV = uvs[vertexIndex];
    return output;
}
```

`fullscreen.frag`：

```hlsl
Texture2D offscreenTexture : register(t0);
SamplerState offscreenSampler : register(s0);

struct PSInput
{
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 color = offscreenTexture.Sample(offscreenSampler, input.UV).rgb;
    return float4(color, 1.0);
}
```

如果画面上下颠倒，先不要紧张。

可以把 UV 的 y 反过来：

```hlsl
output.UV = float2(uvs[vertexIndex].x, 1.0 - uvs[vertexIndex].y);
```

先跑通链路，再整理方向。

编译：

```powershell
Push-Location shaders/hlsl
python compileshaders.py --sample lab0
Pop-Location
```

## 8. 创建 blit descriptor

第二 pass 只需要采样 offscreen texture。

所以可以单独建一个 descriptor set layout：

```cpp
void VulkanExample::createBlitDescriptors() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutCI.bindingCount = 1;
    layoutCI.pBindings = &binding;
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &blitDescriptorSetLayout));

    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &blitDescriptorSetLayout;
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &blitDescriptorSet));

    updateBlitDescriptor();
}
```

`updateBlitDescriptor()`：

```cpp
void VulkanExample::updateBlitDescriptor() {
    VkDescriptorImageInfo imageInfo = offscreenColor.descriptor;

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = blitDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}
```

注意 descriptor pool。

你现在的 pool 只够 scene descriptor sets。
需要把 `createDescriptors()` 里的 pool 扩大：

```cpp
descriptorTypeCounts[1].descriptorCount = MAX_CONCURRENT_FRAMES + 1;
descriptorPoolCI.maxSets = MAX_CONCURRENT_FRAMES + 1;
```

因为现在除了每帧一个 scene descriptor set，还需要一个 blit descriptor set。

## 9. 创建 blit pipeline

`createBlitPipeline()` 和当前 `createPipeline()` 很像，但更简单：

```text
无 vertex input
无 depth test
只输出到 swapchain color format
descriptor set layout 使用 blitDescriptorSetLayout
shader 使用 fullscreen.vert/fullscreen.frag
```

关键差异：

```cpp
VkPipelineVertexInputStateCreateInfo vertexInputStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
};

VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
};
depthStencilStateCI.depthTestEnable = VK_FALSE;
depthStencilStateCI.depthWriteEnable = VK_FALSE;
```

pipeline layout：

```cpp
VkPipelineLayoutCreateInfo layoutCI = vkinit::pipelineLayoutCreateInfo();
layoutCI.setLayoutCount = 1;
layoutCI.pSetLayouts = &blitDescriptorSetLayout;
VK_CHECK_RESULT(vkCreatePipelineLayout(device, &layoutCI, nullptr, &blitPipelineLayout));
```

shader：

```cpp
shaderStages[0].module = loadSPIRVShader(getShadersPath() + "lab0/fullscreen.vert.spv");
shaderStages[1].module = loadSPIRVShader(getShadersPath() + "lab0/fullscreen.frag.spv");
```

draw 时不绑定 vertex/index buffer：

```cpp
vkCmdDraw(commandBuffer, 3, 1, 0, 0);
```

这就是 fullscreen triangle。

## 10. 抽出 drawScene

把当前 render 里这些代码抽成函数：

```text
bind scene descriptor set
bind scene pipeline
push constants
bind vertex/index buffer
draw indexed
第二次 push constants
draw indexed
```

函数签名：

```cpp
void VulkanExample::drawScene(VkCommandBuffer commandBuffer) {
    vkCmdBindDescriptorSets(...);
    vkCmdBindPipeline(...);
    ...
}
```

注意 `drawScene()` 需要用到当前帧的 descriptor set：

```cpp
uniformBuffersV2[currentFrame].descriptorSet
```

也需要用到当前帧更新过的 `ShaderData`。

最简单做法：

```text
继续在 render() 里更新 UBO
drawScene() 里只负责 draw
```

## 11. 修改 render：Pass 1 画到 offscreen

第一 pass 不再使用 swapchain image view，而是：

```cpp
offscreenColor.image.imageView
```

流程：

```cpp
VkImageLayout oldOffscreenLayout = offscreenColor.image.layout;
if (oldOffscreenLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
    oldOffscreenLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

vkutil::cmdTransitionImageLayout(
    commandBuffer,
    offscreenColor.image.image,
    offscreenColor.image.layout,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_ASPECT_COLOR_BIT);
offscreenColor.image.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
```

然后 color attachment：

```cpp
VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
colorAttachment.imageView = offscreenColor.image.imageView;
colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
colorAttachment.clearValue.color = { 0.11f, 0.10f, 0.13f, 1.0f };
```

depth attachment 继续用：

```cpp
depthImage.imageView
```

begin rendering 后：

```cpp
drawScene(commandBuffer);
```

end rendering 后，把 offscreen color 转成 shader read：

```cpp
vkutil::cmdTransitionImageLayout(
    commandBuffer,
    offscreenColor.image.image,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    VK_IMAGE_ASPECT_COLOR_BIT);
offscreenColor.image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
```

这一步非常重要。

因为第二 pass 要采样它。

## 12. 修改 render：Pass 2 画回 swapchain

第二 pass 才处理 swapchain image。

先 transition：

```cpp
vkutil::cmdTransitionImageLayout(
    commandBuffer,
    swapChain.images[imageIndex],
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_ASPECT_COLOR_BIT);
```

然后 color attachment：

```cpp
VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
colorAttachment.imageView = swapChain.imageViews[imageIndex];
colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
colorAttachment.clearValue.color = { 0.02f, 0.02f, 0.025f, 1.0f };
```

这个 pass 不需要 depth：

```cpp
renderingInfo.pDepthAttachment = nullptr;
renderingInfo.pStencilAttachment = nullptr;
```

begin rendering 后：

```cpp
vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline);
vkCmdBindDescriptorSets(
    commandBuffer,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    blitPipelineLayout,
    0,
    1,
    &blitDescriptorSet,
    0,
    nullptr);

vkCmdDraw(commandBuffer, 3, 1, 0, 0);
```

最后 transition swapchain 到 present：

```cpp
vkutil::cmdTransitionImageLayout(
    commandBuffer,
    swapChain.images[imageIndex],
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    VK_IMAGE_ASPECT_COLOR_BIT);
```

## 13. prepare 顺序

建议顺序：

```cpp
void VulkanExample::prepare() {
    createVmaAllocator();

    VulkanExampleBase::prepare();

    createSynchronizationPrimitives();
    createCommandBuffers();
    createVertexBuffer();
    createUniformBuffers();
    loadTexture();
    createOffscreenResources();
    createDescriptors();
    createPipeline();
    createBlitDescriptors();
    createBlitPipeline();
    prepared = true;
}
```

为什么 `createOffscreenResources()` 在 `createBlitDescriptors()` 前？

因为 blit descriptor 要写入：

```cpp
offscreenColor.descriptor
```

为什么 `createDescriptors()` 在 `createBlitDescriptors()` 前？

因为 descriptor pool 在 `createDescriptors()` 里创建。

## 14. resize 处理

offscreen image 尺寸跟窗口一致，所以 resize 时必须重建。

在 `lab0.h`：

```cpp
void windowResized() override;
```

实现：

```cpp
void VulkanExample::windowResized() {
    destroyOffscreenResources();
    createOffscreenResources();
    updateBlitDescriptor();
}
```

基类 `windowResize()` 已经做了：

```text
vkDeviceWaitIdle
createSwapChain
setupDepthStencil
setupFrameBuffer
windowResized
```

所以这里不需要自己再调用 `vkDeviceWaitIdle()`。

如果你发现 resize 时 descriptor 指向旧 image view，通常就是忘了：

```cpp
updateBlitDescriptor();
```

## 15. 析构顺序

析构里新增：

```cpp
vkutil::destroyTexture(device, allocator, offscreenColor);

vkDestroyPipeline(device, blitPipeline, nullptr);
vkDestroyPipelineLayout(device, blitPipelineLayout, nullptr);
vkDestroyDescriptorSetLayout(device, blitDescriptorSetLayout, nullptr);
```

注意：

```text
offscreenColor 必须在 destroyVmaAllocator 前销毁
blit descriptor set 不需要单独 free，descriptorPool 会统一销毁
```

推荐顺序：

```text
destroy scene pipeline
destroy blit pipeline
destroy descriptor set layouts
destroy textures/images/buffers
destroy VMA allocator
```

## 16. 编译和运行

编译 HLSL：

```powershell
Push-Location shaders/hlsl
python compileshaders.py --sample lab0
Pop-Location
```

编译 C++：

```powershell
cmake --build build --config Debug --target lab0 -j 32
```

运行：

```powershell
build\bin\Debug\lab0.exe -v -vl
```

成功标准：

```text
画面仍然显示两个圆盘
validation layer 没有 layout/descriptor 错误
resize 后画面仍然正常
关闭程序没有资源销毁错误
```

如果画面全黑：

```text
检查 pass 1 是否真的 draw 到 offscreenColor
检查 offscreenColor 是否 transition 到 SHADER_READ_ONLY_OPTIMAL
检查 blitDescriptorSet 是否绑定 offscreenColor.descriptor
检查 fullscreen shader 是否编译并加载
```

如果 validation 报 descriptor image layout 不匹配：

```text
检查 offscreenColor.descriptor.imageLayout 是否是 SHADER_READ_ONLY_OPTIMAL
检查 pass 1 结束后是否更新 offscreenColor.image.layout
```

如果 resize 崩溃：

```text
检查 destroyOffscreenResources()
检查 createOffscreenResources()
检查 updateBlitDescriptor()
```

## 17. 跑通后的第一个小实验

跑通后，在 `fullscreen.frag` 里做一个最简单的 post-process：

```hlsl
float3 color = offscreenTexture.Sample(offscreenSampler, input.UV).rgb;
float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
return float4(luminance.xxx, 1.0);
```

这就是灰度后处理。

如果它能正常显示，说明：

```text
你已经拥有了一个真正的 post-process pipeline
```

下一步就可以做：

```text
blur
edge detect
bloom threshold
tone mapping
```

## 18. 目前架构是否足够支撑后续新 example？

结论：足够支撑后续实验起步，而且方向是对的。

你现在已经有了这些可复用基础：

```text
vk_types
-> AllocatedBuffer / AllocatedImage / AllocatedTexture / GPUMeshBuffers

vk_commands
-> immediateSubmit

vk_resources
-> createAllocatedBuffer / createDeviceLocalBuffer / destroyAllocatedBuffer

vk_images
-> createAllocatedImage / destroyAllocatedImage / texture upload / layout transition
```

这些已经足够支撑：

```text
新建 Lab1/Lab2 example
上传 mesh
上传 uniform
上传 procedural texture
创建 depth image
创建 offscreen render target
做最小 post-process
做 shadow map 的第一版
做 deferred G-buffer 的第一版
```

但还不是“完整实验框架”。

后续最值得补的工具层是：

```text
vk_descriptors
-> descriptor set layout / descriptor write helper

vk_pipeline_builder
-> graphics pipeline create info 的整理

vk_shader
-> shader module load 和 shader path 管理

vk_images::loadKtxTexture2D
-> 复用 KTX 解析，但返回你的 AllocatedTexture

RenderTarget / RenderPassResources
-> 管 offscreen color/depth image 的创建、resize、descriptor 更新
```

我的建议是：

```text
先用 Lab0 完成 Step 6
然后新开 Lab1，复用这些 base helper
Lab1 不再从 triangle 原版复制，而是从你自己的 vkutil 起步
```

也就是说：

```text
Lab0 是你搭工具层的训练场
Lab1 开始就是你真正的图形实验室模板
```

现在这套辅助函数已经足够让你开始新 example 了。
不要等框架“完美”再开始实验，那会变成建引擎而不是学图形。

