# Step8: PipelineBuilder 封装指导

这一轮目标是按 vk-guide 那类思路，给当前项目引入一个自己的 `PipelineBuilder`。它不是为了把 Vulkan 全藏起来，而是把创建 graphics pipeline 时最重复、最容易写错的固定流程收进工具层，让 sample 代码只保留“这个 pass 到底需要什么 shader、什么 vertex input、什么 depth/blend/format”。

你现在已经封装了 `vk_images`、`vk_rendering`、`vk_descriptors`，下一步做 `vk_pipelines` 很自然。PipelineBuilder 会成为你之后写 Lab3/Lab4 的“装配台”。

## 1. Lab2 现在的问题

Lab2 里至少有这些 pipeline 创建流程：

- `createScenePipeline()`
- `createSkyboxPipeline()`
- `createLightPipeline()`
- `generateIrradianceCubeMap()` 里的 cubemap filter pipeline
- `generatePrefilteredCubeMap()` 里的 cubemap filter pipeline
- `generateBRDFLUT()` 里的 fullscreen triangle pipeline

这些函数反复填写：

```cpp
VkPipelineInputAssemblyStateCreateInfo
VkPipelineRasterizationStateCreateInfo
VkPipelineColorBlendStateCreateInfo
VkPipelineDepthStencilStateCreateInfo
VkPipelineViewportStateCreateInfo
VkPipelineMultisampleStateCreateInfo
VkPipelineDynamicStateCreateInfo
VkPipelineRenderingCreateInfo
VkGraphicsPipelineCreateInfo
```

实际真正变化的东西并不多：

- shader stages。
- pipeline layout。
- vertex input。
- color/depth attachment format。
- topology。
- cull mode / front face。
- depth test / depth write。
- blend mode。
- 是否是 fullscreen triangle。

所以适合封装成 builder。

## 2. 这一轮封装边界

建议先只封装 graphics pipeline。

这一轮做：

- 新增 `base/vk_pipelines.h/.cpp`。
- 实现 `vkutil::PipelineBuilder`。
- 实现 pipeline layout helper。
- 支持 dynamic rendering。
- 支持 scene、skybox、light、cubemap filter、BRDF LUT 这几类 pipeline。

这一轮先不做：

- compute pipeline。
- shader reflection。
- descriptor layout 自动生成。
- render graph。
- pipeline library。
- pipeline cache 序列化。

小步走，很关键。Pipeline 这块如果一次改太猛，validation layer 和黑屏会结伴而来，像两个沉默但烦人的队友。

## 3. 推荐新增文件

新增：

```text
base/vk_pipelines.h
base/vk_pipelines.cpp
```

并在 `base/CMakeLists.txt` 中加入这两个文件。

命名风格建议保持当前项目一致：

```cpp
namespace vkutil {
    class PipelineBuilder;
}
```

## 4. PipelineBuilder 的设计目标

我建议你的 builder 具备这些特点：

- builder 持有 Vulkan create info 的状态。
- 通过链式函数设置状态。
- `build()` 内部拼装 `VkGraphicsPipelineCreateInfo` 并调用 `vkCreateGraphicsPipelines`。
- builder 自己保存 color formats、dynamic states、vertex input descriptions，避免悬垂指针。
- 支持 `clear()`，方便复用同一个 builder 创建多个 pipeline。

目标使用方式大概是：

```cpp
vkutil::PipelineBuilder builder;

builder
    .setPipelineLayout(pipelinesLayout.scenePipelineLayout)
    .setShaders(
        loadShader(getShadersPath() + pbrSceneVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
        loadShader(getShadersPath() + pbrSceneFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
    .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({
        vkglTF::VertexComponent::Position,
        vkglTF::VertexComponent::Normal
    }))
    .setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
    .setPolygonMode(VK_POLYGON_MODE_FILL)
    .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
    .setColorAttachmentFormat(swapChain.colorFormat)
    .setDepthFormat(depthFormat)
    .enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL)
    .disableBlending();

pipelines.scenePipeline = builder.build(device, pipelineCache);
```

这就是你想要的 vk-guide 风格：sample 里只描述这个 pipeline 的“意图”，不再手写整坨 create info。

