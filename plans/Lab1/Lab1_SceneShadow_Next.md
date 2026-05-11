# Lab1 Scene Shadow：从 Shadow Map Debug 进入真正场景阴影

你现在已经完成了 Shadow Mapping 最容易卡的部分：

```text
light view -> shadow depth image
shadow depth image -> fullscreen debug view
```

这说明 Vulkan 数据流已经通了：

```text
depth attachment 写入
layout transition
descriptor 采样
fullscreen debug pass
```

下一阶段目标是把第二个 pass 从 `drawQuad()` 改成真正的 `drawScene()`：

```text
Pass 1: light view 写 shadow map
Pass 2: camera view 渲染场景，并采样 shadow map 判断阴影
```

这一阶段请不要急着做 PCF，也不要急着加载 glTF 材质贴图。先做 hard shadow。

## 1. 本阶段完成标准

完成后你应该看到：

```text
正常 camera 视角的 3D 场景
场景表面有硬阴影
阴影会随着 lightPos 变化
```

最低完成标准不是“阴影很好看”，而是：

```text
scene pipeline 能绘制模型
scene shader 能拿到 shadowCoord
fragment shader 能采样 shadow map
能通过 bias 调整 shadow acne
```

## 2. 当前不要改的东西

这些现在已经工作了，先不要动：

```text
createShadowResources()
drawShadowMap()
shadowVertex.vert
shadowMap descriptor
shadow map layout transition
debug pipeline
quad.vert / quad.frag
```

也就是说，下一步不是重写 shadow pass，而是在它后面加一个新的 scene pass。

## 3. 新增 HLSL：先复制 shadowmapping 的 scene shader

建议新建：

```text
shaders/hlsl/lab1/scene.vert
shaders/hlsl/lab1/scene.frag
```

第一版可以直接从原工程复制：

```text
shaders/hlsl/shadowmapping/scene.vert
shaders/hlsl/shadowmapping/scene.frag
```

然后再根据 Lab1 慢慢改。

原因是原 shader 的输入顺序正好和你当前 glTF vertex input 一致：

```cpp
vkglTF::Vertex::getPipelineVertexInputState({
    vkglTF::VertexComponent::Position,
    vkglTF::VertexComponent::UV,
    vkglTF::VertexComponent::Color,
    vkglTF::VertexComponent::Normal
});
```

对应 HLSL：

```hlsl
[[vk::location(0)]] float3 Pos    : POSITION0;
[[vk::location(1)]] float2 UV     : TEXCOORD0;
[[vk::location(2)]] float3 Color  : COLOR0;
[[vk::location(3)]] float3 Normal : NORMAL0;
```

这部分先不要创新，先让 location 对齐。

## 4. 修改 shader path

在 `lab1.h` 里补：

```cpp
const std::string sceneVertexShaderPath = "lab1/scene.vert.spv";
const std::string sceneFragmentShaderPath = "lab1/scene.frag.spv";
```

保留：

```cpp
const std::string shadowVertexShaderPath = "lab1/shadowVertex.vert.spv";
const std::string quadVertexShaderPath = "lab1/quad.vert.spv";
const std::string quadFragmentShaderPath = "lab1/quad.frag.spv";
```

注意：shader 路径不要以 `/` 开头。

## 5. 更新 Scene UBO

你现在只更新了 shadow UBO：

```cpp
uniformDataShadow.depthMVP = depthProjectionMatrix * depthViewMatrix * depthModelMatrix;
```

接下来要补 scene UBO：

```cpp
uniformDataScene.projection = camera.matrices.perspective;
uniformDataScene.view = camera.matrices.view;
uniformDataScene.model = glm::mat4(1.0f);
uniformDataScene.depthBiasMVP = uniformDataShadow.depthMVP;
uniformDataScene.lightPos = glm::vec4(lightPos, 1.0f);
uniformDataScene.zNear = shadowNearPlane;
uniformDataScene.zFar = shadowFarPlane;

memcpy(
    uniformBuffers[currentBuffer].sceneBuffer.allocationInfo.pMappedData,
    &uniformDataScene,
    sizeof(uniformDataScene));

vmaFlushAllocation(
    allocator,
    uniformBuffers[currentBuffer].sceneBuffer.allocation,
    0,
    sizeof(uniformDataScene));
```

这里最关键的是：

```cpp
uniformDataScene.depthBiasMVP = uniformDataShadow.depthMVP;
```

`scene.vert` 会用它把 camera 视角下的每个顶点再次投影到 light/shadow map 空间。

## 6. 创建 sceneShadow pipeline

你的 `createPipelines()` 当前已经有：

```text
debug pipeline
shadow pipeline
```

现在要在它们之间补：

```text
scene pipeline
```

scene pipeline 的关键状态：

```text
shader:
    scene.vert.spv
    scene.frag.spv

rendering:
    colorAttachmentCount = 1
    color format = swapChain.colorFormat
    depthAttachmentFormat = depthFormat

vertex input:
    Position, UV, Color, Normal

depth:
    depth test on
    depth write on

cull:
    first version can use VK_CULL_MODE_NONE
```

注意：你现在 debug pipeline 的 `renderingCreateInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED`。

