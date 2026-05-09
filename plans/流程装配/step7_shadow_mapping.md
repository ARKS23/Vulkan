# Step 7：Shadow Mapping，从 Offscreen Color 进阶到 Depth Shadow Map

你现在已经完成了 Step 6：先把场景画到 `offscreenColor`，再把它当作纹理采样到 swapchain。

下一步最适合进入 **Shadow Mapping**。原因很简单：阴影贴图本质上也是多 pass 渲染。

```text
Step 6:
    scene pass -> offscreen color texture
    blit pass  -> sample color texture -> swapchain

Step 7:
    shadow pass -> offscreen depth texture
    scene pass  -> sample depth texture -> swapchain
```

这一步会把你从“会组织 Vulkan 渲染流程”推到“开始组织图形算法的数据流”。后面的 PCF、PCSS、VSM、CSM、SSAO、deferred shading 都会反复用到这套思维。

## 1. 本轮目标

建议新开一个 example，例如：

```text
examples/Lab1ShadowMap/
shaders/hlsl/lab1_shadowmap/
```

不要继续把 Lab0 越堆越大。Lab0 已经完成了基础渲染底座验证，Lab1 应该成为你的第一个“算法型 sample”。

本轮完成标准：

```text
1. 创建 shadow depth image
2. 第一遍从 light 视角渲染 depth-only pass
3. 第二遍从 camera 视角渲染场景，并采样 shadow map
4. 能切换 debug view，直接显示 shadow map
5. 加入最基础的 hard shadow 和 PCF
```

如果这五件事都跑通，你就已经正式进入 Games202 阴影实验的门口了。

## 2. 先读原项目 shadowmapping sample

建议阅读顺序：

```text
examples/shadowmapping/shadowmapping.cpp
shaders/hlsl/shadowmapping/offscreen.vert
shaders/hlsl/shadowmapping/scene.vert
shaders/hlsl/shadowmapping/scene.frag
shaders/hlsl/shadowmapping/quad.frag
```

重点看这几组内容：

```text
OffscreenPass
-> shadow map 的资源集合：depth image、view、sampler、descriptor

UniformDataOffscreen
-> light 视角 MVP，用于第一遍写 shadow map

UniformDataScene
-> camera 矩阵 + lightSpace 矩阵 + lightPos，用于第二遍做阴影判断

prepareOffscreenFramebuffer()
-> 创建可作为 depth attachment 且可被 shader sample 的 depth image

setupDescriptors()
-> scene pass 和 debug pass 都会采样 shadow map

preparePipelines()
-> offscreen depth-only pipeline、scene shadow pipeline、debug pipeline

buildCommandBuffers()
-> 第一遍生成 shadow map，第二遍采样 shadow map
```

但要注意：原 sample 用的是传统 `VkRenderPass + VkFramebuffer`。
你现在的 Lab0 已经走了 Vulkan 1.3 dynamic rendering，所以不要机械照抄它的 render pass / framebuffer 部分。

你要学习的是它的 **资源关系和数据流**：

```text
depth image 既是第一遍的 depth attachment
又是第二遍 fragment shader 里的 sampled texture
```

这和你 Step6 的 `offscreenColor` 是同一个模式，只是 attachment 类型从 color 变成了 depth。

## 3. Shadow Mapping 的核心数据流

Shadow Mapping 分两遍：

```text
Pass 1：Shadow Pass
    视角：light
    输出：shadowDepth
    颜色附件：无
    深度附件：shadowDepth
    shader：通常只需要 vertex shader

Pass 2：Scene Pass
    视角：camera
    输出：swapchain 或 offscreenColor
    输入：shadowDepth sampler
    shader：正常光照 + shadow compare
```

换句话说：

```text
第一遍回答：“从灯光看过去，最近的表面离灯有多远？”
第二遍回答：“从相机看到的这个点，是否比 shadow map 中记录的位置更远？”
```

如果当前片元在 light space 里的深度大于 shadow map 存的深度，说明它被前面的物体挡住了，于是处于阴影中。

## 4. 建议你新建的资源结构

可以先在 `lab1.h` 里写一个简单结构，不急着抽进 `vk_types.h`：

```cpp
struct ShadowMap {
    AllocatedImage depth;
    VkSampler sampler{ VK_NULL_HANDLE };
    VkDescriptorImageInfo descriptor{};
    VkExtent2D extent{ 2048, 2048 };
    VkFormat format{ VK_FORMAT_D16_UNORM };
};
```

为什么暂时不直接抽公共结构？

因为 shadow map 很快会分化：

```text
普通 shadow map      -> 2D depth texture
CSM                  -> depth texture array
Omni shadow          -> cube depth texture
VSM / EVSM           -> color texture，存 depth moments
```

先让 `Lab1ShadowMap` 自己长出来。等你做完 hard shadow + PCF 后，再看是否抽象到 `vk_types.h`。

## 5. 创建 shadow depth image

你需要一个 depth image，同时具备：

