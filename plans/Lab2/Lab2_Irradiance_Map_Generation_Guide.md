# Lab2 Irradiance Map 生成改造指南

这份文档指导你把当前 Lab2 的 irradiance map 生成流程改成稳定、清晰、可扩展的版本。目标不是“把代码抄到能跑”，而是让你真正理解 IBL 里运行时生成 cubemap 的数据流。你之后做 prefiltered environment map、BRDF LUT、bloom、shadow atlas，本质上都会反复遇到类似的 image layout、offscreen、copy 和 descriptor 问题。

## 1. 当前问题在哪里

你现在的方向已经对了一半：

```text
load environment cubemap
create irradiance cubemap
render skybox with convolution shader
scene shader sample irradiance map
```

但当前 `generateIrradianceCubeMap()` 里还有一个核心结构问题：你尝试直接把 `textures.irradianceCubeMap.view` 当作 color attachment 渲染。

```cpp
VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
    textures.irradianceCubeMap.view,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    ...
);
```

这不适合你当前阶段。`textures.irradianceCubeMap.view` 是一个 `VK_IMAGE_VIEW_TYPE_CUBE`，它代表整张 cubemap，而不是某一个 face/mip。你的循环虽然有：

```cpp
for (uint32_t m = 0; m < numMips; ++m) {
    for (uint32_t f = 0; f < 6; ++f) {
        ...
    }
}
```

但循环里的 `m` 和 `f` 只改变了 viewport 和矩阵，并没有真正告诉 Vulkan “这次写入 cubemap 的第 f 个面、第 m 个 mip”。所以它不是一个正确的 cubemap 生成流程。

## 2. 推荐的数据流

先采用原工程 `pbribl.cpp` 的稳定方案：

```text
environmentCubeMap
    shader sampled
        |
        v
offscreen 2D image
    COLOR_ATTACHMENT / ATTACHMENT_OPTIMAL
        |
        v
copy to irradianceCubeMap face/mip
    TRANSFER_SRC -> TRANSFER_DST
        |
        v
irradianceCubeMap
    SHADER_READ_ONLY_OPTIMAL
```

关键点是：不要直接画到 cubemap。先画到一张普通 2D offscreen image，再把它 copy 到 cubemap 的指定 face/mip。

这样你每次循环真正做的是：

```text
第 0 个 mip，第 +X 面：画到 offscreen，再 copy 到 cubemap face 0 mip 0
第 0 个 mip，第 -X 面：画到 offscreen，再 copy 到 cubemap face 1 mip 0
...
第 1 个 mip，第 +X 面：画到 offscreen，再 copy 到 cubemap face 0 mip 1
```

这条路虽然多一个中转 image，但最容易 debug，也最适合你现在学习 Vulkan。

## 3. 资源设计

需要两类 image。

第一类是最终结果：`irradianceCubeMap`。

```cpp
textures.irradianceCubeMap = vkutil::createAllocatedCubeTexture(
    device,
    allocator,
    dim,
    numMips,
    format,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT
);
```

它需要：

```text
VK_IMAGE_USAGE_TRANSFER_DST_BIT
VK_IMAGE_USAGE_SAMPLED_BIT
```

其中 `SAMPLED_BIT` 已经在你的 `createAllocatedCubeTexture` helper 里自动补上了。

第二类是中转结果：`offscreen`。

```cpp
AllocatedImage offscreen = vkutil::createAllocatedImage(
    device,
    allocator,
    { dim, dim, 1 },
    format,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    VK_IMAGE_ASPECT_COLOR_BIT
);
```

它需要：

```text
VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
VK_IMAGE_USAGE_TRANSFER_SRC_BIT
```

因为它先作为 color attachment 被渲染，然后作为 copy 源复制到 cubemap。

## 4. Layout 状态机

这一块是你当前最需要建立肌肉记忆的地方。

`irradianceCubeMap` 的生命周期：

```text
UNDEFINED
    -> TRANSFER_DST_OPTIMAL
        循环中持续接收 copy
    -> SHADER_READ_ONLY_OPTIMAL
        后续 PBR shader 采样
```

