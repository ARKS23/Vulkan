# Lab2 Physically Based Bloom 实现指导

本文档按 LearnOpenGL 的 [Phys. Based Bloom](https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom) 思路，为当前 `Lab2` 设计一版更适合 PBR/IBL 的 Bloom 改造方案。

它和传统 Bloom 最大的区别是：**不做亮度阈值提取，不单独做 bright pass，而是直接对整张 HDR 场景做 mip-chain downsample，再逐级 additive upsample，最后混回 HDR 场景。**

当前 Lab2 已经有 PBR、IBL、glTF 贴图、emissive、自发光和动态渲染，这正好适合进入这一版 Bloom。

## 1. 这篇文章的 Bloom 思路

传统 Bloom 通常是：

```text
HDR Scene
  -> Bright Pass / Threshold
  -> Gaussian Blur Ping-Pong
  -> Add Back
  -> Tone Mapping
```

LearnOpenGL 这篇 Physically Based Bloom 的路线是：

```text
HDR Scene
  -> Downsample Mip Chain
  -> Upsample Mip Chain with Additive Blend
  -> Mix Bloom with HDR Scene
  -> Tone Mapping + Gamma
```

核心区别：

- 传统 Bloom 先找“亮的地方”，物理 Bloom 直接处理整张 HDR 图。
- 传统 Bloom 很依赖 threshold，物理 Bloom 更依赖 HDR 场景真实亮度。
- 传统 Bloom 常用两张图横向/纵向反复 blur，物理 Bloom 用 mip chain 获得大范围低频扩散。
- 物理 Bloom 的强度通常很小，更多是“镜头/视觉扩散”的感觉，而不是一层明显特效。

你现在的 PBR/IBL 场景里有高亮反射、自发光、光源球和 HDR skybox，正是这种 Bloom 的理想输入。

## 2. 当前 Lab2 的关键问题

当前 `examples/lab2/lab2.cpp` 的主流程大概是：

```cpp
cmdDrawSecne(commandBuffer);
cmdDrawPBRTexture(commandBuffer);
cmdDrawLight(commandBuffer);
cmdDrawSkybox(commandBuffer);
```

这几个 pass 现在都直接写到：

```cpp
swapChain.imageViews[currentImageIndex]
```

但 Bloom 需要在 tone mapping 之前处理 HDR 能量，所以第一步必须改成：

```text
Scene/PBR/Skybox/Light -> HDR offscreen color
HDR offscreen color -> Bloom mip chain
HDR offscreen color + Bloom -> Swapchain
```

另外你当前 shader 中还有几个地方提前做了 tone mapping：

- `shaders/hlsl/lab2/PBRTexture.frag`
- `shaders/hlsl/lab2/pbrScene.frag`
- `shaders/hlsl/lab2/skybox.frag`

例如 `PBRTexture.frag` 里目前有：

```hlsl
color = color / (color + 1.0);
color = ACESFilm(color);
output.color = float4(color, 1.0);
```

做 Bloom 前要把这些移走。材质 shader 应该输出 **linear HDR**，最终只在 composite shader 中做一次 tone mapping。

## 3. 推荐数据流

第一版建议采用下面的数据流：

```text
PBR scene / textured glTF / light spheres / skybox
        |
        v
hdrSceneColor
VK_FORMAT_R16G16B16A16_SFLOAT
        |
        v
Bloom Downsample:
hdrSceneColor -> bloomMip[0] -> bloomMip[1] -> bloomMip[2] -> ...
        |
        v
Bloom Upsample:
bloomMip[n] -> bloomMip[n-1] -> ... -> bloomMip[0]
additive blending
        |
        v
Composite:
hdrSceneColor + bloomMip[0] * strength
        |
        v
ACES + gamma -> swapchain
```

格式建议：

| 资源 | 格式 | 用途 |
|---|---|---|
| `hdrSceneColor` | `VK_FORMAT_R16G16B16A16_SFLOAT` | 保存完整 HDR 场景 |
| `bloomMip[i]` | `VK_FORMAT_R16G16B16A16_SFLOAT` | 保存 downsample/upsample 中间结果 |
| `depthStencil` | 当前 `depthFormat` | 场景深度 |
| `swapchain` | 当前 `swapChain.colorFormat` | 最终显示 |

