# Lab1: Shadow Mapping

## 1. 实验目标

- 实现基础 Shadow Mapping。
- 对比 Hard Shadow、PCF、Poisson PCF、PCSS。
- 加入 Bias、Debug Views、UI 调参，理解阴影算法的问题和解决方法。

## 2. 项目结构

- `lab1.cpp / lab1.h`：Vulkan 渲染流程、资源、管线、UI。
- `scene.vert / scene.frag`：场景渲染与阴影计算。
- `shadowVertex.vert`：shadow pass 深度写入。
- `light.vert / light.frag`：光源可视化。
- `quad.vert / quad.frag`：shadow map debug 显示。

## 3. Vulkan 渲染流程

- 初始化资源：shadow map、UBO、descriptor、pipeline。
- Shadow Pass：从光源视角写入深度图。
- Scene Pass：从相机视角渲染场景并采样 shadow map。
- Light Debug Pass：绘制光源小球。
- UI Overlay：调节光源、bias、阴影模式。

## 4. Shadow Mapping 原理

- Light MVP / light space position。
- Shadow map 中保存的 depth。
- 当前片元 `currentDepth` 和 `closestDepth` 比较。
- 为什么使用 `shadowCoord.z` 而不是 world position。

## 5. Bias 处理

- Shadow acne 问题。
- Peter panning 问题。
- Raster Depth Bias：`vkCmdSetDepthBias`。
- Shader Bias：fragment shader 中比较深度时使用。
- Dynamic Slope Bias：根据 `NdotL` 调整 bias。

## 6. PCF

- Hard Shadow 的锯齿问题。
- PCF 的基本采样方式。
- 采样半径和性能关系。
- 方形 PCF 的优缺点。

## 7. Poisson PCF

- 为什么使用 Poisson Disk。
- 固定采样数 vs 方形 PCF 半径平方增长。
- Rotated Poisson / 噪声的作用。
- 画质和性能对比。

## 8. PCSS

- Blocker Search。
- Penumbra Size 估计。
- Variable Radius PCF。
- 为什么 PCSS 更依赖 Poisson 采样。
- `LightSize` 对软阴影的影响。

## 9. Debug Views

- Shadow Mask。
- Shadow UV。
- Current Depth。
- Closest Depth。
- Shadow Bias。
- Normal Visualization。
- 如何通过 debug view 定位问题。

## 10. 性能与画质对比

- Hard Shadow。
- PCF 不同 radius。
- Poisson PCF 不同 sample count。
- PCSS 不同 light size。
- 截图和简单表格。

## 11. 遇到的问题

- Descriptor 绑定 shadow map。
- Push Constant 字段同步。
- HLSL 编译与 `.spv` 文件。
- Bias 调参。
- Poisson offset / PCSS 断层问题。

## 12. 实验总结

- 我理解了什么。
- 哪些问题还没解决。
- 后续计划：SSAO、Deffered Rendering、PBR/IBL。
