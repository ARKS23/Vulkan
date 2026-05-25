# Lab2 Bloom 实现指导文档

这份文档基于当前 `examples/lab2/lab2.cpp/.h` 和 `shaders/hlsl/lab2/PBRTexture.frag` 的状态，目标是把 Lab2 从“直接渲染到 swapchain”升级为：

```text
PBR Scene -> HDR Offscreen
HDR Offscreen -> Bright Pass
Bright Pass -> Blur Ping-Pong
HDR Offscreen + Bloom -> Composite 到 Swapchain
```

Bloom 的核心不是“让 emissive 颜色更亮”这么简单，而是要保留超过 1.0 的 HDR 能量，再把高亮区域扩散成柔和光晕。你现在 `PBRTexture.frag` 已经有 `emissiveStrength = 300.0`，但当前仍直接写 swapchain，并且在 fragment shader 里做了 tone mapping，所以高亮能量会太早被压掉。Bloom 的第一步就是把 tone mapping 从 PBR shader 里移到最后的 composite shader。

## 1. 当前 Lab2 渲染路径

当前 `buildCommandBuffer()` 大致是：

```cpp
vkutil::cmdTransitionImageLayout(commandBuffer, depthStencil.image, ... DEPTH_ATTACHMENT_OPTIMAL);
vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], ... ATTACHMENT_OPTIMAL);

cmdDrawSecne(commandBuffer);
cmdDrawPBRTexture(commandBuffer);
cmdDrawLight(commandBuffer);
cmdDrawSkybox(commandBuffer);

vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], ... PRESENT_SRC_KHR);
```

也就是说，所有 pass 的 color attachment 都是：

```cpp
swapChain.imageViews[currentImageIndex]
```

这对普通 PBR 展示是可以的，但对 Bloom 不够。原因是 swapchain 是 LDR 显示目标，通常是 `B8G8R8A8_*` 这类 8-bit 格式，没法可靠保存 `emissive * 300.0` 这样的高动态范围值。

## 2. Bloom 目标数据流

建议你把 Lab2 改成下面这条数据流：

```text
1. HDR Scene Pass
   scene / PBRTexture / light / skybox
   输出到 hdrSceneColor: VK_FORMAT_R16G16B16A16_SFLOAT
   深度仍使用 depthStencil

2. Bright Pass
   输入 hdrSceneColor
   输出 bloomBright: VK_FORMAT_R16G16B16A16_SFLOAT
   只保留亮度超过 threshold 的像素

3. Blur Pass
   输入 bloomBright
   输出 bloomPing / bloomPong
   多次横向/纵向模糊

4. Composite Pass
   输入 hdrSceneColor + blurredBloom
   color = hdr + bloom * bloomStrength
   exposure + ACES + gamma
   输出到 swapchain

5. UI Overlay
   直接画到 swapchain
```

推荐格式：

| 资源 | 格式 | usage |
|---|---|---|
| `hdrSceneColor` | `VK_FORMAT_R16G16B16A16_SFLOAT` | `COLOR_ATTACHMENT_BIT | SAMPLED_BIT` |
| `bloomBright` | `VK_FORMAT_R16G16B16A16_SFLOAT` | `COLOR_ATTACHMENT_BIT | SAMPLED_BIT` |
| `bloomPing` | `VK_FORMAT_R16G16B16A16_SFLOAT` | `COLOR_ATTACHMENT_BIT | SAMPLED_BIT` |
| `bloomPong` | `VK_FORMAT_R16G16B16A16_SFLOAT` | `COLOR_ATTACHMENT_BIT | SAMPLED_BIT` |
| `depthStencil` | 当前 `depthFormat` | `DEPTH_ATTACHMENT_BIT` |
| `swapchain` | 当前 `swapChain.colorFormat` | 最终显示 |

后续优化可以把 Bloom 贴图做成半分辨率或四分之一分辨率，例如 `width / 2, height / 2`，性能更好，光晕也更自然。但第一版建议先全分辨率，少一个变量。

## 3. 新增数据结构

在 `lab2.h` 里可以扩展资源结构。当前你已经有：

