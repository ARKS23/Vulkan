# Vulkan 学习与实验仓库

这个仓库适合拿来做三件事：

1. 学 Vulkan 的核心对象和渲染流程。
2. 对照多个真实 example，理解常见图形算法在 Vulkan 里的落地方式。
3. 在现有框架上继续写你自己的实验，用作 Games202 或个人图形实验室。

本仓库来自 Sascha Willems 的 Vulkan examples，优点是 example 非常多、覆盖面很广，而且 `triangle` 这种入门例子把很多底层步骤都完整展开了。缺点也很明显：它是一个“教学样例集合”，不是现成的课程框架，所以第一次看时容易陷进大量样板代码里。这个 README 的目标，就是帮你先建立一张“地图”。

## 这个仓库最适合怎么学

建议把它当成“两层结构”来看：

- 第一层是公共框架：负责窗口、实例、设备、交换链、深度缓冲、默认 render pass、UI、输入和主循环。
- 第二层是具体 example：负责自己的顶点数据、uniform、descriptor、pipeline、command buffer 录制和算法逻辑。

如果你的目标是“学明白 Vulkan”，请先认真读一遍 [`examples/triangle/triangle.cpp`](examples/triangle/triangle.cpp)。

如果你的目标是“尽快做自己的图形实验”，不要长期直接改 `triangle`，而是应该在理解它之后，新建一个自己的 example，再从其他更工程化的样例里借代码。

## 快速开始

### 克隆

这个仓库依赖子模块，首次克隆建议：

```bash
git clone --recursive https://github.com/SaschaWillems/Vulkan.git
```

如果你已经克隆过但缺子模块：

```bash
git submodule init
git submodule update
```

### 构建

完整平台说明见 [`BUILD.md`](BUILD.md)。

在 Windows 上，可以先用 CMake 生成工程：

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target triangle
```

如果你想一次性把全部 example 编出来：

```bash
cmake --build build --config Release
```

### 运行

生成的可执行文件通常在 `build/bin/` 或 `build/bin/Release/` 下。

常用运行参数：

```text
--help              查看命令行参数
-v, --validation    开启 validation layers
-gl, --listgpus     列出 Vulkan 设备
-g, --gpu           选择设备
-s, --shaders       选择 shader 语言（glsl / hlsl / slang）
-rp, --resourcepath 指定资源目录
```

第一次建议这样跑：

```bash
triangle.exe -v
```

## Shader 工作流

运行时加载的是已经编译好的 SPIR-V，也就是 `.spv` 文件，不是直接读取 `.vert` / `.frag` 源文件。

相关目录：

- [`shaders/glsl/`](shaders/glsl/)
- [`shaders/hlsl/`](shaders/hlsl/)
- [`shaders/slang/`](shaders/slang/)
- [`shaders/README.md`](shaders/README.md)

仓库里已经提供了编译脚本：

- `shaders/glsl/compileshaders.py`
- `shaders/hlsl/compileshaders.py`
- `shaders/slang/compileshaders.py`

如果你改了 GLSL 源码，记得重新生成对应的 `.spv`。对学习阶段来说，建议先只使用 GLSL 路线。

如果你只想重编某一个 sample 的 GLSL，可以在 `shaders/glsl` 目录下执行：

```bash
python compileshaders.py --sample triangle
```

这个脚本依赖 `glslangValidator` 在 `PATH` 中，或者你手动通过 `--glslang` 指定路径。

## 仓库结构

最值得先认识的是这些目录：

```text
base/                  公共框架层
examples/              每个子目录都是一个独立 example
shaders/               shader 源码和预编译 SPIR-V
assets/                纹理、模型等资源
external/              第三方依赖
cmake/                 CMake 模块
BUILD.md               构建说明
README.md              当前导读
```

进一步拆开看：

- [`base/vulkanexamplebase.h`](base/vulkanexamplebase.h): 框架主类声明，定义了 example 需要重写的接口。
- [`base/vulkanexamplebase.cpp`](base/vulkanexamplebase.cpp): 框架实现，负责初始化和主循环。
- [`base/VulkanDevice.h`](base/VulkanDevice.h): 逻辑设备、队列族、设备能力相关封装。
- [`base/VulkanSwapChain.h`](base/VulkanSwapChain.h): 交换链管理。
- [`examples/CMakeLists.txt`](examples/CMakeLists.txt): 注册所有 example，每个 example 基本对应一个可执行文件。
- [`examples/triangle/triangle.cpp`](examples/triangle/triangle.cpp): 最适合读“底层流程”的入门样例。
- [`shaders/glsl/triangle/`](shaders/glsl/triangle/): `triangle` 的 shader。

## 程序是怎么跑起来的

先看入口链路。以 `triangle` 为例：

```text
main / WinMain
  -> VulkanExample()
  -> initVulkan()
  -> setupWindow()         (不同平台略有差异)
  -> prepare()
  -> renderLoop()