`offscreen` 的生命周期：

```text
UNDEFINED
    -> ATTACHMENT_OPTIMAL
        每轮渲染到 offscreen
    -> TRANSFER_SRC_OPTIMAL
        copy 到 cubemap
    -> ATTACHMENT_OPTIMAL
        下一轮继续渲染
```

注意统一使用 `VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL`。你现在的 dynamic rendering helper 和 `cmdTransitionImageLayout` 都更偏向 Vulkan 1.3 的 `ATTACHMENT_OPTIMAL` 风格。不要一会儿用 `COLOR_ATTACHMENT_OPTIMAL`，一会儿用 `ATTACHMENT_OPTIMAL`。

## 5. 准备顺序建议

建议把 `prepare()` 调整成这种逻辑：

```cpp
void VulkanExample::prepare() {
    createVmaAllocator();
    VulkanExampleBase::prepare();

    loadAssets();

    generateIrradianceCubeMap();
    generatePrefilteredCubeMap();
    generateBRDFLUT();

    createUniformBuffers();
    createDescriptorsPool();
    setupDescriptors();
    createPipelines();

    prepared = true;
}
```

这里的原则是：

```text
先生成 IBL 贴图，再把它们写进 scene descriptor。
```

更推荐的做法是：`generateIrradianceCubeMap()` 内部自己创建临时 descriptor pool、descriptor set layout、descriptor set。不要复用主渲染的 `descriptorPool`。这样 IBL 预计算阶段和主渲染阶段是解耦的。

## 6. generateIrradianceCubeMap 的推荐结构

建议按下面的顺序重写。

### 6.1 创建最终 cubemap

```cpp
const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
const uint32_t dim = 64;
const uint32_t numMips = static_cast<uint32_t>(std::floor(std::log2(dim))) + 1;

textures.irradianceCubeMap = vkutil::createAllocatedCubeTexture(
    device,
    allocator,
    dim,
    numMips,
    format,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT
);
```

先用 `R16G16B16A16_SFLOAT` 就够了。原工程用 `R32G32B32A32_SFLOAT`，精度更高但更重。学习阶段 `R16G16B16A16_SFLOAT` 更平衡。

### 6.2 创建 offscreen 2D image

```cpp
AllocatedImage offscreen = vkutil::createAllocatedImage(
    device,
    allocator,
    { dim, dim, 1 },
    format,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    VK_IMAGE_ASPECT_COLOR_BIT
);
```

### 6.3 创建临时 descriptor

这里 descriptor 只服务于 irradiance 生成阶段：fragment shader 需要采样 `environmentCubeMap`。

```cpp
VkDescriptorSetLayoutBinding binding =
    vks::initializers::descriptorSetLayoutBinding(
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0
    );
```

然后创建临时：

```text
descriptor pool
descriptor set layout
descriptor set
write environmentCubeMap.descriptor
```

生成完成后就销毁这些临时对象。这样它们不会污染主 scene descriptor。

### 6.4 创建 irradiance pipeline

这个 pipeline 用 dynamic rendering，所以：

```cpp
VkPipelineRenderingCreateInfo renderingCreateInfo{
    VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO
};
renderingCreateInfo.colorAttachmentCount = 1;
renderingCreateInfo.pColorAttachmentFormats = &format;
renderingCreateInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
```

并且：

```cpp
pipelineCI.pNext = &renderingCreateInfo;
pipelineCI.renderPass = VK_NULL_HANDLE;
```

这个你现在已经基本做对了。

### 6.5 开始 command buffer

先 transition 两张图：

```cpp
VkImageSubresourceRange cubeRange = vkutil::cubeSubresourceRange(numMips);

vkutil::cmdTransitionImageLayout(
    cmd,
    textures.irradianceCubeMap.image,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    cubeRange
);

vkutil::cmdTransitionImageLayout(
    cmd,
    offscreen.image,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_ASPECT_COLOR_BIT
);
```

### 6.6 循环 6 个面和 mip

伪代码如下：