```cpp
struct Textures {
    vks::TextureCubeMap environmentCubeMap;
    AllocatedCubeTexture irradianceCubeMap;
    AllocatedCubeTexture prefilteredCubeMap;
    AllocatedTexture brdfLUT;
};
```

建议新增：

```cpp
struct BloomResources {
    AllocatedImage hdrSceneColor;
    AllocatedImage brightColor;
    AllocatedImage blurPing;
    AllocatedImage blurPong;

    VkSampler sampler = VK_NULL_HANDLE;
    VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkExtent2D extent{};
};
```

并在 `VulkanExample` 中添加：

```cpp
BloomResources bloom;

float bloomThreshold = 1.0f;
float bloomStrength = 0.08f;
float exposure = 1.0f;
int blurIterations = 5;
```

同时扩展 pipeline 和 layout：

```cpp
struct Piplelines {
    VkPipeline scenePipeline = VK_NULL_HANDLE;
    VkPipeline pbrTexturePipeline = VK_NULL_HANDLE;
    VkPipeline skyboxPipeline = VK_NULL_HANDLE;
    VkPipeline lightPipeline = VK_NULL_HANDLE;

    VkPipeline brightPipeline = VK_NULL_HANDLE;
    VkPipeline blurPipeline = VK_NULL_HANDLE;
    VkPipeline compositePipeline = VK_NULL_HANDLE;
};

struct PipelinesLayout {
    VkPipelineLayout scenePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout pbrTexturePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout skyboxPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout lightPipelineLayout = VK_NULL_HANDLE;

    VkPipelineLayout brightPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout blurPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout compositePipelineLayout = VK_NULL_HANDLE;
};
```

Descriptor 也建议加三组：

```cpp
struct DescriptorSets {
    VkDescriptorSet sceneDescriptor = VK_NULL_HANDLE;
    VkDescriptorSet pbrTextureDescriptor = VK_NULL_HANDLE;
    VkDescriptorSet skyboxDescriptor = VK_NULL_HANDLE;
    VkDescriptorSet lightDescriptor = VK_NULL_HANDLE;

    VkDescriptorSet brightDescriptor = VK_NULL_HANDLE;
    VkDescriptorSet blurPingDescriptor = VK_NULL_HANDLE;
    VkDescriptorSet blurPongDescriptor = VK_NULL_HANDLE;
    VkDescriptorSet compositeDescriptor = VK_NULL_HANDLE;
};

struct DescriptorSetLayouts {
    VkDescriptorSetLayout sceneDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout skyboxDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightDescriptorSetLayout = VK_NULL_HANDLE;

    VkDescriptorSetLayout singleTextureSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout compositeSetLayout = VK_NULL_HANDLE;
};
```

`singleTextureSetLayout` 用于 Bright/Blur：`binding 0 = combined image sampler`。

`compositeSetLayout` 用于最终合成：`binding 0 = hdrSceneColor`，`binding 1 = blurredBloom`。

## 4. 创建 HDR/Bloom 资源

新增函数：

```cpp
void createBloomResources();
void destroyBloomResources();
```

第一版可以用全分辨率：

```cpp
void VulkanExample::createBloomResources() {
    bloom.extent = { width, height };
    bloom.hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    VkExtent3D extent3D{ bloom.extent.width, bloom.extent.height, 1 };
    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    bloom.hdrSceneColor = vkutil::createAllocatedImage(
        device, allocator, extent3D, bloom.hdrFormat, usage, VK_IMAGE_ASPECT_COLOR_BIT);

    bloom.brightColor = vkutil::createAllocatedImage(
        device, allocator, extent3D, bloom.hdrFormat, usage, VK_IMAGE_ASPECT_COLOR_BIT);

    bloom.blurPing = vkutil::createAllocatedImage(
        device, allocator, extent3D, bloom.hdrFormat, usage, VK_IMAGE_ASPECT_COLOR_BIT);

    bloom.blurPong = vkutil::createAllocatedImage(
        device, allocator, extent3D, bloom.hdrFormat, usage, VK_IMAGE_ASPECT_COLOR_BIT);

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
}
```

销毁：

