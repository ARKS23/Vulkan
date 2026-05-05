# Lab0 进阶路线：从能跑到能改、能封装

这份文档接在你的 `Lab0` 圆盘 sample 后面。

你现在已经完成了一个非常关键的转折：不是只读别人的 Vulkan sample，而是已经能把自己的 example 跑起来，并且把三角形改成了圆盘网格。接下来要做的不是一口气堆复杂效果，而是把这份代码慢慢拆成你能长期维护的“小型图形实验底座”。

建议你把接下来几轮目标理解成：

```text
观察变化
-> 拆分职责
-> 抽通用 helper
-> 管理资源生命周期
-> 接入 VMA
-> 为 Games202 实验准备 pass / resource 结构
```

## 1. 你当前 Lab0 已经说明了什么

你把三角形改成圆盘后，应该先意识到一件事：

```text
变化的是：
- 顶点数据
- 索引数据
- indexCount

基本不变的是：
- pipeline
- descriptor
- UBO
- shader input layout
- dynamic rendering 流程
- command buffer 录制和提交流程
```

这就是 Vulkan 学习里很重要的一层抽象：

- 几何是什么，主要由 CPU 侧 mesh 数据和 vertex/index buffer 决定。
- shader 如何解释几何，主要由 vertex input layout 和 shader location 决定。
- 一帧如何提交，主要由 swapchain、command buffer、sync、barrier、rendering info 决定。

以后你做更复杂实验时，不要把所有变化都混在一起看。先问自己：我现在改的是数据、shader、pipeline 状态，还是一帧调度？

## 2. 下一步最值得做的 5 个小实验

先不要急着封装。先做几个很小但很有观察价值的实验。

### 2.1 改圆盘分段数

你现在的圆盘点是硬编码的 16 段。下一步先手动改成：

```text
3 / 4 / 8 / 16 / 64
```

观察：

- `segmentCount = 3` 时，它本质上还是三角形。
- `segmentCount = 4` 时，它是菱形或方形圆盘。
- `segmentCount = 64` 时，它才接近圆。

这个实验要你记住：GPU 没有“圆形图元”，你画出来的圆盘，本质上还是很多三角形。

### 2.2 改圆心和圆周颜色

先让中心点是白色，圆周点按角度变化颜色。

观察：

- fragment shader 没有做复杂逻辑。
- 颜色渐变来自顶点颜色插值。
- pipeline 不需要改。

这会帮你理解 rasterization 阶段做了什么：它会在三角形内部插值 vertex shader 输出。

### 2.3 每帧更新 model matrix

在 `render()` 里把：

```cpp
shaderData.modelMatrix = glm::mat4(1.0f);
```

改成基于时间旋转。

观察：

- vertex buffer 不变。
- pipeline 不变。
- descriptor 不变。
- 每帧变化的是 UBO 里的矩阵。

这一步能把“静态资源”和“每帧数据”分开。

### 2.4 改 clear color

在 `render()` 里改：

```cpp
colorAttachment.clearValue.color
```

观察：

- 这是 attachment clear 行为。
- 它不属于 shader。
- 它发生在 dynamic rendering 的 attachment 配置里。

这能帮你区分：背景色不是 fragment shader 画出来的。

### 2.5 暂时关掉 depth test

在 `createPipeline()` 里把：

```cpp
depthStencilStateCI.depthTestEnable = VK_FALSE;
depthStencilStateCI.depthWriteEnable = VK_FALSE;
```

观察：

- 当前 2D 圆盘通常不会有明显变化。
- 但你会知道 depth state 是 pipeline 固定状态的一部分。

这个实验以后做 shadow、deferred、SSAO 时会变得很重要。

## 3. 第一轮重构：把 mesh 生成拆出来

现在 `createVertexBuffer()` 同时做了两件事：

1. 生成圆盘顶点和索引。
2. 创建 GPU buffer 并上传数据。

这两个职责应该拆开。

建议先在 `lab0.h` 里加两个小类型：

```cpp
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
```

然后在 `lab0.cpp` 里写：

```cpp
MeshData VulkanExample::createCircleMesh(float radius, uint32_t segmentCount)
{
    MeshData mesh{};
    mesh.vertices.push_back({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });

    for (uint32_t i = 0; i < segmentCount; i++) {
        float angle = 2.0f * glm::pi<float>() * static_cast<float>(i) / static_cast<float>(segmentCount);
        float x = radius * std::cos(angle);
        float y = radius * std::sin(angle);

        float t = static_cast<float>(i) / static_cast<float>(segmentCount);
        mesh.vertices.push_back({ { x, y, 0.0f }, { t, 1.0f - t, 0.5f } });
    }

    for (uint32_t i = 0; i < segmentCount; i++) {
        uint32_t current = i + 1;
        uint32_t next = (i + 1) % segmentCount + 1;
        mesh.indices.push_back(0);
        mesh.indices.push_back(current);
        mesh.indices.push_back(next);
    }

    return mesh;
}
```

