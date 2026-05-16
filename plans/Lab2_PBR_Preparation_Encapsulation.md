# Lab2 PBR 前置封装计划

目标：在进入 Lab2 PBR 编码前，先做一轮小而稳的封装，减少 Lab1 中已经暴露出来的 Vulkan boilerplate，并为 PBR/IBL 的资源、descriptor、pipeline 组织做好准备。

这份计划强调“必要封装”，不是做完整引擎。Lab2 的核心仍然应该是理解 PBR，而不是陷入框架设计。

## 1. Lab2 会比 Lab1 多什么

Lab1 Shadow Map 主要围绕：

- shadow map depth texture
- scene UBO / shadow UBO
- scene pipeline / shadow pipeline / light pipeline
- shadow shader 参数

Lab2 PBR 会新增：

- 材质参数：baseColor、roughness、metallic、specular、ao、emissive。
- 多个光源参数。
- 法线贴图、金属度贴图、粗糙度贴图、AO 贴图。
- 后续 IBL：environment cube、irradiance cube、prefiltered cube、BRDF LUT。
- skybox pipeline。
- 更多 descriptor binding。
- 更多 debug view：normal、roughness、metallic、NdotV、F0、diffuse/specular、IBL contribution。

所以 Lab2 前最应该封装的是：

```text
rendering begin/end
descriptor 写入
pipeline 创建
per-frame resource
texture / material 数据结构
```

## 2. 先不要做的封装

暂时不要做：

- 完整 RenderGraph。
- ECS / Scene Graph。
- 多后端渲染抽象。
- 完整材质系统编辑器。
- 自动反射 shader descriptor。
- 复杂 asset manager。

原因：这些都很重要，但会抢走 PBR 学习本身的注意力。现在应该先让 Lab2 可以稳定写出来。

## 3. 第一阶段：Rendering Helpers

### 目标

把 Lab1 中重复的 dynamic rendering 代码抽到 `base/vk_rendering.h/.cpp`。

建议接口：

```cpp
namespace vkutil {
    void cmdSetViewportAndScissor(
        VkCommandBuffer cmd,
        uint32_t width,
        uint32_t height);

    VkRenderingAttachmentInfo colorAttachment(
        VkImageView view,
        VkImageLayout layout,
        VkClearColorValue clearColor,
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE);

    VkRenderingAttachmentInfo depthAttachment(
        VkImageView view,
        VkImageLayout layout,
        float clearDepth = 1.0f,
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE);

    void cmdBeginColorDepthRendering(
        VkCommandBuffer cmd,
        VkExtent2D extent,
        const VkRenderingAttachmentInfo& color,
        const VkRenderingAttachmentInfo& depth);

    void cmdBeginDepthOnlyRendering(
        VkCommandBuffer cmd,
        VkExtent2D extent,
        const VkRenderingAttachmentInfo& depth);
}
```

### 为什么先做它

PBR Lab 至少会有：

- scene color + depth pass
- skybox pass
- 后续 IBL 预计算 offscreen pass
- 可能的 BRDF LUT pass

这些都会重复 attachment/renderingInfo/viewport/scissor。

### 验收标准

- Lab1 的 `drawShadowMap()`、`drawScene()` 可以减少 30-50 行重复代码。
- 运行结果完全不变。
- RenderDoc 中 pass 和 attachment 仍然清晰可见。

## 4. 第二阶段：Descriptor Helpers

### 目标

先封装 descriptor 写入小工具，不急着做 descriptor allocator。

建议文件：

```text
base/vk_descriptors.h
base/vk_descriptors.cpp
```

建议接口：

```cpp
namespace vkutil {
    VkDescriptorBufferInfo descriptorBufferInfo(
        VkBuffer buffer,
        VkDeviceSize range,
        VkDeviceSize offset = 0);

    VkWriteDescriptorSet writeUniformBuffer(
        VkDescriptorSet dstSet,
        uint32_t binding,
        const VkDescriptorBufferInfo* bufferInfo);

    VkWriteDescriptorSet writeCombinedImageSampler(
        VkDescriptorSet dstSet,
        uint32_t binding,
        const VkDescriptorImageInfo* imageInfo);

    void updateDescriptorSet(
        VkDevice device,
        std::span<const VkWriteDescriptorSet> writes);
}
```

### 为什么 Lab2 需要它

`pbrbasic` 只有两个 UBO binding：

```text
binding 0: matrices
binding 1: light params
```

但 `pbrtexture / pbribl` 会扩展成：

```text
binding 0: scene UBO
binding 1: params UBO
binding 2: irradiance cube
binding 3: BRDF LUT
binding 4: prefiltered cube
binding 5: albedo
binding 6: normal
binding 7: AO
binding 8: metallic
binding 9: roughness
```

没有 descriptor helper 时，`setupDescriptors()` 会迅速变得又长又脆。

### 验收标准

- Lab1 descriptor 绑定不变。
- Lab2 PBR descriptor 代码可以用 helper 写清楚。
- HLSL `register(t#)` 和 C++ binding 的对应关系在代码里更容易看。

