# 原工程 glTF 模型加载工具使用指南

这份文档讲的是原项目里的模型加载器：

```cpp
#include "VulkanglTFModel.h"
```

它基于 `tinygltf`，封装在 `base/VulkanglTFModel.h/.cpp` 中。你后续做 `Lab1ShadowMap`、GBuffer、SSAO、PBR 时，可以先复用它加载模型，不必一开始就自己写 mesh loader。

不过要先记住一句话：这个 loader 是原工程风格的工具，内部仍然使用 `VulkanDevice::createBuffer()`、手动 `VkDeviceMemory` 和自己的材质 descriptor。它可以很好地帮助你学习图形算法，但暂时不完全符合你现在 Lab0 的 VMA 风格。先用起来，等阴影和 GBuffer 跑通后，再考虑统一资源系统。

## 1. 它能帮你做什么

`vkglTF::Model` 会帮你完成这些事：

```text
读取 .gltf 文件
解析 node / mesh / primitive / material / texture / animation
生成统一的 vertex buffer 和 index buffer
把顶点/索引上传到 GPU device local buffer
创建材质贴图 image / sampler / descriptor set
提供 draw() 函数递归绘制所有 node
提供 vertex input layout helper
```

所以你使用它时，通常只需要关心四件事：

```text
1. 声明 vkglTF::Model
2. loadFromFile()
3. pipeline 使用 vkglTF::Vertex::getPipelineVertexInputState()
4. command buffer 里调用 model.draw()
```

## 2. 最小使用方式

头文件：

```cpp
#include "VulkanglTFModel.h"
```

成员变量：

```cpp
vkglTF::Model scene;
```

加载模型：

```cpp
void VulkanExample::loadAssets()
{
    const uint32_t glTFLoadingFlags =
        vkglTF::FileLoadingFlags::PreTransformVertices |
        vkglTF::FileLoadingFlags::PreMultiplyVertexColors |
        vkglTF::FileLoadingFlags::FlipY;

    scene.loadFromFile(
        getAssetPath() + "models/vulkanscene_shadow.gltf",
        vulkanDevice,
        queue,
        glTFLoadingFlags);
}
```

绘制：

```cpp
vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sceneDescriptorSet, 0, nullptr);

scene.draw(commandBuffer);
```

这会自动绑定模型自己的 vertex buffer 和 index buffer，然后对所有 primitive 发出 `vkCmdDrawIndexed()`。

## 3. 常用模型路径

项目模型在：

```text
assets/models/
```

常用入门模型：

```text
models/cube.gltf
models/plane.gltf
models/sphere.gltf
models/suzanne.gltf
models/teapot.gltf
models/vulkanscene_shadow.gltf
models/samplescene.gltf
models/sponza/sponza.gltf
```

路径一般这样写：

```cpp
getAssetPath() + "models/cube.gltf"
```

注意：当前 `Model::loadFromFile()` 内部调用的是 `tinygltf::LoadASCIIFromFile()`，优先使用 `.gltf`，不要把第一版实验建立在 `.glb` 上。

## 4. loadFromFile 参数解释

接口：

```cpp
void loadFromFile(
    std::string filename,
    vks::VulkanDevice* device,
    VkQueue transferQueue,
    uint32_t fileLoadingFlags = vkglTF::FileLoadingFlags::None,
    float scale = 1.0f);
```

参数含义：

```text
filename
-> glTF 文件路径，通常用 getAssetPath() 拼接

device
-> 原项目的 VulkanDevice wrapper，基类里是 vulkanDevice

transferQueue
-> 用于上传 vertex/index/texture 的 queue，通常传 queue

fileLoadingFlags
-> 加载时预处理开关

scale
-> 全局缩放
```

常见写法：

```cpp
model.loadFromFile(
    getAssetPath() + "models/suzanne.gltf",
    vulkanDevice,
    queue,
    glTFLoadingFlags,
    1.0f);
```

## 5. FileLoadingFlags 怎么选

定义在 `vkglTF::FileLoadingFlags`：

```cpp
PreTransformVertices
PreMultiplyVertexColors
FlipY
DontLoadImages
FlipUV
```