```cpp
void VulkanExample::destroyBloomResources() {
    if (bloom.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, bloom.sampler, nullptr);
        bloom.sampler = VK_NULL_HANDLE;
    }

    vkutil::destroyAllocatedImage(device, allocator, bloom.hdrSceneColor);
    vkutil::destroyAllocatedImage(device, allocator, bloom.brightColor);
    vkutil::destroyAllocatedImage(device, allocator, bloom.blurPing);
    vkutil::destroyAllocatedImage(device, allocator, bloom.blurPong);
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

析构中在 `destroyVmaAllocator()` 前释放：

```cpp
destroyBloomResources();
```

注意：窗口 resize 时，HDR/Bloom 图片也要重建。你现在 Lab2 如果还没有专门处理 resize，第一版可以先不管；后续应在 swapchain 重建后同步重建 `bloom`。

## 5. 修改场景管线的 color format

当前 scene/PBRTexture/light/skybox pipeline 都是：

```cpp
.setColorAttachmentFormat(swapChain.colorFormat)
```

改成 HDR format：

```cpp
.setColorAttachmentFormat(bloom.hdrFormat)
```

需要改的函数：

```text
createScenePipeline()
createPBRTexturePipeline()
createSkyboxPipeline()
createLightPipeline()
```

原因：这些管线之后不再写 swapchain，而是写 `bloom.hdrSceneColor`。Dynamic Rendering 的 pipeline color attachment format 必须和实际 attachment format 一致。

## 6. 修改 PBRTexture.frag：不要提前 tone map

你现在 `PBRTexture.frag` 末尾类似：

```hlsl
float3 color = ambient + Lo + emissive;

color = color / (color + 1.0);
color = ACESFilm(color);

output.color = float4(color, 1.0);
```

Bloom 版本必须改成：

```hlsl
float emissiveStrength = 20.0; // 先从 5~30 调，300 对第一版可能太炸
emissive *= emissiveStrength;

float3 color = ambient + Lo + emissive;

// 不做 tone mapping，不做 gamma，直接输出 linear HDR。
output.color = float4(color, alpha);
return output;
```

`ACESFilm` 应该移动到 composite shader。否则 bright pass 只能看到已经被压缩到 0~1 的 LDR 颜色，Bloom 会很弱或不自然。

同理，`pbrScene.frag`、`skybox.frag`、`light.frag` 如果里面做了 tone mapping/gamma，最好也改成输出 linear HDR。最终只在 composite pass 做一次 tone mapping/gamma。

## 7. 新增全屏三角形 vertex shader

可以复用你 `shaders/hlsl/lab0/fullscreen.vert` 的写法，在 `shaders/hlsl/lab2/fullscreen.vert` 新建：

```hlsl
struct VSOutput {
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float2 UV : TEXCOORD0;
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
    output.Pos = float4(positions[vertexIndex], 0.0, 1.0);
    output.UV = uvs[vertexIndex];
    return output;
}
```

这个 vertex shader 不需要 vertex buffer，pipeline 使用 `setEmptyVertexInput()`。

## 8. Bright Pass Shader

新建 `shaders/hlsl/lab2/bloomBright.frag`：

```hlsl
struct FSInput {
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

Texture2D hdrScene : register(t0, space0);
SamplerState hdrSampler : register(s0, space0);

struct PushConstants {
    float threshold;
    float knee;
};
[[vk::push_constant]] PushConstants pc;

FSOutput main(FSInput input) {
    FSOutput output;

    float3 color = hdrScene.Sample(hdrSampler, input.UV).rgb;
    float brightness = dot(color, float3(0.2126, 0.7152, 0.0722));

    // 第一版硬阈值，简单直观。
    float weight = brightness > pc.threshold ? 1.0 : 0.0;

    output.color = float4(color * weight, 1.0);
    return output;
}
```

后续可以换成 soft threshold：

```hlsl
float soft = saturate((brightness - pc.threshold + pc.knee) / max(2.0 * pc.knee, 0.0001));
soft = soft * soft * (3.0 - 2.0 * soft);
float weight = max(brightness - pc.threshold, 0.0) / max(brightness, 0.0001);
weight = max(weight, soft);
```

第一版建议先用硬阈值，调试更清楚。

## 9. Blur Shader

新建 `shaders/hlsl/lab2/bloomBlur.frag`：

```hlsl
struct FSInput {
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

Texture2D inputTexture : register(t0, space0);
SamplerState inputSampler : register(s0, space0);

struct PushConstants {
    uint horizontal;
    float texelSizeX;
    float texelSizeY;
    float radius;
};
[[vk::push_constant]] PushConstants pc;

FSOutput main(FSInput input) {
    FSOutput output;

    float2 texelSize = float2(pc.texelSizeX, pc.texelSizeY);
    float2 direction = pc.horizontal != 0 ? float2(texelSize.x, 0.0) : float2(0.0, texelSize.y);

    float weights[5] = {
        0.227027,
        0.1945946,
        0.1216216,
        0.054054,
        0.016216
    };

    float3 result = inputTexture.Sample(inputSampler, input.UV).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        float2 offset = direction * float(i) * pc.radius;
        result += inputTexture.Sample(inputSampler, input.UV + offset).rgb * weights[i];
        result += inputTexture.Sample(inputSampler, input.UV - offset).rgb * weights[i];
    }

