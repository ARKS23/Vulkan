# Lab1 Shadow Map Debug：从“红底黑影”走向可读的深度图

你现在看到的画面是：

```text
大面积红色背景
上方有一个很小的黑色模型轮廓
```

这不是失败，反而是一个很好的阶段性信号。

它说明：

```text
1. Lab1 已经能正常启动
2. shadow depth image 创建成功
3. shadow pass 大概率已经写入了 depth
4. debug/fullscreen pass 确实采样到了 shadow map
5. descriptor、sampler、layout、fullscreen triangle 至少部分链路已经打通
```

现在要做的不是推倒重来，而是把 shadow map debug view 调成“可读状态”。

## 1. 为什么现在是红底黑影

你当前的 `quad.frag` 是：

```hlsl
Texture2D offscreenColorTexture : register(t1);
SamplerState offscreenColorSampler : register(s1);

output.color = offscreenColorTexture.Sample(offscreenColorSampler, input.uv);
```

但你采样的其实不是 color texture，而是 depth texture。

对 depth image 采样时，我们真正关心的是 `.r` 通道里的深度值：

```text
clear depth = 1.0
-> 背景深度通常是 1.0

模型写入的深度 < 1.0
-> 模型区域会更暗
```

很多情况下 depth sample 会表现成：

```text
R = depth
G = 0
B = 0
```

所以清屏深度 `1.0` 会变成红色，模型更近的深度接近黑色。

这解释了当前画面：

```text
红色背景：shadow map 中大部分 texel 还是 clear depth = 1.0
黑色形状：模型在 shadow pass 中写入了更近的 depth
```

## 2. 第一件事：把 depth debug shader 改成灰度显示

下一步先不要碰 C++。

先把 `quad.frag` 改成专门显示 depth 的 debug shader。

建议第一版：

```hlsl
Texture2D shadowMapTexture : register(t1);
SamplerState shadowMapSampler : register(s1);

struct FSInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 main(FSInput input) : SV_TARGET
{
    float depth = shadowMapTexture.Sample(shadowMapSampler, input.uv).r;
    return float4(depth.xxx, 1.0);
}
```

这样背景会接近白色，模型区域会变暗。

如果你觉得模型还是不明显，可以先反相：

```hlsl
return float4((1.0 - depth).xxx, 1.0);
```

这时背景会黑，模型会亮。

这一步的目标不是美观，而是确认 depth 分布。

## 3. 第二件事：shadow pass viewport/scissor 要用 shadow map 尺寸

你当前 `drawShadowMap()` 里 render area 是 shadow map 尺寸：

```cpp
renderingInfo.renderArea = { 0, 0, shadowMap.extent.width, shadowMap.extent.height };
```

但 viewport/scissor 用的是窗口尺寸：

```cpp
VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
VkRect2D scissor{ { 0, 0 }, { width, height } };
```

这会导致一个不干净的状态：

```text
shadow image 是 2048 x 2048
但实际 rasterize 只覆盖窗口大小，比如 1280 x 720
```

建议改成：

```cpp
VkViewport viewport{
    0.0f,
    0.0f,
    static_cast<float>(shadowMap.extent.width),
    static_cast<float>(shadowMap.extent.height),
    0.0f,
    1.0f
};

VkRect2D scissor{
    { 0, 0 },
    { shadowMap.extent.width, shadowMap.extent.height }
};
```

这一步完成后，shadow map 里的模型分布会更稳定，也更符合“shadow pass 写整张 shadow texture”的预期。

## 4. 第三件事：先固定 light，不要让它旋转

你现在每帧会：

```cpp
updateLight();
```

并且 `lightPos` 会绕圈。

调试 shadow map debug view 时，光源动起来会增加观察难度。

建议先临时固定：

```cpp
lightPos = glm::vec3(0.0f, -10.0f, 10.0f);
```

或者在 `render()` 里暂时注释：

```cpp
if (!paused || camera.updated) updateLight();
```

先让影子图稳定下来。

等 debug view 能清楚显示模型后，再恢复动态光源。

## 5. 第四件事：缩小 light projection 范围

你现在用的是：

```cpp
glm::ortho(-50.f, 50.f, -50.f, 50.f, shadowNearPlane, shadowFarPlane);
```

这个范围对初学调试太大了。

如果模型只占世界空间一小块，那么映射到 shadow map 后就会非常小。

这正好对应你现在看到的“小黑块”。

建议第一版缩成：

```cpp
glm::ortho(-10.f, 10.f, -10.f, 10.f, 0.1f, 50.0f);
```

甚至可以进一步试：

```cpp
glm::ortho(-5.f, 5.f, -5.f, 5.f, 0.1f, 30.0f);
```

观察规律：

```text
ortho 范围越大
-> 场景在 shadow map 里越小
-> shadow texel 精度越低

ortho 范围越小
-> 场景在 shadow map 里越大
-> 但太小会裁掉场景
```

这就是后面 CSM 要解决的问题之一。

## 6. 第五件事：UBO 写完后 flush

你现在是：

```cpp
memcpy(uniformBuffers[currentBuffer].shadowOffscreenBuffer.allocationInfo.pMappedData, &uniformDataShadow, sizeof(uniformDataShadow));
```

建议后面补：

```cpp
vmaFlushAllocation(
    allocator,
    uniformBuffers[currentBuffer].shadowOffscreenBuffer.allocation,
    0,
    sizeof(UniformDataShadowPass));
```

如果内存不是 host coherent，只 `memcpy` 不 flush 可能导致 GPU 读到旧矩阵。

你现在能看到东西，说明它不是当前最大问题，但这个习惯要建立起来。

## 7. 第六件事：记录 shadow map 当前 layout

你现在每帧都这样开始：

```cpp
vkutil::cmdTransitionImageLayout(
    commandBuffer,
    shadowMap.shadowTexture.image.image,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
    VK_IMAGE_ASPECT_DEPTH_BIT);
```

第一帧可以这么做。

但第一帧结束后，shadow map 已经被你切到了：

```text
VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
```

下一帧再说 old layout 是 `UNDEFINED`，从严格 Vulkan 语义上是不准确的。

建议像 Lab0 那样维护：

```cpp
shadowMap.shadowTexture.image.layout
```

每次 transition 后更新：

```cpp
shadowMap.shadowTexture.image.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
shadowMap.shadowTexture.image.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
```

这样后续 validation 会更干净，也更接近真实引擎资源管理方式。

## 8. 当前代码最值得优先检查的点

建议按这个顺序改，不要同时改一大堆：

```text
1. quad.frag 改灰度 depth debug
2. shadow pass viewport/scissor 改成 shadowMap.extent
3. 固定 lightPos
4. ortho 从 -50..50 缩小到 -10..10
5. UBO memcpy 后 flush
6. 维护 shadowMap image.layout
```

每改一步都运行一次。

你应该观察到：

```text
红色背景 -> 灰度/黑白背景
小黑块 -> 更清晰的深度轮廓
模型位置漂移 -> 固定
模型太小 -> 变大
depth debug 图 -> 更容易读
```

## 9. 下一阶段目标

等 debug shadow map 看起来稳定后，才进入真正的 scene shadow：

```text
Pass 1:
    light view -> shadow depth

Pass 2:
    camera view -> scene color
    fragment shader 采样 shadow map
    比较 currentDepth 和 closestDepth
```

也就是开始接：

```text
scene.vert
scene.frag
```

但现在还不要急。

你当前最重要的任务是让 shadow map debug view 清楚、稳定、可解释。

只要 debug view 可靠，后面的 hard shadow 和 PCF 就会变成 shader 逻辑，而不是 Vulkan 玄学。

