# Step 4：给 Lab0 加 Texture，打通 Image / Sampler / Descriptor / HLSL 采样

你已经完成了：

```text
Step 1 -> GPU Mesh Buffer + VMA
Step 2 -> UniformBuffer 迁移到 VMA
Step 3 -> Push Constant
```

现在可以进入 Step 4：纹理。

这一轮的目标是：

```text
给圆盘增加 UV
加载一张 KTX 纹理
descriptor 从 UBO 扩展到 UBO + Combined Image Sampler
HLSL fragment shader 采样纹理
继续保留 push constant，用它控制纹理颜色倍率
```

这一步非常关键，因为从 Games202 视角看，后面所有重要实验几乎都会用到 image：

```text
shadow map
G-buffer
SSAO texture
depth texture
normal texture
IBL cubemap
HDR color buffer
bloom ping-pong texture
```

所以 Step 4 不只是“给圆盘贴图”，而是在学习 Vulkan 图形算法最核心的一条资源链：

```text
Image -> ImageView -> Sampler -> Descriptor -> Shader Sample
```

## 1. 先读哪些 sample

建议先读：

```text
examples/texture/texture.cpp
shaders/hlsl/texture/texture.vert
shaders/hlsl/texture/texture.frag
base/VulkanTexture.h
base/VulkanTexture.cpp
```

阅读时不要急着理解 `texture.cpp` 的所有细节。

先抓这几件事：

```text
1. texture 怎么加载
2. descriptor pool 怎么增加 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
3. descriptor set layout 怎么增加 binding 1
4. VkDescriptorImageInfo 里有哪些字段
5. shader 里 Texture2D / SamplerState 怎么声明
```

`examples/texture/texture.cpp` 的重点位置：

```text
loadTexture()
-> 创建 image、image view、sampler，并填好 texture.descriptor

setupDescriptors()
-> binding 0 是 UBO
-> binding 1 是 combined image sampler

buildCommandBuffers()
-> bind descriptor set 后 draw
```

你这一轮先借用项目已有的：

```cpp
vks::Texture2D
```

不要急着自己手写完整 image upload。

原因是：

```text
Texture upload 涉及 ktx 解析、staging buffer、VkImage、layout transition、mipmap、sampler、image view
一次全手写会太大
```

本轮先把“使用纹理”的链路跑通。
下一轮再自己抽 `AllocatedImage` / `vk_images`，会更稳。

## 2. 本轮建议使用哪张纹理

仓库里有现成 KTX：

```text
assets/textures/metalplate01_rgba.ktx
assets/textures/vulkan_11_rgba.ktx
assets/textures/gridlines.ktx
assets/textures/rocks_color_rgba.ktx
```

建议先用：

```text
assets/textures/metalplate01_rgba.ktx
```

路径在 C++ 里写：

```cpp
getAssetPath() + "textures/metalplate01_rgba.ktx"
```

格式用：

```cpp
VK_FORMAT_R8G8B8A8_UNORM
```

如果你想更容易看清 UV，可以用：

```text
assets/textures/gridlines.ktx
```

但第一版用 `metalplate01_rgba.ktx` 就很好。

## 3. 第一步：修改 Vertex，增加 UV

当前 `Vertex` 大概是：

```cpp
struct Vertex {
    float position[3];
    float color[3];
};
```

改成：

```cpp
struct Vertex {
    float position[3];
    float color[3];
    float uv[2];
};
```

然后修改 `createCircleMesh()`。

中心点：

```cpp
mesh.vertices.push_back({
    { 0.0f, 0.0f, 0.0f },
    { 1.0f, 1.0f, 1.0f },
    { 0.5f, 0.5f }
});
```

圆环顶点：

```cpp
float u = x / (2.0f * radius) + 0.5f;
float v = y / (2.0f * radius) + 0.5f;

mesh.vertices.push_back({
    { x, y, 0.0f },
    { t, 1.0f - t, 0.33f },
    { u, v }
});
```

这里的 UV 映射逻辑是：

```text
x = -radius -> u = 0
x =  radius -> u = 1
y = -radius -> v = 0
y =  radius -> v = 1
```

圆盘中心就是：

```text
uv = (0.5, 0.5)
```

这是一种最简单的平面投影。