```

对应代码主要在：

- [`examples/triangle/triangle.cpp`](examples/triangle/triangle.cpp)
- [`base/vulkanexamplebase.cpp`](base/vulkanexamplebase.cpp)

### 1. `initVulkan()`: 建立 Vulkan 上下文

`VulkanExampleBase::initVulkan()` 主要完成：

1. 创建 `VkInstance`
2. 开启 validation（如果命令行要求）
3. 枚举物理设备并选择 GPU
4. 查询设备 properties / features / memory properties
5. 调用派生类扩展点，决定启用哪些 feature / extension
6. 创建逻辑设备 `VkDevice`
7. 获取 graphics queue

可以把它理解成：先把“能不能用 Vulkan、用哪块 GPU、准备开哪些功能”这件事搞定。

### 2. `VulkanExampleBase::prepare()`: 建立和窗口有关的通用渲染基础设施

基类 `prepare()` 负责创建绝大多数“每个图形 example 都要有”的公共资源：

1. `createSurface()`
2. `createCommandPool()`
3. `createSwapChain()`
4. `createCommandBuffers()`
5. `createSynchronizationPrimitives()`
6. `setupDepthStencil()`
7. `setupRenderPass()`
8. `createPipelineCache()`
9. `setupFrameBuffer()`
10. UI overlay 初始化

这一步之后，窗口、交换链、默认 framebuffer、深度附件这些“画到屏幕上必须存在的东西”就有了。

### 3. 派生 example 的 `prepare()`: 建立算法自己的资源

`triangle` 在自己的 `prepare()` 里继续做：

1. 创建它自己的一套同步对象
2. 创建它自己的 command pool / command buffers
3. 创建顶点和索引 buffer
4. 创建 uniform buffer
5. 创建 descriptor set layout / pool / sets
6. 创建 graphics pipeline

这里很关键的一点是：

- `triangle` 故意把很多事情手写展开，方便你学习。
- 其他多数 example 更依赖基类的 `prepareFrame()` / `submitFrame()` 和一些辅助函数，代码会更像“工程写法”。

### 4. `renderLoop()`: 进入主循环

基类 `renderLoop()` 会处理平台相关消息循环，并不断调用：

```text
renderLoop()
  -> nextFrame()
     -> render()
```

其中：

- `nextFrame()` 负责计时器、FPS、相机更新等通用事情。
- 真正的一帧渲染，由派生类的 `render()` 来完成。

## `triangle` 的数据流

如果你现在只想抓住 Vulkan 最核心的“资源从哪里来，数据怎么进 GPU，一帧怎么提交”，那就只看 `triangle`。

### A. 顶点/索引数据流

`triangle` 里的三角形顶点和索引先存在 CPU 侧：

```text
CPU 顶点数组 / 索引数组
  -> staging buffer (HOST_VISIBLE)
  -> device local buffer (VERTEX / INDEX BUFFER)
  -> vkCmdCopyBuffer
  -> 渲染时绑定给 pipeline