    output.color = float4(result, 1.0);
    return output;
}
```

第一版用 separable Gaussian blur：横向一次、纵向一次，循环多轮。

## 10. Composite Shader

新建 `shaders/hlsl/lab2/bloomComposite.frag`：

```hlsl
struct FSInput {
    [[vk::location(0)]] float2 UV : TEXCOORD0;
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

    float3 hdr = hdrScene.Sample(hdrSampler, input.UV).rgb;
    float3 bloom = bloomTexture.Sample(bloomSampler, input.UV).rgb;

    float3 color = hdr + bloom * pc.bloomStrength;

    color *= pc.exposure;
    color = ACESFilm(color);
    color = pow(saturate(color), 1.0 / 2.2);

    output.color = float4(color, 1.0);
    return output;
}
```

注意：`pow(..., 1/2.2)` 只在最终写入非 sRGB swapchain 时做。如果你的 swapchain 后续改成 sRGB format，则这里要重新审视，避免重复 gamma。

## 11. 新增 Pipeline

在 `lab2.h` 增加 shader 路径：

```cpp
const std::string fullscreenVertexShader = "lab2/fullscreen.vert.spv";
const std::string bloomBrightFragmentShader = "lab2/bloomBright.frag.spv";
const std::string bloomBlurFragmentShader = "lab2/bloomBlur.frag.spv";
const std::string bloomCompositeFragmentShader = "lab2/bloomComposite.frag.spv";
```

Bright pipeline：

```cpp
void VulkanExample::createBrightPipeline() {
    vkutil::PipelineBuilder builder;
    builder.setPipelineLayout(pipelinesLayout.brightPipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + fullscreenVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + bloomBrightFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setEmptyVertexInput()
        .setColorAttachmentFormat(bloom.hdrFormat)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();

    pipelines.brightPipeline = builder.build(device, pipelineCache);
}
```

Blur pipeline 同理，输出也是 `bloom.hdrFormat`。

Composite pipeline 输出到 swapchain，所以 format 是：

```cpp
.setColorAttachmentFormat(swapChain.colorFormat)
```

并且不需要深度：

```cpp
.disableDepthTest()
.setEmptyVertexInput()
```

## 12. Descriptor Layout 和 Descriptor Set

`singleTextureSetLayout`：

```cpp
std::vector<VkDescriptorSetLayoutBinding> singleTextureLayout = {
    vkutil::descriptorSetLayoutBinding(
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0)
};
VK_CHECK_RESULT(vkCreateDescriptorSetLayout(
    device,
    &vkutil::descriptorSetLayoutCreateInfo(singleTextureLayout),
    nullptr,
    &descriptorSetLayouts.singleTextureSetLayout));
```

`compositeSetLayout`：

```cpp
std::vector<VkDescriptorSetLayoutBinding> compositeLayout = {
    vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
    vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
};
```

Descriptor 写入关系：

| Descriptor set | binding | 输入 |
|---|---|---|
| `brightDescriptor` | 0 | `hdrSceneColor` |
| `blurPingDescriptor` | 0 | `brightColor` 或 `blurPong` |
| `blurPongDescriptor` | 0 | `blurPing` |
| `compositeDescriptor` | 0 | `hdrSceneColor` |
| `compositeDescriptor` | 1 | 最终 blurred bloom，通常 `blurPing` 或 `blurPong` |

一个细节：如果 blur 多轮 ping-pong，最后一次输出在哪张图取决于循环次数。第一版为了简单，可以固定执行偶数轮，例如 10 次，也就是最终回到 `blurPing`。或者在 C++ 中记录：

```cpp
bool finalBloomIsPing = ...;
```

然后更新 composite descriptor。

## 13. 修改 Command Buffer 数据流

目标从：

```cpp
swapchain -> scene -> pbr -> light -> skybox -> present
```

改为：

```cpp
hdrSceneColor -> scene/pbr/light/skybox
hdrSceneColor -> brightColor
brightColor -> blurPing -> blurPong -> ...
hdrSceneColor + finalBloom -> swapchain
drawUI -> swapchain
present
```

建议新增函数：

```cpp
void cmdDrawSceneToHDR(VkCommandBuffer cmd);
void cmdBrightPass(VkCommandBuffer cmd);
void cmdBlurPass(VkCommandBuffer cmd);
void cmdCompositeToSwapchain(VkCommandBuffer cmd);
```

新的 `buildCommandBuffer()`：

```cpp
void VulkanExample::buildCommandBuffer() {
    VkCommandBuffer cmd = drawCmdBuffers[currentBuffer];
    VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
    VK_CHECK_RESULT(vkBeginCommandBuffer(cmd, &cmdBufInfo));

    vkutil::cmdTransitionImageLayout(cmd, depthStencil.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    vkutil::cmdTransitionImageLayout(cmd, bloom.hdrSceneColor.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    cmdDrawSceneToHDR(cmd);

    vkutil::cmdTransitionImageLayout(cmd, bloom.hdrSceneColor.image,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    cmdBrightPass(cmd);
    cmdBlurPass(cmd);

    vkutil::cmdTransitionImageLayout(cmd, swapChain.images[currentImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    cmdCompositeToSwapchain(cmd);
    drawUI(cmd);

    vkutil::cmdTransitionImageLayout(cmd, swapChain.images[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK_RESULT(vkEndCommandBuffer(cmd));
}
```

## 14. Scene Pass 如何合并

你现在有四个函数：

```cpp
cmdDrawSecne()
cmdDrawPBRTexture()
cmdDrawLight()
cmdDrawSkybox()
```

第一版可以保留这种分函数结构，但把 color attachment 从 swapchain 改成 `bloom.hdrSceneColor.imageView`。

例如：

```cpp
VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
    bloom.hdrSceneColor.imageView,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    clearValue,
    VK_ATTACHMENT_LOAD_OP_CLEAR);
```

后续 pass 使用：

```cpp
VK_ATTACHMENT_LOAD_OP_LOAD
```

也就是：

```text
cmdDrawSecneToHDR:       CLEAR color + CLEAR depth
cmdDrawPBRTextureToHDR:  LOAD color + LOAD depth
cmdDrawLightToHDR:       LOAD color + LOAD depth
cmdDrawSkyboxToHDR:      LOAD color + LOAD depth
```

更整洁的做法是把四个 draw 合并到一个 `cmdDrawSceneToHDR()` 的 dynamic rendering 里面，只 begin/end 一次。这会减少重复 begin/end，也更接近引擎：

```cpp
vkutil::cmdBeginColorDepthRendering(... hdrSceneColor, depthStencil ...);
draw procedural PBR grid;
draw DamagedHelmet;
draw lights;
draw skybox;
vkutil::cmdEndRendering(cmd);
```

但你目前分函数比较清楚，第一版不用急着合并。

## 15. Bright Pass Command

```cpp
void VulkanExample::cmdBrightPass(VkCommandBuffer cmd) {
    vkutil::cmdTransitionImageLayout(cmd, bloom.brightColor.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        bloom.brightColor.imageView,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}});

    vkutil::cmdBeginColorOnlyRendering(cmd, bloom.extent, colorAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, bloom.extent.width, bloom.extent.height);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.brightPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelinesLayout.brightPipelineLayout, 0, 1,
            &descriptorSets[currentBuffer].brightDescriptor, 0, nullptr);

        struct BrightPC {
            float threshold;
            float knee;
        } pc{ bloomThreshold, 0.1f };
        vkCmdPushConstants(cmd, pipelinesLayout.brightPipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BrightPC), &pc);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    vkutil::cmdEndRendering(cmd);

    vkutil::cmdTransitionImageLayout(cmd, bloom.brightColor.image,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);
}
```

## 16. Blur Pass Command

第一版可以这样理解：

```text
brightColor --horizontal--> blurPing
blurPing    --vertical----> blurPong
blurPong    --horizontal--> blurPing
blurPing    --vertical----> blurPong
...
```

需要注意每次读写不同 image，不能同一张图同时作为 sampled image 和 color attachment。

伪代码：

```cpp
void VulkanExample::cmdBlurPass(VkCommandBuffer cmd) {
    bool horizontal = true;
    bool firstIteration = true;

    for (int i = 0; i < blurIterations * 2; ++i) {
        AllocatedImage& dst = horizontal ? bloom.blurPing : bloom.blurPong;

        vkutil::cmdTransitionImageLayout(cmd, dst.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
            dst.imageView,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VkClearValue{{0, 0, 0, 1}});

        vkutil::cmdBeginColorOnlyRendering(cmd, bloom.extent, colorAttachment);
        {
            vkutil::cmdSetViewportAndScissor(cmd, bloom.extent.width, bloom.extent.height);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.blurPipeline);

            VkDescriptorSet inputSet = firstIteration
                ? descriptorSets[currentBuffer].blurBrightDescriptor
                : (horizontal
                    ? descriptorSets[currentBuffer].blurPongDescriptor
                    : descriptorSets[currentBuffer].blurPingDescriptor);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelinesLayout.blurPipelineLayout, 0, 1, &inputSet, 0, nullptr);

            struct BlurPC {
                uint32_t horizontal;
                float texelSizeX;
                float texelSizeY;
                float radius;
            } pc{
                horizontal ? 1u : 0u,
                1.0f / float(bloom.extent.width),
                1.0f / float(bloom.extent.height),
                1.0f
            };

            vkCmdPushConstants(cmd, pipelinesLayout.blurPipelineLayout,
                VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BlurPC), &pc);

            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
        vkutil::cmdEndRendering(cmd);

        vkutil::cmdTransitionImageLayout(cmd, dst.image,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        horizontal = !horizontal;
        if (firstIteration) {
            firstIteration = false;
        }
    }
}
```

这里的 descriptor 组织可以有两种：

```text
简单版：提前创建 bright/ping/pong 三个 input descriptor
灵活版：每轮动态 vkUpdateDescriptorSets，不推荐第一版
```

建议用简单版。

## 17. Composite Pass Command

```cpp
void VulkanExample::cmdCompositeToSwapchain(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        swapChain.imageViews[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}});

    vkutil::cmdBeginColorOnlyRendering(cmd, {width, height}, colorAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, width, height);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.compositePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelinesLayout.compositePipelineLayout, 0, 1,
            &descriptorSets[currentBuffer].compositeDescriptor, 0, nullptr);

