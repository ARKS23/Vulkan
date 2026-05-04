# Trianglevulkan13.cpp 阅读导读

这份文档是带你阅读 [`examples/trianglevulkan13/trianglevulkan13.cpp`](../examples/trianglevulkan13/trianglevulkan13.cpp) 的导读。

如果说 [`triangle.cpp`](../examples/triangle/triangle.cpp) 的重点是：

- 把传统 Vulkan 1.0 风格的最小图形流程完整展开

那么 `trianglevulkan13.cpp` 的重点就是：

- 保留“最小三角形 sample”的资源流
- 同时演示 Vulkan 1.3 的 **dynamic rendering**
- 让你看到“没有传统 render pass / framebuffer 之后，一帧是怎么组织的”

所以你可以把它理解成：

- `triangle.cpp` 是“经典写法”
- `trianglevulkan13.cpp` 是“更现代一点的写法”

## 建议同时打开的文件

阅读时建议同时开这几个文件：

- [`trianglevulkan13.cpp`](../examples/trianglevulkan13/trianglevulkan13.cpp)
- [`triangle.cpp`](../examples/triangle/triangle.cpp)
- [`vulkanexamplebase.cpp`](../base/vulkanexamplebase.cpp)
- [`vulkanexamplebase.h`](../base/vulkanexamplebase.h)
- [`shaders/glsl/triangle/triangle.vert`](../shaders/glsl/triangle/triangle.vert)
- [`shaders/glsl/triangle/triangle.frag`](../shaders/glsl/triangle/triangle.frag)

这里有一个容易忽略的小点：

- `trianglevulkan13.cpp` **没有自己的独立 shader 目录**
- 它直接复用了 `triangle` 的 shader

也就是说，这个 sample 的重点不是“换一套 shader”，而是“换一套渲染组织方式”。

## 先记住最核心的差异

和 `triangle.cpp` 相比，`trianglevulkan13.cpp` 的最大差异不是顶点、UBO、descriptor，而是：

1. 使用 Vulkan 1.3
2. 开启 `dynamicRendering`
3. 开启 `synchronization2` feature
4. 不再依赖传统 `VkRenderPass` / `VkFramebuffer`
5. 录命令时通过 `vkCmdBeginRendering()` / `vkCmdEndRendering()` 开启渲染段
6. 颜色和深度 attachment 的 layout 转换要显式用 barrier 处理

一句话总结：

- 传统 render pass 路线：很多 attachment/layout 变化由 render pass 帮你隐式处理
- dynamic rendering 路线：render pass 结构变轻了，但一些 layout transition 需要你自己写得更明确

## 最推荐的阅读顺序

建议按这个顺序读：

1. 先看构造函数，理解它为什么叫 `trianglevulkan13`
2. 再看 `prepare()`
3. 再看类成员
4. 再按 `prepare()` 顺序看资源创建函数
5. 最后精读 `render()`
6. 再回头对比 `triangle.cpp`

## 第 1 步：先看构造函数

跳到：

- [`trianglevulkan13.cpp:103`](../examples/trianglevulkan13/trianglevulkan13.cpp)

这段是理解整个 sample 的钥匙。

这里最重要的不是相机设置，而是这几句：

```cpp
apiVersion = VK_API_VERSION_1_3;
enabledFeatures.dynamicRendering = VK_TRUE;
enabledFeatures.synchronization2 = VK_TRUE;
deviceCreatepNextChain = &enabledFeatures;
```

这意味着：

1. 这个 sample 要求 Vulkan 1.3
2. 设备创建时通过 `pNext` 请求 Vulkan 1.3 feature
3. 它明确要走 dynamic rendering 路线

紧接着再看：

- [`trianglevulkan13.cpp:147`](../examples/trianglevulkan13/trianglevulkan13.cpp)

这里的 `getEnabledFeatures()` 会检查：

- 选中的 GPU 是否真的支持 Vulkan 1.3

所以这个 sample 的第一层阅读重点，不是“怎么画三角形”，而是：

- **它首先是一个 Vulkan 1.3 feature sample**

## 第 2 步：先看 `prepare()`

跳到：

- [`trianglevulkan13.cpp:685`](../examples/trianglevulkan13/trianglevulkan13.cpp)

```cpp
void prepare() override
{
    VulkanExampleBase::prepare();
    createSynchronizationPrimitives();
    createCommandBuffers();
    createVertexBuffer();
    createUniformBuffers();
    createDescriptors();
    createPipeline();
    prepared = true;
}
```

你可以把这段当成目录页。

和 `triangle.cpp` 的 `prepare()` 对比，你会发现两个显著变化：

### 1. descriptor 逻辑被合并了

