# Lab2 Physically Based Bloom 实现指导

这份文档按 LearnOpenGL 的 [Phys. Based Bloom](https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom) 思路重写，不再采用传统的“亮度阈值提取 + Gaussian blur ping-pong”方案。

新的目标是把 Lab2 从当前的直接渲染到 swapchain，改造成下面的数据流：

```text
PBR / glTF / Light / Skybox
        |
        v
HDR Scene Color: R16G16B16A16_SFLOAT
        |
        v
Bloom Downsample Mip Chain: A -> B -> C -> D -> E
        |
        v
Bloom Upsample Additive: E' -> D' -> C' -> B' -> A'
        |
        v
Composite: lerp(HDR, HDR + Bloom, bloomStrength)
        |
        v
Tone Mapping + Gamma -> Swapchain
```

核心变化有三点：

1. **不做 bright threshold**。物理 Bloom 认为镜头和眼睛会对整张 HDR 画面产生散射，只是高亮区域能量更大，所以效果更明显。
2. **不做大半径多轮 Gaussian blur**。通过连续 downsample，再用小核 upsample，近似很大的模糊半径，性能和稳定性都更好。
3. **Bloom 在 tone mapping 之前完成**。`PBRTexture.frag` 和 `pbrScene.frag` 应输出 linear HDR，最终只在 composite pass 中做 ACES/gamma。

## 1. 当前 Lab2 的状态

当前 `examples/lab2/lab2.cpp` 的主渲染路径是：

```cpp
vkutil::cmdTransitionImageLayout(commandBuffer, depthStencil.image, ... DEPTH_ATTACHMENT_OPTIMAL);
vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], ... ATTACHMENT_OPTIMAL);

cmdDrawSecne(commandBuffer);
cmdDrawPBRTexture(commandBuffer);
cmdDrawLight(commandBuffer);
cmdDrawSkybox(commandBuffer);

vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], ... PRESENT_SRC_KHR);
```

也就是说，`cmdDrawSecne()`、`cmdDrawPBRTexture()`、`cmdDrawLight()`、`cmdDrawSkybox()` 都直接写入：

```cpp
swapChain.imageViews[currentImageIndex]
```

这对普通 PBR 展示没问题，但对 Bloom 不够。原因是 swapchain 通常是 8-bit LDR 格式，不能保存自发光、强光源和 HDR IBL 产生的高动态范围能量。

你现在的 `PBRTexture.frag` 还有这段：

```hlsl
float emissiveStrength = 300.0;
emissive *= emissiveStrength;

float3 color = ambient + Lo + emissive;

color = color / (color + 1.0);
color = ACESFilm(color);

output.color = float4(color, 1.0);
```

做 Bloom 前必须改掉：tone mapping 不能放在材质 shader 里，否则高亮能量会提前被压缩，后处理拿不到真正的 HDR 值。

## 2. 物理 Bloom 与旧 Bloom 的区别

旧方案通常是：

```text
HDR/LDR Color -> threshold bright pass -> separable Gaussian blur -> additive composite
```

这个方案容易出现几个问题：

```text
threshold 很难调，不同 HDR 场景亮度差异大
阈值切割会让 Bloom 边界不自然
暗区完全没有散射，画面容易像“特效叠层”
大半径 blur 需要很多采样或很多轮 ping-pong
```

LearnOpenGL 这篇文章采用的路线更适合你现在的 PBR/IBL Lab：

```text
HDR Color -> mip-chain downsample -> progressive upsample + additive blend -> composite
```

它的直觉是：不强行区分“谁应该发光”，而是对完整 HDR 画面做低频扩散。因为 HDR 画面里亮物体数值远大于暗物体，downsample 和 blur 之后自然会主要表现为亮区泛光。

## 3. 推荐数据格式

第一版建议保持清晰，不要过早压缩格式。

