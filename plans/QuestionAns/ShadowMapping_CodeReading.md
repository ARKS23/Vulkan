# ShadowMapping.cpp 阅读指南：从原项目封装读到自己的 Lab1

这份文档带你阅读 `examples/shadowmapping/shadowmapping.cpp`。

你已经完成了 `Lab0` 的 Vulkan 1.3 dynamic rendering 路线，所以第一次读这个 sample 会觉得有点“不一样”：它很多事情直接交给了 `VulkanExampleBase`，并且整体还是传统 `VkRenderPass + VkFramebuffer` 写法。

阅读时不要被差异吓到。我们要吸收的是它的 **Shadow Mapping 数据流**，不是把它的老式结构完整搬进你的 Lab1。

## 1. 先给结论：它和 Lab0 最大区别是什么

`Lab0` 现在更像你自己写的小框架：

```text
自己创建 command pool / command buffer
自己管理 semaphore / fence
自己用 VMA 管理 buffer / image
自己用 dynamic rendering 开始和结束 pass
自己显式做 image layout transition
```

`shadowmapping.cpp` 更像原项目传统 sample：

```text
基类创建 swapchain
基类创建主 renderPass
基类创建主 framebuffer
基类创建 depthStencil
基类创建 drawCmdBuffers
基类处理 prepareFrame / submitFrame
sample 自己只补 shadow map 需要的 offscreen pass
```

所以你会看到它少写了很多你在 Lab0 中手动写过的代码。

这不是它“不需要”，而是已经藏在 `VulkanExampleBase` 里了。

## 2. 基类到底帮它做了什么

`shadowmapping.cpp::prepare()` 第一行是：

```cpp
VulkanExampleBase::prepare();
```

这行非常重。基类会做：

```text
createSurface()
createCommandPool()
createSwapChain()
createCommandBuffers()
createSynchronizationPrimitives()
setupDepthStencil()
setupRenderPass()
createPipelineCache()
setupFrameBuffer()
UI overlay resources
```

对应到 `shadowmapping.cpp` 里，它后面才能直接使用这些成员：

```text
drawCmdBuffers[currentBuffer]
renderPass
frameBuffers[currentImageIndex]
depthStencil
pipelineCache
presentCompleteSemaphores
renderCompleteSemaphores
waitFences
currentBuffer
currentImageIndex
```

你在 Lab0 中自己写过的很多流程，在这里是基类提供的。

## 3. prepareFrame / submitFrame 对应 Lab0 的哪部分

`shadowmapping.cpp::render()` 很短：

```cpp
VulkanExampleBase::prepareFrame();
updateLight();
updateUniformBuffers();
buildCommandBuffer();
VulkanExampleBase::submitFrame();
```

它对应 Lab0 里的手写流程：

```text
prepareFrame()
-> wait fence
-> reset fence
-> acquire swapchain image
-> update overlay

buildCommandBuffer()
-> 录制本帧命令

submitFrame()
-> queue submit
-> present
-> currentBuffer = (currentBuffer + 1) % maxConcurrentFrames
```

所以这个 sample 的 `render()` 看起来很轻，是因为 acquire / submit / present 都被基类收走了。

你自己的 Lab1 如果继续沿用 Lab0 写法，可以不使用 `prepareFrame()/submitFrame()`，继续手写也完全合理。

## 4. 这份代码的主线

建议你按这个顺序阅读：

```text
成员变量
-> prepare()
-> prepareOffscreenFramebuffer()
-> prepareOffscreenRenderpass()
-> prepareUniformBuffers()
-> setupDescriptors()
-> preparePipelines()
-> updateUniformBuffers()
-> buildCommandBuffer()
-> HLSL shader
```

不要从析构函数或者 UI 开始读。Shadow Mapping 的灵魂在资源数据流。

## 5. 成员变量：先看它在组织哪些资源

### 5.1 开关和阴影参数

```cpp
bool displayShadowMap = false;
bool filterPCF = true;

float zNear = 1.0f;
float zFar = 96.0f;

float depthBiasConstant = 1.25f;
float depthBiasSlope = 1.75f;
```

这几个变量就是 UI 上能调的阴影行为：