文章里更偏向节省带宽的 HDR 格式，例如 `R11G11B10F`。但学习阶段建议先用 `R16G16B16A16_SFLOAT`，调试最稳。

## 4. Lab2 需要新增的资源

建议在 `lab2.h` 中添加：

```cpp
struct BloomMip {
    AllocatedImage image;
    VkDescriptorImageInfo descriptor{};
    VkExtent2D extent{};
};

struct BloomResources {
    AllocatedImage hdrSceneColor;
    VkDescriptorImageInfo hdrSceneDescriptor{};

    std::vector<BloomMip> mips;
    VkSampler sampler = VK_NULL_HANDLE;

    VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkExtent2D sceneExtent{};
    uint32_t mipCount = 5;
};
```

然后在 `VulkanExample` 中添加：

```cpp
BloomResources bloom;

bool enableBloom = true;
float exposure = 1.0f;
float bloomStrength = 0.04f;
float bloomFilterRadius = 0.005f;
int bloomMipCount = 5;
```

为什么推荐每个 mip 用一张独立 `AllocatedImage`？

- 你当前的 `vkutil::createAllocatedImage()` 最适合创建单 mip image。
- 每个 mip 都可以直接有一个 image view 和 descriptor。
- Dynamic Rendering 下每个 mip 当作 color attachment 很直观。
- 后续熟悉后，再升级成“一张 image 多个 mip + 每 mip 一个 image view”。

## 5. 创建 Bloom 资源

新增函数：

```cpp
void createBloomResources();
void destroyBloomResources();
```

示例代码：

```cpp
void VulkanExample::createBloomResources() {
    bloom.hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    bloom.sceneExtent = { width, height };
    bloom.mipCount = static_cast<uint32_t>(bloomMipCount);

    bloom.hdrSceneColor = vkutil::createAllocatedImage(
        device,
        allocator,
        VkExtent3D{ width, height, 1 },
        bloom.hdrFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo samplerCI = vks::initializers::samplerCreateInfo();
    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = 1.0f;
    VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &bloom.sampler));

    bloom.hdrSceneDescriptor = vkutil::descriptorImageInfo(
        bloom.sampler,
        bloom.hdrSceneColor.imageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    bloom.mips.clear();

    uint32_t mipWidth = width / 2;
    uint32_t mipHeight = height / 2;

    for (uint32_t i = 0; i < bloom.mipCount; ++i) {
        mipWidth = std::max(1u, mipWidth);
        mipHeight = std::max(1u, mipHeight);

        BloomMip mip{};
        mip.extent = { mipWidth, mipHeight };
        mip.image = vkutil::createAllocatedImage(
            device,
            allocator,
            VkExtent3D{ mipWidth, mipHeight, 1 },
            bloom.hdrFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        mip.descriptor = vkutil::descriptorImageInfo(
            bloom.sampler,
            mip.image.imageView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        bloom.mips.push_back(mip);

        mipWidth /= 2;
        mipHeight /= 2;
    }
}
```

销毁：

```cpp
void VulkanExample::destroyBloomResources() {
    for (BloomMip& mip : bloom.mips) {
        vkutil::destroyAllocatedImage(device, allocator, mip.image);
    }
    bloom.mips.clear();

    vkutil::destroyAllocatedImage(device, allocator, bloom.hdrSceneColor);

    if (bloom.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, bloom.sampler, nullptr);
        bloom.sampler = VK_NULL_HANDLE;
    }
}
```

调用顺序建议：

```cpp
void VulkanExample::prepare() {
    createVmaAllocator();
    VulkanExampleBase::prepare();
    loadAssets();
    createDescriptorsPool();

    generateIrradianceCubeMap();
    generatePrefilteredCubeMap();
    generateBRDFLUT();

    createBloomResources();
    createUniformBuffers();
    setupDescriptors();
    createPipelines();

    prepared = true;
}
```

## 6. Descriptor 设计

Bloom 的 downsample/upsample 都是 fullscreen pass，只需要采样一张输入 texture。

建议新增一个通用 layout：

