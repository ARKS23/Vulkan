# Lab1 封装建议：从 Shadow Lab 到可复用渲染框架

Lab1 现在已经完成了 Shadow Map、PCF、Poisson PCF、PCSS、Debug Views、UI 调参和光源可视化。这个阶段可以开始做“轻量封装”，但不要一下子把实验代码改成大型引擎结构。

封装目标应该是：

- 减少 Vulkan boilerplate 重复。
- 让后续 Lab 可以复用资源创建、descriptor、pipeline、rendering begin/end。
- 保留算法实验的灵活性，不要把 PCF/PCSS 这类快速变化的逻辑封死。

## 1. 最值得优先封装的部分

### 1.1 Dynamic Rendering Begin Helpers

Lab1 里 `drawShadowMap()`、`drawScene()`、`drawQuad()` 都在重复写：

- `VkRenderingAttachmentInfo`
- `VkRenderingInfo`
- `vkCmdBeginRendering`
- viewport / scissor 设置

建议封装到 `base/vk_rendering.h/.cpp`。

推荐函数：

```cpp
namespace vkutil {
    void cmdSetViewportAndScissor(
        VkCommandBuffer cmd,
        uint32_t width,
        uint32_t height);

    void cmdBeginColorDepthRendering(
        VkCommandBuffer cmd,
        VkImageView colorView,
        VkImageLayout colorLayout,
        VkImageView depthView,
        VkImageLayout depthLayout,
        VkExtent2D extent,
        VkClearColorValue clearColor,
        float clearDepth = 1.0f);

    void cmdBeginDepthOnlyRendering(
        VkCommandBuffer cmd,
        VkImageView depthView,
        VkImageLayout depthLayout,
        VkExtent2D extent,
        float clearDepth = 1.0f);
}
```

收益：

- 后续 shadow pass、offscreen pass、GBuffer pass 都能复用。
- Lab 代码会更聚焦在“绑定什么 pipeline / descriptor / draw 什么模型”。

### 1.2 Shadow Map Resource

现在 Lab1 里的 `ShadowMap` 只保存 texture、extent、format，这是好的。下一步可以把创建和销毁逻辑从 `VulkanExample` 中拆出去。

建议保留为 Lab1 专用或半通用结构：

```cpp
struct ShadowMapResource {
    AllocatedTexture texture;
    VkExtent2D extent{2048, 2048};
    VkFormat format{VK_FORMAT_D16_UNORM};

    void create(VkDevice device, VmaAllocator allocator);
    void destroy(VkDevice device, VmaAllocator allocator);
};
```

注意：

- 先不要把它做成复杂的 `RenderTarget` 系统。
- Shadow map 以后会扩展 CSM、cube shadow、VSM，所以可以先保持简单。

### 1.3 Descriptor Write Helpers

`setupDescriptors()` 现在重复写了 debug / offscreen / scene 三套 descriptor set。建议封装“写 descriptor”的小工具，而不是立刻做完整 descriptor allocator。

可以先加：

```cpp
namespace vkutil {
    VkDescriptorBufferInfo bufferInfo(
        VkBuffer buffer,
        VkDeviceSize range,
        VkDeviceSize offset = 0);

    VkWriteDescriptorSet writeBuffer(
        VkDescriptorSet dstSet,
        uint32_t binding,
        const VkDescriptorBufferInfo* bufferInfo);

    VkWriteDescriptorSet writeImage(
        VkDescriptorSet dstSet,
        uint32_t binding,
        const VkDescriptorImageInfo* imageInfo);
}
```

收益：

- 后续 Lab2/PBR/IBL 会有更多 texture 和 UBO，手写 descriptor 会越来越烦。
- 这一步风险低，改动小。

### 1.4 Pipeline Builder

你已经把 `createDebugPipeline()`、`createScenePipeline()`、`createShadowPipeline()`、`createLightPipeline()` 拆开了，这是正确方向。

下一步可以封装一个轻量 `PipelineBuilder`，负责收集常见 pipeline state：

```cpp
struct PipelineBuilder {
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkPipelineColorBlendStateCreateInfo colorBlend;
    VkPipelineDepthStencilStateCreateInfo depthStencil;
    VkPipelineViewportStateCreateInfo viewport;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineDynamicStateCreateInfo dynamicState;
    VkPipelineRenderingCreateInfo renderingInfo;
    VkPipelineLayout layout;

    VkPipeline build(VkDevice device, VkPipelineCache cache);
};
```

但建议先只支持你当前需要的几种配置：

- color + depth scene pipeline
- depth-only shadow pipeline
- fullscreen debug pipeline
- simple unlit mesh pipeline

不要一开始就写成万能 pipeline system。

## 2. 第二阶段再封装的部分

### 2.1 Render Pass / Render Stage 类

当你进入 PBR、Deferred、IBL、PostProcess 后，可以考虑把每个 pass 拆成对象：

```cpp
class ShadowPass {
public:
    void createResources(...);
    void createPipeline(...);
    void update(...);
    void draw(VkCommandBuffer cmd, const Scene& scene);
};

class ScenePass {
public:
    void createPipeline(...);
    void draw(VkCommandBuffer cmd, const Scene& scene);
};
```

但现在不要急。

原因：

