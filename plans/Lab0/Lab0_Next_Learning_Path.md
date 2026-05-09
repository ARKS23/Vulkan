# Lab0 下一阶段学习路线：从 Vulkan 基础封装走向图形实验室

你现在已经完成了一个很好的阶段成果：

```text
Lab0 能跑
-> 三角形改成圆盘网格
-> 引入 VMA
-> 抽出 AllocatedBuffer / GPUMeshBuffers
-> 抽出 createDeviceLocalBuffer
-> 抽出 immediateSubmit
-> 把 helper 从 sample 成员函数移动到 base 工具层
```

这意味着你已经不再只是“读一个 Vulkan example”，而是在搭一个自己的小型实验框架。

下一阶段的目标不是马上做很复杂的特效，而是把 Vulkan 的核心资源流、数据流、渲染流逐步打通。这样后面做 Games202 风格实验时，你不会每次都被 boilerplate 卡住。

## 1. 接下来最重要的学习主线

我建议你接下来按这四条主线推进：

```text
资源系统
-> buffer / image / sampler / descriptor / pipeline resource lifetime

数据上传
-> CPU mesh、uniform、texture 如何进入 GPU

渲染组织
-> render loop、dynamic rendering、offscreen pass、多 pass 渲染

图形算法
-> shadow、deferred、SSAO、IBL、PBR、post-process
```

不要一开始就追求“大而全的 engine abstraction”。

你现在最应该做的是：

```text
每学一个 Vulkan 概念
-> 在 Lab0 里手写一遍
-> 再抽出最小 helper
-> 再读官方 sample 对照
```

这个节奏会比直接照搬 vk-guide 或引擎结构更扎实。

## 2. 第一阶段：把 Lab0 的资源系统统一到 VMA

你现在 mesh buffer 已经走 VMA 了，但 uniform buffer 还在手动管理：

```cpp
vkCreateBuffer
vkGetBufferMemoryRequirements
vkAllocateMemory
vkBindBufferMemory
vkMapMemory
```

下一步建议先做：

```text
把 UniformBuffer 也迁移成 AllocatedBuffer
```

目标结构可以是：

```cpp
struct UniformBuffer {
    AllocatedBuffer buffer;
    VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
    uint8_t* mapped{ nullptr };
};
```

创建时使用你已经抽出来的：

```cpp
vkutil::createAllocatedBuffer(
    allocator,
    sizeof(ShaderData),
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT,
    VMA_MEMORY_USAGE_AUTO);
```

这样你会真正理解：

```text
VMA 不只是给 vertex/index buffer 用
所有 GPU resource allocation 都应该尽量统一管理
```

这一阶段建议阅读：

```text
examples/dynamicuniformbuffer
examples/descriptorsets
examples/pushconstants
```

阅读重点：

```text
dynamicuniformbuffer
-> 多对象 uniform 数据如何放在一个大 buffer 里

descriptorsets
-> descriptor set layout、descriptor pool、descriptor write 的完整关系

pushconstants
-> 小块 per-draw 数据如何避免频繁改 UBO
```

Lab0 对应练习：

```text
1. 把 UniformBuffer 改成 VMA AllocatedBuffer
2. 新增一个 push constant，控制圆盘颜色或缩放
3. 尝试让圆盘旋转，并把 model matrix 数据流讲清楚
```

完成后你应该能说清楚：

```text
CPU 每帧更新什么数据
数据写到哪块 buffer
descriptor 如何让 shader 找到这块 buffer
draw call 如何使用这份数据
```

## 3. 第二阶段：抽象 Descriptor，但不要过度设计

等 uniform buffer 迁移到 VMA 后，你会发现 Lab0 里 descriptor 相关代码也开始重复：

```text
VkDescriptorSetLayoutBinding
VkDescriptorPoolSize
VkDescriptorSetAllocateInfo
VkWriteDescriptorSet
```

这时可以开始做一个很小的 descriptor helper。

不要一上来写复杂的 descriptor allocator / descriptor writer。

建议先只抽两个函数：

```cpp
VkDescriptorSetLayout createDescriptorSetLayout(
    VkDevice device,
    std::span<const VkDescriptorSetLayoutBinding> bindings);

void writeBufferDescriptor(
    VkDevice device,
    VkDescriptorSet set,
    uint32_t binding,
    VkDescriptorType type,
    const VkDescriptorBufferInfo& bufferInfo);
```

或者放到：

```text
base/vk_descriptors.h
base/vk_descriptors.cpp
```

这一阶段的原则：

```text
先抽“确实重复的 Vulkan CreateInfo 填充”
不要急着抽“未来可能会需要的框架”
```

建议阅读：

```text
examples/descriptorsets
examples/descriptorindexing
examples/descriptorbuffer
```

阅读顺序建议：

```text
先读 descriptorsets
-> 理解传统 descriptor 模型

再粗读 descriptorindexing
-> 理解 bindless / array descriptor 的方向

最后只浏览 descriptorbuffer
-> 这是更现代但更进阶的路线，暂时不需要改 Lab0
```

Lab0 对应练习：