| 资源 | 推荐格式 | 用途 |
|---|---|---|
| `hdrSceneColor` | `VK_FORMAT_R16G16B16A16_SFLOAT` | 场景 HDR color attachment + sampled input |
| `bloomMip[i]` | `VK_FORMAT_R16G16B16A16_SFLOAT` | downsample/upsample 的 mip chain |
| `depthStencil` | 当前 `depthFormat` | scene pass 深度 |
| `swapchain` | 当前 `swapChain.colorFormat` | 最终显示 |

OpenGL 文章中使用了类似 `R11G11B10F` 的浮点 RGB 格式来节省带宽。Vulkan 中可以后续考虑 `VK_FORMAT_B10G11R11_UFLOAT_PACK32`，但学习阶段我建议先用 `R16G16B16A16_SFLOAT`。它更直观，也更不容易踩格式兼容坑。

## 4. Lab2 需要新增的资源结构

建议在 `lab2.h` 中添加 Bloom 资源。不要把所有 mip 放进一张 mipmapped image，第一版更推荐每个 mip 一张独立 `AllocatedImage`，因为你的 `vkutil::createAllocatedImage()` 当前默认创建单 mip 2D image，和 Dynamic Rendering 结合更简单。

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

在 `VulkanExample` 中加：

```cpp
BloomResources bloom;

float exposure = 1.0f;
float bloomStrength = 0.04f;
float bloomFilterRadius = 0.005f;
int bloomMipCount = 5;
bool enableBloom = true;
```

这里的 `bloomStrength = 0.04` 很重要。物理 Bloom 不是把光晕暴力加上去，而是强烈偏向原始 HDR 画面，只混入一小部分 Bloom。

## 5. 创建 HDR Scene 和 Bloom Mips

新增函数：

```cpp
void createBloomResources();
void destroyBloomResources();
```

伪代码如下：

```cpp
void VulkanExample::createBloomResources() {
    bloom.hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    bloom.sceneExtent = { width, height };
    bloom.mipCount = static_cast<uint32_t>(bloomMipCount);

    VkImageUsageFlags hdrUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    bloom.hdrSceneColor = vkutil::createAllocatedImage(
        device,
        allocator,
        VkExtent3D{ width, height, 1 },
        bloom.hdrFormat,
        hdrUsage,
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

注意：mip chain 的第一张不是 full resolution，而是 `width / 2, height / 2`。这和文章一致：full resolution 是原始 HDR source，Bloom mip chain 从半分辨率开始。

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

## 6. 管线扩展

在 `Piplelines` 中加入：

```cpp
VkPipeline bloomDownsamplePipeline = VK_NULL_HANDLE;
VkPipeline bloomUpsamplePipeline = VK_NULL_HANDLE;
VkPipeline compositePipeline = VK_NULL_HANDLE;
```

在 `PipelinesLayout` 中加入：

```cpp
VkPipelineLayout bloomDownsamplePipelineLayout = VK_NULL_HANDLE;
VkPipelineLayout bloomUpsamplePipelineLayout = VK_NULL_HANDLE;
VkPipelineLayout compositePipelineLayout = VK_NULL_HANDLE;
```

场景相关管线需要从 swapchain format 改成 HDR format：

```cpp
// createScenePipeline()
// createPBRTexturePipeline()
// createSkyboxPipeline()
// createLightPipeline()
.setColorAttachmentFormat(bloom.hdrFormat)
```

后处理管线：

```cpp
createBloomDownsamplePipeline();
createBloomUpsamplePipeline();
createCompositePipeline();
```

其中 downsample/upsample 输出到 `bloom.hdrFormat`，composite 输出到 `swapChain.colorFormat`。

## 7. Descriptor 设计

第一版建议保持简单：为每个可采样 image 创建一个 descriptor set。

新增 layout：

```cpp
VkDescriptorSetLayout bloomSingleTextureSetLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout compositeSetLayout = VK_NULL_HANDLE;
```

`bloomSingleTextureSetLayout`：

```cpp
binding 0 = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
stage = VK_SHADER_STAGE_FRAGMENT_BIT
```

`compositeSetLayout`：

```cpp
binding 0 = hdrSceneColor
binding 1 = bloom.mips[0]
```

建议的 descriptor set：

```cpp
struct BloomDescriptorSets {
    VkDescriptorSet hdrSceneSet = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> mipSets;
    VkDescriptorSet compositeSet = VK_NULL_HANDLE;
};
```

每帧一份：

```cpp
std::array<BloomDescriptorSets, maxConcurrentFrames> bloomDescriptorSets;
```

绑定关系：

| Pass | 采样输入 |
|---|---|
| Downsample mip 0 | `hdrSceneColor` |
| Downsample mip i | `bloom.mips[i - 1]` |
| Upsample i -> i - 1 | `bloom.mips[i]` |
| Composite | `hdrSceneColor` + `bloom.mips[0]` |

`compositeSet` 的 binding 1 固定绑定 `bloom.mips[0]`，因为 upsample 完后最终 Bloom 结果会累计在第一张 bloom mip 上。

## 8. 新增 Shader 文件

新增三个 shader：

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

### bloomDownsample.frag

这是文章中的 13-tap downsample 思路，HLSL 版本如下：

```hlsl
struct FSInput {
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

Texture2D srcTexture : register(t0, space0);
SamplerState srcSampler : register(s0, space0);

struct PushConstants {
    float2 srcResolution;
    uint mipLevel;
    float _padding;
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
    float2 uv = input.UV;