```cpp
VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
VK_IMAGE_USAGE_SAMPLED_BIT
```

建议第一版：

```cpp
shadowMap.extent = { 2048, 2048 };
shadowMap.format = VK_FORMAT_D16_UNORM;

shadowMap.depth = vkutil::createAllocatedImage(
    device,
    allocator,
    VkExtent3D{ shadowMap.extent.width, shadowMap.extent.height, 1 },
    shadowMap.format,
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    VK_IMAGE_ASPECT_DEPTH_BIT);
```

sampler 建议：

```text
filter: NEAREST 起步，PCF 阶段再尝试 LINEAR
address mode: CLAMP_TO_EDGE
border color: OPAQUE_WHITE
```

`OPAQUE_WHITE` 很重要。shadow map 外部区域通常希望被视为“没有遮挡”，否则边缘外采样容易产生错误黑影。

descriptor 记录采样 layout：

```cpp
shadowMap.descriptor.sampler = shadowMap.sampler;
shadowMap.descriptor.imageView = shadowMap.depth.imageView;
shadowMap.descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
```

## 6. 你需要扩展 image layout helper

Step6 的 color offscreen 主要用：

```text
SHADER_READ_ONLY_OPTIMAL -> ATTACHMENT_OPTIMAL
ATTACHMENT_OPTIMAL      -> SHADER_READ_ONLY_OPTIMAL
```

Shadow map 更建议用 depth 专用 layout：

```text
DEPTH_STENCIL_READ_ONLY_OPTIMAL -> DEPTH_ATTACHMENT_OPTIMAL
DEPTH_ATTACHMENT_OPTIMAL       -> DEPTH_STENCIL_READ_ONLY_OPTIMAL
UNDEFINED                      -> DEPTH_ATTACHMENT_OPTIMAL
```

你可以先在 `vk_images.cpp` 里给 `cmdTransitionImageLayout()` 增加 depth 分支。

第一版不要追求覆盖所有 Vulkan layout，只支持 Lab1 用到的三种即可。你的目标是理解 barrier，而不是写一个完整引擎库。

## 7. UBO 设计

建议拆成两个 UBO：

```cpp
struct ShadowPassUBO {
    glm::mat4 lightViewProj;
};

struct ScenePassUBO {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
    glm::mat4 lightViewProj;
    glm::vec4 lightDir;
};
```

如果做方向光，建议先用正交投影：

```cpp
glm::vec3 lightDir = glm::normalize(glm::vec3(-1.0f, -2.0f, -1.0f));
glm::vec3 lightPos = -lightDir * 10.0f;

glm::mat4 lightView = glm::lookAt(
    lightPos,
    glm::vec3(0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f));

glm::mat4 lightProj = glm::ortho(
    -10.0f, 10.0f,
    -10.0f, 10.0f,
    0.1f, 30.0f);

glm::mat4 lightViewProj = lightProj * lightView;
```

先用正交投影，是因为 Games202 里方向光阴影更常用 orthographic shadow map。
原项目 `shadowmapping.cpp` 使用 perspective light，也可以读，但你的实验建议先从方向光开始。

## 8. Pipeline 规划

至少需要两个 pipeline，建议加一个 debug pipeline：

```text
shadowDepthPipeline
-> depth-only
-> 只写 shadowMap.depth
-> viewport/scissor = shadow map size
-> depth test/write on
-> color attachment count = 0

scenePipeline
-> 正常渲染场景
-> descriptor 里有 scene UBO + shadow map sampler
-> fragment shader 里做 shadow compare

debugShadowPipeline
-> fullscreen triangle
-> 采样 shadow map
-> 把 depth 可视化到屏幕
```

第一版建议把 debug pipeline 先做出来。

原因很朴素：Shadow Mapping 最常见的问题不是“算法不会”，而是 shadow map 根本没写对。
能直接显示 shadow map，你会省掉大量玄学调试。

## 9. HLSL 最小版本

### shadow_depth.vert

第一遍只需要把顶点变换到 light clip space：

```hlsl
struct VSInput {
    [[vk::location(0)]] float3 Pos : POSITION0;
};

struct ShadowUBO {
    float4x4 lightViewProj;
    float4x4 model;
};

cbuffer ubo : register(b0) { ShadowUBO ubo; }

float4 main(VSInput input) : SV_POSITION {
    return mul(ubo.lightViewProj, mul(ubo.model, float4(input.Pos, 1.0)));
}
```

第一版可以不写 fragment shader，或者写一个空的 fragment shader。

### scene.vert

第二遍要额外输出 light space 坐标：

```hlsl
static const float4x4 biasMat = float4x4(
    0.5, 0.0, 0.0, 0.5,
    0.0, 0.5, 0.0, 0.5,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0);

output.ShadowCoord = mul(biasMat, mul(ubo.lightViewProj, worldPos));
```

`biasMat` 的作用是把裁剪空间坐标转成 shadow map 的 UV 空间。

```text
clip/NDC:   [-1, 1]
texture UV: [ 0, 1]
```