```text
1. 把 descriptor set layout 创建抽成 helper
2. 把 uniform buffer descriptor write 抽成 helper
3. 保持 Lab0 行为不变，确认只是结构变清楚
```

## 4. 第三阶段：学习 Image 和 Texture 上传

Buffer 走通后，真正进入图形实验前必须理解 image。

建议下一轮新增：

```cpp
struct AllocatedImage {
    VkImage image{ VK_NULL_HANDLE };
    VkImageView imageView{ VK_NULL_HANDLE };
    VmaAllocation allocation{ VK_NULL_HANDLE };
    VkExtent3D extent{};
    VkFormat format{ VK_FORMAT_UNDEFINED };
};
```

可以放在：

```text
base/vk_types.h
```

然后新增 helper：

```text
base/vk_images.h
base/vk_images.cpp
```

先实现最小功能：

```cpp
AllocatedImage createAllocatedImage(...);
void destroyAllocatedImage(...);
void transitionImage(...);
void copyBufferToImage(...);
```

这一阶段建议阅读：

```text
examples/texture
examples/texturemipmapgen
examples/texturearray
examples/texturecubemap
examples/sphericalenvmapping
```

阅读顺序建议：

```text
texture
-> 最基础的 image + sampler + descriptor

texturemipmapgen
-> image layout transition 和 blit 的实际用途

texturecubemap
-> 后面 IBL 必备

sphericalenvmapping
-> 环境贴图方向，之后接 PBR/IBL
```

Lab0 对应练习：

```text
1. 给圆盘贴一张简单纹理
2. HLSL shader 增加 uv
3. Vertex 增加 texcoord
4. descriptor 从只绑定 UBO 变成 UBO + combined image sampler
```

完成后你应该能画出这条数据流：

```text
CPU image file
-> staging buffer
-> VkImage
-> image layout transition
-> VkImageView
-> VkSampler
-> descriptor
-> fragment shader sample
```

这条线非常重要。
Games202 后面做 shadow map、IBL、G-buffer、SSAO 都离不开 image。

## 5. 第四阶段：从单 pass 走向 Offscreen / Multi-pass

当你能画带纹理的圆盘后，就可以进入多 pass。

这时优先读：

```text
examples/offscreen
examples/dynamicrendering
examples/deferred
examples/deferredshadows
examples/ssao
```

阅读重点：

```text
offscreen
-> 不画到 swapchain，而是画到自己创建的 image

dynamicrendering
-> Vulkan 1.3 下不依赖传统 render pass/framebuffer 的渲染方式

deferred
-> G-buffer 如何组织多个 render target

deferredshadows
-> shadow pass + lighting pass 如何串起来

ssao
-> 屏幕空间算法需要哪些输入 texture
```

Lab0 对应练习路线：

```text
1. 创建一个 offscreen color image
2. 第一 pass 把圆盘画到 offscreen image
3. 第二 pass 把 offscreen image 画回 swapchain
4. 加一个简单 post-process，比如 grayscale 或 edge color
```

这一步完成后，你会真正理解：

```text
render target 不一定是 swapchain
一个 pass 的输出可以成为下一个 pass 的输入
image layout transition 是 pass 之间的数据契约
```

这是从“画物体”进入“做图形算法”的分界线。

## 6. 第五阶段：开始 Games202 风格实验

等你完成 texture 和 offscreen 后，就可以逐步进入 Games202 方向。

建议实验顺序：

```text
Shadow Mapping
-> examples/shadowmapping
-> examples/shadowmappingomni
-> examples/shadowmappingcascade

Deferred Shading
-> examples/deferred
-> examples/deferredshadows

SSAO
-> examples/ssao

PBR
-> examples/pbrbasic
-> examples/pbrtexture
-> examples/pbribl

HDR / Bloom
-> examples/hdr
-> examples/bloom
```

不要一开始就冲 PBR IBL。

推荐顺序是：

```text
Shadow Mapping
-> 因为它会逼你理解 depth image、offscreen pass、sampler compare、bias

Deferred Shading
-> 因为它会逼你理解多 render target、G-buffer、lighting pass

SSAO
-> 因为它建立在 depth/normal buffer 之上，很适合接 deferred

PBR / IBL
-> 因为它需要 texture、cubemap、BRDF、环境光预计算

HDR / Bloom
-> 因为它需要 floating-point render target 和 post-process chain
```

这条线和 Games202 的思维非常接近：

```text
先掌握 render data 怎么存
再掌握 pass 怎么组织
最后掌握算法怎么消费这些 texture/buffer
```

## 7. 第六阶段：Mesh、Scene 和 glTF

当 Lab0 不再只是一个圆盘，你就需要加载模型。

建议阅读：

```text
examples/vertexattributes
examples/gltfloading
examples/gltfscenerendering
examples/pbrtexture
```

你可以把当前：

```cpp
MeshData
GPUMeshBuffers
```

逐步升级成：

```cpp
struct MeshAsset {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct GPUMesh {
    GPUMeshBuffers buffers;
    uint32_t materialIndex{ 0 };
};
```