    float3 a = srcTexture.Sample(srcSampler, uv + float2(-2.0 * x,  2.0 * y)).rgb;
    float3 b = srcTexture.Sample(srcSampler, uv + float2( 0.0,      2.0 * y)).rgb;
    float3 c = srcTexture.Sample(srcSampler, uv + float2( 2.0 * x,  2.0 * y)).rgb;
    float3 d = srcTexture.Sample(srcSampler, uv + float2(-2.0 * x,  0.0)).rgb;
    float3 e = srcTexture.Sample(srcSampler, uv).rgb;
    float3 f = srcTexture.Sample(srcSampler, uv + float2( 2.0 * x,  0.0)).rgb;
    float3 g = srcTexture.Sample(srcSampler, uv + float2(-2.0 * x, -2.0 * y)).rgb;
    float3 h = srcTexture.Sample(srcSampler, uv + float2( 0.0,     -2.0 * y)).rgb;
    float3 i = srcTexture.Sample(srcSampler, uv + float2( 2.0 * x, -2.0 * y)).rgb;
    float3 j = srcTexture.Sample(srcSampler, uv + float2(-x, y)).rgb;
    float3 k = srcTexture.Sample(srcSampler, uv + float2( x, y)).rgb;
    float3 l = srcTexture.Sample(srcSampler, uv + float2(-x, -y)).rgb;
    float3 m = srcTexture.Sample(srcSampler, uv + float2( x, -y)).rgb;

    float3 downsample;

