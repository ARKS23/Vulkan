# Bloom 可复用模块设计笔记

这份文档记录我准备把 Lab2 的 Physically Based Bloom 抽离成通用模块时的设计思路。目标是让 Lab2、Lab3 以及后续实验都能复用同一套 Bloom 资源、管线和录制逻辑，而不是每个 Lab 都复制一份 downsample / upsample 代码。

## 1. 模块目标

Bloom 模块只做一件事：

```text
输入：一张线性 HDR 场景颜色图
处理：downsample mip chain + upsample accumulation
输出：一张线性 HDR bloom 纹理
```

也就是说，Bloom 模块的职责是生成“光晕贡献”，不是负责最终显示。

最终画面应该由另一个 Composite / ToneMapping pass 完成：

```text
HDR Scene + Bloom Texture
    -> exposure
    -> tone mapping
    -> gamma correction
    -> swapchain
```

这个边界非常重要。Lab2 现在把 Bloom 和最终合成放在一起写是可以工作的，但如果要让 Lab3 复用，就应该把 BloomPass 和 CompositePass 分开。

## 2. 为什么不要让 Bloom 管最终输出

Lab2 的输出链路比较简单：

```text
HDR Scene
    -> Bloom Downsample
    -> Bloom Upsample
    -> Composite + Tone Mapping
    -> Swapchain
```

但 Lab3 会复杂很多：

```text
G-Buffer
    -> Deferred Lighting
    -> SSAO
    -> Bloom
    -> Composite / Debug View
    -> Swapchain
```

Lab3 还需要 G-Buffer debug、SSAO debug、lighting debug 等模式。如果 BloomPass 自己负责 swapchain 输出，它就会和这些调试视图缠在一起，后面维护会很难。

所以我希望 BloomPass 保持纯粹：

```text
BloomPass 只输出 bloom texture
CompositePass 才负责最终显示
```

## 3. 推荐的类边界

建议新增一个可复用类，例如：

```cpp
class BloomPass {
public:
    struct Settings {
        uint32_t mipCount = 5;
        float filterRadius = 0.5f;
        bool useKarisAverage = true;
        bool enabled = true;
    };

    void init(
        VkDevice device,
        VmaAllocator allocator,
        VkDescriptorPool descriptorPool,
        VkPipelineCache pipelineCache,
        const std::string& shaderPath,
        const std::string& fullscreenVertexShader,
        VkFormat hdrFormat
    );

    void resize(VkExtent2D extent, VkFormat hdrFormat, uint32_t mipCount);

    void destroy();

    void updateSource(const VkDescriptorImageInfo& hdrSceneDescriptor);

    void record(
        VkCommandBuffer cmd,
        const Settings& settings
    );

    VkDescriptorImageInfo getBloomDescriptor() const;
    VkExtent2D getExtent() const;
};
```

这个接口表达的是：

```text
init       -> 创建 descriptor layout、pipeline layout、pipeline、sampler
resize     -> 创建 bloom mip images，并为每层 mip 创建 descriptor set
updateSource -> 把当前 HDR scene descriptor 写入第 0 次 downsample 的输入
record     -> 录制 downsample 和 upsample 命令
getBloomDescriptor -> 给外部 composite pass 使用
```

## 4. BloomPass 应该拥有的资源

BloomPass 内部应该管理这些东西：

```cpp
struct BloomMip {
    AllocatedImage image;
    VkDescriptorImageInfo descriptor{};
    VkExtent2D extent{};
};

struct BloomPass {
    std::vector<BloomMip> mips;
    VkSampler sampler;

    VkDescriptorSetLayout sampleSetLayout;
    VkDescriptorSet hdrSourceSet;
    std::vector<VkDescriptorSet> mipSets;

    VkPipelineLayout downsampleLayout;
    VkPipelineLayout upsampleLayout;

    VkPipeline downsamplePipeline;
    VkPipeline upsamplePipeline;
};
```

其中：

```text
hdrSourceSet
    -> 第 0 次 downsample 的输入，采样外部 HDR scene

mipSets[i]
    -> 采样 bloom.mips[i]

mips[0]
    -> 最终 bloom 输出，给 CompositePass 使用
```

## 5. BloomPass 不应该拥有的资源

这些东西不应该放进 BloomPass：

```text
HDR scene color image
swapchain image
G-Buffer
SSAO texture
UI 参数
exposure
tone mapping
gamma correction
final composite pipeline
```

原因是 Bloom 是后处理链中的一个 pass，不应该反向拥有整条渲染链。

