# Triangle.cpp 阅读导读

这份文档的目标不是把 [`triangle.cpp`](../examples/triangle/triangle.cpp) 每一行都翻译一遍，而是带着你建立一条“阅读路线”。

如果你第一次读 Vulkan sample，最容易发生的事情是：

1. 被大量 `Vk*CreateInfo` 和样板代码淹没。
2. 看到了很多对象，但不知道它们在一帧里是怎么串起来的。
3. 不知道哪些部分是“这个 sample 自己做的”，哪些是“基类已经帮你做了”。

`triangle.cpp` 的价值非常高，因为它把很多底层步骤都摊开了。但也正因为如此，第一次不要试图顺着从第 1 行死磕到最后一行。

## 建议同时打开的文件

阅读这个 sample 时，建议同时开这几个文件：

- [`triangle.cpp`](../examples/triangle/triangle.cpp)
- [`triangle.vert`](../shaders/glsl/triangle/triangle.vert)
- [`triangle.frag`](../shaders/glsl/triangle/triangle.frag)
- [`vulkanexamplebase.cpp`](../base/vulkanexamplebase.cpp)
- [`vulkanexamplebase.h`](../base/vulkanexamplebase.h)

重点对应位置：

- `triangle.cpp:35` `class VulkanExample`
- `triangle.cpp:894` `prepare()`
- `triangle.cpp:908` `render()`
- `vulkanexamplebase.cpp:997` `initVulkan()`
- `vulkanexamplebase.cpp:221` `VulkanExampleBase::prepare()`
- `vulkanexamplebase.cpp:307` `VulkanExampleBase::renderLoop()`

## 先记住总流程

先不要管细节，先记住程序是这样跑起来的：

```text
main / WinMain
  -> VulkanExample()
  -> initVulkan()                  // 基类
  -> setupWindow()                 // 平台相关
  -> prepare()                     // 派生类 + 基类
  -> renderLoop()                  // 基类
  -> render()                      // 每帧调用，派生类
```

如果只记一件事，那就是：

- 基类负责把“Vulkan 程序能跑起来”这件事搭好。
- `triangle` 负责把“这一帧怎么画一个三角形”讲清楚。

## 最推荐的阅读顺序

不要按文件顺序读。建议按下面顺序：

1. 先看 shader，确认 GPU 到底需要什么输入。
2. 再看 `prepare()`，把资源创建顺序记下来。
3. 再回去看类成员，理解每个成员在准备什么。
4. 再按 `prepare()` 里的调用顺序跳读各个创建函数。
5. 最后精读 `render()`，把一帧提交流程串起来。
6. 如果还有余力，再看最底部平台相关入口和基类实现。

下面按这个顺序展开。

## 第 1 步：先看 shader

先看 [`triangle.vert`](../shaders/glsl/triangle/triangle.vert)：

- `line 3`: `location = 0` 输入位置
- `line 4`: `location = 1` 输入颜色
- `line 6`: `binding = 0` 的 UBO
- `line 24`: `gl_Position = projection * view * model * position`

再看 [`triangle.frag`](../shaders/glsl/triangle/triangle.frag)：

- `line 3`: 接收顶点颜色
- `line 5`: 输出最终颜色
- `line 9`: 直接把颜色写出去

看完 shader 后，你应该先在脑子里得到这几个结论：

1. 顶点格式至少要有 `position` 和 `color`。
2. CPU 必须给 vertex shader 提供一个 UBO，里面有三套矩阵。
3. descriptor set layout 至少要有一个 binding 0 的 uniform buffer。
4. pipeline 的 vertex input 描述一定要和 shader 的 `location = 0/1` 对齐。

这一步非常重要，因为它能帮你避免“先看 CPU 代码，结果不知道它为什么这样组织数据”。

## 第 2 步：先看 `prepare()`

直接跳到 [`triangle.cpp:894`](../examples/triangle/triangle.cpp)：

