# Step 6 补充文档：从第 9 步开始重新梳理

这份文档是对 `step6.md` 第 9 步之后的补充版。

你现在已经写了一部分 Step 6 代码：

```text
offscreenColor
blitDescriptorSetLayout
blitDescriptorSet
blitPipelineLayout
blitPipeline
fullscreen.vert / fullscreen.frag
createBlitDescriptors()
createBlitPipeline()
drawScene()
windowResized()
```

这一版文档从这里继续，不再重复前面 offscreen resource 的概念，而是把后半段整理成一个更明确的执行顺序。

最终目标仍然是：

```text
Pass 1: scene pipeline -> offscreenColor
Pass 2: blit pipeline  -> swapchain
```

## 9. 先修 descriptor pool

你现在已经有 scene descriptor：

```text
每个 frame 一个 descriptor set
binding 0 -> UBO
binding 1 -> baseColorTexture
```

Step 6 又新增一个 blit descriptor：

```text
binding 0 -> offscreenColor
```

所以 descriptor pool 要能容纳：

```text
MAX_CONCURRENT_FRAMES 个 UBO descriptor
MAX_CONCURRENT_FRAMES 个 baseColor texture descriptor
1 个 offscreen texture descriptor
```

`createDescriptors()` 里建议这样写：

```cpp
VkDescriptorPoolSize descriptorTypeCounts[2]{};
descriptorTypeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
descriptorTypeCounts[0].descriptorCount = MAX_CONCURRENT_FRAMES;

descriptorTypeCounts[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
descriptorTypeCounts[1].descriptorCount = MAX_CONCURRENT_FRAMES + 1;

VkDescriptorPoolCreateInfo descriptorPoolCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
descriptorPoolCI.poolSizeCount = 2;
descriptorPoolCI.pPoolSizes = descriptorTypeCounts;
descriptorPoolCI.maxSets = MAX_CONCURRENT_FRAMES + 1;
VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));
```

注意不要写成：

```cpp
descriptorPoolCI.pPoolSizes = descriptorTypeCounts + 1;
```

这个会跳过 `descriptorTypeCounts[0]`，导致 pool 没有 UBO descriptor 配额。

正确写法是：

```cpp
descriptorPoolCI.pPoolSizes = descriptorTypeCounts;
```

## 10. 完成 blit pipeline

`createBlitPipeline()` 的目标是创建一个 fullscreen triangle pipeline。

它和 scene pipeline 的关键区别：

```text
不需要 vertex buffer
不需要 index buffer
不需要 depth test
不需要 push constant
descriptor set layout 只绑定 offscreen texture
vertex shader 使用 SV_VertexID 生成三角形
fragment shader 采样 offscreen texture
```

### 10.1 必须设置空 vertex input

即使 fullscreen triangle 不用 vertex buffer，也建议显式提供空的 vertex input state：

```cpp
VkPipelineVertexInputStateCreateInfo vertexInputStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
};
vertexInputStateCI.vertexBindingDescriptionCount = 0;
vertexInputStateCI.vertexAttributeDescriptionCount = 0;
```

然后挂到 pipeline：

```cpp
graphicsPipelineCI.pVertexInputState = &vertexInputStateCI;
```

这是当前最容易漏的一步。

### 10.2 blit pipeline 不需要 depth format

因为第二个 pass 只是画 fullscreen triangle 到 swapchain，不使用 depth。

所以 dynamic rendering 的格式信息应该是：

```cpp
VkPipelineRenderingCreateInfoKHR pipelineRenderingCI{
    VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR
};
pipelineRenderingCI.colorAttachmentCount = 1;
pipelineRenderingCI.pColorAttachmentFormats = &swapChain.colorFormat;
pipelineRenderingCI.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
pipelineRenderingCI.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
```

不要沿用 scene pipeline 的：

```cpp
pipelineRenderingCI.depthAttachmentFormat = depthFormat;
pipelineRenderingCI.stencilAttachmentFormat = depthFormat;
```

虽然 depth test 关了，但 blit pass 本身确实没有 depth attachment。

### 10.3 blit pipeline 核心骨架

`createBlitPipeline()` 可以按这个结构检查：