```cpp
for (uint32_t mip = 0; mip < numMips; ++mip) {
    const uint32_t mipDim = static_cast<uint32_t>(dim * std::pow(0.5f, mip));

    for (uint32_t face = 0; face < 6; ++face) {
        VkExtent2D renderExtent{ mipDim, mipDim };

        VkRenderingAttachmentInfo colorAttachment =
            vkutil::renderingAttachmentInfo(
                offscreen.imageView,
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                VkClearValue{{ 0.0f, 0.0f, 0.0f, 1.0f }}
            );

        vkutil::cmdBeginColorOnlyRendering(cmd, renderExtent, colorAttachment);
        {
            vkutil::cmdSetViewportAndScissor(cmd, mipDim, mipDim);

            pushBlock.mvp = captureProjection * captureViews[face];
            vkCmdPushConstants(...);

            vkCmdBindPipeline(...);
            vkCmdBindDescriptorSets(...);
            skyboxCube.draw(cmd);
        }
        vkutil::cmdEndRendering(cmd);

        vkutil::cmdTransitionImageLayout(
            cmd,
            offscreen.image,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.mipLevel = 0;
        copyRegion.srcSubresource.baseArrayLayer = 0;
        copyRegion.srcSubresource.layerCount = 1;

        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.mipLevel = mip;
        copyRegion.dstSubresource.baseArrayLayer = face;
        copyRegion.dstSubresource.layerCount = 1;

        copyRegion.extent = { mipDim, mipDim, 1 };

        vkCmdCopyImage(
            cmd,
            offscreen.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            textures.irradianceCubeMap.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion
        );

        vkutil::cmdTransitionImageLayout(
            cmd,
            offscreen.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
    }
}
```

这里最重要的是 `copyRegion.dstSubresource`：

```cpp
copyRegion.dstSubresource.mipLevel = mip;
copyRegion.dstSubresource.baseArrayLayer = face;
```

这两行才是真正把数据写入 cubemap 不同 face/mip 的地方。

### 6.7 结束时转为 shader read

```cpp
vkutil::cmdTransitionImageLayout(
    cmd,
    textures.irradianceCubeMap.image,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    cubeRange
);

textures.irradianceCubeMap.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
textures.irradianceCubeMap.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
```

注意这里 old layout 应该写真实状态 `TRANSFER_DST_OPTIMAL`，不要写 `textures.irradianceCubeMap.layout`，除非你在每次 transition 后都同步维护这个字段。

### 6.8 清理临时资源

```cpp
vkDestroyPipeline(device, pipeline, nullptr);
vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
vkDestroyDescriptorPool(device, tempDescriptorPool, nullptr);
vkDestroyDescriptorSetLayout(device, tempDescriptorSetLayout, nullptr);
vkutil::destroyAllocatedImage(device, allocator, offscreen);
```

注意：`textures.irradianceCubeMap` 不要销毁，它是主渲染阶段要采样的资源。

## 7. Shader 侧先改成原工程公式

你当前 `irradianceMap.frag` 的卷积公式有两个问题：

```hlsl
float3 localCoordinate = N + up + right;
tagentVec = tagentVec * localCoordinate;
```

这不是正确的 tangent space 到 world space 转换。建议先直接用原工程写法：

```hlsl
float3 N = normalize(input.UVW);
float3 up = float3(0.0, 1.0, 0.0);
float3 right = normalize(cross(up, N));
up = cross(N, right);

float3 color = float3(0.0, 0.0, 0.0);
uint sampleCount = 0;

for (float phi = 0.0; phi < 2.0 * PI; phi += pushConstants.deltaPhi) {
    for (float theta = 0.0; theta < 0.5 * PI; theta += pushConstants.deltaTheta) {
        float3 tempVec = cos(phi) * right + sin(phi) * up;
        float3 sampleVector = cos(theta) * N + sin(theta) * tempVec;
        color += envCube.Sample(envSampler, sampleVector).rgb * cos(theta) * sin(theta);
        sampleCount++;
    }
}

color = PI * color / float(sampleCount);
```