## 4. 第二步：修改 pipeline vertex input

当前 vertex input 只有两个 attribute：

```text
location 0 -> position
location 1 -> color
```

现在增加：

```text
location 2 -> uv
```

把 attribute 数组从：

```cpp
std::array<VkVertexInputAttributeDescription, 2> vertexInputAttributs{};
```

改成：

```cpp
std::array<VkVertexInputAttributeDescription, 3> vertexInputAttributs{};
```

新增：

```cpp
vertexInputAttributs[2].binding = 0;
vertexInputAttributs[2].location = 2;
vertexInputAttributs[2].format = VK_FORMAT_R32G32_SFLOAT;
vertexInputAttributs[2].offset = offsetof(Vertex, uv);
```

并且：

```cpp
vertexInputStateCI.vertexAttributeDescriptionCount = 3;
```

这一段必须和 HLSL 对齐：

```hlsl
[[vk::location(0)]] float3 Pos
[[vk::location(1)]] float3 Color
[[vk::location(2)]] float2 UV
```

如果 location 或 format 不一致，轻则画面错，重则 validation 报错。

## 5. 第三步：修改 HLSL vertex shader

修改：

```text
shaders/hlsl/lab0/lab0.vert
```

输入增加 UV：

```hlsl
struct VSInput
{
    [[vk::location(0)]] float3 Pos : POSITION0;
    [[vk::location(1)]] float3 Color : COLOR0;
    [[vk::location(2)]] float2 UV : TEXCOORD0;
};
```

输出增加 UV：

```hlsl
struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float3 Color : COLOR0;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
};
```

main 里传出去：

```hlsl
output.Color = input.Color;
output.UV = input.UV;
```

位置计算仍然保持：

```hlsl
output.Pos = mul(ubo.projectionMatrix,
             mul(ubo.viewMatrix,
             mul(ubo.modelMatrix, float4(input.Pos.xyz, 1.0))));
```

这一阶段不要动矩阵逻辑。

## 6. 第四步：修改 HLSL fragment shader

修改：

```text
shaders/hlsl/lab0/lab0.frag
```

建议写成：

```hlsl
struct PushConstants {
    float4 colorMultiplier;
};

[[vk::push_constant]]
PushConstants pushConstants;

Texture2D baseColorTexture : register(t1);
SamplerState baseColorSampler : register(s1);

struct PSInput
{
    [[vk::location(0)]] float3 Color : COLOR0;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 texColor = baseColorTexture.Sample(baseColorSampler, input.UV);
    float3 finalColor = texColor.rgb * input.Color * pushConstants.colorMultiplier.rgb;
    return float4(finalColor, texColor.a);
}
```

这里几个点要记住：

```text
Texture2D register(t1)
SamplerState register(s1)
-> 对应 C++ descriptor binding 1

PSInput location 0
-> 来自 vertex shader output Color

PSInput location 1
-> 来自 vertex shader output UV
```

你可能会觉得奇怪：为什么一个 combined image sampler 在 HLSL 里看起来像 Texture2D + SamplerState 两个对象？

这是 HLSL/DXC 到 SPIR-V 的映射习惯。
这个项目的 `examples/texture` 也是这样写的：

```hlsl
Texture2D textureColor : register(t1);
SamplerState samplerColor : register(s1);
```

C++ 侧仍然用：

```cpp
VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
```

## 7. 第五步：在 Lab0 里加入 Texture2D 成员

在 `lab0.h` include：

```cpp
#include "VulkanTexture.h"
```

然后在 `VulkanExample` 成员里加：

```cpp
vks::Texture2D colorTexture;
```

再声明：

```cpp
void loadTexture();
```

建议放在：

```cpp
void createVertexBuffer();
void createUniformBuffers();
```

附近。

## 8. 第六步：实现 loadTexture

在 `lab0.cpp` 里新增：

```cpp
void VulkanExample::loadTexture() {
    colorTexture.loadFromFile(
        getAssetPath() + "textures/metalplate01_rgba.ktx",
        VK_FORMAT_R8G8B8A8_UNORM,
        vulkanDevice,
        queue);
}
```

这里用到的是项目已有 helper：

```cpp
vks::Texture2D::loadFromFile()
```

它内部会帮你做：