```cpp
void VulkanExample::createBlitPipeline() {
    VkPipelineLayoutCreateInfo layoutCI = vkinit::pipelineLayoutCreateInfo();
    layoutCI.setLayoutCount = 1;
    layoutCI.pSetLayouts = &blitDescriptorSetLayout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &layoutCI, nullptr, &blitPipelineLayout));

    VkGraphicsPipelineCreateInfo pipelineCI{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.layout = blitPipelineLayout;

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineVertexInputStateCreateInfo vertexInputStateCI{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    VkPipelineRasterizationStateCreateInfo rasterizationStateCI{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCI.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.colorWriteMask = 0xf;
    blendAttachmentState.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlendStateCI{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    colorBlendStateCI.attachmentCount = 1;
    colorBlendStateCI.pAttachments = &blendAttachmentState;

    VkPipelineViewportStateCreateInfo viewportStateCI{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };
    viewportStateCI.viewportCount = 1;
    viewportStateCI.scissorCount = 1;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateCI{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
    };
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateCI.pDynamicStates = dynamicStates.data();

    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    depthStencilStateCI.depthTestEnable = VK_FALSE;
    depthStencilStateCI.depthWriteEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampleStateCI{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
    };
    multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = loadSPIRVShader(getShadersPath() + blitVertShaderPath);
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = loadSPIRVShader(getShadersPath() + blitFragShaderPath);
    shaderStages[1].pName = "main";

    VkPipelineRenderingCreateInfoKHR renderingCI{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR
    };
    renderingCI.colorAttachmentCount = 1;
    renderingCI.pColorAttachmentFormats = &swapChain.colorFormat;
    renderingCI.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    renderingCI.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.pVertexInputState = &vertexInputStateCI;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.pNext = &renderingCI;

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &blitPipeline));

    vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
    vkDestroyShaderModule(device, shaderStages[1].module, nullptr);
}
```

你可以先按这个版本跑通。

后面再抽 `PipelineBuilder`，不要现在就抽。

## 11. 抽出 drawScene

现在 `drawScene()` 为空。

建议把原来 `render()` 里画两个圆盘的部分挪进去。

为了让函数拿到当前帧的基础 model matrix，建议改签名：

```cpp
void drawScene(VkCommandBuffer commandBuffer, const glm::mat4& baseModelMatrix);
```

`lab0.h` 也要同步改：

```cpp
void drawScene(VkCommandBuffer commandBuffer, const glm::mat4& baseModelMatrix);
```

实现可以是：

```cpp
void VulkanExample::drawScene(
    VkCommandBuffer commandBuffer,
    const glm::mat4& baseModelMatrix)
{
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0,
        1,
        &uniformBuffersV2[currentFrame].descriptorSet,
        0,
        nullptr);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkDeviceSize offsets[1]{ 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &circleMeshBuffers.vertexBuffer.handle, offsets);
    vkCmdBindIndexBuffer(commandBuffer, circleMeshBuffers.indexBuffer.handle, 0, circleMeshBuffers.indexType);

    PushConstantData pushConstantData{};
    pushConstantData.modelMatrix = baseModelMatrix;
    pushConstantData.colorMultiplier = glm::vec4(1.0f, 1.0f, 0.5f, 1.0f);
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(PushConstantData),
        &pushConstantData);
    vkCmdDrawIndexed(commandBuffer, circleMeshBuffers.indexCount, 1, 0, 0, 0);

    pushConstantData.modelMatrix =
        glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 1.5f, 2.0f)) *
        baseModelMatrix;
    pushConstantData.colorMultiplier = glm::vec4(0.5f, 2.0f, 1.0f, 1.0f);
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(PushConstantData),
        &pushConstantData);
    vkCmdDrawIndexed(commandBuffer, circleMeshBuffers.indexCount, 1, 0, 0, 0);
}
```

这样 `render()` 里只负责 pass 组织，`drawScene()` 只负责画 scene。

这是 Step 6 最重要的结构变化。

## 12. prepare 顺序要补全

你现在的 `prepare()` 还没有调用 Step 6 新函数。

建议改成：

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

顺序原因：

```text
createOffscreenResources()
-> 必须在 updateBlitDescriptor() 前，因为 descriptor 要指向 offscreenColor.imageView

createDescriptors()
-> 创建 descriptorPool，所以必须在 createBlitDescriptors() 前

createBlitDescriptors()
-> 创建 blitDescriptorSetLayout 和 blitDescriptorSet

createBlitPipeline()
-> 需要 blitDescriptorSetLayout
```

## 13. render 重写成两个 pass

这一节是补充文档最关键的部分。

你现在的 `render()` 还是：

```text
transition swapchain -> attachment
transition depth -> attachment
begin rendering
draw scene
end rendering
transition swapchain -> present
```

Step 6 要改成：