```cpp
VkDescriptorSetLayout bloomSingleTextureSetLayout = VK_NULL_HANDLE;
```

binding：

```cpp
binding 0: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
stage: VK_SHADER_STAGE_FRAGMENT_BIT
```

Composite 需要同时采样 HDR scene 和 bloom 结果：

```cpp
VkDescriptorSetLayout bloomCompositeSetLayout = VK_NULL_HANDLE;
```

binding：

```cpp
binding 0: hdrSceneColor
binding 1: bloom.mips[0]
```

建议每帧保存：

```cpp
struct BloomDescriptorSets {
    VkDescriptorSet hdrSceneSet = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> mipSets;
    VkDescriptorSet compositeSet = VK_NULL_HANDLE;
};

std::array<BloomDescriptorSets, maxConcurrentFrames> bloomDescriptorSets;
```

绑定关系：

| pass | 输入 descriptor |
|---|---|
| downsample mip 0 | `hdrSceneSet` |
| downsample mip i | `mipSets[i - 1]` |
| upsample i -> i - 1 | `mipSets[i]` |
| composite | `hdrSceneColor + mipSets[0]` |

注意：descriptor 中的 `imageLayout` 可以固定写 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`，但 command buffer 里要确保真正采样前已经 transition 到这个 layout。

## 7. Pipeline 设计

新增三条 pipeline：

```cpp
VkPipeline bloomDownsamplePipeline = VK_NULL_HANDLE;
VkPipeline bloomUpsamplePipeline = VK_NULL_HANDLE;
VkPipeline bloomCompositePipeline = VK_NULL_HANDLE;
```

对应 layout：

```cpp
VkPipelineLayout bloomDownsamplePipelineLayout = VK_NULL_HANDLE;
VkPipelineLayout bloomUpsamplePipelineLayout = VK_NULL_HANDLE;
VkPipelineLayout bloomCompositePipelineLayout = VK_NULL_HANDLE;
```

场景相关 pipeline 要从 swapchain format 改成 HDR format：

```cpp
// createScenePipeline()
// createPBRTexturePipeline()
// createSkyboxPipeline()
// createLightPipeline()
.setColorAttachmentFormat(bloom.hdrFormat)
```

Bloom pipeline 用 fullscreen triangle：

```cpp
.setEmptyVertexInput()
.disableDepthTest()
.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
```

Downsample：

```cpp
.setColorAttachmentFormat(bloom.hdrFormat)
.disableBlending()
```

Upsample：

```cpp
.setColorAttachmentFormat(bloom.hdrFormat)
.enableAdditiveBlending()
```

Composite：

```cpp
.setColorAttachmentFormat(swapChain.colorFormat)
.disableBlending()
```

你的 `PipelineBuilder` 里已经有 `setEmptyVertexInput()`、`enableAdditiveBlending()`、`disableDepthTest()`，很适合接这套流程。

## 8. 新增 Shader 文件

建议新增：

```text
shaders/hlsl/lab2/fullscreen.vert
shaders/hlsl/lab2/bloomDownsample.frag
shaders/hlsl/lab2/bloomUpsample.frag
shaders/hlsl/lab2/bloomComposite.frag
```

新增 shader 后记得重新 configure：

```powershell
cmake -S . -B build
cmake --build build --config Debug --target lab2 -j 32
```

### fullscreen.vert

```hlsl
struct VSOutput {
    float4 position : SV_POSITION;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexIndex : SV_VertexID) {
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
    output.position = float4(positions[vertexIndex], 0.0, 1.0);
    output.uv = uvs[vertexIndex];
    return output;
}
```

## 9. Downsample Shader

文章中的 downsample 使用 13-tap filter，并且第一层可以用 Karis average 抑制 HDR fireflies。

```hlsl
struct FSInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

Texture2D srcTexture : register(t0, space0);
SamplerState srcSampler : register(s0, space0);

struct PushConstants {
    float2 srcResolution;
    uint mipLevel;
    float padding;
};
[[vk::push_constant]] PushConstants pc;

float RGBToLuminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float KarisAverage(float3 color) {
    float luma = RGBToLuminance(color) * 0.25;
    return 1.0 / (1.0 + luma);
}