```text
displayShadowMap
-> 不渲染正常场景，直接显示 shadow map，方便 debug

filterPCF
-> 是否使用 PCF 版本 pipeline

zNear / zFar
-> light projection 的深度范围，影响 shadow map 精度

depthBiasConstant / depthBiasSlope
-> 避免 shadow acne，但过大又会导致 Peter Panning
```

这几个以后你做 Games202 阴影实验时都要保留，甚至要做成 UI 参数。

### 5.2 两套 UBO

```cpp
struct UniformDataScene {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
    glm::mat4 depthBiasMVP;
    glm::vec4 lightPos;
    float zNear;
    float zFar;
};

struct UniformDataOffscreen {
    glm::mat4 depthMVP;
};
```

它拆成两套是合理的：

```text
UniformDataOffscreen
-> 给 shadow pass 使用
-> 只需要 light 视角的 MVP

UniformDataScene
-> 给 camera scene pass 使用
-> 需要相机矩阵、模型矩阵、lightSpace 矩阵、灯光位置
```

你的 Lab1 也建议这么拆。不要把所有矩阵塞进一个巨大的 UBO 里。

### 5.3 三类 descriptor set

```cpp
struct DescriptorSets {
    VkDescriptorSet offscreen;
    VkDescriptorSet scene;
    VkDescriptorSet debug;
};
```

这非常重要。

```text
offscreen set
-> shadow pass 使用
-> binding 0: offscreen UBO

scene set
-> 正常场景 pass 使用
-> binding 0: scene UBO
-> binding 1: shadow map sampler

debug set
-> shadow map 可视化 pass 使用
-> binding 0: scene UBO，用 zNear/zFar 线性化深度
-> binding 1: shadow map sampler
```

注意：它为了简单复用了同一个 descriptor set layout。
offscreen set 虽然 layout 里有 binding 1，但 offscreen shader 不访问 shadow sampler，所以只写 binding 0 也能工作。

你自己的 Lab1 可以更清晰一点：给 shadow pass 和 scene pass 分两个 layout。

## 6. OffscreenPass：Shadow Map 的资源集合

```cpp
struct OffscreenPass {
    int32_t width, height;
    VkFramebuffer frameBuffer;
    FrameBufferAttachment depth;
    VkRenderPass renderPass;
    VkSampler depthSampler;
    VkDescriptorImageInfo descriptor;
} offscreenPass{};
```

它表达的是：

```text
shadow map 是一个独立 pass
这个 pass 有自己的尺寸
这个 pass 有自己的 depth image
这个 pass 有自己的 renderPass/framebuffer
这个 depth image 后续会被 scene pass 采样
```

如果迁移到你的 dynamic rendering + VMA 风格，大概会变成：

```cpp
struct ShadowMap {
    AllocatedImage depth;
    VkSampler sampler;
    VkDescriptorImageInfo descriptor;
    VkExtent2D extent;
    VkFormat format;
};
```

也就是说：

```text
原 sample 的 VkImage + VkDeviceMemory
-> 替换为你的 AllocatedImage + VMA

原 sample 的 renderPass + framebuffer
-> 替换为 vkCmdBeginRendering + VkRenderingInfo
```

## 7. prepareOffscreenFramebuffer：创建 shadow depth texture

这个函数做五件事：

```text
1. 设置 shadow map 尺寸为 2048x2048
2. 创建 depth image
3. 分配并绑定显存
4. 创建 depth image view
5. 创建 sampler
6. 创建 offscreen render pass 和 framebuffer
```

最关键的是 image usage：

```cpp
image.usage =
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
    VK_IMAGE_USAGE_SAMPLED_BIT;
```

这句话就是 Shadow Mapping 的 Vulkan 资源本质：

```text
第一遍：它是 depth attachment，被写入
第二遍：它是 sampled texture，被读取
```

这和你 Step6 的 offscreen color 很像：

```text
Step6 offscreenColor:
    COLOR_ATTACHMENT_BIT | SAMPLED_BIT

Step7 shadowMap.depth:
    DEPTH_STENCIL_ATTACHMENT_BIT | SAMPLED_BIT
```

sampler 的 `borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE` 也值得记住。

当 shadow coord 跑到 shadow map 外部时，白色通常代表“没有遮挡”，可以减少边缘黑影。