```text
Pass 1:
    transition offscreenColor -> attachment
    transition depth -> attachment
    begin rendering(offscreenColor + depth)
    drawScene()
    end rendering
    transition offscreenColor -> shader read

Pass 2:
    transition swapchain -> attachment
    begin rendering(swapchain only)
    draw fullscreen triangle sampling offscreenColor
    end rendering
    transition swapchain -> present
```

### 13.1 Pass 1：画到 offscreen

在 command buffer 录制开始后，先写：

```cpp
vkutil::cmdTransitionImageLayout(
    commandBuffer,
    offscreenColor.image.image,
    offscreenColor.image.layout,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_ASPECT_COLOR_BIT);
offscreenColor.image.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
```

这里要求 `cmdTransitionImageLayout()` 支持：

```text
UNDEFINED -> ATTACHMENT_OPTIMAL
SHADER_READ_ONLY_OPTIMAL -> ATTACHMENT_OPTIMAL
```

因为第一帧 old layout 是 `UNDEFINED`。

第二帧开始 old layout 是上一帧结束后的 `SHADER_READ_ONLY_OPTIMAL`。

depth 仍然可以暂时用你现有的 barrier：

```cpp
const VkImageAspectFlags depthAspectMask = getDepthAspectMask(depthFormat);
vks::tools::insertImageMemoryBarrier(
    commandBuffer,
    depthImage.image,
    0,
    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
    VkImageSubresourceRange{ depthAspectMask, 0, 1, 0, 1 });
```

注意这里用 `depthAspectMask`，不要硬编码：

```cpp
VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
```

然后构造 offscreen color attachment：

```cpp
VkRenderingAttachmentInfo sceneColorAttachment{
    VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO
};
sceneColorAttachment.imageView = offscreenColor.image.imageView;
sceneColorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
sceneColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
sceneColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
sceneColorAttachment.clearValue.color = { 0.11f, 0.10f, 0.13f, 1.0f };
```

depth attachment：

```cpp
VkRenderingAttachmentInfo depthAttachment{
    VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO
};
depthAttachment.imageView = depthImage.imageView;
depthAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
depthAttachment.clearValue.depthStencil = { 1.0f, 0 };
```

rendering info：

```cpp
VkRenderingInfo sceneRenderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
sceneRenderingInfo.renderArea = { 0, 0, width, height };
sceneRenderingInfo.layerCount = 1;
sceneRenderingInfo.colorAttachmentCount = 1;
sceneRenderingInfo.pColorAttachments = &sceneColorAttachment;
sceneRenderingInfo.pDepthAttachment = &depthAttachment;
sceneRenderingInfo.pStencilAttachment = &depthAttachment;
```

begin pass：

```cpp
vkCmdBeginRendering(commandBuffer, &sceneRenderingInfo);

VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

VkRect2D scissor{ { 0, 0 }, { width, height } };
vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

drawScene(commandBuffer, shaderData.modelMatrix);

vkCmdEndRendering(commandBuffer);
```

Pass 1 结束后，把 offscreen 转成 shader read：

```cpp
vkutil::cmdTransitionImageLayout(
    commandBuffer,
    offscreenColor.image.image,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    VK_IMAGE_ASPECT_COLOR_BIT);
offscreenColor.image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
```

### 13.2 Pass 2：采样 offscreen，画到 swapchain

先 transition swapchain：

```cpp
vkutil::cmdTransitionImageLayout(
    commandBuffer,
    swapChain.images[imageIndex],
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_ASPECT_COLOR_BIT);
```

这里要求 `cmdTransitionImageLayout()` 支持：

```text
UNDEFINED -> ATTACHMENT_OPTIMAL
```

构造 swapchain color attachment：

```cpp
VkRenderingAttachmentInfo presentColorAttachment{
    VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO
};
presentColorAttachment.imageView = swapChain.imageViews[imageIndex];
presentColorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
presentColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
presentColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
presentColorAttachment.clearValue.color = { 0.02f, 0.02f, 0.025f, 1.0f };
```

present pass 不需要 depth：

```cpp
VkRenderingInfo presentRenderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
presentRenderingInfo.renderArea = { 0, 0, width, height };
presentRenderingInfo.layerCount = 1;
presentRenderingInfo.colorAttachmentCount = 1;
presentRenderingInfo.pColorAttachments = &presentColorAttachment;
presentRenderingInfo.pDepthAttachment = nullptr;
presentRenderingInfo.pStencilAttachment = nullptr;
```

begin pass：

```cpp
vkCmdBeginRendering(commandBuffer, &presentRenderingInfo);

VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

VkRect2D scissor{ { 0, 0 }, { width, height } };
vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

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

vkCmdEndRendering(commandBuffer);
```

最后 transition 到 present：