推荐静态场景第一版使用：

```cpp
const uint32_t glTFLoadingFlags =
    vkglTF::FileLoadingFlags::PreTransformVertices |
    vkglTF::FileLoadingFlags::PreMultiplyVertexColors |
    vkglTF::FileLoadingFlags::FlipY;
```

各 flag 含义：

```text
PreTransformVertices
-> 把 node 层级 transform 预先烘焙到 vertex position / normal 里
-> 静态场景很方便，shadow mapping 入门推荐用
-> 如果后续要做动画、骨骼或保留节点变换，要谨慎使用

PreMultiplyVertexColors
-> 把 material baseColorFactor 乘到 vertex color 里
-> 方便简单 shader 直接用 vertex color

FlipY
-> 翻转 Y 轴，很多 sample 都会开
-> 用于适配项目中的坐标/模型约定

DontLoadImages
-> 只加载几何，不加载贴图
-> depth-only shadow pass 或纯几何实验可以用

FlipUV
-> 翻转 UV 的 t 分量
-> 如果纹理上下颠倒时再考虑
```

如果你的 `Lab1ShadowMap` 第一版只需要几何和深度，可以先用：

```cpp
const uint32_t glTFLoadingFlags =
    vkglTF::FileLoadingFlags::PreTransformVertices |
    vkglTF::FileLoadingFlags::FlipY |
    vkglTF::FileLoadingFlags::DontLoadImages;
```

但如果你后面想让 scene pass 采样 glTF 材质贴图，就不要开 `DontLoadImages`。

## 6. Vertex Input：让 pipeline 和 HLSL 对齐

loader 的顶点结构是：

```cpp
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color;
    glm::vec4 joint0;
    glm::vec4 weight0;
    glm::vec4 tangent;
};
```

你不需要手写 `VkVertexInputAttributeDescription`，可以直接用：

```cpp
pipelineCI.pVertexInputState =
    vkglTF::Vertex::getPipelineVertexInputState({
        vkglTF::VertexComponent::Position,
        vkglTF::VertexComponent::Normal,
        vkglTF::VertexComponent::UV
    });
```

这里传入的顺序决定 shader location：

```text
Position -> location 0
Normal   -> location 1
UV       -> location 2
```

HLSL 要对应：

```hlsl
struct VSInput
{
    [[vk::location(0)]] float3 Pos    : POSITION0;
    [[vk::location(1)]] float3 Normal : NORMAL0;
    [[vk::location(2)]] float2 UV     : TEXCOORD0;
};
```

如果你传的是：

```cpp
{
    vkglTF::VertexComponent::Position,
    vkglTF::VertexComponent::UV,
    vkglTF::VertexComponent::Color,
    vkglTF::VertexComponent::Normal
}
```

那 HLSL 就必须是：

```hlsl
[[vk::location(0)]] float3 Pos    : POSITION0;
[[vk::location(1)]] float2 UV     : TEXCOORD0;
[[vk::location(2)]] float4 Color  : COLOR0;
[[vk::location(3)]] float3 Normal : NORMAL0;
```

这里最容易犯错。Vulkan 真正在意的是 `[[vk::location(n)]]`，语义名 `POSITION0 / NORMAL0` 更多是给 HLSL 前端看的。

## 7. draw() 的三种常见调用

### 7.1 只画几何，不绑定材质贴图

```cpp
model.draw(commandBuffer);
```

适合：

```text
shadow depth pass
纯 vertex color pass
自己统一绑定材质/纹理的 pass
```

这会自动：

```text
绑定 vertex buffer
绑定 index buffer
递归遍历 node
对每个 primitive 调用 vkCmdDrawIndexed()
```

### 7.2 绘制时自动绑定 glTF 材质贴图

```cpp
model.draw(
    commandBuffer,
    vkglTF::RenderFlags::BindImages,
    pipelineLayout);
```

这会在每个 primitive 绘制前自动：

```cpp
vkCmdBindDescriptorSets(
    commandBuffer,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    pipelineLayout,
    bindImageSet,
    1,
    &material.descriptorSet,
    0,
    nullptr);
```

