# Lab1 Shadow Lab 进阶路线：从 PCF 到可调试阴影实验台

你现在已经完成了：

```text
Shadow Map
Hard Shadow
PCF
UI 调参
Raster Bias / Shader Bias
光源可视化物体
```

这说明 Lab1 已经不是一个简单 demo，而是可以继续扩展成“阴影实验台”。下一步建议不要马上开新 Lab，而是把 Lab1 打磨到能解释、能调试、能测量、能对比的状态。

推荐路线：

```text
Debug Views
-> Bias 系统实验
-> Timestamp Query 性能测量
-> Light Frustum / Ortho Box 可视化
-> PCF 优化
-> PCSS
```

## 1. Debug Views

### 目标

给 shader 增加 UI 可切换的 debug mode，让你不用猜阴影问题来自哪里。

推荐模式：

```text
0: 正常渲染
1: shadow mask
2: shadowUV
3: currentDepth
4: shadowMapDepth
5: normal
6: baseColor
7: dynamic bias
```

### 为什么先做它

后续你会继续做 bias、Poisson PCF、PCSS。没有 debug view 的话，阴影错了很难定位。

例如：

```text
shadowUV 错 -> light space transform 或 NDC 到 UV 转换有问题
currentDepth 错 -> light-space depth 不对
shadowMapDepth 错 -> shadow pass 写入或 layout 有问题
normal 错 -> vertex input 或 normal transform 有问题
bias 错 -> acne / peter panning 很难调
```

### 建议实现

在 push constants 里加：

```cpp
int32_t debugMode = 0;
```

UI：

```cpp
overlay->sliderInt("Debug Mode", &pushConstan.debugMode, 0, 7);
```

HLSL 中根据 `debugMode` 选择输出。

示例：

```hlsl
if (pushConstan.debugMode == 1) {
    return float4((1.0 - shadow).xxx, 1.0);
}

if (pushConstan.debugMode == 5) {
    return float4(normalize(input.normal) * 0.5 + 0.5, 1.0);
}
```

验收标准：

```text
能看到 shadow mask 黑白图
能看到 normal 方向颜色
能看到 baseColor
能用 debug view 判断某个阴影异常来自哪个阶段
```

## 2. Bias 系统实验

### 目标

系统理解三类 bias：

```text
Raster Depth Bias
Shader Compare Bias
Dynamic Slope Bias
```

### 三者区别

```text
Raster Depth Bias:
在 shadow pass 写入深度时偏移深度。
对应 vkCmdSetDepthBias。

Shader Compare Bias:
在 scene pass 采样 shadow map 后比较深度时减去 bias。
对应 currentDepth - bias > closestDepth。

Dynamic Slope Bias:
根据 normal 和 light direction 的夹角动态调整 shader bias。
表面越斜，bias 越大。
```

### 推荐实验顺序

1. 只用 Raster Depth Bias

```text
shader bias 设为 0 或极小值
调 Raster Bias / Raster Slope
观察 acne 是否减少
```

2. 只用 Shader Compare Bias

```text
Raster Bias 设为 0
调 shader bias
观察 acne 和 peter panning
```

3. 两者结合

```text
Raster Bias 负责大部分 acne
Shader Bias 保留一个很小值
```

4. 加 Dynamic Slope Bias

HLSL 思路：

```hlsl
float computeSlopeBias(float3 N, float3 L)
{
    float ndotl = saturate(dot(N, L));
    return max(pushConstan.minShadowBias, pushConstan.slopeShadowBias * (1.0 - ndotl));
}
```

### 推荐 UI 参数

```text
Raster Bias: 0.0 - 5.0
Raster Slope: 0.0 - 5.0
Min Bias: 0.0 - 0.01
Slope Bias: 0.0 - 0.05
```

### 验收标准

```text
能制造并解释 shadow acne
能制造并解释 peter panning
能说明 Raster Bias 和 Shader Bias 的区别
能用 dynamic bias 减少斜面 acne
```

## 3. Timestamp Query 性能测量

### 目标

不要只看 UI 的 FPS，而是测每个 pass 的 GPU 时间。

建议至少测：

```text
Shadow Pass
Scene Pass
PCF radius 对 Scene Pass 的影响
UI / Light Gizmo 是否有明显开销
```

### 为什么要做

PCF 的开销主要在 scene fragment shader。

采样次数是：

```text
(2 * radius + 1)^2
```

所以：

```text
radius 1 -> 9 samples
radius 2 -> 25 samples
radius 3 -> 49 samples
radius 4 -> 81 samples
```

用 timestamp query 后，你可以得到真实的 GPU pass 时间，而不是只凭 FPS 感觉。

### 推荐实现思路

创建一个 query pool：

```cpp
VkQueryPool timestampQueryPool;
```

每帧 command buffer 中：

```cpp
vkCmdResetQueryPool(commandBuffer, timestampQueryPool, 0, queryCount);

vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampQueryPool, 0);
drawShadowMap(commandBuffer);
vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampQueryPool, 1);

vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampQueryPool, 2);
drawScene(commandBuffer);
vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampQueryPool, 3);
```

CPU 读取结果后，用 `timestampPeriod` 转换成毫秒。

### 验收标准

```text
UI 上显示 Shadow Pass ms
UI 上显示 Scene Pass ms
能看到 PCF radius 变大时 Scene Pass 时间增长
能解释为什么 shadow pass 时间变化较小、scene pass 变化明显
```