这样 `createVertexBuffer()` 的前半段就会变成：

```cpp
MeshData circle = createCircleMesh(0.8f, 64);

const std::vector<Vertex>& vertices = circle.vertices;
const std::vector<uint32_t>& indices = circle.indices;
```

这一轮重构的目标不是减少多少代码，而是建立第一个清晰边界：

```text
CPU mesh generation != Vulkan buffer upload
```

## 4. 第二轮重构：把 GPU mesh buffer 拆出来

现在你有：

```cpp
VulkanBuffer vertexBuffer;
VulkanBuffer indexBuffer;
uint32_t indexCount;
```

可以把它整理成一个更像图形项目的结构：

```cpp
struct GPUMesh {
    VulkanBuffer vertexBuffer;
    VulkanBuffer indexBuffer;
    uint32_t indexCount{ 0 };
};
```

然后 `VulkanExample` 里只保留：

```cpp
GPUMesh circleMesh;
```

未来你画多个物体时，就会自然变成：

```cpp
GPUMesh circle;
GPUMesh quad;
GPUMesh sphere;
```

这一步能帮你从“全局变量式 sample”过渡到“资源对象式项目”。

## 5. 第三轮重构：抽 staging upload

这是最值得做的 Vulkan helper。

你现在 `createVertexBuffer()` 里有一大段固定流程：

```text
创建 staging buffer
map
memcpy
创建 device local buffer
录 vkCmdCopyBuffer
提交并等待
销毁 staging buffer
```

这段以后会反复出现：

- 上传 mesh vertex buffer
- 上传 mesh index buffer
- 上传 texture 像素数据
- 上传 compute 初始数据

所以建议抽成：

```cpp
VulkanBuffer VulkanExample::createDeviceLocalBuffer(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage);
```

调用时：

```cpp
circleMesh.vertexBuffer = createDeviceLocalBuffer(
    vertices.data(),
    vertexBufferSize,
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

circleMesh.indexBuffer = createDeviceLocalBuffer(
    indices.data(),
    indexBufferSize,
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
```

这个 helper 内部自动补：

```cpp
VK_BUFFER_USAGE_TRANSFER_DST_BIT
```

也就是说，调用者只关心“我要这个 buffer 最终作为 vertex buffer 或 index buffer 使用”，不需要每次都记得 staging 细节。

## 6. 第四轮重构：抽 immediate submit

上传 buffer 时，你现在手动做了：

```text
分配临时 command buffer
begin
record copy
end
创建 fence
queue submit
wait fence
destroy fence
free command buffer
```

这也很适合抽出来：

```cpp
void VulkanExample::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
```

用法会变成：

```cpp
immediateSubmit([&](VkCommandBuffer cmd) {
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, stagingBuffer.handle, dstBuffer.handle, 1, &copyRegion);
});
```

这一步特别像 `vk-guide` 的风格，而且很实用。

注意：第一次写可以先继续复用当前 `commandPool` 和 `queue`。等项目变复杂后，再单独做一个 upload command pool。

## 7. 第五轮重构：资源生命周期账本

你现在析构函数里手动销毁：

```cpp
vkDestroyPipeline
vkDestroyPipelineLayout
vkDestroyDescriptorSetLayout
vkDestroyBuffer
vkFreeMemory
...
```

这在 sample 里可以，但随着资源变多，会越来越难维护。

建议下一步做一个简单的 `DeletionQueue`：

```cpp
struct DeletionQueue {
    std::deque<std::function<void()>> deletors;

    void pushFunction(std::function<void()>&& function) {
        deletors.push_back(std::move(function));
    }

    void flush() {
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
            (*it)();
        }
        deletors.clear();
    }
};
```

资源创建成功后顺手登记：

```cpp
mainDeletionQueue.pushFunction([=]() {
    vkDestroyBuffer(device, buffer.handle, nullptr);
    vkFreeMemory(device, buffer.memory, nullptr);
});
```

这样你会更清楚地理解 Vulkan 的资源所有权：

```text
谁创建
谁持有
谁销毁
什么时候销毁
```

## 8. 第六轮重构：把 create info helper 补完整

你已经开始写 `vk_initializers.h/.cpp`，这是很好的方向。

下一批可以补这些：

```cpp
VkBufferCreateInfo bufferCreateInfo(VkDeviceSize size, VkBufferUsageFlags usage);
VkMemoryAllocateInfo memoryAllocateInfo(VkDeviceSize size, uint32_t memoryTypeIndex);
VkCommandBufferAllocateInfo commandBufferAllocateInfo(VkCommandPool pool, uint32_t count);
VkCommandBufferBeginInfo commandBufferBeginInfo(VkCommandBufferUsageFlags flags = 0);
VkSubmitInfo submitInfo(VkCommandBuffer* cmd);
VkRenderingAttachmentInfo renderingAttachmentInfo(...);
VkRenderingInfo renderingInfo(...);
```