## 8. prepareOffscreenRenderpass：传统 render pass 如何管理 layout

这段代码是传统 Vulkan 的写法：

```cpp
attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
```

意思是：

```text
进入 render pass 前，不关心原始内容
render pass 内作为 depth attachment 写入
render pass 结束后，自动变成可被 shader 读取的 depth layout
```

它还写了两个 subpass dependency：

```text
fragment shader read -> depth attachment write
depth attachment write -> fragment shader read
```

原 sample 依靠 render pass dependency 帮它做同步和 layout transition。

而你的 Lab0 / Lab1 dynamic rendering 路线不会自动有这些 dependency。
所以你需要手动写：

```text
DEPTH_STENCIL_READ_ONLY_OPTIMAL -> DEPTH_ATTACHMENT_OPTIMAL
DEPTH_ATTACHMENT_OPTIMAL       -> DEPTH_STENCIL_READ_ONLY_OPTIMAL
```

这是它和 Lab0 最大的实践差异之一。

## 9. setupDescriptors：三套 set 如何连接两个 pass

它的 descriptor layout 有两个 binding：

```text
binding 0: uniform buffer
binding 1: combined image sampler
```

然后每个 in-flight frame 分配三套 descriptor set：

```text
descriptorSets[i].debug
descriptorSets[i].offscreen
descriptorSets[i].scene
```

其中 shadow map descriptor 是：

```cpp
VkDescriptorImageInfo shadowMapDescriptor =
    descriptorImageInfo(
        offscreenPass.depthSampler,
        offscreenPass.depth.view,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
```

这表示 scene/debug pass 采样的就是第一遍生成的 shadow depth。

你要记住这个连接：

```text
offscreen pass 输出:
    offscreenPass.depth.view

scene pass 输入:
    binding 1 -> shadowMapDescriptor -> offscreenPass.depth.view
```

这就是多 pass 算法的血管。

## 10. preparePipelines：四条 pipeline 分别做什么

它创建了四条 pipeline：

```text
pipelines.debug
-> 全屏三角形显示 shadow map

pipelines.sceneShadow
-> 正常场景，hard shadow

pipelines.sceneShadowPCF
-> 正常场景，PCF shadow

pipelines.offscreen
-> shadow pass，只写 depth
```

### 10.1 debug pipeline

```cpp
shaderStages[0] = quad.vert.spv
shaderStages[1] = quad.frag.spv
pipelineCI.pVertexInputState = &emptyInputState;
```

这个和你的 Step6 fullscreen blit 非常像。

`quad.vert` 使用 `SV_VertexID` 生成全屏三角形，不需要 vertex buffer。

`quad.frag` 采样 shadow map，把 depth 可视化。

### 10.2 sceneShadow / sceneShadowPCF

两条 pipeline 用同一套 shader：

```cpp
scene.vert.spv
scene.frag.spv
```

区别是 specialization constant：

```cpp
enablePCF = 0;
vkCreateGraphicsPipelines(..., &pipelines.sceneShadow);

enablePCF = 1;
vkCreateGraphicsPipelines(..., &pipelines.sceneShadowPCF);
```

HLSL 里对应：

```hlsl
[[vk::constant_id(0)]] const int enablePCF = 0;
```

这个技巧很优雅：同一个 shader 编译出两条 pipeline，运行时不用 if 控制大逻辑，只在 pipeline 创建时决定功能。

你自己的 Lab1 第一版可以先不用 specialization constant，直接在 shader 里 hardcode PCF 开关。等跑通后再加。

### 10.3 offscreen pipeline

这条 pipeline 只用 vertex shader：

```cpp
shaderStages[0] = offscreen.vert.spv;
pipelineCI.stageCount = 1;
colorBlendStateCI.attachmentCount = 0;
pipelineCI.renderPass = offscreenPass.renderPass;
```

它的目标不是输出颜色，而是让 rasterization + depth test/write 写入 depth attachment。

这里还启用了 depth bias：

```cpp
rasterizationStateCI.depthBiasEnable = VK_TRUE;
dynamicStateEnables.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
```

然后实际录制命令时调用：

```cpp
vkCmdSetDepthBias(cmdBuffer, depthBiasConstant, 0.0f, depthBiasSlope);
```