FSOutput main(FSInput input) {
    FSOutput output;

    float2 texelSize = 1.0 / pc.srcResolution;
    float x = texelSize.x;
    float y = texelSize.y;
    float2 uv = input.uv;

    float3 a = srcTexture.Sample(srcSampler, uv + float2(-2.0 * x,  2.0 * y)).rgb;
    float3 b = srcTexture.Sample(srcSampler, uv + float2( 0.0,      2.0 * y)).rgb;
    float3 c = srcTexture.Sample(srcSampler, uv + float2( 2.0 * x,  2.0 * y)).rgb;
    float3 d = srcTexture.Sample(srcSampler, uv + float2(-2.0 * x,  0.0)).rgb;
    float3 e = srcTexture.Sample(srcSampler, uv).rgb;
    float3 f = srcTexture.Sample(srcSampler, uv + float2( 2.0 * x,  0.0)).rgb;
    float3 g = srcTexture.Sample(srcSampler, uv + float2(-2.0 * x, -2.0 * y)).rgb;
    float3 h = srcTexture.Sample(srcSampler, uv + float2( 0.0,     -2.0 * y)).rgb;
    float3 i = srcTexture.Sample(srcSampler, uv + float2( 2.0 * x, -2.0 * y)).rgb;
    float3 j = srcTexture.Sample(srcSampler, uv + float2(-x,  y)).rgb;
    float3 k = srcTexture.Sample(srcSampler, uv + float2( x,  y)).rgb;
    float3 l = srcTexture.Sample(srcSampler, uv + float2(-x, -y)).rgb;
    float3 m = srcTexture.Sample(srcSampler, uv + float2( x, -y)).rgb;

    float3 downsample;

    if (pc.mipLevel == 0) {
        float3 groups[5];
        groups[0] = (a + b + d + e) * (0.125 / 4.0);
        groups[1] = (b + c + e + f) * (0.125 / 4.0);
        groups[2] = (d + e + g + h) * (0.125 / 4.0);
        groups[3] = (e + f + h + i) * (0.125 / 4.0);
        groups[4] = (j + k + l + m) * (0.5 / 4.0);

        groups[0] *= KarisAverage(groups[0]);
        groups[1] *= KarisAverage(groups[1]);
        groups[2] *= KarisAverage(groups[2]);
        groups[3] *= KarisAverage(groups[3]);
        groups[4] *= KarisAverage(groups[4]);

        downsample = groups[0] + groups[1] + groups[2] + groups[3] + groups[4];
    } else {
        downsample = e * 0.125;
        downsample += (a + c + g + i) * 0.03125;
        downsample += (b + d + f + h) * 0.0625;
        downsample += (j + k + l + m) * 0.125;
    }

    downsample = max(downsample, 0.0001.xxx);
    output.color = float4(downsample, 1.0);
    return output;
}
```

说明：

- `pc.srcResolution` 是输入 texture 的分辨率，不是输出 mip 的分辨率。
- `pc.mipLevel == 0` 表示第一次从 full HDR scene 降采样到 half size。
- `max(downsample, 0.0001)` 可以减少黑块扩散。
- 如果第一版觉得 Karis average 让画面偏暗，可以先全部走普通 downsample，后续再恢复。

## 10. Upsample Shader

Upsample 使用 tent filter，配合 additive blending 写回更高一级 mip。

```hlsl
struct FSInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

Texture2D srcTexture : register(t0, space0);
SamplerState srcSampler : register(s0, space0);

struct PushConstants {
    float filterRadius;
    float3 padding;
};
[[vk::push_constant]] PushConstants pc;