scene pipeline 如果要使用 swapchain scene depth，则要把它切成：

```cpp
renderingCreateInfo.depthAttachmentFormat = depthFormat;
renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
```

如果当前 `depthFormat` 是 depth-stencil 格式，并且你在 `VkRenderingInfo` 同时传了 stencil attachment，再改 stencil。第一版可以只传 depth attachment，先不碰 stencil。

创建 scene pipeline 后记得：

```cpp
VK_CHECK_RESULT(vkCreateGraphicsPipelines(
    device,
    pipelineCache,
    1,
    &pipelineCI,
    nullptr,
    &pipelines.sceneShadow));
```

## 7. 新增 drawScene()

建议在 `lab1.h` 声明：

```cpp
void drawScene(VkCommandBuffer commandBuffer);
```

实现结构：

```cpp
void VulkanExample::drawScene(VkCommandBuffer commandBuffer)
{
    VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = swapChain.imageViews[currentImageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { 0.02f, 0.02f, 0.025f, 1.0f };

    VkRenderingAttachmentInfo depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAttachment.imageView = depthStencil.view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderingInfo.renderArea = { 0, 0, width, height };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    renderingInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    {
        VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{ { 0, 0 }, { width, height } };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.sceneShadow);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0,
            1,
            &descriptorSets[currentBuffer].scene,
            0,
            nullptr);

        scenes[sceneIndex].draw(commandBuffer);
    }
    vkCmdEndRendering(commandBuffer);
}
```

这一步先不要绑定 glTF 材质贴图。

## 8. buildCommandBuffer 的新结构

从：

```cpp
drawShadowMap(commandBuffer);
drawQuad(commandBuffer);
```

改成：

```cpp
drawShadowMap(commandBuffer);
drawScene(commandBuffer);
```

完整数据流：

```text
1. shadowMap: read-only/undefined -> depth attachment
2. drawShadowMap()
3. shadowMap: depth attachment -> depth read-only
4. swapchain: undefined -> color attachment
5. depthStencil: undefined -> depth attachment
6. drawScene()
7. swapchain: color attachment -> present
```

你要额外注意 scene pass 的 depth image。

基类创建了：

```cpp
depthStencil.image
depthStencil.view
```

但 dynamic rendering 下你要自己 transition：

```cpp
vks::tools::insertImageMemoryBarrier(
    commandBuffer,
    depthStencil.image,
    0,
    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
    VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 });
```

如果 `depthFormat` 含 stencil，aspect mask 需要加 `VK_IMAGE_ASPECT_STENCIL_BIT`。

## 9. 先做“无阴影 scene pass”调试

在正式比较 shadow map 之前，我建议你先让 scene pass 只显示普通颜色。

也就是临时把 `scene.frag` 改成：

```hlsl
float3 N = normalize(input.Normal);
float3 L = normalize(input.LightVec);
float diffuseTerm = max(dot(N, L), 0.1);
return float4(input.Color * diffuseTerm, 1.0);
```

这一步目标：

```text
确认 scene pipeline 正常
确认 camera 矩阵正常
确认 depthStencil 正常
确认 glTF vertex input 正常
```

如果 scene 都画不出来，不要急着查 shadow compare。

## 10. 再打开 hard shadow

scene 正常后，再启用 shadow：

```hlsl
float4 sc = input.ShadowCoord / input.ShadowCoord.w;
float closestDepth = shadowMapTexture.Sample(shadowMapSampler, sc.xy).r;
float currentDepth = sc.z;
float bias = 0.003;

float shadow = (currentDepth - bias > closestDepth) ? 0.2 : 1.0;
```

为了避免 shadow map 外采样干扰，建议加边界判断：

```hlsl
if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0) {
    shadow = 1.0;
}
```

第一版 hard shadow 可以不做 PCF。

## 11. 常见问题

如果 scene pass 黑屏：

```text
scene pipeline 没创建成功
buildCommandBuffer 仍然调用 drawQuad
swapchain color layout 没 transition
scene UBO 没更新
shader location 和 VertexComponent 顺序不一致
depth attachment layout 不对
```

如果 scene 正常但没有阴影：

```text
scene.frag 没采样 shadow map
descriptorSets[currentBuffer].scene 没绑定 shadowMap
ShadowCoord 没除以 w
lightSpace 矩阵没传给 scene UBO
shadow compare 方向反了
bias 太大
```

如果场景全黑：

```text
shadow compare 方向反了
bias 太小
shadowCoord 超出 [0, 1] 但没有边界判断
shadow map descriptor layout 和实际 layout 不一致
```

如果阴影大量 acne：

```text
bias 太小
shadow pass 没有 vkCmdSetDepthBias
normal/light 方向不稳定
light projection near/far 太大
```

## 12. 完成后再进入 PCF

当 hard shadow 工作后，再做 PCF：

```text
3x3 kernel
每个 sample 单独 compare
求平均
```

不要一开始就用原工程的 PCF shader。

先手写最简单版本，确认你理解：

```text
PCF = 多次 hard shadow compare 的平均
```

这一步完成后，你的 Lab1 就正式进入 Games202 阴影实验阶段了。