        struct CompositePC {
            float exposure;
            float bloomStrength;
        } pc{ exposure, bloomStrength };

        vkCmdPushConstants(cmd, pipelinesLayout.compositePipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompositePC), &pc);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        drawUI(cmd);
    }
    vkutil::cmdEndRendering(cmd);
}
```

建议把 UI 放在 composite 后面，因为 UI 是 LDR overlay，不应该参与 HDR bloom 和 tone mapping。

## 18. UI 参数

在 `OnUpdateUIOverlay()` 里加：

```cpp
if (overlay->header("Bloom")) {
    overlay->sliderFloat("Exposure", &exposure, 0.1f, 5.0f);
    overlay->sliderFloat("Threshold", &bloomThreshold, 0.1f, 10.0f);
    overlay->sliderFloat("Strength", &bloomStrength, 0.0f, 2.0f);
    overlay->sliderInt("Blur Iter", &blurIterations, 1, 10);
}
```

如果 UI overlay 没有 `sliderInt`，可以用 float 临时代替。

推荐初始值：

```cpp
exposure = 1.0f;
bloomThreshold = 1.0f;
bloomStrength = 0.08f;
blurIterations = 5;
```

如果 emissive 很强，例如 `emissiveStrength = 300`，threshold 可以调到 `5~20`。如果 emissiveStrength 降到 `10~30`，threshold 可以在 `1~3`。

## 19. 编译 Shader

Lab2 的 HLSL shader 放在：

```text
shaders/hlsl/lab2/
```

新增：

```text
fullscreen.vert
bloomBright.frag
bloomBlur.frag
bloomComposite.frag
```

项目的 CMake 会按 example 名字 glob 当前目录 shader。新增 shader 后建议重新 configure 一次，保证新文件进入工程：

```powershell
cmake -S . -B build
cmake --build build --config Debug --target lab2 -j 32
```

如果只 build 不生成 `.spv`，通常是 CMake 没重新扫描到新 shader。

## 20. 常见 Bug 清单

### Bloom 完全没有效果

优先检查：

```text
PBRTexture.frag 是否还在提前 tone map
emissiveStrength 是否足够大
bright threshold 是否太高
bright pass 输入是否是 HDR scene
blur/composite descriptor 是否绑定了正确 image
```

临时调试：

```hlsl
// composite.frag 中只输出 bloom
output.color = float4(bloom, 1.0);
```

如果全黑，问题在 bright/blur；如果有图，问题在 composite 参数。

### 画面整体过曝

调低：

```text
exposure
bloomStrength
emissiveStrength
direct light intensity
```

也可以提高：

```text
bloomThreshold
```

### 画面变暗

检查是否重复 tone mapping/gamma：

```text
PBR shader 做了一次 ACES
composite 又做一次 ACES
skybox shader 做了 gamma
composite 又做 gamma
```

Bloom 架构里，tone mapping/gamma 只应该在最终 composite 做一次。

### Validation 报 image layout 错

每个 image 在作为 color attachment 前应为：

```text
VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL
```

作为采样输入前应为：

```text
VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
```

最容易错的是 blur ping-pong，因为同一张图在不同 iteration 中轮流读写。

### Blur 后画面拖影或闪烁

检查：

```text
读写是不是同一张 texture
descriptor 是否指向上一轮输出
每轮是否正确 transition
blurIterations 变化后 composite 输入是否还是最终输出图
```

## 21. 推荐实现顺序

不要一次把所有代码塞进去。建议这样推进：

1. **先建 HDR scene texture**
   把 scene/PBR/light/skybox 写到 `hdrSceneColor`，composite 只显示 `hdrSceneColor`。

2. **把 tone mapping 移到 composite**
   确认画面和之前接近。

3. **做 bright pass**
   composite 临时只显示 `brightColor`，确认能看到 emissive/高亮区域。

4. **做 blur pass**
   composite 临时只显示 blurred bloom，确认有柔和光斑。

5. **做最终合成**
   `hdr + bloom * strength`，再 ACES/gamma。

6. **调 UI**
   加 exposure、threshold、strength、blurIterations。

## 22. 对当前 Lab2 的特别提醒

你的 `PBRTexture.frag` 当前已经用了：

```hlsl
float emissiveStrength = 300.0;
color = color / (color + 1.0);
color = ACESFilm(color);
```

做 Bloom 前要改成：

```hlsl
float emissiveStrength = 10.0; // 先别太大
float3 color = ambient + Lo + emissive * emissiveStrength;
output.color = float4(color, alpha);
```

然后在 `bloomComposite.frag` 中统一：

```hlsl
color = hdr + bloom * bloomStrength;
color *= exposure;
color = ACESFilm(color);
color = pow(saturate(color), 1.0 / 2.2);
```

这一步是 Bloom 的心脏。只要 HDR 能量没有提前丢失，后面的 bright/blur/composite 就能逐步调出来。