FSOutput main(FSInput input) {
    FSOutput output;

    float x = pc.filterRadius;
    float y = pc.filterRadius;
    float2 uv = input.uv;

    float3 a = srcTexture.Sample(srcSampler, uv + float2(-x,  y)).rgb;
    float3 b = srcTexture.Sample(srcSampler, uv + float2( 0,  y)).rgb;
    float3 c = srcTexture.Sample(srcSampler, uv + float2( x,  y)).rgb;
    float3 d = srcTexture.Sample(srcSampler, uv + float2(-x,  0)).rgb;
    float3 e = srcTexture.Sample(srcSampler, uv).rgb;
    float3 f = srcTexture.Sample(srcSampler, uv + float2( x,  0)).rgb;
    float3 g = srcTexture.Sample(srcSampler, uv + float2(-x, -y)).rgb;
    float3 h = srcTexture.Sample(srcSampler, uv + float2( 0, -y)).rgb;
    float3 i = srcTexture.Sample(srcSampler, uv + float2( x, -y)).rgb;

    float3 upsample = e * 4.0;
    upsample += (b + d + f + h) * 2.0;
    upsample += (a + c + g + i);
    upsample *= 1.0 / 16.0;

    output.color = float4(upsample, 1.0);
    return output;
}
```

`filterRadius` 推荐从下面开始：

```cpp
bloomFilterRadius = 0.005f;
```

它不是像素单位，而是 UV 空间单位。数值太大时，Bloom 会变成一层灰雾。

## 11. Composite Shader

Composite 负责：

1. 读取原始 HDR scene。
2. 读取最终 bloom result，也就是 `bloom.mips[0]`。
3. 把 bloom 以很小的比例混入 HDR。
4. 做 exposure、ACES 和 gamma。

```hlsl
struct FSInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

Texture2D hdrScene : register(t0, space0);
SamplerState hdrSampler : register(s0, space0);

Texture2D bloomTexture : register(t1, space0);
SamplerState bloomSampler : register(s1, space0);

struct PushConstants {
    float exposure;
    float bloomStrength;
    uint enableBloom;
    float padding;
};
[[vk::push_constant]] PushConstants pc;