## 5. 第三阶段：Pipeline Builder

### 目标

封装常见 pipeline state，减少 scene/skybox/debug pipeline 的重复配置。

建议文件：

```text
base/vk_pipeline_builder.h
base/vk_pipeline_builder.cpp
```

建议先做轻量版本：

```cpp
struct PipelineBuilder {
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkPipelineColorBlendStateCreateInfo colorBlend;
    VkPipelineDepthStencilStateCreateInfo depthStencil;
    VkPipelineViewportStateCreateInfo viewport;
    VkPipelineMultisampleStateCreateInfo multisample;
    VkPipelineDynamicStateCreateInfo dynamicState;
    VkPipelineVertexInputStateCreateInfo* vertexInput = nullptr;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };

    VkPipeline build(VkDevice device, VkPipelineCache cache) const;
};
```

也可以提供几个配置函数：

```cpp
void setShaders(...);
void setInputTopology(VkPrimitiveTopology topology);
void setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
void enableDepthTest(bool depthWrite, VkCompareOp compareOp);
void disableDepthTest();
void setColorAttachmentFormat(VkFormat format);
void setDepthFormat(VkFormat format);
```

### Lab2 中的典型 pipeline

```text
PBR pipeline:
    color + depth
    vertex input: Position / Normal / UV / Tangent
    cull back
    depth test/write on

Skybox pipeline:
    color + depth
    vertex input: Position
    cull front 或 none
    depth test on, depth write off

Debug fullscreen pipeline:
    no vertex input
    color only
    depth off
```

### 验收标准

- 每个 `createXPipeline()` 只描述差异点。
- pipeline layout、shader path、vertex input、attachment format 仍然显式。
- 不隐藏太多 Vulkan 细节，方便你继续学习。

## 6. 第四阶段：FrameResources

### 目标

把 per-frame UBO 和 descriptor set 放到同一个结构里。

建议：

```cpp
struct Lab2FrameResources {
    AllocatedBuffer sceneBuffer;
    AllocatedBuffer paramsBuffer;
    VkDescriptorSet pbrSet{ VK_NULL_HANDLE };
    VkDescriptorSet skyboxSet{ VK_NULL_HANDLE };
};

std::array<Lab2FrameResources, maxConcurrentFrames> frames;
```

### 为什么要做

PBR 会有更多每帧更新的数据：

- camera matrices
- camera position
- light positions / colors
- exposure / gamma
- debug mode
- 后续可能有 timestamp query

把每帧资源放一起，比平行数组更不容易乱。

### 验收标准

- `frames[currentBuffer].sceneBuffer`
- `frames[currentBuffer].pbrSet`
- `frames[currentBuffer].skyboxSet`

这些访问路径一眼能看出“当前帧使用的资源”。

## 7. 第五阶段：PBR 数据结构

这一部分建议先放在 Lab2 自己的 `lab2.h`，不要急着放 `base`。

### 材质参数

如果先做 `pbrbasic` 风格，可以使用 push constants：

```cpp
struct PBRMaterialPushConstants {
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor{0.0f};
    float roughnessFactor{0.5f};
    float aoFactor{1.0f};
    int useTextureMaps{0};
};
```

如果进入 `pbrtexture`，建议材质结构扩展为：

```cpp
struct PBRMaterial {
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor{0.0f};
    float roughnessFactor{0.5f};
    float aoFactor{1.0f};

    AllocatedTexture* albedoMap{ nullptr };
    AllocatedTexture* normalMap{ nullptr };
    AllocatedTexture* metallicMap{ nullptr };
    AllocatedTexture* roughnessMap{ nullptr };
    AllocatedTexture* aoMap{ nullptr };
};
```

注意：

- 一开始可以用固定材质数组，不要马上做完整 glTF material parser。
- 等你读完 `pbrtexture` 和 `VulkanglTFModel` 后，再决定是否接入 glTF 材质。

### 场景 UBO

建议：

```cpp
struct PBRSceneUBO {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
    glm::vec4 cameraPos;
};
```

### 光照参数 UBO

建议：

```cpp
struct PBRParamsUBO {
    glm::vec4 lightPositions[4];
    glm::vec4 lightColors[4];
    float exposure{ 4.5f };
    float gamma{ 2.2f };
    int debugMode{ 0 };
};
```

注意 HLSL/C++ 对齐。`vec3` 尽量用 `vec4` 传，少踩坑。

## 8. 第六阶段：Texture Set / IBL Resources

Lab2 如果只做基础 PBR，可以先不封装 IBL。

如果要进入 IBL，可以准备：

```cpp
struct IBLResources {
    AllocatedTexture environmentCube;
    AllocatedTexture irradianceCube;
    AllocatedTexture prefilteredCube;
    AllocatedTexture brdfLut;
};
```

但这里有一个现实点：