```cpp
void prepare() override
{
    VulkanExampleBase::prepare();
    createSynchronizationPrimitives();
    createCommandBuffers();
    createVertexBuffer();
    createUniformBuffers();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets();
    createPipelines();
    prepared = true;
}
```

这一小段代码是整份 sample 的“目录页”。

你应该先理解两件事：

### 1. `triangle` 不是从零开始

它先调用了 `VulkanExampleBase::prepare()`，这意味着很多通用资源已经由基类创建了。

基类的 `prepare()` 在 [`vulkanexamplebase.cpp:221`](../base/vulkanexamplebase.cpp) 里，主要会做：

1. `createSurface()`
2. `createCommandPool()`
3. `createSwapChain()`
4. `createCommandBuffers()`
5. `createSynchronizationPrimitives()`
6. `setupDepthStencil()`
7. `setupRenderPass()`
8. `createPipelineCache()`
9. `setupFrameBuffer()`

注意这里有一个非常关键的点：

- `triangle` 自己重写了 `setupDepthStencil()`、`setupFrameBuffer()`、`setupRenderPass()`
- 所以虽然是基类在 `prepare()` 中调用这些函数，但实际执行的是 `triangle` 的版本

这就是 C++ 虚函数在这里的作用。

### 2. `triangle` 故意把很多事情又自己写了一套

比如：

- 它自己又建了同步对象
- 自己又建了 command pool / command buffers
- 自己手动写了 acquire / submit / present

这不是“写重复了”，而是为了教学，把底层流程完整展开。

## 第 3 步：回头看类成员

现在回到文件顶部，从 [`triangle.cpp:35`](../examples/triangle/triangle.cpp) 开始看类成员。

### 1. 顶点与 shader 数据

重点看：

- `line 39` `struct Vertex`
- `line 81` `struct ShaderData`

这里你要对照 shader 看：

- `Vertex.position` 对应 vertex shader 的 `inPos`
- `Vertex.color` 对应 vertex shader 的 `inColor`
- `ShaderData` 对应 UBO 里的三套矩阵

这是 CPU 数据布局和 GPU shader 接口第一次对齐的地方。

### 2. 资源所有权

再看这些成员：

- 顶点缓冲 `vertices`
- 索引缓冲 `indices`
- 每帧一个 `uniformBuffers`
- `pipelineLayout`
- `pipeline`
- `descriptorSetLayout`

这里要建立一个意识：

- `triangle` 这个类本身就是资源拥有者
- 构造阶段只做少量逻辑初始化
- 真正的 Vulkan 资源在 `prepare()` 期间创建
- 析构函数统一销毁

### 3. 同步与每帧资源

重点看：

- `MAX_CONCURRENT_FRAMES = 2`
- `presentCompleteSemaphores`
- `renderCompleteSemaphores`
- `waitFences`
- `currentFrame`

你需要先接受一个概念：

- 这份 sample 不是“只有一套帧资源”
- 它有 `in-flight frames`
- 每一帧会轮转使用自己的 fence、command buffer、uniform buffer

这也是为什么 `uniformBuffers` 是数组，而不是单个对象。

## 第 4 步：看构造函数和析构函数

### 构造函数 `triangle.cpp:116`

构造函数里最重要的不是 Vulkan API，而是 sample 的默认行为：

1. 设置窗口标题
2. 关闭 UI overlay
3. 配好一个默认相机

你可以把这里看成“示例运行时的初始状态配置”。

### 析构函数 `triangle.cpp:129`

析构函数值得认真看一次，因为它能帮助你理清资源所有权。

你可以边读边问自己：

1. 哪些资源是 `triangle` 自己创建的？
2. 哪些资源由基类销毁？
3. 为什么这里要先销毁 pipeline / descriptor / buffer，再销毁同步对象？

这一步的意义是建立“谁创建，谁销毁”的直觉。