`bindImageSet` 默认是 `1`。

也就是说你的 pipeline layout 要有：

```text
set 0: 你自己的 scene UBO / camera / light
set 1: vkglTF::descriptorSetLayoutImage
```

创建 pipeline layout 时这样写：

```cpp
std::array<VkDescriptorSetLayout, 2> setLayouts = {
    sceneDescriptorSetLayout,
    vkglTF::descriptorSetLayoutImage
};

VkPipelineLayoutCreateInfo pipelineLayoutCI =
    vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));
```

然后 fragment shader 里可以按 glTF 材质 layout 采样 base color：

```hlsl
Texture2D baseColorTexture : register(t0, space1);
SamplerState baseColorSampler : register(s0, space1);
```

注意：如果你自己的 HLSL 不使用 `space1`，也可以沿用项目 shader 写法，但概念上这个材质贴图是 Vulkan descriptor set 1。

### 7.3 指定材质贴图绑定到别的 set

```cpp
model.draw(
    commandBuffer,
    vkglTF::RenderFlags::BindImages,
    pipelineLayout,
    2);
```

这表示材质 descriptor 会绑定到 set 2。

适合你的 pipeline layout 已经安排成：

```text
set 0: scene UBO
set 1: shadow map / GBuffer / pass resources
set 2: glTF material images
```

第一版不建议搞太复杂。Lab1 可以先不绑定 glTF 材质贴图，只用 vertex color 或统一颜色。

## 8. glTF 材质 descriptor 是怎么来的

`loadFromFile()` 末尾会创建模型自己的 descriptor pool 和 descriptor sets。

它会创建全局静态 layout：

```cpp
vkglTF::descriptorSetLayoutImage
```

默认只绑定 base color：

```cpp
vkglTF::descriptorBindingFlags = vkglTF::DescriptorBindingFlags::ImageBaseColor;
```

如果你要同时使用 base color 和 normal map，需要在加载模型前设置：

```cpp
vkglTF::descriptorBindingFlags =
    vkglTF::DescriptorBindingFlags::ImageBaseColor |
    vkglTF::DescriptorBindingFlags::ImageNormalMap;
```

然后再调用：

```cpp
model.loadFromFile(...);
```

顺序很重要：`descriptorBindingFlags` 要在 `loadFromFile()` 之前设置，因为 layout 和 material descriptor set 会在加载时创建。

## 9. 在 Shadow Mapping 里怎么用

你的 `Lab1ShadowMap` 建议分两种 pass 使用模型：

### 9.1 Shadow depth pass

目标：只写 shadow map depth，不需要材质贴图。

```cpp
vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPipeline);
vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout, 0, 1, &shadowDescriptorSet, 0, nullptr);

scene.draw(commandBuffer);
```

pipeline vertex input 可以只需要 position：

```cpp
pipelineCI.pVertexInputState =
    vkglTF::Vertex::getPipelineVertexInputState({
        vkglTF::VertexComponent::Position
    });
```

HLSL：

```hlsl
struct VSInput
{
    [[vk::location(0)]] float3 Pos : POSITION0;
};
```

### 9.2 Scene pass

目标：正常从 camera 视角渲染，并采样 shadow map。

第一版建议不要急着绑定 glTF material images，先用 vertex color：

```cpp
pipelineCI.pVertexInputState =
    vkglTF::Vertex::getPipelineVertexInputState({
        vkglTF::VertexComponent::Position,
        vkglTF::VertexComponent::Normal,
        vkglTF::VertexComponent::Color
    });
```

绘制：

```cpp
vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipeline);
vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout, 0, 1, &sceneDescriptorSet, 0, nullptr);

scene.draw(commandBuffer);
```

等 hard shadow / PCF 跑通后，再升级为：

```cpp
scene.draw(commandBuffer, vkglTF::RenderFlags::BindImages, scenePipelineLayout);
```

这时 pipeline layout 才需要包含 `vkglTF::descriptorSetLayoutImage`。

## 10. 在 dynamic rendering 里使用

模型加载器和 dynamic rendering 没冲突。

`examples/dynamicrendering/dynamicrendering.cpp` 就是很好的参考：