```text
读取 KTX
创建 staging buffer
创建 VkImage
copy buffer -> image
layout transition
创建 sampler
创建 image view
填充 descriptor
```

这一轮我们先使用它。

你需要在析构里销毁：

```cpp
colorTexture.destroy();
```

建议放在：

```cpp
vkDestroyDescriptorSetLayout(...)
```

附近，确保 device 还有效。

## 9. 第七步：调整 prepare 顺序

当前顺序大概是：

```cpp
createVmaAllocator();
createSynchronizationPrimitives();
createCommandBuffers();
createVertexBuffer();
createUniformBuffers();
createDescriptors();
createPipeline();
```

加入 texture 后建议：

```cpp
createVmaAllocator();
createSynchronizationPrimitives();
createCommandBuffers();
createVertexBuffer();
createUniformBuffers();
loadTexture();
createDescriptors();
createPipeline();
```

为什么 `loadTexture()` 要在 `createDescriptors()` 前？

因为 descriptor 写入时要用：

```cpp
colorTexture.descriptor
```

这个 descriptor 必须在 texture 加载后才有效。

## 10. 第八步：修改 descriptor pool

当前 descriptor pool 只有 UBO：

```cpp
VkDescriptorPoolSize descriptorTypeCounts[1]{};
descriptorTypeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
descriptorTypeCounts[0].descriptorCount = MAX_CONCURRENT_FRAMES;
```

改成两个：

```cpp
VkDescriptorPoolSize descriptorTypeCounts[2]{};

descriptorTypeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
descriptorTypeCounts[0].descriptorCount = MAX_CONCURRENT_FRAMES;

descriptorTypeCounts[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
descriptorTypeCounts[1].descriptorCount = MAX_CONCURRENT_FRAMES;
```

并且：

```cpp
descriptorPoolCI.poolSizeCount = 2;
```

这里为什么 image sampler 也要 `MAX_CONCURRENT_FRAMES`？

因为你给每个 frame 分配了一个 descriptor set。
每个 descriptor set 都需要一个 binding 1。

虽然它们都指向同一张 texture，但 descriptor 数量仍然是每 set 一个。

## 11. 第九步：修改 descriptor set layout

当前 layout 只有一个 binding：

```cpp
VkDescriptorSetLayoutBinding layoutBinding{};
layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
layoutBinding.descriptorCount = 1;
layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
layoutBinding.binding = 0;
```

改成两个 binding：

```cpp
std::array<VkDescriptorSetLayoutBinding, 2> layoutBindings{};

layoutBindings[0].binding = 0;
layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
layoutBindings[0].descriptorCount = 1;
layoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

layoutBindings[1].binding = 1;
layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
layoutBindings[1].descriptorCount = 1;
layoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
```

然后：

```cpp
VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
descriptorLayoutCI.bindingCount = static_cast<uint32_t>(layoutBindings.size());
descriptorLayoutCI.pBindings = layoutBindings.data();
VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayout));
```

这里和 HLSL 对应：

```text
binding 0
-> cbuffer ubo : register(b0)

binding 1
-> Texture2D baseColorTexture : register(t1)
-> SamplerState baseColorSampler : register(s1)
```

## 12. 第十步：修改 descriptor write

当前每个 frame 只写 UBO：

```cpp
VkWriteDescriptorSet writeDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
...
vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
```

改成写两个 descriptor：

```cpp
VkDescriptorBufferInfo bufferInfo{};
bufferInfo.buffer = uniformBuffersV2[i].buffer.handle;
bufferInfo.offset = 0;
bufferInfo.range = sizeof(ShaderData);

VkDescriptorImageInfo imageInfo = colorTexture.descriptor;

std::array<VkWriteDescriptorSet, 2> writeDescriptorSets{};

writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
writeDescriptorSets[0].dstSet = uniformBuffersV2[i].descriptorSet;
writeDescriptorSets[0].dstBinding = 0;
writeDescriptorSets[0].descriptorCount = 1;
writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
writeDescriptorSets[0].pBufferInfo = &bufferInfo;

writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
writeDescriptorSets[1].dstSet = uniformBuffersV2[i].descriptorSet;
writeDescriptorSets[1].dstBinding = 1;
writeDescriptorSets[1].descriptorCount = 1;
writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
writeDescriptorSets[1].pImageInfo = &imageInfo;

vkUpdateDescriptorSets(
    device,
    static_cast<uint32_t>(writeDescriptorSets.size()),
    writeDescriptorSets.data(),
    0,
    nullptr);
```