Lab2 可以有自己的：

```cpp
AllocatedImage hdrSceneColor;
```

Lab3 可以有自己的：

```cpp
HDRResources hdr;
```

BloomPass 只接收它们的 descriptor：

```cpp
bloom.updateSource(hdr.sceneColor.descriptor);
```

## 6. 推荐的数据流

### Lab2

```text
Scene / PBR / Skybox / Light Proxy
    -> hdrSceneColor

BloomPass
    input  -> hdrSceneColor
    output -> bloom.mips[0]

CompositePass
    input 0 -> hdrSceneColor
    input 1 -> bloom.mips[0]
    output  -> swapchain
```

伪代码：

```cpp
cmdDrawSceneToHDR(cmd);

vkutil::cmdTransitionTrackedImageLayout(
    cmd,
    hdrSceneColor,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    VK_IMAGE_ASPECT_COLOR_BIT
);

bloom.updateSource(hdrSceneDescriptor);
bloom.record(cmd, bloomSettings);

cmdDrawComposite(
    cmd,
    hdrSceneDescriptor,
    bloom.getBloomDescriptor(),
    exposure,
    bloomStrength
);
```

### Lab3

```text
G-Buffer
    -> Deferred Lighting
    -> hdr.sceneColor

BloomPass
    input  -> hdr.sceneColor
    output -> bloom.mips[0]

CompositePass / DebugPass
    input 0 -> hdr.sceneColor
    input 1 -> bloom.mips[0]
    output  -> swapchain
```

伪代码：

```cpp
transitionGBufferForWriting(cmd);
cmdDrawGBuffer(cmd);

transitionGBufferForSampling(cmd);

vkutil::cmdTransitionTrackedImageLayout(
    cmd,
    hdr.sceneColor.image,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_ASPECT_COLOR_BIT
);

cmdDrawDeferredLighting(cmd);

vkutil::cmdTransitionTrackedImageLayout(
    cmd,
    hdr.sceneColor.image,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    VK_IMAGE_ASPECT_COLOR_BIT
);

bloom.updateSource(hdr.sceneColor.descriptor);
bloom.record(cmd, bloomSettings);

cmdDrawComposite(cmd);
```

## 7. Shader 组织方式

推荐把 Bloom 的纯计算函数抽到公共 include：

```text
shaders/hlsl/common/Bloom.hlsli
```

里面放：

```hlsl
float Luminance(float3 color);
float KarisWeight(float3 color);
float3 KarisAverage4(float3 a, float3 b, float3 c, float3 d);
float3 Downsample13Tap(...);
float3 UpsampleTent(...);
```

但资源绑定仍然放在具体 pass shader 里：

```hlsl
Texture2D srcTexture : register(t0);
SamplerState srcTextureSampler : register(s0);
```

原因是 descriptor layout 属于 pass，不应该藏在公共工具函数里。

推荐 shader 文件：

```text
shaders/hlsl/common/Bloom.hlsli
shaders/hlsl/shared/bloomDownsample.frag
shaders/hlsl/shared/bloomUpsample.frag
```

其中 `shared` 里的 shader 可以被 Lab2 和 Lab3 共同加载。

如果暂时不想改编译脚本的 sample 逻辑，也可以先保留：

```text
shaders/hlsl/lab2/downSample.frag
shaders/hlsl/lab2/upSample.frag
shaders/hlsl/lab3/downSample.frag
shaders/hlsl/lab3/upSample.frag
```

它们都 include 同一个：

```hlsl
#include "common/Bloom.hlsli"
```

## 8. Pipeline 设计

BloomPass 至少需要两个 pipeline：

```text
downsamplePipeline
    -> 不开 blending
    -> 每层 mip 都是新写入的滤波结果

upsamplePipeline
    -> 开 additive blending
    -> 小 mip 的模糊结果加回大 mip
```

Downsample：

```cpp
for (uint32_t i = 0; i < mipCount; ++i) {
    src = (i == 0) ? hdrSceneSet : mipSets[i - 1];
    dst = mips[i];

    transition dst -> ATTACHMENT_OPTIMAL;
    begin rendering dst;
    bind downsamplePipeline;
    bind src descriptor;
    push srcResolution + useKarisAverage;
    draw fullscreen triangle;
    end rendering;
    transition dst -> SHADER_READ_ONLY_OPTIMAL;
}
```

Upsample：