float3 ACESFilm(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

FSOutput main(FSInput input) {
    FSOutput output;

    float3 hdr = hdrScene.Sample(hdrSampler, input.uv).rgb;
    float3 bloom = bloomTexture.Sample(bloomSampler, input.uv).rgb;

    float strength = pc.enableBloom != 0 ? pc.bloomStrength : 0.0;
    float3 color = lerp(hdr, hdr + bloom, strength);

    color *= pc.exposure;
    color = ACESFilm(color);

    // 如果 swapchain 不是 SRGB 格式，需要手动 gamma。
    color = pow(saturate(color), 1.0 / 2.2);

    output.color = float4(color, 1.0);
    return output;
}
```

注意：如果你后续确认 swapchain 是 `VK_FORMAT_B8G8R8A8_SRGB`，这里就不要再手动 `pow`，否则会 gamma 两次。

## 12. Command Buffer 改造

新的 `buildCommandBuffer()` 应该拆成四个阶段：

```cpp
void cmdDrawSceneToHDR(VkCommandBuffer cmd);
void cmdBloomDownsample(VkCommandBuffer cmd);
void cmdBloomUpsample(VkCommandBuffer cmd);
void cmdCompositeToSwapchain(VkCommandBuffer cmd);
```

总流程：

```cpp
void VulkanExample::buildCommandBuffer() {
    VkCommandBuffer cmd = drawCmdBuffers[currentBuffer];
    VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
    VK_CHECK_RESULT(vkBeginCommandBuffer(cmd, &cmdBufInfo));

    vkutil::cmdTransitionImageLayout(
        cmd,
        depthStencil.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    vkutil::cmdTransitionImageLayout(
        cmd,
        bloom.hdrSceneColor.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    cmdDrawSceneToHDR(cmd);

    vkutil::cmdTransitionImageLayout(
        cmd,
        bloom.hdrSceneColor.image,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    cmdBloomDownsample(cmd);
    cmdBloomUpsample(cmd);

    vkutil::cmdTransitionImageLayout(
        cmd,
        swapChain.images[currentImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    cmdCompositeToSwapchain(cmd);

    vkutil::cmdTransitionImageLayout(
        cmd,
        swapChain.images[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK_RESULT(vkEndCommandBuffer(cmd));
}
```

`cmdDrawSceneToHDR()` 内部可以暂时复用当前几个绘制函数，但要把它们的 render target 改成 `bloom.hdrSceneColor.imageView`。

## 13. Downsample Pass 伪代码

```cpp
void VulkanExample::cmdBloomDownsample(VkCommandBuffer cmd) {
    VkExtent2D srcExtent = bloom.sceneExtent;

    for (uint32_t i = 0; i < bloom.mips.size(); ++i) {
        BloomMip& dstMip = bloom.mips[i];

        vkutil::cmdTransitionImageLayout(
            cmd,
            dstMip.image.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
            dstMip.image.imageView,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}},
            VK_ATTACHMENT_LOAD_OP_CLEAR);

        vkutil::cmdBeginColorOnlyRendering(cmd, dstMip.extent, colorAttachment);
        {
            vkutil::cmdSetViewportAndScissor(cmd, dstMip.extent.width, dstMip.extent.height);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.bloomDownsamplePipeline);

            VkDescriptorSet srcSet = (i == 0)
                ? bloomDescriptorSets[currentBuffer].hdrSceneSet
                : bloomDescriptorSets[currentBuffer].mipSets[i - 1];

            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelinesLayout.bloomDownsamplePipelineLayout,
                0,
                1,
                &srcSet,
                0,
                nullptr);

            struct DownsamplePC {
                glm::vec2 srcResolution;
                uint32_t mipLevel;
                float padding;
            } pc{ glm::vec2(srcExtent.width, srcExtent.height), i, 0.0f };

            vkCmdPushConstants(
                cmd,
                pipelinesLayout.bloomDownsamplePipelineLayout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(DownsamplePC),
                &pc);

            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
        vkutil::cmdEndRendering(cmd);

        vkutil::cmdTransitionImageLayout(
            cmd,
            dstMip.image.image,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        srcExtent = dstMip.extent;
    }
}
```

这里最容易错的是 `srcResolution`：它必须是输入 texture 的分辨率。

## 14. Upsample Pass 伪代码

Upsample 需要从最小 mip 往回加：

```text
mip[n] -> mip[n - 1]
mip[n - 1] -> mip[n - 2]
...
mip[1] -> mip[0]
```

目标 mip 要 `LOAD`，并用 additive blending：

```cpp
void VulkanExample::cmdBloomUpsample(VkCommandBuffer cmd) {
    if (bloom.mips.size() < 2) {
        return;
    }

    for (int32_t i = static_cast<int32_t>(bloom.mips.size()) - 1; i > 0; --i) {
        BloomMip& srcMip = bloom.mips[i];
        BloomMip& dstMip = bloom.mips[i - 1];

        vkutil::cmdTransitionImageLayout(
            cmd,
            dstMip.image.image,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
            dstMip.image.imageView,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}},
            VK_ATTACHMENT_LOAD_OP_LOAD);

        vkutil::cmdBeginColorOnlyRendering(cmd, dstMip.extent, colorAttachment);
        {
            vkutil::cmdSetViewportAndScissor(cmd, dstMip.extent.width, dstMip.extent.height);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.bloomUpsamplePipeline);

            VkDescriptorSet srcSet = bloomDescriptorSets[currentBuffer].mipSets[i];

            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelinesLayout.bloomUpsamplePipelineLayout,
                0,
                1,
                &srcSet,
                0,
                nullptr);

            struct UpsamplePC {
                float filterRadius;
                glm::vec3 padding;
            } pc{ bloomFilterRadius, glm::vec3(0.0f) };

            vkCmdPushConstants(
                cmd,
                pipelinesLayout.bloomUpsamplePipelineLayout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(UpsamplePC),
                &pc);

            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
        vkutil::cmdEndRendering(cmd);

        vkutil::cmdTransitionImageLayout(
            cmd,
            dstMip.image.image,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }
}
```

关键点：

- `srcMip` 只是采样输入。
- `dstMip` 是 color attachment。
- `dstMip` 必须用 `VK_ATTACHMENT_LOAD_OP_LOAD`。
- Pipeline 必须启用 additive blending。

## 15. Composite Pass 伪代码

```cpp
void VulkanExample::cmdCompositeToSwapchain(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        swapChain.imageViews[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}},
        VK_ATTACHMENT_LOAD_OP_CLEAR);

    vkutil::cmdBeginColorOnlyRendering(cmd, VkExtent2D{width, height}, colorAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, width, height);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.bloomCompositePipeline);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelinesLayout.bloomCompositePipelineLayout,
            0,
            1,
            &bloomDescriptorSets[currentBuffer].compositeSet,
            0,
            nullptr);

        struct CompositePC {
            float exposure;
            float bloomStrength;
            uint32_t enableBloom;
            float padding;
        } pc{ exposure, bloomStrength, enableBloom ? 1u : 0u, 0.0f };

        vkCmdPushConstants(
            cmd,
            pipelinesLayout.bloomCompositePipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(CompositePC),
            &pc);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        drawUI(cmd);
    }
    vkutil::cmdEndRendering(cmd);
}
```

注意：你当前 `cmdDrawSkybox()` 里有 `drawUI(cmd)`，做 Bloom 后建议移到 composite pass 末尾。UI 是 LDR，不应该被写入 HDR，也不应该参与 Bloom。

## 16. 材质 Shader 要怎么改

### PBRTexture.frag

当前：

```hlsl
float3 color = ambient + Lo + emissive;