    if (pc.mipLevel == 0) {
        // 可选：第一层使用 Karis average，减少 HDR fireflies。
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

如果你觉得 Karis average 让 Bloom 变暗，可以先把 `mipLevel == 0` 分支去掉，全都走普通 downsample。等出现高亮噪点或闪烁时再加回来。

### bloomUpsample.frag

```hlsl
struct FSInput {
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

Texture2D srcTexture : register(t0, space0);
SamplerState srcSampler : register(s0, space0);

struct PushConstants {
    float filterRadius;
    float3 _padding;
};
[[vk::push_constant]] PushConstants pc;

FSOutput main(FSInput input) {
    FSOutput output;

    float x = pc.filterRadius;
    float y = pc.filterRadius;
    float2 uv = input.UV;

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

`filterRadius` 是 texture coordinate 空间里的半径，推荐从：

```cpp
bloomFilterRadius = 0.005f;
```

开始调。

### bloomComposite.frag

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
    uint enableBloom;
    float _padding;
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

    float strength = pc.enableBloom != 0 ? pc.bloomStrength : 0.0;
    float3 color = lerp(hdr, hdr + bloom, strength);

    color *= pc.exposure;
    color = ACESFilm(color);

    // 如果 swapchain 是非 sRGB 格式，需要手动 gamma。
    color = pow(saturate(color), 1.0 / 2.2);

    output.color = float4(color, 1.0);
    return output;
}
```

## 9. Pipeline 创建要点

downsample pipeline：

```cpp
void VulkanExample::createBloomDownsamplePipeline() {
    vkutil::PipelineBuilder builder;
    builder.setPipelineLayout(pipelinesLayout.bloomDownsamplePipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + "lab2/fullscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + "lab2/bloomDownsample.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT))
        .setEmptyVertexInput()
        .setColorAttachmentFormat(bloom.hdrFormat)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();

    pipelines.bloomDownsamplePipeline = builder.build(device, pipelineCache);
}
```

upsample pipeline 需要 additive blending：

```cpp
void VulkanExample::createBloomUpsamplePipeline() {
    vkutil::PipelineBuilder builder;
    builder.setPipelineLayout(pipelinesLayout.bloomUpsamplePipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + "lab2/fullscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + "lab2/bloomUpsample.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT))
        .setEmptyVertexInput()
        .setColorAttachmentFormat(bloom.hdrFormat)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .enableAdditiveBlending();

    pipelines.bloomUpsamplePipeline = builder.build(device, pipelineCache);
}
```

你的 `PipelineBuilder` 已经有 `enableAdditiveBlending()`，它正好对应文章里的：

```text
目标 mip = 原目标 mip + blur(更小一级 mip)
```

composite pipeline：

```cpp
void VulkanExample::createCompositePipeline() {
    vkutil::PipelineBuilder builder;
    builder.setPipelineLayout(pipelinesLayout.compositePipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + "lab2/fullscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + "lab2/bloomComposite.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT))
        .setEmptyVertexInput()
        .setColorAttachmentFormat(swapChain.colorFormat)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();

    pipelines.compositePipeline = builder.build(device, pipelineCache);
}
```

## 10. 场景 Pass 改成写入 HDR

把 `cmdDrawSecne()`、`cmdDrawPBRTexture()`、`cmdDrawLight()`、`cmdDrawSkybox()` 的 color attachment 从：

```cpp
swapChain.imageViews[currentImageIndex]
```

改成：

```cpp
bloom.hdrSceneColor.imageView
```

第一帧 scene pass 用 `VK_ATTACHMENT_LOAD_OP_CLEAR`，后面的 PBRTexture/light/skybox 用 `VK_ATTACHMENT_LOAD_OP_LOAD`。

目前 `cmdDrawSkybox()` 里有：

```cpp
drawUI(cmd);
```

做 Bloom 后要移走。UI 是 LDR overlay，应该放到 composite pass 后画到 swapchain，不能写进 HDR scene，也不能参与 Bloom。

## 11. PBR Shader 输出 linear HDR

`PBRTexture.frag` 推荐改成：

```hlsl
float emissiveStrength = 20.0;
float3 color = ambient + Lo + emissive * emissiveStrength;

output.color = float4(color, alpha);
return output;
```

`pbrScene.frag` 推荐改成：

```hlsl
float3 color = ambient + Lo;
output.color = float4(color, 1.0);
return output;
```

`light.frag` 如果用来画光源小球，可以输出 HDR light color，例如：

```hlsl
output.color = float4(lightColor * lightIntensityScale, 1.0);
```

但为了避免光源小球炸屏，可以先让它输出一个适度的 HDR 值，例如 `3.0 ~ 10.0`，不要一上来几百。

## 12. Command Buffer 总流程

建议新增函数：

```cpp
void cmdDrawSceneToHDR(VkCommandBuffer cmd);
void cmdBloomDownsample(VkCommandBuffer cmd);
void cmdBloomUpsample(VkCommandBuffer cmd);
void cmdCompositeToSwapchain(VkCommandBuffer cmd);
```

新的 `buildCommandBuffer()` 逻辑：

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
    drawUI(cmd);

    vkutil::cmdTransitionImageLayout(
        cmd,
        swapChain.images[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK_RESULT(vkEndCommandBuffer(cmd));
}
```

## 13. Downsample Pass 实现

downsample 的核心循环：

```text
hdrSceneColor -> bloom.mips[0]
bloom.mips[0] -> bloom.mips[1]
bloom.mips[1] -> bloom.mips[2]
...
```

伪代码：

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
            VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}});

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

## 14. Upsample Pass 实现

upsample 的核心循环：

```text
smallest -> ... -> bloom.mips[1] -> bloom.mips[0]
```

关键点：目标 mip 不是清空写入，而是 **LOAD 原内容，然后 additive blending**。也就是：

```text
bloom.mips[i - 1] = bloom.mips[i - 1] + blur(bloom.mips[i])
```

伪代码：

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

这个 pass 跑完后，最终 Bloom 结果在：

```cpp
bloom.mips[0]
```

## 15. Composite Pass 实现

Composite 读取：

```text
binding 0: hdrSceneColor
binding 1: bloom.mips[0]
```

输出到 swapchain：

```cpp
void VulkanExample::cmdCompositeToSwapchain(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        swapChain.imageViews[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}});

    vkutil::cmdBeginColorOnlyRendering(cmd, VkExtent2D{width, height}, colorAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, width, height);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.compositePipeline);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelinesLayout.compositePipelineLayout,
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
            pipelinesLayout.compositePipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(CompositePC),
            &pc);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    vkutil::cmdEndRendering(cmd);
}
```

UI 放在这个 pass 后面单独画：

```cpp
cmdCompositeToSwapchain(cmd);
drawUI(cmd);
```

如果 `drawUI(cmd)` 需要处在 active rendering 内部，就把它放进 `cmdCompositeToSwapchain()` 的 `vkutil::cmdBeginColorOnlyRendering()` 与 `vkutil::cmdEndRendering()` 之间，且放在 fullscreen composite draw 之后。

## 16. UI 参数

在 `OnUpdateUIOverlay()` 加：

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
exposure = 1.0f;
bloomStrength = 0.04f;
bloomFilterRadius = 0.005f;
bloomMipCount = 5;
```