## 5. Header 设计建议

建议 `base/vk_pipelines.h` 长这样：

```cpp
#pragma once

#include "vk_types.h"

namespace vkutil {

VkPipelineLayout createPipelineLayout(
    VkDevice device,
    const std::vector<VkDescriptorSetLayout>& setLayouts,
    const std::vector<VkPushConstantRange>& pushConstantRanges = {});

class PipelineBuilder {
public:
    PipelineBuilder();

    PipelineBuilder& clear();

    PipelineBuilder& setPipelineLayout(VkPipelineLayout layout);

    PipelineBuilder& setShaders(
        VkPipelineShaderStageCreateInfo vertexShader,
        VkPipelineShaderStageCreateInfo fragmentShader);

    PipelineBuilder& setVertexInput(const VkPipelineVertexInputStateCreateInfo& vertexInput);
    PipelineBuilder& setEmptyVertexInput();

    PipelineBuilder& setInputTopology(VkPrimitiveTopology topology);
    PipelineBuilder& setPolygonMode(VkPolygonMode mode);
    PipelineBuilder& setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);

    PipelineBuilder& setColorAttachmentFormat(VkFormat format);
    PipelineBuilder& setColorAttachmentFormats(const std::vector<VkFormat>& formats);
    PipelineBuilder& setDepthFormat(VkFormat format);

    PipelineBuilder& enableDepthTest(bool depthWriteEnable, VkCompareOp compareOp);
    PipelineBuilder& disableDepthTest();

    PipelineBuilder& disableBlending();
    PipelineBuilder& enableAlphaBlending();
    PipelineBuilder& enableAdditiveBlending();

    PipelineBuilder& setMultisamplingNone();
    PipelineBuilder& setDynamicStates(const std::vector<VkDynamicState>& states);

    VkPipeline build(VkDevice device, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

private:
    void refreshVertexInputPointers();

private:
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    VkPipelineMultisampleStateCreateInfo multisampling{};
    VkPipelineDepthStencilStateCreateInfo depthStencil{};

    std::vector<VkDynamicState> dynamicStates;
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{};

    std::vector<VkFormat> colorAttachmentFormats;
    VkPipelineRenderingCreateInfo renderingInfo{};

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
};

} // namespace vkutil
```

注意这里我没有让 builder 长期保存外部 `VkPipelineVertexInputStateCreateInfo*`。它会把 binding 和 attribute 拷贝进自己的 vector，这样更稳。

## 6. PipelineBuilder 默认状态

`clear()` 应该把 builder 重置到一个安全默认状态：

- triangle list。
- fill polygon。
- back-face culling。
- front face 使用 `VK_FRONT_FACE_COUNTER_CLOCKWISE`。
- multisampling none。
- blending disabled。
- depth disabled。
- dynamic viewport/scissor。
- no vertex input。
- no color/depth format。
- no pipeline layout。

默认 depth disabled 是一个好选择，因为 offscreen/filter/fullscreen pass 很多都不需要 depth。scene pipeline 显式调用 `enableDepthTest()`，意图更清楚。

## 7. CPP 实现骨架

`base/vk_pipelines.cpp` 可以按这个方向写：

```cpp
#include "vk_pipelines.h"
#include "VulkanTools.h"

namespace vkutil {

VkPipelineLayout createPipelineLayout(
    VkDevice device,
    const std::vector<VkDescriptorSetLayout>& setLayouts,
    const std::vector<VkPushConstantRange>& pushConstantRanges)
{
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    info.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();
    info.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
    info.pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &info, nullptr, &layout));
    return layout;
}

PipelineBuilder::PipelineBuilder()
{
    clear();
}

PipelineBuilder& PipelineBuilder::clear()
{
    shaderStages.clear();
    vertexBindings.clear();
    vertexAttributes.clear();
    colorAttachmentFormats.clear();

    vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;

    depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    dynamicStateInfo = {};
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;

    renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

    pipelineLayout = VK_NULL_HANDLE;
    depthFormat = VK_FORMAT_UNDEFINED;
    stencilFormat = VK_FORMAT_UNDEFINED;

    return *this;
}

} // namespace vkutil
```