`triangle.cpp` 里是：

- `createDescriptorSetLayout()`
- `createDescriptorPool()`
- `createDescriptorSets()`

这里变成了：

- `createDescriptors()`

也就是说，这个 sample 结构上更紧凑一点。

### 2. pipeline 函数也更简化

`triangle.cpp` 里叫 `createPipelines()`

这里叫：

- `createPipeline()`

背后的语义也更符合这个 sample 的规模：它只有一条 graphics pipeline。

## 第 3 步：回头看类成员

从：

- [`trianglevulkan13.cpp:34`](../examples/trianglevulkan13/trianglevulkan13.cpp)

开始看类成员。

### 1. 顶点和资源持有结构更紧凑

这里有一个小变化：

```cpp
struct VulkanBuffer {
    VkDeviceMemory memory;
    VkBuffer handle;
};
```

然后：

- `vertexBuffer`
- `indexBuffer`
- `UniformBuffer : VulkanBuffer`

相比 `triangle.cpp`，这里对 buffer 的组织更统一一些。

### 2. 仍然是每帧一份 UBO

你会看到：

- [`trianglevulkan13.cpp:61`](../examples/trianglevulkan13/trianglevulkan13.cpp)

```cpp
std::array<UniformBuffer, MAX_CONCURRENT_FRAMES> uniformBuffers;
```

这和 `triangle.cpp` 的思路完全一致：

- 每个 in-flight frame 有自己的一份 uniform buffer

所以这份 sample 虽然更现代，但并没有改变“每帧资源轮转”这个核心思路。

### 3. 仍然自己维护一套 command buffer / sync

和 `triangle.cpp` 一样，它也没有完全使用基类那套 `drawCmdBuffers/currentBuffer` 路线，而是自己维护：

- `commandPool`
- `commandBuffers`
- `presentCompleteSemaphores`
- `renderCompleteSemaphores`
- `waitFences`
- `currentFrame`

所以它仍然是“教学型 sample”，不是“最大化复用基类”的 sample。

## 第 4 步：按 `prepare()` 顺序看资源创建函数

## A. `createSynchronizationPrimitives()`

位置：

- [`trianglevulkan13.cpp:174`](../examples/trianglevulkan13/trianglevulkan13.cpp)

这一段和 `triangle.cpp` 非常像。

你阅读时抓这三个角色就够了：

1. `Fence`：保护每帧 command buffer 的重录
2. `presentCompleteSemaphore`：等 swapchain image 可用
3. `renderCompleteSemaphore`：等渲染完成再 present

这里说明：

- 虽然 sample 升级到了 Vulkan 1.3
- 但 acquire / submit / present 的基本同步模型并没有消失

## B. `createCommandBuffers()`

位置：

- [`trianglevulkan13.cpp:201`](../examples/trianglevulkan13/trianglevulkan13.cpp)

这一段也和 `triangle.cpp` 很像：

1. 建 command pool
2. 从 pool 里分配每帧 command buffer

所以你可以得到一个重要结论：

- dynamic rendering 改的是“rendering section 的组织方式”
- 不是“命令缓冲模型”

## C. `createVertexBuffer()`

位置：

- [`trianglevulkan13.cpp:215`](../examples/trianglevulkan13/trianglevulkan13.cpp)

这部分依然是典型 staging buffer 上传路径：

```text
CPU vertex/index data
  -> host visible staging buffer
  -> device local vertex/index buffer
  -> vkCmdCopyBuffer
  -> 渲染时绑定
```

和 `triangle.cpp` 相比，这里有两点值得注意：

### 1. staging buffer 被合并成一个

这里不是单独一个顶点 staging buffer 加一个索引 staging buffer，而是：

- 一个 staging buffer
- 前半段放 vertices
- 后半段放 indices

所以后面 copy index 时，要显式设置：

- `copyRegion.srcOffset = vertexBufferSize`

这是一个很好的小技巧，值得记住。

### 2. 资源流没有因为 Vulkan 1.3 而改变

也就是说：

- dynamic rendering 不会改变 buffer 上传的根本模式

所以你以后学 Vulkan 时要建立一个意识：

- 有些变化属于“绘制流程层”
- 有些事情属于“资源管理层”

这两层不要混起来。

## D. `createDescriptors()`

位置：

- [`trianglevulkan13.cpp:337`](../examples/trianglevulkan13/trianglevulkan13.cpp)

这一段把三件事合并在一起：

1. 建 descriptor pool
2. 建 descriptor set layout
3. 分配并更新 descriptor set

你可以把它理解成：

- 这份 sample 想更紧凑地表达“shader 接口 + 资源绑定”