注意：

```text
pBufferInfo 用于 UBO
pImageInfo 用于 texture/sampler
```

不要混。

## 13. 第十一步：重新编译 HLSL shader

改完 shader 后运行：

```powershell
Push-Location shaders/hlsl
python compileshaders.py --sample lab0
Pop-Location
```

应该生成或更新：

```text
shaders/hlsl/lab0/lab0.vert.spv
shaders/hlsl/lab0/lab0.frag.spv
```

如果 shader 编译报错，优先检查：

```text
HLSL register 是否写对
VSOutput/PSInput location 是否对齐
Texture2D / SamplerState 是否在全局声明
```

## 14. 第十二步：编译和运行

编译：

```powershell
cmake --build build --config Debug --target lab0 -j 32
```

运行：

```powershell
build\bin\Debug\lab0.exe -v -vl
```

成功标准：

```text
圆盘仍然正常显示
颜色能看到纹理变化
push constant 仍然能改变整体颜色倍率
validation layer 没有 descriptor/layout 相关错误
```

如果画面变黑：

```text
检查 loadTexture() 是否在 createDescriptors() 前
检查 descriptor binding 1 是否写入了 colorTexture.descriptor
检查 shader 是否真的加载 lab0/lab0.frag.spv
检查 HLSL 是否使用 binding/register 1
```

如果 validation 报 descriptor set layout 不匹配：

```text
检查 C++ descriptor set layout binding 1
检查 fragment shader Texture2D/SamplerState register
检查 descriptor type 是否是 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
```

如果图案方向不符合预期：

```text
可以先不管
UV 的上下方向和纹理坐标习惯有关
之后做 texture 专项时再统一处理
```

## 15. 本轮你应该理解的数据流

完成后，你应该能说清楚：

```text
CPU 侧 KTX 文件
-> vks::Texture2D loadFromFile
-> VkImage
-> VkImageView
-> VkSampler
-> VkDescriptorImageInfo
-> descriptor set binding 1
-> fragment shader Texture2D.Sample
-> 输出最终颜色
```

以及 vertex 侧：

```text
Vertex.uv
-> VkVertexInputAttributeDescription location 2
-> HLSL VSInput UV
-> HLSL VSOutput UV
-> HLSL PSInput UV
-> texture sample coordinate
```

这两条线合起来，就是最基础的 texture mapping。

## 16. 为什么这轮先不用 VMA 自己写 image

你可能会问：

```text
既然我已经引入 VMA，为什么 texture 不直接用 VMA image？
```

答案是：可以，但不建议在 Step 4 一起做。

因为这里其实有两个独立知识点：

```text
如何使用纹理
-> UV / sampler / descriptor / shader sample

如何创建 image
-> VkImageCreateInfo / VmaAllocation / layout transition / copyBufferToImage / mipmap
```

如果一次全做，很容易调试爆炸。

所以 Step 4 的策略是：

```text
先用项目自带 Texture2D 跑通“使用纹理”
再在 Step 5 自己实现 AllocatedImage 和 vk_images helper
```

这不是偷懒，是把学习曲线拆开。

## 17. Step 4 完成后的下一步

Step 4 跑通后，下一轮建议做：

```text
Step 5：抽出 AllocatedImage 和 vk_images helper
```

目标是用 VMA 自己管理 image：

```cpp
struct AllocatedImage {
    VkImage image{ VK_NULL_HANDLE };
    VkImageView imageView{ VK_NULL_HANDLE };
    VmaAllocation allocation{ VK_NULL_HANDLE };
    VkExtent3D extent{};
    VkFormat format{ VK_FORMAT_UNDEFINED };
};
```

然后逐步实现：

```cpp
createAllocatedImage()
destroyAllocatedImage()
transitionImage()
copyBufferToImage()
```

那一轮会真正把 image creation 纳入你自己的工具层。

但现在先完成 Step 4：

```text
让圆盘真正采样一张 texture。
```