### scene.frag

最小 hard shadow：

```hlsl
float shadowCompare(float4 shadowCoord)
{
    float3 proj = shadowCoord.xyz / shadowCoord.w;

    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) {
        return 1.0;
    }

    float closestDepth = shadowMapTexture.Sample(shadowMapSampler, proj.xy).r;
    float currentDepth = proj.z;
    float bias = 0.003;

    return (currentDepth - bias > closestDepth) ? 0.2 : 1.0;
}
```

这里的 `0.2` 是环境光兜底，不要让阴影区完全黑掉。

## 10. 第一版不要急着做复杂模型

Lab0 的圆盘是 2D 场景，不太适合观察阴影。

建议 Lab1 先做：

```text
一个 ground plane
一个 cube
一个稍微悬空或偏移的 cube / sphere
```

如果你暂时不想写 cube mesh，可以直接使用原项目的 glTF 模型加载逻辑。
但从学习角度，我更建议先手写一个 cube + plane，因为你会更清楚：

```text
顶点位置
法线
model matrix
depth test
阴影投射关系
```

## 11. 推荐实施顺序

### 11.1 新建 Lab1

复制 Lab0 的当前版本作为起点：

```text
examples/Lab1ShadowMap/lab1_shadowmap.cpp
examples/Lab1ShadowMap/lab1_shadowmap.h
shaders/hlsl/lab1_shadowmap/
```

然后修改 CMake，让它生成一个新的可执行 sample。

### 11.2 先保留 Step6 的双 pass

不要一上来删掉 offscreen color。

建议先保持：

```text
scene -> offscreenColor -> swapchain
```

等 Lab1 跑起来后，再加入：

```text
shadowDepth -> scene -> offscreenColor -> swapchain
```

这样你每一步都能确认画面还活着。

### 11.3 创建 shadowMap.depth，并做 debug 显示

先不要做阴影判断。

只做：

```text
shadow pass 写 depth
debug pass 显示 depth
```

如果 debug view 中能看到从 light 视角的深度分布，再进入下一步。

### 11.4 scene pass 采样 shadow map

给 scene descriptor 加：

```text
binding 0: scene UBO
binding 1: base color texture
binding 2: shadow map sampler
```

然后在 HLSL fragment shader 中做 hard shadow。

### 11.5 加 PCF

先做 3x3 PCF：

```text
对 shadowCoord.xy 周围 9 个采样点分别 compare
求平均
```

这一步开始，你就会明显看到硬阴影边缘变软。

## 12. 最常见 bug 检查表

如果 shadow map 全白：

```text
light camera 没看见物体
near/far 范围不合适
depth pass 没有画 geometry
depth write 没开
viewport/scissor 仍然是窗口大小，而不是 shadow map 大小
```

如果 shadow map 全黑：

```text
depth clear value 错了
compare op / projection 方向错了
debug shader 没有线性化或显示范围不合适
```

如果场景全在阴影中：

```text
shadowCoord 没有除以 w
bias 太小或方向错
lightViewProj 和 shader 矩阵顺序不一致
descriptor imageLayout 和实际 layout 不一致
sampler 采样到了 shadow map 外部黑边
```

如果没有任何阴影：

```text
shadow map 没有被 scene pass 正确绑定
currentDepth / closestDepth 比较方向写反
bias 太大
lightViewProj 覆盖范围太大，深度精度太差
```

如果阴影有条纹或闪烁：

```text
shadow map 分辨率不够
depth bias 需要调
light near/far 范围太大
PCF kernel 太小
物体法线和光照方向不稳定
```

## 13. 你这轮应该重点理解什么

不要只追求“画面出现阴影”。

这一轮真正要吃透的是：

```text
1. depth image 如何既当 attachment 又当 texture
2. light space matrix 如何把世界坐标投到 shadow map
3. descriptor 如何把第一 pass 的输出交给第二 pass
4. image layout transition 如何表达读写阶段切换
5. bias 为什么能减少 shadow acne，但又会导致 peter panning
6. PCF 为什么只是多个 hard shadow compare 的平均
```

如果这六点理解了，Shadow Mapping 就不再是“神秘算法”，而是一个非常清晰的多 pass 数据流。

## 14. 完成 Step7 后的下一步

Step7 完成后，建议进入 Games202 阴影专题：

```text
Step 8.1 Hard Shadow 对比实验
Step 8.2 PCF kernel size 对比
Step 8.3 slope-scale depth bias 调参
Step 8.4 PCSS blocker search
Step 8.5 PCSS penumbra estimation
Step 8.6 CSM，为大场景做级联阴影
```

这时你可以再回头抽象：

```text
RenderTarget2D
DepthTarget2D
FullscreenPass
PipelineBuilder
DescriptorWriter
```

现在先不要一口气抽完。
让 Shadow Mapping 这个真实算法把你需要的封装“逼出来”，这样你的代码会更贴着问题长，而不是变成空泛的小引擎架子。