先不要追求自己推导版本。等跑通后，再回来推数学。我们现在的目标是把 Vulkan 数据流和 IBL 数据流同时稳住。

## 8. Scene Shader 接入

scene descriptor binding 2 继续给 irradiance map：

```cpp
VkDescriptorImageInfo irradianceImageInfo =
    textures.irradianceCubeMap.descriptor;
```

或者：

```cpp
VkDescriptorImageInfo irradianceImageInfo = vkutil::descriptorImageInfo(
    textures.irradianceCubeMap.sampler,
    textures.irradianceCubeMap.view,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
);
```

fragment shader 里先 debug 输出：

```hlsl
float3 irradiance = irradianceMap.Sample(irradianceMapSampler, N).rgb;
output.color = float4(irradiance, 1.0);
```

你应该看到一个非常柔和的环境颜色，不应该看到清晰天空盒细节。

确认成功后再接入 PBR diffuse IBL：

```hlsl
float3 diffuseIBL = irradiance * albedo;
```

## 9. 最小实现阶段

为了减少一次性爆炸，建议分两阶段。

### 阶段 A：只生成 mip0

先把 `numMips` 暂时设为 1：

```cpp
const uint32_t numMips = 1;
```

这样你只需要确认 6 个 face 正确写入。能显示正常的 irradiance debug 后，再恢复多 mip。

### 阶段 B：恢复所有 mip

恢复：

```cpp
const uint32_t numMips = static_cast<uint32_t>(std::floor(std::log2(dim))) + 1;
```

然后检查：

```text
mipDim 是否正确递减
copyRegion.extent 是否使用 mipDim
copyRegion.dstSubresource.mipLevel 是否等于 mip
```

## 10. 常见报错定位

如果报 `imageView is VK_NULL_HANDLE`：

```text
descriptor 写入发生在 cubemap 创建之前
或者 createAllocatedCubeTexture 返回值没有保存
```

如果报 `unsupported layout transition!`：

```text
cmdTransitionImageLayout 没支持这组 old/new layout
或者你混用了 COLOR_ATTACHMENT_OPTIMAL 和 ATTACHMENT_OPTIMAL
```

如果 validation 报 attachment layout 不匹配：

```text
VkRenderingAttachmentInfo.imageLayout
和你实际 transition 到的 layout 不一致
```

如果画面全黑：

```text
先 debug 输出 environmentCubeMap，确认输入 cubemap 能采样
再 debug 输出 irradiance map，确认生成结果非黑
检查 copyRegion 的 face/mip 是否写对
检查 shader 里的采样方向是否 normalize
```

如果结果像清晰天空盒：

```text
卷积 shader 没有真正做半球积分
或者采样方向转换错了
```

如果结果特别暗：

```text
积分权重可能漏了 PI
采样范围可能错成了整球或方向错乱
tone mapping/gamma 也可能影响观察
```

## 11. 你现在最应该改的三件事

第一，把 `generateIrradianceCubeMap()` 改成 offscreen 2D 中转，不要直接把 cubemap view 当 attachment。

第二，统一 layout 使用：

```text
dynamic rendering attachment: VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL
copy source: VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
copy destination: VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
shader sample: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
```

第三，先 `numMips = 1` 跑通 mip0 的 6 个 face。等 debug view 正常，再恢复完整 mip 链。

## 12. 图形工程师视角的理解

你现在卡住的不是 PBR 数学，而是渲染器资源流。这正是图形工程师和“只写 shader demo”的区别。

这一步你真正要掌握的是：

```text
谁生产资源
谁消费资源
生产时是什么 layout
消费时是什么 layout
是否需要中转资源
descriptor 什么时候写入
资源生命周期归谁管理
```

Irradiance map 是一个很好的训练点，因为它同时包含：

```text
采样已有 cubemap
offscreen 渲染
image layout transition
copy image 到 cubemap face/mip
生成资源再交给主渲染采样
```

把这条链跑通之后，你做 prefiltered environment map 会轻松很多。因为 prefiltered map 的数据流几乎一样，只是 fragment shader 的采样公式和 mip/roughness 的关系更复杂。