## 4. Light Frustum / Ortho Box 可视化

### 目标

你现在能看到光源位置了，下一步应该看到光源的投影范围。

对当前方向光/正交 shadow map 来说，你需要可视化的是：

```text
light view-projection 的 orthographic box
```

也就是：

```cpp
glm::ortho(left, right, bottom, top, near, far)
```

这个范围决定了场景如何映射到 shadow map。

### 为什么重要

shadow map 精度和光源投影范围强相关：

```text
ortho 范围太大 -> 场景占 shadow map 很小一块 -> 阴影糊
ortho 范围太小 -> 场景或阴影被裁掉
near/far 太大 -> 深度精度浪费
```

如果你能画出 light ortho box，就能直观看到 shadow 精度为什么变化。

### 推荐实现

做一个简单 line pipeline：

```text
输入：位置
拓扑：VK_PRIMITIVE_TOPOLOGY_LINE_LIST
shader：unlit color
depth test：true
depth write：false
```

先生成 8 个角点和 12 条边。

世界空间角点可以从 light view-projection 的逆矩阵变换得到：

```cpp
glm::mat4 invLightVP = glm::inverse(depthProjectionMatrix * depthViewMatrix);
```

NDC 8 个角：

```text
(-1, -1, 0)
( 1, -1, 0)
( 1,  1, 0)
(-1,  1, 0)
(-1, -1, 1)
( 1, -1, 1)
( 1,  1, 1)
(-1,  1, 1)
```

注意 Vulkan 深度范围是 `[0, 1]`，不是 OpenGL 的 `[-1, 1]`。

### 验收标准

```text
能看到包围场景的 light ortho box
调 ortho 范围时 box 尺寸变化
能解释 shadow map 精度和 box 大小的关系
```

## 5. PCF 优化

### 目标

从方形 PCF 过渡到更接近工程实践的采样方式。

当前方形 PCF：

```text
radius 1 -> 3x3
radius 2 -> 5x5
radius 3 -> 7x7
```

优点：

```text
直观
容易实现
适合学习
```

缺点：

```text
采样数增长快
边缘容易有方形滤波痕迹
```

### Poisson Disk PCF

Poisson PCF 用固定数量的随机分布采样点模拟柔和阴影。

示例：

```hlsl
static const float2 poissonDisk[16] = {
    float2(-0.94201624, -0.39906216),
    float2( 0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870),
    float2( 0.34495938,  0.29387760),
    float2(-0.91588581,  0.45771432),
    float2(-0.81544232, -0.87912464),
    float2(-0.38277543,  0.27676845),
    float2( 0.97484398,  0.75648379),
    float2( 0.44323325, -0.97511554),
    float2( 0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023),
    float2( 0.79197514,  0.19090188),
    float2(-0.24188840,  0.99706507),
    float2(-0.81409955,  0.91437590),
    float2( 0.19984126,  0.78641367),
    float2( 0.14383161, -0.14100790)
};
```

采样：

```hlsl
float shadow = 0.0;
for (int i = 0; i < sampleCount; i++) {
    float2 offset = poissonDisk[i] * filterRadius * texelSize;
    shadow += shadowCompare(shadowUV + offset, currentDepth, bias);
}
shadow /= sampleCount;
```

### 推荐 UI 参数

```text
PCF Mode: 0 方形 PCF, 1 Poisson PCF
Poisson Samples: 4 / 8 / 16
Filter Radius: 0.5 - 4.0
```

### 验收标准

```text
方形 PCF 和 Poisson PCF 能切换
Poisson 16 samples 比 radius 4 的 81 samples 更便宜
阴影边缘不再那么方
能用 timestamp query 证明性能差异
```

## 6. PCSS 过渡

PCSS 是 PCF 的自然下一步。

它模拟的是：

```text
遮挡物离接收面越远，阴影越软
遮挡物离接收面越近，阴影越硬
```

### PCSS 三步

1. Blocker Search

在当前片元附近找比当前深度更近的 shadow map depth。

```text
这些更近的 depth 就是 blocker
```

2. Penumbra Size

根据：

```text
receiverDepth - averageBlockerDepth
```

估计半影大小。

3. Variable Radius PCF

用估计出来的半影大小作为 PCF filter radius。

### 建议先不急

在进入 PCSS 前，先完成：

```text
Debug Views
Bias 实验
Timestamp Query
Poisson PCF
```

否则 PCSS 出问题时很难定位。

## 7. 当前 Lab1 的推荐完成标准

我建议 Lab1 最终做到这个程度：

```text
1. Hard Shadow / PCF 可切换
2. 方形 PCF / Poisson PCF 可切换
3. Bias 参数完整可调
4. Debug Views 完整
5. 光源位置可视化
6. Light Ortho Box 可视化
7. Timestamp Query 显示 pass 时间
8. README 记录实验结果
```

这样 Lab1 就不只是一个 shadow mapping demo，而是一个完整的 shadow lab。

## 8. 推荐下一步执行顺序

最建议按这个顺序：

```text
1. Debug Mode
2. Bias 实验记录
3. Timestamp Query
4. Light Ortho Box
5. Poisson PCF
6. PCSS
```

如果你想本周推进，建议：

```text
今天：Debug Mode
明天：Bias 对比实验
后天：Timestamp Query
周末：Light Ortho Box
```

不要把 PCSS 提前到 debug 系统之前。现在先把实验台搭稳，后面做高级阴影会舒服很多。