## 第 5 步：按 `prepare()` 顺序跳读资源创建函数

现在开始按调用顺序读，不要跳。

### A. `createSynchronizationPrimitives()` `triangle.cpp:180`

这里主要解决的问题是：

- CPU 什么时候可以重用一帧的 command buffer？
- GPU 什么时候可以开始渲染？
- 渲染完成后什么时候可以 present？

第一次阅读时只抓住这三样：

1. `Fence`: CPU 等 GPU
2. `presentCompleteSemaphore`: 等 swapchain image 可用
3. `renderCompleteSemaphore`: 等渲染完成再 present

注意这里一个容易混淆的点：

- `Fence` 按 `currentFrame` 轮转
- `renderCompleteSemaphore` 按 `swapChain imageIndex` 使用

这件事你在 `render()` 里会再次看到。

### B. `createCommandBuffers()` `triangle.cpp:207`

这里的重点不是命令池创建信息本身，而是：

1. `triangle` 用自己的 command pool
2. 它自己管理每帧 command buffer
3. 之后的 `render()` 会每帧重录 command buffer

第一次读到这里时，记一句话就够：

- Vulkan 不是直接“调用 draw 就立刻画”
- Vulkan 是“先录命令，再提交命令”

### C. `createVertexBuffer()` `triangle.cpp:223`

这是整份 sample 里最值得精读的函数之一。

如果你只能挑一个函数认真啃，我最推荐这个。

它展示了最经典的 Vulkan 上传路径：

```text
CPU 数组
  -> staging buffer
  -> device local buffer
  -> vkCmdCopyBuffer
  -> 渲染时绑定
```

你读这一段时，重点看下面几个阶段：

1. 先在 CPU 侧定义顶点和索引数组
2. 创建 host visible staging buffer
3. `vkMapMemory + memcpy`
4. 创建真正的 GPU local vertex/index buffer
5. 用一个一次性 command buffer 进行 copy
6. 等 copy 完成后销毁 staging buffer

这一段是未来你上传 mesh、instance buffer、很多纹理数据时都会不断复用的模式。

第一次读时不要过度纠结每个 `VkBufferCreateInfo` 字段，先抓下面三个问题：

1. 为什么要有 staging buffer？
2. 为什么最终渲染用的是 device local buffer？
3. 为什么 copy 也要通过 command buffer 提交给 queue？

### D. `createUniformBuffers()` `triangle.cpp:854`

这个函数相对简单，但非常重要。

你要关注这几件事：

1. 每个并发帧都有自己的 UBO
2. UBO 使用 host visible + host coherent 内存
3. 创建后就直接 `map`，后面每帧只做 `memcpy`

这就是典型的“每帧更新的 CPU -> GPU 小数据”路径。

### E. `createDescriptorSetLayout()` `triangle.cpp:407`

这里你要始终对照 shader 看：

- shader 里 `binding = 0` 是一个 uniform block
- 所以 descriptor set layout 这里就只有一个 binding
- 类型是 `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`
- stage 是 `VK_SHADER_STAGE_VERTEX_BIT`

如果你读 descriptor 常常觉得抽象，可以把它简单理解成：

- shader 先声明“我要什么资源”
- layout 负责定义这份接口

### F. `createDescriptorPool()` `triangle.cpp:378`

descriptor pool 第一次读容易觉得很无聊，但这里其实是在回答：

- 这次 sample 最多会分配多少个 descriptor？
- 它们是什么类型？

因为每帧一个 UBO，所以这里按 `MAX_CONCURRENT_FRAMES` 分配是合理的。

### G. `createDescriptorSets()` `triangle.cpp:426`

这里是在做“把具体资源接到 shader 接口上”。

你要看清楚这条链：

```text
uniformBuffers[i].buffer
  -> VkDescriptorBufferInfo
  -> VkWriteDescriptorSet
  -> uniformBuffers[i].descriptorSet
```

