# Vulkan C++ examples and demos

A comprehensive collection of open source C++ examples for [Vulkan®](https://www.vulkan.org), the low-level graphics and compute API from Khronos.

## ARK Branch
- 图形算法实验项目，在原项目的基础上扩展了`VMA`管理内存分配，供后续图形算法实验复用。
- 封装了纹理资源、管线、动态渲染等操作，减少模板代码量。
- 把一些常用的着色器函数封装到通用着色器文件中复用。

### 固定管线状态封装 （参考教程Vulkan-Guide）

在 `base/` 目录下补充了一组面向实验复用的 Vulkan 工具层，用来减少每个 Lab 重复编写固定管线和资源管理模板代码：

- `base/vk_pipelines.*`：封装 `vkutil::PipelineBuilder`，集中管理 shader stage、vertex input、input assembly、rasterizer、depth/stencil、blend、dynamic state 和 dynamic rendering attachment format。
- `base/vk_descriptors.*`：封装 descriptor set layout、descriptor pool、descriptor write 等常见创建逻辑。
- `base/vk_images.*` / `base/vk_resources.*`：封装 VMA buffer/image 创建、销毁、layout transition、staging upload 等资源操作。
- `base/vk_rendering.*`：封装 dynamic rendering 常用 attachment info 和 begin/end helpers。

pipeline 创建方式如下：

```cpp
vkutil::PipelineBuilder builder;
builder.setPipelineLayout(pipelineLayout)
    .setShaders(
        loadShader(vertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT),
        loadShader(fragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT))
    .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({
        vkglTF::VertexComponent::Position,
        vkglTF::VertexComponent::Normal,
        vkglTF::VertexComponent::UV
    }))
    .setColorAttachmentFormat(hdrFormat)
    .setDepthFormat(depthFormat)
    .enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL)
    .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
    .disableBlending();

VkPipeline pipeline = builder.build(device, pipelineCache);
```

这样每个实验只需要描述“这条管线和别的管线不同的状态”，例如是否 MRT、是否 depth test、是否 additive blending、拓扑是 triangle 还是 line list，而不用反复填完整的 `VkGraphicsPipelineCreateInfo`。

### 着色器通用函数

HLSL 侧增加了 `shaders/hlsl/common/` 公共函数目录，用于放置跨 Lab 复用的纯计算函数：

- `Math.hlsli`：数学常量和通用数学函数，例如 `PI`。
- `Color.hlsli`：颜色空间与显示变换，例如 `ACESFilm`、`LinearToSRGB`。
- `PBR.hlsli`：PBR BRDF 相关函数，例如 GGX、Smith、Fresnel、Cook-Torrance BRDF。
- `Bloom.hlsli`：Bloom 下采样、Karis average、tent upsample 等滤波函数。

shader 中推荐使用稳定的工程相对路径引用：

```hlsl
#include "common/PBR.hlsli"
#include "common/Color.hlsli"
```

`shaders/hlsl/compileshaders.py` 会把 `shaders/hlsl` 加入 include path，因此各个 Lab 的 shader 不需要单独写 `-I` 参数。公共文件里尽量放“无资源绑定、无采样器依赖、无具体 pass 状态”的纯函数；纹理采样、descriptor 绑定和 pass 专用逻辑仍保留在各自 shader 中，这样公共函数可以被 Lab2、Lab3 以及后续实验安全复用。

## Lab
### Lab1 : Shadow mapping
- [Lab1文档](examples\lab1\README.md)
- 展示

https://github.com/user-attachments/assets/c6d4ad54-c4fb-4a4c-b212-20ca4c8ed60c

### Lab2 : PBR + Bloom
- 展示

https://github.com/user-attachments/assets/3c68b9dd-9acf-48ad-ae42-67719fdaac18