后面再补各个 setter 和 `build()`。

## 8. Setter 实现建议

这些 setter 都很短：

```cpp
PipelineBuilder& PipelineBuilder::setPipelineLayout(VkPipelineLayout layout)
{
    pipelineLayout = layout;
    return *this;
}

PipelineBuilder& PipelineBuilder::setShaders(
    VkPipelineShaderStageCreateInfo vertexShader,
    VkPipelineShaderStageCreateInfo fragmentShader)
{
    shaderStages.clear();
    shaderStages.push_back(vertexShader);
    shaderStages.push_back(fragmentShader);
    return *this;
}

PipelineBuilder& PipelineBuilder::setInputTopology(VkPrimitiveTopology topology)
{
    inputAssembly.topology = topology;
    return *this;
}

PipelineBuilder& PipelineBuilder::setPolygonMode(VkPolygonMode mode)
{
    rasterizer.polygonMode = mode;
    return *this;
}

PipelineBuilder& PipelineBuilder::setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace)
{
    rasterizer.cullMode = cullMode;
    rasterizer.frontFace = frontFace;
    return *this;
}
```

vertex input 建议用“拷贝”方式：

```cpp
PipelineBuilder& PipelineBuilder::setVertexInput(const VkPipelineVertexInputStateCreateInfo& input)
{
    vertexBindings.assign(
        input.pVertexBindingDescriptions,
        input.pVertexBindingDescriptions + input.vertexBindingDescriptionCount);

    vertexAttributes.assign(
        input.pVertexAttributeDescriptions,
        input.pVertexAttributeDescriptions + input.vertexAttributeDescriptionCount);

    refreshVertexInputPointers();
    return *this;
}

PipelineBuilder& PipelineBuilder::setEmptyVertexInput()
{
    vertexBindings.clear();
    vertexAttributes.clear();
    refreshVertexInputPointers();
    return *this;
}

void PipelineBuilder::refreshVertexInputPointers()
{
    vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
    vertexInputInfo.pVertexBindingDescriptions = vertexBindings.empty() ? nullptr : vertexBindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.empty() ? nullptr : vertexAttributes.data();
}
```

这个设计比直接保存 `VkPipelineVertexInputStateCreateInfo*` 更安全。因为 `vkglTF::Vertex::getPipelineVertexInputState()` 返回的是静态数据，目前没问题，但你以后自己构建 vertex input 时可能传局部变量。builder 复制一份，后患少很多。

## 9. Color / Depth / Blend Setter

color format：

```cpp
PipelineBuilder& PipelineBuilder::setColorAttachmentFormat(VkFormat format)
{
    colorAttachmentFormats = { format };
    return *this;
}

PipelineBuilder& PipelineBuilder::setColorAttachmentFormats(const std::vector<VkFormat>& formats)
{
    colorAttachmentFormats = formats;
    return *this;
}

PipelineBuilder& PipelineBuilder::setDepthFormat(VkFormat format)
{
    depthFormat = format;
    return *this;
}
```

depth：

```cpp
PipelineBuilder& PipelineBuilder::enableDepthTest(bool depthWriteEnable, VkCompareOp compareOp)
{
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = compareOp;
    return *this;
}

PipelineBuilder& PipelineBuilder::disableDepthTest()
{
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    return *this;
}
```

blend：

```cpp
PipelineBuilder& PipelineBuilder::disableBlending()
{
    colorBlendAttachment.blendEnable = VK_FALSE;
    return *this;
}

PipelineBuilder& PipelineBuilder::enableAlphaBlending()
{
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    return *this;
}

PipelineBuilder& PipelineBuilder::enableAdditiveBlending()
{
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    return *this;
}
```

后续 bloom、粒子、透明物体会用到 blend，所以现在顺手留接口。

## 10. build() 实现

`build()` 是 builder 的核心：