但现在先别急着做 scene graph。

建议先完成：

```text
1. 用同一套 upload helper 上传多个 mesh
2. 每个 mesh 有自己的 indexCount
3. render() 里循环 draw 多个 mesh
4. 用 push constant 给每个 mesh 不同 model matrix
```

等你真的需要层级变换、材质系统、节点树时，再去读 glTF scene。

## 8. 第七阶段：Compute Shader

Compute 不一定要很晚学。
但我建议你在 texture/offscreen 后再学，会更顺。

推荐 sample：

```text
examples/computeshader
examples/computeparticles
examples/computenbody
examples/computecloth
```

学习重点：

```text
compute shader 如何绑定 storage buffer / storage image
graphics queue 和 compute queue 的同步
compute 输出如何被 graphics pass 消费
```

Lab0 可做的小练习：

```text
1. 用 compute shader 更新一组点的位置
2. graphics pass 把这些点画出来
3. 用 storage buffer 替代 CPU 每帧更新
```

这对后面做 tiled lighting、clustered shading、GPU culling 都有帮助。

## 9. HLSL 学习建议

既然你后续想主要写 HLSL，建议从现在开始让 Lab0 的新 shader 都走 HLSL。

优先掌握这些写法：

```hlsl
struct VSInput {
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 color : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]]
cbuffer CameraBuffer {
    float4x4 projection;
    float4x4 view;
    float4x4 model;
};

[[vk::binding(1, 0)]]
Texture2D baseColorTexture;

[[vk::binding(2, 0)]]
SamplerState baseColorSampler;
```

建议你之后所有 Lab 都坚持：

```text
shader 用 HLSL
C++ 里明确写 descriptor binding
文档里记录每个 binding 对应什么资源
```

这样你会很快建立 Vulkan descriptor 和 HLSL binding 之间的直觉。

## 10. 现在不要急着做的事情

暂时不建议你立刻做：

```text
完整 engine 架构
复杂 render graph
bindless descriptor 大改造
多线程 command recording
ray tracing
完整 glTF PBR scene renderer
```

这些都很有趣，但现在做容易把学习重心带偏。

你当前最缺的不是“高级架构”，而是：

```text
每一种 Vulkan 资源到底怎么创建
每一种资源如何绑定到 shader
每一个 pass 的输入输出是什么
同步和 layout transition 发生在哪里
```

这些打稳以后，再做 render graph 才会知道自己在抽象什么。

## 11. 建议的具体执行顺序

我建议接下来按这个顺序推进：

```text
Step 1
-> 清理 Lab0 helper 抽离后的代码，确认 vk_commands/vk_resources 职责干净

Step 2
-> 把 UniformBuffer 迁移到 VMA AllocatedBuffer

Step 3
-> 增加 push constant，让圆盘颜色、缩放或 model matrix 可控

Step 4
-> 给圆盘增加 uv，接入一张 texture

Step 5
-> 抽出 AllocatedImage 和 image helper

Step 6
-> 做 offscreen pass，把圆盘先画到 texture 再画回屏幕

Step 7
-> 做 shadow mapping，这是第一个真正的 Games202 风格实验

Step 8
-> 做 deferred shading 和 SSAO

Step 9
-> 进入 PBR / IBL / Bloom
```

这个顺序的好处是：

```text
每一步都有可见结果
每一步都复用上一轮工具
每一步都在为下一类图形算法铺路
```

## 12. 每读一个 sample 时该怎么读

不要从头到尾硬啃。

建议每个 sample 都按这个模板读：

```text
1. 它创建了哪些 GPU resource？
2. CPU 数据怎么上传？
3. descriptor layout 有哪些 binding？
4. pipeline 依赖哪些 shader input？
5. render() 里 command buffer 录了哪些关键命令？
6. 它有没有 offscreen pass？
7. pass 之间 image layout 怎么变？
8. 哪些代码可以搬进 Lab0 工具层？
9. 哪些代码只属于这个 sample，不应该抽？
```

你可以在 `plans/` 里给每个重点 sample 写一个短阅读笔记。

推荐笔记格式：

```text
这个 sample 解决什么问题
关键 Vulkan 对象
数据流
渲染流
我可以搬到 Lab0 的 helper
我暂时不搬的 sample-specific 逻辑
```

这个习惯会非常有用。
因为你不是在“看示例”，你是在给自己的图形实验室积累模块。

## 13. 当前最推荐你马上做的一个任务

下一轮我最推荐你做：

```text
把 UniformBuffer 改造成 VMA AllocatedBuffer
```

理由很简单：

```text
它难度适中
它直接复用你刚抽出的 vk_resources
它会统一 buffer 生命周期
它会帮助你重新理解 descriptor buffer info
它不会引入 image/layout/sampler 这些新复杂度
```

这一步完成后，你的 Lab0 buffer 系统会变得非常干净：

```text
vertex buffer  -> AllocatedBuffer
index buffer   -> AllocatedBuffer
uniform buffer -> AllocatedBuffer
```

然后再进入 texture/image，节奏就很漂亮。