这就是减少 shadow acne 的关键手段。

## 11. updateUniformBuffers：光源视角矩阵怎么来

这段是 shadow mapping 的数学核心：

```cpp
glm::mat4 depthProjectionMatrix =
    glm::perspective(glm::radians(lightFOV), 1.0f, zNear, zFar);

glm::mat4 depthViewMatrix =
    glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));

uniformDataOffscreen.depthMVP =
    depthProjectionMatrix * depthViewMatrix * depthModelMatrix;
```

它做的是：

```text
从 light 的视角看整个场景
把场景投影到 shadow map 中
记录每个 texel 最近的深度
```

然后 scene pass 也需要同一个 light-space 矩阵：

```cpp
uniformDataScene.depthBiasMVP = uniformDataOffscreen.depthMVP;
```

在 `scene.vert` 里，会把世界坐标变换到 shadow map UV 空间：

```hlsl
output.ShadowCoord =
    mul(biasMat, mul(ubo.lightSpace, mul(ubo.model, float4(input.Pos, 1.0))));
```

其中 `biasMat` 负责：

```text
NDC [-1, 1] -> UV [0, 1]
```

你的 Lab1 如果做方向光，建议第一版改成正交投影：

```cpp
glm::mat4 lightProj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 40.0f);
```

这会比透视光源更贴近 Games202 的方向光阴影实验。

## 12. buildCommandBuffer：两遍渲染在哪里发生

这是整份代码最重要的函数。

### 12.1 第一遍：从 light POV 生成 shadow map

```cpp
renderPassBeginInfo.renderPass = offscreenPass.renderPass;
renderPassBeginInfo.framebuffer = offscreenPass.frameBuffer;
renderPassBeginInfo.renderArea.extent.width = offscreenPass.width;
renderPassBeginInfo.renderArea.extent.height = offscreenPass.height;

vkCmdBeginRenderPass(cmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
```

这一遍的特点：

```text
render target 是 shadow map framebuffer
viewport 是 shadow map 尺寸
clear depth = 1.0
绑定 pipelines.offscreen
绑定 descriptorSets[currentBuffer].offscreen
画 glTF 场景
不输出 color
只写 depth
```

然后：

```cpp
vkCmdEndRenderPass(cmdBuffer);
```

由于 offscreen render pass 的 `finalLayout` 是 `DEPTH_STENCIL_READ_ONLY_OPTIMAL`，结束后 shadow map 就可以被 fragment shader 采样。

### 12.2 第二遍：从 camera POV 渲染场景并采样 shadow map

```cpp
renderPassBeginInfo.renderPass = renderPass;
renderPassBeginInfo.framebuffer = frameBuffers[currentImageIndex];
```

这里用的是基类创建的主 render pass 和 swapchain framebuffer。

这一遍的特点：

```text
render target 是当前 swapchain image
viewport 是窗口尺寸
绑定 scene/debug descriptor set
根据 UI 选择 debug 或正常阴影场景
最后 drawUI
```

如果 `displayShadowMap == true`：

```text
绑定 pipelines.debug
vkCmdDraw(3)
直接显示 shadow map
```

否则：

```text
绑定 pipelines.sceneShadow 或 pipelines.sceneShadowPCF
画 glTF 场景
fragment shader 中采样 shadow map 判断阴影
```

这就是你明天 Lab1 要复现的核心流程。

## 13. HLSL 阅读重点

### 13.1 offscreen.vert

```hlsl
return mul(ubo.depthMVP, float4(Pos, 1.0));
```

它只做一件事：把模型顶点变到 light clip space。

没有 fragment shader 也能写 depth，因为 depth 是 rasterization 后由固定管线产生并写入 depth attachment。

### 13.2 scene.vert

它除了正常输出位置、法线、颜色，还输出：

```hlsl
output.ShadowCoord = mul(biasMat, mul(ubo.lightSpace, mul(ubo.model, float4(input.Pos, 1.0))));
```

这个 `ShadowCoord` 会传给 fragment shader，用来查 shadow map。

### 13.3 scene.frag

核心函数是：