调参直觉：

```text
bloomStrength 控制 Bloom 混入量，越大越梦幻，也越容易糊。
filterRadius 控制 upsample 时的扩散范围，越大光晕越松。
mipCount 控制最大扩散半径，越多越大，但也越容易让画面灰。
exposure 控制 tone mapping 前整体亮度。
```

## 17. 与当前代码最相关的修改点

你当前最需要改的地方：

1. `lab2.h`
   添加 `BloomResources`、pipeline/layout/descriptor 字段、shader 路径和函数声明。

2. `prepare()`
   在 `generateBRDFLUT()` 后、`setupDescriptors()` 前创建 Bloom 资源：

   ```cpp
   createBloomResources();
   createUniformBuffers();
   setupDescriptors();
   createPipelines();
   ```

3. `destroy`
   在 `destroyVmaAllocator()` 前调用 `destroyBloomResources()`。

4. `setupDescriptors()`
   增加 bloom single texture layout、composite layout、每个 mip 的 descriptor set。

5. `createPipelines()`
   创建 downsample、upsample、composite 三条管线。

6. `createScenePipeline()` 等场景管线
   `setColorAttachmentFormat(swapChain.colorFormat)` 改为 `setColorAttachmentFormat(bloom.hdrFormat)`。

7. `buildCommandBuffer()`
   改成 HDR scene -> downsample -> upsample -> composite -> UI -> present。

8. `PBRTexture.frag` / `pbrScene.frag`
   移除材质 shader 里的 tone mapping，直接输出 linear HDR。

## 18. 常见问题与排查

### 画面完全没有 Bloom

优先检查：