读这一段时重点看这条链：

```text
uniformBuffers[i].handle
  -> VkDescriptorBufferInfo
  -> VkWriteDescriptorSet
  -> uniformBuffers[i].descriptorSet
```

虽然函数名变了，但本质和 `triangle.cpp` 是一样的。

## E. `setupDepthStencil()`

位置：

- [`trianglevulkan13.cpp:405`](../examples/trianglevulkan13/trianglevulkan13.cpp)

这个函数和 `triangle.cpp` 也很像：

1. 创建 depth image
2. 分配并绑定 memory
3. 创建 depth image view

这里有一个很重要的认知：

- dynamic rendering 不等于“不要 depth image”
- 它只是“不再需要传统 render pass / framebuffer 描述 attachment”

attachment 资源本身还是要你自己创建。

## F. `loadSPIRVShader()`

位置：

- [`trianglevulkan13.cpp:452`](../examples/trianglevulkan13/trianglevulkan13.cpp)

这一段和 `triangle.cpp` 基本一致。

这里你应该注意一个小细节：

- 它加载的是 `triangle/triangle.vert.spv`
- 不是 `trianglevulkan13/triangle.vert.spv`

这再次说明：

- sample 的重点在渲染路径变化
- shader 逻辑本身没有变化

## G. `createPipeline()`

位置：

- [`trianglevulkan13.cpp:498`](../examples/trianglevulkan13/trianglevulkan13.cpp)

这是整份 sample 里最值得和 `triangle.cpp` 对照阅读的函数之一。

前半段很多东西你会很熟悉：

1. `pipelineLayout`
2. input assembly
3. rasterization
4. blend
5. viewport/scissor
6. dynamic state
7. depth stencil
8. multisample
9. vertex input
10. shader stages

真正的关键差别在这里：

```cpp
VkPipelineRenderingCreateInfoKHR pipelineRenderingCI{ ... };
pipelineCI.pNext = &pipelineRenderingCI;
```

你要抓住这一点：

- 传统 pipeline 创建时，会写 `pipelineCI.renderPass = renderPass`
- 这里没有传统 render pass，而是通过 `VkPipelineRenderingCreateInfo` 告诉 pipeline：
  - color attachment format
  - depth attachment format
  - stencil attachment format

这就是 dynamic rendering 的核心差异之一。

所以这部分阅读时，你要特别问自己：

1. 如果没有 `renderPass`，pipeline 怎么知道 attachment 格式？
2. 为什么这里要单独提供 `VkPipelineRenderingCreateInfo`？

## H. `createUniformBuffers()`

位置：

- [`trianglevulkan13.cpp:653`](../examples/trianglevulkan13/trianglevulkan13.cpp)

这部分也和 `triangle.cpp` 很接近：

1. 创建 uniform buffer
2. 申请 host visible + host coherent memory
3. bind
4. map 一次，后面每帧 `memcpy`

这段说明：

- Vulkan 1.3 没有改变“每帧小块 uniform 数据更新”的基本模式

## 第 5 步：最后精读 `render()`

位置：

- [`trianglevulkan13.cpp:697`](../examples/trianglevulkan13/trianglevulkan13.cpp)

这部分是整份 sample 最重要的地方。

先给你一条高度简化的一帧流程：

```text
wait fence
-> acquire swapchain image
-> update current frame UBO
-> reset command buffer
-> begin command buffer
-> 显式 barrier: 准备 color/depth attachment
-> 配置 VkRenderingAttachmentInfo / VkRenderingInfo
-> vkCmdBeginRendering()
-> set viewport/scissor
-> bind descriptor
-> bind pipeline
-> bind vertex/index buffer
-> draw indexed
-> vkCmdEndRendering()
-> barrier: color image 转到 present
-> end command buffer
-> queue submit
-> queue present
```

现在把它拆开看。

### A. 前半段和 `triangle.cpp` 很像

这几步几乎一样：

1. 等 fence
2. acquire swapchain image
3. 更新 UBO
4. reset / begin command buffer

所以你可以先把“传统 sample 和 1.3 sample 的共同骨架”看出来。

### B. 关键变化 1：显式 image barrier

这里是最重要的差异点之一。

你会看到：

- 对 swapchain color image 插 barrier
- 对 depth image 插 barrier

这是因为：

- 在传统 render pass 模型里，一些 layout transition 可以通过 attachment description + subpass dependency 隐式完成
- 在 dynamic rendering 路线里，这些 transition 更常需要你自己显式写出来

所以这一段是理解 dynamic rendering 的核心。

### C. 关键变化 2：`VkRenderingAttachmentInfo`

你会看到：

- `colorAttachment`
- `depthStencilAttachment`