这一步之后，shader 的 `binding = 0` 才真的指向了某个 buffer。

### H. `setupDepthStencil()` `triangle.cpp:460`

这是第一次明确创建 depth image 的地方。

这个函数建议你带着“image 和 buffer 有什么不同”去看：

1. image 创建
2. image memory 分配与绑定
3. image view 创建

第一次阅读时，重点理解：

- image 不是直接拿来用的
- 访问 image 时通常通过 view

### I. `setupFrameBuffer()` `triangle.cpp:510`

这里做的是：

- 每个 swapchain image 对应一个 framebuffer
- framebuffer 把 color attachment 和 depth attachment 绑在一起

重点结论：

- swapchain image view 是每帧不同的
- depth view 在这个 sample 中是共用的

### J. `setupRenderPass()` `triangle.cpp:541`

这是另一个非常值得认真读一次的函数。

它定义的是：

1. 这次渲染会用哪些 attachment
2. 开始时要不要清
3. 结束后要不要保留
4. attachment 在 render pass 前后处于什么 layout
5. subpass 和 dependency 如何描述同步/布局转换

第一次读时，重点抓住这些核心点：

1. color attachment 最终要去 `PRESENT_SRC_KHR`
2. depth attachment 最终去 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`
3. render pass 结束后，color attachment 会隐式转换到可 present 的 layout

如果你以后学 deferred、shadow map、多 pass，这一段会反复出现。

### K. `loadSPIRVShader()` `triangle.cpp:630`

这一小段的意义是提醒你：

- 运行时加载的是 `.spv`
- 不是直接读 `.vert` / `.frag`

如果你改了 shader 源码，记得重编 SPIR-V。

### L. `createPipelines()` `triangle.cpp:681`

这是 sample 中最密集的一段 Vulkan 状态描述。

第一次读它时，不要逐字段精读，建议按“模块”看：

1. `pipelineLayout`
2. input assembly
3. rasterization
4. color blend
5. viewport / scissor
6. dynamic state
7. depth stencil
8. multisampling
9. vertex input
10. shader stages

这里最关键的是把 shader 接口和 pipeline 状态连起来：

- `Vertex` 里的 `position/color`
- shader 里的 `location = 0/1`
- `VkVertexInputAttributeDescription`

这三者一定要完全对齐。

第一次读到这段时，你只需要确认：

1. 顶点怎么解释
2. shader 从哪里加载
3. 这个 pipeline 附着到哪个 render pass
4. viewport / scissor 是动态设置的

## 第 6 步：最后精读 `render()`

跳到 [`triangle.cpp:908`](../examples/triangle/triangle.cpp)。

这是整份 sample 的“每帧时间线”。

建议你第一次就把它抄成下面这条流程图：

```text
wait fence
-> acquire next image
-> update uniform buffer
-> reset command buffer
-> begin command buffer
-> begin render pass
-> set viewport/scissor
-> bind descriptor set
-> bind pipeline
-> bind vertex buffer
-> bind index buffer
-> draw indexed
-> end render pass
-> end command buffer
-> queue submit
-> queue present
-> currentFrame = (currentFrame + 1) % 2
```

然后按下面几个阶段看。

### A. 等待和获取 swapchain image

开头这几步是在做：

1. 等上一轮 `currentFrame` 的 GPU 工作结束
2. 重置 fence
3. 获取下一张可渲染的 swapchain image

这是 CPU/GPU 和 swapchain 对接的入口。

### B. 更新当前帧 UBO

这里把：

- `camera.matrices.perspective`
- `camera.matrices.view`
- `modelMatrix`

打包进 `ShaderData`，然后 `memcpy` 到当前帧的 uniform buffer。

这一步就是“CPU 每帧把本帧参数喂给 shader”。

### C. 录制 command buffer

这一段是最核心的命令录制流程：

1. reset command buffer
2. begin command buffer
3. 配 clear values
4. begin render pass
5. set viewport/scissor
6. bind descriptor
7. bind pipeline
8. bind vertex/index buffer
9. `vkCmdDrawIndexed`
10. end render pass
11. end command buffer

第一次读时，一定要把“状态设置”和“资源绑定”分开：

- `vkCmdSetViewport` / `vkCmdSetScissor` 是动态状态
- `vkCmdBindDescriptorSets` 是 shader 资源绑定
- `vkCmdBindPipeline` 是固定功能和 shader 的整体绑定
- `vkCmdBindVertexBuffers` / `vkCmdBindIndexBuffer` 是几何输入绑定

### D. 提交和 present

命令录制完后，下一步不是“自动显示”，而是要显式提交给 queue：

1. submit 到 graphics queue
2. 用 semaphore 串起等待和完成关系
3. 调用 `vkQueuePresentKHR`

这一步能帮助你真正理解：

- Vulkan 的绘制不是即时模式
- 命令提交和显示是两步

## 第 7 步：最底部入口只看你平台相关部分

`triangle.cpp` 最后有很多平台入口：

- Windows `WinMain`
- Android `android_main`
- Linux / Wayland / DirectFB / macOS 等

第一次阅读时，你只需要看你当前平台对应的入口。

如果你在 Windows 上，重点看：

- [`triangle.cpp:1064`](../examples/triangle/triangle.cpp)

这里本质上只是：

1. 创建 `VulkanExample`
2. `initVulkan()`
3. `setupWindow()`
4. `prepare()`
5. `renderLoop()`

不要一开始把精力浪费在跨平台入口代码上。

## 第一次阅读时可以先忽略什么

第一次不要过度纠结下面这些部分：

1. 每个 `Vk*CreateInfo` 结构体里所有字段
2. 多平台 `main` 入口
3. 很细的错误处理分支
4. 某些格式、flag 的所有可选值

第一次你应该优先抓的是：

1. 对象之间的关系
2. prepare 阶段创建了什么
3. render 阶段一帧怎么走
4. CPU 数据怎么进入 shader

## 第二遍阅读时带着这些问题

建议第二遍边看边回答下面这些问题：

1. `Vertex` 为什么必须和 `triangle.vert` 的输入一一对应？
2. 为什么顶点/索引数据不直接放在 host visible 内存里渲染？
3. 为什么 uniform buffer 要做成 `MAX_CONCURRENT_FRAMES` 份？
4. 为什么 `renderCompleteSemaphore` 用 `imageIndex`，而 `waitFences` 用 `currentFrame`？
5. 为什么 `setupRenderPass()` 里要声明 attachment 的 final layout？
6. 为什么 command buffer 每帧都要重新录制？
7. `triangle` 为什么要自己写 acquire / submit / present，而不直接复用基类包装？

如果这些问题你都能答出来，这个 sample 你就不是“看过”，而是真的“读懂了”。

## 和后续学习的连接

读完 `triangle.cpp` 后，下一步最适合接着看的不是随机挑一个高级 sample，而是带着目的继续：

1. `trianglevulkan13`
   目的：看看 Vulkan 1.3 / dynamic rendering 下，这个最小 sample 会怎么变。
2. `descriptorsets`
   目的：从“一个 UBO”扩展到“多个对象、多个 descriptor set”。
3. `texture`
   目的：把“上传 buffer”扩展到“上传 image + sampler”。
4. `offscreen`
   目的：把“默认 framebuffer”扩展到“离屏 pass”。

如果你的目标是以后写 `games202lab`，那么 `triangle` 最重要的价值就是帮你搞清楚这件事：

- Vulkan 里的每一帧，到底是谁在什么时候创建、更新、绑定、提交了哪些资源。

理解了这一点，后面加阴影、延迟渲染、SSAO、PBR，本质上都只是把这条最小链路扩展成更复杂的版本。