```

对应函数：

- `createVertexBuffer()`

这个函数非常值得精读，因为它把 Vulkan 里最经典的一套资源上传流程完整演示出来了：

1. 在 CPU 可见内存里创建 staging buffer
2. `vkMapMemory` + `memcpy`
3. 在 device local 内存里创建真正的顶点/索引 buffer
4. 录一个一次性的 copy command buffer
5. 提交 copy
6. 等待完成后销毁 staging buffer

这就是你以后上传 mesh、instance data、甚至很多 texture 数据时反复会用到的模式。

### B. 相机矩阵和 uniform 数据流

`triangle` 的 vertex shader 需要三类矩阵：

- `projectionMatrix`
- `viewMatrix`
- `modelMatrix`

CPU 侧每帧更新它们：

```text
camera / model 变换
  -> ShaderData
  -> 当前帧的 uniform buffer
  -> descriptor set
  -> vertex shader
```

对应代码：

- C++: [`examples/triangle/triangle.cpp`](examples/triangle/triangle.cpp)
- Vertex shader: [`shaders/glsl/triangle/triangle.vert`](shaders/glsl/triangle/triangle.vert)
- Fragment shader: [`shaders/glsl/triangle/triangle.frag`](shaders/glsl/triangle/triangle.frag)

vertex shader 做的事情很直接：

```glsl
gl_Position = projection * view * model * vec4(position, 1.0);
```

同时把顶点颜色直接传给 fragment shader。

### C. 描述符和 pipeline 数据流

这部分可以记成一句话：

```text
shader 先声明自己要什么
-> descriptor set layout 描述接口
-> descriptor set 绑定具体 buffer / image
-> pipeline layout 引用 descriptor set layout
-> graphics pipeline 组合 shader + 固定功能状态
```

在 `triangle` 里：

- descriptor set layout 只有 1 个 binding
- 这个 binding 是 vertex shader 用的 uniform buffer
- pipeline 使用这个 layout
- 绘制前绑定当前帧对应的 descriptor set

对应函数：

- `createDescriptorSetLayout()`
- `createDescriptorPool()`
- `createDescriptorSets()`
- `createPipelines()`

### D. 一帧渲染的数据流

把 `triangle::render()` 简化后，可以记成下面这条链：

```text
wait fence
-> acquire swapchain image
-> 更新当前帧 uniform buffer
-> reset command buffer
-> begin command buffer
-> begin render pass
-> set viewport / scissor
-> bind descriptor set
-> bind pipeline
-> bind vertex/index buffer
-> draw indexed
-> end render pass
-> end command buffer
-> queue submit
-> queue present
-> currentFrame++
```

其中最重要的几个同步点：

- `Fence`: 保证 CPU 不会重用仍在执行中的帧资源。
- `presentCompleteSemaphore`: 保证拿到交换链图像后再开始渲染。
- `renderCompleteSemaphore`: 保证渲染完成后再 present。

这一段理解了，你就已经抓住了 Vulkan 图形渲染最核心的“每帧提交模型”。

## 为什么 `triangle` 很重要，但不适合长期直接改

`triangle` 是一个“把底层细节摊开给你看”的 example，所以它很适合：

- 理解 buffer / memory / render pass / framebuffer / pipeline / sync 这些对象到底是什么。
- 理解一帧命令是怎么录制和提交的。
- 对照 shader 看 CPU 和 GPU 的数据接口如何对齐。

但它不太适合直接作为长期实验底座，原因是：

1. 它有意重复了一些框架中本可复用的逻辑。
2. 它以“教学展开”为目标，不以“后续扩展舒服”为目标。
3. 当你开始做阴影、G-buffer、后处理、多 pass 时，继续在这个文件里堆代码会很快失控。

所以更好的策略是：

1. 先读懂 `triangle`
2. 再对照 `trianglevulkan13` 和其他常规 example
3. 最后新建你自己的 example 作为实验入口

## 第一次阅读的推荐顺序

建议按下面顺序读，而不是一上来在整个仓库里乱跳：

1. [`examples/triangle/triangle.cpp`](examples/triangle/triangle.cpp)
2. [`shaders/glsl/triangle/triangle.vert`](shaders/glsl/triangle/triangle.vert)
3. [`shaders/glsl/triangle/triangle.frag`](shaders/glsl/triangle/triangle.frag)
4. [`base/vulkanexamplebase.h`](base/vulkanexamplebase.h)
5. [`base/vulkanexamplebase.cpp`](base/vulkanexamplebase.cpp)
6. [`base/VulkanDevice.h`](base/VulkanDevice.h) + [`base/VulkanDevice.cpp`](base/VulkanDevice.cpp)
7. [`base/VulkanSwapChain.h`](base/VulkanSwapChain.h) + [`base/VulkanSwapChain.cpp`](base/VulkanSwapChain.cpp)

读的时候建议你回答这些问题：

1. 这个对象是谁创建的，谁销毁的？
2. 这个资源是每帧更新，还是只初始化一次？
3. 这个数据最终是给 CPU 用，还是给 shader 用？
4. 这个同步原语是在防什么问题？
5. 如果我要把“三角形”换成“网格 + 材质 + 多 pass”，这一段还能不能复用？

## 推荐学习路线

如果你的目标是“学 Vulkan + 学图形算法”，我建议按下面顺序推进：

### 第 0 阶段：理解 Vulkan 画一帧

- `triangle`
- `trianglevulkan13`

目标：

- 理解 instance / device / queue / swapchain / framebuffer / render pass / pipeline / descriptor / command buffer / sync
- 比较“经典 render pass 路线”和“Vulkan 1.3 dynamic rendering 路线”

### 第 1 阶段：理解 shader 输入数据怎么组织

- `descriptorsets`
- `pushconstants`
- `dynamicuniformbuffer`
- `vertexattributes`
- `texture`

目标：

- 学会把矩阵、材质参数、纹理等数据喂给 shader
- 理解 descriptor set、push constant、不同 buffer 更新方式的差异

### 第 2 阶段：理解场景和离屏渲染

- `gltfloading`
- `offscreen`
- `inputattachments`
- `subpasses`

目标：

- 学会加载模型
- 学会先渲染到离屏 attachment，再把结果给后续 pass 使用

### 第 3 阶段：理解常见实时图形算法

- `shadowmapping`
- `shadowmappingcascade`
- `deferred`
- `ssao`
- `pbrbasic`
- `pbribl`

目标：

- 开始把课程里常见的图形算法和 Vulkan 资源/同步模型联系起来

### 第 4 阶段：再去看高级或专题功能

- `multisampling`
- `oit`
- `computeshader`
- `meshshader`
- `raytracing*`

这一阶段更适合在你已经能独立写一个多 pass raster pipeline 之后再看。

## 如何写你自己的 Example

建议不要直接改现有 example，而是新建一个自己的目录，例如：

```text
examples/games202lab/
shaders/glsl/games202lab/
```

### 最小步骤

1. 新建 `examples/games202lab/games202lab.cpp`
2. 让它继承 `VulkanExampleBase`
3. 新建 `shaders/glsl/games202lab/`
4. 提供至少一对 shader 和对应 `.spv`
5. 把 `games202lab` 加到 [`examples/CMakeLists.txt`](examples/CMakeLists.txt) 的 `EXAMPLES` 列表
6. 如有模型和纹理，放到 `assets/`

补充一个小约定：`examples/CMakeLists.txt` 默认会把 `examples/<name>/<name>.cpp` 当成入口文件；如果目录里存在 `main.cpp`，也可以改用 `main.cpp` 作为入口。

### 模板怎么选

有两种常见起点：

- 如果你想继续练基本功：从 `triangle` 抄最小版本。
- 如果你想尽快做课程实验：从更接近目标的 example 起步。

我对 Games202 的建议是：

- 入门调通阶段：读 `triangle`
- 自己开新实验时：不要复制完整的 `triangle`
- 更推荐从 `trianglevulkan13`、`descriptorsets`、`texture`、`offscreen` 这些 example 中有选择地拼出自己的底座

### 一个更稳的实验底座应该有什么

当你开始做自己的实验时，建议尽快把代码分成下面几块：

- `FrameData`: 每帧独立资源，例如 command buffer、fence、semaphore、per-frame UBO
- `SceneUBO`: 相机、光源、时间等全局参数
- `Material/Pass` 数据结构: 控制 shader、pipeline 和 descriptor
- `OffscreenPass`: 阴影贴图、G-buffer、后处理输入等
- `FullscreenPass`: 全屏合成、后处理

不用一开始就做很重的架构，但至少要避免把所有逻辑都塞进一个上千行的 `.cpp` 里。

## 面向 Games202 的具体建议

如果你要把这里发展成自己的图形实验室，我建议这样改：

### 1. 先做一个“统一实验底座”

不要每个实验都从零再搭 Vulkan 对象。你可以维护一个自己的 `games202lab` example，把下面这些公共能力固定下来：

- 可自由飞行的相机
- 基本 mesh / glTF 加载
- 一个统一的场景 UBO
- 一套稳定的 descriptor 组织方式
- 一套离屏渲染和屏幕合成流程
- 简单的调试 UI

这样后面写阴影、SSAO、PBR、体积效果时，工作重点才会落在算法本身，而不是重复搭脚手架。

### 2. 读懂 `triangle`，但尽快转向“现代写法”

如果你的显卡和驱动支持 Vulkan 1.3，我建议你在学完 `triangle` 后尽快对照：

- `trianglevulkan13`
- `dynamicrendering`

原因很简单：你的课程实验更可能是“自己长期维护的小框架”，而不是为了复刻早期 Vulkan 样板。现代路径在多 pass 管理上通常更轻一些。

### 3. 课程实验的一个可行推进顺序

可以考虑：

1. `Lab 0`: 三角形 -> 网格 -> 相机控制
2. `Lab 1`: Blinn-Phong / 法线贴图 / 多光源
3. `Lab 2`: Shadow Mapping
4. `Lab 3`: Deferred Shading
5. `Lab 4`: SSAO / SSR / 屏幕后处理
6. `Lab 5`: PBR / IBL

这个顺序和仓库里 example 的组织也比较对得上，迁移成本低。

### 4. 优先复用这些能力

下面这些能力不值得你重复造轮子，优先从仓库里借：

- 相机：`base/camera.hpp`
- glTF 加载：`VulkanglTFModel.*` 和相关 example
- 纹理上传：`VulkanTexture.*`
- 设备封装：`VulkanDevice.*`
- swapchain：`VulkanSwapChain.*`

真正值得你自己设计的是：

- 实验的 pass 结构
- 场景数据布局
- 材质系统的最小接口
- 课程里各算法共用的中间 buffer / attachment

## 我对这个仓库的改写建议

如果你准备长期在这个仓库上做实验，我建议逐步做下面几件事：

1. 保留 upstream example，不要大改原例子。
2. 单独维护一个你自己的 `examples/games202lab/`。
3. 给自己的 example 再拆成几个 `.h/.cpp` 文件，不要把所有逻辑都塞在一个源文件里。
4. 先统一 `FrameData` 和 `SceneUBO` 的组织，再加功能。
5. 多 pass 需求一出现，就把离屏 pass 抽出来，不要继续把 render pass / framebuffer / pipeline 混写在主循环里。
6. 从一开始就给 shader、资源目录、实验说明建立固定约定。

一个很实用的原则是：

先“读例子”，再“抄局部”，最后“做自己的组织”。

不要把这个仓库直接当成最终架构；把它当成一座材料库和参考实现库会更合适。

## 最后一句建议

如果你接下来要真的在这里做 Games202 实验，我最推荐的起点不是“继续改 `triangle`”，而是：

1. 用 `triangle` 学清楚一帧是怎么工作的。
2. 对照 `trianglevulkan13` 看现代写法。
3. 新建 `games202lab`，从 `descriptorsets`、`texture`、`offscreen`、`shadowmapping` 这些 example 里按需吸收代码。

这样你会同时获得两件事：

- 你知道 Vulkan 底层到底发生了什么。
- 你的实验工程不会被教学样板代码拖着走。