color = color / (color + 1.0);
color = ACESFilm(color);

output.color = float4(color, 1.0);
```

改成：

```hlsl
float emissiveStrength = 20.0;
float3 color = ambient + Lo + emissive * emissiveStrength;

output.color = float4(color, alpha);
```

这里 `emissiveStrength` 后续最好做成 UI 参数，先不要写太夸张。现在 `300.0` 对测试 Bloom 很有冲击力，但容易直接把 HDR mips 推爆。

### pbrScene.frag

当前也有 ACES。改成：

```hlsl
float3 color = ambient + Lo;
output.color = float4(color, 1.0);
```

### skybox.frag

skybox 当前如果做了 Reinhard：

```hlsl
color = color / (color + 1.0f);
```

也要移除。skybox 写入 HDR scene 时应保持 linear HDR。

### light.frag

光源球可以输出 HDR 值，这样能产生可见 Bloom：

```hlsl
output.color = float4(lightColor * lightDisplayIntensity, 1.0);
```

但 `lightDisplayIntensity` 不建议和真实光照强度完全绑定，否则光源球可能变成巨大的白色块。第一版可以用 `3.0 ~ 10.0`。

## 17. UI 参数建议

在 `OnUpdateUIOverlay()` 里加：

```cpp
if (overlay->header("Bloom")) {
    overlay->checkBox("Enable Bloom", &enableBloom);
    overlay->sliderFloat("Exposure", &exposure, 0.1f, 5.0f);
    overlay->sliderFloat("Bloom Strength", &bloomStrength, 0.0f, 0.3f);
    overlay->sliderFloat("Filter Radius", &bloomFilterRadius, 0.001f, 0.02f);
}
```

推荐初始值：

```cpp
enableBloom = true;
exposure = 1.0f;
bloomStrength = 0.04f;
bloomFilterRadius = 0.005f;
bloomMipCount = 5;
```

调参经验：

- `bloomStrength` 控制最终混合量，太大就会灰。
- `bloomFilterRadius` 控制 upsample 的扩散范围，太大就会糊。
- `mipCount` 控制最大光晕范围，越多越大，但也更容易让画面发雾。
- `exposure` 会影响 tone mapping 前的整体亮度，也会间接改变 Bloom 感受。

## 18. 分阶段实现顺序

强烈建议按下面顺序来，不要一口气全部做完。

### 阶段 1：只做 HDR Scene + Composite

目标：

```text
场景写入 hdrSceneColor
composite 采样 hdrSceneColor
输出到 swapchain
```

暂时不做 downsample/upsample。

验收标准：

- 画面和原来接近。
- tone mapping 已经从 `PBRTexture.frag` / `pbrScene.frag` / `skybox.frag` 移到 composite。
- UI 仍然正常显示。

### 阶段 2：只做 Downsample，直接显示 mip

先让 composite shader 只输出某一级 mip：

```hlsl
output.color = float4(bloomTexture.Sample(bloomSampler, input.uv).rgb, 1.0);
```

验收标准：

- `mip[0]` 是半分辨率、略微模糊的场景。
- `mip[1]` 更小、更糊。
- 没有黑块、花屏、layout validation error。

### 阶段 3：Upsample Additive

只显示 `bloom.mips[0]`。

验收标准：

- 画面是一张明显模糊的低频光照图。
- 高亮区域更明显，但整体不是硬 threshold 效果。
- 没有越加越亮到纯白。

### 阶段 4：最终 Composite

恢复：

```hlsl
color = lerp(hdr, hdr + bloom, bloomStrength);
```

验收标准：

- 自发光或光源周围有柔和扩散。
- 高亮金属反射有轻微柔化。
- 非高亮区域不应该明显发灰。

## 19. 常见问题排查

### 没有任何 Bloom

检查：

- `PBRTexture.frag` 是否还提前 ACES 了。
- `pbrScene.frag` 是否还提前 ACES 了。
- `skybox.frag` 是否还提前 Reinhard 了。
- 场景 pipeline 是否真的写到 `bloom.hdrFormat`。
- `hdrSceneColor` 是否从 attachment transition 到 shader read。
- `bloomStrength` 是否太低。
- composite 的 binding 1 是否是 `bloom.mips[0]`。

### Bloom 让画面发灰

原因通常是：

- `bloomStrength` 太大。
- `mipCount` 太多。
- `bloomFilterRadius` 太大。
- skybox 整体亮度过高，整张图都参与了 Bloom。

解决：

```cpp
bloomStrength = 0.02f;
bloomFilterRadius = 0.003f;
bloomMipCount = 5;
```

如果还是灰，先在 composite 里单独显示 bloom texture，看看是不是整张图都非常亮。

### 自发光过曝

当前 `PBRTexture.frag` 中 `emissiveStrength = 300.0` 很强。接入物理 Bloom 后建议先降到：

```hlsl
float emissiveStrength = 10.0;
```

后面再通过 UI 调大。

### 出现黑块

Downsample 里保留：

```hlsl
downsample = max(downsample, 0.0001.xxx);
```

这可以避免黑色像素在 mip chain 中扩散成明显块状。

### Validation 报 layout 错

记住三个状态：

```text
写入 color attachment 前：VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL
被 shader 采样前：VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
swapchain present 前：VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
```

Upsample 最容易错，因为同一张 mip 一会儿作为 sampled image，一会儿作为 color attachment。写完后一定要转回 shader read。

### UI 被 Bloom 或变暗

UI 不能画进 `hdrSceneColor`。

做法：

- 从 `cmdDrawSkybox()` 中移除 `drawUI(cmd)`。
- 在 `cmdCompositeToSwapchain()` 里 fullscreen composite 之后、`cmdEndRendering()` 之前调用 `drawUI(cmd)`。

## 20. 和旧文档的区别

之前的 Bloom 文档是传统后处理流程：

```text
HDR -> Bright Pass -> Blur -> Composite
```

这份文档是 LearnOpenGL Physically Based Bloom：

```text
HDR -> Downsample Mip Chain -> Upsample Additive -> Composite
```

所以你实现时不要再写 `bloomBright.frag`，也不要做 threshold 参数。真正需要的是：

```text
fullscreen.vert
bloomDownsample.frag
bloomUpsample.frag
bloomComposite.frag
```

如果后续你想对艺术效果做更多控制，可以在 composite 阶段加一个 soft threshold，但第一版建议忠实实现文章路线。

## 21. 你现在最应该先做什么

第一天目标不要直接做完整 Bloom。建议只完成：

1. 新增 `hdrSceneColor`。
2. 场景 pipeline 改为写 `bloom.hdrFormat`。
3. `PBRTexture.frag` / `pbrScene.frag` / `skybox.frag` 输出 linear HDR。
4. 新增 fullscreen composite，把 HDR 画回 swapchain。
5. ACES 和 gamma 只放在 composite pass。

这一步跑通后，你就已经完成了 Bloom 最容易出错的 50%。后面 downsample/upsample 是在这个稳定地基上加模块。

## 22. 参考资料

- LearnOpenGL Guest Article: [Phys. Based Bloom](https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom)
- LearnOpenGL: [Bloom](https://learnopengl.com/Advanced-Lighting/Bloom)
- Advances in Real-Time Rendering: [SIGGRAPH 2014 Course](https://advances.realtimerendering.com/s2014/)