```cpp
for (int i = mipCount - 1; i > 0; --i) {
    src = mips[i];
    dst = mips[i - 1];

    transition dst -> ATTACHMENT_OPTIMAL;
    begin rendering dst;
    bind upsamplePipeline;
    bind src descriptor;
    push srcResolution + filterRadius;
    draw fullscreen triangle;
    end rendering;
    transition dst -> SHADER_READ_ONLY_OPTIMAL;
}
```

这里的关键点是：

```text
downsample 不混合
upsample 开 Additive Blend
```

因为 upsample 是把更小 mip 的大范围光晕累加回更高分辨率 mip。

## 9. CompositePass 如何使用 BloomPass

CompositePass 的 descriptor layout 可以设计成：

```text
binding 0 -> HDR scene color
binding 1 -> bloom texture
```

对应 shader：

```hlsl
Texture2D hdrScene : register(t0);
SamplerState hdrSampler : register(s0);

Texture2D bloomTexture : register(t1);
SamplerState bloomSampler : register(s1);
```

最终合成逻辑：

```hlsl
float3 hdrColor = hdrScene.Sample(hdrSampler, uv).rgb;
float3 bloomColor = bloomTexture.Sample(bloomSampler, uv).rgb;

if (enableBloom == 0) {
    bloomColor = 0.0.xxx;
}

float3 color = hdrColor + bloomColor * bloomStrength;
color *= exposure;
color = ACESFilm(color);
color = LinearToSRGB(color);
```

注意这里 CompositePass 使用 BloomPass 的输出：

```cpp
VkDescriptorImageInfo bloomInfo = bloom.getBloomDescriptor();
```

## 10. Resize 处理

窗口尺寸变化时，BloomPass 必须重建 mip chain。

推荐顺序：

```cpp
vkDeviceWaitIdle(device);

destroy frame-size resources:
    HDR scene
    G-Buffer
    SSAO
    BloomPass mips

create frame-size resources:
    HDR scene
    G-Buffer
    SSAO
    BloomPass.resize(newExtent, hdrFormat, mipCount)

update descriptors:
    HDR descriptors
    Bloom source descriptor
    Composite descriptors
```

BloomPass 的 pipeline 不需要因为窗口 resize 重建，除非 HDR format 变化。

## 11. 建议的迁移步骤

第一步：只抽 shader 公共函数。

```text
common/Bloom.hlsli
lab2/downSample.frag include common/Bloom.hlsli
lab2/upSample.frag include common/Bloom.hlsli
```

第二步：抽 C++ 资源结构。

```text
base/vk_bloom.h
base/vk_bloom.cpp
```

先把这些从 Lab2 移进去：

```text
BloomMip
BloomResources
createBloomResources
destroyBloomResources
```

第三步：抽 descriptor 和 pipeline。

```text
createDescriptorSetLayouts
allocateDescriptorSets
updateDescriptorSets
createPipelineLayouts
createPipelines
```

第四步：抽 command recording。

```text
recordDownsample
recordUpsample
record
```

第五步：Lab2 接入 BloomPass。

第六步：Lab3 接入 BloomPass。

这样可以避免一次性重构太大，方便每一步都验证画面是否一致。

## 12. 常见坑

1. Bloom 输入必须是线性 HDR，不能是 tone mapping 后的 LDR。
2. BloomPass 不应该拥有 HDR scene，只应该采样外部 HDR scene。
3. 每个 mip image 在写入前要切到 `VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL`。
4. 每个 mip image 写完后要切回 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`。
5. Downsample pass 不开混合。
6. Upsample pass 开 additive blending。
7. `mips[0]` 是最终 bloom 输出。
8. Composite pass 才做 exposure、tone mapping 和 gamma。
9. 修改 mip 数量或窗口大小后，需要重建 mip images 和 descriptor sets。
10. 如果 shader 放到 `shared` 目录，要确认编译脚本会编译这些 shared shader。

## 13. 我对最终架构的理解

理想状态下，Lab2 和 Lab3 都应该变成这种结构：

```text
Scene Pass / Deferred Lighting Pass
    -> HDR scene

BloomPass
    -> bloom texture

CompositePass
    -> swapchain
```

这样 Bloom 就不再是 Lab2 的专属代码，而是一个真正的后处理模块。

后续如果我要继续做：

```text
Depth of Field
Motion Blur
Auto Exposure
Color Grading
TAA
```

也可以沿用同样的设计思路：

```text
每个后处理 pass 只负责自己的输入、输出和中间资源；
最终显示统一交给 Composite / Tonemap pass。
```