```text
材质 shader 是否仍提前 ACES/tone mapping
hdrSceneColor 是否真的是 R16G16B16A16_SFLOAT
downsample mip0 是否能看到缩小后的场景
upsample pass 是否开启 additive blending
composite binding 1 是否绑定 bloom.mips[0]
bloomStrength 是否太低
```

调试办法：让 `bloomComposite.frag` 只输出 bloom：

```hlsl
output.color = float4(bloom, 1.0);
```

如果全黑，问题在 downsample/upsample；如果能看到模糊画面，问题在 composite 或参数。

### Bloom 让画面发灰

通常是：

```text
bloomStrength 太大
mipCount 太多
filterRadius 太大
HDR scene 里 skybox 太亮，整张图都被 bloom 了
```

解决：

```text
bloomStrength 从 0.02 ~ 0.06 调
mipCount 先固定 5
filterRadius 从 0.003 ~ 0.008 调
必要时降低 skybox 输出亮度
```

### 出现黑块

LearnOpenGL 的文章也提到这个问题。第一层 downsample 后如果出现完全黑的像素，后续 downsample 会把黑块扩散。解决方法是在 downsample shader 末尾加：

```hlsl
downsample = max(downsample, 0.0001.xxx);
```

### 高亮区域出现闪烁或 fireflies

如果 HDR 里有极亮的点，例如 emissive 几百上千，downsample 时可能出现亮点闪烁。解决：

```text
第一层 downsample 使用 Karis average
降低 emissiveStrength
给 emissive 做合理艺术控制，不要无限大
用 RenderDoc 看每一级 mip 的值
```

### Validation 报 layout 错

每张 mip 在作为 render target 前：

```text
VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL
```

作为 sampled image 前：

```text
VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
```

upsample 特别容易错，因为 `dstMip` 原本是 shader read，接着要转成 attachment，写完又要转回 shader read。

### UI 被 Bloom 或变暗

原因：UI 被画进 HDR scene 里了。

解决：把 `cmdDrawSkybox()` 里的 `drawUI(cmd)` 移走，放到 composite 后的 swapchain pass。

## 19. 推荐实现顺序

不要一口气全部做完。建议按下面顺序推进：

1. **HDR Scene Pass**
   先创建 `hdrSceneColor`，让场景写入 HDR，再用 composite 原样显示。

2. **移动 Tone Mapping**
   把 `PBRTexture.frag` 和 `pbrScene.frag` 的 ACES 移到 composite shader，确保画面接近原本效果。

3. **Downsample Mip Chain**
   只做 downsample，不做 upsample。临时显示 `bloom.mips[0]`、`bloom.mips[1]`，确认能看到逐级缩小的 HDR 场景。

4. **Upsample Additive**
   从最小 mip 往回加到 `bloom.mips[0]`。临时只显示 `bloom.mips[0]`，确认它是模糊光晕。

5. **Composite**
   `lerp(hdr, hdr + bloom, bloomStrength)`，再 ACES/gamma。

6. **UI 调参**
   加 `enableBloom`、`bloomStrength`、`filterRadius`、`exposure`。

## 20. 你需要记住的核心图

```text
Full HDR
   |
   v
  A  half resolution
   |
   v
  B  quarter resolution
   |
   v
  C
   |
   v
  D
   |
   v
  E  smallest

Upsample:

E' = E
D' = D + blur(E')
C' = C + blur(D')
B' = B + blur(C')
A' = A + blur(B')

Final:

color = lerp(HDR, HDR + A', bloomStrength)
```

这就是这个方案最核心的精神：不是找亮区，不是堆大 blur，而是用 mip chain 提取低频光能，再逐级扩散回去。

## 21. 参考资料

- [LearnOpenGL: Phys. Based Bloom](https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom)
- [Call of Duty: Advanced Warfare, SIGGRAPH 2014 Advances in Real-Time Rendering](https://advances.realtimerendering.com/s2014/)
- [LearnOpenGL: Bloom](https://learnopengl.com/Advanced-Lighting/Bloom)