```cpp
VkPipeline PipelineBuilder::build(VkDevice device, VkPipelineCache pipelineCache)
{
    if (pipelineLayout == VK_NULL_HANDLE) {
        vks::tools::exitFatal("PipelineBuilder: pipeline layout is null", -1);
    }

    if (shaderStages.empty()) {
        vks::tools::exitFatal("PipelineBuilder: shader stages are empty", -1);
    }

    refreshVertexInputPointers();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateInfo.pDynamicStates = dynamicStates.empty() ? nullptr : dynamicStates.data();

    VkPipelineColorBlendStateCreateInfo colorBlendState{};
    colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendState.logicOpEnable = VK_FALSE;
    colorBlendState.attachmentCount = 1;
    colorBlendState.pAttachments = &colorBlendAttachment;

    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentFormats.size());
    renderingInfo.pColorAttachmentFormats = colorAttachmentFormats.empty() ? nullptr : colorAttachmentFormats.data();
    renderingInfo.depthAttachmentFormat = depthFormat;
    renderingInfo.stencilAttachmentFormat = stencilFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlendState;
    pipelineInfo.pDynamicState = &dynamicStateInfo;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline));
    return pipeline;
}
```

注意：这里默认是 dynamic rendering 路线，所以 `renderPass = VK_NULL_HANDLE`，attachment format 通过 `VkPipelineRenderingCreateInfo` 传入。Lab2 现在使用 Vulkan 1.3 dynamic rendering，应该统一往这个方向走。

## 11. 生命周期重点

这是这轮最重要的坑点。

`VkGraphicsPipelineCreateInfo` 里面很多字段都是裸指针。比如：

```cpp
pStages
pVertexInputState
pRasterizationState
pColorBlendState
pDynamicState
pNext
```

安全原则：

- builder 成员可以被 `build()` 里的 create info 指向。
- `build()` 局部变量可以被 create info 指向，因为它们在 `vkCreateGraphicsPipelines()` 返回前都活着。
- 不要让 create info 指向一个已经销毁的临时对象。
- 不要长期保存 caller 传入的局部 vector 的 `data()`。

所以本文设计里：

- shader stages 存在 builder 的 vector 里。
- vertex binding/attribute 被复制到 builder 的 vector 里。
- color formats 存在 builder 的 vector 里。
- dynamic states 存在 builder 的 vector 里。
- `VkPipelineViewportStateCreateInfo` 和 `VkPipelineColorBlendStateCreateInfo` 在 `build()` 内部临时创建并立即使用。

这套设计很稳，不会因为一个局部变量生命周期被 Vulkan 暗杀。

## 12. Scene Pipeline 如何改

原来的 `createScenePipeline()` 可以缩成：

```cpp
void VulkanExample::createScenePipeline() {
    vkutil::PipelineBuilder builder;

    builder
        .setPipelineLayout(pipelinesLayout.scenePipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + pbrSceneVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + pbrSceneFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({
            vkglTF::VertexComponent::Position,
            vkglTF::VertexComponent::Normal
        }))
        .setColorAttachmentFormat(swapChain.colorFormat)
        .setDepthFormat(depthFormat)
        .enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();

    pipelines.scenePipeline = builder.build(device, pipelineCache);
}
```

这段代码看起来就像在描述 pass 本身：我要哪些 shader、哪些顶点属性、要不要 depth、要不要 blend。

## 13. Skybox Pipeline 如何改

skybox 的建议配置：

```cpp
void VulkanExample::createSkyboxPipeline() {
    vkutil::PipelineBuilder builder;

    builder
        .setPipelineLayout(pipelinesLayout.skyboxPipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + skyboxVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + skyboxFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({
            vkglTF::VertexComponent::Position
        }))
        .setColorAttachmentFormat(swapChain.colorFormat)
        .setDepthFormat(depthFormat)
        .enableDepthTest(false, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();

    pipelines.skyboxPipeline = builder.build(device, pipelineCache);
}
```

这里 `enableDepthTest(false, ...)` 表示开 depth test，但不写 depth。skybox 一般作为背景，不应该污染 depth buffer。

如果你选择最后绘制 skybox，也可以根据实际表现改成关闭 depth test；但当前 Lab2 有 scene/light/skybox 顺序，保守做法是 depth test on、depth write off。

## 14. Light Pipeline 如何改

光源小球 pipeline：