```hlsl
float textureProj(float4 shadowCoord, float2 off)
{
    float dist = shadowMapTexture.Sample(shadowMapSampler, shadowCoord.xy + off).r;
    if (shadowCoord.w > 0.0 && dist < shadowCoord.z) {
        shadow = ambient;
    }
}
```

逻辑是：

```text
dist
-> shadow map 中记录的最近深度

shadowCoord.z
-> 当前 fragment 在 light space 中的深度

dist < shadowCoord.z
-> 当前点比 shadow map 记录得更远，说明被挡住了
```

PCF 只是对周围多个 texel 做多次 `textureProj()`，然后平均。

```text
hard shadow:
    采样 1 次

PCF:
    周围 3x3 采样 9 次
    多个 hard compare 求平均
```

这点非常 Games202。

## 14. 和你的 Lab0 逐项对照

| 主题 | shadowmapping.cpp | Lab0 当前路线 |
|---|---|---|
| 主渲染路径 | 传统 render pass | dynamic rendering |
| swapchain acquire/present | 基类 `prepareFrame/submitFrame` | 你在 `render()` 里手写 |
| command buffer | 基类 `drawCmdBuffers` | 自己的 `commandBuffers` |
| 同步对象 | 基类创建 | 自己创建 |
| buffer/image 内存 | `vks::Buffer` + 手动 memory | VMA + `AllocatedBuffer/AllocatedImage` |
| 模型 | glTF loader | 你自己的 mesh |
| offscreen color | 没有 | Step6 已有 |
| shadow map | depth framebuffer | 下一步要做 |
| layout transition | render pass dependency 隐式处理 | 你需要显式 barrier |
| pipeline render target 信息 | `pipelineCI.renderPass` | `VkPipelineRenderingCreateInfoKHR` |

这张表要牢牢记住。

你明天写 Lab1 时，不应该把 `prepareOffscreenRenderpass()` 和 `VkFramebuffer` 照搬过去。
你应该把它翻译成：

```text
create ShadowMap AllocatedImage
create sampler + descriptor
render() 中手动 transition depth layout
vkCmdBeginRendering(depth attachment only)
vkCmdEndRendering()
transition depth attachment -> shader read
scene pass 采样 shadow map
```

## 15. 明天动工前的建议阅读任务

建议你今晚或明天开工前完成这几个小阅读点：

1. 在 `buildCommandBuffer()` 里标出两个 pass 的开始和结束。
2. 在 `setupDescriptors()` 里画出 `offscreenPass.depth.view` 如何进入 `scene/debug descriptor set`。
3. 在 `preparePipelines()` 里标出四条 pipeline 的用途。
4. 在 `scene.vert` 里确认 `ShadowCoord` 是如何从 world position 算出来的。
5. 在 `scene.frag` 里手写一遍 `dist < shadowCoord.z` 的含义。

你可以把这五点写成注释或笔记。写完后，Shadow Mapping 就会从“样例代码”变成“你知道自己要复刻的数据流”。

## 16. 明天 Lab1 的最小迁移计划

建议你第一天只做最小闭环：

```text
1. 复制 Lab0 为 Lab1ShadowMap
2. 新增 ShadowMap 结构
3. 创建 depth sampled image
4. 添加 shadowDepthPipeline
5. render() 中先录制 shadow pass
6. 添加 debug fullscreen pipeline 显示 shadow map
```

第一天可以先不做最终阴影。

只要你能在屏幕上显示 shadow map，就已经成功了一半。剩下的 scene pass 阴影判断，反而是比较可控的 shader 逻辑。

## 17. 这份代码里哪些可以借鉴，哪些不要照抄

可以借鉴：

```text
OffscreenPass 的资源组织思想
debug shadow map 开关
depth bias 参数
offscreen / scene / debug 三套 descriptor set 的职责拆分
PCF 用 specialization constant 切 pipeline
shadow map 先 debug 再接入 scene pass 的调试顺序
```

不要照抄：

```text
手动 vkAllocateMemory
传统 offscreen renderPass/framebuffer
所有 pass 共用一个 descriptor set layout 的偷懒方式
透视 light projection 作为你的第一版方向光阴影
把 Lab1 继续塞回 Lab0
```

你已经有了更现代、更适合实验扩展的底座。
我们读这个 sample，是为了拿到算法路径和 Vulkan 资源关系，不是回退到旧架构。