```cpp
vkutil::cmdTransitionImageLayout(
    commandBuffer,
    swapChain.images[imageIndex],
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    VK_IMAGE_ASPECT_COLOR_BIT);
```

这里要求 `cmdTransitionImageLayout()` 支持：

```text
ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
```

## 14. 必须扩展 cmdTransitionImageLayout

为了支持 Step 6，`base/vk_images.cpp` 里的 `cmdTransitionImageLayout()` 至少要支持这些 transition：

```text
UNDEFINED -> TRANSFER_DST_OPTIMAL
TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
UNDEFINED -> ATTACHMENT_OPTIMAL
SHADER_READ_ONLY_OPTIMAL -> ATTACHMENT_OPTIMAL
ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
```

可以在已有两个分支后继续加：

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

这不是最终版同步系统，但足够支撑 Step 6。

## 15. windowResized 要真的更新 descriptor

你现在 `windowResized()` 还是空函数。

建议实现：

```cpp
void VulkanExample::windowResized() {
    destroyOffscreenResources();
    createOffscreenResources();
    updateBlitDescriptor();
}
```

原因：

```text
resize 后 offscreenColor.imageView 会变
blitDescriptorSet 里还指向旧 imageView
所以必须 updateBlitDescriptor()
```

如果 resize 后黑屏或 validation 报 descriptor image view 已销毁，大概率就是这里漏了。

## 16. 析构要补 blit 和 offscreen

析构里建议加入：

```cpp
vkDestroyPipeline(device, blitPipeline, nullptr);
vkDestroyPipelineLayout(device, blitPipelineLayout, nullptr);
vkDestroyDescriptorSetLayout(device, blitDescriptorSetLayout, nullptr);
vkutil::destroyTexture(device, allocator, offscreenColor);
```

顺序建议：

```text
pipeline
pipeline layout
descriptor set layout
offscreen texture
base color texture
depth image
mesh/uniform buffers
VMA allocator
```

descriptor set 不需要手动 free。

`descriptorPool` 是基类成员，会在基类析构里销毁。

## 17. 编译和检查

先编译 shader：

```powershell
Push-Location shaders/hlsl
python compileshaders.py --sample lab0
Pop-Location
```

再编译 C++：

```powershell
cmake --build build --config Debug --target lab0 -j 32
```

运行：

```powershell
build\bin\Debug\lab0.exe -v -vl
```

建议先做这些搜索检查：

```powershell
rg -n "pPoolSizes = descriptorTypeCounts \\+ 1|pVertexInputState|depthAttachmentFormat|createOffscreenResources|createBlitDescriptors|createBlitPipeline|drawScene\\(" examples/Lab0/lab0.cpp
```

你希望看到：

```text
没有 pPoolSizes = descriptorTypeCounts + 1
blit pipeline 设置了 pVertexInputState
blit dynamic rendering depth/stencil format 是 VK_FORMAT_UNDEFINED
prepare 调用了 createOffscreenResources / createBlitDescriptors / createBlitPipeline
render 调用了 drawScene(...)
```

## 18. 如果画面黑，按这个顺序查

先查 pass 1：

```text
offscreenColor 是否创建成功
Pass 1 的 colorAttachment 是否是 offscreenColor.image.imageView
drawScene 是否真的被调用
Pass 1 结束后是否 transition 到 SHADER_READ_ONLY_OPTIMAL
```

再查 pass 2：

```text
fullscreen.vert/.frag 是否编译成 spv
createBlitPipeline 是否成功
blitDescriptorSet 是否写入 offscreenColor.descriptor
Pass 2 是否绑定 blitPipeline 和 blitDescriptorSet
是否调用 vkCmdDraw(commandBuffer, 3, 1, 0, 0)
```

再查 descriptor pool：

```text
combined image sampler descriptorCount 是否是 MAX_CONCURRENT_FRAMES + 1
maxSets 是否是 MAX_CONCURRENT_FRAMES + 1
pPoolSizes 是否传 descriptorTypeCounts
```

## 19. 跑通后的最小验证

先让 `fullscreen.frag` 原样输出：

```hlsl
float3 color = offscreenTexture.Sample(offscreenSampler, input.UV).rgb;
return float4(color, 1.0);
```

跑通后再改成灰度：

```hlsl
float3 color = offscreenTexture.Sample(offscreenSampler, input.UV).rgb;
float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
return float4(luminance.xxx, 1.0);
```

如果灰度能显示，说明：

```text
scene pass -> offscreen texture -> fullscreen post-process pass -> swapchain
```

这条图形算法主干已经通了。