```cpp
void VulkanExample::createLightPipeline() {
    vkutil::PipelineBuilder builder;

    builder
        .setPipelineLayout(pipelinesLayout.lightPipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + lightVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + lightFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({
            vkglTF::VertexComponent::Position,
            vkglTF::VertexComponent::Normal
        }))
        .setColorAttachmentFormat(swapChain.colorFormat)
        .setDepthFormat(depthFormat)
        .enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();

    pipelines.lightPipeline = builder.build(device, pipelineCache);
}
```

后续你可以继续抽一个 `createDebugMeshPipeline()`，用于 light、debug sphere、bounding box 等。

## 15. Cubemap Filter Pipeline 如何改

Irradiance 和 prefilter 的 pipeline 几乎一样，适合抽函数：

```cpp
VkPipeline VulkanExample::createCubeFilterPipeline(
    VkPipelineLayout layout,
    const std::string& fragmentShader,
    VkFormat colorFormat)
{
    vkutil::PipelineBuilder builder;

    builder
        .setPipelineLayout(layout)
        .setShaders(
            loadShader(getShadersPath() + filterCubeVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({
            vkglTF::VertexComponent::Position,
            vkglTF::VertexComponent::Normal,
            vkglTF::VertexComponent::UV
        }))
        .setColorAttachmentFormat(colorFormat)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();

    return builder.build(device, pipelineCache);
}
```

然后：

```cpp
VkPipeline pipeline = createCubeFilterPipeline(
    pipelinelayout,
    irradianceFragmentShader,
    format);
```

prefilter 只换 fragment shader：

```cpp
VkPipeline pipeline = createCubeFilterPipeline(
    pipelinelayout,
    prefilterFragmentShader,
    format);
```

这样 `generateIrradianceCubeMap()` 和 `generatePrefilteredCubeMap()` 会少掉一大块重复管线代码，真正留下的是 cubemap 的 face/mip 渲染和 copy 数据流。

## 16. BRDF LUT Pipeline 如何改

BRDF LUT 是 fullscreen triangle，不需要 vertex buffer：

```cpp
VkPipeline VulkanExample::createFullscreenPipeline(
    VkPipelineLayout layout,
    const std::string& vertexShader,
    const std::string& fragmentShader,
    VkFormat colorFormat)
{
    vkutil::PipelineBuilder builder;

    builder
        .setPipelineLayout(layout)
        .setShaders(
            loadShader(getShadersPath() + vertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setEmptyVertexInput()
        .setColorAttachmentFormat(colorFormat)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();

    return builder.build(device, pipelineCache);
}
```

然后 BRDF LUT 生成处就可以：

```cpp
VkPipeline pipeline = createFullscreenPipeline(
    pipelinelayout,
    brdfLUTVertexShader,
    brdfLUTFragmentShader,
    format);
```

这类 helper 后续做 tone mapping、bloom、FXAA、debug texture view 都可以复用。

## 17. Pipeline Layout Helper

Pipeline layout 也建议放进 `vk_pipelines`：

```cpp
VkPipelineLayout createPipelineLayout(
    VkDevice device,
    const std::vector<VkDescriptorSetLayout>& setLayouts,
    const std::vector<VkPushConstantRange>& pushConstantRanges = {});
```

scene layout 可以变成：

```cpp
void VulkanExample::createScenePipelineLayout() {
    std::vector<VkDescriptorSetLayout> setLayouts = {
        descriptorSetLayouts.sceneDescriptorSetLayout
    };

    std::vector<VkPushConstantRange> pushRanges = {
        vks::initializers::pushConstantRange(
            VK_SHADER_STAGE_VERTEX_BIT,
            sizeof(glm::vec3),
            0),
        vks::initializers::pushConstantRange(
            VK_SHADER_STAGE_FRAGMENT_BIT,
            sizeof(Material::PushBlock),
            sizeof(glm::vec3))
    };

    pipelinesLayout.scenePipelineLayout =
        vkutil::createPipelineLayout(device, setLayouts, pushRanges);
}
```

这个 helper 很薄，但能让 layout 创建也统一起来。

## 18. 推荐重构顺序

别一口气全改。建议顺序：