- Lab1 还在快速实验 PCSS、debug view、timestamp query。
- 过早拆 pass 类会让你每改一个参数都要跳很多文件。

建议等你做完 timestamp query 和 README 之后再拆。

### 2.2 Frame Resources

现在你有：

```cpp
std::array<UniformBuffers, maxConcurrentFrames> uniformBuffers;
std::array<DescriptorSets, maxConcurrentFrames> descriptorSets;
```

后续可以合并成：

```cpp
struct FrameResources {
    UniformBuffers uniformBuffers;
    DescriptorSets descriptorSets;
};

std::array<FrameResources, maxConcurrentFrames> frames;
```

收益：

- 更符合“每帧资源”的思维。
- 后续加 timestamp query、per-frame descriptor、staging buffer 会更清楚。

### 2.3 Scene / Model 管理

现在场景模型直接放在 Lab1：

```cpp
std::vector<vkglTF::Model> scenes;
vkglTF::Model lightSphere;
```

后续可以抽一个非常薄的 `SceneAssets`：

```cpp
struct SceneAssets {
    std::vector<vkglTF::Model> scenes;
    std::vector<std::string> sceneNames;
    vkglTF::Model lightSphere;
};
```

但不建议现在做复杂 ECS 或 Scene Graph。

## 3. 暂时不要封装的部分

### 3.1 Shadow 算法参数

这些先保留在 Lab1：

- `minShadowBias`
- `slopeShadowBias`
- `shadowMode`
- `lightSize`
- `PCFRadius`
- `PoissonSampleCount`
- `debugMode`

原因：

- 它们是实验变量，不是稳定引擎接口。
- 你还要写文档、截图、做性能对比，放在 Lab1 里更直观。

### 3.2 PCF / PCSS Shader 逻辑

PCF、Poisson、PCSS 先留在 shader 中，不要试图抽象成跨 Lab 通用 shader library。

后续如果多个实验都需要阴影，再考虑：

```text
shaders/hlsl/common/shadow.hlsli
```

现在先别拆，否则调 shader 会更麻烦。

### 3.3 大型 RenderGraph

RenderGraph 很重要，但现在不是第一优先级。

建议路线：

```text
先封装 dynamic rendering helper
-> 再拆 pass 类
-> 再做简单 FrameGraph / RenderGraph
```

不要直接从 Lab1 跳到完整 RenderGraph。

## 4. 推荐重构顺序

### Step 1：封装 Rendering Helpers

目标：

- `cmdSetViewportAndScissor`
- `cmdBeginColorDepthRendering`
- `cmdBeginDepthOnlyRendering`

验收：

- `drawShadowMap()` 少掉 attachment/renderingInfo boilerplate。
- `drawScene()` 少掉 color/depth attachment boilerplate。
- 程序运行结果不变。

### Step 2：封装 ShadowMapResource

目标：

- 把 `createShadowResources()` 和 `destroyShadowResources()` 的核心逻辑迁移到 `ShadowMapResource`。

验收：

- Lab1 仍然控制 shadow map 参数。
- 资源创建/销毁职责更集中。

### Step 3：封装 Descriptor Helpers

目标：

- 减少 `setupDescriptors()` 里手写 `VkDescriptorBufferInfo` 和 `VkWriteDescriptorSet` 的重复。

验收：

- descriptor set 结构不变。
- HLSL binding 不变。

### Step 4：封装 PipelineBuilder

目标：

- 减少四个 pipeline 创建函数中的重复 state。

验收：

- 每个 pipeline 函数只描述差异点：shader、vertex input、cull mode、depth mode、attachment format。
- 不改变 pipeline layout 和 shader 绑定。

### Step 5：合并 FrameResources

目标：

- 把 per-frame UBO 和 descriptor set 放到一起。

验收：

- `currentBuffer` 访问更直观。
- 为 timestamp query 做准备。

## 5. 推荐文件放置

通用工具放 `base`：

```text
base/vk_rendering.h
base/vk_rendering.cpp
base/vk_descriptors.h
base/vk_descriptors.cpp
base/vk_pipeline_builder.h
base/vk_pipeline_builder.cpp
```

Lab1 专用结构先放 Lab1：

```text
examples/lab1/lab1.h
examples/lab1/lab1.cpp
```

如果后续 Lab2/Lab3 也复用，再上移到 `base`。

原则：

```text
被两个以上 Lab 复用，再放 base。
只服务 Shadow Lab 的，先留在 Lab1。
```

## 6. 判断是否值得封装的标准

值得封装：

- 重复出现三次以上。
- Vulkan boilerplate 多，但业务含义稳定。
- 后续 Lab 肯定会复用。
- 封装后不影响你调试 RenderDoc。

暂时不封装：

- 算法还在变化。
- 参数还在实验。
- 你还没能清楚说出稳定接口。
- 封装后会让代码跳转更多、理解更难。

## 7. 当前最推荐的行动

最近不要大改架构。建议顺序是：

```text
1. 先完成 Lab1 README 和截图。
2. 给 DebugData 补 valid/default。
3. 加 timestamp query，记录性能。
4. 再开始 Step 1：vk_rendering helpers。
```

这样做比较稳：先把实验成果固定下来，再用小步重构提高复用性。Lab1 现在已经有作品价值了，封装的目标是让它更好维护，而不是把它变成另一个难以调试的大迷宫。