它们描述的是：

1. 这次渲染段要用哪个 image view
2. 用什么 layout
3. load/store 行为是什么
4. clear value 是什么

这可以理解成：

- 以前这些信息主要写在 `VkRenderPass + VkFramebuffer` 那一套对象里
- 现在变成“录命令时直接描述”

### D. 关键变化 3：`vkCmdBeginRendering()`

这是 dynamic rendering 的核心命令：

```cpp
vkCmdBeginRendering(commandBuffer, &renderingInfo);
...
vkCmdEndRendering(commandBuffer);
```

你可以把它先直观理解为：

- 这是“不依赖传统 render pass 对象”的渲染段开始/结束

### E. 关键变化 4：渲染结束后显式转去 present

在 `vkCmdEndRendering()` 后，sample 又显式插了一次 barrier，把 color image 转成：

- `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`

这是和 `triangle.cpp` 里“render pass 结束后隐式转换到 present layout”最直接的区别。

这一点非常值得你重点记住。

## 第 6 步：为什么 `setupFrameBuffer()` 和 `setupRenderPass()` 是空的

跳到：

- [`trianglevulkan13.cpp:823`](../examples/trianglevulkan13/trianglevulkan13.cpp)

你会看到：

```cpp
void setupFrameBuffer() override {}
void setupRenderPass() override {}
```

这两行非常有信息量。

它在告诉你：

- 基类 `prepare()` 原本会尝试创建默认 framebuffer 和 render pass
- 但这个 sample 不需要传统这套对象
- 所以把它们 override 成空实现，阻止基类生成那套内容

这里其实是本 sample 最漂亮的一笔，因为它非常清楚地表达了：

- **dynamic rendering 的 sample，不要基类帮我创建传统 render pass / framebuffer**

## 第 7 步：你应该怎么拿它和 `triangle.cpp` 对照

建议你把两个 sample 并排看，只盯这几个问题：

### 1. 哪些东西没有变

比如：

- 顶点/索引上传
- UBO
- descriptor
- pipeline 大部分固定功能状态
- acquire / submit / present

### 2. 哪些东西变了

比如：

- 设备 feature 开启方式
- 不再使用传统 render pass
- 不再使用 framebuffer
- pipeline 通过 `VkPipelineRenderingCreateInfo` 指定 attachment format
- 渲染时用 `vkCmdBeginRendering()`
- layout transition 更显式

### 3. 哪些变化是“现代化 API 组织”，哪些不是“算法变化”

这个很重要，因为你以后做图形实验时要分得清：

- 这是 API 组织差异
- 还是算法本身差异

对 `triangle` 和 `triangle13` 来说：

- 算法没变
- 主要是 API 组织方式变了

## 第一次阅读时可以先忽略什么

第一次不用深究下面这些东西：

1. `VkPipelineRenderingCreateInfoKHR` 里每个字段的完整扩展历史
2. `synchronization2` 在这份 sample 中为什么开了但没有大规模用到 `vkQueueSubmit2`
3. 平台相关的 `main` 入口
4. 每个 create info 结构体所有 flag 的全部含义

第一次你应该重点抓的是：

1. dynamic rendering 到底替代了什么
2. pipeline 在没有 render pass 的情况下怎么创建
3. attachment layout 为什么更需要显式 barrier

## 第二遍阅读时建议问自己的问题

1. `trianglevulkan13.cpp` 为什么还保留 depth image？
2. 如果没有 `VkRenderPass`，pipeline 怎么知道 attachment 格式？
3. 为什么这里需要在命令缓冲里显式插入 image barrier？
4. 为什么 `setupFrameBuffer()` 和 `setupRenderPass()` 可以是空实现？
5. 这个 sample 哪些部分和 `triangle.cpp` 完全一样？
6. 如果我以后写自己的实验框架，是不是应该更偏向这个 sample 的方向？

## 我对你阅读这份代码的建议

如果你的目标是后面做自己的图形实验，我建议这样理解这两个 sample：

### `triangle.cpp`

适合学：

- 最传统、最展开的 Vulkan 图形提交流程

### `trianglevulkan13.cpp`

适合学：

- 在现代 Vulkan 下，如何减少 render pass / framebuffer 这一层样板
- 如何显式管理 attachment 和 layout transition

所以它们最好的关系不是“二选一”，而是：

- `triangle.cpp` 帮你建立基础概念
- `trianglevulkan13.cpp` 帮你建立更适合长期框架的现代视角

如果你以后真的打算做 `games202lab`，那从长期维护的角度看：

- `triangle.cpp` 更适合当“教材”
- `trianglevulkan13.cpp` 更适合当“思路参考”