原项目的 `vks::TextureCubeMap` 和 `vks::Texture2D` 已经有 KTX 加载逻辑。如果你当前自己的 `AllocatedTexture` 只支持从 pixels 创建 2D texture，那么短期内可以继续复用原项目 `VulkanTexture`。

推荐策略：

```text
Lab2 初版:
    先复用 vks::Texture2D / TextureCubeMap 加载 KTX 和 cubemap

等 Lab2 跑通:
    再考虑把常用 texture loading 接到自己的 AllocatedTexture/VMA 封装
```

不要为了统一资源封装，先卡在 KTX/cubemap 加载上。

## 9. 推荐执行顺序

### Step 0：阅读 PBR examples

阅读顺序：

```text
pbrbasic
-> pbrtexture
-> pbribl
-> gltfscenerendering
```

每个 example 重点看：

- UBO 有哪些。
- push constants 传什么。
- descriptor binding 如何对应 shader。
- pipeline vertex input 需要哪些 vertex components。
- texture 从哪里加载，如何写入 descriptor。
- draw 时绑定哪个 descriptor set。

### Step 1：先做 Rendering Helpers

修改范围：

```text
base/vk_rendering.h/.cpp
base/CMakeLists.txt
examples/lab1/lab1.cpp
```

验收：

- Lab1 运行结果不变。

### Step 2：做 Descriptor Helpers

修改范围：

```text
base/vk_descriptors.h/.cpp
base/CMakeLists.txt
examples/lab1/lab1.cpp
```

验收：

- Lab1 descriptor 绑定不变。
- RenderDoc 里 binding 仍正确。

### Step 3：开 Lab2 骨架

Lab2 初版只实现 `pbrbasic`：

```text
模型：sphere / teapot / torusknot / venus
材质：roughness / metallic / baseColor
光源：4 个点光
shader：Cook-Torrance BRDF
UI：切换材质、roughness、metallic、debug mode
```

暂时不做 IBL。

### Step 4：Pipeline Builder

当 Lab2 出现 skybox pipeline 或 debug pipeline 时，再封装 PipelineBuilder。

不要在 Lab2 开始前就把 PipelineBuilder 做得过大。

### Step 5：进入 Texture PBR

加入：

```text
albedo
normal
metallic
roughness
ao
```

这一步 descriptor helper 会开始真正体现价值。

### Step 6：进入 IBL

加入：

```text
environment cubemap
irradiance cubemap
prefiltered cubemap
BRDF LUT
skybox
```

这一步再考虑 `IBLResources`。

## 10. Lab2 第一版建议范围

第一版不要太大，建议只做：

- 一个模型。
- 一个 PBR shader。
- 材质参数 push constants。
- 4 个点光源。
- UI 调 roughness / metallic / baseColor。
- Debug view：baseColor、normal、NdotL、roughness、metallic、diffuse、specular。

先做到这一版，你就能真正理解 PBR 方程。

第二版再做：

- glTF 材质贴图。
- normal map。
- AO / metallic / roughness texture。

第三版再做：

- IBL。
- skybox。
- BRDF LUT。
- irradiance / prefilter。

## 11. 不要踩的坑

### 坑 1：同时重构和实现 PBR

不要一边大改框架，一边写 PBR shader。建议：

```text
先小步封装 Lab1
确认 Lab1 结果不变
再开 Lab2
```

### 坑 2：一上来做完整 IBL

IBL 需要 cubemap、offscreen rendering、mipmap、BRDF LUT、prefilter。如果你还没把 Cook-Torrance BRDF 写明白，直接做 IBL 会很痛苦。

### 坑 3：C++ 和 HLSL 对齐不一致

PBR 参数很多，尽量：

- 用 `vec4` 对齐。
- 少用裸 `vec3`。
- C++ struct 和 HLSL cbuffer/push constant 字段顺序保持一致。

### 坑 4：descriptor binding 不写文档

Lab2 descriptor 会比 Lab1 多很多。建议在 `lab2.h` 或 README 里写清楚：

```text
binding 0: SceneUBO
binding 1: ParamsUBO
binding 2: irradianceMap
binding 3: brdfLUT
binding 4: prefilteredMap
binding 5: albedoMap
...
```

### 坑 5：忽略颜色空间

PBR 中颜色空间很重要：

- albedo/baseColor 通常是 sRGB。
- normal/roughness/metallic/AO 是 linear data。
- 最终输出需要 tone mapping / gamma correction。

这部分是 Lab2 的重点知识点之一。

## 12. 最推荐你现在做什么

最近的顺序建议：

```text
1. 阅读 pbrbasic，整理 UBO / push constants / descriptor / shader 数据流。
2. 封装 vk_rendering helpers。
3. 用 helper 回改 Lab1，确认 Lab1 结果不变。
4. 阅读 pbrtexture，重点看多纹理 descriptor。
5. 封装 descriptor helpers。
6. 开 Lab2 basic PBR。
```

这个节奏比较安全：既能减少 Lab2 里的重复代码，又不会把你拖进大重构泥潭。我们的目标是让封装服务 PBR 学习，而不是让 PBR 变成封装系统的附属品。