```text
loadAssets()
-> model.loadFromFile(...)

preparePipelines()
-> pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState(...)
-> pipeline layout 包含 vkglTF::descriptorSetLayoutImage

buildCommandBuffer()
-> vkCmdBeginRenderingKHR(...)
-> vkCmdBindDescriptorSets(set 0)
-> vkCmdBindPipeline(...)
-> model.draw(cmdBuffer, vkglTF::RenderFlags::BindImages, pipelineLayout)
-> vkCmdEndRenderingKHR(...)
```

你自己的 Lab0/Lab1 是 Vulkan 1.3 core dynamic rendering，写法会稍微不同，但概念一样：

```text
dynamic rendering 决定“画到哪个 attachment”
vkglTF::Model 决定“画哪些 vertex/index”
descriptor set 决定“shader 读哪些 UBO / texture”
```

这三件事不要混在一起看。

## 11. 生命周期和清理

`vkglTF::Model` 析构时会清理：

```text
vertex/index buffer
texture image/sampler/view
descriptor pool
nodes/materials/animations
```

所以如果它是 `VulkanExample` 的成员变量，通常不需要你手动 destroy。

但要注意：

```text
Model 必须在 device 仍然有效时析构
不要在 vkDestroyDevice 之后才释放 Model
不要把 Model 放到比 VulkanExample 更长寿的全局对象里
```

原项目 sample 通常把 `vkglTF::Model` 作为 example 类成员，这个方式可以沿用。

## 12. 常见坑

### 12.1 HLSL location 和 VertexComponent 顺序不一致

症状：

```text
模型乱飞
法线错误
UV 错位
颜色异常
```

检查：

```text
getPipelineVertexInputState({ ... }) 的顺序
HLSL [[vk::location(n)]] 的顺序
```

### 12.2 使用 BindImages 但 pipeline layout 没有 set 1

症状：

```text
validation layer 报 descriptor set layout 不匹配
或者绘制时崩溃
```

解决：

```cpp
std::array<VkDescriptorSetLayout, 2> setLayouts = {
    yourSceneLayout,
    vkglTF::descriptorSetLayoutImage
};
```

### 12.3 开了 DontLoadImages 还调用 BindImages

症状：

```text
材质 descriptor 没创建
贴图绑定为空
```

解决：

```text
depth-only pass 不要 BindImages
需要贴图的 scene pass 不要 DontLoadImages
```

### 12.4 模型上下颠倒或朝向奇怪

优先尝试：

```cpp
vkglTF::FileLoadingFlags::FlipY
```

如果 UV 上下颠倒，再试：

```cpp
vkglTF::FileLoadingFlags::FlipUV
```

### 12.5 阴影实验里模型太复杂

Shadow Mapping 第一版不要直接上 Sponza。

推荐顺序：

```text
cube.gltf
plane.gltf + cube.gltf
vulkanscene_shadow.gltf
samplescene.gltf
sponza/sponza.gltf
```

先用简单模型把 depth pass、debug shadow map、hard shadow、PCF 跑通，再换大场景。

## 13. 给 Lab1ShadowMap 的建议

明天如果你要开始 Lab1，我建议这样接入模型：

```text
第一阶段：
    使用 vkglTF::Model 加载 cube.gltf 或 vulkanscene_shadow.gltf
    不加载材质贴图，先只做 geometry + depth
    shadow pass: model.draw(commandBuffer)
    debug pass: 显示 shadow map

第二阶段：
    scene pass 使用 Position + Normal + Color
    做 hard shadow
    仍然不绑定 glTF 贴图

第三阶段：
    打开材质贴图
    pipeline layout 加 vkglTF::descriptorSetLayoutImage
    scene.draw(commandBuffer, vkglTF::RenderFlags::BindImages, pipelineLayout)

第四阶段：
    再考虑 normal map、PBR、GBuffer
```

这条路线会比较稳：先把阴影数据流跑通，再逐步把模型材质接进来。模型加载器是帮你省掉“造几何”的工具，不要让它一开始就把 descriptor、材质、PBR 全部卷进来。图形学习里，怪物要一只一只打，别一次开团。