1. 新增 `vk_pipelines.h/.cpp`。
2. 加入 `base/CMakeLists.txt`。
3. 实现 `createPipelineLayout()`。
4. 只替换 `createScenePipelineLayout()`。
5. 编译运行。
6. 实现 `PipelineBuilder::clear()` 和基础 setter。
7. 实现 `PipelineBuilder::build()`。
8. 只替换 `createScenePipeline()`。
9. 编译运行。
10. 替换 `createLightPipeline()`。
11. 编译运行。
12. 替换 `createSkyboxPipeline()`。
13. 编译运行。
14. 抽 `createCubeFilterPipeline()`，替换 irradiance。
15. 编译运行。
16. 替换 prefilter。
17. 编译运行。
18. 抽 `createFullscreenPipeline()`，替换 BRDF LUT。
19. 最后清理旧的重复代码。

每一步都运行一次。这里不追求“一招重构成功”，我们追求“每一步都知道自己改了什么”。这就是图形工程里最省命的节奏。

## 19. CMake 注意事项

你新增文件后，需要在 `base/CMakeLists.txt` 里加入：

```cmake
vk_pipelines.h
vk_pipelines.cpp
```

如果 base 是用变量收集源文件，就加到对应变量里。不要只创建文件不加 CMake，否则 VS 工程里不会编译 `.cpp`。

## 20. 和 vk-guide 的区别

vk-guide 的 PipelineBuilder 通常很轻量，直接暴露成员：

```cpp
PipelineBuilder pipelineBuilder;
pipelineBuilder._shaderStages = ...
pipelineBuilder._inputAssembly = ...
pipelineBuilder.build_pipeline(device);
```

你也可以这么做。但在你现在的项目里，我更建议“链式 setter + builder 自己保存 vector”的版本，因为：

- 你已经有比较多 Lab 和 helper，接口清晰一点更适合长期复用。
- dynamic rendering 需要保存 color format vector，生命周期要更谨慎。
- glTF vertex input 来自 helper，复制一份更稳。
- 后续 fullscreen、cubemap、scene pipeline 都能复用同一个 builder。

也就是说：思想借鉴 vk-guide，但工程上稍微更安全一点。

## 21. 常见坑

### 忘记设置 color attachment format

dynamic rendering pipeline 必须知道 color attachment format。否则 pipeline 创建或 draw 时会出问题。

### depth 开了但没设置 depth format

如果调用了 `enableDepthTest()`，就要记得 `setDepthFormat(depthFormat)`。

### skybox 写入 depth

skybox 通常不应该写 depth。否则后续 debug draw 或透明物体可能被挡。

### BRDF LUT 还使用 glTF vertex input

fullscreen triangle 不需要 vertex input。用 `setEmptyVertexInput()`。

### pipeline layout 和 shader 不匹配

shader 里用的 descriptor set、push constant range 必须出现在 pipeline layout 中。pipeline 创建可能成功，但 draw 会报 validation error 或读错数据。

### builder 复用前忘记 clear

如果同一个 builder 创建多个 pipeline，要么每次新建 builder，要么调用 `clear()`。否则上一个 pipeline 的 shader、format、depth 状态可能残留。

## 22. 这一轮完成后的理想状态

最终 Lab2 应该变成：

```text
lab2.cpp
  createScenePipelineLayout()
  createScenePipeline()
  createSkyboxPipelineLayout()
  createSkyboxPipeline()
  createLightPipelineLayout()
  createLightPipeline()
  createCubeFilterPipeline()
  createFullscreenPipeline()

base/vk_pipelines.h/.cpp
  createPipelineLayout()
  PipelineBuilder

base/vk_rendering.h/.cpp
  dynamic rendering begin/end helper

base/vk_descriptors.h/.cpp
  descriptor helper
```

你的 sample 层会更像这样：

```cpp
createImages();
createDescriptors();
createPipelineLayouts();
createPipelines();
recordCommandBuffer();
```

这就是一个“小型渲染器骨架”开始稳定成型的感觉。后续你写新的实验时，主要精力就能放在算法和数据流上，而不是每次和 `VkGraphicsPipelineCreateInfo` 重新掰手腕。