但这里有个节奏建议：

- 先抽你已经写过 2 次以上的 create info。
- 不要为了“看起来像框架”一次性包完所有 Vulkan 结构体。

好的 helper 应该来自重复痛点，而不是来自想象。

## 9. VMA 应该什么时候接

我建议你在完成下面两个 helper 之后再接 VMA：

```text
createDeviceLocalBuffer()
immediateSubmit()
```

原因是：VMA 主要解决的是 buffer/image 的内存分配问题，而不是帮你理解 command buffer、descriptor、pipeline。

最稳路线是：

```text
第一版：
VulkanBuffer {
    VkBuffer handle;
    VkDeviceMemory memory;
}

第二版：
AllocatedBuffer {
    VkBuffer handle;
    VmaAllocation allocation;
}
```

外部调用尽量保持：

```cpp
createDeviceLocalBuffer(...)
destroyBuffer(...)
```

这样以后从 `vkAllocateMemory` 切到 VMA 时，改的是 helper 内部，不是每个 sample 都到处改。

## 10. 不建议现在立刻封装的东西

### 10.1 不要太早封装 pipeline

Pipeline 确实很长，但它现在对你很有教学价值。

你还需要反复看清楚：

- input assembly
- rasterization
- blend
- viewport/scissor
- depth/stencil
- multisample
- vertex input
- shader stage
- dynamic rendering attachment format

等你写到第二三个 pipeline，再开始做 `PipelineBuilder`。

### 10.2 不要太早改 `VulkanExampleBase`

`VulkanExampleBase` 影响所有 example。

你现在更适合在 `Lab0` 或新增 helper 文件里实验，等接口稳定后再考虑是否进入 `base/` 的通用层。

### 10.3 不要马上上多 pass

`offscreen`、`shadowmapping`、`deferred` 都很诱人，但先别急。

先把单 pass 的数据、上传、同步、descriptor、pipeline 都打磨清楚，之后多 pass 会轻松很多。

## 11. 推荐的文件组织

第一阶段可以先这样：

```text
examples/lab0/
  lab0.h
  lab0.cpp
```

当 helper 稳定后，再考虑放到：

```text
base/
  vk_initializers.h
  vk_initializers.cpp
  vk_types.h
  vk_resources.h
  vk_resources.cpp
  vk_deletion_queue.h
```

建议命名空间：

```cpp
namespace vkinit {}
namespace vkutil {}
```

先不要混进 `vks::`，这样你自己的实验封装和原项目封装边界更清楚。

## 12. 一条非常具体的下一步路线

接下来你可以按这个顺序改：

1. 把硬编码圆盘顶点改成 `createCircleMesh(radius, segmentCount)`。
2. 把 `vertexBuffer/indexBuffer/indexCount` 包进 `GPUMesh`。
3. 把 staging 上传抽成 `createDeviceLocalBuffer()`。
4. 把临时 copy command buffer 流程抽成 `immediateSubmit()`。
5. 加 `DeletionQueue`，让资源创建和销毁更成体系。
6. 给 `Lab0` 添加自己的 HLSL shader 目录，不再复用 `triangle/triangle.vert.spv`。
7. 把 `modelMatrix` 改成每帧旋转，确认 UBO 更新链路。
8. 准备接 VMA，把 `VulkanBuffer` 升级成 `AllocatedBuffer`。

如果你只想选一个马上做，我推荐第 1 步和第 3 步。

它们最能增强理解：

```text
createCircleMesh()
让你理解“几何数据从哪里来”

createDeviceLocalBuffer()
让你理解“数据怎么进 GPU”
```

## 13. 每次改完都问自己的问题

每一轮改造后，停下来问自己：

1. 这次改动属于 CPU 数据生成、GPU 资源创建、shader 接口、pipeline 状态，还是每帧调度？
2. 我有没有改变 vertex input layout？
3. 我有没有改变 descriptor set layout？
4. 我有没有改变 command buffer 录制顺序？
5. 我有没有新增资源？它在哪里销毁？
6. 如果我把 Vulkan 原生内存替换成 VMA，外部调用代码需要改多少？

这些问题比“代码看起来更封装”更重要。

## 14. 最终目标

`Lab0` 的最终形态不需要很大。

它应该变成一个你能随手打开并确认 Vulkan 基础链路的小实验：

```text
CPU mesh generation
-> staging upload
-> GPU vertex/index buffer
-> UBO update
-> descriptor bind
-> dynamic rendering
-> indexed draw
-> present
```

当这条链你完全掌控后，再进入 `texture`、`offscreen`、`shadowmapping`、`ssao`，就不会是“在复杂 sample 里找路”，而是“把新概念接到你已经熟悉的底座上”。
